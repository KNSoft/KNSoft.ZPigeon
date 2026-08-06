using KNSoft.ZPigeon.Server.Managed;
using System.Net.WebSockets;

namespace KNSoft.ZPigeon.Web;

internal static class WindowCaptureWebSocket
{
    internal static async Task RunAsync(
        HttpContext context,
        NativeServer server,
        ConnectionPerformanceManager performance)
    {
        var desktop = bool.TryParse(context.Request.Query["desktop"], out var desktopValue) && desktopValue;
        var monitorIndex = uint.MaxValue;
        if (!context.WebSockets.IsWebSocketRequest ||
            !ulong.TryParse(context.Request.Query["handle"], out var handle) ||
            !uint.TryParse(context.Request.Query["processId"], out var processId) ||
            !uint.TryParse(context.Request.Query["threadId"], out var threadId) ||
            !bool.TryParse(context.Request.Query["captureCursor"], out var captureCursor) ||
            !uint.TryParse(context.Request.Query["maxDimension"], out var maxDimension) ||
            !byte.TryParse(context.Request.Query["frameRate"], out var frameRate) ||
            !byte.TryParse(context.Request.Query["imageQuality"], out var imageQuality) ||
            !byte.TryParse(context.Request.Query["captureMode"], out var captureMode) || captureMode > 2 ||
            !byte.TryParse(context.Request.Query["videoCodec"], out var videoCodec) || videoCodec > 1 ||
            (desktop && !uint.TryParse(context.Request.Query["monitorIndex"], out monitorIndex)) ||
            !uint.TryParse(context.Request.Query["directStreamId"], out var directStreamId))
        {
            context.Response.StatusCode = StatusCodes.Status400BadRequest;
            return;
        }
        using var socket = await context.WebSockets.AcceptWebSocketAsync();
        try
        {
            var requestedOptions = new WindowCaptureOptions(
                captureCursor,
                maxDimension,
                frameRate,
                imageQuality,
                desktop,
                monitorIndex,
                (WindowCaptureMode)captureMode,
                (WindowVideoCodec)videoCodec);
            var effectiveOptions = Adapt(requestedOptions, performance.Current);
            await using var capture = await server.OpenWindowCaptureAsync(
                handle,
                processId,
                threadId,
                effectiveOptions,
                directStreamId);
            var input = ReceiveInputAsync(socket, capture, context.RequestAborted);
            var output = directStreamId != 0 ? capture.Completion :
                                               SendOutputAsync(socket, capture, context.RequestAborted);
            using var qualityCancellation = new CancellationTokenSource();
            var quality = AdaptAsync(capture,
                                     requestedOptions,
                                     effectiveOptions,
                                     performance,
                                     qualityCancellation.Token);
            try
            {
                var completed = await Task.WhenAny(capture.Completion, input, output, quality);
                if (completed == input)
                {
                    await input;
                    if (socket.State != WebSocketState.Open) return;
                }
                if (completed == output) await output;
                if (completed == quality) await quality;
            }
            finally
            {
                qualityCancellation.Cancel();
                try
                {
                    await quality;
                }
                catch (OperationCanceledException)
                {
                }
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

    private static async Task AdaptAsync(
        WindowCaptureStream capture,
        WindowCaptureOptions requested,
        WindowCaptureOptions effective,
        ConnectionPerformanceManager performance,
        CancellationToken cancellationToken)
    {
        while (!capture.Completion.IsCompleted)
        {
            if (await Task.WhenAny(Task.Delay(2000, cancellationToken), capture.Completion) ==
                capture.Completion)
            {
                return;
            }
            cancellationToken.ThrowIfCancellationRequested();
            var next = Adapt(requested, performance.Current);
            if (next == effective) continue;
            try
            {
                capture.Update(next);
            }
            catch (NativeException exception) when (
                exception.Status is { Type: ZpStatusType.NtStatus, Code: 0xC0000184 })
            {
                await capture.Completion.WaitAsync(cancellationToken);
                return;
            }
            effective = next;
        }
    }

    private static WindowCaptureOptions Adapt(
        WindowCaptureOptions requested,
        ConnectionPerformance performance)
    {
        var qualityClass = Math.Min(performance.EffectiveSpeedClass,
                                    performance.EffectiveLatencyClass);
        var limits = qualityClass switch
        {
            0 => (FrameRate: (byte)3, Dimension: 720U, Quality: (byte)60),
            1 => (FrameRate: (byte)6, Dimension: 960U, Quality: (byte)72),
            2 => (FrameRate: (byte)12, Dimension: 1280U, Quality: (byte)80),
            3 => (FrameRate: (byte)18, Dimension: 1920U, Quality: (byte)85),
            _ => (FrameRate: byte.MaxValue, Dimension: uint.MaxValue, Quality: byte.MaxValue)
        };
        return requested with
        {
            FrameRate = Math.Min(requested.FrameRate, limits.FrameRate),
            MaxDimension = requested.MaxDimension == 7680 ? requested.MaxDimension :
                Math.Min(requested.MaxDimension, limits.Dimension),
            ImageQuality = Math.Min(requested.ImageQuality, limits.Quality)
        };
    }

    private static async Task SendOutputAsync(
        WebSocket socket,
        WindowCaptureStream capture,
        CancellationToken cancellationToken)
    {
        await foreach (var data in capture.Output.ReadAllAsync(cancellationToken))
        {
            using (data)
                await socket.SendAsync(data.Memory, WebSocketMessageType.Binary, true, cancellationToken);
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
            try
            {
                capture.Send(buffer.AsSpan(0, length));
            }
            catch (NativeException exception) when (
                exception.Status is { Type: ZpStatusType.NtStatus, Code: 0xC0000184 })
            {
                await capture.Completion.WaitAsync(cancellationToken);
                return;
            }
        }
    }
}
