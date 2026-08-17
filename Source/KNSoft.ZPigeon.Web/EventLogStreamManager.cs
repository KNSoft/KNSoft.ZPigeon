using System.Collections.Concurrent;
using System.Text;
using KNSoft.ZPigeon.Server.Managed;
using Microsoft.Net.Http.Headers;

namespace KNSoft.ZPigeon.Web;

internal sealed class EventLogStreamManager(NativeServer server) : IDisposable
{
    private readonly ConcurrentDictionary<Guid, CancellationTokenSource> streams = [];

    internal async Task StreamAsync(
        HttpContext context,
        Guid id,
        string channelPath,
        string? query,
        string? name)
    {
        using var stop = new CancellationTokenSource();
        if (!streams.TryAdd(id, stop))
        {
            context.Response.StatusCode = StatusCodes.Status409Conflict;
            return;
        }
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(
            stop.Token,
            context.RequestAborted);
        var token = cancellation.Token;
        try
        {
            var page = await server.QueryEventLogPageAsync(channelPath, query, null, 1);
            var bookmark = page.Records.FirstOrDefault()?.Bookmark;
            context.Response.ContentType = "text/plain; charset=utf-8";
            context.Response.GetTypedHeaders().ContentDisposition =
                new ContentDispositionHeaderValue("attachment")
                {
                    FileNameStar = CreateFileName(name ?? channelPath)
                };
            await using var writer = new StreamWriter(
                context.Response.Body,
                new UTF8Encoding(true),
                leaveOpen: true);
            await writer.WriteLineAsync("# KNSoft ZPigeon EventLog Stream".AsMemory(), token);
            await writer.WriteLineAsync($"# Channel: {channelPath}".AsMemory(), token);
            if (!string.IsNullOrWhiteSpace(query))
            {
                await writer.WriteLineAsync($"# Query: {query}".AsMemory(), token);
            }
            await writer.WriteLineAsync($"# Started: {DateTimeOffset.Now:O}".AsMemory(), token);
            await writer.WriteLineAsync(ReadOnlyMemory<char>.Empty, token);
            await writer.FlushAsync(token);
            while (!token.IsCancellationRequested)
            {
                await Task.Delay(TimeSpan.FromSeconds(1), token);
                do
                {
                    page = await server.QueryEventLogPageAsync(
                        channelPath,
                        query,
                        bookmark,
                        256,
                        true);
                    foreach (var record in page.Records)
                    {
                        await writer.WriteLineAsync(record.Xml.AsMemory(), token);
                    }
                    if (page.Records.Length != 0)
                    {
                        bookmark = page.NextBookmark;
                        await writer.FlushAsync(token);
                    }
                }
                while (page.HasMore && !token.IsCancellationRequested);
            }
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested)
        {
        }
        catch (NativeException exception) when (context.Response.HasStarted)
        {
            var error = $"[ZPigeon Error: {exception.Status.Type} 0x{exception.Status.Code:X8}]";
            await context.Response.WriteAsync(error + Environment.NewLine, CancellationToken.None);
        }
        finally
        {
            streams.TryRemove(id, out _);
        }
    }

    internal bool Stop(Guid id) =>
        streams.TryGetValue(id, out var stream) && Cancel(stream);

    public void Dispose()
    {
        foreach (var stream in streams.Values)
        {
            stream.Cancel();
        }
    }

    private static bool Cancel(CancellationTokenSource stream)
    {
        stream.Cancel();
        return true;
    }

    private static string CreateFileName(string value)
    {
        var invalid = Path.GetInvalidFileNameChars();
        var name = new string(value.Trim().Select(character =>
            invalid.Contains(character) ? '_' : character).ToArray());
        return $"{(name.Length == 0 ? "Events" : name)}-{DateTime.Now:yyyyMMdd-HHmmss}.log";
    }
}
