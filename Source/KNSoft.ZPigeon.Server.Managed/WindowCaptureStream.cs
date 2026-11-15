using System.Runtime.InteropServices;
using System.Threading.Channels;
using System.Buffers.Binary;

namespace KNSoft.ZPigeon.Server.Managed;

public enum WindowCaptureMode : byte
{
    Image,
    Auto,
    Video
}

public enum WindowVideoCodec : byte
{
    H264,
    H265
}

public readonly record struct WindowCaptureOptions(
    bool CaptureCursor = false,
    uint MaxDimension = 1280,
    byte FrameRate = 12,
    byte ImageQuality = 85,
    bool Desktop = false,
    uint MonitorIndex = uint.MaxValue,
    WindowCaptureMode Mode = WindowCaptureMode.Image,
    WindowVideoCodec VideoCodec = WindowVideoCodec.H264,
    uint MaxBitRate = 100_000_000)
{
    internal uint Flags => (CaptureCursor ? 1U : 0) | (Desktop ? 2U : 0);
    internal byte Encoding => (byte)((byte)Mode | (byte)((byte)VideoCodec << 2));
}

public readonly record struct WindowCaptureCompletion(ZpStatus Status);

public sealed partial class NativeServer
{
    private static readonly NativeMethods.WindowCaptureOpenCallback WindowCaptureOpenCallback =
        CompleteWindowCaptureOpen;
    private static readonly NativeMethods.WindowCaptureDataCallback WindowCaptureDataCallback =
        ReceiveWindowCaptureData;
    private static readonly NativeMethods.WindowCaptureWritableCallback WindowCaptureWritableCallback =
        SignalWindowCaptureWritable;
    private static readonly NativeMethods.WindowCaptureCloseCallback WindowCaptureCloseCallback =
        CompleteWindowCaptureClose;

    public Task<WindowCaptureStream> OpenWindowCaptureAsync(
        ulong handle,
        uint processId,
        uint threadId,
        WindowCaptureOptions options,
        uint directStreamId)
    {
        ValidateWindowCaptureOptions(options);
        var creation = new WindowCaptureCreation();
        creation.Handle = GCHandle.Alloc(creation);
        var status = NativeMethods.OpenWindowCapture(ClientId, handle,
                                                     processId,
                                                     threadId,
                                                     options.Flags,
                                                     options.MaxDimension,
                                                     options.FrameRate,
                                                     options.ImageQuality,
                                                     directStreamId,
                                                     options.MonitorIndex,
                                                     options.Encoding,
                                                     WindowCaptureOpenCallback,
                                                     WindowCaptureDataCallback,
                                                     WindowCaptureWritableCallback,
                                                     WindowCaptureCloseCallback,
                                                     GCHandle.ToIntPtr(creation.Handle));
        if (status < 0)
        {
            creation.Handle.Free();
            ThrowIfFailed(status);
        }
        return creation.Completion.Task;
    }

    internal static void ValidateWindowCaptureOptions(WindowCaptureOptions options)
    {
        if (options.MaxDimension is 0 or > 7680 || options.FrameRate is 0 or > 60 ||
            options.ImageQuality is 0 or > 100 || options.Mode > WindowCaptureMode.Video ||
            options.VideoCodec > WindowVideoCodec.H265 || options.MaxBitRate is < 256_000 or > 100_000_000)
        {
            throw new ArgumentOutOfRangeException(nameof(options));
        }
    }

    private static void CompleteWindowCaptureOpen(ZpStatus status, nint stream, nint context)
    {
        var creation = GetWindowCaptureCreation(context);
        if (!status.IsSuccess)
        {
            creation.Handle.Free();
            creation.Completion.SetException(new NativeException(status));
            return;
        }
        creation.Stream = new WindowCaptureStream(stream, creation);
        creation.Completion.SetResult(creation.Stream);
    }

    private static bool ReceiveWindowCaptureData(nint data, uint dataLength, nint context) =>
        GetWindowCaptureCreation(context).Stream?.Receive(data, dataLength) == true;

    private static void SignalWindowCaptureWritable(uint creditBytes, nint context) =>
        GetWindowCaptureCreation(context).Stream?.SignalWritable();

    private static void CompleteWindowCaptureClose(ZpStatus status, nint context) =>
        GetWindowCaptureCreation(context).Stream?.Complete(status);

    private static WindowCaptureCreation GetWindowCaptureCreation(nint context) =>
        (WindowCaptureCreation)GCHandle.FromIntPtr(context).Target!;

    internal sealed class WindowCaptureCreation
    {
        internal readonly TaskCompletionSource<WindowCaptureStream> Completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal GCHandle Handle;
        internal WindowCaptureStream? Stream;
    }
}

public sealed class WindowCaptureStream : IAsyncDisposable
{
    private const int Retry = unchecked((int)0xC000022D);
    private const int InvalidDeviceState = unchecked((int)0xC0000184);
    private const int MaximumBufferedBytes = 32 * 1024 * 1024;
    private readonly nint stream;
    private readonly NativeServer.WindowCaptureCreation creation;
    private readonly Channel<OwnedBuffer> output =
        Channel.CreateBounded<OwnedBuffer>(new BoundedChannelOptions(128)
        {
            SingleReader = true,
            SingleWriter = true,
            FullMode = BoundedChannelFullMode.Wait
        });
    private readonly TaskCompletionSource<WindowCaptureCompletion> completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly SemaphoreSlim sendLock = new(1, 1);
    private readonly SemaphoreSlim writable = new(0);
    private readonly CancellationTokenSource closing = new();
    private int disposed;
    private int bufferedBytes;
    private readonly Action<int> releaseBuffer;

    internal WindowCaptureStream(nint stream, NativeServer.WindowCaptureCreation creation)
    {
        releaseBuffer = ReleaseBuffer;
        this.stream = stream;
        this.creation = creation;
    }

    public ChannelReader<OwnedBuffer> Output => output.Reader;
    public Task<WindowCaptureCompletion> Completion => completion.Task;

    internal bool Receive(nint data, uint dataLength)
    {
        if (disposed != 0 || dataLength > MaximumBufferedBytes) return false;
        if (Interlocked.Add(ref bufferedBytes, (int)dataLength) > MaximumBufferedBytes)
        {
            ReleaseBuffer((int)dataLength);
            return false;
        }
        var buffer = OwnedBuffer.Copy(data, (int)dataLength, releaseBuffer);
        if (output.Writer.TryWrite(buffer)) return true;
        buffer.Dispose();
        return false;
    }

    private void ReleaseBuffer(int length) => Interlocked.Add(ref bufferedBytes, -length);

    internal void Complete(ZpStatus status)
    {
        closing.Cancel();
        output.Writer.TryComplete();
        completion.TrySetResult(new(status));
    }

    internal void SignalWritable() => writable.Release();

    public async ValueTask SendAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        using var waiting = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, closing.Token);
        try
        {
            await sendLock.WaitAsync(waiting.Token);
            try
            {
                while (true)
                {
                    waiting.Token.ThrowIfCancellationRequested();
                    var status = Send(data.Span);
                    if (status != Retry)
                    {
                        NativeServer.ThrowIfFailed(status);
                        return;
                    }
                    await writable.WaitAsync(waiting.Token);
                }
            }
            finally
            {
                sendLock.Release();
            }
        }
        catch (OperationCanceledException) when (closing.IsCancellationRequested &&
                                                  !cancellationToken.IsCancellationRequested)
        {
            NativeServer.ThrowIfFailed(InvalidDeviceState);
        }
    }

    private unsafe int Send(ReadOnlySpan<byte> data)
    {
        fixed (byte* pointer = data)
        {
            return NativeMethods.SendWindowCaptureInput(stream, pointer, (uint)data.Length);
        }
    }

    public ValueTask UpdateAsync(WindowCaptureOptions options, CancellationToken cancellationToken = default)
    {
        NativeServer.ValidateWindowCaptureOptions(options);
        var message = new byte[sizeof(byte) + sizeof(ushort) + 3 * sizeof(byte) + sizeof(uint)];
        message[0] = 5;
        BinaryPrimitives.WriteUInt16LittleEndian(message.AsSpan(sizeof(byte)), (ushort)options.MaxDimension);
        message[sizeof(byte) + sizeof(ushort)] = options.FrameRate;
        message[sizeof(byte) + sizeof(ushort) + sizeof(byte)] = options.ImageQuality;
        message[sizeof(byte) + sizeof(ushort) + 2 * sizeof(byte)] = options.Encoding;
        BinaryPrimitives.WriteUInt32LittleEndian(message.AsSpan(sizeof(byte) + sizeof(ushort) + 3 * sizeof(byte)),
                                                options.MaxBitRate);
        return SendAsync(message, cancellationToken);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0) return;
        closing.Cancel();
        output.Writer.TryComplete();
        await sendLock.WaitAsync();
        try
        {
            _ = NativeMethods.CloseWindowCapture(stream);
        }
        finally
        {
            sendLock.Release();
        }
        await completion.Task;
        OwnedBuffer.Drain(output.Reader);
        creation.Handle.Free();
        sendLock.Dispose();
        writable.Dispose();
        closing.Dispose();
    }
}

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void WindowCaptureOpenCallback(ZpStatus status, nint stream, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal delegate bool WindowCaptureDataCallback(nint data, uint dataLength, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void WindowCaptureWritableCallback(uint creditBytes, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void WindowCaptureCloseCallback(ZpStatus status, nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_OpenWindowCapture")]
    internal static partial int OpenWindowCapture(
        ulong clientId,
        ulong handle,
        uint processId,
        uint threadId,
        uint flags,
        uint maxDimension,
        byte frameRate,
        byte quality,
        uint directStreamId,
        uint monitorIndex,
        byte encoding,
        WindowCaptureOpenCallback openCallback,
        WindowCaptureDataCallback dataCallback,
        WindowCaptureWritableCallback writableCallback,
        WindowCaptureCloseCallback closeCallback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CloseWindowCapture")]
    internal static partial int CloseWindowCapture(nint stream);

    [LibraryImport(Library, EntryPoint = "ZpNative_SendWindowCaptureInput")]
    internal static unsafe partial int SendWindowCaptureInput(
        nint stream,
        byte* data,
        uint dataLength);
}
