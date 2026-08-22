using KNSoft.ZPigeon.Server.Managed;
using System.Net.WebSockets;

namespace KNSoft.ZPigeon.Web;

internal static class WindowCaptureWebSocket
{
    internal static async Task RunAsync(HttpContext context, NativeServer server)
    {
        var desktop = bool.TryParse(context.Request.Query["desktop"], out var desktopValue) && desktopValue;
        if (!context.WebSockets.IsWebSocketRequest ||
            !ulong.TryParse(context.Request.Query["handle"], out var handle) ||
            !uint.TryParse(context.Request.Query["processId"], out var processId) ||
            !uint.TryParse(context.Request.Query["threadId"], out var threadId) ||
            !bool.TryParse(context.Request.Query["captureCursor"], out var captureCursor) ||
            !uint.TryParse(context.Request.Query["maxDimension"], out var maxDimension) ||
            !ushort.TryParse(context.Request.Query["frameRate"], out var frameRate) ||
            !ushort.TryParse(context.Request.Query["imageQuality"], out var imageQuality) ||
            !uint.TryParse(context.Request.Query["monitorIndex"], out var monitorIndex) ||
            !uint.TryParse(context.Request.Query["directStreamId"], out var directStreamId))
        {
            context.Response.StatusCode = StatusCodes.Status400BadRequest;
            return;
        }
        using var socket = await context.WebSockets.AcceptWebSocketAsync();
        try
        {
            await using var capture = await server.OpenWindowCaptureAsync(
                handle,
                processId,
                threadId,
                new(captureCursor, maxDimension, frameRate, imageQuality, desktop, monitorIndex),
                directStreamId);
            var input = desktop ? ReceiveInputAsync(socket, capture, context.RequestAborted) :
                                  StreamWebSocket.WaitForCloseAsync(socket, context.RequestAborted);
            var output = directStreamId != 0 ? capture.Completion :
                                               SendOutputAsync(socket, capture, context.RequestAborted);
            var completed = await Task.WhenAny(capture.Completion, input, output);
            if (completed == input)
            {
                await input;
                return;
            }
            if (completed == output) await output;
            var completion = await capture.Completion;
            if (socket.State == WebSocketState.Open)
            {
                await socket.CloseAsync(
                    completion.Status.IsSuccess ? WebSocketCloseStatus.NormalClosure :
                                                  WebSocketCloseStatus.InternalServerError,
                    completion.Status.IsSuccess ? null :
                        $"ZPigeon {completion.Status.Type}: 0x{completion.Status.Code:X8}",
                    CancellationToken.None);
            }
        }
        catch (NativeException exception)
        {
            if (socket.State == WebSocketState.Open)
            {
                await socket.CloseAsync(WebSocketCloseStatus.InternalServerError,
                                        exception.Message,
                                        CancellationToken.None);
            }
        }
        catch (OperationCanceledException) when (context.RequestAborted.IsCancellationRequested)
        {
        }
        catch (WebSocketException)
        {
        }
    }

    private static async Task SendOutputAsync(
        WebSocket socket,
        WindowCaptureStream capture,
        CancellationToken cancellationToken)
    {
        await foreach (var data in capture.Output.ReadAllAsync(cancellationToken))
        {
            await socket.SendAsync(data, WebSocketMessageType.Binary, true, cancellationToken);
        }
    }

    private static async Task ReceiveInputAsync(
        WebSocket socket,
        WindowCaptureStream capture,
        CancellationToken cancellationToken)
    {
        const int maximumLength = 0x00100002;
        var buffer = GC.AllocateUninitializedArray<byte>(maximumLength);
        while (socket.State == WebSocketState.Open)
        {
            var length = 0;
            ValueWebSocketReceiveResult result;
            do
            {
                result = await socket.ReceiveAsync(buffer.AsMemory(length), cancellationToken);
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    await socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure,
                                                  null,
                                                  CancellationToken.None);
                    return;
                }
                if (result.MessageType != WebSocketMessageType.Binary || result.Count == 0)
                {
                    await socket.CloseAsync(WebSocketCloseStatus.InvalidMessageType,
                                            "远程控制消息无效",
                                            CancellationToken.None);
                    return;
                }
                length += result.Count;
                if (!result.EndOfMessage && length == buffer.Length)
                {
                    await socket.CloseAsync(WebSocketCloseStatus.MessageTooBig,
                                            "远程控制消息过大",
                                            CancellationToken.None);
                    return;
                }
            }
            while (!result.EndOfMessage);
            capture.Send(buffer.AsSpan(0, length));
        }
    }
}
