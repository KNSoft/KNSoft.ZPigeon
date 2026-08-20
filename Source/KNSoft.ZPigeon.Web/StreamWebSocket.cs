using System.Net.WebSockets;

namespace KNSoft.ZPigeon.Web;

internal static class StreamWebSocket
{
    internal static async Task WaitForCloseAsync(WebSocket socket, CancellationToken cancellationToken)
    {
        var buffer = new byte[1];
        try
        {
            while (socket.State == WebSocketState.Open)
            {
                var result = await socket.ReceiveAsync(buffer, cancellationToken);
                if (result.MessageType != WebSocketMessageType.Close) continue;
                await socket.CloseOutputAsync(result.CloseStatus ?? WebSocketCloseStatus.NormalClosure,
                                              result.CloseStatusDescription,
                                              CancellationToken.None);
                return;
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (WebSocketException)
        {
        }
    }
}
