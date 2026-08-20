using System.Collections.Concurrent;
using System.Text;
using KNSoft.ZPigeon.Server.Managed;

namespace KNSoft.ZPigeon.Web;

internal static class ExecutionWebApi
{
    private static readonly ConcurrentDictionary<string, string> CleanupPaths = [];

    internal static void MapExecutionApi(
        this WebApplication app,
        NativeServer server,
        TerminalWebSessionManager terminals)
    {
        app.MapPost("/api/execution/sessions", async () =>
            await server.EnumerateExecutionSessionsAsync());
        app.MapPost("/api/execution/jobs", async () =>
        {
            var jobs = await server.EnumerateExecutionJobsAsync();
            foreach (var job in jobs)
            {
                if (job.State == ExecutionJobState.Exited && CleanupPaths.TryRemove(job.JobId, out var path))
                {
                    try
                    {
                        await server.DeleteFileAsync(path);
                    }
                    catch (NativeException)
                    {
                    }
                }
            }
            return jobs;
        });
        app.MapPost("/api/execution/start", async (ExecutionStartRequest request) =>
        {
            if (!Enum.IsDefined(request.Engine) || !Enum.IsDefined(request.Identity))
            {
                return Results.BadRequest();
            }
            var start = request.ToExecutionStart();
            var job = await server.StartExecutionAsync(start);
            if (!string.IsNullOrEmpty(request.CleanupPath))
            {
                CleanupPaths[job.JobId] = request.CleanupPath;
            }
            return Results.Ok(job);
        });
        app.MapPost("/api/execution/terminate", async (ExecutionJobRequest request) =>
        {
            if (!uint.TryParse(request.JobId, out var jobId))
            {
                return Results.BadRequest();
            }
            await server.TerminateExecutionAsync(jobId);
            return Results.NoContent();
        });
        app.MapPost("/api/execution/staging", async (ExecutionStagingRequest request) =>
        {
            var name = Path.GetFileName(request.Name);
            return name.Length is 0 or > 260 ?
                Results.BadRequest() :
                Results.Ok(new { Path = await server.CreateExecutionStagingAsync(name) });
        });
        app.MapPost("/api/terminal/script", async (TerminalScriptRequest request) =>
        {
            if (!Enum.IsDefined(request.Shell) || request.Columns == 0 || request.Rows == 0 ||
                request.Script.Length is 0 or > 0x00400000)
            {
                return Results.BadRequest();
            }
            var extension = request.Extension.ToLowerInvariant();
            if (request.Shell == TerminalShell.CommandPrompt ?
                    extension is not (".cmd" or ".bat") :
                    extension != ".ps1")
            {
                return Results.BadRequest();
            }
            var path = await server.CreateExecutionStagingAsync("Script" + extension);
            var encoding = request.Shell == TerminalShell.WindowsPowerShell ?
                new UTF8Encoding(true) :
                new UTF8Encoding(false);
            try
            {
                await UploadAsync(server, path, encoding.GetBytes(request.Script));
                return Results.Ok(await terminals.CreateScriptAsync(
                    request.Shell,
                    path,
                    request.Columns,
                    request.Rows));
            }
            catch
            {
                try
                {
                    await server.DeleteFileAsync(path);
                }
                catch (NativeException)
                {
                }
                throw;
            }
        });
    }

    private static async Task UploadAsync(NativeServer server, string path, byte[] data)
    {
        await using var transfer = await server.OpenFileWriteAsync(path, (ulong)data.Length, false);
        await transfer.WriteAsync(data);
        var status = (await transfer.Completion).Status;
        if (!status.IsSuccess)
        {
            throw new NativeException(status);
        }
    }
}

internal sealed record ExecutionStartRequest(
    ExecutionEngine Engine,
    ExecutionIdentity Identity,
    uint SessionId,
    ExecutionFlags Flags,
    string FileName,
    string? Arguments,
    string? WorkingDirectory,
    string? Verb,
    string? UserName,
    string? Password,
    string? CleanupPath)
{
    internal ExecutionStart ToExecutionStart() =>
        new(Engine,
            Identity,
            SessionId,
            Flags,
            FileName,
            Arguments,
            WorkingDirectory,
            Verb,
            UserName,
            Password);
}

internal sealed record ExecutionJobRequest(string JobId);
internal sealed record ExecutionStagingRequest(string Name);
internal sealed record TerminalScriptRequest(
    TerminalShell Shell,
    string Extension,
    string Script,
    ushort Columns,
    ushort Rows);
