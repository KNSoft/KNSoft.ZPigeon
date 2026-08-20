using KNSoft.ZPigeon.Server.Managed;
using System.Net.WebSockets;

namespace KNSoft.ZPigeon.Web;

internal static class SerialWebSocket
{
    internal static async Task RunAsync(HttpContext context, NativeServer server)
    {
        if (!context.WebSockets.IsWebSocketRequest ||
            string.IsNullOrWhiteSpace(context.Request.Query["port"]) ||
            !uint.TryParse(context.Request.Query["baudRate"], out var baudRate) ||
            !byte.TryParse(context.Request.Query["dataBits"], out var dataBits) ||
            !Enum.TryParse<SerialParity>(context.Request.Query["parity"], out var parity) ||
            !Enum.TryParse<SerialStopBits>(context.Request.Query["stopBits"], out var stopBits) ||
            !Enum.TryParse<SerialFlowControl>(context.Request.Query["flowControl"], out var flowControl) ||
            baudRate is 0 or > 4000000 || dataBits is < 5 or > 8 ||
            !Enum.IsDefined(parity) || !Enum.IsDefined(stopBits) || !Enum.IsDefined(flowControl))
        {
            context.Response.StatusCode = StatusCodes.Status400BadRequest;
            return;
        }
        var port = context.Request.Query["port"].ToString();
        if (port.Length > 8)
        {
            context.Response.StatusCode = StatusCodes.Status400BadRequest;
            return;
        }
        using var socket = await context.WebSockets.AcceptWebSocketAsync();
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(context.RequestAborted);
        try
        {
            await using var serial = await server.OpenSerialPortAsync(port,
                                                                      baudRate,
                                                                      dataBits,
                                                                      parity,
                                                                      stopBits,
                                                                      flowControl);
            var upload = UploadAsync(socket, serial, cancellation.Token);
            var download = DownloadAsync(socket, serial, cancellation.Token);
            var finished = await Task.WhenAny(upload, download, serial.Completion);
            await cancellation.CancelAsync();
            await IgnoreCancellationAsync(upload);
            await IgnoreCancellationAsync(download);
            if (finished != serial.Completion) return;
            var completion = await serial.Completion;
            if (socket.State == WebSocketState.Open)
            {
                await socket.CloseAsync(completion.Status.IsSuccess ? WebSocketCloseStatus.NormalClosure :
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
        catch (InvalidDataException exception)
        {
            if (socket.State == WebSocketState.Open)
            {
                await socket.CloseAsync(WebSocketCloseStatus.InvalidPayloadData,
                                        exception.Message,
                                        CancellationToken.None);
            }
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
        {
        }
        catch (WebSocketException)
        {
        }
    }

    private static async Task UploadAsync(
        WebSocket socket,
        RemoteTunnel serial,
        CancellationToken cancellationToken)
    {
        var buffer = GC.AllocateUninitializedArray<byte>(0x10000);
        while (socket.State == WebSocketState.Open)
        {
            var offset = 0;
            ValueWebSocketReceiveResult result;
            do
            {
                result = await socket.ReceiveAsync(buffer.AsMemory(offset), cancellationToken);
                if (result.MessageType == WebSocketMessageType.Close) return;
                if (result.MessageType != WebSocketMessageType.Binary || result.Count == 0)
                {
                    throw new InvalidDataException("串口仅接受二进制消息");
                }
                offset += result.Count;
                if (!result.EndOfMessage && offset == buffer.Length)
                {
                    throw new InvalidDataException("串口消息过大");
                }
            }
            while (!result.EndOfMessage);
            await serial.WriteAsync(buffer.AsMemory(0, offset), cancellationToken);
        }
    }

    private static async Task DownloadAsync(
        WebSocket socket,
        RemoteTunnel serial,
        CancellationToken cancellationToken)
    {
        await foreach (var data in serial.Output.ReadAllAsync(cancellationToken))
        {
            await socket.SendAsync(data, WebSocketMessageType.Binary, true, cancellationToken);
        }
    }

    private static async Task IgnoreCancellationAsync(Task task)
    {
        try
        {
            await task;
        }
        catch (OperationCanceledException)
        {
        }
    }
}
