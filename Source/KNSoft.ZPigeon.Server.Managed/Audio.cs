using System.Runtime.InteropServices;
using System.Threading.Channels;

namespace KNSoft.ZPigeon.Server.Managed;

public enum AudioFlow : byte
{
    Output = 1,
    Input = 2
}

public enum AudioEndpointControl : byte
{
    Volume = 1,
    Mute,
    Default,
    Enabled
}

public enum AudioSessionControl : byte
{
    Volume = 1,
    Mute
}

public sealed record AudioDevice(
    AudioFlow Flow,
    uint State,
    uint Flags,
    uint Volume,
    string Id,
    string Name);

public sealed record AudioSession(
    uint ProcessId,
    uint State,
    uint Flags,
    uint Volume,
    string DeviceId,
    string Id,
    string Name);

public readonly record struct AudioStreamCompletion(ZpStatus Status);

public sealed partial class NativeServer
{
    private static readonly NativeMethods.AudioDevicesCallback AudioDevicesCallback = CompleteAudioDevices;
    private static readonly NativeMethods.AudioSessionsCallback AudioSessionsCallback = CompleteAudioSessions;
    private static readonly NativeMethods.AudioStreamOpenCallback AudioStreamOpenCallback = CompleteAudioStreamOpen;
    private static readonly NativeMethods.AudioStreamDataCallback AudioStreamDataCallback = ReceiveAudioStreamData;
    private static readonly NativeMethods.AudioStreamCloseCallback AudioStreamCloseCallback = CompleteAudioStreamClose;

    public Task<AudioDevice[]> EnumerateAudioDevicesAsync() =>
        RunManagementAsync<AudioDevice[]>(context =>
            NativeMethods.EnumerateAudioDevices(ClientId, AudioDevicesCallback, context));

    public Task<AudioSession[]> EnumerateAudioSessionsAsync() =>
        RunManagementAsync<AudioSession[]>(context =>
            NativeMethods.EnumerateAudioSessions(ClientId, AudioSessionsCallback, context));

    public Task ControlAudioEndpointAsync(
        AudioFlow flow,
        AudioEndpointControl control,
        uint value,
        string deviceId) =>
        RunStatusAsync((callback, context) => NativeMethods.ControlAudioEndpoint(ClientId,
            flow,
            control,
            value,
            deviceId,
            (uint)deviceId.Length,
            callback,
            context));

    public Task ControlAudioSessionAsync(
        AudioSessionControl control,
        uint value,
        string deviceId,
        string sessionId) =>
        RunStatusAsync((callback, context) => NativeMethods.ControlAudioSession(ClientId,
            control,
            value,
            deviceId,
            (uint)deviceId.Length,
            sessionId,
            (uint)sessionId.Length,
            callback,
            context));

    public Task<AudioStream> OpenAudioStreamAsync(AudioFlow flow, string? deviceId, uint directStreamId)
    {
        var creation = new AudioStreamCreation();
        creation.Handle = GCHandle.Alloc(creation);
        var status = NativeMethods.OpenAudioStream(ClientId, flow,
                                                   directStreamId,
                                                   deviceId,
                                                   (uint)(deviceId?.Length ?? 0),
                                                   AudioStreamOpenCallback,
                                                   AudioStreamDataCallback,
                                                   AudioStreamCloseCallback,
                                                   GCHandle.ToIntPtr(creation.Handle));
        if (status < 0)
        {
            creation.Handle.Free();
            ThrowIfFailed(status);
        }
        return creation.Completion.Task;
    }

    private static void CompleteAudioDevices(ZpStatus status, nint records, uint recordCount, nint context)
    {
        var completion = GetCompletion<AudioDevice[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new AudioDevice[recordCount];
        var size = Marshal.SizeOf<NativeMethods.AudioDeviceRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.AudioDeviceRecord>(records + index * size);
            result[index] = new AudioDevice(record.Flow,
                                            record.State,
                                            record.Flags,
                                            record.Volume,
                                            ReadString(record.Id, record.IdLength),
                                            ReadString(record.Name, record.NameLength));
        }
        completion.SetResult(result);
    }

    private static void CompleteAudioSessions(ZpStatus status, nint records, uint recordCount, nint context)
    {
        var completion = GetCompletion<AudioSession[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new AudioSession[recordCount];
        var size = Marshal.SizeOf<NativeMethods.AudioSessionRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.AudioSessionRecord>(records + index * size);
            result[index] = new AudioSession(record.ProcessId,
                                              record.State,
                                              record.Flags,
                                              record.Volume,
                                              ReadString(record.DeviceId, record.DeviceIdLength),
                                              ReadString(record.Id, record.IdLength),
                                              ReadString(record.Name, record.NameLength));
        }
        completion.SetResult(result);
    }

    private static void CompleteAudioStreamOpen(ZpStatus status, nint stream, nint context)
    {
        var creation = GetAudioStreamCreation(context);
        if (!status.IsSuccess)
        {
            creation.Handle.Free();
            creation.Completion.SetException(new NativeException(status));
            return;
        }
        creation.Stream = new AudioStream(stream, creation);
        creation.Completion.SetResult(creation.Stream);
    }

    private static bool ReceiveAudioStreamData(nint data, uint dataLength, nint context) =>
        GetAudioStreamCreation(context).Stream?.Receive(data, dataLength) == true;

    private static void CompleteAudioStreamClose(ZpStatus status, nint context) =>
        GetAudioStreamCreation(context).Stream?.Complete(status);

    private static AudioStreamCreation GetAudioStreamCreation(nint context) =>
        (AudioStreamCreation)GCHandle.FromIntPtr(context).Target!;

    internal sealed class AudioStreamCreation
    {
        internal readonly TaskCompletionSource<AudioStream> Completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal GCHandle Handle;
        internal AudioStream? Stream;
    }
}

public sealed class AudioStream : IAsyncDisposable
{
    private readonly nint stream;
    private readonly NativeServer.AudioStreamCreation creation;
    private readonly Channel<OwnedBuffer> output =
        Channel.CreateBounded<OwnedBuffer>(new BoundedChannelOptions(32)
        {
            SingleReader = true,
            SingleWriter = true,
            FullMode = BoundedChannelFullMode.Wait
        });
    private readonly TaskCompletionSource<AudioStreamCompletion> completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int disposed;

    internal AudioStream(nint stream, NativeServer.AudioStreamCreation creation)
    {
        this.stream = stream;
        this.creation = creation;
    }

    public ChannelReader<OwnedBuffer> Output => output.Reader;
    public Task<AudioStreamCompletion> Completion => completion.Task;

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
        _ = NativeMethods.CloseAudioStream(stream);
        await completion.Task;
        OwnedBuffer.Drain(output.Reader);
        creation.Handle.Free();
    }
}

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void AudioDevicesCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void AudioSessionsCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void AudioStreamOpenCallback(ZpStatus status, nint stream, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal delegate bool AudioStreamDataCallback(nint data, uint dataLength, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void AudioStreamCloseCallback(ZpStatus status, nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct AudioDeviceRecord
    {
        internal readonly AudioFlow Flow;
        internal readonly uint State;
        internal readonly uint Flags;
        internal readonly uint Volume;
        internal readonly nint Id;
        internal readonly uint IdLength;
        internal readonly nint Name;
        internal readonly uint NameLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct AudioSessionRecord
    {
        internal readonly uint ProcessId;
        internal readonly uint State;
        internal readonly uint Flags;
        internal readonly uint Volume;
        internal readonly nint DeviceId;
        internal readonly uint DeviceIdLength;
        internal readonly nint Id;
        internal readonly uint IdLength;
        internal readonly nint Name;
        internal readonly uint NameLength;
    }

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateAudioDevices")]
    internal static partial int EnumerateAudioDevices(ulong clientId, AudioDevicesCallback callback, nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateAudioSessions")]
    internal static partial int EnumerateAudioSessions(ulong clientId, AudioSessionsCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ControlAudioEndpoint",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ControlAudioEndpoint(
        ulong clientId,
        AudioFlow flow,
        AudioEndpointControl control,
        uint value,
        string deviceId,
        uint deviceIdLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ControlAudioSession",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ControlAudioSession(
        ulong clientId,
        AudioSessionControl control,
        uint value,
        string deviceId,
        uint deviceIdLength,
        string sessionId,
        uint sessionIdLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_OpenAudioStream",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int OpenAudioStream(
        ulong clientId,
        AudioFlow flow,
        uint directStreamId,
        string? deviceId,
        uint deviceIdLength,
        AudioStreamOpenCallback openCallback,
        AudioStreamDataCallback dataCallback,
        AudioStreamCloseCallback closeCallback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CloseAudioStream")]
    internal static partial int CloseAudioStream(nint stream);
}
