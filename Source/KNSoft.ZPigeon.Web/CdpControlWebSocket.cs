using System.Buffers;
using System.Net.WebSockets;
using System.Text.Json;

namespace KNSoft.ZPigeon.Web;

internal static class CdpControlWebSocket
{
    private const int MaximumControlMessage = 0x00010000;
    private const int MaximumCdpMessage = 0x02000000;
    private static readonly HashSet<string> AllowedMethods =
    [
        "Input.dispatchKeyEvent",
        "Input.dispatchMouseEvent",
        "Input.insertText",
        "Page.getNavigationHistory",
        "Page.handleJavaScriptDialog",
        "Page.navigate",
        "Page.navigateToHistoryEntry",
        "Page.reload",
        "Page.stopLoading"
    ];

    internal static async Task RunAsync(
        HttpContext context,
        CdpSessionManager sessions,
        Guid sessionId,
        string targetId,
        int width,
        int height,
        int quality)
    {
        if (!context.WebSockets.IsWebSocketRequest ||
            width is < 320 or > 3840 || height is < 240 or > 2160 || quality is < 20 or > 100)
        {
            context.Response.StatusCode = StatusCodes.Status400BadRequest;
            return;
        }
        Uri endpoint;
        try
        {
            endpoint = await sessions.GetControlEndpointAsync(sessionId, targetId, context.RequestAborted);
        }
        catch (KeyNotFoundException)
        {
            context.Response.StatusCode = StatusCodes.Status404NotFound;
            return;
        }
        using var browser = await context.WebSockets.AcceptWebSocketAsync();
        using var remote = new ClientWebSocket();
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(context.RequestAborted);
        using var remoteSend = new SemaphoreSlim(1, 1);
        try
        {
            await remote.ConnectAsync(endpoint, cancellation.Token);
            await InitializeAsync(remote, remoteSend, width, height, quality, cancellation.Token);
            var receiveRemote = ReceiveRemoteAsync(remote, browser, remoteSend, cancellation.Token);
            var receiveBrowser = ReceiveBrowserAsync(browser, remote, remoteSend, cancellation.Token);
            await Task.WhenAny(receiveRemote, receiveBrowser);
            cancellation.Cancel();
            await IgnoreCancellationAsync(receiveRemote);
            await IgnoreCancellationAsync(receiveBrowser);
        }
        catch (Exception exception) when (exception is WebSocketException or
                                          HttpRequestException or
                                          InvalidDataException)
        {
            if (browser.State == WebSocketState.Open)
            {
                await browser.CloseOutputAsync(WebSocketCloseStatus.InternalServerError,
                    exception.Message,
                    CancellationToken.None);
            }
        }
    }

    private static async Task InitializeAsync(
        ClientWebSocket remote,
        SemaphoreSlim sendLock,
        int width,
        int height,
        int quality,
        CancellationToken cancellationToken)
    {
        await SendRemoteAsync(remote, sendLock, new
        {
            id = 1,
            method = "Page.enable"
        }, cancellationToken);
        await SendRemoteAsync(remote, sendLock, new
        {
            id = 2,
            method = "Page.bringToFront"
        }, cancellationToken);
        await SendRemoteAsync(remote, sendLock, new
        {
            id = 3,
            method = "Emulation.setDeviceMetricsOverride",
            @params = new { width, height, deviceScaleFactor = 1, mobile = false }
        }, cancellationToken);
        await SendRemoteAsync(remote, sendLock, new
        {
            id = 4,
            method = "Page.startScreencast",
            @params = new
            {
                format = "jpeg",
                quality,
                maxWidth = width,
                maxHeight = height,
                everyNthFrame = 1,
                maxFramesInFlight = 1,
                sendLastFrame = true
            }
        }, cancellationToken);
        await SendRemoteAsync(remote, sendLock, new
        {
            id = 5,
            method = "Page.getNavigationHistory"
        }, cancellationToken);
    }

    private static async Task ReceiveRemoteAsync(
        ClientWebSocket remote,
        WebSocket browser,
        SemaphoreSlim sendLock,
        CancellationToken cancellationToken)
    {
        while (remote.State == WebSocketState.Open && browser.State == WebSocketState.Open)
        {
            var message = await ReceiveAsync(remote, MaximumCdpMessage, cancellationToken);
            if (message.Type == WebSocketMessageType.Close) return;
            if (message.Type != WebSocketMessageType.Text) continue;
            try
            {
                using var document = JsonDocument.Parse(message.Data);
                var root = document.RootElement;
                if (root.TryGetProperty("method", out var method) &&
                    method.ValueEquals("Page.screencastFrame") &&
                    root.TryGetProperty("params", out var parameters) &&
                    parameters.TryGetProperty("data", out var encoded) &&
                    parameters.TryGetProperty("sessionId", out var frameSessionId) &&
                    parameters.TryGetProperty("metadata", out var metadata))
                {
                    // CDP mandates Base64 here; decode the UTF-8 token without creating a UTF-16 string.
                    var data = encoded.GetBytesFromBase64();
                    var sessionId = frameSessionId.GetInt32();
                    await browser.SendAsync(JsonSerializer.SerializeToUtf8Bytes(new
                    {
                        method = "ZPigeon.screencastFrame",
                        @params = new
                        {
                            sessionId,
                            metadata
                        }
                    }), WebSocketMessageType.Text, true, cancellationToken);
                    await browser.SendAsync(data,
                        WebSocketMessageType.Binary,
                        true,
                        cancellationToken);
                    continue;
                }
            }
            catch (Exception exception) when (exception is JsonException or
                                              FormatException or
                                              InvalidOperationException)
            {
                continue;
            }
            await browser.SendAsync(message.Data,
                WebSocketMessageType.Text,
                true,
                cancellationToken);
        }
    }

    private static async Task ReceiveBrowserAsync(
        WebSocket browser,
        ClientWebSocket remote,
        SemaphoreSlim sendLock,
        CancellationToken cancellationToken)
    {
        while (browser.State == WebSocketState.Open && remote.State == WebSocketState.Open)
        {
            var message = await ReceiveAsync(browser, MaximumControlMessage, cancellationToken);
            if (message.Type == WebSocketMessageType.Close) return;
            if (message.Type == WebSocketMessageType.Text &&
                TryGetFrameAcknowledgement(message.Data, out var sessionId))
            {
                await SendRemoteAsync(remote, sendLock, new
                {
                    id = 0x70000000 + sessionId,
                    method = "Page.screencastFrameAck",
                    @params = new { sessionId }
                }, cancellationToken);
                continue;
            }
            if (message.Type != WebSocketMessageType.Text || !IsAllowed(message.Data))
            {
                await browser.SendAsync(JsonSerializer.SerializeToUtf8Bytes(new
                {
                    method = "ZPigeon.error",
                    @params = new { code = "invalidControlMessage" }
                }), WebSocketMessageType.Text, true, cancellationToken);
                continue;
            }
            await sendLock.WaitAsync(cancellationToken);
            try
            {
                await remote.SendAsync(message.Data,
                    WebSocketMessageType.Text,
                    true,
                    cancellationToken);
            }
            finally
            {
                sendLock.Release();
            }
        }
    }

    private static bool TryGetFrameAcknowledgement(ReadOnlyMemory<byte> message, out int sessionId)
    {
        sessionId = 0;
        try
        {
            using var document = JsonDocument.Parse(message);
            var root = document.RootElement;
            return root.ValueKind == JsonValueKind.Object &&
                   root.TryGetProperty("method", out var method) &&
                   method.ValueEquals("ZPigeon.screencastFrameAck") &&
                   root.TryGetProperty("params", out var parameters) &&
                   parameters.TryGetProperty("sessionId", out var value) &&
                   value.TryGetInt32(out sessionId) && sessionId > 0;
        }
        catch (JsonException)
        {
            return false;
        }
    }

    private static bool IsAllowed(ReadOnlyMemory<byte> message)
    {
        try
        {
            using var document = JsonDocument.Parse(message);
            var root = document.RootElement;
            return root.ValueKind == JsonValueKind.Object &&
                   root.TryGetProperty("id", out var id) && id.TryGetInt32(out var value) && value > 0 &&
                   root.TryGetProperty("method", out var method) &&
                   method.ValueKind == JsonValueKind.String &&
                   AllowedMethods.Contains(method.GetString()!);
        }
        catch (JsonException)
        {
            return false;
        }
    }

    private static async Task SendRemoteAsync(
        ClientWebSocket remote,
        SemaphoreSlim sendLock,
        object message,
        CancellationToken cancellationToken)
    {
        var data = JsonSerializer.SerializeToUtf8Bytes(message);
        await sendLock.WaitAsync(cancellationToken);
        try
        {
            await remote.SendAsync(data, WebSocketMessageType.Text, true, cancellationToken);
        }
        finally
        {
            sendLock.Release();
        }
    }

    private static async Task<Message> ReceiveAsync(
        WebSocket socket,
        int maximumLength,
        CancellationToken cancellationToken)
    {
        var buffer = ArrayPool<byte>.Shared.Rent(0x00010000);
        try
        {
            using var output = new MemoryStream();
            ValueWebSocketReceiveResult result;
            do
            {
                result = await socket.ReceiveAsync(buffer.AsMemory(), cancellationToken);
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    return new(result.MessageType, ReadOnlyMemory<byte>.Empty);
                }
                if (output.Length + result.Count > maximumLength)
                {
                    throw new InvalidDataException("WebSocket 消息过大。");
                }
                output.Write(buffer, 0, result.Count);
            } while (!result.EndOfMessage);
            return new(result.MessageType,
                output.GetBuffer().AsMemory(0, checked((int)output.Length)));
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(buffer);
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
        catch (WebSocketException)
        {
        }
    }

    private readonly record struct Message(
        WebSocketMessageType Type,
        ReadOnlyMemory<byte> Data);
}
