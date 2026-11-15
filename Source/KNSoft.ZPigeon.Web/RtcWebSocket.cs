using KNSoft.ZPigeon.Server.Managed;
using System.Net.WebSockets;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace KNSoft.ZPigeon.Web;

internal static class RtcWebSocket
{
    private const int MaximumSignalLength = 512 * 1024;
    private const int CloseMilliseconds = 5000;

    internal static async Task RunAsync(
        HttpContext context,
        NativeServer server,
        StunServer stun,
        string[] configuredIceServers,
        string proxyUserHeader)
    {
        if (!RemoteAccessWebApi.IsAuthenticated(context, proxyUserHeader))
        {
            context.Response.StatusCode = StatusCodes.Status401Unauthorized;
            return;
        }
        if (!context.WebSockets.IsWebSocketRequest)
        {
            context.Response.StatusCode = StatusCodes.Status400BadRequest;
            return;
        }
        var managementAddress = context.Connection.RemoteIpAddress;
        if (managementAddress == null)
        {
            context.Response.StatusCode = StatusCodes.Status403Forbidden;
            return;
        }
        var iceServers = configuredIceServers.Prepend(stun.Url(context)).Distinct().ToArray();
        using var lease = stun.Authorize(managementAddress, server.GetClientAddress());
        using var socket = await context.WebSockets.AcceptWebSocketAsync();
        using var negotiation = CancellationTokenSource.CreateLinkedTokenSource(context.RequestAborted);
        negotiation.CancelAfter(StunServer.LeaseMilliseconds);
        var sessionId = Guid.NewGuid();
        Task<string>? opening = null;
        try
        {
            await socket.SendAsync(JsonSerializer.SerializeToUtf8Bytes(new { iceServers }),
                                   WebSocketMessageType.Text,
                                   true,
                                   negotiation.Token);
            var signal = await ReceiveAsync(socket, negotiation.Token);
            var offer = JsonSerializer.Deserialize<RtcOffer>(signal)?.Offer;
            if (string.IsNullOrEmpty(offer)) throw new InvalidDataException("无效的 WebRTC Offer");
            using (server.SelectCancellation(CancellationToken.None))
            {
                opening = server.OpenRtcAsync(sessionId, offer, iceServers);
            }
            var answer = await opening.WaitAsync(negotiation.Token);
            await socket.SendAsync(JsonSerializer.SerializeToUtf8Bytes(new { answer }),
                                   WebSocketMessageType.Text,
                                   true,
                                   negotiation.Token);
            lease.Dispose();
            var closeBuffer = new byte[1];
            while (socket.State == WebSocketState.Open)
            {
                var message = await socket.ReceiveAsync(closeBuffer, context.RequestAborted);
                if (message.MessageType == WebSocketMessageType.Close) break;
                await CloseOutputAsync(socket, WebSocketCloseStatus.InvalidMessageType, "WebRTC 信令已完成");
                break;
            }
        }
        catch (OperationCanceledException) when (negotiation.IsCancellationRequested)
        {
            if (!context.RequestAborted.IsCancellationRequested)
            {
                await CloseOutputAsync(socket, WebSocketCloseStatus.PolicyViolation, "P2P 协商超时");
            }
        }
        catch (Exception exception) when (
            exception is NativeException or InvalidDataException or JsonException or WebSocketException)
        {
            if (socket.State == WebSocketState.Open)
            {
                var message = exception.Message;
                if (message.Length > 40) message = message[..40];
                await CloseOutputAsync(socket, WebSocketCloseStatus.InternalServerError, message);
            }
        }
        finally
        {
            if (opening != null)
            {
                if (opening.IsCompleted)
                {
                    await CloseSessionAsync(server, sessionId, opening);
                }
                else
                {
                    // A timed-out offer can still create a native session; close it when the request finishes.
                    _ = CloseSessionAsync(server, sessionId, opening);
                }
            }
        }
    }

    private static async Task CloseSessionAsync(NativeServer server, Guid sessionId, Task<string> opening)
    {
        try
        {
            await opening;
        }
        catch (Exception exception) when (exception is NativeException or OperationCanceledException)
        {
        }
        using var cleanup = new CancellationTokenSource(CloseMilliseconds);
        using var selection = server.SelectCancellation(cleanup.Token);
        try
        {
            await server.CloseRtcAsync(sessionId).WaitAsync(cleanup.Token);
        }
        catch (Exception exception) when (exception is NativeException or OperationCanceledException)
        {
        }
    }

    private static async Task CloseOutputAsync(WebSocket socket, WebSocketCloseStatus status, string message)
    {
        if (socket.State != WebSocketState.Open) return;
        using var timeout = new CancellationTokenSource(CloseMilliseconds);
        try
        {
            await socket.CloseOutputAsync(status, message, timeout.Token);
        }
        catch (Exception exception) when (exception is OperationCanceledException or WebSocketException)
        {
            socket.Abort();
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
