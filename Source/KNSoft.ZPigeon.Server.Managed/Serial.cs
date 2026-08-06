using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed record SerialPort(string Name, string Device);

public enum SerialParity : byte
{
    None,
    Odd,
    Even,
    Mark,
    Space
}

public enum SerialStopBits : byte
{
    One,
    OneAndHalf,
    Two
}

public enum SerialFlowControl : byte
{
    None,
    XonXoff,
    RtsCts,
    DsrDtr
}

public sealed partial class NativeServer
{
    private static readonly NativeMethods.SerialPortsCallback SerialPortsCallback = CompleteSerialPorts;

    public Task<SerialPort[]> EnumerateSerialPortsAsync() =>
        RunManagementAsync<SerialPort[]>(context =>
            NativeMethods.EnumerateSerialPorts(ClientId, SerialPortsCallback, context));

    public Task<RemoteTunnel> OpenSerialPortAsync(
        string port,
        uint baudRate,
        byte dataBits,
        SerialParity parity,
        SerialStopBits stopBits,
        SerialFlowControl flowControl)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(port);
        var creation = new TunnelCreation();
        creation.Handle = GCHandle.Alloc(creation);
        var status = NativeMethods.OpenSerialPort(ClientId, port,
                                                  (uint)port.Length,
                                                  baudRate,
                                                  dataBits,
                                                  parity,
                                                  stopBits,
                                                  flowControl,
                                                  TunnelOpenCallback,
                                                  TunnelDataCallback,
                                                  TunnelWritableCallback,
                                                  TunnelCloseCallback,
                                                  GCHandle.ToIntPtr(creation.Handle));
        if (status < 0)
        {
            creation.Handle.Free();
            ThrowIfFailed(status);
        }
        return creation.Completion.Task;
    }

    private static void CompleteSerialPorts(ZpStatus status, nint records, uint recordCount, nint context)
    {
        var completion = GetCompletion<SerialPort[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new SerialPort[recordCount];
        var size = Marshal.SizeOf<NativeMethods.SerialPortRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.SerialPortRecord>(records + index * size);
            result[index] = new SerialPort(
                ReadString(record.Name, record.NameLength),
                ReadString(record.Device, record.DeviceLength));
        }
        completion.SetResult(result);
    }
}

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void SerialPortsCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct SerialPortRecord
    {
        internal readonly nint Name;
        internal readonly uint NameLength;
        internal readonly nint Device;
        internal readonly uint DeviceLength;
    }

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateSerialPorts")]
    internal static partial int EnumerateSerialPorts(ulong clientId, SerialPortsCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_OpenSerialPort",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int OpenSerialPort(
        ulong clientId,
        string port,
        uint portLength,
        uint baudRate,
        byte dataBits,
        SerialParity parity,
        SerialStopBits stopBits,
        SerialFlowControl flowControl,
        TunnelOpenCallback openCallback,
        TunnelDataCallback dataCallback,
        TunnelWritableCallback writableCallback,
        TunnelCloseCallback closeCallback,
        nint context);
}
