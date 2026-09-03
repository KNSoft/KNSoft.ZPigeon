using KNSoft.ZPigeon.Server.Managed;

namespace KNSoft.ZPigeon.Application;

public sealed partial class ZPigeonApplication
{
    private static readonly AdministrationOperation[] AdministrationEnumerations =
    [
        AdministrationOperation.EnumerateUsers,
        AdministrationOperation.EnumerateSessions,
        AdministrationOperation.EnumerateLogonSessions,
        AdministrationOperation.EnumerateUserProfiles,
        AdministrationOperation.EnumerateSoftware,
        AdministrationOperation.EnumerateFeatures,
        AdministrationOperation.EnumeratePackageProviders,
        AdministrationOperation.EnumerateSoftwareDeployments,
        AdministrationOperation.EnumerateInputMethods,
        AdministrationOperation.EnumerateHardware,
        AdministrationOperation.EnumerateUpdates,
        AdministrationOperation.EnumerateTasks,
        AdministrationOperation.EnumerateFirewall,
        AdministrationOperation.EnumeratePower,
        AdministrationOperation.EnumerateSystem,
        AdministrationOperation.EnumerateWlan,
        AdministrationOperation.EnumerateCertificates,
        AdministrationOperation.EnumerateCertificateStores,
        AdministrationOperation.EnumerateClipboard,
        AdministrationOperation.EnumerateCredentials,
        AdministrationOperation.EnumerateFirmwareVariables,
        AdministrationOperation.EnumeratePublishedShares,
        AdministrationOperation.EnumerateNetworkConnections,
        AdministrationOperation.EnumerateNetworkAdapters,
        AdministrationOperation.EnumerateNetworkRoutes,
        AdministrationOperation.EnumerateNetworkEndpoints,
        AdministrationOperation.EnumeratePageFiles,
        AdministrationOperation.EnumerateBluetooth,
        AdministrationOperation.EnumerateFonts,
        AdministrationOperation.EnumerateAppContainers,
        AdministrationOperation.EnumerateWslDistributions,
        AdministrationOperation.EnumerateWslProcesses,
        AdministrationOperation.EnumerateProxyVpn,
        AdministrationOperation.EnumerateClientStatus,
        AdministrationOperation.EnumerateSystemProtection,
        AdministrationOperation.EnumerateRestorePoints,
        AdministrationOperation.EnumerateShadowCopies,
        AdministrationOperation.EnumerateBitLockerVolumes,
        AdministrationOperation.EnumerateBitLockerProtectors
    ];

    private static readonly AdministrationOperation[] AdministrationQueries =
    [
        AdministrationOperation.QueryPackages,
        AdministrationOperation.QueryCertificate,
        AdministrationOperation.QueryFirmware,
        AdministrationOperation.QueryPublishedShare,
        AdministrationOperation.QueryObjectDirectory
    ];

    private static readonly AdministrationControl[] AdministrationControls =
    [
        new(AdministrationOperation.ControlUser,
            [AdministrationAction.Create, AdministrationAction.Delete, AdministrationAction.SetPassword,
             AdministrationAction.Rename, AdministrationAction.Enable, AdministrationAction.Disable,
             AdministrationAction.Disconnect, AdministrationAction.SignOut]),
        new(AdministrationOperation.ControlUserProfile,
            [AdministrationAction.Create, AdministrationAction.Delete, AdministrationAction.Configure]),
        new(AdministrationOperation.ControlFeature,
            [AdministrationAction.Enable, AdministrationAction.Disable]),
        new(AdministrationOperation.ControlInputMethod,
            [AdministrationAction.Enable, AdministrationAction.Disable, AdministrationAction.Activate,
             AdministrationAction.SetDefault]),
        new(AdministrationOperation.ControlHardware,
            [AdministrationAction.Refresh, AdministrationAction.Enable, AdministrationAction.Disable,
             AdministrationAction.Restart, AdministrationAction.Uninstall]),
        new(AdministrationOperation.ControlUpdate,
            [AdministrationAction.Refresh, AdministrationAction.Check]),
        new(AdministrationOperation.ControlTask,
            [AdministrationAction.Run, AdministrationAction.Stop, AdministrationAction.Enable,
             AdministrationAction.Disable, AdministrationAction.Delete, AdministrationAction.Configure]),
        new(AdministrationOperation.ControlFirewall,
            [AdministrationAction.Enable, AdministrationAction.Disable, AdministrationAction.Allow,
             AdministrationAction.Block]),
        new(AdministrationOperation.ControlPower,
            [AdministrationAction.Enable, AdministrationAction.Disable, AdministrationAction.Activate,
             AdministrationAction.Sleep, AdministrationAction.Hibernate, AdministrationAction.Shutdown,
             AdministrationAction.Restart, AdministrationAction.Firmware, AdministrationAction.SignOut,
             AdministrationAction.Lock, AdministrationAction.TurnOffDisplay]),
        new(AdministrationOperation.ControlSystem,
            [AdministrationAction.Configure, AdministrationAction.Delete]),
        new(AdministrationOperation.ControlWlan,
            [AdministrationAction.Connect, AdministrationAction.Disconnect, AdministrationAction.Delete]),
        new(AdministrationOperation.ControlClipboard,
            [AdministrationAction.Configure, AdministrationAction.Delete]),
        new(AdministrationOperation.ControlCredential,
            [AdministrationAction.Create, AdministrationAction.Configure, AdministrationAction.Delete]),
        new(AdministrationOperation.ControlPublishedShare,
            [AdministrationAction.Create, AdministrationAction.Configure, AdministrationAction.Delete]),
        new(AdministrationOperation.ControlNetworkConnection,
            [AdministrationAction.Connect, AdministrationAction.Disconnect]),
        new(AdministrationOperation.ControlNetworkAdapter,
            [AdministrationAction.Enable, AdministrationAction.Disable, AdministrationAction.Configure]),
        new(AdministrationOperation.ControlNetworkRoute,
            [AdministrationAction.Create, AdministrationAction.Configure, AdministrationAction.Delete]),
        new(AdministrationOperation.ControlNetworkEndpoint, [AdministrationAction.Stop]),
        new(AdministrationOperation.ControlPageFile,
            [AdministrationAction.Configure, AdministrationAction.Delete]),
        new(AdministrationOperation.ControlBluetooth,
            [AdministrationAction.Configure, AdministrationAction.Delete]),
        new(AdministrationOperation.ControlFont,
            [AdministrationAction.Install, AdministrationAction.Uninstall]),
        new(AdministrationOperation.ControlAppContainer,
            [AdministrationAction.Create, AdministrationAction.Configure, AdministrationAction.Delete]),
        new(AdministrationOperation.ControlWslDistribution,
            [AdministrationAction.Run, AdministrationAction.Stop, AdministrationAction.Restart,
             AdministrationAction.Activate]),
        new(AdministrationOperation.ControlWslProcess,
            [AdministrationAction.Stop, AdministrationAction.Disable, AdministrationAction.Enable]),
        new(AdministrationOperation.ControlProxyVpn,
            [AdministrationAction.Configure, AdministrationAction.Delete, AdministrationAction.Disconnect]),
        new(AdministrationOperation.ControlSystemProtection,
            [AdministrationAction.Enable, AdministrationAction.Disable, AdministrationAction.Configure]),
        new(AdministrationOperation.ControlRestorePoint,
            [AdministrationAction.Create, AdministrationAction.Delete, AdministrationAction.Activate]),
        new(AdministrationOperation.ControlShadowCopy,
            [AdministrationAction.Create, AdministrationAction.Delete]),
        new(AdministrationOperation.ControlBitLockerVolume,
            [AdministrationAction.Encrypt, AdministrationAction.Decrypt, AdministrationAction.Pause,
             AdministrationAction.Resume, AdministrationAction.Enable, AdministrationAction.Disable,
             AdministrationAction.Lock, AdministrationAction.Unlock]),
        new(AdministrationOperation.ControlBitLockerProtector,
            [AdministrationAction.Create, AdministrationAction.Delete])
    ];

    public async Task<LimitedResult<AdministrationRecord>> GetAdministrationAsync(
        ulong clientId,
        AdministrationOperation operation,
        string? query,
        int limit,
        CancellationToken cancellationToken = default)
    {
        ValidateAdministrationValue(operation, AdministrationEnumerations, nameof(operation));
        ValidateOptionalText(query, 256, nameof(query));
        var values = await RunAsync(clientId,
                                    () => server.EnumerateAdministrationAsync(operation),
                                    cancellationToken)
            .ConfigureAwait(false);
        return Limit(string.IsNullOrWhiteSpace(query) ? values : values.Where(value =>
            Contains(value.Value, query) ||
            Contains(value.Identity, query) ||
            Contains(value.Name, query) ||
            Contains(value.Description, query) ||
            Contains(value.Detail, query)), limit);
    }

    public async Task<LimitedResult<AdministrationRecord>> QueryAdministrationAsync(
        ulong clientId,
        AdministrationOperation operation,
        string identity,
        int limit,
        CancellationToken cancellationToken = default)
    {
        ValidateAdministrationValue(operation, AdministrationQueries, nameof(operation));
        ValidateRequiredText(identity, 32767, nameof(identity));
        return Limit(await RunAsync(clientId,
                                    () => server.QueryAdministrationAsync(operation, identity),
                                    cancellationToken)
            .ConfigureAwait(false), limit);
    }

    public async Task<AdministrationControlResult> ControlAdministrationAsync(
        ulong clientId,
        AdministrationOperation operation,
        AdministrationAction action,
        string? identity,
        string? argument,
        string? secret,
        CancellationToken cancellationToken = default)
    {
        var control = AdministrationControls.SingleOrDefault(value => value.Operation == operation);
        if (control is null) throw new ArgumentOutOfRangeException(nameof(operation));
        ValidateAdministrationValue(action, control.Actions, nameof(action));
        ValidateOptionalText(identity, 32767, nameof(identity));
        ValidateOptionalText(argument, 32767, nameof(argument));
        ValidateOptionalText(secret, 32767, nameof(secret));
        if (operation == AdministrationOperation.ControlFeature)
        {
            ValidateRequiredText(identity!, 32767, nameof(identity));
            var requiredAction = await RunAsync(clientId,
                                                () => server.ControlWindowsFeatureAsync(action, identity!),
                                                cancellationToken).ConfigureAwait(false);
            return new(requiredAction);
        }
        await RunAsync(clientId,
                       () => server.ControlAdministrationAsync(operation,
                                                                action,
                                                                identity,
                                                                argument,
                                                                secret),
                       cancellationToken).ConfigureAwait(false);
        return new(null);
    }

    public static AdministrationCapabilities GetAdministrationCapabilities() =>
        new(ToNames(AdministrationEnumerations),
            ToNames(AdministrationQueries),
            [.. AdministrationControls.Select(control => new AdministrationControlCapability(
                control.Operation.ToString(),
                ToNames(control.Actions)))]);

    private static string[] ToNames<T>(IEnumerable<T> values) where T : struct, Enum =>
        [.. values.Select(value => value.ToString())];

    private static void ValidateAdministrationValue<T>(T value, T[] allowed, string parameterName)
        where T : struct, Enum
    {
        if (!allowed.Contains(value)) throw new ArgumentOutOfRangeException(parameterName);
    }

    private sealed record AdministrationControl(
        AdministrationOperation Operation,
        AdministrationAction[] Actions);
}

public sealed record AdministrationCapabilities(
    string[] Enumerations,
    string[] Queries,
    AdministrationControlCapability[] Controls);

public sealed record AdministrationControlCapability(string Operation, string[] Actions);

public sealed record AdministrationControlResult(WindowsFeatureRequiredAction? RequiredAction);
