using KNSoft.ZPigeon.Server.Managed;
using System.Buffers.Binary;
using System.Net.WebSockets;

namespace KNSoft.ZPigeon.Web;

internal static class VideoWebSocket
{
    internal static async Task RunAsync(HttpContext context, NativeServer server)
    {
        if (!context.WebSockets.IsWebSocketRequest ||
            string.IsNullOrEmpty(context.Request.Query["deviceId"]) ||
            !uint.TryParse(context.Request.Query["maxDimension"], out var maxDimension) ||
            !ushort.TryParse(context.Request.Query["frameRate"], out var frameRate) ||
            !ushort.TryParse(context.Request.Query["quality"], out var quality) ||
            maxDimension is 0 or > 3840 || frameRate is 0 or > 30 || quality is 0 or > 100)
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
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(context.RequestAborted);
        var receive = ReceiveAsync(socket, cancellation);
        try
        {
            await using var video = await server.OpenVideoStreamAsync(deviceId, maxDimension, frameRate, quality);
            var header = new byte[12];
            var headerOffset = 0;
            byte[]? frame = null;
            var frameOffset = 0;
            await foreach (var chunk in video.Output.ReadAllAsync(cancellation.Token))
            {
                var source = chunk;
                while (!source.IsEmpty)
                {
                    if (frame is null)
                    {
                        var copied = Math.Min(header.Length - headerOffset, source.Length);
                        source.Span[..copied].CopyTo(header.AsSpan(headerOffset));
                        source = source[copied..];
                        headerOffset += copied;
                        if (headerOffset != header.Length) continue;
                        var width = BinaryPrimitives.ReadUInt32LittleEndian(header);
                        var height = BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(4));
                        var length = BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(8));
                        if (width is 0 or > 3840 || height is 0 or > 3840 || length is 0 or > 0x01000000)
                        {
                            throw new InvalidDataException("无效的视频帧");
                        }
                        frame = GC.AllocateUninitializedArray<byte>((int)length);
                        frameOffset = 0;
                    }
                    var count = Math.Min(frame.Length - frameOffset, source.Length);
                    source.Span[..count].CopyTo(frame.AsSpan(frameOffset));
                    source = source[count..];
                    frameOffset += count;
                    if (frameOffset != frame.Length) continue;
                    await socket.SendAsync(frame, WebSocketMessageType.Binary, true, cancellation.Token);
                    frame = null;
                    headerOffset = 0;
                }
            }
            var completion = await video.Completion;
            if (!cancellation.IsCancellationRequested && socket.State == WebSocketState.Open)
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
        finally
        {
            await cancellation.CancelAsync();
            await receive;
        }
    }

    private static async Task ReceiveAsync(WebSocket socket, CancellationTokenSource cancellation)
    {
        var buffer = new byte[1];
        try
        {
            while (!cancellation.IsCancellationRequested && socket.State == WebSocketState.Open)
            {
                var result = await socket.ReceiveAsync(buffer, cancellation.Token);
                if (result.MessageType != WebSocketMessageType.Close) continue;
                await socket.CloseOutputAsync(result.CloseStatus ?? WebSocketCloseStatus.NormalClosure,
                                              result.CloseStatusDescription,
                                              CancellationToken.None);
                await cancellation.CancelAsync();
            }
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
        {
        }
        catch (WebSocketException)
        {
            await cancellation.CancelAsync();
        }
    }
}
