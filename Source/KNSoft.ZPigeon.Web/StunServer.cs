using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;

namespace KNSoft.ZPigeon.Web;

internal sealed class StunServer(ushort port) : IDisposable
{
    private const uint MagicCookie = 0x2112A442;
    private const ushort BindingRequest = 0x0001;
    private const ushort BindingSuccess = 0x0101;
    private const ushort XorMappedAddress = 0x0020;
    private const int HeaderLength = 20;
    private const int MaximumRequestLength = 2048;
    private const int MaximumRequestsPerSecond = 64;
    private readonly Lock sync = new();
    private readonly Dictionary<IPAddress, AllowEntry> allowed = [];
    private readonly CancellationTokenSource cancellation = new();
    private Socket? socket;
    private Task? worker;

    internal ushort Port => port;

    internal void Start()
    {
        socket = new Socket(AddressFamily.InterNetworkV6, SocketType.Dgram, ProtocolType.Udp)
        {
            DualMode = true
        };
        socket.Bind(new IPEndPoint(IPAddress.IPv6Any, port));
        worker = RunAsync();
    }

    internal IDisposable Authorize(params IPAddress[] addresses)
    {
        var unique = addresses.Select(Normalize).Distinct().ToArray();
        lock (sync)
        {
            foreach (var address in unique)
            {
                if (!allowed.TryGetValue(address, out var entry))
                {
                    entry = new AllowEntry();
                    allowed.Add(address, entry);
                }
                entry.References++;
            }
        }
        return new Lease(this, unique);
    }

    internal string Url(HttpContext context)
    {
        var host = context.Request.Host.Host;
        if (IPAddress.TryParse(host, out var address) && address.AddressFamily == AddressFamily.InterNetworkV6)
        {
            host = $"[{host}]";
        }
        return $"stun:{host}:{port}";
    }

    private async Task RunAsync()
    {
        var buffer = GC.AllocateUninitializedArray<byte>(MaximumRequestLength);
        EndPoint remote = new IPEndPoint(IPAddress.IPv6Any, 0);
        while (!cancellation.IsCancellationRequested)
        {
            SocketReceiveFromResult received;
            try
            {
                received = await socket!.ReceiveFromAsync(buffer, SocketFlags.None, remote, cancellation.Token);
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (SocketException exception) when (
                exception.SocketErrorCode is SocketError.MessageSize or SocketError.ConnectionReset)
            {
                continue;
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            var source = (IPEndPoint)received.RemoteEndPoint;
            if (!Allow(source.Address) ||
                !TryCreateResponse(buffer.AsSpan(0, received.ReceivedBytes), source, out var response))
            {
                continue;
            }
            try
            {
                await socket.SendToAsync(response, SocketFlags.None, source, cancellation.Token);
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (SocketException)
            {
            }
        }
    }

    private bool Allow(IPAddress address)
    {
        address = Normalize(address);
        var now = Environment.TickCount64;
        lock (sync)
        {
            if (!allowed.TryGetValue(address, out var entry)) return false;
            if (now - entry.RequestWindow >= 1000)
            {
                entry.RequestWindow = now;
                entry.RequestCount = 0;
            }
            return entry.RequestCount++ < MaximumRequestsPerSecond;
        }
    }

    private static bool TryCreateResponse(
        ReadOnlySpan<byte> request,
        IPEndPoint source,
        out byte[] response)
    {
        response = null!;
        if (request.Length < HeaderLength || BinaryPrimitives.ReadUInt16BigEndian(request) != BindingRequest ||
            BinaryPrimitives.ReadUInt16BigEndian(request[2..]) != request.Length - HeaderLength ||
            (request.Length - HeaderLength) % sizeof(uint) != 0 ||
            BinaryPrimitives.ReadUInt32BigEndian(request[4..]) != MagicCookie)
        {
            return false;
        }
        var address = Normalize(source.Address).GetAddressBytes();
        var valueLength = address.Length == 4 ? 8 : 20;
        response = GC.AllocateUninitializedArray<byte>(HeaderLength + sizeof(uint) + valueLength);
        BinaryPrimitives.WriteUInt16BigEndian(response, BindingSuccess);
        BinaryPrimitives.WriteUInt16BigEndian(response.AsSpan(2), (ushort)(sizeof(uint) + valueLength));
        request.Slice(4, 16).CopyTo(response.AsSpan(4));
        BinaryPrimitives.WriteUInt16BigEndian(response.AsSpan(HeaderLength), XorMappedAddress);
        BinaryPrimitives.WriteUInt16BigEndian(response.AsSpan(HeaderLength + 2), (ushort)valueLength);
        response[HeaderLength + 4] = 0;
        response[HeaderLength + 5] = address.Length == 4 ? (byte)1 : (byte)2;
        BinaryPrimitives.WriteUInt16BigEndian(response.AsSpan(HeaderLength + 6),
                                              (ushort)(source.Port ^ (int)(MagicCookie >> 16)));
        var encodedAddress = response.AsSpan(HeaderLength + 8, address.Length);
        for (var index = 0; index < address.Length; index++)
        {
            encodedAddress[index] = (byte)(address[index] ^ request[4 + index]);
        }
        return true;
    }

    private void Release(IPAddress[] addresses)
    {
        lock (sync)
        {
            foreach (var address in addresses)
            {
                if (!allowed.TryGetValue(address, out var entry)) continue;
                if (--entry.References == 0) allowed.Remove(address);
            }
        }
    }

    private static IPAddress Normalize(IPAddress address) =>
        address.IsIPv4MappedToIPv6 ? address.MapToIPv4() : address;

    public void Dispose()
    {
        cancellation.Cancel();
        socket?.Dispose();
        try
        {
            worker?.GetAwaiter().GetResult();
        }
        catch (SocketException)
        {
        }
        catch (ObjectDisposedException)
        {
        }
        cancellation.Dispose();
    }

    private sealed class AllowEntry
    {
        internal int References;
        internal int RequestCount;
        internal long RequestWindow;
    }

    private sealed class Lease(StunServer owner, IPAddress[] addresses) : IDisposable
    {
        private StunServer? server = owner;

        public void Dispose() => Interlocked.Exchange(ref server, null)?.Release(addresses);
    }
}
