using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer
{
    public unsafe Task<string> OpenRtcAsync(Guid sessionId, string offer, IReadOnlyList<string> iceServers)
    {
        ArgumentException.ThrowIfNullOrEmpty(offer);
        if (iceServers.Count > 8) throw new ArgumentOutOfRangeException(nameof(iceServers));
        var id = sessionId.ToByteArray();
        var serverList = string.Join('\n', iceServers);
        fixed (byte* idPointer = id)
        {
            var identifier = (nint)idPointer;
            return RunOperationAsync<string>(context => NativeMethods.OpenRtc(ClientId,
                identifier,
                offer,
                (uint)offer.Length,
                serverList,
                (uint)serverList.Length,
                StringCallback,
                context));
        }
    }

    public unsafe Task CloseRtcAsync(Guid sessionId)
    {
        var id = sessionId.ToByteArray();
        fixed (byte* idPointer = id)
        {
            var identifier = (nint)idPointer;
            return RunStatusAsync((callback, context) => NativeMethods.CloseRtc(ClientId,
                identifier,
                callback,
                context));
        }
    }
}

internal static partial class NativeMethods
{
    [LibraryImport(Library, EntryPoint = "ZpNative_OpenRtc", StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int OpenRtc(
        ulong clientId,
        nint sessionId,
        string offer,
        uint offerLength,
        string iceServers,
        uint iceServersLength,
        StringCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CloseRtc")]
    internal static partial int CloseRtc(ulong clientId, nint sessionId, StatusCallback callback, nint context);
}
