using KNSoft.ZPigeon.Server.Managed;
using System.Buffers.Binary;
using System.Net.WebSockets;
using System.Text.Json;

namespace KNSoft.ZPigeon.Web;

internal static class VideoWebSocket
{
    internal static async Task RunAsync(HttpContext context, NativeServer server)
    {
        if (!context.WebSockets.IsWebSocketRequest ||
            string.IsNullOrEmpty(context.Request.Query["deviceId"]) ||
            !uint.TryParse(context.Request.Query["width"], out var width) ||
            !uint.TryParse(context.Request.Query["height"], out var height) ||
            !uint.TryParse(context.Request.Query["frameRateNumerator"], out var frameRateNumerator) ||
            !uint.TryParse(context.Request.Query["frameRateDenominator"], out var frameRateDenominator) ||
            !ushort.TryParse(context.Request.Query["quality"], out var quality) ||
            !uint.TryParse(context.Request.Query["directStreamId"], out var directStreamId) ||
            !VideoStreamSettings.Validate(width,
                                         height,
                                         frameRateNumerator,
                                         frameRateDenominator,
                                         quality))
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
        var receive = Task.CompletedTask;
        try
        {
            await using var video = await server.OpenVideoStreamAsync(deviceId,
                                                                      new(width,
                                                                          height,
                                                                          frameRateNumerator,
                                                                          frameRateDenominator),
                                                                      quality,
                                                                      directStreamId);
            receive = ReceiveAsync(socket, video, cancellation);
            var header = new byte[12];
            var headerOffset = 0;
            byte[]? frame = null;
            var frameOffset = 0;
            if (directStreamId != 0)
            {
                if (await Task.WhenAny(video.Completion, receive) == receive) return;
            }
            else await foreach (var chunk in video.Output.ReadAllAsync(cancellation.Token))
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
                        var frameWidth = BinaryPrimitives.ReadUInt32LittleEndian(header);
                        var frameHeight = BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(4));
                        var length = BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(8));
                        if (frameWidth is 0 or > 3840 || frameHeight is 0 or > 3840 ||
                            length is 0 or > 0x01000000)
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

    private static async Task ReceiveAsync(
        WebSocket socket,
        VideoStream video,
        CancellationTokenSource cancellation)
    {
        var buffer = new byte[512];
        try
        {
            while (!cancellation.IsCancellationRequested && socket.State == WebSocketState.Open)
            {
                var result = await socket.ReceiveAsync(buffer, cancellation.Token);
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    await socket.CloseOutputAsync(result.CloseStatus ?? WebSocketCloseStatus.NormalClosure,
                                                  result.CloseStatusDescription,
                                                  CancellationToken.None);
                    await cancellation.CancelAsync();
                    continue;
                }
                if (result.MessageType != WebSocketMessageType.Text || !result.EndOfMessage)
                {
                    await socket.CloseAsync(WebSocketCloseStatus.InvalidPayloadData,
                                            "无效的视频参数",
                                            CancellationToken.None);
                    await cancellation.CancelAsync();
                    continue;
                }
                var settings = JsonSerializer.Deserialize<VideoStreamSettings>(buffer.AsSpan(0, result.Count),
                                                                                JsonSerializerOptions.Web);
                if (settings is null || !settings.IsValid)
                {
                    await socket.CloseAsync(WebSocketCloseStatus.InvalidPayloadData,
                                            "无效的视频参数",
                                            CancellationToken.None);
                    await cancellation.CancelAsync();
                    continue;
                }
                await video.UpdateAsync(settings.Format, settings.Quality);
            }
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
        {
        }
        catch (WebSocketException)
        {
            await cancellation.CancelAsync();
        }
        catch (NativeException exception)
        {
            if (socket.State == WebSocketState.Open)
            {
                await socket.CloseAsync(WebSocketCloseStatus.InternalServerError,
                                        exception.Message,
                                        CancellationToken.None);
            }
            await cancellation.CancelAsync();
        }
        catch (JsonException)
        {
            if (socket.State == WebSocketState.Open)
            {
                await socket.CloseAsync(WebSocketCloseStatus.InvalidPayloadData,
                                        "无效的视频参数",
                                        CancellationToken.None);
            }
            await cancellation.CancelAsync();
        }
    }

    private sealed record VideoStreamSettings(
        uint Width,
        uint Height,
        uint FrameRateNumerator,
        uint FrameRateDenominator,
        ushort Quality)
    {
        internal VideoFormat Format => new(Width, Height, FrameRateNumerator, FrameRateDenominator);
        internal bool IsValid => Validate(Width, Height, FrameRateNumerator, FrameRateDenominator, Quality);

        internal static bool Validate(
            uint width,
            uint height,
            uint frameRateNumerator,
            uint frameRateDenominator,
            ushort quality) =>
            width is > 0 and <= 3840 && height is > 0 and <= 3840 && frameRateNumerator != 0 &&
            frameRateDenominator != 0 && frameRateNumerator <= (ulong)frameRateDenominator * 120 &&
            quality is > 0 and <= 100;
    }
}
