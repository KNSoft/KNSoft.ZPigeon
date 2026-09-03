using System.Globalization;
using System.Net;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using Microsoft.Win32.SafeHandles;

namespace KNSoft.ZPigeon.Server.Managed;

public readonly record struct ConnectionStatistics(
    ulong CompletedRequests,
    ulong FailedRequests,
    ulong SmoothedRequestMilliseconds,
    ulong SentBytes,
    ulong ReceivedBytes,
    ulong SentBitsPerSecond,
    ulong ReceivedBitsPerSecond,
    ulong SentSampleTickCount,
    ulong ReceivedSampleTickCount,
    ulong OutstandingSendBytes,
    ulong MaximumOutstandingSendBytes,
    ulong MaximumSendQueueDelayMilliseconds,
    ulong RejectedSends,
    uint PendingRequests,
    uint ConsecutiveFailures,
    int Transport,
    byte SpeedClass,
    byte LatencyClass);

public sealed partial class NativeServer(string directory) : IDisposable
{
    private const ushort Port = 4433;
    private const int StatusBufferTooSmall = unchecked((int)0xC0000023);
    private const int StatusInvalidDeviceState = unchecked((int)0xC0000184);
    // AsyncLocal keeps concurrent Web requests and the work they start bound to the selected Client.
    private readonly AsyncLocal<ulong> selectedClientId = new();
    private readonly AsyncLocal<CancellationToken> requestCancellation = new();
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
    private EventWaitHandle? clientChangeEvent;
    private RegisteredWaitHandle? clientChangeRegistration;
    private X509Certificate2? certificate;

    public event EventHandler? ClientsChanged;

    public static int State => NativeMethods.GetState();
    public bool ClientConnected => IsClientConnected(ClientId);
    public ulong ClientId => selectedClientId.Value;

    public static bool IsClientConnected(ulong clientId) =>
        clientId != 0 && NativeMethods.IsClientConnected(clientId);

    public unsafe ConnectedClient[] GetClients()
    {
        while (true)
        {
            ThrowIfFailed(NativeMethods.EnumerateClients(null, 0, out var count));
            if (count == 0) return [];
            var clients = new NativeMethods.ClientInfo[count];
            int status;
            fixed (NativeMethods.ClientInfo* pointer = clients)
            {
                status = NativeMethods.EnumerateClients(pointer,
                                                        (uint)clients.Length,
                                                        out count);
            }
            if (status == StatusBufferTooSmall) continue;
            ThrowIfFailed(status);
            var result = new ConnectedClient[count];
            for (var index = 0; index < result.Length; index++)
            {
                fixed (byte* publicKey = clients[index].PublicKey)
                fixed (byte* address = clients[index].Address.Value)
                {
                    result[index] = new(
                        clients[index].ClientId,
                        Convert.ToHexString(SHA256.HashData(
                            new ReadOnlySpan<byte>(publicKey,
                                                   NativeMethods.ClientPublicKeySize))),
                        new IPAddress(new ReadOnlySpan<byte>(
                            address,
                            clients[index].Address.Family == 2 ? 4 : 16)),
                        CreateConnectionStatistics(clients[index].Statistics));
                }
            }
            return result;
        }
    }

    public ClientSelection SelectClient(ulong clientId)
    {
        if (!IsClientConnected(clientId))
        {
            ThrowIfFailed(unchecked((int)0xC000009D));
        }
        return new(selectedClientId, clientId);
    }

    public CancellationSelection SelectCancellation(CancellationToken cancellationToken) =>
        new(requestCancellation, cancellationToken);

    public unsafe IPAddress GetClientAddress()
    {
        Span<byte> address = stackalloc byte[16];
        uint length;
        fixed (byte* pointer = address)
        {
            ThrowIfFailed(NativeMethods.QueryClientAddress(ClientId, pointer, out length));
        }
        return new IPAddress(address[..(int)length]);
    }

    internal static string ReadString(nint value, uint length) =>
        length == 0 ? string.Empty : Marshal.PtrToStringUni(value, (int)length)!;

    public bool TryGetConnectionStatistics(out ConnectionStatistics statistics)
    {
        var status = NativeMethods.QueryConnectionStatistics(ClientId, out var native);
        statistics = CreateConnectionStatistics(native);
        return status >= 0;
    }

    private static ConnectionStatistics CreateConnectionStatistics(
        NativeMethods.ConnectionStatistics native) =>
        new(native.CompletedRequests,
            native.FailedRequests,
            native.SmoothedRequestMilliseconds,
            native.SentBytes,
            native.ReceivedBytes,
            native.SentBitsPerSecond,
            native.ReceivedBitsPerSecond,
            native.SentSampleTickCount,
            native.ReceivedSampleTickCount,
            native.OutstandingSendBytes,
            native.MaximumOutstandingSendBytes,
            native.MaximumSendQueueDelayMilliseconds,
            native.RejectedSends,
            native.PendingRequests,
            native.ConsecutiveFailures,
            native.Transport,
            native.Policy.SpeedClass,
            native.Policy.LatencyClass);

    public void SetConnectionPolicy(byte speedClass, byte latencyClass) =>
        ThrowIfFailed(NativeMethods.SetConnectionPolicy(ClientId, speedClass, latencyClass));

    public Task ProbeConnectionAsync() =>
        RunStatusAsync((callback, context) =>
            NativeMethods.ProbeConnection(ClientId, callback, context));

    public void Start()
    {
        certificate = LoadCertificate(directory);
        ThrowIfFailed(NativeMethods.Start(certificate.Handle, Port));
        var eventHandle = NativeMethods.GetClientChangeEvent();
        if (eventHandle == 0)
        {
            _ = NativeMethods.Stop();
            ThrowIfFailed(StatusInvalidDeviceState);
        }
        clientChangeEvent = new(false, EventResetMode.AutoReset)
        {
            SafeWaitHandle = new SafeWaitHandle(eventHandle, false)
        };
        clientChangeRegistration = ThreadPool.RegisterWaitForSingleObject(
            clientChangeEvent,
            NotifyClientsChanged,
            this,
            Timeout.Infinite,
            false);
    }

    private static void NotifyClientsChanged(object? state, bool _) =>
        ((NativeServer)state!).ClientsChanged?.Invoke(state, EventArgs.Empty);

    public Task<SystemInfo> GetSystemInfoAsync() =>
        RunOperationAsync<SystemInfo>(context =>
            NativeMethods.GetSystemInfo(ClientId, SystemCallback, context));

    public Task<EventLogPage> QueryEventLogPageAsync(
        string channelPath,
        string? query,
        string? bookmark,
        uint maxEvents,
        bool forward = false)
    {
        return RunOperationAsync<EventLogPage>(context =>
            NativeMethods.QueryEventLogPage(ClientId,
                channelPath,
                (uint)channelPath.Length,
                query,
                (uint)(query?.Length ?? 0),
                bookmark,
                (uint)(bookmark?.Length ?? 0),
                forward,
                maxEvents,
                EventLogCallback,
                context));
    }

    public Task<string[]> EnumerateEventLogChannelsAsync() =>
        RunOperationAsync<string[]>(context =>
            NativeMethods.EnumerateEventLogChannels(ClientId, EventLogChannelsCallback, context));

    public Task<EventLogChannelInfo> QueryEventLogChannelInfoAsync(string channelPath) =>
        RunOperationAsync<EventLogChannelInfo>(context =>
            NativeMethods.QueryEventLogChannelInfo(ClientId,
                channelPath,
                (uint)channelPath.Length,
                EventLogChannelInfoCallback,
                context));

    public Task SetEventLogChannelEnabledAsync(string channelPath, bool enabled) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.SetEventLogChannelEnabled(ClientId, channelPath,
                                                     (uint)channelPath.Length,
                                                     enabled,
                                                     callback,
                                                     context));

    public Task ClearEventLogAsync(string channelPath) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.ClearEventLog(ClientId, channelPath,
                                        (uint)channelPath.Length,
                                        callback,
                                        context));

    public Task ConfigureEventLogChannelAsync(
        string channelPath,
        bool enabled,
        EventLogRetentionMode retentionMode,
        ulong maximumSize) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.ConfigureEventLogChannel(ClientId, channelPath,
                                                     (uint)channelPath.Length,
                                                     enabled,
                                                     (byte)retentionMode,
                                                     maximumSize,
                                                     callback,
                                                     context));

    private Task RunStatusAsync(
        Func<NativeMethods.StatusCallback, nint, int> start)
    {
        var completion = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var operation = new StatusOperation(completion);
        operation.Handle = GCHandle.Alloc(operation);
        var context = GCHandle.ToIntPtr(operation.Handle);
        RegisterCancellation(operation, context);
        var status = start(StatusCallback, context);
        if (status < 0)
        {
            operation.Release();
            ThrowIfFailed(status);
        }
        RetryCancellationIfNeeded(operation, context);
        return completion.Task;
    }

    private Task<T> RunOperationAsync<T>(Func<nint, int> start)
    {
        var completion = new TaskCompletionSource<T>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var operation = new Operation<T>(completion);
        operation.Handle = GCHandle.Alloc(operation);
        var context = GCHandle.ToIntPtr(operation.Handle);
        RegisterCancellation(operation, context);
        var status = start(context);
        if (status < 0)
        {
            operation.Release();
            ThrowIfFailed(status);
        }
        RetryCancellationIfNeeded(operation, context);
        return completion.Task;
    }

    private void RegisterCancellation(Operation operation, nint context)
    {
        var cancellationToken = requestCancellation.Value;
        operation.CancellationToken = cancellationToken;
        if (cancellationToken.CanBeCanceled)
        {
            operation.Cancellation = cancellationToken.Register(static value =>
                _ = NativeMethods.CancelOperation((nint)value!), context);
        }
    }

    private static void RetryCancellationIfNeeded(Operation operation, nint context)
    {
        // Cancellation can run before the Native callback context enters its operation table.
        if (operation.CancellationToken.IsCancellationRequested && !operation.Completed)
        {
            _ = NativeMethods.CancelOperation(context);
        }
    }

    private abstract class Operation
    {
        private int released;

        internal GCHandle Handle;
        internal CancellationToken CancellationToken;
        internal CancellationTokenRegistration Cancellation;
        internal abstract bool Completed { get; }

        internal void Release()
        {
            if (Interlocked.Exchange(ref released, 1) != 0) return;
            Cancellation.Dispose();
            Handle.Free();
        }
    }

    private sealed class StatusOperation(TaskCompletionSource completion) : Operation
    {
        internal TaskCompletionSource Completion { get; } = completion;
        internal override bool Completed => Completion.Task.IsCompleted;
    }

    private sealed class Operation<T>(TaskCompletionSource<T> completion) : Operation
    {
        internal TaskCompletionSource<T> Completion { get; } = completion;
        internal override bool Completed => Completion.Task.IsCompleted;
    }

    private static void CompleteSystemInfo(
        ZpStatus status,
        byte architecture,
        uint majorVersion,
        uint minorVersion,
        uint buildNumber,
        uint processorCount,
        ulong physicalMemoryBytes,
        nint computerName,
        uint computerNameLength,
        nint context)
    {
        var completion = GetCompletion<SystemInfo>(context);
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
            ReadString(computerName, computerNameLength)));
    }

    private static void CompleteStatus(ZpStatus status, nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var target = handle.Target;
        TaskCompletionSource completion;

        if (target is StatusOperation operation)
        {
            operation.Release();
            completion = operation.Completion;
        }
        else
        {
            completion = (TaskCompletionSource)target!;
            handle.Free();
        }
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
        var completion = GetCompletion<EventLogPage>(context);
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
                ReadString(record.Bookmark, record.BookmarkLength),
                ReadString(record.Xml, record.XmlLength));
        }
        completion.SetResult(new EventLogPage(
            hasMore,
            ReadString(nextBookmark, nextBookmarkLength),
            result));
    }

    private static void CompleteEventLogChannels(
        ZpStatus status,
        nint channels,
        uint channelCount,
        nint context)
    {
        var completion = GetCompletion<string[]>(context);
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
            result[index] = ReadString(value.Buffer, value.Length);
        }
        completion.SetResult(result);
    }

    private static void CompleteEventLogChannelInfo(
        ZpStatus status,
        bool enabled,
        byte type,
        byte retentionMode,
        ulong maximumSize,
        ulong fileSize,
        ulong creationTime,
        ulong lastAccessTime,
        ulong lastWriteTime,
        nint logFilePath,
        uint logFilePathLength,
        nint context)
    {
        var completion = GetCompletion<EventLogChannelInfo>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        completion.SetResult(new EventLogChannelInfo(
            enabled,
            type,
            (EventLogRetentionMode)retentionMode,
            maximumSize.ToString(CultureInfo.InvariantCulture),
            fileSize.ToString(CultureInfo.InvariantCulture),
            creationTime.ToString(CultureInfo.InvariantCulture),
            lastAccessTime.ToString(CultureInfo.InvariantCulture),
            lastWriteTime.ToString(CultureInfo.InvariantCulture),
            ReadString(logFilePath, logFilePathLength)));
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
                [new("1.3.6.1.5.5.7.3.1")],
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
        if (clientChangeRegistration != null)
        {
            using var completed = new ManualResetEvent(false);
            if (clientChangeRegistration.Unregister(completed)) completed.WaitOne();
            clientChangeRegistration = null;
        }
        clientChangeEvent?.Dispose();
        clientChangeEvent = null;
        try
        {
            ThrowIfFailed(NativeMethods.Stop());
        }
        finally
        {
            certificate?.Dispose();
            certificate = null;
        }
    }

    public readonly struct ClientSelection : IDisposable
    {
        private readonly AsyncLocal<ulong> selection;
        private readonly ulong previousClientId;

        internal ClientSelection(AsyncLocal<ulong> selection, ulong clientId)
        {
            this.selection = selection;
            previousClientId = selection.Value;
            selection.Value = clientId;
        }

        public void Dispose() => selection.Value = previousClientId;
    }

    public readonly struct CancellationSelection : IDisposable
    {
        private readonly AsyncLocal<CancellationToken> selection;
        private readonly CancellationToken previousCancellation;

        internal CancellationSelection(
            AsyncLocal<CancellationToken> selection,
            CancellationToken cancellationToken)
        {
            this.selection = selection;
            previousCancellation = selection.Value;
            selection.Value = cancellationToken;
        }

        public void Dispose() => selection.Value = previousCancellation;
    }
}

public sealed record ConnectedClient(
    ulong Id,
    string Fingerprint,
    IPAddress Address,
    ConnectionStatistics Statistics);

public sealed record SystemInfo(
    byte Architecture,
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
public enum EventLogRetentionMode : byte
{
    Overwrite,
    Archive,
    Manual
}
public sealed record EventLogChannelInfo(
    bool Enabled,
    byte Type,
    EventLogRetentionMode RetentionMode,
    string MaximumSize,
    string FileSize,
    string CreationTime,
    string LastAccessTime,
    string LastWriteTime,
    string LogFilePath);

public enum ZpStatusType : byte
{
    None,
    NtStatus,
    Win32,
    Winsock,
    HResult,
    Security,
    Quic,
    ProcessExit,
    ConfigurationManager,
    Sqlite
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
        ZpStatusType.Win32 or ZpStatusType.Winsock or ZpStatusType.ConfigurationManager or
        ZpStatusType.Sqlite => Code == 0,
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
    internal const int ClientPublicKeySize = 65;

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct ClientInfo
    {
        internal ulong ClientId;
        internal fixed byte PublicKey[ClientPublicKeySize];
        internal IpAddress Address;
        internal ConnectionStatistics Statistics;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct IpAddress
    {
        internal ushort Family;
        internal fixed byte Value[16];
    }

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void SystemInfoCallback(
        ZpStatus status,
        byte architecture,
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
        byte type,
        byte retentionMode,
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

    [LibraryImport(Library, EntryPoint = "ZpNative_GetClientChangeEvent")]
    internal static partial nint GetClientChangeEvent();

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateClients")]
    internal static unsafe partial int EnumerateClients(
        ClientInfo* clients,
        uint capacity,
        out uint count);

    [LibraryImport(Library, EntryPoint = "ZpNative_IsClientConnected")]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static partial bool IsClientConnected(ulong clientId);

    [LibraryImport(Library, EntryPoint = "ZpNative_CancelOperation")]
    internal static partial int CancelOperation(nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_QueryConnectionStatistics")]
    internal static partial int QueryConnectionStatistics(ulong clientId, out ConnectionStatistics statistics);

    [LibraryImport(Library, EntryPoint = "ZpNative_QueryClientAddress")]
    internal static unsafe partial int QueryClientAddress(ulong clientId, byte* address, out uint addressLength);

    [LibraryImport(Library, EntryPoint = "ZpNative_SetConnectionPolicy")]
    internal static partial int SetConnectionPolicy(ulong clientId, byte speedClass, byte latencyClass);

    [LibraryImport(Library, EntryPoint = "ZpNative_ProbeConnection")]
    internal static partial int ProbeConnection(ulong clientId, StatusCallback callback, nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ConnectionStatistics
    {
        internal readonly ulong CompletedRequests;
        internal readonly ulong FailedRequests;
        internal readonly ulong SmoothedRequestMilliseconds;
        internal readonly ulong SentBytes;
        internal readonly ulong ReceivedBytes;
        internal readonly ulong SentBitsPerSecond;
        internal readonly ulong ReceivedBitsPerSecond;
        internal readonly ulong SentSampleTickCount;
        internal readonly ulong ReceivedSampleTickCount;
        internal readonly ulong OutstandingSendBytes;
        internal readonly ulong MaximumOutstandingSendBytes;
        internal readonly ulong MaximumSendQueueDelayMilliseconds;
        internal readonly ulong RejectedSends;
        internal readonly uint PendingRequests;
        internal readonly uint ConsecutiveFailures;
        internal readonly byte Transport;
        internal readonly ConnectionPolicy Policy;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ConnectionPolicy
    {
        private readonly byte value;

        internal byte SpeedClass => (byte)(value & 0x07);
        internal byte LatencyClass => (byte)((value >> 3) & 0x07);
    }

    [LibraryImport(Library, EntryPoint = "ZpNative_GetSystemInfo")]
    internal static partial int GetSystemInfo(ulong clientId, SystemInfoCallback callback,
                                               nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryEventLogPage",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryEventLogPage(
        ulong clientId,
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
        ulong clientId,
        EventLogChannelsCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryEventLogChannelInfo",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryEventLogChannelInfo(
        ulong clientId,
        string channelPath,
        uint channelPathLength,
        EventLogChannelInfoCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_SetEventLogChannelEnabled",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int SetEventLogChannelEnabled(
        ulong clientId,
        string channelPath,
        uint channelPathLength,
        [MarshalAs(UnmanagedType.U1)] bool enabled,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ClearEventLog",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ClearEventLog(ulong clientId, string channelPath,
                                               uint channelPathLength,
                                               StatusCallback callback,
                                               nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ConfigureEventLogChannel",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ConfigureEventLogChannel(
        ulong clientId,
        string channelPath,
        uint channelPathLength,
        [MarshalAs(UnmanagedType.U1)] bool enabled,
        byte retentionMode,
        ulong maximumSize,
        StatusCallback callback,
        nint context);
}
