using KNSoft.ZPigeon.Server.Managed;
using Microsoft.Net.Http.Headers;

namespace KNSoft.ZPigeon.Web;

internal static class ManagementWebApi
{
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
        app.MapPost("/api/file/hash", async (FileHashRequest request) =>
            await server.HashFileAsync(request.Path, request.Algorithm));
        app.MapPost("/api/file/delete", async (PathRequest request) =>
            await server.DeleteFileAsync(request.Path));
        app.MapPost("/api/file/rename", async (FileRenameRequest request) =>
            await server.RenameFileAsync(request.Path, request.NewPath));
        app.MapPost("/api/file/attributes", async (FileAttributesRequest request) =>
            await server.SetFileAttributesAsync(request.Path, request.Attributes));
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
        app.MapPost("/api/process/terminate", async (ProcessIdentityRequest request) =>
            await server.TerminateProcessAsync(request.ProcessId, ulong.Parse(request.CreateTime)));
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
    }
}

internal sealed record PathRequest(string Path);
internal sealed record FilePageRequest(string? Path, string? EnumerationId);
internal sealed record FileRenameRequest(string Path, string NewPath);
internal sealed record FileHashRequest(string Path, FileHashAlgorithm Algorithm);
internal sealed record FileAttributesRequest(string Path, uint Attributes);
internal sealed record ServiceRequest(string ServiceName);
internal sealed record ServiceControlRequest(string ServiceName, ServiceControl Control, string? Argument);
internal sealed record ProcessIdentityRequest(uint ProcessId, string CreateTime);
internal sealed record WindowIdentityRequest(string Handle, uint ProcessId, uint ThreadId);
internal sealed record WindowControlRequest(
    string Handle,
    uint ProcessId,
    uint ThreadId,
    WindowControl Control);
internal sealed record ProcessWebRecord(
    uint ProcessId,
    uint ParentProcessId,
    uint SessionId,
    uint ThreadCount,
    uint HandleCount,
    string CreateTime,
    string UserTime,
    string KernelTime,
    string WorkingSetBytes,
    string PrivateBytes,
    string ImageName)
{
    internal static ProcessWebRecord From(ProcessRecord value) =>
        new(value.ProcessId,
            value.ParentProcessId,
            value.SessionId,
            value.ThreadCount,
            value.HandleCount,
            value.CreateTime.ToString(),
            value.UserTime.ToString(),
            value.KernelTime.ToString(),
            value.WorkingSetBytes.ToString(),
            value.PrivateBytes.ToString(),
            value.ImageName);
}

internal sealed record ProcessInfoWebRecord(
    uint ProcessId,
    uint ParentProcessId,
    uint SessionId,
    uint ThreadCount,
    uint HandleCount,
    DateTime CreateTime,
    string UserTime,
    string KernelTime,
    string WorkingSetBytes,
    string PrivateBytes,
    string ImageName,
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
            value.CreateTime,
            value.UserTime.ToString(),
            value.KernelTime.ToString(),
            value.WorkingSetBytes.ToString(),
            value.PrivateBytes.ToString(),
            value.ImageName,
            value.ImagePathStatus,
            value.ImagePath,
            value.CommandLineStatus,
            value.CommandLine);
}
