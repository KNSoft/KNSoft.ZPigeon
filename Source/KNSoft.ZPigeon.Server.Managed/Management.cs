using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer
{
    private static readonly NativeMethods.FilePageCallback FilePageCallback = CompleteFilePage;
    private static readonly NativeMethods.FileInfoCallback FileInfoCallback = CompleteFileInfo;
    private static readonly NativeMethods.FileHashCallback FileHashCallback = CompleteFileHash;
    private static readonly NativeMethods.ProcessListCallback ProcessListCallback = CompleteProcessList;
    private static readonly NativeMethods.ProcessInfoCallback ProcessInfoCallback = CompleteProcessInfo;
    private static readonly NativeMethods.ServiceListCallback ServiceListCallback = CompleteServiceList;
    private static readonly NativeMethods.ServiceInfoCallback ServiceInfoCallback = CompleteServiceInfo;

    public Task<FilePage> EnumerateFilesPageAsync(string path, string? cursor, uint maxEntries) =>
        RunManagementAsync<FilePage>((context) => NativeMethods.EnumerateFilesPage(
            path,
            (uint)path.Length,
            cursor,
            (uint)(cursor?.Length ?? 0),
            maxEntries,
            FilePageCallback,
            context));

    public Task<FileInfo> QueryFileAsync(string path) =>
        RunManagementAsync<FileInfo>((context) => NativeMethods.QueryFile(
            path,
            (uint)path.Length,
            FileInfoCallback,
            context));

    public Task<FileHash> HashFileAsync(string path) =>
        RunManagementAsync<FileHash>((context) => NativeMethods.HashFile(
            path,
            (uint)path.Length,
            FileHashCallback,
            context));

    public Task DeleteFileAsync(string path) =>
        RunStatusAsync((callback, context) => NativeMethods.DeleteFile(
            path,
            (uint)path.Length,
            callback,
            context));

    public Task RenameFileAsync(string path, string newPath) =>
        RunStatusAsync((callback, context) => NativeMethods.RenameFile(
            path,
            (uint)path.Length,
            newPath,
            (uint)newPath.Length,
            callback,
            context));

    public Task<ProcessRecord[]> EnumerateProcessesAsync() =>
        RunManagementAsync<ProcessRecord[]>((context) => NativeMethods.EnumerateProcesses(ProcessListCallback, context));

    public Task<ProcessInfo> QueryProcessAsync(uint processId, ulong createTime) =>
        RunManagementAsync<ProcessInfo>((context) =>
            NativeMethods.QueryProcess(processId, createTime, ProcessInfoCallback, context));

    public Task<ServiceRecord[]> EnumerateServicesAsync() =>
        RunManagementAsync<ServiceRecord[]>((context) => NativeMethods.EnumerateServices(ServiceListCallback, context));

    public Task<ServiceInfo> QueryServiceAsync(string serviceName) =>
        RunManagementAsync<ServiceInfo>((context) => NativeMethods.QueryService(
            serviceName,
            (uint)serviceName.Length,
            ServiceInfoCallback,
            context));

    public Task SetServiceRunningAsync(string serviceName, bool running) =>
        RunStatusAsync((callback, context) => NativeMethods.ControlService(
            running,
            serviceName,
            (uint)serviceName.Length,
            callback,
            context));

    private static Task<T> RunManagementAsync<T>(Func<nint, int> start)
    {
        var completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        var status = start(GCHandle.ToIntPtr(handle));
        if (status < 0)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    private static TaskCompletionSource<T> GetCompletion<T>(nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var completion = (TaskCompletionSource<T>)handle.Target!;
        handle.Free();
        return completion;
    }

    private static void CompleteFilePage(
        ZpStatus status,
        nint nextCursor,
        uint nextCursorLength,
        nint records,
        uint recordCount,
        nint context)
    {
        var completion = GetCompletion<FilePage>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new FileRecord[recordCount];
        var size = Marshal.SizeOf<NativeMethods.FileRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.FileRecord>(records + index * size);
            result[index] = new FileRecord(
                Marshal.PtrToStringUni(record.Name, (int)record.NameLength) ?? string.Empty,
                record.Attributes,
                record.Size,
                FileTime(record.CreationTime),
                FileTime(record.LastAccessTime),
                FileTime(record.LastWriteTime));
        }
        completion.SetResult(new FilePage(
            Marshal.PtrToStringUni(nextCursor, (int)nextCursorLength),
            result));
    }

    private static void CompleteFileInfo(
        ZpStatus status,
        uint attributes,
        ulong size,
        ulong creationTime,
        ulong lastAccessTime,
        ulong lastWriteTime,
        nint context)
    {
        var completion = GetCompletion<FileInfo>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        completion.SetResult(new FileInfo(attributes,
                                          size,
                                          FileTime(creationTime),
                                          FileTime(lastAccessTime),
                                          FileTime(lastWriteTime)));
    }

    private static void CompleteFileHash(
        ZpStatus status,
        ulong fileSize,
        nint digest,
        uint digestLength,
        nint context)
    {
        var completion = GetCompletion<FileHash>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var bytes = new byte[digestLength];
        Marshal.Copy(digest, bytes, 0, bytes.Length);
        completion.SetResult(new FileHash(fileSize, Convert.ToHexString(bytes)));
    }

    private static void CompleteProcessList(
        ZpStatus status,
        nint records,
        uint recordCount,
        nint context)
    {
        var completion = GetCompletion<ProcessRecord[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new ProcessRecord[recordCount];
        var size = Marshal.SizeOf<NativeMethods.ProcessRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.ProcessRecord>(records + index * size);
            result[index] = new ProcessRecord(
                record.ProcessId,
                record.ParentProcessId,
                record.SessionId,
                record.ThreadCount,
                record.HandleCount,
                record.CreateTime,
                record.UserTime,
                record.KernelTime,
                record.WorkingSetBytes,
                record.PrivateBytes,
                Marshal.PtrToStringUni(record.ImageName, (int)record.ImageNameLength) ?? string.Empty);
        }
        completion.SetResult(result);
    }

    private static void CompleteProcessInfo(ZpStatus status, nint info, nint context)
    {
        var completion = GetCompletion<ProcessInfo>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var value = Marshal.PtrToStructure<NativeMethods.ProcessInfo>(info);
        completion.SetResult(new ProcessInfo(
            value.ProcessId,
            value.ParentProcessId,
            value.SessionId,
            value.ThreadCount,
            value.HandleCount,
            FileTime(value.CreateTime),
            value.UserTime,
            value.KernelTime,
            value.WorkingSetBytes,
            value.PrivateBytes,
            String(value.ImageName),
            value.ImagePathStatus,
            String(value.ImagePath),
            value.CommandLineStatus,
            String(value.CommandLine)));
    }

    private static void CompleteServiceList(
        ZpStatus status,
        nint records,
        uint recordCount,
        nint context)
    {
        var completion = GetCompletion<ServiceRecord[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new ServiceRecord[recordCount];
        var size = Marshal.SizeOf<NativeMethods.ServiceRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.ServiceRecord>(records + index * size);
            result[index] = new ServiceRecord(
                record.ServiceType,
                record.CurrentState,
                record.ProcessId,
                Marshal.PtrToStringUni(record.ServiceName, (int)record.ServiceNameLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.DisplayName, (int)record.DisplayNameLength) ?? string.Empty);
        }
        completion.SetResult(result);
    }

    private static void CompleteServiceInfo(ZpStatus status, nint info, nint context)
    {
        var completion = GetCompletion<ServiceInfo>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var value = Marshal.PtrToStructure<NativeMethods.ServiceInfo>(info);
        completion.SetResult(new ServiceInfo(
            value.ServiceType,
            value.CurrentState,
            value.ProcessId,
            value.StartType,
            value.ErrorControl,
            String(value.ServiceName),
            String(value.DisplayName),
            String(value.BinaryPathName),
            String(value.StartName)));
    }

    private static DateTime FileTime(ulong value) => DateTime.FromFileTimeUtc((long)value);

    private static string String(NativeMethods.StringView value) =>
        Marshal.PtrToStringUni(value.Buffer, (int)value.Length) ?? string.Empty;
}

public sealed record FileRecord(
    string Name,
    uint Attributes,
    ulong Size,
    DateTime CreationTime,
    DateTime LastAccessTime,
    DateTime LastWriteTime);
public sealed record FilePage(string? NextCursor, FileRecord[] Records);
public sealed record FileInfo(
    uint Attributes,
    ulong Size,
    DateTime CreationTime,
    DateTime LastAccessTime,
    DateTime LastWriteTime);
public sealed record FileHash(ulong FileSize, string Sha256);
public sealed record ProcessRecord(
    uint ProcessId,
    uint ParentProcessId,
    uint SessionId,
    uint ThreadCount,
    uint HandleCount,
    ulong CreateTime,
    ulong UserTime,
    ulong KernelTime,
    ulong WorkingSetBytes,
    ulong PrivateBytes,
    string ImageName);
public sealed record ProcessInfo(
    uint ProcessId,
    uint ParentProcessId,
    uint SessionId,
    uint ThreadCount,
    uint HandleCount,
    DateTime CreateTime,
    ulong UserTime,
    ulong KernelTime,
    ulong WorkingSetBytes,
    ulong PrivateBytes,
    string ImageName,
    int ImagePathStatus,
    string ImagePath,
    int CommandLineStatus,
    string CommandLine);
public sealed record ServiceRecord(
    uint ServiceType,
    uint CurrentState,
    uint ProcessId,
    string ServiceName,
    string DisplayName);
public sealed record ServiceInfo(
    uint ServiceType,
    uint CurrentState,
    uint ProcessId,
    uint StartType,
    uint ErrorControl,
    string ServiceName,
    string DisplayName,
    string BinaryPathName,
    string StartName);

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void FilePageCallback(
        ZpStatus status,
        nint nextCursor,
        uint nextCursorLength,
        nint records,
        uint recordCount,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void FileInfoCallback(
        ZpStatus status,
        uint attributes,
        ulong size,
        ulong creationTime,
        ulong lastAccessTime,
        ulong lastWriteTime,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void FileHashCallback(
        ZpStatus status,
        ulong fileSize,
        nint digest,
        uint digestLength,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ProcessListCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ProcessInfoCallback(ZpStatus status, nint info, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ServiceListCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ServiceInfoCallback(ZpStatus status, nint info, nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct StringView
    {
        internal readonly nint Buffer;
        internal readonly uint Length;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct FileRecord
    {
        internal readonly uint Attributes;
        internal readonly ulong Size;
        internal readonly ulong CreationTime;
        internal readonly ulong LastAccessTime;
        internal readonly ulong LastWriteTime;
        internal readonly nint Name;
        internal readonly uint NameLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ProcessRecord
    {
        internal readonly uint ProcessId;
        internal readonly uint ParentProcessId;
        internal readonly uint SessionId;
        internal readonly uint ThreadCount;
        internal readonly uint HandleCount;
        internal readonly ulong CreateTime;
        internal readonly ulong UserTime;
        internal readonly ulong KernelTime;
        internal readonly ulong WorkingSetBytes;
        internal readonly ulong PrivateBytes;
        internal readonly nint ImageName;
        internal readonly uint ImageNameLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ProcessInfo
    {
        internal readonly uint ProcessId;
        internal readonly uint ParentProcessId;
        internal readonly uint SessionId;
        internal readonly uint ThreadCount;
        internal readonly uint HandleCount;
        internal readonly ulong CreateTime;
        internal readonly ulong UserTime;
        internal readonly ulong KernelTime;
        internal readonly ulong WorkingSetBytes;
        internal readonly ulong PrivateBytes;
        internal readonly StringView ImageName;
        internal readonly int ImagePathStatus;
        internal readonly StringView ImagePath;
        internal readonly int CommandLineStatus;
        internal readonly StringView CommandLine;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ServiceRecord
    {
        internal readonly uint ServiceType;
        internal readonly uint CurrentState;
        internal readonly uint ProcessId;
        internal readonly nint ServiceName;
        internal readonly uint ServiceNameLength;
        internal readonly nint DisplayName;
        internal readonly uint DisplayNameLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ServiceInfo
    {
        internal readonly uint ServiceType;
        internal readonly uint CurrentState;
        internal readonly uint ProcessId;
        internal readonly uint StartType;
        internal readonly uint ErrorControl;
        internal readonly StringView ServiceName;
        internal readonly StringView DisplayName;
        internal readonly StringView BinaryPathName;
        internal readonly StringView StartName;
    }

    [LibraryImport(Library,
        EntryPoint = "ZpNative_EnumerateFilesPage",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int EnumerateFilesPage(
        string path,
        uint pathLength,
        string? cursor,
        uint cursorLength,
        uint maxEntries,
        FilePageCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryFile",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryFile(string path, uint pathLength, FileInfoCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_HashFile",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int HashFile(string path, uint pathLength, FileHashCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_DeleteFile",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int DeleteFile(string path, uint pathLength, StatusCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_RenameFile",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int RenameFile(
        string path,
        uint pathLength,
        string newPath,
        uint newPathLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateProcesses")]
    internal static partial int EnumerateProcesses(ProcessListCallback callback, nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_QueryProcess")]
    internal static partial int QueryProcess(
        uint processId,
        ulong createTime,
        ProcessInfoCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateServices")]
    internal static partial int EnumerateServices(ServiceListCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryService",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryService(
        string serviceName,
        uint serviceNameLength,
        ServiceInfoCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ControlService",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ControlService(
        [MarshalAs(UnmanagedType.U1)] bool start,
        string serviceName,
        uint serviceNameLength,
        StatusCallback callback,
        nint context);
}
