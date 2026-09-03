using System.Buffers.Binary;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer
{
    private const uint CertificateInstallSourcePath = 0x00000001;
    private const uint CertificateInstallExportable = 0x00000002;
    private const int CertificateInstallMaxLength = 0x000C0000;
    private const uint GuidWireSize = 16;
    private static readonly NativeMethods.AdministrationCallback AdministrationCallback = CompleteAdministration;
    private static readonly NativeMethods.DataCallback BinaryDataCallback = CompleteBinaryData;

    public Task<AdministrationRecord[]> EnumerateAdministrationAsync(AdministrationOperation operation) =>
        RunManagementAsync<AdministrationRecord[]>(context =>
            NativeMethods.EnumerateAdministration(ClientId, operation.ModuleId(), operation.OperationId(),
                                                  AdministrationCallback, context));

    public Task<AdministrationRecord[]> QueryAdministrationAsync(
        AdministrationOperation operation,
        string identity) =>
        RunManagementAsync<AdministrationRecord[]>(context => NativeMethods.QueryAdministration(ClientId,
            operation.ModuleId(),
            operation.OperationId(),
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
        RunStatusAsync((callback, context) => NativeMethods.ControlAdministration(ClientId,
            operation.ModuleId(),
            operation.OperationId(),
            (byte)action,
            identity,
            (uint)(identity?.Length ?? 0),
            argument,
            (uint)(argument?.Length ?? 0),
            secret,
            (uint)(secret?.Length ?? 0),
            callback,
            context));

    public async Task<WindowsFeatureRequiredAction> ControlWindowsFeatureAsync(
        AdministrationAction action,
        string identity)
    {
        if (action is not (AdministrationAction.Enable or AdministrationAction.Disable))
            throw new ArgumentOutOfRangeException(nameof(action));
        ArgumentException.ThrowIfNullOrEmpty(identity);
        var operation = AdministrationOperation.ControlFeature;
        var data = await RunManagementAsync<byte[]>(context => NativeMethods.ControlAdministrationResult(
            ClientId,
            operation.ModuleId(),
            operation.OperationId(),
            (byte)action,
            identity,
            (uint)identity.Length,
            null,
            0,
            null,
            0,
            BinaryDataCallback,
            context)).ConfigureAwait(false);
        if (data.Length != sizeof(byte))
            throw new InvalidDataException("The client returned an invalid Windows feature result.");
        var requiredAction = (WindowsFeatureRequiredAction)data[0];
        if (requiredAction is not (WindowsFeatureRequiredAction.None or WindowsFeatureRequiredAction.Reboot))
            throw new InvalidDataException("The client returned an invalid Windows feature action.");
        return requiredAction;
    }

    public Task<byte[]> QueryAdministrationDataAsync(
        AdministrationOperation operation,
        string? identity = null) =>
        RunManagementAsync<byte[]>(context => NativeMethods.QueryAdministrationData(ClientId,
            operation.ModuleId(),
            operation.OperationId(),
            identity,
            (uint)(identity?.Length ?? 0),
            BinaryDataCallback,
            context));

    public unsafe Task ControlAdministrationDataAsync(
        AdministrationOperation operation,
        AdministrationAction action,
        uint flags,
        string identity,
        byte[] data)
    {
        fixed (char* identityPointer = identity)
        fixed (byte* dataPointer = data)
        {
            return ControlAdministrationDataAsync(operation,
                                                   action,
                                                   flags,
                                                   (nint)identityPointer,
                                                   checked((uint)identity.Length * sizeof(char)),
                                                   (nint)dataPointer,
                                                   (uint)data.Length);
        }
    }

    public unsafe Task ControlAdministrationStringDataAsync(
        AdministrationOperation operation,
        AdministrationAction action,
        uint flags,
        string identity,
        string data)
    {
        fixed (char* identityPointer = identity)
        fixed (char* dataPointer = data)
        {
            return ControlAdministrationDataAsync(operation,
                                                   action,
                                                   flags,
                                                   (nint)identityPointer,
                                                   checked((uint)identity.Length * sizeof(char)),
                                                   (nint)dataPointer,
                                                   checked((uint)data.Length * sizeof(char)));
        }
    }

    public unsafe Task ControlAdministrationStringDataAsync(
        AdministrationOperation operation,
        AdministrationAction action,
        uint flags,
        Guid identity,
        string data)
    {
        fixed (char* dataPointer = data)
        {
            return ControlAdministrationDataAsync(operation,
                                                   action,
                                                   flags,
                                                   (nint)(&identity),
                                                   GuidWireSize,
                                                   (nint)dataPointer,
                                                   checked((uint)data.Length * sizeof(char)));
        }
    }

    public Task InstallCertificateFileAsync(
        string storeIdentity,
        string path,
        string? password,
        bool exportable)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        if (path.Length > 32767 || path.Contains('\0')) throw new ArgumentException(null, nameof(path));
        return InstallCertificateAsync(storeIdentity,
                                       password,
                                       Encoding.Unicode.GetBytes(path),
                                       CertificateInstallSourcePath |
                                       (exportable ? CertificateInstallExportable : 0));
    }

    public Task InstallCertificateDataAsync(
        string storeIdentity,
        byte[] data,
        string? password,
        bool exportable)
    {
        ArgumentNullException.ThrowIfNull(data);
        return InstallCertificateAsync(storeIdentity,
                                       password,
                                       data,
                                       exportable ? CertificateInstallExportable : 0);
    }

    private Task InstallCertificateAsync(
        string storeIdentity,
        string? password,
        byte[] source,
        uint flags)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(storeIdentity);
        password ??= string.Empty;
        if (storeIdentity.Length > 65535 || storeIdentity.Contains('\0'))
            throw new ArgumentException(null, nameof(storeIdentity));
        if (password.Length > 32767 || password.Contains('\0'))
            throw new ArgumentException(null, nameof(password));
        if (source.Length == 0) throw new ArgumentException(null, nameof(source));
        var passwordBytes = checked(password.Length * sizeof(char));
        var payload = GC.AllocateUninitializedArray<byte>(
            checked(sizeof(uint) + passwordBytes + source.Length));
        if (payload.Length > CertificateInstallMaxLength) throw new ArgumentException(null, nameof(source));
        BinaryPrimitives.WriteUInt32LittleEndian(payload, (uint)password.Length);
        Encoding.Unicode.GetBytes(password, payload.AsSpan(sizeof(uint), passwordBytes));
        source.CopyTo(payload, sizeof(uint) + passwordBytes);
        try
        {
            return ControlAdministrationDataAsync(AdministrationOperation.ControlCertificateData,
                                                  AdministrationAction.Install,
                                                  flags,
                                                  storeIdentity,
                                                  payload);
        }
        finally
        {
            CryptographicOperations.ZeroMemory(payload);
        }
    }

    private Task ControlAdministrationDataAsync(
        AdministrationOperation operation,
        AdministrationAction action,
        uint flags,
        nint identity,
        uint identityLength,
        nint data,
        uint dataLength) =>
        RunStatusAsync((callback, context) => NativeMethods.ControlAdministrationData(ClientId,
            operation.ModuleId(),
            operation.OperationId(),
            action,
            flags,
            identity,
            identityLength,
            data,
            dataLength,
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
        try
        {
            var result = new AdministrationRecord[recordCount];
            var size = Marshal.SizeOf<NativeMethods.AdministrationRecord>();
            for (var index = 0; index < result.Length; index++)
            {
                var record = Marshal.PtrToStructure<NativeMethods.AdministrationRecord>(records + index * size);
                var kind = (AdministrationKind)record.Kind;
                var deployment = kind == AdministrationKind.SoftwareDeployment;
                if (deployment && record.DataLength != GuidWireSize)
                    throw new InvalidDataException("The client returned an invalid software deployment identifier.");
                result[index] = new AdministrationRecord(
                    kind,
                    record.State,
                    record.Flags,
                    record.Value.ToString(CultureInfo.InvariantCulture),
                    deployment ? Marshal.PtrToStructure<Guid>(record.Data).ToString("D") :
                        ReadString(record.Identity, record.IdentityLength),
                    ReadString(record.Name, record.NameLength),
                    ReadString(record.Description, record.DescriptionLength),
                    ReadString(record.Detail, record.DetailLength),
                    deployment ? null : ReadAdministrationData(kind, record.Data, record.DataLength));
            }
            completion.SetResult(result);
        }
        catch (Exception exception)
        {
            completion.SetException(exception);
        }
    }

    private static object? ReadAdministrationData(
        AdministrationKind kind,
        nint pointer,
        uint length)
    {
        if (length == 0 && kind != AdministrationKind.WindowsFeature) return null;
        var data = GC.AllocateUninitializedArray<byte>(checked((int)length));
        Marshal.Copy(pointer, data, 0, data.Length);
        var span = data.AsSpan();
        switch (kind)
        {
            case AdministrationKind.WindowsFeature when span.Length == 5:
                return new WindowsFeatureData(
                    (WindowsFeatureApplicability)unchecked((sbyte)span[0]),
                    (WindowsFeatureSelectability)unchecked((sbyte)span[1]),
                    (WindowsFeatureInstallState)unchecked((sbyte)span[2]),
                    (WindowsFeatureInstallState)unchecked((sbyte)span[3]),
                    (WindowsFeatureInstallState)unchecked((sbyte)span[4]));
            case AdministrationKind.BluetoothRadio when span.Length == 4:
                return new BluetoothRadioData(
                    BinaryPrimitives.ReadUInt16LittleEndian(span),
                    BinaryPrimitives.ReadUInt16LittleEndian(span[2..]));
            case AdministrationKind.Location when span.Length == 40:
                return new LocationData(
                    ReadDouble(span),
                    ReadDouble(span[8..]),
                    ReadDouble(span[16..]),
                    ReadDouble(span[24..]),
                    ReadDouble(span[32..]));
            case AdministrationKind.Battery or AdministrationKind.Ups when span.Length == 4:
                return new PowerSupplyData(BinaryPrimitives.ReadUInt32LittleEndian(span));
            case AdministrationKind.SystemInformation when span.Length == 12:
                return new SystemDisplayData(
                    BinaryPrimitives.ReadUInt32LittleEndian(span),
                    BinaryPrimitives.ReadUInt32LittleEndian(span[4..]),
                    BinaryPrimitives.ReadUInt32LittleEndian(span[8..]));
            case AdministrationKind.Certificate:
            {
                if (span.Length == 0 || span[0] > 1) break;
                var allPurposes = span[0] != 0;
                var offset = sizeof(byte);
                var friendlyName = ReadAdministrationString(data, ref offset);
                if (data.Length - offset < sizeof(ushort)) break;
                var count = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(offset));
                offset += sizeof(ushort);
                var usages = new string[count];
                for (var index = 0; index < usages.Length; index++)
                    usages[index] = ReadAdministrationString(data, ref offset);
                if (offset == data.Length) return new CertificateMetadata(allPurposes, friendlyName, usages);
                break;
            }
            case AdministrationKind.LogonSession:
            {
                if (span.Length < sizeof(uint)) break;
                var offset = sizeof(uint);
                var result = new LogonSessionData(
                    BinaryPrimitives.ReadUInt32LittleEndian(span),
                    ReadAdministrationString(data, ref offset),
                    ReadAdministrationString(data, ref offset),
                    ReadAdministrationString(data, ref offset),
                    ReadAdministrationString(data, ref offset));
                if (offset == data.Length) return result;
                break;
            }
        }
        throw new InvalidDataException("The client returned invalid Administration extension data.");
    }

    private static double ReadDouble(ReadOnlySpan<byte> data) =>
        BitConverter.Int64BitsToDouble((long)BinaryPrimitives.ReadUInt64LittleEndian(data));

    private static string ReadAdministrationString(byte[] data, ref int offset)
    {
        if (data.Length - offset < sizeof(uint))
            throw new InvalidDataException("The client returned an invalid Administration string.");
        var characterCount = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(offset));
        offset += sizeof(uint);
        var byteCount = checked((int)characterCount * sizeof(char));
        if (data.Length - offset < byteCount)
            throw new InvalidDataException("The client returned an invalid Administration string.");
        var result = Encoding.Unicode.GetString(data, offset, byteCount);
        offset += byteCount;
        return result;
    }

    private static void CompleteBinaryData(ZpStatus status, nint data, uint dataLength, nint context)
    {
        var completion = GetCompletion<byte[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = GC.AllocateUninitializedArray<byte>(checked((int)dataLength));
        Marshal.Copy(data, result, 0, result.Length);
        completion.SetResult(result);
    }
}

public enum AdministrationOperation : ushort
{
    EnumerateUsers = 9 << 8 | 1,
    ControlUser = 9 << 8 | 2,
    EnumerateSessions = 9 << 8 | 3,
    EnumerateLogonSessions = 9 << 8 | 4,
    EnumerateUserProfiles = 9 << 8 | 5,
    ControlUserProfile = 9 << 8 | 6,
    EnumerateSoftware = 20 << 8 | 1,
    ControlSoftware = 20 << 8 | 2,
    EnumerateFeatures = 20 << 8 | 3,
    ControlFeature = 20 << 8 | 4,
    EnumeratePackageProviders = 20 << 8 | 5,
    EnumerateSoftwareDeployments = 20 << 8 | 6,
    EnumerateInputMethods = 20 << 8 | 7,
    ControlInputMethod = 20 << 8 | 8,
    QueryPackages = 20 << 8 | 9,
    EnumerateHardware = 21 << 8 | 1,
    ControlHardware = 21 << 8 | 2,
    EnumerateUpdates = 22 << 8 | 1,
    ControlUpdate = 22 << 8 | 2,
    EnumerateTasks = 23 << 8 | 1,
    ControlTask = 23 << 8 | 2,
    EnumerateFirewall = 24 << 8 | 1,
    ControlFirewall = 24 << 8 | 2,
    EnumeratePower = 25 << 8 | 1,
    ControlPower = 25 << 8 | 2,
    EnumerateSystem = 26 << 8 | 1,
    ControlSystem = 26 << 8 | 2,
    EnumerateWlan = 27 << 8 | 1,
    ControlWlan = 27 << 8 | 2,
    QueryWlanProfile = 27 << 8 | 3,
    EnumerateCertificates = 28 << 8 | 1,
    QueryCertificate = 28 << 8 | 2,
    QueryCertificateData = 28 << 8 | 3,
    ControlCertificateData = 28 << 8 | 4,
    EnumerateCertificateStores = 28 << 8 | 5,
    EnumerateClipboard = 29 << 8 | 1,
    ControlClipboard = 29 << 8 | 2,
    WaitClipboard = 29 << 8 | 3,
    QueryClipboardImage = 29 << 8 | 4,
    EnumerateCredentials = 30 << 8 | 1,
    QueryCredential = 30 << 8 | 2,
    ControlCredential = 30 << 8 | 3,
    EnumerateFirmwareVariables = 31 << 8 | 1,
    QueryFirmware = 31 << 8 | 2,
    QueryFirmwareData = 31 << 8 | 3,
    ControlFirmwareData = 31 << 8 | 4,
    EnumeratePublishedShares = 32 << 8 | 1,
    QueryPublishedShare = 32 << 8 | 2,
    ControlPublishedShare = 32 << 8 | 3,
    EnumerateNetworkConnections = 32 << 8 | 4,
    ControlNetworkConnection = 32 << 8 | 5,
    ControlPublishedShareSecurity = 32 << 8 | 6,
    EnumerateNetworkAdapters = 33 << 8 | 1,
    ControlNetworkAdapter = 33 << 8 | 2,
    EnumerateNetworkRoutes = 33 << 8 | 3,
    EnumerateNetworkEndpoints = 33 << 8 | 4,
    ControlNetworkRoute = 33 << 8 | 5,
    ControlNetworkEndpoint = 33 << 8 | 6,
    EnumeratePageFiles = 34 << 8 | 1,
    ControlPageFile = 34 << 8 | 2,
    EnumerateBluetooth = 35 << 8 | 1,
    ControlBluetooth = 35 << 8 | 2,
    WaitKeyboard = 36 << 8 | 1,
    QueryLocation = 37 << 8 | 1,
    EnumerateFonts = 38 << 8 | 1,
    ControlFont = 38 << 8 | 2,
    EnumerateAppContainers = 39 << 8 | 1,
    ControlAppContainer = 39 << 8 | 2,
    QueryObjectDirectory = 40 << 8 | 1,
    EnumerateWslDistributions = 41 << 8 | 1,
    ControlWslDistribution = 41 << 8 | 2,
    EnumerateWslProcesses = 41 << 8 | 3,
    ControlWslProcess = 41 << 8 | 4,
    QueryUiAutomationChildren = 42 << 8 | 1,
    QueryUiAutomationProperties = 42 << 8 | 2,
    EnumerateProxyVpn = 43 << 8 | 1,
    ControlProxyVpn = 43 << 8 | 2,
    EnumerateClientStatus = 44 << 8 | 1,
    EnumerateSystemProtection = 45 << 8 | 1,
    EnumerateRestorePoints = 45 << 8 | 2,
    EnumerateShadowCopies = 45 << 8 | 3,
    ControlSystemProtection = 45 << 8 | 4,
    ControlRestorePoint = 45 << 8 | 5,
    ControlShadowCopy = 45 << 8 | 6,
    EnumerateBitLockerVolumes = 46 << 8 | 1,
    EnumerateBitLockerProtectors = 46 << 8 | 2,
    ControlBitLockerVolume = 46 << 8 | 3,
    ControlBitLockerProtector = 46 << 8 | 4
}

internal static class AdministrationOperationExtensions
{
    internal static byte ModuleId(this AdministrationOperation operation) => (byte)((ushort)operation >> 8);

    internal static byte OperationId(this AdministrationOperation operation) => (byte)operation;
}

#pragma warning disable CA1720 // Names mirror the native protocol vocabulary.
public enum AdministrationKind : byte
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
    LogonSession,
    PageFile,
    BluetoothRadio,
    BluetoothDevice,
    KeyboardEvent,
    Location,
    Font,
    AppContainerProfile,
    AppContainerCapability,
    AppContainerBinary,
    ObjectDirectory,
    Object,
    Battery,
    WslDistribution,
    WslProcess,
    UiAutomationElement,
    Proxy,
    Vpn,
    Package,
    SoftwareDeployment,
    InputMethodContext,
    InputMethod,
    PackageContext,
    PackageProvider,
    ClientStatus,
    ClientEnvironmentVariable,
    UiAutomationProperty,
    SecurityDescriptor,
    UserProfile,
    SystemProtectionVolume,
    RestorePoint,
    ShadowCopy,
    BitLockerVolume,
    BitLockerProtector,
    ClipboardFile,
    WindowsFeatureParent
}
#pragma warning restore CA1720

public enum CredentialStore : ushort
{
    Windows = 1,
    Web
}

public enum AdministrationAction : byte
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
    TurnOffDisplay,
    Upgrade,
    SetDefault,
    Encrypt,
    Decrypt,
    Pause,
    Resume,
    Unlock
}

public enum WindowsFeatureApplicability : sbyte
{
    Invalid = -1,
    All = 0,
    NotApplicable = 1,
    NeedsParent = 2,
    Applicable = 4
}

public enum WindowsFeatureSelectability : sbyte
{
    Invalid = -1,
    All = 0,
    Son = 1,
    Root = 2
}

public enum WindowsFeatureInstallState : sbyte
{
    PartiallyInstalled = -19,
    Cancel = -18,
    Superseded = -17,
    Default = -16,
    InvalidPermanent = -8,
    InvalidInstalled = -7,
    InvalidStaged = -4,
    InvalidResolved = -2,
    Unknown = -1,
    Absent = 0,
    Resolving = 1,
    Resolved = 2,
    Staging = 3,
    Staged = 4,
    UninstallRequested = 5,
    InstallRequested = 6,
    Installed = 7,
    Permanent = 8,
    Invalid = sbyte.MaxValue
}

public enum WindowsFeatureRequiredAction : byte
{
    None = 0,
    Reboot = 1
}

public sealed record AdministrationRecord(
    AdministrationKind Kind,
    uint State,
    uint Flags,
    string Value,
    string Identity,
    string Name,
    string Description,
    string Detail,
    object? Data);

public sealed record WindowsFeatureData(
    WindowsFeatureApplicability Applicability,
    WindowsFeatureSelectability Selectability,
    WindowsFeatureInstallState CurrentState,
    WindowsFeatureInstallState IntendedState,
    WindowsFeatureInstallState RequestedState);
public sealed record BluetoothRadioData(ushort Manufacturer, ushort LmpSubversion);
public sealed record LocationData(
    double Latitude,
    double Longitude,
    double Accuracy,
    double Altitude,
    double AltitudeAccuracy);
public sealed record PowerSupplyData(uint EstimatedTime);
public sealed record SystemDisplayData(uint Width, uint Height, uint Frequency);
public sealed record CertificateMetadata(bool AllPurposes, string FriendlyName, string[] EnhancedKeyUsages);
public sealed record LogonSessionData(
    uint SessionId,
    string AuthenticationPackage,
    string UserPrincipalName,
    string LogonServer,
    string DnsDomain);

internal static partial class NativeMethods
{
    internal delegate void AdministrationCallback(ZpStatus status, nint records, uint recordCount, nint context);
    internal delegate void DataCallback(ZpStatus status, nint data, uint dataLength, nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct AdministrationRecord
    {
        internal readonly byte Kind;
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
        internal readonly nint Data;
        internal readonly uint DataLength;
    }

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateAdministration")]
    internal static partial int EnumerateAdministration(
        ulong clientId,
        byte moduleId,
        byte operationId,
        AdministrationCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryAdministration",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryAdministration(
        ulong clientId,
        byte moduleId,
        byte operationId,
        string identity,
        uint identityLength,
        AdministrationCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryAdministrationData",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryAdministrationData(
        ulong clientId,
        byte moduleId,
        byte operationId,
        string? identity,
        uint identityLength,
        DataCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_ControlAdministrationData")]
    internal static partial int ControlAdministrationData(
        ulong clientId,
        byte moduleId,
        byte operationId,
        AdministrationAction action,
        uint flags,
        nint identity,
        uint identityLength,
        nint data,
        uint dataLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ControlAdministration",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ControlAdministration(
        ulong clientId,
        byte moduleId,
        byte operationId,
        byte action,
        string? identity,
        uint identityLength,
        string? argument,
        uint argumentLength,
        string? secret,
        uint secretLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ControlAdministrationResult",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ControlAdministrationResult(
        ulong clientId,
        byte moduleId,
        byte operationId,
        byte action,
        string? identity,
        uint identityLength,
        string? argument,
        uint argumentLength,
        string? secret,
        uint secretLength,
        DataCallback callback,
        nint context);
}
