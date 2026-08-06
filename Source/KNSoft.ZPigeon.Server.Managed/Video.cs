using System.Runtime.InteropServices;
using System.Threading.Channels;

namespace KNSoft.ZPigeon.Server.Managed;

public readonly record struct VideoFormat(
    uint Width,
    uint Height,
    uint FrameRateNumerator,
    uint FrameRateDenominator);
public sealed record VideoDevice(string Id, string Name, VideoFormat[] Formats);
public readonly record struct VideoStreamCompletion(ZpStatus Status);

public sealed partial class NativeServer
{
    private static readonly NativeMethods.VideoDevicesCallback VideoDevicesCallback = CompleteVideoDevices;
    private static readonly NativeMethods.VideoStreamOpenCallback VideoStreamOpenCallback = CompleteVideoStreamOpen;
    private static readonly NativeMethods.VideoStreamDataCallback VideoStreamDataCallback = ReceiveVideoStreamData;
    private static readonly NativeMethods.VideoStreamCloseCallback VideoStreamCloseCallback = CompleteVideoStreamClose;

    public Task<VideoDevice[]> EnumerateVideoDevicesAsync() =>
        RunManagementAsync<VideoDevice[]>(context =>
            NativeMethods.EnumerateVideoDevices(ClientId, VideoDevicesCallback, context));

    public Task<VideoStream> OpenVideoStreamAsync(
        string deviceId,
        VideoFormat format,
        byte quality,
        uint directStreamId)
    {
        var creation = new VideoStreamCreation();
        creation.Handle = GCHandle.Alloc(creation);
        var status = NativeMethods.OpenVideoStream(ClientId, deviceId,
                                                   (uint)deviceId.Length,
                                                   format.Width,
                                                   format.Height,
                                                   format.FrameRateNumerator,
                                                   format.FrameRateDenominator,
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
            var formats = new VideoFormat[record.FormatCount];
            var formatSize = Marshal.SizeOf<NativeMethods.VideoFormatRecord>();
            for (var formatIndex = 0; formatIndex < formats.Length; formatIndex++)
            {
                var format = Marshal.PtrToStructure<NativeMethods.VideoFormatRecord>(
                    record.Formats + formatIndex * formatSize);
                formats[formatIndex] = new(format.Width,
                                           format.Height,
                                           format.FrameRateNumerator,
                                           format.FrameRateDenominator);
            }
            result[index] = new VideoDevice(
                ReadString(record.Id, record.IdLength),
                ReadString(record.Name, record.NameLength),
                formats);
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
    private readonly Channel<OwnedBuffer> output =
        Channel.CreateBounded<OwnedBuffer>(new BoundedChannelOptions(8)
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

    public ChannelReader<OwnedBuffer> Output => output.Reader;
    public Task<VideoStreamCompletion> Completion => completion.Task;

    public Task UpdateAsync(VideoFormat format, byte quality)
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        var result = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(result);
        var status = NativeMethods.UpdateVideoStream(stream,
                                                     format.Width,
                                                     format.Height,
                                                     format.FrameRateNumerator,
                                                     format.FrameRateDenominator,
                                                     quality,
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

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0) return;
        output.Writer.TryComplete();
        _ = NativeMethods.CloseVideoStream(stream);
        await completion.Task;
        OwnedBuffer.Drain(output.Reader);
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
        internal readonly nint Formats;
        internal readonly uint FormatCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct VideoFormatRecord
    {
        internal readonly uint Width;
        internal readonly uint Height;
        internal readonly uint FrameRateNumerator;
        internal readonly uint FrameRateDenominator;
    }

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateVideoDevices")]
    internal static partial int EnumerateVideoDevices(ulong clientId, VideoDevicesCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_OpenVideoStream",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int OpenVideoStream(
        ulong clientId,
        string deviceId,
        uint deviceIdLength,
        uint width,
        uint height,
        uint frameRateNumerator,
        uint frameRateDenominator,
        byte quality,
        uint directStreamId,
        VideoStreamOpenCallback openCallback,
        VideoStreamDataCallback dataCallback,
        VideoStreamCloseCallback closeCallback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_UpdateVideoStream")]
    internal static partial int UpdateVideoStream(
        nint stream,
        uint width,
        uint height,
        uint frameRateNumerator,
        uint frameRateDenominator,
        byte quality,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CloseVideoStream")]
    internal static partial int CloseVideoStream(nint stream);
}
