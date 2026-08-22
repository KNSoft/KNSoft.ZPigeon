using System.Runtime.InteropServices;
using System.Threading.Channels;

namespace KNSoft.ZPigeon.Server.Managed;

[Flags]
public enum TerminalShell : uint
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
            "cmd.exe /D /Q"),
        new(TerminalShell.WindowsPowerShell,
            "Windows PowerShell",
            "powershell.exe"),
        new(TerminalShell.PowerShell,
            "PowerShell",
            "pwsh.exe")
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

    public Task<TerminalShellInfo[]> GetTerminalShellsAsync()
    {
        var completion = new TaskCompletionSource<TerminalShellInfo[]>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        var status = NativeMethods.QueryTerminalShells(
            ShellsCallback,
            GCHandle.ToIntPtr(handle));
        if (status < 0)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    public Task<TerminalSession> CreateShellAsync(
        TerminalShell shell,
        ushort columns,
        ushort rows,
        string? workingDirectory = null)
    {
        var descriptor = GetTerminalShell(shell);
        return CreateTerminalAsync(descriptor.CommandLine,
                                   columns,
                                   rows,
                                   workingDirectory,
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
            TerminalShell.CommandPrompt => ($"cmd.exe /D /Q /C call \"{path}\"", GetTerminalShell(shell).Info),
            TerminalShell.WindowsPowerShell => ($"powershell.exe -File \"{path}\"", GetTerminalShell(shell).Info),
            TerminalShell.PowerShell => ($"pwsh.exe -File \"{path}\"", GetTerminalShell(shell).Info),
            TerminalShell.ConsoleScriptHost => ($"cscript.exe //NoLogo \"{path}\"",
                                                new TerminalShellInfo(shell, "VBScript (cscript)")),
            TerminalShell.WindowsScriptHost => ($"wscript.exe //NoLogo \"{path}\"",
                                                new TerminalShellInfo(shell, "VBScript (wscript)")),
            TerminalShell.HtmlApplication => ($"mshta.exe \"{path}\"",
                                              new TerminalShellInfo(shell, "HTML Application")),
            _ => throw new ArgumentOutOfRangeException(nameof(shell))
        };
        return CreateTerminalAsync(profile.Item1, columns, rows, null, profile.Item2);
    }

    public Task<TerminalSession> CreateTerminalAsync(
        string commandLine,
        ushort columns,
        ushort rows,
        string? workingDirectory = null) =>
        CreateTerminalAsync(commandLine,
                            columns,
                            rows,
                            workingDirectory,
                            null);

    private Task<TerminalSession> CreateTerminalAsync(
        string commandLine,
        ushort columns,
        ushort rows,
        string? workingDirectory,
        TerminalShellInfo? shell)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(commandLine);
        var completion = new TerminalCreation { Shell = shell };
        completion.Handle = GCHandle.Alloc(completion);
        var status = NativeMethods.CreateTerminal(
            columns,
            rows,
            commandLine,
            (uint)commandLine.Length,
            workingDirectory,
            (uint)(workingDirectory?.Length ?? 0),
            CreateCallback,
            DataCallback,
            WritableCallback,
            CloseCallback,
            GCHandle.ToIntPtr(completion.Handle));
        if (status < 0)
        {
            completion.Handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Completion.Task;
    }

    private static void CompleteTerminalShells(
        ZpStatus status,
        uint shells,
        nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var completion =
            (TaskCompletionSource<TerminalShellInfo[]>)handle.Target!;
        handle.Free();
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

    private sealed record TerminalShellDescriptor(
        TerminalShell Id,
        string Name,
        string CommandLine)
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
    private readonly Channel<ReadOnlyMemory<byte>> output =
        Channel.CreateBounded<ReadOnlyMemory<byte>>(
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
    public ChannelReader<ReadOnlyMemory<byte>> Output => output.Reader;
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
        var buffer = GC.AllocateUninitializedArray<byte>((int)dataLength);
        Marshal.Copy(data, buffer, 0, buffer.Length);
        return output.Writer.TryWrite(buffer);
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
        NativeMethods.CloseTerminal(terminal);
        await completion.Task;
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
        uint shells,
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
        TerminalShellsCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_CreateTerminal",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int CreateTerminal(
        ushort columns,
        ushort rows,
        string commandLine,
        uint commandLineLength,
        string? workingDirectory,
        uint workingDirectoryLength,
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
