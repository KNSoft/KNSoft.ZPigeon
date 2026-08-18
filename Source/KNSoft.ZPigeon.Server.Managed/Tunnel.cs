using System.Runtime.InteropServices;
using System.Threading.Channels;

namespace KNSoft.ZPigeon.Server.Managed;

public readonly record struct TunnelCompletion(ZpStatus Status);

public sealed partial class NativeServer
{
    private const ushort TunnelProtocolTcp = 1;
    private const ushort TunnelProtocolUdp = 2;
    private static readonly NativeMethods.TunnelOpenCallback TunnelOpenCallback = CompleteTunnelOpen;
    private static readonly NativeMethods.TunnelDataCallback TunnelDataCallback = ReceiveTunnelData;
    private static readonly NativeMethods.TunnelWritableCallback TunnelWritableCallback = SignalTunnelWritable;
    private static readonly NativeMethods.TunnelCloseCallback TunnelCloseCallback = CompleteTunnelClose;

    public Task<RemoteTunnel> OpenTunnelAsync(string host, ushort port) =>
        OpenTunnelAsync(host, port, TunnelProtocolTcp);

    public Task<RemoteTunnel> OpenUdpTunnelAsync(string host, ushort port) =>
        OpenTunnelAsync(host, port, TunnelProtocolUdp);

    private Task<RemoteTunnel> OpenTunnelAsync(string host, ushort port, ushort protocol)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(host);
        ArgumentOutOfRangeException.ThrowIfZero(port);
        if (host.Length > 255 || host.Contains('\0'))
        {
            throw new ArgumentException("Invalid tunnel target host.", nameof(host));
        }
        var creation = new TunnelCreation();
        creation.Datagram = protocol == TunnelProtocolUdp;
        creation.Handle = GCHandle.Alloc(creation);
        var status = NativeMethods.OpenTunnel(host,
                                              (uint)host.Length,
                                              port,
                                              protocol,
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

    private static void CompleteTunnelOpen(ZpStatus status, nint tunnel, nint context)
    {
        var creation = GetTunnelCreation(context);
        if (!status.IsSuccess)
        {
            creation.Handle.Free();
            creation.Completion.SetException(new NativeException(status));
            return;
        }
        creation.Tunnel = new RemoteTunnel(tunnel, creation, creation.Datagram);
        creation.Completion.SetResult(creation.Tunnel);
    }

    private static bool ReceiveTunnelData(nint data, uint dataLength, nint context) =>
        GetTunnelCreation(context).Tunnel?.Receive(data, dataLength) == true;

    private static void SignalTunnelWritable(uint creditBytes, nint context) =>
        GetTunnelCreation(context).Tunnel?.SignalWritable();

    private static void CompleteTunnelClose(ZpStatus status, nint context) =>
        GetTunnelCreation(context).Tunnel?.Complete(status);

    private static TunnelCreation GetTunnelCreation(nint context) =>
        (TunnelCreation)GCHandle.FromIntPtr(context).Target!;

    internal sealed class TunnelCreation
    {
        internal readonly TaskCompletionSource<RemoteTunnel> Completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal GCHandle Handle;
        internal RemoteTunnel? Tunnel;
        internal bool Datagram;
    }
}

public sealed class RemoteTunnel : IAsyncDisposable
{
    private const int Retry = unchecked((int)0xC000022D);
    private const int ChunkSize = 0x10000;
    private const int DatagramMaxSize = 65507;
    private const int DatagramFrameMaxSize = DatagramMaxSize + 1;
    private readonly nint tunnel;
    private readonly NativeServer.TunnelCreation creation;
    private readonly bool datagram;
    private readonly Channel<ReadOnlyMemory<byte>> output =
        Channel.CreateBounded<ReadOnlyMemory<byte>>(new BoundedChannelOptions(16)
        {
            SingleReader = true,
            SingleWriter = true,
            FullMode = BoundedChannelFullMode.Wait
        });
    private readonly SemaphoreSlim sendLock = new(1, 1);
    private readonly SemaphoreSlim writable = new(0);
    private readonly TaskCompletionSource<TunnelCompletion> completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int disposed;

    internal RemoteTunnel(nint tunnel, NativeServer.TunnelCreation creation, bool datagram)
    {
        this.tunnel = tunnel;
        this.creation = creation;
        this.datagram = datagram;
    }

    public ChannelReader<ReadOnlyMemory<byte>> Output => output.Reader;
    public Task<TunnelCompletion> Completion => completion.Task;

    public async ValueTask WriteAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        if (datagram)
        {
            throw new InvalidOperationException("Use WriteDatagramAsync for a UDP tunnel.");
        }
        await sendLock.WaitAsync(cancellationToken);
        try
        {
            for (var offset = 0; offset < data.Length;)
            {
                var length = Math.Min(ChunkSize, data.Length - offset);
                var chunk = data.Slice(offset, length);
                int status;
                while ((status = Send(chunk.Span)) == Retry)
                {
                    await writable.WaitAsync(cancellationToken);
                }
                NativeServer.ThrowIfFailed(status);
                offset += length;
            }
        }
        finally
        {
            sendLock.Release();
        }
    }

    public async ValueTask WriteDatagramAsync(
        ReadOnlyMemory<byte> data,
        CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        if (!datagram)
        {
            throw new InvalidOperationException("The tunnel is not UDP.");
        }
        ArgumentOutOfRangeException.ThrowIfGreaterThan(data.Length, DatagramMaxSize);
        var frame = GC.AllocateUninitializedArray<byte>(data.Length + 1);
        frame[0] = 0;
        data.CopyTo(frame.AsMemory(1));
        await sendLock.WaitAsync(cancellationToken);
        try
        {
            int status;
            while ((status = Send(frame)) == Retry)
            {
                await writable.WaitAsync(cancellationToken);
            }
            NativeServer.ThrowIfFailed(status);
        }
        finally
        {
            sendLock.Release();
        }
    }

    internal bool Receive(nint data, uint dataLength)
    {
        if (disposed != 0)
        {
            return false;
        }
        if (datagram && (dataLength is 0 or > DatagramFrameMaxSize || Marshal.ReadByte(data) != 0))
        {
            return false;
        }
        var offset = datagram ? 1 : 0;
        var buffer = GC.AllocateUninitializedArray<byte>((int)dataLength - offset);
        Marshal.Copy(data + offset, buffer, 0, buffer.Length);
        try
        {
            output.Writer.WriteAsync(buffer).AsTask().GetAwaiter().GetResult();
            return true;
        }
        catch (ChannelClosedException)
        {
            return false;
        }
    }

    internal void SignalWritable() => writable.Release();

    internal void Complete(ZpStatus status)
    {
        output.Writer.TryComplete();
        completion.TrySetResult(new(status));
        writable.Release();
    }

    private unsafe int Send(ReadOnlySpan<byte> data)
    {
        fixed (byte* pointer = data)
        {
            return NativeMethods.TunnelSend(tunnel, pointer, (uint)data.Length);
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0)
        {
            return;
        }
        output.Writer.TryComplete();
        NativeMethods.CloseTunnel(tunnel);
        await completion.Task;
        creation.Handle.Free();
        sendLock.Dispose();
        writable.Dispose();
    }
}

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void TunnelOpenCallback(ZpStatus status, nint tunnel, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal delegate bool TunnelDataCallback(nint data, uint dataLength, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void TunnelWritableCallback(uint creditBytes, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void TunnelCloseCallback(ZpStatus status, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_OpenTunnel",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int OpenTunnel(
        string host,
        uint hostLength,
        ushort port,
        ushort protocol,
        TunnelOpenCallback openCallback,
        TunnelDataCallback dataCallback,
        TunnelWritableCallback writableCallback,
        TunnelCloseCallback closeCallback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_TunnelSend")]
    internal static unsafe partial int TunnelSend(nint tunnel, byte* data, uint dataLength);

    [LibraryImport(Library, EntryPoint = "ZpNative_CloseTunnel")]
    internal static partial int CloseTunnel(nint tunnel);
}
