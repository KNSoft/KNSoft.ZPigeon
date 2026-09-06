using System.Collections.Concurrent;
using System.Net;

namespace KNSoft.ZPigeon.Web;

internal sealed class RdpForwardManager(
    TcpForwardManager tcpForwards,
    UdpForwardManager udpForwards) : IDisposable
{
    private readonly ConcurrentDictionary<IPAddress, Pair> pairs = new();
    private readonly object sync = new();

    internal PortForwardInfo GetOrCreate(IPAddress ownerAddress, ushort targetPort)
    {
        ownerAddress = Normalize(ownerAddress);
        lock (sync)
        {
            if (pairs.TryGetValue(ownerAddress, out var pair))
            {
                var existing = Get(pair, ownerAddress);
                if (existing is { State: "Waiting" or "Connected" }) return existing;
                pairs.TryRemove(ownerAddress, out _);
                Close(pair, ownerAddress);
            }
            var tcp = tcpForwards.Create(ownerAddress, ownerAddress, "RDP", "127.0.0.1", targetPort);
            try
            {
                var udp = udpForwards.Create(ownerAddress,
                                             ownerAddress,
                                             "RDP",
                                             "127.0.0.1",
                                             targetPort,
                                             listenPort: tcp.Port);
                pairs[ownerAddress] = new(tcp.Id, udp.Id);
                return Combine(tcp, udp);
            }
            catch
            {
                tcpForwards.Close(tcp.Id);
                throw;
            }
        }
    }

    internal PortForwardInfo? Get(Guid id, IPAddress ownerAddress)
    {
        ownerAddress = Normalize(ownerAddress);
        return pairs.TryGetValue(ownerAddress, out var pair) && pair.TcpId == id ?
                   Get(pair, ownerAddress) :
                   null;
    }

    internal PortForwardInfo[] GetAll(IPAddress ownerAddress)
    {
        ownerAddress = Normalize(ownerAddress);
        var info = pairs.TryGetValue(ownerAddress, out var pair) ? Get(pair, ownerAddress) : null;
        return info is null ? [] : [info];
    }

    internal bool Close(Guid id, IPAddress ownerAddress)
    {
        ownerAddress = Normalize(ownerAddress);
        lock (sync)
        {
            if (!pairs.TryGetValue(ownerAddress, out var pair) || pair.TcpId != id ||
                !pairs.TryRemove(ownerAddress, out pair))
            {
                return false;
            }
            Close(pair, ownerAddress);
            return true;
        }
    }

    private PortForwardInfo? Get(Pair pair, IPAddress ownerAddress)
    {
        var tcp = tcpForwards.Get(pair.TcpId, ownerAddress);
        var udp = udpForwards.Get(pair.UdpId, ownerAddress);
        return tcp is null || udp is null ? null : Combine(tcp, udp);
    }

    private void Close(Pair pair, IPAddress ownerAddress)
    {
        tcpForwards.Close(pair.TcpId);
        udpForwards.Close(pair.UdpId, ownerAddress);
    }

    private static PortForwardInfo Combine(PortForwardInfo tcp, PortForwardInfo udp) =>
        tcp with
        {
            Protocol = "TCP/UDP",
            IdleExpires = tcp.IdleExpires is null || udp.IdleExpires is null ? null :
                tcp.IdleExpires < udp.IdleExpires ? tcp.IdleExpires : udp.IdleExpires,
            ActiveCount = tcp.ActiveCount + udp.ActiveCount,
            State = tcp.State == "Failed" ? tcp.State : udp.State == "Failed" ? udp.State :
                tcp.State == "Connected" || udp.State == "Connected" ? "Connected" :
                tcp.State == "Expired" || udp.State == "Expired" ? "Expired" : tcp.State,
            Status = tcp.Status ?? udp.Status
        };

    private static IPAddress Normalize(IPAddress address) =>
        address.IsIPv4MappedToIPv6 ? address.MapToIPv4() : address;

    public void Dispose()
    {
        lock (sync)
        {
            foreach (var (ownerAddress, pair) in pairs) Close(pair, ownerAddress);
            pairs.Clear();
        }
    }

    private sealed record Pair(Guid TcpId, Guid UdpId);
}
