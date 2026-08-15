using System.Threading.Channels;
using KNSoft.ZPigeon.Server.Managed;

namespace KNSoft.ZPigeon.Web;

internal sealed class TerminalWebSessionManager : IAsyncDisposable
{
    private readonly NativeServer server;
    private readonly Lock sync = new();
    private readonly Dictionary<uint, TerminalWebSession> sessions = [];
    private uint nextId;

    internal TerminalWebSessionManager(NativeServer server) =>
        this.server = server;

    internal async Task<TerminalWebSessionInfo> CreateAsync(
        TerminalShell shell,
        ushort columns,
        ushort rows)
    {
        var terminal = await server.CreateShellAsync(shell, columns, rows);
        TerminalWebSession session;
        lock (sync)
        {
            var id = ++nextId;
            var number = 1U;
            while (sessions.Values.Any(value =>
                       !value.Ended && value.Shell == shell && value.Number == number))
            {
                number++;
            }
            session = new TerminalWebSession(
                id,
                terminal,
                shell,
                number,
                $"{terminal.Shell!.Name} {number}");
            sessions.Add(id, session);
        }
        session.Start();
        return session.Info;
    }

    internal TerminalWebSessionInfo[] GetSessions()
    {
        lock (sync)
        {
            return [.. sessions.Values.OrderBy(session => session.Id).Select(session => session.Info)];
        }
    }

    internal bool TryGet(uint id, out TerminalWebSession? session)
    {
        lock (sync)
        {
            return sessions.TryGetValue(id, out session);
        }
    }

    internal bool Rename(uint id, string title)
    {
        lock (sync)
        {
            if (!sessions.TryGetValue(id, out var session))
            {
                return false;
            }
            session.Rename(title);
            return true;
        }
    }

    internal async Task<bool> CloseAsync(uint id)
    {
        TerminalWebSession? session;
        lock (sync)
        {
            if (!sessions.Remove(id, out session))
            {
                return false;
            }
        }
        await session.DisposeAsync();
        return true;
    }

    public async ValueTask DisposeAsync()
    {
        TerminalWebSession[] values;
        lock (sync)
        {
            values = [.. sessions.Values];
            sessions.Clear();
        }
        await Task.WhenAll(values.Select(session => session.DisposeAsync().AsTask()));
    }
}

internal sealed class TerminalWebSession : IAsyncDisposable
{
    private const int HistoryLimit = 0x00400000;
    private readonly TerminalSession terminal;
    private readonly Lock sync = new();
    private readonly Queue<byte[]> history = [];
    private Channel<byte[]>? attachment;
    private TerminalCompletion? completion;
    private Task? pump;
    private int historyLength;
    private string title;

    internal TerminalWebSession(
        uint id,
        TerminalSession terminal,
        TerminalShell shell,
        uint number,
        string title)
    {
        Id = id;
        Shell = shell;
        Number = number;
        this.terminal = terminal;
        this.title = title;
    }

    internal uint Id { get; }
    internal TerminalShell Shell { get; }
    internal uint Number { get; }

    internal bool Ended
    {
        get
        {
            lock (sync)
            {
                return completion.HasValue;
            }
        }
    }

    internal TerminalWebSessionInfo Info
    {
        get
        {
            lock (sync)
            {
                return new(Id,
                           terminal.ProcessId,
                           terminal.Shell!,
                           title,
                           completion.HasValue);
            }
        }
    }

    internal void Start() => pump = PumpAsync();

    internal TerminalWebAttachment Attach()
    {
        var live = Channel.CreateBounded<byte[]>(new BoundedChannelOptions(64)
        {
            SingleReader = true,
            SingleWriter = true,
            FullMode = BoundedChannelFullMode.DropOldest
        });
        byte[][] snapshot;
        lock (sync)
        {
            attachment?.Writer.TryComplete();
            attachment = live;
            snapshot = [.. history];
            if (completion.HasValue)
            {
                live.Writer.TryComplete();
            }
        }
        return new(this, live, snapshot);
    }

    internal void Detach(Channel<byte[]> channel)
    {
        lock (sync)
        {
            if (ReferenceEquals(attachment, channel))
            {
                attachment = null;
                channel.Writer.TryComplete();
            }
        }
    }

    internal void Rename(string value)
    {
        lock (sync)
        {
            title = value;
        }
    }

    internal ValueTask WriteAsync(ReadOnlyMemory<byte> data, CancellationToken cancellationToken) =>
        terminal.WriteAsync(data, cancellationToken);

    internal Task ResizeAsync(ushort columns, ushort rows) =>
        terminal.ResizeAsync(columns, rows);

    internal bool TryGetCompletion(out TerminalCompletion value)
    {
        lock (sync)
        {
            value = completion.GetValueOrDefault();
            return completion.HasValue;
        }
    }

    private async Task PumpAsync()
    {
        await foreach (var data in terminal.Output.ReadAllAsync())
        {
            var buffer = data.ToArray();
            lock (sync)
            {
                history.Enqueue(buffer);
                historyLength += buffer.Length;
                while (historyLength > HistoryLimit && history.TryDequeue(out var discarded))
                {
                    historyLength -= discarded.Length;
                }
                attachment?.Writer.TryWrite(buffer);
            }
        }
        var result = await terminal.Completion;
        lock (sync)
        {
            completion = result;
            attachment?.Writer.TryComplete();
        }
    }

    public async ValueTask DisposeAsync()
    {
        await terminal.DisposeAsync();
        if (pump != null)
        {
            await pump;
        }
    }
}

internal sealed class TerminalWebAttachment(
    TerminalWebSession session,
    Channel<byte[]> live,
    byte[][] snapshot) : IDisposable
{
    internal byte[][] Snapshot { get; } = snapshot;
    internal ChannelReader<byte[]> Live => live.Reader;

    public void Dispose() => session.Detach(live);
}

internal sealed record TerminalWebSessionInfo(
    uint Id,
    uint ProcessId,
    TerminalShellInfo Shell,
    string Title,
    bool Ended);
