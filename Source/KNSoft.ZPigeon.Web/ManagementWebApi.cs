using KNSoft.ZPigeon.Server.Managed;
using Microsoft.Net.Http.Headers;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;

namespace KNSoft.ZPigeon.Web;

internal static class ManagementWebApi
{
    private static readonly object UpdateCheckLock = new();
    private static Task? updateCheck;

    internal static void MapManagementApi(this WebApplication app, NativeServer server)
    {
        app.MapPost("/api/files", async (FilePageRequest request) =>
        {
            var enumerationId = 0UL;
            if (request.EnumerationId is not null &&
                !ulong.TryParse(request.EnumerationId, out enumerationId))
            {
                return Results.BadRequest();
            }
            return Results.Ok(await server.EnumerateFilesPageAsync(request.Path, enumerationId));
        });
        app.MapPost("/api/file/info", async (PathRequest request) =>
            await server.QueryFileAsync(request.Path));
        app.MapPost("/api/file/security", async (PathRequest request) => new
        {
            Sddl = await server.QueryFileSecurityAsync(request.Path)
        });
        app.MapPost("/api/file/security/set", async (SecurityDescriptorRequest request) =>
            await server.SetFileSecurityAsync(request.Path, request.Sddl));
        app.MapPost("/api/security/account", async (SecurityAccountRequest request) => new
        {
            Value = request.Sid ? await server.ResolveAccountSidAsync(request.Value) :
                                  await server.ResolveAccountNameAsync(request.Value)
        });
        app.MapPost("/api/file/hash", async (FileHashRequest request) =>
            await server.HashFileAsync(request.Path, request.Algorithm));
        app.MapPost("/api/file/delete", async (PathRequest request) =>
            await server.DeleteFileAsync(request.Path));
        app.MapPost("/api/file/rename", async (FileRenameRequest request) =>
            await server.RenameFileAsync(request.Path, request.NewPath));
        app.MapPost("/api/file/attributes", async (FileAttributesRequest request) =>
            await server.SetFileAttributesAsync(request.Path, request.Attributes));
        app.MapPost("/api/file/volume", async (PathRequest request) =>
            await server.QueryFileVolumeAsync(request.Path));
        app.MapPost("/api/file/volume/label", async (FileVolumeLabelRequest request) =>
            await server.SetFileVolumeLabelAsync(request.Path, request.Label));
        app.MapPost("/api/file/volume/format", async (FileVolumeFormatRequest request) =>
        {
            if (!System.Text.RegularExpressions.Regex.IsMatch(request.Path, "^[A-Za-z]:\\\\$") ||
                request.Label.Length > 32 ||
                request.Label.IndexOfAny(['\"', '/', '\\']) >= 0 ||
                request.FileSystem is not ("NTFS" or "ReFS" or "exFAT" or "FAT32"))
            {
                return Results.BadRequest();
            }
            var arguments = $"{request.Path[..2]} /FS:{request.FileSystem} /V:\"{request.Label}\" /Y /X" +
                            (request.Quick ? " /Q" : string.Empty);
            return Results.Ok(await server.StartExecutionAsync(new ExecutionStart(
                ExecutionEngine.CreateProcess,
                ExecutionIdentity.Current,
                uint.MaxValue,
                ExecutionFlags.Hidden,
                "format.com",
                arguments,
                null,
                null,
                null,
                null)));
        });
        app.MapPost("/api/file/search", async (FileSearchRequest request) =>
        {
            if (request.Query.Length is 0 or > 260 || request.Query.Contains('\"') ||
                request.Path.Length is 0 or > 32767 || request.Path.Contains('\"') ||
                request.Mode is < 1 or > 3 ||
                (request.Mode != 1 && request.Query.IndexOfAny(['\\', '/', ':']) >= 0))
            {
                return Results.BadRequest();
            }
            string command;
            if (request.Mode == 1)
            {
                command = $"where.exe /R \"{request.Path}\" \"{request.Query}\"";
            }
            else if (request.Mode == 2)
            {
                var scope = request.Path.Replace('\\', '/').Replace("'", "''");
                var query = request.Query.Replace("'", "''");
                var script = "$c=New-Object -ComObject ADODB.Connection;" +
                             "$c.Open(\"Provider=Search.CollatorDSO;Extended Properties='Application=Windows';\");" +
                             "$r=$c.Execute(\"SELECT TOP 5000 System.ItemPathDisplay FROM SYSTEMINDEX " +
                             $"WHERE SCOPE='file:{scope}' AND System.FileName LIKE '%{query}%'\");" +
                             "while(!$r.EOF){[Console]::WriteLine($r.Fields.Item('System.ItemPathDisplay').Value);" +
                             "$r.MoveNext()};$r.Close();$c.Close()";
                command = "powershell.exe -NoLogo -NoProfile -NonInteractive -EncodedCommand " +
                          Convert.ToBase64String(Encoding.Unicode.GetBytes(script));
            }
            else
            {
                var pattern = request.Query.IndexOfAny(['*', '?']) >= 0 ? request.Query : $"*{request.Query}*";
                command = $"cmd.exe /D /Q /C dir /A /S /B \"{Path.Combine(request.Path, pattern)}\"";
            }
            return Results.Ok(await RemoteCommand.RunAsync(server, command, request.Path));
        });
        app.MapGet("/api/file/download", async (HttpContext context, string path) =>
        {
            await using var transfer = await server.OpenFileReadAsync(path);
            context.Response.ContentLength = checked((long)transfer.FileSize);
            context.Response.ContentType = "application/octet-stream";
            context.Response.GetTypedHeaders().ContentDisposition = new ContentDispositionHeaderValue("attachment")
            {
                FileNameStar = System.IO.Path.GetFileName(path)
            };
            await foreach (var data in transfer.Output.ReadAllAsync(context.RequestAborted))
            {
                await context.Response.Body.WriteAsync(data, context.RequestAborted);
            }
            var status = (await transfer.Completion).Status;
            if (!status.IsSuccess)
            {
                throw new NativeException(status);
            }
        });
        app.MapPut("/api/file/upload", async (HttpContext context, string path, bool overwrite) =>
        {
            if (context.Request.ContentLength is not long length || length < 0)
            {
                return Results.StatusCode(StatusCodes.Status411LengthRequired);
            }
            await using var transfer = await server.OpenFileWriteAsync(path, (ulong)length, overwrite);
            var buffer = GC.AllocateUninitializedArray<byte>(0x10000);
            long received = 0;
            int read;
            while ((read = await context.Request.Body.ReadAsync(buffer, context.RequestAborted)) != 0)
            {
                await transfer.WriteAsync(buffer.AsMemory(0, read), context.RequestAborted);
                received += read;
            }
            if (received != length)
            {
                throw new EndOfStreamException();
            }
            var status = (await transfer.Completion).Status;
            if (!status.IsSuccess)
            {
                throw new NativeException(status);
            }
            return Results.NoContent();
        });
        app.MapPost("/api/processes", async () =>
            (await server.EnumerateProcessesAsync()).Select(ProcessWebRecord.From));
        app.MapPost("/api/process/info", async (ProcessIdentityRequest request) =>
            ProcessInfoWebRecord.From(
                await server.QueryProcessAsync(request.ProcessId, ulong.Parse(request.CreateTime))));
        app.MapPost("/api/process/control", async (ProcessControlRequest request) =>
        {
            if (!Enum.IsDefined(request.Control)) return Results.BadRequest();
            await server.ControlProcessAsync(
                request.ProcessId,
                ulong.Parse(request.CreateTime),
                request.Control,
                request.Value);
            return Results.NoContent();
        });
        app.MapPost("/api/process/dump", async (HttpContext context, ProcessDumpRequest request) =>
        {
            var path = await server.CreateProcessDumpAsync(
                request.ProcessId,
                ulong.Parse(request.CreateTime),
                request.DumpType);
            try
            {
                await using var transfer = await server.OpenFileReadAsync(path);
                context.Response.ContentLength = checked((long)transfer.FileSize);
                context.Response.ContentType = "application/octet-stream";
                context.Response.GetTypedHeaders().ContentDisposition = new ContentDispositionHeaderValue("attachment")
                {
                    FileNameStar = System.IO.Path.GetFileName(path)
                };
                await foreach (var data in transfer.Output.ReadAllAsync(context.RequestAborted))
                {
                    await context.Response.Body.WriteAsync(data, context.RequestAborted);
                }
                var status = (await transfer.Completion).Status;
                if (!status.IsSuccess) throw new NativeException(status);
            }
            finally
            {
                await server.DeleteFileAsync(path);
            }
        });
        app.MapPost("/api/windows", async () => await server.EnumerateWindowsAsync());
        app.MapPost("/api/window/info", async (WindowIdentityRequest request) =>
            await server.QueryWindowAsync(ulong.Parse(request.Handle), request.ProcessId, request.ThreadId));
        app.MapPost("/api/window/control", async (WindowControlRequest request) =>
        {
            if (!Enum.IsDefined(request.Control))
            {
                return Results.BadRequest();
            }
            await server.ControlWindowAsync(
                ulong.Parse(request.Handle),
                request.ProcessId,
                request.ThreadId,
                request.Control);
            return Results.NoContent();
        });
        app.MapPost("/api/browsers", async () => await server.EnumerateBrowsersAsync());
        app.MapPost("/api/browser/query", async (BrowserQueryRequest request) =>
        {
            if (!Enum.IsDefined(request.Browser) || !Enum.IsDefined(request.Kind) ||
                request.Kind < BrowserKind.History || request.Profile.Length is 0 or >= 260 ||
                request.Profile.IndexOfAny(['\\', '/', ':']) >= 0 ||
                !ulong.TryParse(request.Cursor, out var cursor))
            {
                return Results.BadRequest();
            }
            return Results.Ok(await server.QueryBrowserAsync(
                request.Browser,
                request.Kind,
                request.Profile,
                cursor));
        });
        app.MapPost("/api/services", async () => await server.EnumerateServicesAsync());
        app.MapPost("/api/service/info", async (ServiceRequest request) =>
            await server.QueryServiceAsync(request.ServiceName));
        app.MapPost("/api/service/control", async (ServiceControlRequest request) =>
        {
            if (!Enum.IsDefined(request.Control))
            {
                return Results.BadRequest();
            }
            await server.ControlServiceAsync(request.ServiceName, request.Control, request.Argument);
            return Results.NoContent();
        });
        app.MapPost("/api/service/configure", async (ServiceConfig request) =>
        {
            await server.ConfigureServiceAsync(request);
            return Results.NoContent();
        });
        app.MapPost("/api/service/configure-recovery", async (ServiceRecoveryConfig request) =>
        {
            await server.ConfigureServiceRecoveryAsync(request);
            return Results.NoContent();
        });
        app.MapPost("/api/service/configure-account", async (ServiceAccountConfig request) =>
        {
            await server.ConfigureServiceAccountAsync(request);
            return Results.NoContent();
        });
        MapAdministration(app, server, "users", AdministrationOperation.EnumerateUsers,
                          AdministrationOperation.ControlUser);
        MapAdministration(app, server, "software", AdministrationOperation.EnumerateSoftware,
                          AdministrationOperation.ControlSoftware);
        MapAdministration(app, server, "hardware", AdministrationOperation.EnumerateHardware,
                          AdministrationOperation.ControlHardware);
        MapAdministration(app, server, "updates", AdministrationOperation.EnumerateUpdates,
                          AdministrationOperation.ControlUpdate);
        app.MapGet("/api/updates/check-state", () => new
        {
            Checking = Volatile.Read(ref updateCheck) is { IsCompleted: false }
        });
        app.MapPost("/api/updates/check", async () =>
        {
            await StartUpdateCheck(server);
            return Results.NoContent();
        });
        MapAdministration(app, server, "tasks", AdministrationOperation.EnumerateTasks,
                          AdministrationOperation.ControlTask);
        MapAdministration(app, server, "firewall", AdministrationOperation.EnumerateFirewall,
                          AdministrationOperation.ControlFirewall);
        MapAdministration(app, server, "power", AdministrationOperation.EnumeratePower,
                          AdministrationOperation.ControlPower);
        MapAdministration(app, server, "features", AdministrationOperation.EnumerateFeatures,
                          AdministrationOperation.ControlFeature);
        MapAdministration(app, server, "system-details", AdministrationOperation.EnumerateSystem,
                          AdministrationOperation.ControlSystem);
        MapAdministration(app, server, "wlan", AdministrationOperation.EnumerateWlan,
                          AdministrationOperation.ControlWlan);
        MapAdministration(app, server, "certificates", AdministrationOperation.EnumerateCertificates,
                          AdministrationOperation.ControlCertificate);
        app.MapPost("/api/certificates/details", async (AdministrationIdentityRequest request) =>
        {
            var records = await server.QueryAdministrationAsync(
                AdministrationOperation.QueryCertificate,
                request.Identity);
            var record = records.Single(value => value.Kind == AdministrationKind.CertificateDetails);
            var rawData = Convert.FromBase64String(record.Detail);
            using var certificate = X509CertificateLoader.LoadCertificate(rawData);
            var usage = certificate.Extensions.OfType<X509EnhancedKeyUsageExtension>().FirstOrDefault();
            return new
            {
                certificate.Subject,
                certificate.Issuer,
                SubjectName = certificate.GetNameInfo(X509NameType.SimpleName, false),
                IssuerName = certificate.GetNameInfo(X509NameType.SimpleName, true),
                certificate.SerialNumber,
                certificate.Thumbprint,
                certificate.Version,
                NotBefore = certificate.NotBefore.ToUniversalTime(),
                NotAfter = certificate.NotAfter.ToUniversalTime(),
                SignatureAlgorithm = certificate.SignatureAlgorithm.FriendlyName ??
                                     certificate.SignatureAlgorithm.Value,
                PublicKeyAlgorithm = certificate.PublicKey.Oid.FriendlyName ??
                                     certificate.PublicKey.Oid.Value,
                record.Flags,
                ChainError = record.State,
                Purposes = usage?.EnhancedKeyUsages.Cast<Oid>()
                    .Select(oid => oid.FriendlyName ?? oid.Value)
                    .ToArray(),
                Extensions = certificate.Extensions.Select(extension => new
                {
                    Oid = extension.Oid?.Value,
                    Name = extension.Oid?.FriendlyName,
                    extension.Critical,
                    Value = extension.Format(false)
                }).ToArray(),
                RawData = record.Detail,
                Chain = records.Where(value => value.Kind == AdministrationKind.CertificateChain).ToArray()
            };
        });
    }

    private static Task StartUpdateCheck(NativeServer server)
    {
        lock (UpdateCheckLock)
        {
            if (updateCheck is not { IsCompleted: false })
            {
                updateCheck = server.ControlAdministrationAsync(
                    AdministrationOperation.ControlUpdate,
                    AdministrationAction.Check);
            }
            return updateCheck;
        }
    }

    private static void MapAdministration(
        WebApplication app,
        NativeServer server,
        string path,
        AdministrationOperation enumerate,
        AdministrationOperation control)
    {
        app.MapPost($"/api/{path}", async () => await server.EnumerateAdministrationAsync(enumerate));
        app.MapPost($"/api/{path}/control", async (AdministrationControlRequest request) =>
        {
            if (!Enum.IsDefined(request.Action)) return Results.BadRequest();
            await server.ControlAdministrationAsync(control,
                                                    request.Action,
                                                    request.Identity,
                                                    request.Argument,
                                                    request.Secret);
            return Results.NoContent();
        });
    }
}

internal sealed record PathRequest(string Path);
internal sealed record SecurityDescriptorRequest(string Path, string Sddl);
internal sealed record SecurityAccountRequest(string Value, bool Sid);
internal sealed record FilePageRequest(string? Path, string? EnumerationId);
internal sealed record FileRenameRequest(string Path, string NewPath);
internal sealed record FileHashRequest(string Path, FileHashAlgorithm Algorithm);
internal sealed record FileAttributesRequest(string Path, uint Attributes);
internal sealed record FileVolumeLabelRequest(string Path, string Label);
internal sealed record FileVolumeFormatRequest(string Path, string FileSystem, string Label, bool Quick);
internal sealed record FileSearchRequest(string Path, string Query, uint Mode);
internal sealed record ServiceRequest(string ServiceName);
internal sealed record ServiceControlRequest(string ServiceName, ServiceControl Control, string? Argument);
internal sealed record ProcessIdentityRequest(uint ProcessId, string CreateTime);
internal sealed record ProcessControlRequest(
    uint ProcessId,
    string CreateTime,
    ProcessControl Control,
    uint Value);
internal sealed record ProcessDumpRequest(uint ProcessId, string CreateTime, uint DumpType);
internal sealed record WindowIdentityRequest(string Handle, uint ProcessId, uint ThreadId);
internal sealed record WindowControlRequest(
    string Handle,
    uint ProcessId,
    uint ThreadId,
    WindowControl Control);
internal sealed record BrowserQueryRequest(
    BrowserType Browser,
    BrowserKind Kind,
    string Profile,
    string Cursor);
internal sealed record AdministrationControlRequest(
    AdministrationAction Action,
    string? Identity,
    string? Argument,
    string? Secret);
internal sealed record AdministrationIdentityRequest(string Identity);
internal sealed record ProcessWebRecord(
    uint ProcessId,
    uint ParentProcessId,
    uint SessionId,
    uint ThreadCount,
    uint HandleCount,
    uint Flags,
    ushort MachineType,
    ushort PriorityClass,
    string CreateTime,
    string UserTime,
    string KernelTime,
    string WorkingSetBytes,
    string PrivateBytes,
    string ImageName,
    string UserName,
    string ImagePath,
    string[] ServiceNames)
{
    internal static ProcessWebRecord From(ProcessRecord value) =>
        new(value.ProcessId,
            value.ParentProcessId,
            value.SessionId,
            value.ThreadCount,
            value.HandleCount,
            value.Flags,
            value.MachineType,
            value.PriorityClass,
            value.CreateTime.ToString(),
            value.UserTime.ToString(),
            value.KernelTime.ToString(),
            value.WorkingSetBytes.ToString(),
            value.PrivateBytes.ToString(),
            value.ImageName,
            value.UserName,
            value.ImagePath,
            value.ServiceNames);
}

internal sealed record ProcessInfoWebRecord(
    uint ProcessId,
    uint ParentProcessId,
    uint SessionId,
    uint ThreadCount,
    uint HandleCount,
    uint Flags,
    ushort MachineType,
    ushort PriorityClass,
    DateTime CreateTime,
    string UserTime,
    string KernelTime,
    string WorkingSetBytes,
    string PrivateBytes,
    string ImageName,
    string UserName,
    int ImagePathStatus,
    string ImagePath,
    int CommandLineStatus,
    string CommandLine)
{
    internal static ProcessInfoWebRecord From(ProcessInfo value) =>
        new(value.ProcessId,
            value.ParentProcessId,
            value.SessionId,
            value.ThreadCount,
            value.HandleCount,
            value.Flags,
            value.MachineType,
            value.PriorityClass,
            value.CreateTime,
            value.UserTime.ToString(),
            value.KernelTime.ToString(),
            value.WorkingSetBytes.ToString(),
            value.PrivateBytes.ToString(),
            value.ImageName,
            value.UserName,
            value.ImagePathStatus,
            value.ImagePath,
            value.CommandLineStatus,
            value.CommandLine);
}
