using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public enum RecordingSource : ushort
{
    AudioOutput = 1,
    AudioInput,
    Camera,
    Window
}

public enum RecordingCodec : ushort
{
    Auto = 1,
    Pcm,
    Aac,
    H264,
    H265,
    WmvScreen
}

public enum RecordingAudioSource : ushort
{
    None,
    Output,
    Input
}

public enum RecordingState : ushort
{
    Recording = 1,
    Finalizing,
    Completed,
    Interrupted,
    Failed
}

[Flags]
public enum RecordingFlags : ushort
{
    CaptureCursor = 1
}

public sealed record RecordingOptions(
    RecordingSource Source,
    RecordingCodec Codec,
    ushort FrameRate,
    RecordingAudioSource AudioSource,
    RecordingFlags Flags,
    uint MaxDimension,
    uint VideoBitRate,
    uint AudioBitRate,
    ulong WindowHandle,
    string? SourceId,
    string? AudioDeviceId);

public sealed record RecordingRecord(
    uint RecordingId,
    RecordingSource Source,
    RecordingCodec Codec,
    RecordingState State,
    ZpStatus Status,
    ulong StartTime,
    ulong Duration,
    ulong FileSize,
    string Path);

public sealed partial class NativeServer
{
    private static readonly NativeMethods.RecordingCapabilitiesCallback RecordingCapabilitiesCallback =
        CompleteRecordingCapabilities;
    private static readonly NativeMethods.RecordingRecordsCallback RecordingRecordsCallback = CompleteRecordingRecords;

    public Task<uint> QueryRecordingCapabilitiesAsync() =>
        RunManagementAsync<uint>(context => NativeMethods.QueryRecordingCapabilities(
            RecordingCapabilitiesCallback,
            context));

    public async Task<RecordingRecord> StartRecordingAsync(RecordingOptions options)
    {
        var records = await RunManagementAsync<RecordingRecord[]>(context => NativeMethods.StartRecording(
            options.Source,
            options.Codec,
            options.FrameRate,
            options.AudioSource,
            options.Flags,
            options.MaxDimension,
            options.VideoBitRate,
            options.AudioBitRate,
            options.WindowHandle,
            options.SourceId,
            (uint)(options.SourceId?.Length ?? 0),
            options.AudioDeviceId,
            (uint)(options.AudioDeviceId?.Length ?? 0),
            RecordingRecordsCallback,
            context));
        return records.Length == 1 ? records[0] : throw new InvalidDataException("Invalid recording response");
    }

    public Task<RecordingRecord[]> EnumerateRecordingsAsync() =>
        RunManagementAsync<RecordingRecord[]>(context => NativeMethods.EnumerateRecordings(
            RecordingRecordsCallback,
            context));

    public Task StopRecordingAsync(uint recordingId) =>
        RunStatusAsync((callback, context) => NativeMethods.StopRecording(recordingId, callback, context));

    public Task DeleteRecordingAsync(uint recordingId) =>
        RunStatusAsync((callback, context) => NativeMethods.DeleteRecording(recordingId, callback, context));

    private static void CompleteRecordingCapabilities(ZpStatus status, uint codecs, nint context)
    {
        var completion = GetCompletion<uint>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        completion.SetResult(codecs);
    }

    private static void CompleteRecordingRecords(ZpStatus status, nint records, uint recordCount, nint context)
    {
        var completion = GetCompletion<RecordingRecord[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new RecordingRecord[recordCount];
        var size = Marshal.SizeOf<NativeMethods.RecordingRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.RecordingRecord>(records + index * size);
            result[index] = new RecordingRecord(
                record.RecordingId,
                record.Source,
                record.Codec,
                record.State,
                record.Status,
                record.StartTime,
                record.Duration,
                record.FileSize,
                Marshal.PtrToStringUni(record.Path, (int)record.PathLength) ?? string.Empty);
        }
        completion.SetResult(result);
    }
}

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void RecordingCapabilitiesCallback(ZpStatus status, uint codecs, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void RecordingRecordsCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct RecordingRecord
    {
        internal readonly uint RecordingId;
        internal readonly RecordingSource Source;
        internal readonly RecordingCodec Codec;
        internal readonly RecordingState State;
        internal readonly ZpStatus Status;
        internal readonly ulong StartTime;
        internal readonly ulong Duration;
        internal readonly ulong FileSize;
        internal readonly nint Path;
        internal readonly uint PathLength;
    }

    [LibraryImport(Library, EntryPoint = "ZpNative_QueryRecordingCapabilities")]
    internal static partial int QueryRecordingCapabilities(RecordingCapabilitiesCallback callback, nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_StartRecording", StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int StartRecording(
        RecordingSource source,
        RecordingCodec codec,
        ushort frameRate,
        RecordingAudioSource audioSource,
        RecordingFlags flags,
        uint maxDimension,
        uint videoBitRate,
        uint audioBitRate,
        ulong windowHandle,
        string? sourceId,
        uint sourceIdLength,
        string? audioDeviceId,
        uint audioDeviceIdLength,
        RecordingRecordsCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateRecordings")]
    internal static partial int EnumerateRecordings(RecordingRecordsCallback callback, nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_StopRecording")]
    internal static partial int StopRecording(uint recordingId, StatusCallback callback, nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_DeleteRecording")]
    internal static partial int DeleteRecording(uint recordingId, StatusCallback callback, nint context);
}
