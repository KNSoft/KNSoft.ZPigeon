using KNSoft.ZPigeon.Server.Managed;
using System.Net.WebSockets;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace KNSoft.ZPigeon.Web;

internal static class RtcWebSocket
{
    private const int MaximumSignalLength = 512 * 1024;

    internal static async Task RunAsync(HttpContext context, NativeServer server, string[] iceServers)
    {
        if (!context.WebSockets.IsWebSocketRequest)
        {
            context.Response.StatusCode = StatusCodes.Status400BadRequest;
            return;
        }
        using var socket = await context.WebSockets.AcceptWebSocketAsync();
        var sessionId = Guid.NewGuid();
        var opened = false;
        try
        {
            await socket.SendAsync(JsonSerializer.SerializeToUtf8Bytes(new { iceServers }),
                                   WebSocketMessageType.Text,
                                   true,
                                   context.RequestAborted);
            var signal = await ReceiveAsync(socket, context.RequestAborted);
            var offer = JsonSerializer.Deserialize<RtcOffer>(signal)?.Offer;
            if (string.IsNullOrEmpty(offer)) throw new InvalidDataException("无效的 WebRTC Offer");
            var answer = await server.OpenRtcAsync(sessionId, offer, iceServers);
            opened = true;
            await socket.SendAsync(JsonSerializer.SerializeToUtf8Bytes(new { answer }),
                                   WebSocketMessageType.Text,
                                   true,
                                   context.RequestAborted);
            while (socket.State == WebSocketState.Open)
            {
                var message = await socket.ReceiveAsync(new byte[1], context.RequestAborted);
                if (message.MessageType == WebSocketMessageType.Close) break;
            }
        }
        catch (OperationCanceledException) when (context.RequestAborted.IsCancellationRequested)
        {
        }
        catch (Exception exception) when (exception is NativeException or InvalidDataException or WebSocketException)
        {
            if (socket.State == WebSocketState.Open)
            {
                var message = exception.Message;
                if (message.Length > 100) message = message[..100];
                await socket.CloseAsync(WebSocketCloseStatus.InternalServerError,
                                        message,
                                        CancellationToken.None);
            }
        }
        finally
        {
            if (opened)
            {
                try
                {
                    await server.CloseRtcAsync(sessionId);
                }
                catch (NativeException)
                {
                }
            }
        }
    }

    private static async Task<byte[]> ReceiveAsync(WebSocket socket, CancellationToken cancellationToken)
    {
        using var stream = new MemoryStream();
        var buffer = new byte[16 * 1024];
        WebSocketReceiveResult result;
        do
        {
            result = await socket.ReceiveAsync(buffer, cancellationToken);
            if (result.MessageType != WebSocketMessageType.Text || stream.Length + result.Count > MaximumSignalLength)
            {
                throw new InvalidDataException("无效的 WebRTC 信令");
            }
            stream.Write(buffer, 0, result.Count);
        }
        while (!result.EndOfMessage);
        return stream.ToArray();
    }

    private sealed record RtcOffer([property: JsonPropertyName("offer")] string Offer);
}
