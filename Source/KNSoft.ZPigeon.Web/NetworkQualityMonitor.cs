namespace KNSoft.ZPigeon.Web;

internal sealed class NetworkQualityMonitor(ConnectionPerformanceManager performance)
{
    internal NetworkQuality Current
    {
        get
        {
            var current = performance.Current;
            if (!current.Connected)
                return NetworkQuality.Disconnected;
            if (current.RoundTripMilliseconds is null)
                return new(current.Level,
                           null,
                           current.FailedRequests,
                           current.ConsecutiveFailures,
                           current.PendingRequests);
            return new(current.Level,
                       current.RoundTripMilliseconds,
                       current.FailedRequests,
                       current.ConsecutiveFailures,
                       current.PendingRequests);
        }
    }
}

internal sealed record NetworkQuality(
    int Level,
    uint? RoundTripMilliseconds,
    ulong FailedRequests,
    uint ConsecutiveFailures,
    uint PendingRequests)
{
    internal static readonly NetworkQuality Disconnected = new(0, null, 0, 0, 0);
}
