using KNSoft.ZPigeon.Server.Managed;
using System.Net.WebSockets;

namespace KNSoft.ZPigeon.Web;

internal static class WindowCaptureWebSocket
{
    internal static async Task RunAsync(HttpContext context, NativeServer server)
    {
        if (!context.WebSockets.IsWebSocketRequest ||
            !ulong.TryParse(context.Request.Query["handle"], out var handle) ||
            !uint.TryParse(context.Request.Query["processId"], out var processId) ||
            !uint.TryParse(context.Request.Query["threadId"], out var threadId) ||
            !bool.TryParse(context.Request.Query["captureCursor"], out var captureCursor) ||
            !uint.TryParse(context.Request.Query["maxDimension"], out var maxDimension) ||
            !ushort.TryParse(context.Request.Query["frameRate"], out var frameRate) ||
            !ushort.TryParse(context.Request.Query["imageQuality"], out var imageQuality))
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
                new(captureCursor, maxDimension, frameRate, imageQuality));
            await foreach (var data in capture.Output.ReadAllAsync(context.RequestAborted))
            {
                await socket.SendAsync(data,
                                       WebSocketMessageType.Binary,
                                       true,
                                       context.RequestAborted);
            }
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
}
