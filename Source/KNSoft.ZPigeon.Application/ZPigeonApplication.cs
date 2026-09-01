using KNSoft.ZPigeon.Server.Managed;
using System.Globalization;

namespace KNSoft.ZPigeon.Application;

public sealed partial class ZPigeonApplication(NativeServer server)
{
    private const int MaximumResultCount = 200;

    public ConnectedClientSummary[] GetClients() =>
        server.GetClients()
              .Select(client => new ConnectedClientSummary(
                  client.Id.ToString(CultureInfo.InvariantCulture),
                  client.Fingerprint,
                  client.Address.ToString(),
                  client.Statistics.Transport))
              .ToArray();

    public bool IsClientConnected(ulong clientId) => NativeServer.IsClientConnected(clientId);

    public Task<SystemInfo> GetSystemInfoAsync(ulong clientId, CancellationToken cancellationToken = default) =>
        RunAsync(clientId, server.GetSystemInfoAsync, cancellationToken);

    public Task<string[]> GetEventLogChannelsAsync(
        ulong clientId,
        CancellationToken cancellationToken = default) =>
        RunAsync(clientId, server.EnumerateEventLogChannelsAsync, cancellationToken);

    public Task<EventLogPage> QueryEventLogAsync(
        ulong clientId,
        string channelPath,
        string? query,
        string? bookmark,
        uint maximumEvents,
        CancellationToken cancellationToken = default)
    {
        ValidateRequiredText(channelPath, 512, nameof(channelPath));
        ValidateOptionalText(query, 8192, nameof(query));
        ValidateOptionalText(bookmark, 8192, nameof(bookmark));
        if (maximumEvents is 0 or > MaximumResultCount)
        {
            throw new ArgumentOutOfRangeException(nameof(maximumEvents));
        }
        return RunAsync(clientId,
                        () => server.QueryEventLogPageAsync(channelPath,
                                                            query,
                                                            bookmark,
                                                            maximumEvents),
                        cancellationToken);
    }

    private async Task<T> RunAsync<T>(
        ulong clientId,
        Func<Task<T>> operation,
        CancellationToken cancellationToken)
    {
        ArgumentOutOfRangeException.ThrowIfZero(clientId);
        using var cancellation = server.SelectCancellation(cancellationToken);
        using var client = server.SelectClient(clientId);
        return await operation().ConfigureAwait(false);
    }

    private async Task RunAsync(
        ulong clientId,
        Func<Task> operation,
        CancellationToken cancellationToken)
    {
        ArgumentOutOfRangeException.ThrowIfZero(clientId);
        using var cancellation = server.SelectCancellation(cancellationToken);
        using var client = server.SelectClient(clientId);
        await operation().ConfigureAwait(false);
    }

    private static LimitedResult<T> Limit<T>(IEnumerable<T> values, int limit)
    {
        ValidateLimit(limit);
        var items = values.ToArray();
        return new(items.Length <= limit ? items : items[..limit], items.Length);
    }

    private static void ValidateLimit(int limit)
    {
        if (limit is < 1 or > MaximumResultCount)
        {
            throw new ArgumentOutOfRangeException(nameof(limit));
        }
    }

    private static void ValidateRequiredText(string value, int maximumLength, string parameterName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(value, parameterName);
        if (value.Length > maximumLength || value.Contains('\0'))
        {
            throw new ArgumentException(null, parameterName);
        }
    }

    private static void ValidateOptionalText(string? value, int maximumLength, string parameterName)
    {
        if (value?.Length > maximumLength || value?.Contains('\0') == true)
        {
            throw new ArgumentException(null, parameterName);
        }
    }

    private static bool Contains(string? value, string query) =>
        value?.Contains(query, StringComparison.OrdinalIgnoreCase) == true;
}

public sealed record LimitedResult<T>(T[] Items, int Total)
{
    public bool Truncated => Items.Length < Total;
}

public sealed record ConnectedClientSummary(string Id, string Fingerprint, string Address, int Transport);
