using KNSoft.ZPigeon.Server.Managed;
using Microsoft.Net.Http.Headers;

namespace KNSoft.ZPigeon.Web;

internal static class ManagementWebApi
{
    internal static void MapManagementApi(this WebApplication app, NativeServer server)
    {
        app.MapPost("/api/files", async (FilePageRequest request) =>
            await server.EnumerateFilesPageAsync(request.Path, request.Cursor, request.MaxEntries));
        app.MapPost("/api/file/info", async (PathRequest request) =>
            await server.QueryFileAsync(request.Path));
        app.MapPost("/api/file/hash", async (PathRequest request) =>
            await server.HashFileAsync(request.Path));
        app.MapPost("/api/file/delete", async (PathRequest request) =>
            await server.DeleteFileAsync(request.Path));
        app.MapPost("/api/file/rename", async (FileRenameRequest request) =>
            await server.RenameFileAsync(request.Path, request.NewPath));
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
        app.MapPost("/api/services", async () => await server.EnumerateServicesAsync());
        app.MapPost("/api/service/info", async (ServiceRequest request) =>
            await server.QueryServiceAsync(request.ServiceName));
        app.MapPost("/api/service/control", async (ServiceControlRequest request) =>
            await server.SetServiceRunningAsync(request.ServiceName, request.Running));
    }
}

internal sealed record PathRequest(string Path);
internal sealed record FilePageRequest(string Path, string? Cursor, uint MaxEntries);
internal sealed record FileRenameRequest(string Path, string NewPath);
internal sealed record ServiceRequest(string ServiceName);
internal sealed record ServiceControlRequest(string ServiceName, bool Running);
internal sealed record ProcessIdentityRequest(uint ProcessId, string CreateTime);
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
