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
    WindowVideoCodec VideoCodec = WindowVideoCodec.H264)
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
            options.VideoCodec > WindowVideoCodec.H265)
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
    private readonly nint stream;
    private readonly NativeServer.WindowCaptureCreation creation;
    private readonly Channel<OwnedBuffer> output =
        Channel.CreateBounded<OwnedBuffer>(new BoundedChannelOptions(8)
        {
            SingleReader = true,
            SingleWriter = true,
            FullMode = BoundedChannelFullMode.Wait
        });
    private readonly TaskCompletionSource<WindowCaptureCompletion> completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int disposed;

    internal WindowCaptureStream(nint stream, NativeServer.WindowCaptureCreation creation)
    {
        this.stream = stream;
        this.creation = creation;
    }

    public ChannelReader<OwnedBuffer> Output => output.Reader;
    public Task<WindowCaptureCompletion> Completion => completion.Task;

    internal bool Receive(nint data, uint dataLength)
    {
        if (disposed != 0) return false;
        var buffer = OwnedBuffer.Copy(data, (int)dataLength);
        if (output.Writer.TryWrite(buffer)) return true;
        buffer.Dispose();
        return false;
    }

    internal void Complete(ZpStatus status)
    {
        output.Writer.TryComplete();
        completion.TrySetResult(new(status));
    }

    public unsafe void Send(ReadOnlySpan<byte> data)
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        fixed (byte* pointer = data)
        {
            NativeServer.ThrowIfFailed(NativeMethods.SendWindowCaptureInput(
                stream,
                pointer,
                (uint)data.Length));
        }
    }

    public void Update(WindowCaptureOptions options)
    {
        NativeServer.ValidateWindowCaptureOptions(options);
        Span<byte> message = stackalloc byte[sizeof(byte) + sizeof(uint) + 3 * sizeof(byte)];
        message[0] = 5;
        BinaryPrimitives.WriteUInt32LittleEndian(message[sizeof(byte)..], options.MaxDimension);
        message[sizeof(byte) + sizeof(uint)] = options.FrameRate;
        message[sizeof(byte) + sizeof(uint) + sizeof(byte)] = options.ImageQuality;
        message[sizeof(byte) + sizeof(uint) + 2 * sizeof(byte)] = options.Encoding;
        Send(message);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0) return;
        output.Writer.TryComplete();
        _ = NativeMethods.CloseWindowCapture(stream);
        await completion.Task;
        OwnedBuffer.Drain(output.Reader);
        creation.Handle.Free();
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
