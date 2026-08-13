using System.Runtime.InteropServices;
using System.Threading.Channels;

namespace KNSoft.ZPigeon.Server.Managed;

public readonly record struct FileTransferCompletion(ZpStatus Status);

public sealed partial class NativeServer
{
    private static readonly NativeMethods.FileOpenCallback FileOpenCallback = CompleteFileOpen;
    private static readonly NativeMethods.FileDataCallback FileDataCallback = ReceiveFileData;
    private static readonly NativeMethods.FileWritableCallback FileWritableCallback = SignalFileWritable;
    private static readonly NativeMethods.FileCloseCallback FileCloseCallback = CompleteFileClose;

    public Task<FileTransfer> OpenFileReadAsync(string path) =>
        OpenFileAsync(path, 0, false, false);

    public Task<FileTransfer> OpenFileWriteAsync(string path, ulong fileSize, bool overwrite) =>
        OpenFileAsync(path, fileSize, overwrite, true);

    private static Task<FileTransfer> OpenFileAsync(string path, ulong fileSize, bool overwrite, bool write)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        var creation = new FileTransferCreation(write);
        creation.Handle = GCHandle.Alloc(creation);
        var context = GCHandle.ToIntPtr(creation.Handle);
        var status = write ?
            NativeMethods.OpenFileWrite(path,
                                        (uint)path.Length,
                                        fileSize,
                                        overwrite,
                                        FileOpenCallback,
                                        FileWritableCallback,
                                        FileCloseCallback,
                                        context) :
            NativeMethods.OpenFileRead(path,
                                       (uint)path.Length,
                                       FileOpenCallback,
                                       FileDataCallback,
                                       FileCloseCallback,
                                       context);
        if (status < 0)
        {
            creation.Handle.Free();
            ThrowIfFailed(status);
        }
        return creation.Completion.Task;
    }

    private static void CompleteFileOpen(ZpStatus status, nint transfer, ulong fileSize, nint context)
    {
        var creation = GetFileTransferCreation(context);
        if (!status.IsSuccess)
        {
            creation.Handle.Free();
            creation.Completion.SetException(new NativeException(status));
            return;
        }
        creation.Transfer = new FileTransfer(transfer, fileSize, creation);
        creation.Completion.SetResult(creation.Transfer);
    }

    private static bool ReceiveFileData(nint data, uint dataLength, nint context) =>
        GetFileTransferCreation(context).Transfer?.Receive(data, dataLength) == true;

    private static void SignalFileWritable(uint creditBytes, nint context) =>
        GetFileTransferCreation(context).Transfer?.SignalWritable();

    private static void CompleteFileClose(ZpStatus status, nint context) =>
        GetFileTransferCreation(context).Transfer?.Complete(status);

    private static FileTransferCreation GetFileTransferCreation(nint context) =>
        (FileTransferCreation)GCHandle.FromIntPtr(context).Target!;

    internal sealed class FileTransferCreation(bool write)
    {
        internal readonly TaskCompletionSource<FileTransfer> Completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal readonly bool Write = write;
        internal GCHandle Handle;
        internal FileTransfer? Transfer;
    }
}

public sealed class FileTransfer : IAsyncDisposable
{
    private const int Retry = unchecked((int)0xC000022D);
    private const int ChunkSize = 0x10000;
    private readonly nint transfer;
    private readonly NativeServer.FileTransferCreation creation;
    private readonly Channel<ReadOnlyMemory<byte>> output =
        Channel.CreateBounded<ReadOnlyMemory<byte>>(new BoundedChannelOptions(16)
        {
            SingleReader = true,
            SingleWriter = true,
            FullMode = BoundedChannelFullMode.Wait
        });
    private readonly SemaphoreSlim sendLock = new(1, 1);
    private readonly SemaphoreSlim writable = new(0);
    private readonly TaskCompletionSource<FileTransferCompletion> completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int disposed;

    internal FileTransfer(nint transfer, ulong fileSize, NativeServer.FileTransferCreation creation)
    {
        this.transfer = transfer;
        this.creation = creation;
        FileSize = fileSize;
    }

    public ulong FileSize { get; }
    public bool IsWrite => creation.Write;
    public ChannelReader<ReadOnlyMemory<byte>> Output => output.Reader;
    public Task<FileTransferCompletion> Completion => completion.Task;

    public async ValueTask WriteAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        if (!IsWrite)
        {
            throw new InvalidOperationException("The transfer is read-only.");
        }
        await sendLock.WaitAsync(cancellationToken);
        try
        {
            for (var offset = 0; offset < data.Length;)
            {
                var length = Math.Min(ChunkSize, data.Length - offset);
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

    internal bool Receive(nint data, uint dataLength)
    {
        if (disposed != 0 || IsWrite)
        {
            return false;
        }
        var buffer = GC.AllocateUninitializedArray<byte>((int)dataLength);
        Marshal.Copy(data, buffer, 0, buffer.Length);
        try
        {
            output.Writer.WriteAsync(buffer).AsTask().GetAwaiter().GetResult();
            return true;
        }
        catch (ChannelClosedException)
        {
            return false;
        }
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
            return NativeMethods.FileSend(transfer, pointer, (uint)data.Length);
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0)
        {
            return;
        }
        output.Writer.TryComplete();
        NativeMethods.CloseFileTransfer(transfer);
        await completion.Task;
        creation.Handle.Free();
        sendLock.Dispose();
        writable.Dispose();
    }
}

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void FileOpenCallback(ZpStatus status, nint transfer, ulong fileSize, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal delegate bool FileDataCallback(nint data, uint dataLength, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void FileWritableCallback(uint creditBytes, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void FileCloseCallback(ZpStatus status, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_OpenFileRead",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int OpenFileRead(
        string path,
        uint pathLength,
        FileOpenCallback openCallback,
        FileDataCallback dataCallback,
        FileCloseCallback closeCallback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_OpenFileWrite",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int OpenFileWrite(
        string path,
        uint pathLength,
        ulong fileSize,
        [MarshalAs(UnmanagedType.Bool)] bool overwrite,
        FileOpenCallback openCallback,
        FileWritableCallback writableCallback,
        FileCloseCallback closeCallback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_FileSend")]
    internal static unsafe partial int FileSend(nint transfer, byte* data, uint dataLength);

    [LibraryImport(Library, EntryPoint = "ZpNative_CloseFileTransfer")]
    internal static partial int CloseFileTransfer(nint transfer);
}
