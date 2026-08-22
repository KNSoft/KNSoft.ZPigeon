using KNSoft.ZPigeon.Server.Managed;
namespace KNSoft.ZPigeon.Web;

internal sealed class NetworkQualityMonitor(NativeServer server)
{
    internal NetworkQuality Current
    {
        get
        {
            if (!server.ClientConnected || !server.TryGetConnectionStatistics(out var statistics))
                return NetworkQuality.Disconnected;
            if (statistics.CompletedRequests == 0)
                return new(statistics.FailedRequests == 0 ? 0 : 1,
                           null,
                           statistics.FailedRequests,
                           statistics.ConsecutiveFailures,
                           statistics.PendingRequests);
            var milliseconds = (uint)Math.Min(statistics.SmoothedRequestMilliseconds, uint.MaxValue);
            return new(Math.Max(1, QualityLevel(milliseconds) -
                                   (int)Math.Min(statistics.ConsecutiveFailures, 4U)),
                       milliseconds,
                       statistics.FailedRequests,
                       statistics.ConsecutiveFailures,
                       statistics.PendingRequests);
        }
    }

    private static int QualityLevel(double milliseconds) => milliseconds switch
    {
        <= 40 => 5,
        <= 80 => 4,
        <= 150 => 3,
        <= 300 => 2,
        _ => 1
    };

}

internal sealed record NetworkQuality(
    int Level,
    uint? RequestMilliseconds,
    ulong FailedRequests,
    uint ConsecutiveFailures,
    uint PendingRequests)
{
    internal static readonly NetworkQuality Disconnected = new(0, null, 0, 0, 0);
}
