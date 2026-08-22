using KNSoft.ZPigeon.Server.Managed;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Threading.Channels;

namespace KNSoft.ZPigeon.Web;

internal sealed class UdpForwardManager(NativeServer server) : IDisposable
{
    private const int AssociationLimit = 256;
    private const int DatagramQueueLimit = 256;
    private const int DatagramMaxSize = 65507;
    private static readonly TimeSpan DefaultIdleTimeout = TimeSpan.FromSeconds(3600);
    private readonly ConcurrentDictionary<Guid, Lease> leases = new();
    private readonly CancellationTokenSource stopping = new();

    internal PortForwardInfo Create(
        IPAddress ownerAddress,
        IPAddress sourceAddress,
        string kind,
        string targetHost,
        ushort targetPort,
        TimeSpan? idleTimeout = null,
        int listenPort = 0)
    {
        var socket = new Socket(AddressFamily.InterNetworkV6, SocketType.Dgram, ProtocolType.Udp)
        {
            DualMode = true
        };
        socket.Bind(new IPEndPoint(IPAddress.IPv6Any, listenPort));
        var lease = new Lease(socket,
                              Normalize(ownerAddress),
                              Normalize(sourceAddress),
                              kind,
                              targetHost,
                              targetPort,
                              idleTimeout ?? DefaultIdleTimeout,
                              RunAssociationAsync);
        leases[lease.Id] = lease;
        _ = RunAsync(lease);
        return lease.GetInfo();
    }

    internal PortForwardInfo? Get(Guid id, IPAddress ownerAddress) =>
        leases.TryGetValue(id, out var lease) &&
        lease.OwnerAddress.Equals(Normalize(ownerAddress)) ? lease.GetInfo() : null;

    internal PortForwardInfo[] GetAll(IPAddress ownerAddress)
    {
        var address = Normalize(ownerAddress);
        return leases.Values.Where(lease => lease.OwnerAddress.Equals(address))
            .Select(lease => lease.GetInfo())
            .OrderBy(info => info.Port)
            .ToArray();
    }

    internal bool Close(Guid id, IPAddress ownerAddress)
    {
        if (!leases.TryGetValue(id, out var lease) ||
            !lease.OwnerAddress.Equals(Normalize(ownerAddress)) ||
            !leases.TryRemove(id, out lease))
        {
            return false;
        }
        lease.Close();
        return true;
    }

    private async Task RunAsync(Lease lease)
    {
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(
            stopping.Token,
            lease.Cancellation.Token);
        var buffer = GC.AllocateUninitializedArray<byte>(DatagramMaxSize);
        EndPoint remote = new IPEndPoint(IPAddress.IPv6Any, 0);
        try
        {
            for (;;)
            {
                var result = await lease.Socket.ReceiveFromAsync(
                    buffer,
                    SocketFlags.None,
                    remote,
                    cancellation.Token);
                var endpoint = (IPEndPoint)result.RemoteEndPoint;
                if (!Normalize(endpoint.Address).Equals(lease.SourceAddress))
                {
                    continue;
                }
                var key = new IPEndPoint(Normalize(endpoint.Address), endpoint.Port);
                var association = lease.GetOrCreateAssociation(key, endpoint);
                if (association is null)
                {
                    continue;
                }
                var datagram = GC.AllocateUninitializedArray<byte>(result.ReceivedBytes);
                buffer.AsSpan(0, result.ReceivedBytes).CopyTo(datagram);
                if (association.TryWrite(datagram))
                {
                    lease.Touch();
                }
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (ObjectDisposedException)
        {
        }
        catch (SocketException exception)
        {
            lease.SetFailure(new(ZpStatusType.Winsock, (uint)exception.SocketErrorCode));
        }
        finally
        {
            lease.Socket.Dispose();
        }
    }

    private async Task RunAssociationAsync(Lease lease, Lease.Association association)
    {
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(
            stopping.Token,
            lease.Cancellation.Token,
            association.Cancellation.Token);
        try
        {
            await using var tunnel = await server.OpenUdpTunnelAsync(lease.TargetHost, lease.TargetPort);
            var upload = UploadAsync(association, tunnel, cancellation.Token);
            var download = DownloadAsync(lease, association, tunnel, cancellation.Token);
            await Task.WhenAny(upload, download);
            cancellation.Cancel();
            await IgnoreCancellationAsync(upload);
            await IgnoreCancellationAsync(download);
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
        finally
        {
            association.Close();
            lease.RemoveAssociation(association);
        }
    }

    private static async Task UploadAsync(
        Lease.Association association,
        RemoteTunnel tunnel,
        CancellationToken cancellationToken)
    {
        await foreach (var datagram in association.Input.ReadAllAsync(cancellationToken))
        {
            await tunnel.WriteDatagramAsync(datagram, cancellationToken);
            association.Touch();
        }
    }

    private static async Task DownloadAsync(
        Lease lease,
        Lease.Association association,
        RemoteTunnel tunnel,
        CancellationToken cancellationToken)
    {
        await foreach (var datagram in tunnel.Output.ReadAllAsync(cancellationToken))
        {
            await lease.Socket.SendToAsync(datagram,
                                           SocketFlags.None,
                                           association.RemoteEndpoint,
                                           cancellationToken);
            association.Touch();
            lease.Touch();
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
            lease.Close();
        }
        stopping.Dispose();
    }

    private sealed class Lease
    {
        private readonly Lock sync = new();
        private readonly Timer idleTimer;
        private readonly ConcurrentDictionary<IPEndPoint, Association> associations = new();
        private readonly Func<Lease, Association, Task> runAssociation;
        private string state = "Waiting";
        private ZpStatus? status;

        internal Lease(
            Socket socket,
            IPAddress ownerAddress,
            IPAddress sourceAddress,
            string kind,
            string targetHost,
            ushort targetPort,
            TimeSpan idleTimeout,
            Func<Lease, Association, Task> runAssociation)
        {
            Socket = socket;
            OwnerAddress = ownerAddress;
            SourceAddress = sourceAddress;
            Kind = kind;
            TargetHost = targetHost;
            TargetPort = targetPort;
            IdleTimeout = idleTimeout;
            this.runAssociation = runAssociation;
            IdleExpires = DateTimeOffset.UtcNow.Add(idleTimeout);
            Port = ((IPEndPoint)socket.LocalEndPoint!).Port;
            idleTimer = new(_ => Expire(), null, idleTimeout, Timeout.InfiniteTimeSpan);
        }

        internal Guid Id { get; } = Guid.NewGuid();
        internal Socket Socket { get; }
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
                           "UDP",
                           Kind,
                           SourceAddress.ToString(),
                           TargetHost,
                           TargetPort,
                           (uint)IdleTimeout.TotalSeconds,
                           IdleExpires,
                           associations.Count,
                           state,
                           status);
            }
        }

        internal Association? GetOrCreateAssociation(IPEndPoint key, IPEndPoint remoteEndpoint)
        {
            if (associations.TryGetValue(key, out var association))
            {
                return association;
            }
            lock (sync)
            {
                if (state is "Closed" or "Expired" || associations.Count >= AssociationLimit)
                {
                    return null;
                }
                association = new(this, key, remoteEndpoint);
                if (!associations.TryAdd(key, association))
                {
                    association.Close();
                    return associations.GetValueOrDefault(key);
                }
                state = "Connected";
                status = null;
                association.Start();
                return association;
            }
        }

        internal void RemoveAssociation(Association association)
        {
            associations.TryRemove(new KeyValuePair<IPEndPoint, Association>(association.Key, association));
            lock (sync)
            {
                if (associations.IsEmpty && state == "Connected")
                {
                    state = "Waiting";
                }
            }
        }

        private void StartAssociation(Association association) =>
            _ = runAssociation(this, association);

        internal void Touch()
        {
            lock (sync)
            {
                if (state is "Closed" or "Expired")
                {
                    return;
                }
                state = associations.IsEmpty ? "Waiting" : "Connected";
                status = null;
                IdleExpires = DateTimeOffset.UtcNow.Add(IdleTimeout);
                idleTimer.Change(IdleTimeout, Timeout.InfiniteTimeSpan);
            }
        }

        internal void SetFailure(ZpStatus value)
        {
            lock (sync)
            {
                if (state is "Closed" or "Expired")
                {
                    return;
                }
                state = "Failed";
                status = value;
            }
        }

        private void Expire()
        {
            lock (sync)
            {
                if (state is "Closed" or "Expired")
                {
                    return;
                }
                state = "Expired";
                IdleExpires = null;
            }
            idleTimer.Dispose();
            Cancellation.Cancel();
            Socket.Dispose();
            foreach (var association in associations.Values)
            {
                association.Close();
            }
        }

        internal void Close()
        {
            lock (sync)
            {
                state = "Closed";
                IdleExpires = null;
            }
            idleTimer.Dispose();
            Cancellation.Cancel();
            Socket.Dispose();
            foreach (var association in associations.Values)
            {
                association.Close();
            }
        }

        internal sealed class Association
        {
            private readonly Lock sync = new();
            private readonly Lease lease;
            private readonly Channel<byte[]> input = Channel.CreateBounded<byte[]>(
                new BoundedChannelOptions(DatagramQueueLimit)
                {
                    SingleReader = true,
                    SingleWriter = true,
                    FullMode = BoundedChannelFullMode.Wait
                });
            private readonly Timer idleTimer;
            private int closed;

            internal Association(Lease lease, IPEndPoint key, IPEndPoint remoteEndpoint)
            {
                this.lease = lease;
                Key = key;
                RemoteEndpoint = remoteEndpoint;
                idleTimer = new(_ => Close(), null, lease.IdleTimeout, Timeout.InfiniteTimeSpan);
            }

            internal IPEndPoint Key { get; }
            internal IPEndPoint RemoteEndpoint { get; }
            internal ChannelReader<byte[]> Input => input.Reader;
            internal CancellationTokenSource Cancellation { get; } = new();

            internal void Start() => lease.StartAssociation(this);

            internal bool TryWrite(byte[] datagram)
            {
                var written = closed == 0 && input.Writer.TryWrite(datagram);
                if (written)
                {
                    Touch();
                }
                return written;
            }

            internal void Touch()
            {
                lock (sync)
                {
                    if (closed == 0)
                    {
                        idleTimer.Change(lease.IdleTimeout, Timeout.InfiniteTimeSpan);
                    }
                }
            }

            internal void Close()
            {
                if (Interlocked.Exchange(ref closed, 1) != 0)
                {
                    return;
                }
                lock (sync)
                {
                    idleTimer.Dispose();
                }
                input.Writer.TryComplete();
                Cancellation.Cancel();
            }
        }
    }
}
