using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer
{
    private static readonly NativeMethods.AdministrationCallback AdministrationCallback = CompleteAdministration;

    public Task<AdministrationRecord[]> EnumerateAdministrationAsync(AdministrationOperation operation) =>
        RunManagementAsync<AdministrationRecord[]>(context =>
            NativeMethods.EnumerateAdministration((ushort)operation, AdministrationCallback, context));

    public Task<AdministrationRecord[]> QueryAdministrationAsync(
        AdministrationOperation operation,
        string identity) =>
        RunManagementAsync<AdministrationRecord[]>(context => NativeMethods.QueryAdministration(
            (ushort)operation,
            identity,
            (uint)identity.Length,
            AdministrationCallback,
            context));

    public Task ControlAdministrationAsync(
        AdministrationOperation operation,
        AdministrationAction action,
        string? identity = null,
        string? argument = null,
        string? secret = null) =>
        RunStatusAsync((callback, context) => NativeMethods.ControlAdministration(
            (ushort)operation,
            (ushort)action,
            identity,
            (uint)(identity?.Length ?? 0),
            argument,
            (uint)(argument?.Length ?? 0),
            secret,
            (uint)(secret?.Length ?? 0),
            callback,
            context));

    private static void CompleteAdministration(ZpStatus status, nint records, uint recordCount, nint context)
    {
        var completion = GetCompletion<AdministrationRecord[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new AdministrationRecord[recordCount];
        var size = Marshal.SizeOf<NativeMethods.AdministrationRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.AdministrationRecord>(records + index * size);
            result[index] = new AdministrationRecord(
                (AdministrationKind)record.Kind,
                record.State,
                record.Flags,
                record.Value.ToString(),
                Marshal.PtrToStringUni(record.Identity, (int)record.IdentityLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.Name, (int)record.NameLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.Description, (int)record.DescriptionLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.Detail, (int)record.DetailLength) ?? string.Empty);
        }
        completion.SetResult(result);
    }
}

public enum AdministrationOperation : ushort
{
    EnumerateUsers = 1,
    ControlUser,
    EnumerateSoftware,
    ControlSoftware,
    EnumerateHardware,
    ControlHardware,
    EnumerateUpdates,
    ControlUpdate,
    EnumerateTasks,
    ControlTask,
    EnumerateFirewall,
    ControlFirewall,
    EnumeratePower,
    ControlPower,
    EnumerateFeatures,
    ControlFeature,
    EnumerateSystem,
    ControlSystem,
    EnumerateWlan,
    ControlWlan,
    EnumerateCertificates,
    QueryCertificate,
    ControlCertificate,
    EnumerateClipboard,
    ControlClipboard,
    WaitClipboard,
    QueryWlanProfile,
    EnumerateCredentials,
    QueryCredential,
    ControlCredential,
    EnumerateFirmwareVariables,
    QueryFirmware,
    ControlFirmware,
    EnumeratePublishedShares,
    QueryPublishedShare,
    ControlPublishedShare,
    EnumerateNetworkConnections,
    ControlNetworkConnection,
    EnumerateNetworkAdapters,
    ControlNetworkAdapter,
    EnumerateNetworkRoutes,
    EnumerateNetworkEndpoints,
    ControlNetworkRoute,
    EnumerateSessions,
    EnumerateLogonSessions
}

public enum AdministrationKind : ushort
{
    User = 1,
    DesktopProgram,
    WindowsApp,
    WindowsFeature,
    Device,
    Update,
    Task,
    UpdateHistory,
    TaskFolder,
    FirewallProfile,
    FirewallRule,
    PowerSetting,
    PowerPlan,
    Ups,
    SystemInformation,
    WlanInterface,
    WlanNetwork,
    WlanProfile,
    EnvironmentVariable,
    CertificateStore,
    Certificate,
    CertificateDetails,
    CertificateChain,
    ClipboardFormat,
    ClipboardState,
    WindowsCredential,
    WebCredential,
    FirmwareVariable,
    FirmwareBootEntry,
    CpuidSnapshot,
    SmbiosTable,
    AcpiTable,
    PublishedShare,
    NetworkConnection,
    NetworkAdapter,
    NetworkAdapterAddress,
    NetworkRoute,
    TcpEndpoint,
    UdpEndpoint,
    Session,
    LogonSession
}

public enum CredentialStore : ushort
{
    Windows = 1,
    Web
}

public enum AdministrationAction : ushort
{
    Create = 1,
    Delete,
    Enable,
    Disable,
    SetPassword,
    Run,
    Stop,
    Install,
    Uninstall,
    Refresh,
    Rename,
    Restart,
    Check,
    Allow,
    Block,
    Sleep,
    Hibernate,
    Shutdown,
    SignOut,
    Lock,
    Activate,
    Firmware,
    Configure,
    Connect,
    Disconnect,
    SetPermissions,
    TurnOffDisplay
}

public sealed record AdministrationRecord(
    AdministrationKind Kind,
    uint State,
    uint Flags,
    string Value,
    string Identity,
    string Name,
    string Description,
    string Detail);

internal static partial class NativeMethods
{
    internal delegate void AdministrationCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct AdministrationRecord
    {
        internal readonly ushort Kind;
        internal readonly uint State;
        internal readonly uint Flags;
        internal readonly ulong Value;
        internal readonly nint Identity;
        internal readonly uint IdentityLength;
        internal readonly nint Name;
        internal readonly uint NameLength;
        internal readonly nint Description;
        internal readonly uint DescriptionLength;
        internal readonly nint Detail;
        internal readonly uint DetailLength;
    }

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateAdministration")]
    internal static partial int EnumerateAdministration(
        ushort operation,
        AdministrationCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryAdministration",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryAdministration(
        ushort operation,
        string identity,
        uint identityLength,
        AdministrationCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ControlAdministration",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ControlAdministration(
        ushort operation,
        ushort action,
        string? identity,
        uint identityLength,
        string? argument,
        uint argumentLength,
        string? secret,
        uint secretLength,
        StatusCallback callback,
        nint context);
}
