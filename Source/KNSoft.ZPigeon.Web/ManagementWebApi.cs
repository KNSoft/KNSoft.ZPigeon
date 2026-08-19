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
            var page = await server.EnumerateFilesPageAsync(request.Path, enumerationId);
            return Results.Ok(new
            {
                page.EnumerationId,
                Records = page.Records.Select(record => new
                {
                    record.Name,
                    record.Attributes,
                    Size = record.Size.ToString(),
                    record.CreationTime,
                    record.LastAccessTime,
                    record.LastWriteTime,
                    record.HasChildren
                })
            });
        });
        app.MapPost("/api/file/info", async (PathRequest request) =>
            await server.QueryFileAsync(request.Path));
        app.MapPost("/api/file/range", async (FileRangeRequest request) =>
        {
            if (!ulong.TryParse(request.Offset, out var offset) || request.Length is 0 or > 0x10000)
            {
                return Results.BadRequest();
            }
            await using var transfer = await server.OpenFileReadAsync(request.Path, offset);
            var length = (int)Math.Min(request.Length, transfer.FileSize - offset);
            var data = GC.AllocateUninitializedArray<byte>(length);
            var written = 0;
            await foreach (var chunk in transfer.Output.ReadAllAsync())
            {
                var copy = Math.Min(chunk.Length, length - written);
                chunk.Span[..copy].CopyTo(data.AsSpan(written));
                written += copy;
                if (written == length) break;
            }
            if (written != length)
            {
                var completion = await transfer.Completion;
                if (!completion.Status.IsSuccess) throw new NativeException(completion.Status);
                throw new EndOfStreamException();
            }
            return Results.Ok(new { Size = transfer.FileSize.ToString(), Offset = request.Offset, Data = data });
        });
        app.MapPost("/api/file/range/write", async (FileRangeWriteRequest request) =>
        {
            if (!ulong.TryParse(request.Offset, out var offset) || request.Data.Length is 0 or > 0x10000)
            {
                return Results.BadRequest();
            }
            await server.WriteFileRangeAsync(request.Path, offset, request.Data);
            return Results.NoContent();
        });
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
        app.MapPost("/api/process/memory/read", async (ProcessMemoryReadRequest request) =>
        {
            if (!ulong.TryParse(request.CreateTime, out var createTime) ||
                !ulong.TryParse(request.Address, out var address) || request.Length is 0 or > 0x10000)
            {
                return Results.BadRequest();
            }
            return Results.Ok(new
            {
                Address = request.Address,
                Data = await server.ReadProcessMemoryAsync(request.ProcessId, createTime, address, request.Length)
            });
        });
        app.MapPost("/api/process/memory/write", async (ProcessMemoryWriteRequest request) =>
        {
            if (!ulong.TryParse(request.CreateTime, out var createTime) ||
                !ulong.TryParse(request.Address, out var address) || request.Data.Length is 0 or > 0x10000)
            {
                return Results.BadRequest();
            }
            await server.WriteProcessMemoryAsync(request.ProcessId, createTime, address, request.Data);
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
        app.MapPost("/api/audio/devices", async () => await server.EnumerateAudioDevicesAsync());
        app.MapPost("/api/audio/sessions", async () => await server.EnumerateAudioSessionsAsync());
        app.MapPost("/api/audio/endpoint", async (AudioEndpointRequest request) =>
        {
            if (!Enum.IsDefined(request.Flow) || !Enum.IsDefined(request.Control)) return Results.BadRequest();
            await server.ControlAudioEndpointAsync(request.Flow, request.Control, request.Value, request.DeviceId);
            return Results.NoContent();
        });
        app.MapPost("/api/audio/session", async (AudioSessionRequest request) =>
        {
            if (!Enum.IsDefined(request.Control)) return Results.BadRequest();
            await server.ControlAudioSessionAsync(request.Control,
                                                   request.Value,
                                                   request.DeviceId,
                                                   request.SessionId);
            return Results.NoContent();
        });
        app.Map("/api/audio/stream", context => AudioWebSocket.RunAsync(context, server));
        app.MapPost("/api/video/devices", async () => await server.EnumerateVideoDevicesAsync());
        app.Map("/api/video/stream", context => VideoWebSocket.RunAsync(context, server));
        app.MapPost("/api/window/info", async (WindowIdentityRequest request) =>
            await server.QueryWindowAsync(ulong.Parse(request.Handle), request.ProcessId, request.ThreadId));
        app.MapPost("/api/window/image", async (WindowCaptureRequest request) =>
            Results.File(
                await server.CaptureWindowAsync(
                    ulong.Parse(request.Handle),
                    request.ProcessId,
                    request.ThreadId,
                    request.Options),
                "image/jpeg"));
        app.Map("/api/window/stream", context =>
            WindowCaptureWebSocket.RunAsync(context, server));
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
        app.MapPost("/api/window/update", async (WindowUpdateRequest request) =>
        {
            const WindowUpdateFields mask = WindowUpdateFields.Caption | WindowUpdateFields.Rect |
                                            WindowUpdateFields.Style | WindowUpdateFields.ExStyle;
            if (request.Fields == 0 || (request.Fields & ~mask) != 0 || request.Caption is null ||
                request.Caption.Length > 512 ||
                (request.Fields.HasFlag(WindowUpdateFields.Rect) &&
                 (request.Right <= request.Left || request.Bottom <= request.Top)))
            {
                return Results.BadRequest();
            }
            await server.UpdateWindowAsync(
                ulong.Parse(request.Handle),
                request.ProcessId,
                request.ThreadId,
                new WindowUpdate(
                    request.Fields,
                    request.Caption,
                    request.Left,
                    request.Top,
                    request.Right,
                    request.Bottom,
                    request.Style,
                    request.ExStyle));
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
        app.MapPost("/api/wmi/namespaces", async (WmiNamespaceRequest request) =>
        {
            ValidateWmiNamespace(request.Namespace);
            return Results.Ok(await server.EnumerateWmiNamespacesAsync(request.Namespace));
        });
        app.MapPost("/api/wmi/classes", async (WmiNamespaceRequest request) =>
        {
            ValidateWmiNamespace(request.Namespace);
            return Results.Ok(await server.EnumerateWmiClassesAsync(request.Namespace));
        });
        app.MapPost("/api/wmi/query", async (WmiQueryRequest request) =>
        {
            ValidateWmiNamespace(request.Namespace);
            if (string.IsNullOrWhiteSpace(request.Query) || request.Query.Length > 32768 ||
                request.Limit is 0 or > 1000)
            {
                throw new BadHttpRequestException("WMI 查询参数无效");
            }
            return Results.Ok(await server.QueryWmiAsync(
                request.Namespace,
                request.Query,
                request.Limit,
                request.SystemProperties));
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
        app.MapPost("/api/sessions", async () =>
            await server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateSessions));
        app.MapPost("/api/logon-sessions", async () =>
            await server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateLogonSessions));
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
        MapAdministration(app, server, "network-shares/published",
                          AdministrationOperation.EnumeratePublishedShares,
                          AdministrationOperation.ControlPublishedShare);
        MapAdministration(app, server, "network-shares/connections",
                          AdministrationOperation.EnumerateNetworkConnections,
                          AdministrationOperation.ControlNetworkConnection);
        app.MapPost("/api/network-shares/published/query", async (AdministrationIdentityRequest request) =>
            await server.QueryAdministrationAsync(AdministrationOperation.QueryPublishedShare, request.Identity));
        MapAdministration(app, server, "network-adapters",
                          AdministrationOperation.EnumerateNetworkAdapters,
                          AdministrationOperation.ControlNetworkAdapter);
        MapAdministration(app, server, "network-routes",
                          AdministrationOperation.EnumerateNetworkRoutes,
                          AdministrationOperation.ControlNetworkRoute);
        app.MapPost("/api/network-endpoints", async () =>
            await server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateNetworkEndpoints));
        MapAdministration(app, server, "certificates", AdministrationOperation.EnumerateCertificates,
                          AdministrationOperation.ControlCertificate);
        app.MapPost("/api/credentials", async (HttpContext context) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            return await server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateCredentials);
        });
        app.MapPost("/api/credentials/secret", async (HttpContext context, AdministrationIdentityRequest request) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            var records = await server.QueryAdministrationAsync(
                AdministrationOperation.QueryCredential,
                request.Identity);
            return Results.Ok(new { Secret = records.Single().Detail });
        });
        app.MapPost("/api/credentials/control", async (CredentialControlRequest request) =>
        {
            if (!Enum.IsDefined(request.Store) ||
                request.Action is not (AdministrationAction.Create or AdministrationAction.Configure or
                    AdministrationAction.Delete))
            {
                return Results.BadRequest();
            }
            string identity;
            if (request.Action == AdministrationAction.Create)
            {
                if (string.IsNullOrEmpty(request.Target) || string.IsNullOrEmpty(request.UserName) ||
                    request.Target.Length > 32767 || request.UserName.Length > 513 ||
                    request.Target.Contains('\0') || request.UserName.Contains('\0') || request.Secret is null ||
                    request.Secret.Contains('\0') || request.Secret.Length > 32767 ||
                    request.Store == CredentialStore.Windows && request.Type is not (1 or 2))
                {
                    return Results.BadRequest();
                }
                identity = request.Store == CredentialStore.Windows ?
                               $"W{request.Type}:{request.Target}" :
                               $"V{request.Target.Length}:{request.Target}{request.UserName}";
            }
            else
            {
                var prefix = request.Store == CredentialStore.Windows ? 'W' : 'V';
                if (string.IsNullOrEmpty(request.Identity) || request.Identity[0] != prefix ||
                    request.Identity.Length > 65535 ||
                    (request.Store == CredentialStore.Windows &&
                     request.Action == AdministrationAction.Configure &&
                     string.IsNullOrEmpty(request.UserName)) ||
                    (request.Action != AdministrationAction.Delete && request.Secret is null) ||
                    (request.Secret?.Contains('\0') ?? false) || request.Secret is { Length: > 32767 })
                {
                    return Results.BadRequest();
                }
                identity = request.Identity;
            }
            await server.ControlAdministrationAsync(
                AdministrationOperation.ControlCredential,
                request.Action,
                identity,
                request.UserName,
                request.Secret);
            return Results.NoContent();
        });
        app.MapFirmwareApi(server);
        MapAdministration(app, server, "clipboard", AdministrationOperation.EnumerateClipboard,
                          AdministrationOperation.ControlClipboard);
        app.MapPost("/api/clipboard/wait", async (AdministrationIdentityRequest request) =>
            await server.QueryAdministrationAsync(AdministrationOperation.WaitClipboard, request.Identity));
        app.MapPost("/api/wlan/profile", async (AdministrationIdentityRequest request) =>
            await server.QueryAdministrationAsync(AdministrationOperation.QueryWlanProfile, request.Identity));
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

    private static void ValidateWmiNamespace(string? value)
    {
        if (string.IsNullOrEmpty(value) || value.Length > 512 || value.Contains('\0') ||
            !(value.Equals("ROOT", StringComparison.OrdinalIgnoreCase) ||
              value.StartsWith("ROOT\\", StringComparison.OrdinalIgnoreCase)))
        {
            throw new BadHttpRequestException("WMI 命名空间无效");
        }
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
internal sealed record FileRangeRequest(string Path, string Offset, uint Length);
internal sealed record FileRangeWriteRequest(string Path, string Offset, byte[] Data);
internal sealed record ProcessMemoryReadRequest(uint ProcessId, string CreateTime, string Address, uint Length);
internal sealed record ProcessMemoryWriteRequest(uint ProcessId, string CreateTime, string Address, byte[] Data);
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
internal sealed record AudioEndpointRequest(
    AudioFlow Flow,
    AudioEndpointControl Control,
    uint Value,
    string DeviceId);
internal sealed record AudioSessionRequest(
    AudioSessionControl Control,
    uint Value,
    string DeviceId,
    string SessionId);
internal sealed record WindowCaptureRequest(
    string Handle,
    uint ProcessId,
    uint ThreadId,
    bool CaptureCursor,
    uint MaxDimension,
    ushort FrameRate,
    ushort ImageQuality)
{
    internal WindowCaptureOptions Options =>
        new(CaptureCursor, MaxDimension, FrameRate, ImageQuality);
}
internal sealed record WindowControlRequest(
    string Handle,
    uint ProcessId,
    uint ThreadId,
    WindowControl Control);
internal sealed record WindowUpdateRequest(
    string Handle,
    uint ProcessId,
    uint ThreadId,
    WindowUpdateFields Fields,
    string? Caption,
    int Left,
    int Top,
    int Right,
    int Bottom,
    uint Style,
    uint ExStyle);
internal sealed record BrowserQueryRequest(
    BrowserType Browser,
    BrowserKind Kind,
    string Profile,
    string Cursor);
internal sealed record WmiNamespaceRequest(string Namespace);
internal sealed record WmiQueryRequest(string Namespace, string Query, uint Limit, bool SystemProperties);
internal sealed record AdministrationControlRequest(
    AdministrationAction Action,
    string? Identity,
    string? Argument,
    string? Secret);
internal sealed record AdministrationIdentityRequest(string Identity);
internal sealed record CredentialControlRequest(
    AdministrationAction Action,
    CredentialStore Store,
    uint Type,
    string? Identity,
    string? Target,
    string? UserName,
    string? Secret);
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
    int ImageBaseStatus,
    string ImageBase,
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
            value.ImageBaseStatus,
            value.ImageBase.ToString(),
            value.ImageName,
            value.UserName,
            value.ImagePathStatus,
            value.ImagePath,
            value.CommandLineStatus,
            value.CommandLine);
}
