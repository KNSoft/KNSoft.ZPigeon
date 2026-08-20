using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer
{
    public unsafe Task<string> OpenRtcAsync(Guid sessionId, string offer, IReadOnlyList<string> iceServers)
    {
        if (string.IsNullOrEmpty(offer) || iceServers.Count > 8) throw new ArgumentException(nameof(offer));
        var id = sessionId.ToByteArray();
        var serverList = string.Join('\n', iceServers);
        var completion = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        int status;
        fixed (byte* idPointer = id)
        {
            status = NativeMethods.OpenRtc(
                (nint)idPointer,
                offer,
                (uint)offer.Length,
                serverList,
                (uint)serverList.Length,
                StringCallback,
                GCHandle.ToIntPtr(handle));
        }
        if (status >= 0) return completion.Task;
        handle.Free();
        ThrowIfFailed(status);
        return completion.Task;
    }

    public unsafe Task CloseRtcAsync(Guid sessionId)
    {
        var id = sessionId.ToByteArray();
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        int status;
        fixed (byte* idPointer = id)
        {
            status = NativeMethods.CloseRtc(
                (nint)idPointer,
                StatusCallback,
                GCHandle.ToIntPtr(handle));
        }
        if (status >= 0) return completion.Task;
        handle.Free();
        ThrowIfFailed(status);
        return completion.Task;
    }
}

internal static partial class NativeMethods
{
    [LibraryImport(Library, EntryPoint = "ZpNative_OpenRtc", StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int OpenRtc(
        nint sessionId,
        string offer,
        uint offerLength,
        string iceServers,
        uint iceServersLength,
        StringCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CloseRtc")]
    internal static partial int CloseRtc(nint sessionId, StatusCallback callback, nint context);
}
