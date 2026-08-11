using System.Net.WebSockets;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text.Json;
using KNSoft.ZPigeon.Server.Managed;

namespace KNSoft.ZPigeon.Web;

internal static class TerminalWebSocket
{
    private const int BufferSize = 0x1000;

    internal static async Task RunAsync(
        HttpContext context,
        NativeServer server)
    {
        if (!context.WebSockets.IsWebSocketRequest ||
            !uint.TryParse(context.Request.Query["shell"], out var shellValue) ||
            !ushort.TryParse(context.Request.Query["columns"], out var columns) ||
            !ushort.TryParse(context.Request.Query["rows"], out var rows) ||
            columns == 0 || rows == 0 ||
            !Enum.IsDefined((TerminalShell)shellValue))
        {
            context.Response.StatusCode = StatusCodes.Status400BadRequest;
            return;
        }
        await using var terminal = await server.CreateShellAsync(
            (TerminalShell)shellValue,
            columns,
            rows);
        using var socket = await context.WebSockets.AcceptWebSocketAsync();
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(
            context.RequestAborted);
        var send = SendAsync(socket, terminal, cancellation.Token);
        var receive = ReceiveAsync(socket, terminal, cancellation.Token);
        var completed = await Task.WhenAny(send, receive);
        if (completed == send && send.IsCompletedSuccessfully &&
            socket.State == WebSocketState.Open)
        {
            await socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure,
                                          null,
                                          CancellationToken.None);
        }
        else
        {
            cancellation.Cancel();
        }
        try
        {
            await Task.WhenAll(send, receive);
        }
        catch (OperationCanceledException)
        {
        }
        catch (WebSocketException)
        {
            return;
        }
        catch (NativeException exception)
        {
            if (socket.State == WebSocketState.Open)
            {
                await socket.CloseOutputAsync(
                    WebSocketCloseStatus.InternalServerError,
                    exception.Message,
                    CancellationToken.None);
            }
            return;
        }
        if (socket.State is WebSocketState.Open or WebSocketState.CloseReceived)
        {
            await socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure,
                                          null,
                                          CancellationToken.None);
        }
    }

    private static async Task SendAsync(
        WebSocket socket,
        TerminalSession terminal,
        CancellationToken cancellationToken)
    {
        await foreach (var data in terminal.Output.ReadAllAsync(
                           cancellationToken))
        {
            await socket.SendAsync(data,
                                   WebSocketMessageType.Binary,
                                   true,
                                   cancellationToken);
        }
        var completion = await terminal.Completion;
        if (socket.State == WebSocketState.Open)
        {
            await socket.SendAsync(
                JsonSerializer.SerializeToUtf8Bytes(
                    CreateCloseMessage(completion)),
                WebSocketMessageType.Text,
                true,
                cancellationToken);
        }
    }

    private static async Task ReceiveAsync(
        WebSocket socket,
        TerminalSession terminal,
        CancellationToken cancellationToken)
    {
        var buffer = new byte[BufferSize];
        while (socket.State == WebSocketState.Open)
        {
            var result = await socket.ReceiveAsync(buffer, cancellationToken);
            if (result.MessageType == WebSocketMessageType.Close)
            {
                return;
            }
            if (result.MessageType == WebSocketMessageType.Binary)
            {
                if (result.Count != 0)
                {
                    await terminal.WriteAsync(
                        buffer.AsMemory(0, result.Count),
                        cancellationToken);
                }
                continue;
            }
            if (!result.EndOfMessage)
            {
                await socket.CloseOutputAsync(
                    WebSocketCloseStatus.MessageTooBig,
                    null,
                    cancellationToken);
                return;
            }
            var resize = JsonSerializer.Deserialize<ResizeMessage>(
                buffer.AsSpan(0, result.Count));
            if (resize?.Type != "resize" ||
                resize.Columns == 0 || resize.Rows == 0)
            {
                await socket.CloseOutputAsync(
                    WebSocketCloseStatus.InvalidPayloadData,
                    null,
                    cancellationToken);
                return;
            }
            await terminal.ResizeAsync(resize.Columns, resize.Rows);
        }
    }

    private sealed record ResizeMessage(
        string Type,
        ushort Columns,
        ushort Rows);

    private static CloseMessage CreateCloseMessage(
        TerminalCompletion completion) =>
        new("closed",
            GetStatusSource(completion.Status.Type),
            completion.Status.Code,
            GetStatusName(completion.Status));

    private static string GetStatusSource(ZpStatusType type) => type switch
    {
        ZpStatusType.None => "None",
        ZpStatusType.NtStatus => "NTSTATUS",
        ZpStatusType.Win32 => "Win32",
        ZpStatusType.Winsock => "Winsock",
        ZpStatusType.HResult => "HRESULT",
        ZpStatusType.Security => "Security",
        ZpStatusType.Quic => "QUIC",
        ZpStatusType.ProcessExit => "ProcessExit",
        _ => "Unknown"
    };

    private static string? GetStatusName(ZpStatus status) => status.Type switch
    {
        ZpStatusType.NtStatus => GetNtStatusName(status.Code),
        ZpStatusType.Win32 or ZpStatusType.Winsock =>
            new Win32Exception((int)status.Code).Message,
        ZpStatusType.HResult or
        ZpStatusType.Security or
        ZpStatusType.Quic =>
            Marshal.GetExceptionForHR(unchecked((int)status.Code))?.Message,
        _ => null
    };

    private static string? GetNtStatusName(uint status) => status switch
    {
        0xC0000001 => "STATUS_UNSUCCESSFUL",
        0xC000000D => "STATUS_INVALID_PARAMETER",
        0xC0000017 => "STATUS_NO_MEMORY",
        0xC00000B5 => "STATUS_IO_TIMEOUT",
        0xC0000120 => "STATUS_CANCELLED",
        0xC000014B => "STATUS_PIPE_BROKEN",
        0xC000020C => "STATUS_CONNECTION_DISCONNECTED",
        _ => null
    };

    private sealed record CloseMessage(
        string Type,
        string Source,
        uint Code,
        string? Name);
}
