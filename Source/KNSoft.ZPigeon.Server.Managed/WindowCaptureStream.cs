using System.Runtime.InteropServices;
using System.Threading.Channels;

namespace KNSoft.ZPigeon.Server.Managed;

public readonly record struct WindowCaptureOptions(
    bool CaptureCursor = false,
    uint MaxDimension = 1280,
    ushort FrameRate = 12,
    ushort ImageQuality = 85)
{
    internal uint Flags => CaptureCursor ? 1U : 0;
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
        WindowCaptureOptions options)
    {
        ValidateWindowCaptureOptions(options);
        var creation = new WindowCaptureCreation();
        creation.Handle = GCHandle.Alloc(creation);
        var status = NativeMethods.OpenWindowCapture(handle,
                                                     processId,
                                                     threadId,
                                                     options.Flags,
                                                     options.MaxDimension,
                                                     options.FrameRate,
                                                     options.ImageQuality,
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
            options.ImageQuality is 0 or > 100)
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
    private readonly Channel<ReadOnlyMemory<byte>> output =
        Channel.CreateBounded<ReadOnlyMemory<byte>>(new BoundedChannelOptions(16)
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

    public ChannelReader<ReadOnlyMemory<byte>> Output => output.Reader;
    public Task<WindowCaptureCompletion> Completion => completion.Task;

    internal bool Receive(nint data, uint dataLength)
    {
        if (disposed != 0) return false;
        var buffer = GC.AllocateUninitializedArray<byte>((int)dataLength);
        Marshal.Copy(data, buffer, 0, buffer.Length);
        return output.Writer.TryWrite(buffer);
    }

    internal void Complete(ZpStatus status)
    {
        output.Writer.TryComplete();
        completion.TrySetResult(new(status));
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0) return;
        output.Writer.TryComplete();
        NativeMethods.CloseWindowCapture(stream);
        await completion.Task;
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
        ulong handle,
        uint processId,
        uint threadId,
        uint flags,
        uint maxDimension,
        ushort frameRate,
        ushort quality,
        WindowCaptureOpenCallback openCallback,
        WindowCaptureDataCallback dataCallback,
        WindowCaptureCloseCallback closeCallback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CloseWindowCapture")]
    internal static partial int CloseWindowCapture(nint stream);
}
