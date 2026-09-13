using KNSoft.ZPigeon.Server.Managed;
using System.Globalization;

namespace KNSoft.ZPigeon.Web;

internal sealed class RdpPatchManager(NativeServer server, string catalogPath) : IDisposable
{
    private const uint ConfigurationEnabled = 0x00010000;
    private const uint ConfigurationSameUserMultipleSessions = 0x00020000;
    private const uint ChildSessionsEnabled = 0x00000001;
    private const uint ChildSessionCredentialDelegation = 0x00000002;
    private const uint ChildSessionHelloOnly = 0x00000004;
    private const uint EnablePatch = 1;
    private const uint ServiceStopped = 1;
    private const uint StatusNotFound = 0xC0000225;
    private const uint StatusServiceNotActive = 0xC0070426;
    private readonly SemaphoreSlim gate = new(1, 1);
    private readonly Lazy<RdpPatchCatalog> catalog = new(() => new(catalogPath));

    internal async Task<RdpStatus> GetStatusAsync()
    {
        await gate.WaitAsync().ConfigureAwait(false);
        try
        {
            var configuration = await GetConfigurationAsync().ConfigureAwait(false);
            if (!catalog.Value.TryCreatePlan(configuration.VersionValue, out var plan))
            {
                return configuration.ToStatus(false, null, null);
            }
            if (configuration.ServiceState == ServiceStopped)
            {
                return configuration.ToStatus(true, false, null);
            }
            try
            {
                await server.ControlAdministrationDataAsync(AdministrationOperation.ControlRemoteDesktopPatch,
                                                             AdministrationAction.Check,
                                                             0,
                                                             string.Empty,
                                                             plan).ConfigureAwait(false);
                return configuration.ToStatus(true, true, null);
            }
            catch (NativeException exception) when (
                exception.Status.Type == ZpStatusType.NtStatus &&
                exception.Status.Code is StatusNotFound or StatusServiceNotActive)
            {
                return configuration.ToStatus(true, false, null);
            }
            catch (NativeException exception)
            {
                return configuration.ToStatus(true, null, exception.Status);
            }
        }
        finally
        {
            gate.Release();
        }
    }

    internal async Task ConfigureAsync(bool enabled, ushort port, bool sameUserMultipleSessions)
    {
        var flags = (uint)port |
                    (enabled ? ConfigurationEnabled : 0) |
                    (sameUserMultipleSessions ? ConfigurationSameUserMultipleSessions : 0);
        await gate.WaitAsync().ConfigureAwait(false);
        try
        {
            await server.ControlAdministrationDataAsync(AdministrationOperation.ConfigureRemoteDesktop,
                                                         AdministrationAction.Configure,
                                                         flags,
                                                         string.Empty,
                                                         []).ConfigureAwait(false);
        }
        finally
        {
            gate.Release();
        }
    }

    internal async Task SetEnabledAsync(bool enabled)
    {
        await gate.WaitAsync().ConfigureAwait(false);
        try
        {
            var configuration = await GetConfigurationAsync().ConfigureAwait(false);
            if (!catalog.Value.TryCreatePlan(configuration.VersionValue, out var plan))
                throw new NotSupportedException("The termsrv.dll version is not present in rdpwrap.ini.");
            if (!enabled && configuration.ServiceState == ServiceStopped) return;
            await server.ControlAdministrationDataAsync(AdministrationOperation.ControlRemoteDesktopPatch,
                                                         AdministrationAction.Configure,
                                                         enabled ? EnablePatch : 0,
                                                         string.Empty,
                                                         plan).ConfigureAwait(false);
        }
        finally
        {
            gate.Release();
        }
    }

    internal async Task<RdpChildSessionStatus> GetChildSessionStatusAsync()
    {
        await gate.WaitAsync().ConfigureAwait(false);
        try
        {
            return (await GetConfigurationAsync().ConfigureAwait(false)).ChildSession;
        }
        finally
        {
            gate.Release();
        }
    }

    internal Task SetChildSessionsEnabledAsync(bool enabled) =>
        server.ControlAdministrationAsync(AdministrationOperation.ControlRemoteDesktopChildSession,
                                          enabled ? AdministrationAction.Enable : AdministrationAction.Disable);

    internal Task SetChildSessionRunningAsync(bool running) =>
        server.ControlAdministrationAsync(AdministrationOperation.ControlRemoteDesktopChildSession,
                                          running ? AdministrationAction.Run : AdministrationAction.Stop);

    internal Task SetChildSessionCredentialDelegationAsync(bool enabled) =>
        server.ControlAdministrationAsync(AdministrationOperation.ControlRemoteDesktopChildSession,
                                          enabled ? AdministrationAction.Allow : AdministrationAction.Block);

    internal Task SetChildSessionHelloOnlyAsync(bool enabled) =>
        server.ControlAdministrationAsync(AdministrationOperation.ControlRemoteDesktopChildSession,
                                          enabled ? AdministrationAction.Lock : AdministrationAction.Unlock);

    private async Task<RdpConfiguration> GetConfigurationAsync()
    {
        var records = await server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateRemoteDesktop)
                                  .ConfigureAwait(false);
        var childSession = GetRecord(records, "remoteDesktopChildSession");
        var childValue = ParseValue(childSession);
        return new(GetBoolean(records, "remoteDesktopEnabled"),
                   checked((ushort)GetValue(records, "remoteDesktopPort")),
                   GetBoolean(records, "remoteDesktopSameUserMultipleSessions"),
                   checked((uint)GetValue(records, "remoteDesktopServiceState")),
                   GetValue(records, "remoteDesktopVersion"),
                   new((childSession.Flags & ChildSessionsEnabled) != 0,
                       (childSession.Flags & ChildSessionCredentialDelegation) != 0,
                       (childSession.Flags & ChildSessionHelloOnly) != 0,
                       childSession.State,
                       (uint)childValue,
                       (uint)(childValue >> 32)));
    }

    private static bool GetBoolean(AdministrationRecord[] records, string identity) =>
        GetValue(records, identity) switch
        {
            0 => false,
            1 => true,
            _ => throw new InvalidDataException($"Invalid {identity} value.")
        };

    private static AdministrationRecord GetRecord(AdministrationRecord[] records, string identity) =>
        Array.Find(records, record => record.Identity == identity) ??
        throw new InvalidDataException($"Missing {identity} value.");

    private static ulong ParseValue(AdministrationRecord record) =>
        ulong.TryParse(record.Value, NumberStyles.None, CultureInfo.InvariantCulture, out var value) ? value :
        throw new InvalidDataException($"Invalid {record.Identity} value.");

    private static ulong GetValue(AdministrationRecord[] records, string identity)
    {
        return ParseValue(GetRecord(records, identity));
    }

    public void Dispose() => gate.Dispose();

    private sealed record RdpConfiguration(
        bool Enabled,
        ushort Port,
        bool SameUserMultipleSessions,
        uint ServiceState,
        ulong VersionValue,
        RdpChildSessionStatus ChildSession)
    {
        internal RdpStatus ToStatus(bool supported, bool? applied, ZpStatus? error) =>
            new(Enabled,
                Port,
                SameUserMultipleSessions,
                ServiceState,
                RdpPatchCatalog.FormatVersion(VersionValue),
                supported,
                applied,
                error,
                ChildSession.Enabled,
                ChildSession.CredentialDelegation,
                ChildSession.HelloOnly,
                ChildSession.State,
                ChildSession.SessionId,
                ChildSession.Error);
    }
}

internal sealed record RdpStatus(
    bool Enabled,
    ushort Port,
    bool SameUserMultipleSessions,
    uint ServiceState,
    string Version,
    bool Supported,
    bool? Applied,
    ZpStatus? Error,
    bool ChildSessionsEnabled,
    bool ChildSessionCredentialDelegation,
    bool ChildSessionHelloOnly,
    uint ChildSessionState,
    uint ChildSessionId,
    uint ChildSessionError);

internal sealed record RdpChildSessionStatus(
    bool Enabled,
    bool CredentialDelegation,
    bool HelloOnly,
    uint State,
    uint SessionId,
    uint Error);
