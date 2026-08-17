using KNSoft.ZPigeon.Server.Managed;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;

namespace KNSoft.ZPigeon.Web;

internal sealed record TcpForwardInfo(
    Guid Id,
    int Port,
    DateTimeOffset Expires,
    string State,
    ZpStatus? Status);

internal sealed class TcpForwardManager(NativeServer server) : IDisposable
{
    private static readonly TimeSpan DefaultLeaseLifetime = TimeSpan.FromSeconds(60);
    private readonly ConcurrentDictionary<Guid, Lease> leases = new();
    private readonly CancellationTokenSource stopping = new();

    public TcpForwardInfo Create(
        IPAddress sourceAddress,
        ushort targetPort,
        bool singleUse = true,
        TimeSpan? lifetime = null)
    {
        var listener = new TcpListener(IPAddress.Any, 0);
        listener.Start(singleUse ? 1 : 16);
        var leaseLifetime = lifetime ?? DefaultLeaseLifetime;
        var lease = new Lease(listener,
                              Normalize(sourceAddress),
                              targetPort,
                              DateTimeOffset.UtcNow.Add(leaseLifetime),
                              singleUse,
                              leaseLifetime);
        leases[lease.Id] = lease;
        _ = RunAsync(lease);
        return lease.GetInfo();
    }

    public TcpForwardInfo? Get(Guid id) =>
        leases.TryGetValue(id, out var lease) ? lease.GetInfo() : null;

    public bool Close(Guid id)
    {
        if (!leases.TryGetValue(id, out var lease))
        {
            return false;
        }
        lease.Close();
        return true;
    }

    private async Task RunAsync(Lease lease)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(
            stopping.Token,
            lease.Cancellation.Token);
        timeout.CancelAfter(lease.Lifetime);
        try
        {
            for (;;)
            {
                var client = await lease.Listener.AcceptTcpClientAsync(timeout.Token);
                var address = ((IPEndPoint)client.Client.RemoteEndPoint!).Address;
                if (!Normalize(address).Equals(lease.SourceAddress))
                {
                    client.Dispose();
                    continue;
                }
                if (lease.SingleUse)
                {
                    lease.Listener.Stop();
                    lease.SetState("Connecting");
                    await ForwardAsync(client, lease, timeout.Token);
                    lease.SetState("Closed");
                    return;
                }
                _ = ObserveForwardAsync(client, lease, timeout.Token);
            }
        }
        catch (OperationCanceledException) when (!stopping.IsCancellationRequested &&
                                                  !lease.Cancellation.IsCancellationRequested)
        {
            lease.SetState("Expired");
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
            lease.Close(false);
        }
        catch (SocketException exception)
        {
            lease.SetFailure(new(ZpStatusType.Winsock, (uint)exception.SocketErrorCode));
        }
        catch (IOException exception) when (exception.InnerException is SocketException socketException)
        {
            lease.SetFailure(new(ZpStatusType.Winsock, (uint)socketException.SocketErrorCode));
        }
    }

    private async Task ForwardAsync(
        TcpClient client,
        Lease lease,
        CancellationToken cancellationToken)
    {
        using (client)
        await using (var tunnel = await server.OpenTunnelAsync(lease.TargetPort))
        using (var transfer = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken))
        {
            lease.BeginConnection();
            try
            {
                var stream = client.GetStream();
                var upload = UploadAsync(stream, tunnel, transfer.Token);
                var download = DownloadAsync(stream, tunnel, transfer.Token);
                await Task.WhenAny(upload, download);
                transfer.Cancel();
                await IgnoreCancellationAsync(upload);
                await IgnoreCancellationAsync(download);
            }
            finally
            {
                lease.EndConnection();
            }
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
            await stream.WriteAsync(data, cancellationToken);
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
        private string state = "Waiting";
        private ZpStatus? status;
        private int activeConnections;

        internal Lease(
            TcpListener listener,
            IPAddress sourceAddress,
            ushort targetPort,
            DateTimeOffset expires,
            bool singleUse,
            TimeSpan lifetime)
        {
            Listener = listener;
            SourceAddress = sourceAddress;
            TargetPort = targetPort;
            Expires = expires;
            SingleUse = singleUse;
            Lifetime = lifetime;
            Port = ((IPEndPoint)listener.LocalEndpoint).Port;
        }

        internal Guid Id { get; } = Guid.NewGuid();
        internal TcpListener Listener { get; }
        internal IPAddress SourceAddress { get; }
        internal ushort TargetPort { get; }
        internal DateTimeOffset Expires { get; }
        internal int Port { get; }
        internal bool SingleUse { get; }
        internal TimeSpan Lifetime { get; }
        internal CancellationTokenSource Cancellation { get; } = new();

        internal TcpForwardInfo GetInfo()
        {
            lock (sync)
            {
                return new(Id, Port, Expires, state, status);
            }
        }

        internal void SetState(string value)
        {
            lock (sync)
            {
                state = value;
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

        internal void BeginConnection()
        {
            lock (sync)
            {
                activeConnections++;
                state = "Connected";
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
                if (!SingleUse && activeConnections == 0 && state == "Connected")
                {
                    state = "Waiting";
                }
            }
        }

        internal void Close(bool setState = true)
        {
            if (setState)
            {
                SetState("Closed");
            }
            Cancellation.Cancel();
            Listener.Stop();
        }
    }
}
