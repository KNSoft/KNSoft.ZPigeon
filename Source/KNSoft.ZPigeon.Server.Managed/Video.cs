using System.Runtime.InteropServices;
using System.Threading.Channels;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed record VideoDevice(string Id, string Name);
public readonly record struct VideoStreamCompletion(ZpStatus Status);

public sealed partial class NativeServer
{
    private static readonly NativeMethods.VideoDevicesCallback VideoDevicesCallback = CompleteVideoDevices;
    private static readonly NativeMethods.VideoStreamOpenCallback VideoStreamOpenCallback = CompleteVideoStreamOpen;
    private static readonly NativeMethods.VideoStreamDataCallback VideoStreamDataCallback = ReceiveVideoStreamData;
    private static readonly NativeMethods.VideoStreamCloseCallback VideoStreamCloseCallback = CompleteVideoStreamClose;

    public Task<VideoDevice[]> EnumerateVideoDevicesAsync() =>
        RunManagementAsync<VideoDevice[]>(context => NativeMethods.EnumerateVideoDevices(VideoDevicesCallback, context));

    public Task<VideoStream> OpenVideoStreamAsync(
        string deviceId,
        uint maxDimension,
        ushort frameRate,
        ushort quality,
        uint directStreamId)
    {
        var creation = new VideoStreamCreation();
        creation.Handle = GCHandle.Alloc(creation);
        var status = NativeMethods.OpenVideoStream(deviceId,
                                                   (uint)deviceId.Length,
                                                   maxDimension,
                                                   frameRate,
                                                   quality,
                                                   directStreamId,
                                                   VideoStreamOpenCallback,
                                                   VideoStreamDataCallback,
                                                   VideoStreamCloseCallback,
                                                   GCHandle.ToIntPtr(creation.Handle));
        if (status < 0)
        {
            creation.Handle.Free();
            ThrowIfFailed(status);
        }
        return creation.Completion.Task;
    }

    private static void CompleteVideoDevices(ZpStatus status, nint records, uint recordCount, nint context)
    {
        var completion = GetCompletion<VideoDevice[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new VideoDevice[recordCount];
        var size = Marshal.SizeOf<NativeMethods.VideoDeviceRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.VideoDeviceRecord>(records + index * size);
            result[index] = new VideoDevice(
                Marshal.PtrToStringUni(record.Id, (int)record.IdLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.Name, (int)record.NameLength) ?? string.Empty);
        }
        completion.SetResult(result);
    }

    private static void CompleteVideoStreamOpen(ZpStatus status, nint stream, nint context)
    {
        var creation = GetVideoStreamCreation(context);
        if (!status.IsSuccess)
        {
            creation.Handle.Free();
            creation.Completion.SetException(new NativeException(status));
            return;
        }
        creation.Stream = new VideoStream(stream, creation);
        creation.Completion.SetResult(creation.Stream);
    }

    private static bool ReceiveVideoStreamData(nint data, uint dataLength, nint context) =>
        GetVideoStreamCreation(context).Stream?.Receive(data, dataLength) == true;

    private static void CompleteVideoStreamClose(ZpStatus status, nint context) =>
        GetVideoStreamCreation(context).Stream?.Complete(status);

    private static VideoStreamCreation GetVideoStreamCreation(nint context) =>
        (VideoStreamCreation)GCHandle.FromIntPtr(context).Target!;

    internal sealed class VideoStreamCreation
    {
        internal readonly TaskCompletionSource<VideoStream> Completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal GCHandle Handle;
        internal VideoStream? Stream;
    }
}

public sealed class VideoStream : IAsyncDisposable
{
    private readonly nint stream;
    private readonly NativeServer.VideoStreamCreation creation;
    private readonly Channel<ReadOnlyMemory<byte>> output =
        Channel.CreateBounded<ReadOnlyMemory<byte>>(new BoundedChannelOptions(8)
        {
            SingleReader = true,
            SingleWriter = true,
            FullMode = BoundedChannelFullMode.Wait
        });
    private readonly TaskCompletionSource<VideoStreamCompletion> completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int disposed;

    internal VideoStream(nint stream, NativeServer.VideoStreamCreation creation)
    {
        this.stream = stream;
        this.creation = creation;
    }

    public ChannelReader<ReadOnlyMemory<byte>> Output => output.Reader;
    public Task<VideoStreamCompletion> Completion => completion.Task;

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
        NativeMethods.CloseVideoStream(stream);
        await completion.Task;
        creation.Handle.Free();
    }
}

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void VideoDevicesCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void VideoStreamOpenCallback(ZpStatus status, nint stream, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal delegate bool VideoStreamDataCallback(nint data, uint dataLength, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void VideoStreamCloseCallback(ZpStatus status, nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct VideoDeviceRecord
    {
        internal readonly nint Id;
        internal readonly uint IdLength;
        internal readonly nint Name;
        internal readonly uint NameLength;
    }

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateVideoDevices")]
    internal static partial int EnumerateVideoDevices(VideoDevicesCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_OpenVideoStream",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int OpenVideoStream(
        string deviceId,
        uint deviceIdLength,
        uint maxDimension,
        ushort frameRate,
        ushort quality,
        uint directStreamId,
        VideoStreamOpenCallback openCallback,
        VideoStreamDataCallback dataCallback,
        VideoStreamCloseCallback closeCallback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CloseVideoStream")]
    internal static partial int CloseVideoStream(nint stream);
}
