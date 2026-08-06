using System.Collections.Concurrent;
using KNSoft.ZPigeon.Server.Managed;

namespace KNSoft.ZPigeon.Web;

internal sealed class ClientServicesRegistry : IDisposable
{
    private readonly ConcurrentDictionary<ulong, ClientServicesEntry> clients = [];
    private readonly NativeServer server;
    private int disposed;

    internal ClientServicesRegistry(NativeServer server)
    {
        this.server = server;
        server.ClientsChanged += OnClientsChanged;
    }

    internal NativeServer Server => server;

    internal ClientServices Current
    {
        get
        {
            ObjectDisposedException.ThrowIf(disposed != 0, this);
            var clientId = server.ClientId;
            if (clientId == 0) throw new InvalidOperationException("No Client is selected.");
            return clients.GetOrAdd(clientId, _ => new(server)).Get();
        }
    }

    private void OnClientsChanged(object? _, EventArgs __)
    {
        if (Volatile.Read(ref disposed) != 0) return;
        var connectedClients = server.GetClients().Select(client => client.Id).ToHashSet();
        Prune(connectedClients);
    }

    private void Prune(HashSet<ulong> connectedClients)
    {
        foreach (var clientId in clients.Keys)
        {
            if (!connectedClients.Contains(clientId)) Remove(clientId);
        }
    }

    internal void Remove(ulong clientId)
    {
        if (clients.TryRemove(clientId, out var client)) client.Dispose();
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0) return;
        server.ClientsChanged -= OnClientsChanged;
        foreach (var client in clients.Values) client.Dispose();
        clients.Clear();
    }

    private sealed class ClientServicesEntry(NativeServer server) : IDisposable
    {
        private readonly object sync = new();
        private ClientServices? value;
        private bool disposed;

        internal ClientServices Get()
        {
            lock (sync)
            {
                ObjectDisposedException.ThrowIf(disposed, this);
                return value ??= new(server);
            }
        }

        public void Dispose()
        {
            ClientServices? current;
            lock (sync)
            {
                if (disposed) return;
                disposed = true;
                current = value;
            }
            current?.Dispose();
        }
    }
}

internal sealed class ClientServices : IDisposable
{
    internal ClientServices(NativeServer server)
    {
        Server = server;
        TerminalSessions = new(server);
        EventLogStreams = new(server);
        TcpForwards = new(server);
        UdpForwards = new(server);
        RdpForwards = new(TcpForwards, UdpForwards);
        CdpSessions = new(server, TcpForwards);
        ConnectionPerformance = new(server);
        NetworkQuality = new(ConnectionPerformance);
        SoftwareDeployments = new(server);
    }

    internal NativeServer Server { get; }
    internal TerminalWebSessionManager TerminalSessions { get; }
    internal EventLogStreamManager EventLogStreams { get; }
    internal TcpForwardManager TcpForwards { get; }
    internal UdpForwardManager UdpForwards { get; }
    internal RdpForwardManager RdpForwards { get; }
    internal CdpSessionManager CdpSessions { get; }
    internal ConnectionPerformanceManager ConnectionPerformance { get; }
    internal NetworkQualityMonitor NetworkQuality { get; }
    internal SoftwareDeploymentManager SoftwareDeployments { get; }
    internal ConcurrentDictionary<string, string> ExecutionCleanupPaths { get; } = [];
    internal object UpdateCheckLock { get; } = new();
    internal Task? UpdateCheck;

    public void Dispose()
    {
        TerminalSessions.DisposeAsync().AsTask().GetAwaiter().GetResult();
        EventLogStreams.Dispose();
        CdpSessions.Dispose();
        RdpForwards.Dispose();
        TcpForwards.Dispose();
        UdpForwards.Dispose();
        SoftwareDeployments.Dispose();
    }
}
