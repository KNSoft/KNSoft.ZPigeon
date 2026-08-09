using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

namespace KNSoft.ZPigeon.Web;

internal sealed class NativeServer(string directory) : IDisposable
{
    private const int Success = 0;
    private const ushort Port = 4433;
    private static readonly NativeMethods.SystemInfoCallback SystemCallback =
        CompleteSystemInfo;
    private static readonly NativeMethods.StatusCallback StatusCallback =
        CompleteStatus;
    private static readonly NativeMethods.EventLogCallback EventLogCallback =
        CompleteEventLog;
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
        if (status < Success)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    public Task TerminateProcessAsync(uint processId) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.TerminateProcess(processId, callback, context));

    public Task<EventLogPage> QueryEventLogPageAsync(
        string channelPath,
        string? query,
        string? bookmark,
        uint maxEvents)
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
            maxEvents,
            EventLogCallback,
            GCHandle.ToIntPtr(handle));
        if (status < Success)
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

    private static Task RunStatusAsync(
        Func<NativeMethods.StatusCallback, nint, int> start)
    {
        var completion = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        var status = start(StatusCallback, GCHandle.ToIntPtr(handle));
        if (status < Success)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    private static void CompleteSystemInfo(
        int status,
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
        if (status < Success)
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

    private static void CompleteStatus(int status, nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var completion = (TaskCompletionSource)handle.Target!;
        handle.Free();
        if (status < Success)
        {
            completion.SetException(new NativeException(status));
        }
        else
        {
            completion.SetResult();
        }
    }

    private static void CompleteEventLog(
        int status,
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
        if (status < Success)
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

    private static void ThrowIfFailed(int status)
    {
        if (status < Success)
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

internal sealed record SystemInfo(
    int Architecture,
    uint MajorVersion,
    uint MinorVersion,
    uint BuildNumber,
    uint ProcessorCount,
    ulong PhysicalMemoryBytes,
    string ComputerName);

internal sealed record EventLogRecord(string Bookmark, string Xml);
internal sealed record EventLogPage(
    bool HasMore,
    string NextBookmark,
    EventLogRecord[] Records);

internal sealed class NativeException(int status) :
    Exception($"ZPigeon NTSTATUS: 0x{status:X8}");

internal static partial class NativeMethods
{
    private const string Library = "KNSoft.ZPigeon.Server.Native.dll";

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void SystemInfoCallback(
        int status,
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
    internal delegate void StatusCallback(int status, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void EventLogCallback(
        int status,
        [MarshalAs(UnmanagedType.U1)] bool hasMore,
        nint nextBookmark,
        uint nextBookmarkLength,
        nint records,
        uint recordCount,
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
    internal static partial int Start(nint certificate, ushort port);

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

    [LibraryImport(Library, EntryPoint = "ZpNative_TerminateProcess")]
    internal static partial int TerminateProcess(uint processId,
                                                  StatusCallback callback,
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
        uint maxEvents,
        EventLogCallback callback,
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
}
