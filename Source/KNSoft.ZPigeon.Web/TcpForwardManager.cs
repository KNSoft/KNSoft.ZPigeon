using KNSoft.ZPigeon.Server.Managed;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;

namespace KNSoft.ZPigeon.Web;

internal sealed record PortForwardInfo(
    Guid Id,
    int Port,
    string Protocol,
    string Kind,
    string SourceAddress,
    string TargetHost,
    ushort TargetPort,
    uint IdleTimeoutSeconds,
    DateTimeOffset? IdleExpires,
    int ActiveCount,
    string State,
    ZpStatus? Status);

internal sealed class TcpForwardManager(NativeServer server) : IDisposable
{
    private static readonly TimeSpan DefaultIdleTimeout = TimeSpan.FromSeconds(3600);
    private readonly ConcurrentDictionary<Guid, Lease> leases = new();
    private readonly CancellationTokenSource stopping = new();

    public PortForwardInfo Create(
        IPAddress ownerAddress,
        IPAddress sourceAddress,
        string kind,
        string targetHost,
        ushort targetPort,
        TimeSpan? idleTimeout = null)
    {
        var listener = new TcpListener(IPAddress.IPv6Any, 0);
        listener.Server.DualMode = true;
        listener.Start(16);
        var lease = new Lease(listener,
                              Normalize(ownerAddress),
                              Normalize(sourceAddress),
                              kind,
                              targetHost,
                              targetPort,
                              idleTimeout ?? DefaultIdleTimeout);
        leases[lease.Id] = lease;
        _ = RunAsync(lease);
        return lease.GetInfo();
    }

    public PortForwardInfo? Get(Guid id) =>
        leases.TryGetValue(id, out var lease) ? lease.GetInfo() : null;

    public PortForwardInfo? Get(Guid id, IPAddress sourceAddress) =>
        leases.TryGetValue(id, out var lease) &&
        lease.OwnerAddress.Equals(Normalize(sourceAddress)) ? lease.GetInfo() : null;

    public PortForwardInfo[] GetAll(IPAddress sourceAddress)
    {
        var address = Normalize(sourceAddress);
        return leases.Values.Where(lease => lease.OwnerAddress.Equals(address))
            .Select(lease => lease.GetInfo())
            .OrderBy(info => info.Port)
            .ToArray();
    }

    public bool Close(Guid id)
    {
        if (!leases.TryRemove(id, out var lease))
        {
            return false;
        }
        lease.Dispose();
        return true;
    }

    public bool Close(Guid id, IPAddress sourceAddress)
    {
        if (!leases.TryGetValue(id, out var lease) ||
            !lease.OwnerAddress.Equals(Normalize(sourceAddress)))
        {
            return false;
        }
        if (!leases.TryRemove(id, out lease))
        {
            return false;
        }
        lease.Dispose();
        return true;
    }

    private async Task RunAsync(Lease lease)
    {
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(
            stopping.Token,
            lease.Cancellation.Token);
        try
        {
            while (true)
            {
                var client = await lease.Listener.AcceptTcpClientAsync(cancellation.Token);
                var address = ((IPEndPoint)client.Client.RemoteEndPoint!).Address;
                if (!Normalize(address).Equals(lease.SourceAddress))
                {
                    client.Dispose();
                    continue;
                }
                if (!lease.BeginConnection())
                {
                    client.Dispose();
                    continue;
                }
                _ = ObserveForwardAsync(client, lease, cancellation.Token);
            }
        }
        catch (NativeException exception)
        {
            lease.SetFailure(exception.Status);
        }
        catch (SocketException exception)
        {
            lease.SetFailure(new(ZpStatusType.Winsock, (uint)exception.SocketErrorCode));
        }
        catch (IOException exception) when (exception.InnerException is SocketException socketException)
        {
            lease.SetFailure(new(ZpStatusType.Winsock, (uint)socketException.SocketErrorCode));
        }
        catch (OperationCanceledException)
        {
        }
        finally
        {
            lease.Listener.Stop();
        }
    }

    private async Task ObserveForwardAsync(
        TcpClient client,
        Lease lease,
        CancellationToken cancellationToken)
    {
        try
        {
            await ForwardAsync(client, lease, cancellationToken);
        }
        catch (OperationCanceledException)
        {
        }
        catch (NativeException exception)
        {
            lease.SetFailure(exception.Status);
        }
        catch (SocketException exception)
        {
            lease.SetFailure(new(ZpStatusType.Winsock, (uint)exception.SocketErrorCode));
        }
        catch (IOException exception) when (exception.InnerException is SocketException socketException)
        {
            lease.SetFailure(new(ZpStatusType.Winsock, (uint)socketException.SocketErrorCode));
        }
        finally
        {
            lease.EndConnection();
        }
    }

    private async Task ForwardAsync(
        TcpClient client,
        Lease lease,
        CancellationToken cancellationToken)
    {
        using (client)
        await using (var tunnel = await server.OpenTunnelAsync(lease.TargetHost, lease.TargetPort))
        using (var transfer = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken))
        {
            var stream = client.GetStream();
            var upload = UploadAsync(stream, tunnel, transfer.Token);
            var download = DownloadAsync(stream, tunnel, transfer.Token);
            await Task.WhenAny(upload, download);
            transfer.Cancel();
            await IgnoreCancellationAsync(upload);
            await IgnoreCancellationAsync(download);
        }
    }

    private static async Task UploadAsync(
        NetworkStream stream,
        RemoteTunnel tunnel,
        CancellationToken cancellationToken)
    {
        var buffer = GC.AllocateUninitializedArray<byte>(0x10000);
        int length;
        while ((length = await stream.ReadAsync(buffer, cancellationToken)) != 0)
        {
            await tunnel.WriteAsync(buffer.AsMemory(0, length), cancellationToken);
        }
    }

    private static async Task DownloadAsync(
        NetworkStream stream,
        RemoteTunnel tunnel,
        CancellationToken cancellationToken)
    {
        await foreach (var data in tunnel.Output.ReadAllAsync(cancellationToken))
        {
            using (data) await stream.WriteAsync(data.Memory, cancellationToken);
        }
        var completion = await tunnel.Completion;
        if (!completion.Status.IsSuccess)
        {
            throw new NativeException(completion.Status);
        }
    }

    private static async Task IgnoreCancellationAsync(Task task)
    {
        try
        {
            await task;
        }
        catch (OperationCanceledException)
        {
        }
    }

    private static IPAddress Normalize(IPAddress address) =>
        address.IsIPv4MappedToIPv6 ? address.MapToIPv4() : address;

    public void Dispose()
    {
        stopping.Cancel();
        foreach (var lease in leases.Values)
        {
            lease.Dispose();
        }
        stopping.Dispose();
    }

    private sealed class Lease : IDisposable
    {
        private readonly Lock sync = new();
        private readonly Timer idleTimer;
        private string state = "Waiting";
        private ZpStatus? status;
        private int activeConnections;

        internal Lease(
            TcpListener listener,
            IPAddress ownerAddress,
            IPAddress sourceAddress,
            string kind,
            string targetHost,
            ushort targetPort,
            TimeSpan idleTimeout)
        {
            Listener = listener;
            OwnerAddress = ownerAddress;
            SourceAddress = sourceAddress;
            Kind = kind;
            TargetHost = targetHost;
            TargetPort = targetPort;
            IdleTimeout = idleTimeout;
            IdleExpires = DateTimeOffset.UtcNow.Add(idleTimeout);
            Port = ((IPEndPoint)listener.LocalEndpoint).Port;
            idleTimer = new(_ => Expire(), null, idleTimeout, Timeout.InfiniteTimeSpan);
        }

        internal Guid Id { get; } = Guid.NewGuid();
        internal TcpListener Listener { get; }
        internal IPAddress OwnerAddress { get; }
        internal IPAddress SourceAddress { get; }
        internal string Kind { get; }
        internal string TargetHost { get; }
        internal ushort TargetPort { get; }
        internal int Port { get; }
        internal TimeSpan IdleTimeout { get; }
        internal DateTimeOffset? IdleExpires { get; private set; }
        internal CancellationTokenSource Cancellation { get; } = new();

        internal PortForwardInfo GetInfo()
        {
            lock (sync)
            {
                return new(Id,
                           Port,
                           "TCP",
                           Kind,
                           SourceAddress.ToString(),
                           TargetHost,
                           TargetPort,
                           (uint)IdleTimeout.TotalSeconds,
                           IdleExpires,
                           activeConnections,
                           state,
                           status);
            }
        }

        internal void SetFailure(ZpStatus value)
        {
            lock (sync)
            {
                state = "Failed";
                status = value;
            }
        }

        internal bool BeginConnection()
        {
            lock (sync)
            {
                if (state is "Closed" or "Expired") return false;
                activeConnections++;
                IdleExpires = null;
                idleTimer.Change(Timeout.InfiniteTimeSpan, Timeout.InfiniteTimeSpan);
                state = "Connected";
                status = null;
                return true;
            }
        }

        internal void EndConnection()
        {
            lock (sync)
            {
                if (activeConnections != 0)
                {
                    activeConnections--;
                }
                if (activeConnections == 0 && state is not ("Closed" or "Expired"))
                {
                    state = "Waiting";
                    IdleExpires = DateTimeOffset.UtcNow.Add(IdleTimeout);
                    idleTimer.Change(IdleTimeout, Timeout.InfiniteTimeSpan);
                }
            }
        }

        private void Expire()
        {
            lock (sync)
            {
                if (activeConnections != 0 || state is "Closed" or "Expired") return;
                state = "Expired";
                IdleExpires = null;
            }
            idleTimer.Dispose();
            Cancellation.Cancel();
            Listener.Stop();
        }

        public void Dispose()
        {
            lock (sync)
            {
                state = "Closed";
                IdleExpires = null;
            }
            idleTimer.Dispose();
            Cancellation.Cancel();
            Listener.Stop();
        }
    }
}
