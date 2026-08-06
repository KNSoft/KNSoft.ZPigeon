using System.Collections.Concurrent;
using System.Net;

namespace KNSoft.ZPigeon.Web;

internal sealed class RdpForwardManager(
    TcpForwardManager tcpForwards,
    UdpForwardManager udpForwards) : IDisposable
{
    private readonly ConcurrentDictionary<Guid, Pair> pairs = new();

    internal PortForwardInfo Create(IPAddress ownerAddress, ushort targetPort)
    {
        var tcp = tcpForwards.Create(ownerAddress, ownerAddress, "RDP", "127.0.0.1", targetPort);
        try
        {
            var udp = udpForwards.Create(ownerAddress,
                                         ownerAddress,
                                         "RDP",
                                         "127.0.0.1",
                                         targetPort,
                                         listenPort: tcp.Port);
            pairs[tcp.Id] = new(tcp.Id, udp.Id, Normalize(ownerAddress));
            return Combine(tcp, udp);
        }
        catch
        {
            tcpForwards.Close(tcp.Id);
            throw;
        }
    }

    internal PortForwardInfo? Get(Guid id, IPAddress ownerAddress)
    {
        if (!pairs.TryGetValue(id, out var pair) || !pair.OwnerAddress.Equals(Normalize(ownerAddress)))
        {
            return null;
        }
        var tcp = tcpForwards.Get(pair.TcpId);
        var udp = udpForwards.Get(pair.UdpId, ownerAddress);
        return tcp is null || udp is null ? null : Combine(tcp, udp);
    }

    internal PortForwardInfo[] GetAll(IPAddress ownerAddress) =>
        pairs.Keys.Select(id => Get(id, ownerAddress))
            .Where(info => info is not null)
            .Cast<PortForwardInfo>()
            .OrderBy(info => info.Port)
            .ToArray();

    internal bool Close(Guid id, IPAddress ownerAddress)
    {
        if (!pairs.TryGetValue(id, out var pair) || !pair.OwnerAddress.Equals(Normalize(ownerAddress)) ||
            !pairs.TryRemove(id, out pair))
        {
            return false;
        }
        tcpForwards.Close(pair.TcpId);
        udpForwards.Close(pair.UdpId, ownerAddress);
        return true;
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
        foreach (var pair in pairs.Values)
        {
            tcpForwards.Close(pair.TcpId);
            udpForwards.Close(pair.UdpId, pair.OwnerAddress);
        }
        pairs.Clear();
    }

    private sealed record Pair(Guid TcpId, Guid UdpId, IPAddress OwnerAddress);
}
