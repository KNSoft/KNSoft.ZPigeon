using KNSoft.ZPigeon.Server.Managed;
using System.Net.WebSockets;

namespace KNSoft.ZPigeon.Web;

internal static class AudioWebSocket
{
    internal static async Task RunAsync(HttpContext context, NativeServer server)
    {
        if (!context.WebSockets.IsWebSocketRequest ||
            !Enum.TryParse<AudioFlow>(context.Request.Query["flow"], true, out var flow) ||
            !Enum.IsDefined(flow) ||
            !uint.TryParse(context.Request.Query["directStreamId"], out var directStreamId))
        {
            context.Response.StatusCode = StatusCodes.Status400BadRequest;
            return;
        }
        var deviceId = context.Request.Query["deviceId"].ToString();
        if (deviceId.Length > 1024)
        {
            context.Response.StatusCode = StatusCodes.Status400BadRequest;
            return;
        }
        using var socket = await context.WebSockets.AcceptWebSocketAsync();
        try
        {
            await using var audio = await server.OpenAudioStreamAsync(flow,
                                                                      string.IsNullOrEmpty(deviceId) ? null : deviceId,
                                                                      directStreamId);
            if (directStreamId != 0)
            {
                if (await Task.WhenAny(audio.Completion,
                                       StreamWebSocket.WaitForCloseAsync(socket, context.RequestAborted)) !=
                    audio.Completion)
                {
                    return;
                }
            }
            else await foreach (var data in audio.Output.ReadAllAsync(context.RequestAborted))
            {
                using (data)
                    await socket.SendAsync(data.Memory, WebSocketMessageType.Binary, true, context.RequestAborted);
            }
            var completion = await audio.Completion;
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
