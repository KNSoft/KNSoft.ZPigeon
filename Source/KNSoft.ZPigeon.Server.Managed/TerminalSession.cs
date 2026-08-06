using System.Runtime.InteropServices;
using System.Threading.Channels;

namespace KNSoft.ZPigeon.Server.Managed;

[Flags]
public enum TerminalShell : byte
{
    CommandPrompt = 0x00000001,
    WindowsPowerShell = 0x00000002,
    PowerShell = 0x00000004,
    ConsoleScriptHost = 0x00000008,
    WindowsScriptHost = 0x00000010,
    HtmlApplication = 0x00000020
}

public sealed record TerminalShellInfo(
    TerminalShell Id,
    string Name);

public readonly record struct TerminalCompletion(
    ZpStatus Status);

public sealed partial class NativeServer
{
    private static readonly TerminalShellDescriptor[] TerminalShells =
    [
        new(TerminalShell.CommandPrompt,
            "Command Prompt",
            "cmd.exe",
            "/D /Q"),
        new(TerminalShell.WindowsPowerShell,
            "Windows PowerShell",
            "powershell.exe",
            null),
        new(TerminalShell.PowerShell,
            "PowerShell",
            "pwsh.exe",
            null)
    ];
    private static readonly NativeMethods.TerminalShellsCallback ShellsCallback =
        CompleteTerminalShells;
    private static readonly NativeMethods.TerminalCreateCallback CreateCallback =
        CompleteTerminalCreate;
    private static readonly NativeMethods.TerminalDataCallback DataCallback =
        ReceiveTerminalData;
    private static readonly NativeMethods.TerminalWritableCallback WritableCallback =
        SignalTerminalWritable;
    private static readonly NativeMethods.TerminalCloseCallback CloseCallback =
        CompleteTerminalClose;

    public Task<TerminalShellInfo[]> GetTerminalShellsAsync() =>
        RunOperationAsync<TerminalShellInfo[]>(context =>
            NativeMethods.QueryTerminalShells(ClientId, ShellsCallback, context));

    public Task<TerminalSession> CreateShellAsync(
        TerminalShell shell,
        ushort columns,
        ushort rows,
        string? workingDirectory = null)
    {
        var descriptor = GetTerminalShell(shell);
        return CreateTerminalAsync(new ExecutionStart(
                                       ExecutionEngine.CreateProcess,
                                       ExecutionIdentity.Current,
                                       uint.MaxValue,
                                       ExecutionFlags.Hidden,
                                       descriptor.FileName,
                                       descriptor.Arguments,
                                       workingDirectory,
                                       null,
                                       null,
                                       null,
                                       null),
                                   columns,
                                   rows,
                                   descriptor.Info);
    }

    public Task<TerminalSession> CreateScriptTerminalAsync(
        TerminalShell shell,
        string path,
        ushort columns,
        ushort rows)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        var profile = shell switch
        {
            TerminalShell.CommandPrompt => ("cmd.exe", $"/D /Q /C call \"{path}\"", GetTerminalShell(shell).Info),
            TerminalShell.WindowsPowerShell => ("powershell.exe",
                                                 $"-NoLogo -NoProfile -ExecutionPolicy Bypass -File \"{path}\"",
                                                 GetTerminalShell(shell).Info),
            TerminalShell.PowerShell => ("pwsh.exe",
                                          $"-NoLogo -NoProfile -ExecutionPolicy Bypass -File \"{path}\"",
                                          GetTerminalShell(shell).Info),
            TerminalShell.ConsoleScriptHost or TerminalShell.WindowsScriptHost =>
                CreateWindowsScriptHostProfile(shell, path),
            TerminalShell.HtmlApplication => ("mshta.exe",
                                              $"\"{path}\"",
                                              new TerminalShellInfo(shell, "HTML Application")),
            _ => throw new ArgumentOutOfRangeException(nameof(shell))
        };
        return CreateTerminalAsync(new ExecutionStart(
                                       ExecutionEngine.CreateProcess,
                                       ExecutionIdentity.Current,
                                       uint.MaxValue,
                                       ExecutionFlags.Hidden,
                                       profile.Item1,
                                       profile.Item2,
                                       Path.GetDirectoryName(path),
                                       null,
                                       null,
                                       null,
                                       null),
                                   columns,
                                   rows,
                                   profile.Item3);
    }

    public Task<TerminalSession> CreateTerminalAsync(
        ExecutionStart start,
        ushort columns,
        ushort rows) =>
        CreateTerminalAsync(start, columns, rows, (TerminalShellInfo?)null);

    public Task<TerminalSession> CreateTerminalAsync(
        ExecutionStart start,
        ushort columns,
        ushort rows,
        string title) =>
        CreateTerminalAsync(start, columns, rows, new TerminalShellInfo(0, title));

    private unsafe Task<TerminalSession> CreateTerminalAsync(
        ExecutionStart start,
        ushort columns,
        ushort rows,
        TerminalShellInfo? shell)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(start.FileName);
        var completion = new TerminalCreation { Shell = shell };
        completion.Handle = GCHandle.Alloc(completion);
        int status;
        fixed (byte* customToken = start.CustomToken)
        {
            status = NativeMethods.CreateTerminal(ClientId,
                columns,
                rows,
                (byte)start.Identity,
                start.SessionId,
                (uint)start.Flags,
                start.FileName,
                (uint)start.FileName.Length,
                start.Arguments,
                (uint)(start.Arguments?.Length ?? 0),
                start.WorkingDirectory,
                (uint)(start.WorkingDirectory?.Length ?? 0),
                start.UserName,
                (uint)(start.UserName?.Length ?? 0),
                start.Password,
                (uint)(start.Password?.Length ?? 0),
                start.AppContainerSid,
                (uint)(start.AppContainerSid?.Length ?? 0),
                customToken,
                (uint)(start.CustomToken?.Length ?? 0),
                CreateCallback,
                DataCallback,
                WritableCallback,
                CloseCallback,
                GCHandle.ToIntPtr(completion.Handle));
        }
        if (status < 0)
        {
            completion.Handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Completion.Task;
    }

    private static void CompleteTerminalShells(
        ZpStatus status,
        byte shells,
        nint context)
    {
        var completion = GetCompletion<TerminalShellInfo[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var flags = (TerminalShell)shells;
        var result = new List<TerminalShellInfo>(3);
        foreach (var shell in TerminalShells)
        {
            if (flags.HasFlag(shell.Info.Id))
            {
                result.Add(shell.Info);
            }
        }
        completion.SetResult([.. result]);
    }

    private static void CompleteTerminalCreate(
        ZpStatus status,
        nint terminal,
        uint processId,
        nint context)
    {
        var creation = GetTerminalCreation(context);
        if (!status.IsSuccess)
        {
            creation.Handle.Free();
            creation.Completion.SetException(new NativeException(status));
            return;
        }
        creation.Session = new TerminalSession(terminal,
                                               processId,
                                               creation);
        creation.Completion.SetResult(creation.Session);
    }

    private static bool ReceiveTerminalData(
        nint data,
        uint dataLength,
        nint context) =>
        GetTerminalCreation(context).Session?.Receive(data, dataLength) == true;

    private static void SignalTerminalWritable(uint creditBytes, nint context) =>
        GetTerminalCreation(context).Session?.SignalWritable();

    private static void CompleteTerminalClose(ZpStatus status, nint context) =>
        GetTerminalCreation(context).Session?.Complete(status);

    private static TerminalCreation GetTerminalCreation(nint context) =>
        (TerminalCreation)GCHandle.FromIntPtr(context).Target!;

    internal sealed class TerminalCreation
    {
        internal readonly TaskCompletionSource<TerminalSession> Completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal GCHandle Handle;
        internal TerminalShellInfo? Shell;
        internal TerminalSession? Session;
    }

    private static TerminalShellDescriptor GetTerminalShell(TerminalShell id)
    {
        foreach (var shell in TerminalShells)
        {
            if (shell.Info.Id == id)
            {
                return shell;
            }
        }
        throw new ArgumentOutOfRangeException(nameof(id));
    }

    private static (string, string, TerminalShellInfo) CreateWindowsScriptHostProfile(
        TerminalShell shell,
        string path)
    {
        var (name, engine) = Path.GetExtension(path).ToLowerInvariant() switch
        {
            ".vbs" => ("VBScript", " //E:VBScript"),
            ".js" => ("JScript", " //E:JScript"),
            ".wsf" => ("Windows Script File", ""),
            _ => throw new ArgumentOutOfRangeException(nameof(path))
        };
        var host = shell == TerminalShell.ConsoleScriptHost ? "cscript" : "wscript";
        return ($"{host}.exe", $"//NoLogo{engine} \"{path}\"",
                new TerminalShellInfo(shell, $"{name} ({host})"));
    }

    private sealed record TerminalShellDescriptor(
        TerminalShell Id,
        string Name,
        string FileName,
        string? Arguments)
    {
        internal TerminalShellInfo Info { get; } =
            new(Id, Name);
    }
}

public sealed class TerminalSession : IAsyncDisposable
{
    private const int Retry = unchecked((int)0xC000022D);
    private const int InputChunkSize = 0x1000;
    private readonly nint terminal;
    private readonly NativeServer.TerminalCreation creation;
    private readonly Channel<OwnedBuffer> output =
        Channel.CreateBounded<OwnedBuffer>(
            new BoundedChannelOptions(32)
            {
                SingleReader = true,
                SingleWriter = false,
                FullMode = BoundedChannelFullMode.Wait
            });
    private readonly SemaphoreSlim sendLock = new(1, 1);
    private readonly SemaphoreSlim writable = new(0);
    private readonly TaskCompletionSource<TerminalCompletion> completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int disposed;

    internal TerminalSession(
        nint terminal,
        uint processId,
        NativeServer.TerminalCreation creation)
    {
        this.terminal = terminal;
        this.creation = creation;
        ProcessId = processId;
        Shell = creation.Shell;
    }

    public uint ProcessId { get; }
    public TerminalShellInfo? Shell { get; }
    public ChannelReader<OwnedBuffer> Output => output.Reader;
    public Task<TerminalCompletion> Completion => completion.Task;

    public async ValueTask WriteAsync(
        ReadOnlyMemory<byte> data,
        CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        await sendLock.WaitAsync(cancellationToken);
        try
        {
            for (var offset = 0; offset < data.Length;)
            {
                var length = Math.Min(InputChunkSize, data.Length - offset);
                var chunk = data.Slice(offset, length);
                int status;
                while ((status = Send(chunk.Span)) == Retry)
                {
                    await writable.WaitAsync(cancellationToken);
                }
                NativeServer.ThrowIfFailed(status);
                offset += length;
            }
        }
        finally
        {
            sendLock.Release();
        }
    }

    public Task ResizeAsync(ushort columns, ushort rows)
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        var result = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(result);
        var status = NativeMethods.ResizeTerminal(
            terminal,
            columns,
            rows,
            NativeServer.StatusCallback,
            GCHandle.ToIntPtr(handle));
        if (status < 0)
        {
            handle.Free();
            NativeServer.ThrowIfFailed(status);
        }
        return result.Task;
    }

    internal bool Receive(nint data, uint dataLength)
    {
        var buffer = OwnedBuffer.Copy(data, (int)dataLength);
        if (output.Writer.TryWrite(buffer)) return true;
        buffer.Dispose();
        return false;
    }

    internal void SignalWritable() => writable.Release();

    internal void Complete(ZpStatus status)
    {
        output.Writer.TryComplete();
        completion.TrySetResult(new(status));
        writable.Release();
    }

    private unsafe int Send(ReadOnlySpan<byte> data)
    {
        fixed (byte* pointer = data)
        {
            return NativeMethods.TerminalSend(terminal,
                                              pointer,
                                              (uint)data.Length);
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0)
        {
            return;
        }
        output.Writer.TryComplete();
        _ = NativeMethods.CloseTerminal(terminal);
        await completion.Task;
        OwnedBuffer.Drain(output.Reader);
        creation.Handle.Free();
        sendLock.Dispose();
        writable.Dispose();
    }
}

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void TerminalShellsCallback(
        ZpStatus status,
        byte shells,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void TerminalCreateCallback(
        ZpStatus status,
        nint terminal,
        uint processId,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal delegate bool TerminalDataCallback(
        nint data,
        uint dataLength,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void TerminalWritableCallback(
        uint creditBytes,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void TerminalCloseCallback(ZpStatus status, nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_QueryTerminalShells")]
    internal static partial int QueryTerminalShells(
        ulong clientId,
        TerminalShellsCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_CreateTerminal",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static unsafe partial int CreateTerminal(
        ulong clientId,
        ushort columns,
        ushort rows,
        byte identity,
        uint sessionId,
        uint flags,
        string fileName,
        uint fileNameLength,
        string? arguments,
        uint argumentsLength,
        string? workingDirectory,
        uint workingDirectoryLength,
        string? userName,
        uint userNameLength,
        string? password,
        uint passwordLength,
        string? appContainerSid,
        uint appContainerSidLength,
        byte* customToken,
        uint customTokenLength,
        TerminalCreateCallback createCallback,
        TerminalDataCallback dataCallback,
        TerminalWritableCallback writableCallback,
        TerminalCloseCallback closeCallback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_TerminalSend")]
    internal static unsafe partial int TerminalSend(
        nint terminal,
        byte* data,
        uint dataLength);

    [LibraryImport(Library, EntryPoint = "ZpNative_ResizeTerminal")]
    internal static partial int ResizeTerminal(
        nint terminal,
        ushort columns,
        ushort rows,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CloseTerminal")]
    internal static partial int CloseTerminal(nint terminal);
}
