using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer(string directory) : IDisposable
{
    private const ushort Port = 4433;
    private static readonly NativeMethods.SystemInfoCallback SystemCallback =
        CompleteSystemInfo;
    internal static readonly NativeMethods.StatusCallback StatusCallback =
        CompleteStatus;
    private static readonly NativeMethods.EventLogCallback EventLogCallback =
        CompleteEventLog;
    private static readonly NativeMethods.EventLogChannelsCallback EventLogChannelsCallback =
        CompleteEventLogChannels;
    private static readonly NativeMethods.EventLogChannelInfoCallback EventLogChannelInfoCallback =
        CompleteEventLogChannelInfo;
    private X509Certificate2? certificate;

    public int State => NativeMethods.GetState();
    public bool ClientConnected => NativeMethods.IsClientConnected();

    public void Start()
    {
        certificate = LoadCertificate(directory);
        ThrowIfFailed(NativeMethods.Start(certificate.Handle, Port));
    }

    public Task<SystemInfo> GetSystemInfoAsync()
    {
        var completion = new TaskCompletionSource<SystemInfo>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        var status = NativeMethods.GetSystemInfo(SystemCallback,
                                                  GCHandle.ToIntPtr(handle));
        if (status < 0)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    public Task<EventLogPage> QueryEventLogPageAsync(
        string channelPath,
        string? query,
        string? bookmark,
        uint maxEvents,
        bool forward = false)
    {
        var completion = new TaskCompletionSource<EventLogPage>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        var status = NativeMethods.QueryEventLogPage(
            channelPath,
            (uint)channelPath.Length,
            query,
            (uint)(query?.Length ?? 0),
            bookmark,
            (uint)(bookmark?.Length ?? 0),
            forward,
            maxEvents,
            EventLogCallback,
            GCHandle.ToIntPtr(handle));
        if (status < 0)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    public Task<string[]> EnumerateEventLogChannelsAsync()
    {
        var completion = new TaskCompletionSource<string[]>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        var status = NativeMethods.EnumerateEventLogChannels(EventLogChannelsCallback,
                                                              GCHandle.ToIntPtr(handle));
        if (status < 0)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    public Task<EventLogChannelInfo> QueryEventLogChannelInfoAsync(string channelPath)
    {
        var completion = new TaskCompletionSource<EventLogChannelInfo>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        var status = NativeMethods.QueryEventLogChannelInfo(channelPath,
                                                             (uint)channelPath.Length,
                                                             EventLogChannelInfoCallback,
                                                             GCHandle.ToIntPtr(handle));
        if (status < 0)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    public Task SetEventLogChannelEnabledAsync(string channelPath, bool enabled) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.SetEventLogChannelEnabled(channelPath,
                                                     (uint)channelPath.Length,
                                                     enabled,
                                                     callback,
                                                     context));

    public Task ClearEventLogAsync(string channelPath) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.ClearEventLog(channelPath,
                                        (uint)channelPath.Length,
                                        callback,
                                        context));

    public Task ConfigureEventLogChannelAsync(
        string channelPath,
        bool enabled,
        EventLogRetentionMode retentionMode,
        ulong maximumSize) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.ConfigureEventLogChannel(channelPath,
                                                     (uint)channelPath.Length,
                                                     enabled,
                                                     (ushort)retentionMode,
                                                     maximumSize,
                                                     callback,
                                                     context));

    private static Task RunStatusAsync(
        Func<NativeMethods.StatusCallback, nint, int> start)
    {
        var completion = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        var status = start(StatusCallback, GCHandle.ToIntPtr(handle));
        if (status < 0)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    private static void CompleteSystemInfo(
        ZpStatus status,
        int architecture,
        uint majorVersion,
        uint minorVersion,
        uint buildNumber,
        uint processorCount,
        ulong physicalMemoryBytes,
        nint computerName,
        uint computerNameLength,
        nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var completion = (TaskCompletionSource<SystemInfo>)handle.Target!;
        handle.Free();
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        completion.SetResult(new SystemInfo(
            architecture,
            majorVersion,
            minorVersion,
            buildNumber,
            processorCount,
            physicalMemoryBytes,
            Marshal.PtrToStringUni(computerName, (int)computerNameLength) ??
                string.Empty));
    }

    private static void CompleteStatus(ZpStatus status, nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var completion = (TaskCompletionSource)handle.Target!;
        handle.Free();
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
        }
        else
        {
            completion.SetResult();
        }
    }

    private static void CompleteEventLog(
        ZpStatus status,
        [MarshalAs(UnmanagedType.U1)] bool hasMore,
        nint nextBookmark,
        uint nextBookmarkLength,
        nint records,
        uint recordCount,
        nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var completion = (TaskCompletionSource<EventLogPage>)handle.Target!;
        handle.Free();
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new EventLogRecord[recordCount];
        var size = Marshal.SizeOf<NativeMethods.EventLogRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.EventLogRecord>(
                records + index * size);
            result[index] = new EventLogRecord(
                Marshal.PtrToStringUni(record.Bookmark,
                                       (int)record.BookmarkLength) ??
                    string.Empty,
                Marshal.PtrToStringUni(record.Xml, (int)record.XmlLength) ??
                    string.Empty);
        }
        completion.SetResult(new EventLogPage(
            hasMore,
            Marshal.PtrToStringUni(nextBookmark,
                                   (int)nextBookmarkLength) ??
                string.Empty,
            result));
    }

    private static void CompleteEventLogChannels(
        ZpStatus status,
        nint channels,
        uint channelCount,
        nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var completion = (TaskCompletionSource<string[]>)handle.Target!;
        handle.Free();
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new string[channelCount];
        var size = Marshal.SizeOf<NativeMethods.StringView>();
        for (var index = 0; index < result.Length; index++)
        {
            var value = Marshal.PtrToStructure<NativeMethods.StringView>(channels + index * size);
            result[index] = Marshal.PtrToStringUni(value.Buffer, (int)value.Length) ?? string.Empty;
        }
        completion.SetResult(result);
    }

    private static void CompleteEventLogChannelInfo(
        ZpStatus status,
        bool enabled,
        uint type,
        ushort retentionMode,
        ulong maximumSize,
        ulong fileSize,
        ulong creationTime,
        ulong lastAccessTime,
        ulong lastWriteTime,
        nint logFilePath,
        uint logFilePathLength,
        nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var completion = (TaskCompletionSource<EventLogChannelInfo>)handle.Target!;
        handle.Free();
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        completion.SetResult(new EventLogChannelInfo(
            enabled,
            type,
            (EventLogRetentionMode)retentionMode,
            maximumSize.ToString(),
            fileSize.ToString(),
            creationTime.ToString(),
            lastAccessTime.ToString(),
            lastWriteTime.ToString(),
            Marshal.PtrToStringUni(logFilePath, (int)logFilePathLength) ?? string.Empty));
    }

    private static X509Certificate2 LoadCertificate(string directory)
    {
        var rootPath = Path.Combine(directory, "zpigeon-root.cer");
        var serverPath = Path.Combine(directory, "zpigeon-server.pfx");
        if (!File.Exists(rootPath) || !File.Exists(serverPath))
        {
            CreateCertificates(rootPath, serverPath);
        }
        return X509CertificateLoader.LoadPkcs12FromFile(
            serverPath,
            null,
            X509KeyStorageFlags.UserKeySet);
    }

    private static void CreateCertificates(string rootPath, string serverPath)
    {
        using var rootKey = ECDsa.Create(ECCurve.NamedCurves.nistP256);
        var rootRequest = new CertificateRequest(
            "CN=KNSoft ZPigeon Local Root",
            rootKey,
            HashAlgorithmName.SHA256);
        rootRequest.CertificateExtensions.Add(
            new X509BasicConstraintsExtension(true, false, 0, true));
        rootRequest.CertificateExtensions.Add(
            new X509KeyUsageExtension(X509KeyUsageFlags.KeyCertSign,
                                      true));
        using var root = rootRequest.CreateSelfSigned(
            DateTimeOffset.UtcNow.AddMinutes(-1),
            DateTimeOffset.UtcNow.AddYears(10));

        using var serverKey = ECDsa.Create(ECCurve.NamedCurves.nistP256);
        var serverRequest = new CertificateRequest(
            "CN=localhost",
            serverKey,
            HashAlgorithmName.SHA256);
        serverRequest.CertificateExtensions.Add(
            new X509BasicConstraintsExtension(false, false, 0, true));
        serverRequest.CertificateExtensions.Add(
            new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature,
                                      true));
        serverRequest.CertificateExtensions.Add(
            new X509EnhancedKeyUsageExtension(
                new OidCollection
                {
                    new("1.3.6.1.5.5.7.3.1")
                },
                true));
        var names = new SubjectAlternativeNameBuilder();
        names.AddDnsName("localhost");
        serverRequest.CertificateExtensions.Add(names.Build());
        var serial = RandomNumberGenerator.GetBytes(16);
        using var issued = serverRequest.Create(
            root,
            DateTimeOffset.UtcNow.AddMinutes(-1),
            DateTimeOffset.UtcNow.AddYears(2),
            serial);
        using var server = issued.CopyWithPrivateKey(serverKey);
        File.WriteAllBytes(rootPath, root.Export(X509ContentType.Cert));
        File.WriteAllBytes(serverPath, server.Export(X509ContentType.Pkcs12));
    }

    internal static void ThrowIfFailed(int status)
    {
        if (status < 0)
        {
            throw new NativeException(ZpStatus.FromNtStatus(status));
        }
    }

    internal static void ThrowIfFailed(ZpStatus status)
    {
        if (!status.IsSuccess)
        {
            throw new NativeException(status);
        }
    }

    public void Dispose()
    {
        NativeMethods.Stop();
        certificate?.Dispose();
    }
}

public sealed record SystemInfo(
    int Architecture,
    uint MajorVersion,
    uint MinorVersion,
    uint BuildNumber,
    uint ProcessorCount,
    ulong PhysicalMemoryBytes,
    string ComputerName);

public sealed record EventLogRecord(string Bookmark, string Xml);
public sealed record EventLogPage(
    bool HasMore,
    string NextBookmark,
    EventLogRecord[] Records);
public enum EventLogRetentionMode : ushort
{
    Overwrite,
    Archive,
    Manual
}
public sealed record EventLogChannelInfo(
    bool Enabled,
    uint Type,
    EventLogRetentionMode RetentionMode,
    string MaximumSize,
    string FileSize,
    string CreationTime,
    string LastAccessTime,
    string LastWriteTime,
    string LogFilePath);

public enum ZpStatusType : ushort
{
    None,
    NtStatus,
    Win32,
    Winsock,
    HResult,
    Security,
    Quic,
    ProcessExit,
    ConfigurationManager
}

[StructLayout(LayoutKind.Sequential)]
public readonly record struct ZpStatus(ZpStatusType Type, uint Code)
{
    public bool IsSuccess => Type switch
    {
        ZpStatusType.None => Code == 0,
        ZpStatusType.NtStatus or
        ZpStatusType.HResult or
        ZpStatusType.Security or
        ZpStatusType.Quic => unchecked((int)Code) >= 0,
        ZpStatusType.Win32 or ZpStatusType.Winsock or ZpStatusType.ConfigurationManager => Code == 0,
        ZpStatusType.ProcessExit => true,
        _ => false
    };

    internal static ZpStatus FromNtStatus(int status) =>
        new(status == 0 ? ZpStatusType.None : ZpStatusType.NtStatus,
            unchecked((uint)status));
}

public sealed class NativeException(ZpStatus status) :
    Exception($"ZPigeon {status.Type}: 0x{status.Code:X8}")
{
    public ZpStatus Status { get; } = status;
}

internal static partial class NativeMethods
{
    private const string Library = "KNSoft.ZPigeon.Server.Native.dll";

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void SystemInfoCallback(
        ZpStatus status,
        int architecture,
        uint majorVersion,
        uint minorVersion,
        uint buildNumber,
        uint processorCount,
        ulong physicalMemoryBytes,
        nint computerName,
        uint computerNameLength,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void StatusCallback(ZpStatus status, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void EventLogCallback(
        ZpStatus status,
        [MarshalAs(UnmanagedType.U1)] bool hasMore,
        nint nextBookmark,
        uint nextBookmarkLength,
        nint records,
        uint recordCount,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void EventLogChannelsCallback(
        ZpStatus status,
        nint channels,
        uint channelCount,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void EventLogChannelInfoCallback(
        ZpStatus status,
        [MarshalAs(UnmanagedType.U1)] bool enabled,
        uint type,
        ushort retentionMode,
        ulong maximumSize,
        ulong fileSize,
        ulong creationTime,
        ulong lastAccessTime,
        ulong lastWriteTime,
        nint logFilePath,
        uint logFilePathLength,
        nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct EventLogRecord
    {
        internal readonly nint Bookmark;
        internal readonly uint BookmarkLength;
        internal readonly nint Xml;
        internal readonly uint XmlLength;
    }

    [LibraryImport(Library, EntryPoint = "ZpNative_Start")]
    internal static partial ZpStatus Start(nint certificate, ushort port);

    [LibraryImport(Library, EntryPoint = "ZpNative_Stop")]
    internal static partial int Stop();

    [LibraryImport(Library, EntryPoint = "ZpNative_GetState")]
    internal static partial int GetState();

    [LibraryImport(Library, EntryPoint = "ZpNative_IsClientConnected")]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static partial bool IsClientConnected();

    [LibraryImport(Library, EntryPoint = "ZpNative_GetSystemInfo")]
    internal static partial int GetSystemInfo(SystemInfoCallback callback,
                                               nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryEventLogPage",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryEventLogPage(
        string channelPath,
        uint channelPathLength,
        string? query,
        uint queryLength,
        string? bookmark,
        uint bookmarkLength,
        [MarshalAs(UnmanagedType.U1)] bool forward,
        uint maxEvents,
        EventLogCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateEventLogChannels")]
    internal static partial int EnumerateEventLogChannels(
        EventLogChannelsCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryEventLogChannelInfo",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryEventLogChannelInfo(
        string channelPath,
        uint channelPathLength,
        EventLogChannelInfoCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_SetEventLogChannelEnabled",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int SetEventLogChannelEnabled(
        string channelPath,
        uint channelPathLength,
        [MarshalAs(UnmanagedType.U1)] bool enabled,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ClearEventLog",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ClearEventLog(string channelPath,
                                               uint channelPathLength,
                                               StatusCallback callback,
                                               nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ConfigureEventLogChannel",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ConfigureEventLogChannel(
        string channelPath,
        uint channelPathLength,
        [MarshalAs(UnmanagedType.U1)] bool enabled,
        ushort retentionMode,
        ulong maximumSize,
        StatusCallback callback,
        nint context);
}
