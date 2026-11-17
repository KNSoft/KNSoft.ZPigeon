using System.Text.Json;

namespace KNSoft.ZPigeon.Web;

internal sealed class McpPolicyStore
{
    private readonly Lock sync = new();
    private readonly string path;
    private readonly Queue<DateTime> callTimes = [];
    private McpPolicyConfiguration configuration;
    private int activeCalls;

    internal McpPolicyStore(string path)
    {
        this.path = path;
        configuration = File.Exists(path) ?
            JsonSerializer.Deserialize<McpPolicyConfiguration>(File.ReadAllBytes(path), JsonSerializerOptions.Web) ??
            new() :
            new();
        Validate(configuration);
    }

    internal McpPolicyConfiguration Get()
    {
        lock (sync) return configuration;
    }

    internal McpPolicyConfiguration Update(McpPolicyConfiguration value)
    {
        Validate(value);
        lock (sync)
        {
            configuration = value;
            Save();
            return configuration;
        }
    }

    internal IDisposable? TryAcquire()
    {
        lock (sync)
        {
            var now = DateTime.UtcNow;
            var cutoff = now - TimeSpan.FromMinutes(1);
            while (callTimes.TryPeek(out var timestamp) && timestamp <= cutoff)
                callTimes.Dequeue();
            if (activeCalls >= configuration.MaxConcurrentCalls ||
                callTimes.Count >= configuration.MaxCallsPerMinute)
                return null;
            activeCalls++;
            callTimes.Enqueue(now);
            return new CallLease(this);
        }
    }

    private void Release()
    {
        lock (sync) activeCalls--;
    }

    private void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + ".tmp";
        File.WriteAllBytes(temporary, JsonSerializer.SerializeToUtf8Bytes(configuration, JsonSerializerOptions.Web));
        File.Move(temporary, path, true);
    }

    private static void Validate(McpPolicyConfiguration value)
    {
        if (value.MaxConcurrentCalls is < 1 or > 64)
            throw new ArgumentException("Maximum concurrent MCP calls must be between 1 and 64.");
        if (value.MaxCallsPerMinute is < 1 or > 10000)
            throw new ArgumentException("Maximum MCP calls per minute must be between 1 and 10000.");
        if (value.CallTimeoutSeconds is < 1 or > 3600)
            throw new ArgumentException("MCP call timeout must be between 1 and 3600 seconds.");
        if (value.MaxResultSizeBytes is < 65536 or > 67108864)
            throw new ArgumentException("Maximum MCP result size must be between 65536 and 67108864 bytes.");
    }

    private sealed class CallLease(McpPolicyStore store) : IDisposable
    {
        private int disposed;

        public void Dispose()
        {
            if (Interlocked.Exchange(ref disposed, 1) == 0) store.Release();
        }
    }
}

internal sealed record McpPolicyConfiguration(
    bool Enabled = false,
    bool ReadOnlyOnly = true,
    int MaxConcurrentCalls = 4,
    int MaxCallsPerMinute = 120,
    int CallTimeoutSeconds = 120,
    int MaxResultSizeBytes = 4194304);

internal sealed record McpPolicyView(
    bool Enabled,
    bool ReadOnlyOnly,
    int MaxConcurrentCalls,
    int MaxCallsPerMinute,
    int CallTimeoutSeconds,
    int MaxResultSizeBytes,
    string Endpoint);
