using System.Buffers.Binary;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using KNSoft.ZPigeon.Server.Managed;

namespace KNSoft.ZPigeon.Web;

internal static class ClientBuildWebApi
{
    private const int ConfigurationSize = 4580;
    private const int CertificateCapacity = 4096;
    private const int MaximumExecutableSize = 64 * 1024 * 1024;
    private const int GroupOffset = 32;
    private const int KeyNameOffset = 162;
    private const int CertificateOffset = 484;
    private static ReadOnlySpan<byte> ConfigurationMagic =>
        [0x5A, 0x50, 0x43, 0x46, 0x47, 0x01, 0x71, 0xD8, 0xA2, 0x4C, 0x6B, 0x19, 0xE5, 0x33, 0x8F, 0xC0];

    internal static void MapClientBuildApi(this WebApplication app, NativeServer server)
    {
        var builds = new ClientBuildService();
        app.Lifetime.ApplicationStopping.Register(() => builds.DisposeAsync().AsTask().GetAwaiter().GetResult());
        app.MapGet("/api/clients/build", async (HttpContext context) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            var error = await builds.GetAvailabilityAsync(context.RequestAborted);
            var root = server.ExportDeploymentRootCertificate();
            if (root.Length is 0 or > CertificateCapacity)
                error = "Deployment certificate exceeds the executable configuration capacity.";
            return new
            {
                Available = error is null,
                Error = error,
                Host = "127.0.0.1",
                Port = server.Port,
                ServerName = "localhost",
                Architecture = "x64",
                Configuration = "Release",
                ClientBuildCatalog.Modules,
                ClientBuildCatalog.ExecutionOptions,
                Transports = ClientBuildCatalog.Transports.Where(transport =>
                    NativeServer.AvailableTransports.Contains(transport.Id))
                    .Select(transport => transport with { Port = server.Port + (transport.Id == 3 ? 1 : 0) }).ToArray(),
                DefaultModules = ClientBuildCatalog.Modules.Select(module => module.Id).ToArray(),
                DefaultTransports = new[] { NativeServer.AvailableTransports[0] },
                DefaultExecutionOptions = ClientBuildCatalog.ExecutionOptions.Select(option => option.Id).ToArray(),
                RootCertificateSha256 = Convert.ToHexString(SHA256.HashData(root)),
                DeploymentId = GetDeploymentId(root),
                MaxGroupLength = 64,
                SupportsTcpTls13 = OperatingSystem.IsWindowsVersionAtLeast(10, 0, 20348),
                Instances = GetInstances(server, builds, GetDeploymentId(root)),
                MaximumPendingBuilds = 4
            };
        });
        app.MapPost("/api/clients/build/jobs", (ClientBuildRequest request, HttpContext context) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            var selection = ClientBuildCatalog.Validate(request);
            if (selection.Transports.Any(transport => !NativeServer.AvailableTransports.Contains(transport)) ||
                request.RequireTcpTls13 && !OperatingSystem.IsWindowsVersionAtLeast(10, 0, 20348))
                throw new ArgumentException("The transport or TLS policy is unavailable on this Server.");
            var group = request.Group?.Trim() ?? string.Empty;
            if (!IsValidText(group, 64)) throw new ArgumentException("Invalid group name.");
            string instanceId;
            if (string.IsNullOrEmpty(request.InstanceId))
            {
                instanceId = Guid.NewGuid().ToString("N").ToUpperInvariant();
            }
            else
            {
                if (!Guid.TryParseExact(request.InstanceId, "N", out var instance))
                    throw new ArgumentException("Invalid Client instance identifier.");
                instanceId = instance.ToString("N").ToUpperInvariant();
                if (!GetInstances(server, builds, GetDeploymentId(server.ExportDeploymentRootCertificate()))
                     .Any(value => value.InstanceId == instanceId))
                    throw new ArgumentException("The Client instance is not known to this Server.");
            }
            var root = server.ExportDeploymentRootCertificate();
            if (root.Length is 0 or > CertificateCapacity)
                throw new InvalidOperationException(
                    "Deployment certificate exceeds the executable configuration capacity.");
            var keyName = $"KNSoft.ZPigeon.Client.{GetDeploymentId(root)}.{instanceId}";
            var job = new ClientBuildJob(selection, group, instanceId, keyName, root, request.RequireTcpTls13);
            if (!builds.TryEnqueue(job))
            {
                job.Cancellation.Dispose();
                return Results.Json(new { Message = "The build queue is full." },
                                    statusCode: StatusCodes.Status429TooManyRequests);
            }
            return Results.Accepted($"/api/clients/build/jobs/{job.Id}", job.GetView());
        });
        app.MapGet("/api/clients/build/jobs/{id:guid}", (Guid id, HttpContext context) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            return builds.Get(id).GetView();
        });
        app.MapGet("/api/clients/build/jobs/{id:guid}/events", (Guid id, HttpContext context) =>
            StreamBuildAsync(builds.Get(id), context));
        app.MapPost("/api/clients/build/jobs/{id:guid}/cancel", (Guid id) =>
        {
            return builds.Cancel(id) ? Results.Accepted() :
                Results.Conflict(new { Message = "The build has already ended." });
        });
        app.MapGet("/api/clients/build/jobs/{id:guid}/download", (Guid id, HttpContext context) =>
        {
            var job = builds.Get(id);
            if (job.State != "completed" || job.TemplatePath is null)
                return Results.Conflict(new { Message = "The executable is not ready." });
            var template = ReadTemplate(job.TemplatePath);
            var configuration = template.Image.AsSpan(template.Offset, ConfigurationSize);
            BinaryPrimitives.WriteUInt32LittleEndian(configuration[20..], (uint)job.Certificate.Length);
            uint endpoints = 0;
            for (var index = 0; index < job.Selection.Transports.Length; index++)
                endpoints |= (uint)job.Selection.Transports[index] << (index * 8);
            BinaryPrimitives.WriteUInt32LittleEndian(configuration[24..], endpoints);
            BinaryPrimitives.WriteUInt32LittleEndian(configuration[28..],
                                                     server.Port | (job.RequireTcpTls13 ? 0x10000U : 0));
            configuration.Slice(GroupOffset, 130).Clear();
            configuration.Slice(KeyNameOffset, 258).Clear();
            Encoding.Unicode.GetBytes(job.Group, configuration[GroupOffset..]);
            Encoding.Unicode.GetBytes(job.KeyName, configuration[KeyNameOffset..]);
            configuration[CertificateOffset..].Clear();
            job.Certificate.CopyTo(configuration[CertificateOffset..]);
            context.Response.Headers.CacheControl = "no-store";
            return Results.File(template.Image, "application/octet-stream", ClientBuildService.ExecutableName);
        });
        app.MapGet("/api/clients/build/jobs/{id:guid}/runtime/{name}", (Guid id, string name, HttpContext context) =>
        {
            var job = builds.Get(id);
            if (job.State != "completed" || job.TemplatePath is null ||
                !job.RuntimeFiles.Contains(name, StringComparer.Ordinal)) return Results.NotFound();
            context.Response.Headers.CacheControl = "no-store";
            return Results.File(Path.Combine(Path.GetDirectoryName(job.TemplatePath)!, name),
                                "application/octet-stream", name);
        });
    }

    private static ClientBuildInstance[] GetInstances(
        NativeServer server, ClientBuildService builds, string deploymentId) =>
        server.GetClients().Where(client => !string.IsNullOrEmpty(client.InstanceId))
              .Select(client => new ClientBuildInstance(client.InstanceId, client.Group))
              .Concat(builds.GetInstances(deploymentId))
              .DistinctBy(value => value.InstanceId, StringComparer.OrdinalIgnoreCase)
              .ToArray();

    private static string GetDeploymentId(byte[] root)
    {
        using var certificate = X509CertificateLoader.LoadCertificate(root);
        return Convert.ToHexString(SHA256.HashData(certificate.PublicKey.ExportSubjectPublicKeyInfo()));
    }

    private static async Task StreamBuildAsync(ClientBuildJob job, HttpContext context)
    {
        context.Response.ContentType = "text/event-stream";
        context.Response.Headers.CacheControl = "no-store";
        context.Response.Headers["X-Accel-Buffering"] = "no";
        try
        {
            while (!context.RequestAborted.IsCancellationRequested)
            {
                await Task.Delay(TimeSpan.FromMilliseconds(250), context.RequestAborted);
                var view = job.GetView();
                await context.Response.WriteAsync("event: build\ndata: ", context.RequestAborted);
                await JsonSerializer.SerializeAsync(context.Response.Body, view,
                                                    JsonSerializerOptions.Web, context.RequestAborted);
                await context.Response.WriteAsync("\n\n", context.RequestAborted);
                await context.Response.Body.FlushAsync(context.RequestAborted);
                if (view.State is "completed" or "failed" or "canceled") return;
                using var keepAlive = CancellationTokenSource.CreateLinkedTokenSource(context.RequestAborted);
                keepAlive.CancelAfter(TimeSpan.FromSeconds(15));
                try { await job.WaitAsync(view.Revision, keepAlive.Token); }
                catch (OperationCanceledException) when (!context.RequestAborted.IsCancellationRequested)
                {
                    await context.Response.WriteAsync(": keepalive\n\n", context.RequestAborted);
                }
            }
        }
        catch (OperationCanceledException) when (context.RequestAborted.IsCancellationRequested)
        {
        }
    }

    private static bool IsValidText(string value, int maximumLength)
    {
        if (value.Length > maximumLength || value.Any(char.IsControl)) return false;
        for (var index = 0; index < value.Length; index++)
        {
            if (!char.IsSurrogate(value[index])) continue;
            if (!char.IsHighSurrogate(value[index]) || index + 1 == value.Length ||
                !char.IsLowSurrogate(value[++index])) return false;
        }
        return true;
    }

    internal static (byte[] Image, int Offset) ReadTemplate(string path)
    {
        if (!File.Exists(path)) throw new InvalidOperationException("Client executable template is missing.");
        if (new System.IO.FileInfo(path).Length is 0 or > MaximumExecutableSize)
            throw new InvalidOperationException("Client executable template size is invalid.");
        var image = File.ReadAllBytes(path);
        using var reader = new PEReader(new MemoryStream(image, writable: false));
        var headers = reader.PEHeaders;
        var pe = headers.PEHeader;
        if (headers.CoffHeader.Machine != Machine.Amd64 || pe is null || pe.Magic != PEMagic.PE32Plus ||
            pe.CertificateTableDirectory.Size != 0)
            throw new InvalidOperationException(
                "Client executable template must be an unsigned Windows x64 executable.");
        var sections = headers.SectionHeaders.Where(section => section.Name == ".zpcfg").ToArray();
        if (sections.Length != 1 || sections[0].SizeOfRawData < ConfigurationSize ||
            sections[0].PointerToRawData < 0 || sections[0].PointerToRawData > image.Length - ConfigurationSize)
            throw new InvalidOperationException("Client executable template has no valid configuration section.");
        var offset = sections[0].PointerToRawData;
        var configuration = image.AsSpan(offset, ConfigurationSize);
        if (!configuration[..16].SequenceEqual(ConfigurationMagic) ||
            BinaryPrimitives.ReadUInt32LittleEndian(configuration[16..]) != 1 ||
            BinaryPrimitives.ReadUInt32LittleEndian(configuration[20..]) != 0 ||
            BinaryPrimitives.ReadUInt32LittleEndian(configuration[28..]) != 21943)
            throw new InvalidOperationException("Client executable configuration template is invalid.");
        BinaryPrimitives.WriteUInt32LittleEndian(image.AsSpan(headers.PEHeaderStartOffset + 64), 0);
        return (image, offset);
    }
}

internal sealed record ClientBuildInstance(string InstanceId, string Group);
