using KNSoft.ZPigeon.Server.Managed;
using System.Diagnostics;

namespace KNSoft.ZPigeon.Web;

internal sealed class ConnectionPerformanceManager
{
    private const byte DefaultClass = 2;
    private readonly NativeServer server;
    private readonly Lock sync = new();
    private bool automatic = true;
    private bool wasConnected;
    private byte manualSpeedClass = DefaultClass;
    private byte manualLatencyClass = DefaultClass;
    private byte effectiveSpeedClass = DefaultClass;
    private byte effectiveLatencyClass = DefaultClass;
    private int speedCandidate = -1;
    private int speedCandidateCount;
    private int latencyCandidate = -1;
    private int latencyCandidateCount;
    private ulong lastSentBytes;
    private ulong lastReceivedBytes;
    private ulong lastSentSampleTickCount;
    private ulong lastReceivedSampleTickCount;
    private long lastProbeTickCount;
    private uint? roundTripMilliseconds;
    private Task<uint>? probeTask;
    private int qualityLevel;

    internal ConnectionPerformanceManager(NativeServer server)
    {
        this.server = server;
        server.SetConnectionPolicy(DefaultClass, DefaultClass);
    }

    internal ConnectionPerformance Current
    {
        get
        {
            lock (sync)
            {
                if (!server.TryGetConnectionStatistics(out var statistics))
                {
                    wasConnected = false;
                    return Disconnected();
                }
                if (!wasConnected || statistics.SentBytes < lastSentBytes ||
                    statistics.ReceivedBytes < lastReceivedBytes)
                {
                    ResetAutomaticState();
                }
                wasConnected = true;
                lastSentBytes = statistics.SentBytes;
                lastReceivedBytes = statistics.ReceivedBytes;

                var sentMbps = Mbps(statistics.SentBitsPerSecond);
                var receivedMbps = Mbps(statistics.ReceivedBitsPerSecond);
                var speedMbps = sentMbps is null ? receivedMbps :
                                receivedMbps is null ? sentMbps :
                                Math.Min(sentMbps.Value, receivedMbps.Value);
                var speedSampleChanged = statistics.SentSampleTickCount != lastSentSampleTickCount ||
                                         statistics.ReceivedSampleTickCount != lastReceivedSampleTickCount;
                if (automatic && speedSampleChanged && speedMbps is not null)
                {
                    var target = SpeedClass(speedMbps.Value);
                    if (lastSentSampleTickCount == 0 && lastReceivedSampleTickCount == 0)
                    {
                        effectiveSpeedClass = target;
                    }
                    else
                    {
                        UpdateClass(ref effectiveSpeedClass,
                                    target,
                                    ref speedCandidate,
                                    ref speedCandidateCount);
                    }
                }
                if (statistics.SpeedClass != effectiveSpeedClass ||
                    statistics.LatencyClass != effectiveLatencyClass)
                {
                    server.SetConnectionPolicy(effectiveSpeedClass, effectiveLatencyClass);
                }
                if (roundTripMilliseconds is null && speedMbps is null)
                {
                    qualityLevel = 0;
                }
                else
                {
                    qualityLevel = Math.Max(
                        1,
                        Math.Min(speedMbps is null ? 5 : effectiveSpeedClass + 1,
                                 roundTripMilliseconds is null ? 5 :
                                     LatencyClass(roundTripMilliseconds.Value) + 1) -
                        (int)Math.Min(statistics.ConsecutiveFailures, 4U));
                }
                lastSentSampleTickCount = statistics.SentSampleTickCount;
                lastReceivedSampleTickCount = statistics.ReceivedSampleTickCount;
                var lastSample = Math.Max(statistics.SentSampleTickCount,
                                          statistics.ReceivedSampleTickCount);
                return new(true,
                           automatic,
                           manualSpeedClass,
                           manualLatencyClass,
                           effectiveSpeedClass,
                           effectiveLatencyClass,
                           speedMbps,
                           sentMbps,
                           receivedMbps,
                           roundTripMilliseconds,
                           SampleAge(lastSample),
                           statistics.Transport,
                           statistics.SentBytes,
                           statistics.ReceivedBytes,
                           statistics.CompletedRequests,
                           statistics.FailedRequests,
                           statistics.PendingRequests,
                           statistics.ConsecutiveFailures,
                           statistics.OutstandingSendBytes,
                           statistics.MaximumOutstandingSendBytes,
                           statistics.MaximumSendQueueDelayMilliseconds,
                           statistics.RejectedSends,
                           qualityLevel);
            }
        }
    }

    internal ConnectionPerformance Configure(ConnectionPerformanceRequest request)
    {
        if (request.SpeedClass >= 5 || request.LatencyClass >= 5)
        {
            throw new ArgumentOutOfRangeException(nameof(request));
        }
        lock (sync)
        {
            automatic = request.Automatic;
            manualSpeedClass = request.SpeedClass;
            manualLatencyClass = request.LatencyClass;
            if (automatic)
            {
                ResetAutomaticState();
            }
            else
            {
                effectiveSpeedClass = manualSpeedClass;
                effectiveLatencyClass = manualLatencyClass;
            }
            server.SetConnectionPolicy(effectiveSpeedClass, effectiveLatencyClass);
        }
        return Current;
    }

    internal void ProbeIfDue()
    {
        lock (sync)
        {
            if (!automatic || !server.ClientConnected || probeTask != null) return;
            var interval = qualityLevel >= 4 ? 15000 : qualityLevel >= 3 ? 30000 : 60000;
            var now = Environment.TickCount64;
            if (lastProbeTickCount != 0 && now - lastProbeTickCount < interval) return;
            StartProbe(now);
        }
    }

    internal async Task<ConnectionPerformance> ProbeAsync()
    {
        Task<uint> task;

        lock (sync)
        {
            task = probeTask ?? StartProbe(Environment.TickCount64);
        }
        var elapsed = await task;
        lock (sync)
        {
            if (automatic)
            {
                effectiveLatencyClass = LatencyClass(elapsed);
                latencyCandidate = -1;
                latencyCandidateCount = 0;
            }
        }
        return Current;
    }

    private Task<uint> StartProbe(long tickCount)
    {
        lastProbeTickCount = tickCount;
        var task = probeTask = ProbeCoreAsync();
        _ = CompleteProbeAsync(task);
        return task;
    }

    private async Task<uint> ProbeCoreAsync()
    {
        var started = Stopwatch.GetTimestamp();

        await server.ProbeConnectionAsync();
        var elapsed = (uint)Math.Min(Stopwatch.GetElapsedTime(started).TotalMilliseconds,
                                     uint.MaxValue);
        lock (sync)
        {
            var firstSample = roundTripMilliseconds is null;
            roundTripMilliseconds = elapsed;
            if (automatic)
            {
                var target = LatencyClass(elapsed);
                if (firstSample)
                {
                    effectiveLatencyClass = target;
                }
                else
                {
                    UpdateClass(ref effectiveLatencyClass,
                                target,
                                ref latencyCandidate,
                                ref latencyCandidateCount);
                }
            }
        }
        return elapsed;
    }

    private async Task CompleteProbeAsync(Task<uint> task)
    {
        try
        {
            await task;
        }
        catch (NativeException)
        {
        }
        finally
        {
            lock (sync)
            {
                if (ReferenceEquals(probeTask, task)) probeTask = null;
            }
        }
    }

    private ConnectionPerformance Disconnected() =>
        new(false,
            automatic,
            manualSpeedClass,
            manualLatencyClass,
            effectiveSpeedClass,
            effectiveLatencyClass,
            null,
            null,
            null,
            null,
            null,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0);

    private void ResetAutomaticState()
    {
        if (automatic)
        {
            effectiveSpeedClass = DefaultClass;
            effectiveLatencyClass = DefaultClass;
        }
        speedCandidate = latencyCandidate = -1;
        speedCandidateCount = latencyCandidateCount = 0;
        lastSentSampleTickCount = lastReceivedSampleTickCount = 0;
        lastProbeTickCount = 0;
        roundTripMilliseconds = null;
        qualityLevel = 0;
    }

    private static void UpdateClass(
        ref byte current,
        byte target,
        ref int candidate,
        ref int candidateCount)
    {
        if (target == current)
        {
            candidate = -1;
            candidateCount = 0;
            return;
        }
        if (candidate != target)
        {
            candidate = target;
            candidateCount = 1;
        }
        else
        {
            candidateCount++;
        }
        if (candidateCount < (target < current ? 2 : 3)) return;
        current = (byte)(current + Math.Sign(target - current));
        candidate = -1;
        candidateCount = 0;
    }

    private static byte SpeedClass(double megabitsPerSecond) => megabitsPerSecond switch
    {
        <= 2 => 0,
        <= 10 => 1,
        <= 50 => 2,
        <= 200 => 3,
        _ => 4
    };

    private static byte LatencyClass(uint milliseconds) => milliseconds switch
    {
        <= 40 => 4,
        <= 80 => 3,
        <= 150 => 2,
        <= 300 => 1,
        _ => 0
    };

    private static double? Mbps(ulong bitsPerSecond) =>
        bitsPerSecond == 0 ? null : Math.Round(bitsPerSecond / 1_000_000d, 1);

    private static ulong? SampleAge(ulong tickCount)
    {
        if (tickCount == 0) return null;
        var now = (ulong)Environment.TickCount64;
        return now >= tickCount ? now - tickCount : 0;
    }
}

internal sealed record ConnectionPerformance(
    bool Connected,
    bool Automatic,
    byte ManualSpeedClass,
    byte ManualLatencyClass,
    byte EffectiveSpeedClass,
    byte EffectiveLatencyClass,
    double? SpeedMbps,
    double? SentMbps,
    double? ReceivedMbps,
    uint? RoundTripMilliseconds,
    ulong? SampleAgeMilliseconds,
    int Transport,
    ulong SentBytes,
    ulong ReceivedBytes,
    ulong CompletedRequests,
    ulong FailedRequests,
    uint PendingRequests,
    uint ConsecutiveFailures,
    ulong OutstandingSendBytes,
    ulong MaximumOutstandingSendBytes,
    ulong MaximumSendQueueDelayMilliseconds,
    ulong RejectedSends,
    int Level);

internal sealed record ConnectionPerformanceRequest(
    bool Automatic,
    byte SpeedClass,
    byte LatencyClass);
