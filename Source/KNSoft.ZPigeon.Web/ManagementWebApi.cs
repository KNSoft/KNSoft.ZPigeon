using KNSoft.ZPigeon.Server.Managed;
using Microsoft.Net.Http.Headers;
using System.Globalization;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.RegularExpressions;

namespace KNSoft.ZPigeon.Web;

internal static partial class ManagementWebApi
{
    [GeneratedRegex("^[A-Za-z]:\\\\$")]
    private static partial Regex VolumePathRegex();

    [GeneratedRegex("^[-_. A-Za-z0-9]{1,64}$")]
    private static partial Regex AppContainerNameRegex();

    internal static void MapManagementApi(
        this WebApplication app,
        ClientServicesRegistry services)
    {
        var server = services.Server;
        app.MapPost("/api/portable/devices", () => server.EnumeratePortableDevicesAsync());
        app.MapPost("/api/portable/objects", async (PortablePageRequest request) =>
        {
            ValidatePortable(request.DeviceId);
            if (request.ParentId is not null) ValidatePortable(request.ParentId);
            var page = await server.EnumeratePortableObjectsAsync(request.DeviceId,
                                                                  request.ParentId,
                                                                  request.Offset);
            return Results.Ok(new
            {
                Objects = page.Objects.Select(item => new
                {
                    Size = item.Size.ToString(CultureInfo.InvariantCulture),
                    ModifiedTime = item.ModifiedTime.ToString(CultureInfo.InvariantCulture),
                    Capacity = item.Capacity.ToString(CultureInfo.InvariantCulture),
                    FreeSpace = item.FreeSpace.ToString(CultureInfo.InvariantCulture),
                    item.Flags,
                    item.Id,
                    item.PersistentId,
                    item.Name
                }),
                page.NextOffset
            });
        });
        app.MapPost("/api/portable/folder", async (PortableNameRequest request) =>
        {
            ValidatePortable(request.DeviceId, request.ObjectId, request.Name);
            await server.CreatePortableFolderAsync(request.DeviceId, request.ObjectId, request.Name);
            return Results.NoContent();
        });
        app.MapPost("/api/portable/delete", async (PortableObjectRequest request) =>
        {
            ValidatePortable(request.DeviceId, request.ObjectId);
            await server.DeletePortableObjectAsync(request.DeviceId, request.ObjectId);
            return Results.NoContent();
        });
        app.MapPost("/api/portable/rename", async (PortableNameRequest request) =>
        {
            ValidatePortable(request.DeviceId, request.ObjectId, request.Name);
            await server.RenamePortableObjectAsync(request.DeviceId, request.ObjectId, request.Name);
            return Results.NoContent();
        });
        app.MapGet("/api/portable/download", async (
            HttpContext context,
            string deviceId,
            string objectId,
            string name) =>
        {
            ValidatePortable(deviceId, objectId, name);
            await using var transfer = await server.OpenPortableReadAsync(deviceId, objectId);
            context.Response.ContentLength = checked((long)transfer.FileSize);
            context.Response.ContentType = "application/octet-stream";
            context.Response.GetTypedHeaders().ContentDisposition = new ContentDispositionHeaderValue("attachment")
            {
                FileNameStar = name
            };
            await foreach (var data in transfer.Output.ReadAllAsync(context.RequestAborted))
            {
                using (data) await context.Response.Body.WriteAsync(data.Memory, context.RequestAborted);
            }
            var status = (await transfer.Completion).Status;
            if (!status.IsSuccess) throw new NativeException(status);
        });
        app.MapPut("/api/portable/upload", async (
            HttpContext context,
            string deviceId,
            string parentId,
            string name) =>
        {
            ValidatePortable(deviceId, parentId, name);
            if (context.Request.ContentLength is not long length || length < 0)
            {
                return Results.StatusCode(StatusCodes.Status411LengthRequired);
            }
            await using var transfer = await server.OpenPortableWriteAsync(deviceId, parentId, name, (ulong)length);
            var buffer = GC.AllocateUninitializedArray<byte>(0x10000);
            long received = 0;
            int read;
            while ((read = await context.Request.Body.ReadAsync(buffer, context.RequestAborted)) != 0)
            {
                await transfer.WriteAsync(buffer.AsMemory(0, read), context.RequestAborted);
                received += read;
            }
            if (received != length) throw new EndOfStreamException();
            var status = (await transfer.Completion).Status;
            if (!status.IsSuccess) throw new NativeException(status);
            return Results.NoContent();
        });
        app.MapPost("/api/files", async (FilePageRequest request) =>
        {
            var enumerationId = 0U;
            if (request.EnumerationId is not null &&
                !uint.TryParse(request.EnumerationId, out enumerationId))
            {
                return Results.BadRequest();
            }
            var page = await server.EnumerateFilesPageAsync(request.Path, enumerationId);
            return Results.Ok(FilePageResult(page));
        });
        app.MapPost("/api/files/picker", async (FilePickerPageRequest request) =>
        {
            if (request.Path is { Length: > 32767 } || request.Path?.Contains('\0') == true ||
                request.Query is { Length: > 260 } ||
                request.Query?.IndexOfAny(['\0', '\\', '/', ':']) >= 0 ||
                request.Group is { Length: > 1 } ||
                (request.Group is { Length: 1 } && request.Group[0] != '#' &&
                 (request.Group[0] < 'A' || request.Group[0] > 'Z')) ||
                request.EnumerationId != 0 &&
                    (request.Path is not null || !string.IsNullOrEmpty(request.Query) ||
                     !string.IsNullOrEmpty(request.Group)))
            {
                return Results.BadRequest();
            }
            var filter = request.Query;
            if (!string.IsNullOrEmpty(filter) && filter.IndexOfAny(['*', '?']) < 0)
            {
                filter = $"*{filter}*";
            }
            var page = await server.EnumerateFilteredFilesPageAsync(request.Path,
                                                                     filter,
                                                                     string.IsNullOrEmpty(request.Group) ? '\0' : request.Group[0],
                                                                     request.EnumerationId);
            return Results.Ok(FilePageResult(page));
        });
        app.MapPost("/api/files/picker/close", async (FileEnumerationCloseRequest request) =>
        {
            if (request.EnumerationId == 0)
            {
                return Results.BadRequest();
            }
            await server.CloseFileEnumerationAsync(request.EnumerationId);
            return Results.NoContent();
        });
        app.MapPost("/api/file/archive", async (FileArchivePageRequest request) =>
        {
            if (!uint.TryParse(request.EnumerationId, out var enumerationId) ||
                (enumerationId == 0) != !string.IsNullOrWhiteSpace(request.Path) ||
                request.Path is { Length: > 32767 } || request.Path?.Contains('\0') == true)
            {
                return Results.BadRequest();
            }
            return Results.Ok(FilePageResult(await server.EnumerateArchivePageAsync(request.Path, enumerationId)));
        });
        app.MapPost("/api/file/shortcut", async (PathRequest request) => new
        {
            Target = await server.QueryShortcutAsync(request.Path)
        });
        app.MapPost("/api/file/certificate", async (PathRequest request) =>
        {
            await using var transfer = await server.OpenFileReadAsync(request.Path);
            if (transfer.FileSize is 0 or > 0x000C0000) return Results.BadRequest();
            var data = GC.AllocateUninitializedArray<byte>((int)transfer.FileSize);
            var offset = 0;
            await foreach (var chunk in transfer.Output.ReadAllAsync())
            {
                using (chunk)
                {
                    if (chunk.Length > data.Length - offset) throw new InvalidDataException();
                    chunk.Span.CopyTo(data.AsSpan(offset));
                    offset += chunk.Length;
                }
            }
            var status = (await transfer.Completion).Status;
            if (!status.IsSuccess) throw new NativeException(status);
            if (offset != data.Length) throw new EndOfStreamException();
            using var certificate = LoadCertificate(data);
            return Results.Ok(CertificateDetails(certificate,
                                                 0,
                                                 0,
                                                 Array.Empty<AdministrationRecord>()));
        });
        app.MapPost("/api/file/image-preview", async (HttpContext context, FileImagePreviewRequest request) =>
        {
            if (!Enum.IsDefined(request.Quality)) return Results.BadRequest();
            context.Response.Headers.CacheControl = "no-store";
            context.Response.Headers.XContentTypeOptions = "nosniff";
            return Results.Bytes(await server.PreviewImageAsync(request.Path, request.Quality), "image/jpeg");
        });
        app.MapPost("/api/file/info", (PathRequest request) =>
            server.QueryFileAsync(request.Path));
        app.MapPost("/api/file/range", async (HttpContext context, FileRangeRequest request) =>
        {
            if (!ulong.TryParse(request.Offset, out var offset) || request.Length is 0 or > 0x10000)
            {
                return Results.BadRequest();
            }
            await using var transfer = await server.OpenFileReadAsync(request.Path, offset);
            if (offset > transfer.FileSize) return Results.BadRequest();
            var length = (int)Math.Min(request.Length, transfer.FileSize - offset);
            var data = GC.AllocateUninitializedArray<byte>(length);
            var written = 0;
            await foreach (var chunk in transfer.Output.ReadAllAsync())
            {
                using (chunk)
                {
                    var copy = Math.Min(chunk.Length, length - written);
                    chunk.Span[..copy].CopyTo(data.AsSpan(written));
                    written += copy;
                    if (written == length) break;
                }
            }
            if (written != length)
            {
                var completion = await transfer.Completion;
                if (!completion.Status.IsSuccess) throw new NativeException(completion.Status);
                throw new EndOfStreamException();
            }
            context.Response.Headers["X-ZPigeon-Size"] = transfer.FileSize.ToString(CultureInfo.InvariantCulture);
            return Results.Bytes(data, "application/octet-stream");
        });
        app.MapPost("/api/file/range/write", async (HttpRequest request) =>
        {
            var (metadata, data) = await BinaryWebApi.ReadAsync<FileRangeWriteRequest>(request, 0x10000);
            if (!ulong.TryParse(metadata.Offset, out var offset) || data.Length == 0)
            {
                return Results.BadRequest();
            }
            await server.WriteFileRangeAsync(metadata.Path, offset, data);
            return Results.NoContent();
        });
        app.MapPost("/api/file/security", (PathRequest request) =>
            server.QueryFileSecurityAsync(request.Path));
        app.MapPost("/api/file/security/set", (SecurityDescriptorRequest request) =>
            server.SetFileSecurityAsync(request.Path, request.Sddl, request.DaclProtected));
        app.MapPost("/api/security/account", async (SecurityAccountRequest request) => new
        {
            Value = request.Sid ? await server.ResolveAccountSidAsync(request.Value) :
                                  await server.ResolveAccountNameAsync(request.Value)
        });
        app.MapPost("/api/file/hash", (FileHashRequest request) =>
            server.HashFileAsync(request.Path, request.Algorithm));
        app.MapPost("/api/file/delete", (PathRequest request) =>
            server.DeleteFileAsync(request.Path));
        app.MapPost("/api/file/rename", (FileRenameRequest request) =>
            server.RenameFileAsync(request.Path, request.NewPath));
        app.MapPost("/api/file/attributes", (FileAttributesRequest request) =>
            server.SetFileAttributesAsync(request.Path, request.Attributes));
        app.MapPost("/api/file/open", async (FileOpenRequest request) =>
        {
            await server.StartExecutionAsync(new ExecutionStart(
                ExecutionEngine.ShellExecute,
                ExecutionIdentity.Current,
                uint.MaxValue,
                request.Hidden ? ExecutionFlags.Hidden : ExecutionFlags.None,
                request.Path,
                null,
                null,
                "open",
                null,
                null,
                null));
            return Results.NoContent();
        });
        app.MapPost("/api/file/owners", (PathRequest request) =>
            server.QueryFileOwnersAsync(request.Path));
        app.MapPost("/api/file/owners/control", async (FileOwnerControlRequest request) =>
        {
            if (request.ProcessIds is not { Length: > 0 } ||
                request.Control is not (FileOwnerControl.Terminate or FileOwnerControl.CloseHandles))
            {
                return Results.BadRequest();
            }
            return Results.Ok(await server.ControlFileOwnersAsync(request.Path,
                                                                    request.Control,
                                                                    request.ProcessIds));
        });
        app.MapPost("/api/file/volume", (PathRequest request) =>
            server.QueryFileVolumeAsync(request.Path));
        app.MapPost("/api/file/volume/label", (FileVolumeLabelRequest request) =>
            server.SetFileVolumeLabelAsync(request.Path, request.Label));
        app.MapPost("/api/file/volume/format", async (FileVolumeFormatRequest request) =>
        {
            if (!VolumePathRegex().IsMatch(request.Path) ||
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
            var separator = command.IndexOf(' ');
            return Results.Ok(await RemoteCommand.RunAsync(server,
                                                            separator < 0 ? command : command[..separator],
                                                            separator < 0 ? null : command[(separator + 1)..],
                                                            request.Path));
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
                using (data) await context.Response.Body.WriteAsync(data.Memory, context.RequestAborted);
            }
            var status = (await transfer.Completion).Status;
            if (!status.IsSuccess)
            {
                throw new NativeException(status);
            }
        });
        app.MapMethods("/api/file/content", ["GET", "HEAD"],
                       (HttpContext context, string path) => StreamFileContentAsync(context, server, path));
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
        app.MapPost("/api/file/url-download", async (FileUrlDownloadRequest request) =>
        {
            if (request.Directory.Length is 0 or > 32767 ||
                request.Directory.Contains('\0') ||
                request.Url.Length is 0 or > 2048 ||
                request.Name.Length is 0 or > 255 ||
                !Path.IsPathFullyQualified(request.Directory) ||
                Path.GetFileName(request.Name) != request.Name ||
                request.Name is "." or ".." ||
                request.Name.TrimEnd(' ', '.') != request.Name ||
                request.Name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
                request.Engine is not (FileDownloadEngine.Bits or FileDownloadEngine.WinHttp) ||
                !Uri.TryCreate(request.Url, UriKind.Absolute, out var uri) ||
                (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps) ||
                uri.Host.Length == 0 || uri.UserInfo.Length != 0 || uri.Fragment.Length != 0)
            {
                return Results.BadRequest();
            }
            var path = Path.Combine(request.Directory, request.Name);
            if (path.Length > 32767) return Results.BadRequest();
            var id = Guid.NewGuid();
            await server.StartFileDownloadAsync(id,
                                                uri.AbsoluteUri,
                                                path,
                                                request.Engine,
                                                request.Overwrite);
            return Results.Ok(new { Id = id.ToString("D"), Path = path });
        });
        app.MapPost("/api/file/url-downloads", async () => Results.Ok(
            (await server.EnumerateFileDownloadsAsync()).Select(download => new
            {
                download.Id,
                download.Url,
                download.Path,
                download.ErrorText,
                Engine = (byte)download.Engine,
                State = (byte)download.State,
                download.Result,
                TransferredBytes = download.TransferredBytes.ToString(CultureInfo.InvariantCulture),
                TotalBytes = download.TotalBytes == ulong.MaxValue ?
                                 null : download.TotalBytes.ToString(CultureInfo.InvariantCulture)
            })));
        app.MapPost("/api/file/url-download/cancel", async (FileUrlDownloadCancelRequest request) =>
        {
            if (!Guid.TryParseExact(request.Id, "D", out var id)) return Results.BadRequest();
            await server.CancelFileDownloadAsync(id);
            return Results.NoContent();
        });
        app.MapPost("/api/processes", async () =>
        {
            var windows = server.EnumerateProcessesAsync();
            var wsl = EnumerateWslProcessesAsync(server);
            await Task.WhenAll(windows, wsl);
            return windows.Result.Select(ProcessWebRecord.From)
                .Concat(wsl.Result.Select(ProcessWebRecord.From));
        });
        app.MapPost("/api/wsl/process/control", async (AdministrationControlRequest request) =>
        {
            if (request.Action is not (AdministrationAction.Enable or
                                       AdministrationAction.Disable or
                                       AdministrationAction.Stop) ||
                string.IsNullOrEmpty(request.Identity))
            {
                return Results.BadRequest();
            }
            await server.ControlAdministrationAsync(AdministrationOperation.ControlWslProcess,
                                                      request.Action,
                                                      request.Identity);
            return Results.NoContent();
        });
        app.MapPost("/api/winobj", (WinObjRequest request) =>
            server.QueryAdministrationAsync(AdministrationOperation.QueryObjectDirectory,
                                            request.Path));
        app.MapPost("/api/uia/children", (UiAutomationRequest request) =>
            server.QueryAdministrationAsync(AdministrationOperation.QueryUiAutomationChildren,
                                            request.Identity));
        app.MapPost("/api/uia/properties", (UiAutomationRequest request) =>
            server.QueryAdministrationAsync(AdministrationOperation.QueryUiAutomationProperties,
                                            request.Identity));
        app.MapPost("/api/process/info", async (ProcessIdentityRequest request) =>
            ProcessInfoWebRecord.From(
                await server.QueryProcessAsync(
                    request.ProcessId,
                    ulong.Parse(request.CreateTime, CultureInfo.InvariantCulture))));
        app.MapPost("/api/process/modules", async (ProcessIdentityRequest request) =>
        {
            if (!ulong.TryParse(request.CreateTime, out var createTime)) return Results.BadRequest();
            return Results.Ok(await server.EnumerateProcessModulesAsync(request.ProcessId, createTime));
        });
        app.MapPost("/api/process/control", async (ProcessControlRequest request) =>
        {
            if (!Enum.IsDefined(request.Control)) return Results.BadRequest();
            await server.ControlProcessAsync(
                request.ProcessId,
                ulong.Parse(request.CreateTime, CultureInfo.InvariantCulture),
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
            return Results.Bytes(
                await server.ReadProcessMemoryAsync(request.ProcessId, createTime, address, request.Length),
                "application/octet-stream");
        });
        app.MapPost("/api/process/memory/write", async (HttpRequest request) =>
        {
            var (metadata, data) = await BinaryWebApi.ReadAsync<ProcessMemoryWriteRequest>(request, 0x10000);
            if (!ulong.TryParse(metadata.CreateTime, out var createTime) ||
                !ulong.TryParse(metadata.Address, out var address) || data.Length == 0)
            {
                return Results.BadRequest();
            }
            await server.WriteProcessMemoryAsync(metadata.ProcessId, createTime, address, data);
            return Results.NoContent();
        });
        app.MapPost("/api/process/memory/map", async (ProcessIdentityRequest request) =>
        {
            if (!ulong.TryParse(request.CreateTime, out var createTime)) return Results.BadRequest();
            return Results.Ok(await server.QueryProcessMemoryMapAsync(request.ProcessId, createTime));
        });
        app.MapPost("/api/process/memory/map/regions", async (ProcessMemoryMapRequest request) =>
            Results.Ok(await server.QueryProcessMemoryRegionsAsync(request.SnapshotId,
                                                                   request.AllocationIndex)));
        app.MapPost("/api/process/memory/map/close", async (ProcessMemoryMapCloseRequest request) =>
        {
            await server.CloseProcessMemoryMapAsync(request.SnapshotId);
            return Results.NoContent();
        });
        app.MapPost("/api/process/dump", async (HttpContext context, ProcessDumpRequest request) =>
        {
            var path = await server.CreateProcessDumpAsync(
                request.ProcessId,
                ulong.Parse(request.CreateTime, CultureInfo.InvariantCulture),
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
                    using (data) await context.Response.Body.WriteAsync(data.Memory, context.RequestAborted);
                }
                var status = (await transfer.Completion).Status;
                if (!status.IsSuccess) throw new NativeException(status);
            }
            finally
            {
                await server.DeleteFileAsync(path);
            }
        });
        app.MapPost("/api/windows", () => server.EnumerateWindowsAsync());
        app.MapPost("/api/audio/devices", () => server.EnumerateAudioDevicesAsync());
        app.MapPost("/api/audio/sessions", () => server.EnumerateAudioSessionsAsync());
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
        app.MapPost("/api/video/devices", () => server.EnumerateVideoDevicesAsync());
        app.Map("/api/video/stream", context => VideoWebSocket.RunAsync(context, server));
        app.MapPost("/api/window/info", (WindowIdentityRequest request) =>
            server.QueryWindowAsync(
                ulong.Parse(request.Handle, CultureInfo.InvariantCulture),
                request.ProcessId,
                request.ThreadId));
        app.MapPost("/api/window/image", async (WindowCaptureRequest request) =>
            Results.File(
                await server.CaptureWindowAsync(
                    ulong.Parse(request.Handle, CultureInfo.InvariantCulture),
                    request.ProcessId,
                    request.ThreadId,
                    request.Options),
                "image/jpeg"));
        app.Map("/api/window/stream", context =>
            WindowCaptureWebSocket.RunAsync(
                context,
                server,
                services.Current.ConnectionPerformance));
        app.MapPost("/api/window/control", async (WindowControlRequest request) =>
        {
            if (!Enum.IsDefined(request.Control))
            {
                return Results.BadRequest();
            }
            await server.ControlWindowAsync(
                ulong.Parse(request.Handle, CultureInfo.InvariantCulture),
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
                ulong.Parse(request.Handle, CultureInfo.InvariantCulture),
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
        app.MapPost("/api/browsers", () => server.EnumerateBrowsersAsync());
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
        app.MapPost("/api/browser/document/open", async (BrowserDocumentOpenRequest request) =>
        {
            if (!Enum.IsDefined(request.Browser) ||
                request.Kind is not BrowserKind.Bookmark and not BrowserKind.Setting ||
                request.Profile.Length is 0 or >= 260 || request.Profile.IndexOfAny(['\\', '/', ':']) >= 0)
            {
                return Results.BadRequest();
            }
            return Results.Ok(await server.OpenBrowserDocumentAsync(request.Browser,
                                                                     request.Kind,
                                                                     request.Profile));
        });
        app.MapPost("/api/browser/document/node", async (BrowserDocumentNodeRequest request) =>
            Results.Ok(await server.QueryBrowserDocumentNodeAsync(request.SnapshotId,
                                                                  request.NodeId,
                                                                  request.Cursor)));
        app.MapPost("/api/browser/document/close", async (BrowserDocumentCloseRequest request) =>
        {
            await server.CloseBrowserDocumentAsync(request.SnapshotId);
            return Results.NoContent();
        });
        app.MapGet("/api/browser/export", async (
            HttpContext context,
            BrowserType browser,
            BrowserKind kind,
            string profile) =>
        {
            if (!Enum.IsDefined(browser) || !Enum.IsDefined(kind) || kind < BrowserKind.History ||
                profile.Length is 0 or >= 260 || profile.IndexOfAny(['\\', '/', ':']) >= 0)
            {
                context.Response.StatusCode = StatusCodes.Status400BadRequest;
                return;
            }
            context.Response.ContentType = "text/csv; charset=utf-8";
            context.Response.GetTypedHeaders().ContentDisposition = new ContentDispositionHeaderValue("attachment")
            {
                FileNameStar = $"ZPigeon-{browser}-{kind}-{profile}.csv"
            };
            await using var writer = new StreamWriter(
                context.Response.Body,
                new UTF8Encoding(true),
                leaveOpen: true);
            await writer.WriteLineAsync(BrowserCsvHeader(kind));
            ulong cursor = 0;
            do
            {
                var page = await server.QueryBrowserAsync(browser, kind, profile, cursor);
                foreach (var record in page.Records)
                {
                    await writer.WriteLineAsync(string.Join(',', BrowserCsvValues(record).Select(BrowserCsv)));
                }
                var nextCursor = ulong.Parse(page.NextCursor, CultureInfo.InvariantCulture);
                if (nextCursor != 0 && nextCursor == cursor)
                {
                    throw new InvalidDataException("Client 返回了无效的浏览器分页游标");
                }
                cursor = nextCursor;
            } while (cursor != 0);
            await writer.FlushAsync(context.RequestAborted);
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
        app.MapPost("/api/services", () => server.EnumerateServicesAsync());
        app.MapPost("/api/service/info", (ServiceRequest request) =>
            server.QueryServiceAsync(request.ServiceName));
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
        MapAdministration(app, server, "user-profiles", AdministrationOperation.EnumerateUserProfiles,
                          AdministrationOperation.ControlUserProfile);
        app.MapPost("/api/sessions", () =>
            server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateSessions));
        app.MapPost("/api/logon-sessions", () =>
            server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateLogonSessions));
        MapAdministration(app, server, "page-files", AdministrationOperation.EnumeratePageFiles,
                          AdministrationOperation.ControlPageFile);
        MapAdministration(app, server, "system-protection", AdministrationOperation.EnumerateSystemProtection,
                          AdministrationOperation.ControlSystemProtection);
        MapAdministration(app, server, "restore-points", AdministrationOperation.EnumerateRestorePoints,
                          AdministrationOperation.ControlRestorePoint);
        MapAdministration(app, server, "shadow-copies", AdministrationOperation.EnumerateShadowCopies,
                          AdministrationOperation.ControlShadowCopy);
        app.MapPost("/api/bitlocker/volumes", () =>
            server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateBitLockerVolumes));
        app.MapPost("/api/bitlocker/protectors", () =>
            server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateBitLockerProtectors));
        app.MapPost("/api/bitlocker/volumes/control", async (AdministrationControlRequest request) =>
        {
            if (string.IsNullOrEmpty(request.Identity) || request.Identity.Length > 260 ||
                request.Action is not (AdministrationAction.Encrypt or AdministrationAction.Decrypt or
                                       AdministrationAction.Pause or AdministrationAction.Resume or
                                       AdministrationAction.Enable or AdministrationAction.Disable or
                                       AdministrationAction.Lock or AdministrationAction.Unlock) ||
                request.Argument?.Length > 8 || request.Secret?.Length > 64)
            {
                return Results.BadRequest();
            }
            await server.ControlAdministrationAsync(AdministrationOperation.ControlBitLockerVolume,
                                                    request.Action,
                                                    request.Identity,
                                                    request.Argument,
                                                    request.Secret);
            return Results.NoContent();
        });
        app.MapPost("/api/bitlocker/protectors/control", async (AdministrationControlRequest request) =>
        {
            if (string.IsNullOrEmpty(request.Identity) || request.Identity.Length > 260 ||
                request.Action is not (AdministrationAction.Create or AdministrationAction.Delete) ||
                request.Argument?.Length > 256 || request.Secret?.Length > 64)
            {
                return Results.BadRequest();
            }
            await server.ControlAdministrationAsync(AdministrationOperation.ControlBitLockerProtector,
                                                    request.Action,
                                                    request.Identity,
                                                    request.Argument,
                                                    request.Secret);
            return Results.NoContent();
        });
        MapAdministration(app, server, "bluetooth", AdministrationOperation.EnumerateBluetooth,
                          AdministrationOperation.ControlBluetooth);
        app.MapPost("/api/keyboard/wait", () =>
            server.QueryAdministrationAsync(AdministrationOperation.WaitKeyboard, "wait"));
        app.MapPost("/api/location", () =>
            server.QueryAdministrationAsync(AdministrationOperation.QueryLocation, "current"));
        MapAdministration(app, server, "fonts", AdministrationOperation.EnumerateFonts,
                          AdministrationOperation.ControlFont);
        app.MapPost("/api/app-containers", () =>
            server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateAppContainers));
        app.MapPost("/api/app-containers/control", async (AppContainerControlRequest request) =>
        {
            switch (request.Action)
            {
                case AdministrationAction.Create:
                    if (string.IsNullOrEmpty(request.Identity) ||
                        !AppContainerNameRegex().IsMatch(request.Identity) ||
                        string.IsNullOrEmpty(request.DisplayName) || request.DisplayName.Length > 512 ||
                        request.DisplayName.IndexOfAny(['\r', '\n', '\0']) >= 0 ||
                        request.Description is null || request.Description.Length > 2048 ||
                        request.Description.Contains('\0') ||
                        request.Capabilities is null or { Length: > 64 } ||
                        request.Capabilities.Any(value => string.IsNullOrWhiteSpace(value) ||
                            value.Length > 256 || value.Contains('\n') || value.Contains('\0')))
                    {
                        return Results.BadRequest();
                    }
                    await server.ControlAdministrationAsync(
                        AdministrationOperation.ControlAppContainer,
                        request.Action,
                        request.Identity,
                        $"{request.DisplayName}\n{request.Description}",
                        string.Join('\n', request.Capabilities.Distinct(StringComparer.OrdinalIgnoreCase)));
                    break;

                case AdministrationAction.Configure:
                    if (string.IsNullOrEmpty(request.Identity) || request.Identity.Length > 184 ||
                        request.Loopback is null)
                    {
                        return Results.BadRequest();
                    }
                    await server.ControlAdministrationAsync(
                        AdministrationOperation.ControlAppContainer,
                        request.Action,
                        request.Identity,
                        request.Loopback.Value ? "1" : "0");
                    break;

                case AdministrationAction.Delete:
                    if (string.IsNullOrEmpty(request.Identity) || request.Identity.Length > 64)
                    {
                        return Results.BadRequest();
                    }
                    await server.ControlAdministrationAsync(
                        AdministrationOperation.ControlAppContainer,
                        request.Action,
                        request.Identity);
                    break;

                default:
                    return Results.BadRequest();
            }
            return Results.NoContent();
        });
        app.MapPost("/api/software", () =>
            server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateSoftware));
        app.MapPost("/api/client-status", () =>
            server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateClientStatus));
        MapAdministration(app, server, "input-methods", AdministrationOperation.EnumerateInputMethods,
                          AdministrationOperation.ControlInputMethod);
        MapAdministration(app, server, "hardware", AdministrationOperation.EnumerateHardware,
                          AdministrationOperation.ControlHardware);
        MapAdministration(app, server, "updates", AdministrationOperation.EnumerateUpdates,
                          AdministrationOperation.ControlUpdate);
        app.MapGet("/api/updates/check-state", () => new
        {
            Checking = Volatile.Read(ref services.Current.UpdateCheck) is { IsCompleted: false }
        });
        app.MapPost("/api/updates/check", async () =>
        {
            await StartUpdateCheck(services.Current);
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
        MapAdministration(app, server, "wsl", AdministrationOperation.EnumerateWslDistributions,
                          AdministrationOperation.ControlWslDistribution);
        MapAdministration(app, server, "proxy-vpn", AdministrationOperation.EnumerateProxyVpn,
                          AdministrationOperation.ControlProxyVpn);
        MapAdministration(app, server, "wlan", AdministrationOperation.EnumerateWlan,
                          AdministrationOperation.ControlWlan);
        MapAdministration(app, server, "network-shares/published",
                          AdministrationOperation.EnumeratePublishedShares,
                          AdministrationOperation.ControlPublishedShare);
        MapAdministration(app, server, "network-shares/connections",
                          AdministrationOperation.EnumerateNetworkConnections,
                          AdministrationOperation.ControlNetworkConnection);
        app.MapPost("/api/network-shares/published/query", (AdministrationIdentityRequest request) =>
            server.QueryAdministrationAsync(AdministrationOperation.QueryPublishedShare, request.Identity));
        app.MapPost("/api/network-shares/published/security/set", (ShareSecurityRequest request) =>
            server.ControlAdministrationStringDataAsync(
                AdministrationOperation.ControlPublishedShareSecurity,
                AdministrationAction.SetPermissions,
                request.DaclProtected ? 1u : 0,
                request.Identity,
                request.Sddl));
        MapAdministration(app, server, "network-adapters",
                          AdministrationOperation.EnumerateNetworkAdapters,
                          AdministrationOperation.ControlNetworkAdapter);
        MapAdministration(app, server, "network-routes",
                          AdministrationOperation.EnumerateNetworkRoutes,
                          AdministrationOperation.ControlNetworkRoute);
        MapAdministration(app, server, "network-endpoints",
                          AdministrationOperation.EnumerateNetworkEndpoints,
                          AdministrationOperation.ControlNetworkEndpoint);
        app.MapPost("/api/certificates", () =>
            server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateCertificates));
        app.MapPost("/api/certificates/stores", () =>
            server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateCertificateStores));
        app.MapPost("/api/certificates/install", async (HttpRequest request) =>
        {
            var (metadata, data) = await BinaryWebApi.ReadAsync<CertificateInstallRequest>(request, 0x000BF000);
            if (string.IsNullOrEmpty(metadata.StoreIdentity) || metadata.StoreIdentity.Length > 65535 ||
                metadata.StoreIdentity.Contains('\0') || data.Length == 0 ||
                metadata.Password is { Length: > 1024 } || metadata.Password?.Contains('\0') == true)
            {
                return Results.BadRequest();
            }
            try
            {
                await server.InstallCertificateDataAsync(metadata.StoreIdentity,
                                                         data,
                                                         metadata.Password,
                                                         metadata.Exportable);
                return Results.NoContent();
            }
            finally
            {
                CryptographicOperations.ZeroMemory(data);
            }
        });
        app.MapPost("/api/certificates/install-file", async (CertificateFileInstallRequest request) =>
        {
            if (string.IsNullOrEmpty(request.StoreIdentity) || request.StoreIdentity.Length > 65535 ||
                request.StoreIdentity.Contains('\0') || string.IsNullOrEmpty(request.Path) ||
                request.Path.Length > 32767 || request.Path.Contains('\0') ||
                request.Password is { Length: > 1024 } || request.Password?.Contains('\0') == true)
            {
                return Results.BadRequest();
            }
            await server.InstallCertificateFileAsync(request.StoreIdentity,
                                                     request.Path,
                                                     request.Password,
                                                     request.Exportable);
            return Results.NoContent();
        });
        app.MapPost("/api/certificates/delete", async (AdministrationIdentityRequest request) =>
        {
            if (string.IsNullOrEmpty(request.Identity) || request.Identity.Length > 65535 ||
                request.Identity.Contains('\0'))
            {
                return Results.BadRequest();
            }
            await server.ControlAdministrationDataAsync(
                AdministrationOperation.ControlCertificateData,
                AdministrationAction.Delete,
                0,
                request.Identity,
                []);
            return Results.NoContent();
        });
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
        app.MapPost("/api/clipboard/image", async () => Results.File(
            await server.QueryAdministrationDataAsync(AdministrationOperation.QueryClipboardImage),
            "image/jpeg"));
        app.MapPost("/api/clipboard/wait", (AdministrationIdentityRequest request) =>
            server.QueryAdministrationAsync(AdministrationOperation.WaitClipboard, request.Identity));
        app.MapPost("/api/wlan/profile", (AdministrationIdentityRequest request) =>
            server.QueryAdministrationAsync(AdministrationOperation.QueryWlanProfile, request.Identity));
        app.MapPost("/api/certificates/details", async (AdministrationIdentityRequest request) =>
        {
            var records = await server.QueryAdministrationAsync(
                AdministrationOperation.QueryCertificate,
                request.Identity);
            var record = records.Single(value => value.Kind == AdministrationKind.CertificateDetails);
            var rawData = await server.QueryAdministrationDataAsync(
                AdministrationOperation.QueryCertificateData,
                request.Identity);
            using var certificate = X509CertificateLoader.LoadCertificate(rawData);
            return CertificateDetails(certificate,
                                      record.Flags,
                                      record.State,
                                      records.Where(value => value.Kind == AdministrationKind.CertificateChain)
                                          .ToArray());
        });
        app.MapPost("/api/certificates/data", async (AdministrationIdentityRequest request) =>
            Results.Bytes(
                await server.QueryAdministrationDataAsync(
                    AdministrationOperation.QueryCertificateData,
                    request.Identity),
                "application/pkix-cert"));
    }

    private static async Task StreamFileContentAsync(HttpContext context, NativeServer server, string path)
    {
        var range = context.Request.GetTypedHeaders().Range;
        if (range is not null &&
            (!range.Unit.Equals("bytes", StringComparison.OrdinalIgnoreCase) || range.Ranges.Count != 1))
        {
            context.Response.StatusCode = StatusCodes.Status416RangeNotSatisfiable;
            return;
        }

        var requested = range?.Ranges.Single();
        var start = requested?.From is long from ? checked((ulong)from) : 0;
        var initialTransfer = await server.OpenFileReadAsync(path, start);
        var fileSize = initialTransfer.FileSize;
        FileTransfer transfer = initialTransfer;
        if (requested is { From: null, To: long suffixLength })
        {
            var suffix = checked((ulong)suffixLength);
            start = suffix >= fileSize ? 0 : fileSize - suffix;
            if (start != 0)
            {
                await initialTransfer.DisposeAsync();
                transfer = await server.OpenFileReadAsync(path, start);
            }
        }

        await using (transfer)
        {
            if (start > fileSize || start == fileSize && (fileSize != 0 || range is not null))
            {
                context.Response.Headers.ContentRange = $"bytes */{fileSize}";
                context.Response.StatusCode = StatusCodes.Status416RangeNotSatisfiable;
                return;
            }
            var end = fileSize == 0 ?
                          0 :
                          requested?.To is long to ? Math.Min(checked((ulong)to), fileSize - 1) : fileSize - 1;
            if (fileSize != 0 && end < start)
            {
                context.Response.Headers.ContentRange = $"bytes */{fileSize}";
                context.Response.StatusCode = StatusCodes.Status416RangeNotSatisfiable;
                return;
            }
            var length = fileSize == 0 ? 0 : end - start + 1;
            if (length > long.MaxValue)
            {
                context.Response.StatusCode = StatusCodes.Status413PayloadTooLarge;
                return;
            }

            context.Response.StatusCode = range is null ? StatusCodes.Status200OK : StatusCodes.Status206PartialContent;
            context.Response.ContentLength = (long)length;
            context.Response.ContentType = GetFileContentType(path);
            context.Response.Headers.AcceptRanges = "bytes";
            context.Response.Headers.CacheControl = "no-store";
            context.Response.Headers.XContentTypeOptions = "nosniff";
            context.Response.GetTypedHeaders().ContentDisposition = new ContentDispositionHeaderValue("inline")
            {
                FileNameStar = Path.GetFileName(path)
            };
            if (range is not null) context.Response.Headers.ContentRange = $"bytes {start}-{end}/{fileSize}";
            if (HttpMethods.IsHead(context.Request.Method) || length == 0) return;

            var remaining = length;
            await foreach (var data in transfer.Output.ReadAllAsync(context.RequestAborted))
            {
                using (data)
                {
                    var count = (int)Math.Min((ulong)data.Length, remaining);
                    await context.Response.Body.WriteAsync(data.Memory[..count], context.RequestAborted);
                    remaining -= (uint)count;
                    if (remaining == 0) break;
                }
            }
            if (remaining != 0)
            {
                var completion = await transfer.Completion;
                if (!completion.Status.IsSuccess) throw new NativeException(completion.Status);
                throw new EndOfStreamException();
            }
        }
    }

    private static object FilePageResult(FilePage page) => new
    {
        page.EnumerationId,
        Records = page.Records.Select(record => new
        {
            record.Name,
            record.Attributes,
            Size = record.Size.ToString(CultureInfo.InvariantCulture),
            record.CreationTime,
            record.LastAccessTime,
            record.LastWriteTime,
            record.HasChildren
        })
    };

    private static string GetFileContentType(string path) => Path.GetExtension(path).ToLowerInvariant() switch
    {
        ".jpg" or ".jpeg" or ".jfif" => "image/jpeg",
        ".png" => "image/png",
        ".gif" => "image/gif",
        ".webp" => "image/webp",
        ".avif" => "image/avif",
        ".bmp" => "image/bmp",
        ".tif" or ".tiff" => "image/tiff",
        ".svg" => "image/svg+xml",
        ".heic" => "image/heic",
        ".ico" => "image/x-icon",
        ".pdf" => "application/pdf",
        ".mp4" or ".m4v" => "video/mp4",
        ".mkv" => "video/x-matroska",
        ".avi" => "video/x-msvideo",
        ".mov" => "video/quicktime",
        ".wmv" => "video/x-ms-wmv",
        ".webm" => "video/webm",
        ".mpeg" or ".mpg" => "video/mpeg",
        ".mp3" => "audio/mpeg",
        ".wav" => "audio/wav",
        ".flac" => "audio/flac",
        ".aac" => "audio/aac",
        ".m4a" => "audio/mp4",
        ".ogg" or ".opus" => "audio/ogg",
        ".wma" => "audio/x-ms-wma",
        ".ttf" => "font/ttf",
        ".otf" => "font/otf",
        ".ttc" => "font/collection",
        _ => "application/octet-stream"
    };

    private static string BrowserCsv(object? value)
    {
        var text = Convert.ToString(value, CultureInfo.InvariantCulture) ?? string.Empty;

        // CSV formula-injection hardening: cells that spreadsheet apps may
        // interpret as formulas get a leading apostrophe (OWASP guidance)
        if (text.Length != 0 &&
            (text[0] is '=' or '+' or '-' or '@' or '\t' or '\r' or '\n' or '＝' or '＋' or '－' or '＠'))
        {
            text = "'" + text;
        }
        return $"\"{text.Replace("\"", "\"\"")}\"";
    }

    private static string BrowserCsvHeader(BrowserKind kind) => kind switch
    {
        BrowserKind.History => "Id,Url,Title,LastVisitTime,VisitCount,TypedCount",
        BrowserKind.Download =>
            "Id,Url,TargetPath,StartTime,EndTime,ReceivedBytes,TotalBytes,State,InterruptReason",
        BrowserKind.Bookmark or BrowserKind.Setting => "Path,Data",
        BrowserKind.Extension => "Id,Path",
        BrowserKind.Cookie =>
            "Id,Domain,Name,Value,Path,CreationTime,ExpirationTime,LastAccessTime,Secure,HttpOnly,SameSite,Encrypted,AppBound",
        BrowserKind.Password => "Id,Origin,Username,Password,CreationTime,Encrypted,AppBound",
        _ => throw new ArgumentOutOfRangeException(nameof(kind))
    };

    private static object?[] BrowserCsvValues(BrowserRecord record)
    {
        switch (record.Kind)
        {
            case BrowserKind.History:
            {
                var data = (BrowserHistoryData)record.Data!;

                return [record.Id, record.Identity, record.Name, data.LastVisitTime, data.VisitCount, data.TypedCount];
            }
            case BrowserKind.Download:
            {
                var data = (BrowserDownloadData)record.Data!;

                return [
                    record.Id,
                    record.Identity,
                    record.Name,
                    data.StartTime,
                    data.EndTime,
                    data.ReceivedBytes,
                    data.TotalBytes,
                    data.State,
                    data.InterruptReason
                ];
            }
            case BrowserKind.Bookmark:
            case BrowserKind.Setting:
                return [record.Location, record.Detail];
            case BrowserKind.Extension:
                return [record.Identity, record.Location];
            case BrowserKind.Cookie:
            {
                var data = (BrowserCookieData)record.Data!;

                return [
                    record.Id,
                    record.Identity,
                    record.Name,
                    record.Detail,
                    record.Location,
                    data.CreationTime,
                    data.ExpirationTime,
                    data.LastAccessTime,
                    (data.Flags & 1) != 0,
                    (data.Flags & 2) != 0,
                    data.SameSite,
                    (data.Flags & 4) != 0,
                    (data.Flags & 8) != 0
                ];
            }
            case BrowserKind.Password:
            {
                var data = (BrowserPasswordData)record.Data!;

                return [
                    record.Id,
                    record.Identity,
                    record.Name,
                    record.Detail,
                    data.CreationTime,
                    (data.Flags & 4) != 0,
                    (data.Flags & 8) != 0
                ];
            }
            default:
                throw new InvalidDataException("The client returned an invalid browser record kind.");
        }
    }

    private static X509Certificate2 LoadCertificate(byte[] data)
    {
        try
        {
            return X509CertificateLoader.LoadCertificate(data);
        }
        catch (CryptographicException)
        {
            return X509Certificate2.CreateFromPem(Encoding.UTF8.GetString(data));
        }
    }

    private static object CertificateDetails(X509Certificate2 certificate,
                                             uint flags,
                                             uint chainError,
                                             AdministrationRecord[] chain)
    {
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
            Flags = flags,
            ChainError = chainError,
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
            Chain = chain
        };
    }

    private static void ValidatePortable(params string?[] values)
    {
        if (values.Any(value => string.IsNullOrEmpty(value) || value.Length > 1024 || value.Contains('\0')))
        {
            throw new BadHttpRequestException("便携设备参数无效");
        }
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

    private static Task StartUpdateCheck(ClientServices client)
    {
        lock (client.UpdateCheckLock)
        {
            if (client.UpdateCheck is not { IsCompleted: false })
            {
                client.UpdateCheck = client.Server.ControlAdministrationAsync(
                    AdministrationOperation.ControlUpdate,
                    AdministrationAction.Check);
            }
            return client.UpdateCheck;
        }
    }

    private static void MapAdministration(
        WebApplication app,
        NativeServer server,
        string path,
        AdministrationOperation enumerate,
        AdministrationOperation control)
    {
        app.MapPost($"/api/{path}", () => server.EnumerateAdministrationAsync(enumerate));
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

    private static async Task<AdministrationRecord[]> EnumerateWslProcessesAsync(NativeServer server)
    {
        try
        {
            return await server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateWslProcesses);
        }
        catch (NativeException)
        {
            return [];
        }
    }
}

internal sealed record PathRequest(string Path);
internal sealed record SecurityDescriptorRequest(string Path, string Sddl, bool DaclProtected);
internal sealed record ShareSecurityRequest(string Identity, string Sddl, bool DaclProtected);
internal sealed record SecurityAccountRequest(string Value, bool Sid);
internal sealed record FilePageRequest(string? Path, string? EnumerationId);
internal sealed record FilePickerPageRequest(
    string? Path,
    string? Query,
    string? Group,
    uint EnumerationId);
internal sealed record FileEnumerationCloseRequest(uint EnumerationId);
internal sealed record FileArchivePageRequest(string? Path, string? EnumerationId);
internal sealed record FileImagePreviewRequest(string Path, FileImagePreviewQuality Quality);
internal sealed record PortablePageRequest(string DeviceId, string? ParentId, uint Offset);
internal sealed record PortableObjectRequest(string DeviceId, string ObjectId);
internal sealed record PortableNameRequest(string DeviceId, string ObjectId, string Name);
internal sealed record FileRenameRequest(string Path, string NewPath);
internal sealed record FileHashRequest(string Path, FileHashAlgorithm Algorithm);
internal sealed record FileRangeRequest(string Path, string Offset, uint Length);
internal sealed record FileRangeWriteRequest(string Path, string Offset);
internal sealed record ProcessMemoryReadRequest(uint ProcessId, string CreateTime, string Address, uint Length);
internal sealed record ProcessMemoryWriteRequest(uint ProcessId, string CreateTime, string Address);
internal sealed record ProcessMemoryMapRequest(uint SnapshotId, uint AllocationIndex);
internal sealed record ProcessMemoryMapCloseRequest(uint SnapshotId);
internal sealed record WinObjRequest(string Path);
internal sealed record UiAutomationRequest(string Identity);
internal sealed record FileAttributesRequest(string Path, uint Attributes);
internal sealed record FileOpenRequest(string Path, bool Hidden);
internal sealed record FileOwnerControlRequest(
    string Path,
    FileOwnerControl Control,
    uint[] ProcessIds);
internal sealed record FileVolumeLabelRequest(string Path, string Label);
internal sealed record FileVolumeFormatRequest(string Path, string FileSystem, string Label, bool Quick);
internal sealed record FileSearchRequest(string Path, string Query, uint Mode);
internal sealed record FileUrlDownloadRequest(
    string Directory,
    string Url,
    string Name,
    FileDownloadEngine Engine,
    bool Overwrite);
internal sealed record FileUrlDownloadCancelRequest(string Id);
internal sealed record AppContainerControlRequest(
    AdministrationAction Action,
    string Identity,
    string? DisplayName,
    string? Description,
    string[]? Capabilities,
    bool? Loopback);
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
    byte FrameRate,
    byte ImageQuality)
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
internal sealed record BrowserDocumentOpenRequest(BrowserType Browser, BrowserKind Kind, string Profile);
internal sealed record BrowserDocumentNodeRequest(uint SnapshotId, uint NodeId, uint Cursor);
internal sealed record BrowserDocumentCloseRequest(uint SnapshotId);
internal sealed record WmiNamespaceRequest(string Namespace);
internal sealed record WmiQueryRequest(string Namespace, string Query, uint Limit, bool SystemProperties);
internal sealed record AdministrationControlRequest(
    AdministrationAction Action,
    string? Identity,
    string? Argument,
    string? Secret);
internal sealed record CertificateInstallRequest(string StoreIdentity, string? Password, bool Exportable);
internal sealed record CertificateFileInstallRequest(
    string StoreIdentity,
    string Path,
    string? Password,
    bool Exportable);
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
    string[] ServiceNames,
    string? WslIdentity,
    string? WslDistribution,
    char? WslState,
    uint? WslUserId,
    uint? WslElapsedSeconds)
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
            value.CreateTime.ToString(CultureInfo.InvariantCulture),
            value.UserTime.ToString(CultureInfo.InvariantCulture),
            value.KernelTime.ToString(CultureInfo.InvariantCulture),
            value.WorkingSetBytes.ToString(CultureInfo.InvariantCulture),
            value.PrivateBytes.ToString(CultureInfo.InvariantCulture),
            value.ImageName,
            value.UserName,
            value.ImagePath,
            value.ServiceNames,
            null,
            null,
            null,
            null,
            null);

    internal static ProcessWebRecord From(AdministrationRecord value)
    {
        var firstSeparator = value.Identity.IndexOf('\n');
        var secondSeparator = value.Identity.IndexOf('\n', firstSeparator + 1);
        if (firstSeparator <= 0 || secondSeparator <= firstSeparator + 1 ||
            !uint.TryParse(value.Identity.AsSpan(firstSeparator + 1,
                                                secondSeparator - firstSeparator - 1),
                           NumberStyles.None,
                           CultureInfo.InvariantCulture,
                           out var processId) ||
            !ulong.TryParse(value.Value, NumberStyles.None, CultureInfo.InvariantCulture, out var packed) ||
            value.State > char.MaxValue)
        {
            throw new InvalidDataException("Invalid WSL process record.");
        }
        return new(processId,
                   (uint)packed,
                   0,
                   0,
                   0,
                   0,
                   0,
                   0,
                   value.Identity,
                   "0",
                   "0",
                   "0",
                   "0",
                   value.Name,
                   $"UID {value.Flags}",
                   value.Detail,
                   [],
                   value.Identity,
                   value.Description,
                   (char)value.State,
                   value.Flags,
                   (uint)(packed >> 32));
    }

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
            value.UserTime.ToString(CultureInfo.InvariantCulture),
            value.KernelTime.ToString(CultureInfo.InvariantCulture),
            value.WorkingSetBytes.ToString(CultureInfo.InvariantCulture),
            value.PrivateBytes.ToString(CultureInfo.InvariantCulture),
            value.ImageBaseStatus,
            value.ImageBase.ToString(CultureInfo.InvariantCulture),
            value.ImageName,
            value.UserName,
            value.ImagePathStatus,
            value.ImagePath,
            value.CommandLineStatus,
            value.CommandLine);
}
