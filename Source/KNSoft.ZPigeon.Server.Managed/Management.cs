using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer
{
    private static readonly NativeMethods.FilePageCallback FilePageCallback = CompleteFilePage;
    private static readonly NativeMethods.FileInfoCallback FileInfoCallback = CompleteFileInfo;
    private static readonly NativeMethods.FileHashCallback FileHashCallback = CompleteFileHash;
    private static readonly NativeMethods.FileVolumeCallback FileVolumeCallback = CompleteFileVolume;
    private static readonly NativeMethods.StringCallback StringCallback = CompleteString;
    private static readonly NativeMethods.ProcessListCallback ProcessListCallback = CompleteProcessList;
    private static readonly NativeMethods.ProcessInfoCallback ProcessInfoCallback = CompleteProcessInfo;
    private static readonly NativeMethods.ProcessDumpCallback ProcessDumpCallback = CompleteProcessDump;
    private static readonly NativeMethods.WindowListCallback WindowListCallback = CompleteWindowList;
    private static readonly NativeMethods.WindowInfoCallback WindowInfoCallback = CompleteWindowInfo;
    private static readonly NativeMethods.ServiceListCallback ServiceListCallback = CompleteServiceList;
    private static readonly NativeMethods.ServiceInfoCallback ServiceInfoCallback = CompleteServiceInfo;

    public Task<FilePage> EnumerateFilesPageAsync(string? path, ulong enumerationId) =>
        RunManagementAsync<FilePage>((context) => NativeMethods.EnumerateFilesPage(
            path,
            (uint)(path?.Length ?? 0),
            enumerationId,
            FilePageCallback,
            context));

    public Task<FileInfo> QueryFileAsync(string path) =>
        RunManagementAsync<FileInfo>((context) => NativeMethods.QueryFile(
            path,
            (uint)path.Length,
            FileInfoCallback,
            context));

    public Task<string> QueryFileSecurityAsync(string path) =>
        RunManagementAsync<string>(context => NativeMethods.QueryFileSecurity(
            path,
            (uint)path.Length,
            StringCallback,
            context));

    public Task SetFileSecurityAsync(string path, string sddl) =>
        RunStatusAsync((callback, context) => NativeMethods.SetFileSecurity(
            path,
            (uint)path.Length,
            sddl,
            (uint)sddl.Length,
            callback,
            context));

    public Task<string> ResolveAccountNameAsync(string name) =>
        RunManagementAsync<string>(context => NativeMethods.ResolveAccountName(
            name,
            (uint)name.Length,
            StringCallback,
            context));

    public Task<string> ResolveAccountSidAsync(string sid) =>
        RunManagementAsync<string>(context => NativeMethods.ResolveAccountSid(
            sid,
            (uint)sid.Length,
            StringCallback,
            context));

    public Task<FileHash> HashFileAsync(string path, FileHashAlgorithm algorithm) =>
        RunManagementAsync<FileHash>((context) => NativeMethods.HashFile(
            path,
            (uint)path.Length,
            algorithm,
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

    public Task SetFileAttributesAsync(string path, uint attributes) =>
        RunStatusAsync((callback, context) => NativeMethods.SetFileAttributes(
            path,
            (uint)path.Length,
            attributes,
            callback,
            context));

    public Task<FileVolumeInfo> QueryFileVolumeAsync(string path) =>
        RunManagementAsync<FileVolumeInfo>(context => NativeMethods.QueryFileVolume(
            path,
            (uint)path.Length,
            FileVolumeCallback,
            context));

    public Task SetFileVolumeLabelAsync(string path, string label) =>
        RunStatusAsync((callback, context) => NativeMethods.SetFileVolumeLabel(
            path,
            (uint)path.Length,
            label,
            (uint)label.Length,
            callback,
            context));

    public Task<ProcessRecord[]> EnumerateProcessesAsync() =>
        RunManagementAsync<ProcessRecord[]>((context) =>
            NativeMethods.EnumerateProcesses(ProcessListCallback, context));

    public Task<ProcessInfo> QueryProcessAsync(uint processId, ulong createTime) =>
        RunManagementAsync<ProcessInfo>((context) =>
            NativeMethods.QueryProcess(processId, createTime, ProcessInfoCallback, context));

    public Task ControlProcessAsync(uint processId, ulong createTime, ProcessControl control, uint value = 0) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.ControlProcess(processId, createTime, control, value, callback, context));

    public Task<string> CreateProcessDumpAsync(uint processId, ulong createTime, uint dumpType) =>
        RunManagementAsync<string>((context) =>
            NativeMethods.CreateProcessDump(processId, createTime, dumpType, ProcessDumpCallback, context));

    public Task<WindowRecord[]> EnumerateWindowsAsync() =>
        RunManagementAsync<WindowRecord[]>((context) =>
            NativeMethods.EnumerateWindows(WindowListCallback, context));

    public Task<WindowInfo> QueryWindowAsync(ulong handle, uint processId, uint threadId) =>
        RunManagementAsync<WindowInfo>((context) =>
            NativeMethods.QueryWindow(handle, processId, threadId, WindowInfoCallback, context));

    public Task ControlWindowAsync(
        ulong handle,
        uint processId,
        uint threadId,
        WindowControl control) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.ControlWindow(handle, processId, threadId, control, callback, context));

    public Task<ServiceRecord[]> EnumerateServicesAsync() =>
        RunManagementAsync<ServiceRecord[]>((context) => NativeMethods.EnumerateServices(ServiceListCallback, context));

    public Task<ServiceInfo> QueryServiceAsync(string serviceName) =>
        RunManagementAsync<ServiceInfo>((context) => NativeMethods.QueryService(
            serviceName,
            (uint)serviceName.Length,
            ServiceInfoCallback,
            context));

    public Task ControlServiceAsync(string serviceName, ServiceControl control, string? argument = null) =>
        RunStatusAsync((callback, context) => NativeMethods.ControlService(
            (uint)control,
            serviceName,
            (uint)serviceName.Length,
            argument,
            (uint)(argument?.Length ?? 0),
            callback,
            context));

    public Task ConfigureServiceAsync(ServiceConfig config) =>
        RunStatusAsync((callback, context) => NativeMethods.ConfigureService(
            config.ServiceName,
            (uint)config.ServiceName.Length,
            config.StartType,
            config.DelayedAutoStart,
            config.DisplayName,
            (uint)config.DisplayName.Length,
            config.Description,
            (uint)config.Description.Length,
            config.BinaryPathName,
            (uint)config.BinaryPathName.Length,
            config.LoadOrderGroup,
            (uint)config.LoadOrderGroup.Length,
            callback,
            context));

    public Task ConfigureServiceRecoveryAsync(ServiceRecoveryConfig config) =>
        RunStatusAsync((callback, context) => NativeMethods.ConfigureServiceRecovery(
            config.ServiceName,
            (uint)config.ServiceName.Length,
            config.ErrorControl,
            config.FailureActionsOnNonCrashFailures,
            config.ResetPeriodSeconds,
            config.RestartDelayMilliseconds,
            config.RebootDelayMilliseconds,
            config.FirstFailureAction,
            config.SecondFailureAction,
            config.ThirdFailureAction,
            config.SubsequentFailureAction,
            config.RebootMessage,
            (uint)config.RebootMessage.Length,
            config.Command,
            (uint)config.Command.Length,
            callback,
            context));

    public Task ConfigureServiceAccountAsync(ServiceAccountConfig config) =>
        RunStatusAsync((callback, context) => NativeMethods.ConfigureServiceAccount(
            config.ServiceName,
            (uint)config.ServiceName.Length,
            config.StartName,
            (uint)config.StartName.Length,
            config.Password,
            (uint)(config.Password?.Length ?? 0),
            config.Password is not null,
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
        ulong enumerationId,
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
                FileTime(record.LastWriteTime),
                record.HasChildren);
        }
        completion.SetResult(new FilePage(
            enumerationId == 0 ? null : enumerationId.ToString(),
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

    private static void CompleteString(ZpStatus status, nint value, uint valueLength, nint context)
    {
        var completion = GetCompletion<string>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        completion.SetResult(Marshal.PtrToStringUni(value, (int)valueLength) ?? string.Empty);
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

    private static void CompleteFileVolume(
        ZpStatus status,
        ulong totalBytes,
        ulong freeBytes,
        uint serialNumber,
        uint maximumComponentLength,
        uint fileSystemFlags,
        nint label,
        uint labelLength,
        nint fileSystem,
        uint fileSystemLength,
        nint context)
    {
        var completion = GetCompletion<FileVolumeInfo>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        completion.SetResult(new FileVolumeInfo(
            totalBytes,
            freeBytes,
            serialNumber,
            maximumComponentLength,
            fileSystemFlags,
            Marshal.PtrToStringUni(label, (int)labelLength) ?? string.Empty,
            Marshal.PtrToStringUni(fileSystem, (int)fileSystemLength) ?? string.Empty));
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
                record.Flags,
                record.MachineType,
                record.PriorityClass,
                record.CreateTime,
                record.UserTime,
                record.KernelTime,
                record.WorkingSetBytes,
                record.PrivateBytes,
                Marshal.PtrToStringUni(record.ImageName, (int)record.ImageNameLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.UserName, (int)record.UserNameLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.ImagePath, (int)record.ImagePathLength) ?? string.Empty,
                (Marshal.PtrToStringUni(record.ServiceNames, (int)record.ServiceNamesLength) ?? string.Empty)
                    .Split('\0', StringSplitOptions.RemoveEmptyEntries));
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
            value.Flags,
            value.MachineType,
            value.PriorityClass,
            FileTime(value.CreateTime),
            value.UserTime,
            value.KernelTime,
            value.WorkingSetBytes,
            value.PrivateBytes,
            String(value.ImageName),
            String(value.UserName),
            value.ImagePathStatus,
            String(value.ImagePath),
            value.CommandLineStatus,
            String(value.CommandLine)));
    }

    private static void CompleteProcessDump(ZpStatus status, nint path, uint pathLength, nint context)
    {
        var completion = GetCompletion<string>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        completion.SetResult(Marshal.PtrToStringUni(path, (int)pathLength) ?? string.Empty);
    }

    private static void CompleteWindowList(
        ZpStatus status,
        nint records,
        uint recordCount,
        nint context)
    {
        var completion = GetCompletion<WindowRecord[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new WindowRecord[recordCount];
        var size = Marshal.SizeOf<NativeMethods.WindowRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.WindowRecord>(records + index * size);
            result[index] = new WindowRecord(
                record.Handle.ToString(),
                record.ParentHandle.ToString(),
                record.ProcessId,
                record.ThreadId,
                record.Style,
                record.ExStyle,
                record.Flags,
                Marshal.PtrToStringUni(record.Caption, (int)record.CaptionLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.ClassName, (int)record.ClassNameLength) ?? string.Empty);
        }
        completion.SetResult(result);
    }

    private static void CompleteWindowInfo(ZpStatus status, nint info, nint context)
    {
        var completion = GetCompletion<WindowInfo>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var value = Marshal.PtrToStructure<NativeMethods.WindowInfo>(info);
        completion.SetResult(new WindowInfo(
            value.Handle.ToString(),
            value.ParentHandle.ToString(),
            value.OwnerHandle.ToString(),
            value.ProcessId,
            value.ThreadId,
            value.Style,
            value.ExStyle,
            value.Flags,
            String(value.Caption),
            String(value.ClassName),
            value.WindowLeft,
            value.WindowTop,
            value.WindowRight,
            value.WindowBottom,
            value.ClientLeft,
            value.ClientTop,
            value.ClientRight,
            value.ClientBottom,
            value.WindowStatus,
            value.BorderWidth,
            value.BorderHeight,
            value.ClassAtom,
            value.CreatorVersion));
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
                record.ControlsAccepted,
                record.ProcessId,
                record.StartType,
                Marshal.PtrToStringUni(record.ServiceName, (int)record.ServiceNameLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.DisplayName, (int)record.DisplayNameLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.Description, (int)record.DescriptionLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.StartName, (int)record.StartNameLength) ?? string.Empty);
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
            value.ControlsAccepted,
            value.ProcessId,
            value.StartType,
            value.ErrorControl,
            value.DelayedAutoStart != 0,
            value.ServiceFlags,
            value.RecoverySupported != 0,
            value.FailureActionsOnNonCrashFailures != 0,
            value.RecoveryActionCount,
            value.ResetPeriodSeconds,
            value.RestartDelayMilliseconds,
            value.RebootDelayMilliseconds,
            value.FirstFailureAction,
            value.SecondFailureAction,
            value.ThirdFailureAction,
            value.SubsequentFailureAction,
            String(value.ServiceName),
            String(value.DisplayName),
            String(value.Description),
            String(value.BinaryPathName),
            String(value.StartName),
            String(value.LoadOrderGroup),
            Strings(value.Dependencies),
            Strings(value.Dependents),
            String(value.ServiceDll),
            String(value.RebootMessage),
            String(value.RecoveryCommand)));
    }

    private static DateTime FileTime(ulong value) => DateTime.FromFileTimeUtc((long)value);

    private static string String(NativeMethods.StringView value) =>
        Marshal.PtrToStringUni(value.Buffer, (int)value.Length) ?? string.Empty;

    private static string[] Strings(NativeMethods.StringView value) =>
        String(value).Split('\0', StringSplitOptions.RemoveEmptyEntries);
}

public sealed record FileRecord(
    string Name,
    uint Attributes,
    ulong Size,
    DateTime CreationTime,
    DateTime LastAccessTime,
    DateTime LastWriteTime,
    bool HasChildren);
public sealed record FilePage(string? EnumerationId, FileRecord[] Records);
public sealed record FileInfo(
    uint Attributes,
    ulong Size,
    DateTime CreationTime,
    DateTime LastAccessTime,
    DateTime LastWriteTime);
public sealed record FileHash(ulong FileSize, string Value);
public sealed record FileVolumeInfo(
    ulong TotalBytes,
    ulong FreeBytes,
    uint SerialNumber,
    uint MaximumComponentLength,
    uint FileSystemFlags,
    string Label,
    string FileSystem);
public enum FileHashAlgorithm
{
    Crc32 = 1,
    Md5 = 2,
    Sha1 = 3,
    Sha256 = 4
}
public sealed record ProcessRecord(
    uint ProcessId,
    uint ParentProcessId,
    uint SessionId,
    uint ThreadCount,
    uint HandleCount,
    uint Flags,
    ushort MachineType,
    ushort PriorityClass,
    ulong CreateTime,
    ulong UserTime,
    ulong KernelTime,
    ulong WorkingSetBytes,
    ulong PrivateBytes,
    string ImageName,
    string UserName,
    string ImagePath,
    string[] ServiceNames);
public sealed record ProcessInfo(
    uint ProcessId,
    uint ParentProcessId,
    uint SessionId,
    uint ThreadCount,
    uint HandleCount,
    uint Flags,
    ushort MachineType,
    ushort PriorityClass,
    DateTime CreateTime,
    ulong UserTime,
    ulong KernelTime,
    ulong WorkingSetBytes,
    ulong PrivateBytes,
    string ImageName,
    string UserName,
    int ImagePathStatus,
    string ImagePath,
    int CommandLineStatus,
    string CommandLine);
public enum ProcessControl : ushort
{
    Terminate = 1,
    TerminateTree,
    Suspend,
    Resume,
    EfficiencyMode,
    Priority,
    UacVirtualization
}
public sealed record WindowRecord(
    string Handle,
    string ParentHandle,
    uint ProcessId,
    uint ThreadId,
    uint Style,
    uint ExStyle,
    uint Flags,
    string Caption,
    string ClassName);
public sealed record WindowInfo(
    string Handle,
    string ParentHandle,
    string OwnerHandle,
    uint ProcessId,
    uint ThreadId,
    uint Style,
    uint ExStyle,
    uint Flags,
    string Caption,
    string ClassName,
    int WindowLeft,
    int WindowTop,
    int WindowRight,
    int WindowBottom,
    int ClientLeft,
    int ClientTop,
    int ClientRight,
    int ClientBottom,
    uint WindowStatus,
    uint BorderWidth,
    uint BorderHeight,
    ushort ClassAtom,
    ushort CreatorVersion);
public enum WindowControl : ushort
{
    Show = 1,
    Hide = 2,
    Minimize = 3,
    Maximize = 4,
    Restore = 5,
    Foreground = 6,
    Close = 7
}
public sealed record ServiceRecord(
    uint ServiceType,
    uint CurrentState,
    uint ControlsAccepted,
    uint ProcessId,
    uint StartType,
    string ServiceName,
    string DisplayName,
    string Description,
    string StartName);
public sealed record ServiceInfo(
    uint ServiceType,
    uint CurrentState,
    uint ControlsAccepted,
    uint ProcessId,
    uint StartType,
    uint ErrorControl,
    bool DelayedAutoStart,
    uint ServiceFlags,
    bool RecoverySupported,
    bool FailureActionsOnNonCrashFailures,
    uint RecoveryActionCount,
    uint ResetPeriodSeconds,
    uint RestartDelayMilliseconds,
    uint RebootDelayMilliseconds,
    uint FirstFailureAction,
    uint SecondFailureAction,
    uint ThirdFailureAction,
    uint SubsequentFailureAction,
    string ServiceName,
    string DisplayName,
    string Description,
    string BinaryPathName,
    string StartName,
    string LoadOrderGroup,
    string[] Dependencies,
    string[] Dependents,
    string ServiceDll,
    string RebootMessage,
    string RecoveryCommand);
public sealed record ServiceConfig(
    string ServiceName,
    uint StartType,
    bool DelayedAutoStart,
    string DisplayName,
    string Description,
    string BinaryPathName,
    string LoadOrderGroup);
public sealed record ServiceRecoveryConfig(
    string ServiceName,
    uint ErrorControl,
    bool FailureActionsOnNonCrashFailures,
    uint ResetPeriodSeconds,
    uint RestartDelayMilliseconds,
    uint RebootDelayMilliseconds,
    uint FirstFailureAction,
    uint SecondFailureAction,
    uint ThirdFailureAction,
    uint SubsequentFailureAction,
    string RebootMessage,
    string Command);
public sealed record ServiceAccountConfig(string ServiceName, string StartName, string? Password);
public enum ServiceControl
{
    Start = 1,
    Stop,
    Pause,
    Continue,
    Restart
}

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void FilePageCallback(
        ZpStatus status,
        ulong enumerationId,
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
    internal delegate void FileVolumeCallback(
        ZpStatus status,
        ulong totalBytes,
        ulong freeBytes,
        uint serialNumber,
        uint maximumComponentLength,
        uint fileSystemFlags,
        nint label,
        uint labelLength,
        nint fileSystem,
        uint fileSystemLength,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void StringCallback(ZpStatus status, nint value, uint valueLength, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ProcessListCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ProcessInfoCallback(ZpStatus status, nint info, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ProcessDumpCallback(ZpStatus status, nint path, uint pathLength, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void WindowListCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void WindowInfoCallback(ZpStatus status, nint info, nint context);

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
        [MarshalAs(UnmanagedType.U1)]
        internal readonly bool HasChildren;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ProcessRecord
    {
        internal readonly uint ProcessId;
        internal readonly uint ParentProcessId;
        internal readonly uint SessionId;
        internal readonly uint ThreadCount;
        internal readonly uint HandleCount;
        internal readonly uint Flags;
        internal readonly ushort MachineType;
        internal readonly ushort PriorityClass;
        internal readonly ulong CreateTime;
        internal readonly ulong UserTime;
        internal readonly ulong KernelTime;
        internal readonly ulong WorkingSetBytes;
        internal readonly ulong PrivateBytes;
        internal readonly nint ImageName;
        internal readonly uint ImageNameLength;
        internal readonly nint UserName;
        internal readonly uint UserNameLength;
        internal readonly nint ImagePath;
        internal readonly uint ImagePathLength;
        internal readonly nint ServiceNames;
        internal readonly uint ServiceNamesLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ProcessInfo
    {
        internal readonly uint ProcessId;
        internal readonly uint ParentProcessId;
        internal readonly uint SessionId;
        internal readonly uint ThreadCount;
        internal readonly uint HandleCount;
        internal readonly uint Flags;
        internal readonly ushort MachineType;
        internal readonly ushort PriorityClass;
        internal readonly ulong CreateTime;
        internal readonly ulong UserTime;
        internal readonly ulong KernelTime;
        internal readonly ulong WorkingSetBytes;
        internal readonly ulong PrivateBytes;
        internal readonly StringView ImageName;
        internal readonly StringView UserName;
        internal readonly int ImagePathStatus;
        internal readonly StringView ImagePath;
        internal readonly int CommandLineStatus;
        internal readonly StringView CommandLine;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct WindowRecord
    {
        internal readonly ulong Handle;
        internal readonly ulong ParentHandle;
        internal readonly uint ProcessId;
        internal readonly uint ThreadId;
        internal readonly uint Style;
        internal readonly uint ExStyle;
        internal readonly uint Flags;
        internal readonly nint Caption;
        internal readonly uint CaptionLength;
        internal readonly nint ClassName;
        internal readonly uint ClassNameLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct WindowInfo
    {
        internal readonly ulong Handle;
        internal readonly ulong ParentHandle;
        internal readonly uint ProcessId;
        internal readonly uint ThreadId;
        internal readonly uint Style;
        internal readonly uint ExStyle;
        internal readonly uint Flags;
        internal readonly StringView Caption;
        internal readonly StringView ClassName;
        internal readonly ulong OwnerHandle;
        internal readonly int WindowLeft;
        internal readonly int WindowTop;
        internal readonly int WindowRight;
        internal readonly int WindowBottom;
        internal readonly int ClientLeft;
        internal readonly int ClientTop;
        internal readonly int ClientRight;
        internal readonly int ClientBottom;
        internal readonly uint WindowStatus;
        internal readonly uint BorderWidth;
        internal readonly uint BorderHeight;
        internal readonly ushort ClassAtom;
        internal readonly ushort CreatorVersion;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ServiceRecord
    {
        internal readonly uint ServiceType;
        internal readonly uint CurrentState;
        internal readonly uint ControlsAccepted;
        internal readonly uint ProcessId;
        internal readonly uint StartType;
        internal readonly nint ServiceName;
        internal readonly uint ServiceNameLength;
        internal readonly nint DisplayName;
        internal readonly uint DisplayNameLength;
        internal readonly nint Description;
        internal readonly uint DescriptionLength;
        internal readonly nint StartName;
        internal readonly uint StartNameLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ServiceInfo
    {
        internal readonly uint ServiceType;
        internal readonly uint CurrentState;
        internal readonly uint ControlsAccepted;
        internal readonly uint ProcessId;
        internal readonly uint StartType;
        internal readonly uint ErrorControl;
        internal readonly uint DelayedAutoStart;
        internal readonly uint ServiceFlags;
        internal readonly uint RecoverySupported;
        internal readonly uint FailureActionsOnNonCrashFailures;
        internal readonly uint RecoveryActionCount;
        internal readonly uint ResetPeriodSeconds;
        internal readonly uint RestartDelayMilliseconds;
        internal readonly uint RebootDelayMilliseconds;
        internal readonly uint FirstFailureAction;
        internal readonly uint SecondFailureAction;
        internal readonly uint ThirdFailureAction;
        internal readonly uint SubsequentFailureAction;
        internal readonly StringView ServiceName;
        internal readonly StringView DisplayName;
        internal readonly StringView Description;
        internal readonly StringView BinaryPathName;
        internal readonly StringView StartName;
        internal readonly StringView LoadOrderGroup;
        internal readonly StringView Dependencies;
        internal readonly StringView Dependents;
        internal readonly StringView ServiceDll;
        internal readonly StringView RebootMessage;
        internal readonly StringView RecoveryCommand;
    }

    [LibraryImport(Library,
        EntryPoint = "ZpNative_EnumerateFilesPage",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int EnumerateFilesPage(
        string? path,
        uint pathLength,
        ulong enumerationId,
        FilePageCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryFile",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryFile(string path, uint pathLength, FileInfoCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryFileSecurity",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryFileSecurity(
        string path,
        uint pathLength,
        StringCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_SetFileSecurity",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int SetFileSecurity(
        string path,
        uint pathLength,
        string sddl,
        uint sddlLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ResolveAccountName",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ResolveAccountName(
        string name,
        uint nameLength,
        StringCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ResolveAccountSid",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ResolveAccountSid(
        string sid,
        uint sidLength,
        StringCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryFileVolume",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryFileVolume(
        string path,
        uint pathLength,
        FileVolumeCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_SetFileVolumeLabel",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int SetFileVolumeLabel(
        string path,
        uint pathLength,
        string label,
        uint labelLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_HashFile",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int HashFile(
        string path,
        uint pathLength,
        FileHashAlgorithm algorithm,
        FileHashCallback callback,
        nint context);

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

    [LibraryImport(Library,
        EntryPoint = "ZpNative_SetFileAttributes",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int SetFileAttributes(
        string path,
        uint pathLength,
        uint attributes,
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

    [LibraryImport(Library, EntryPoint = "ZpNative_ControlProcess")]
    internal static partial int ControlProcess(
        uint processId,
        ulong createTime,
        ProcessControl control,
        uint value,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CreateProcessDump")]
    internal static partial int CreateProcessDump(
        uint processId,
        ulong createTime,
        uint dumpType,
        ProcessDumpCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateWindows")]
    internal static partial int EnumerateWindows(WindowListCallback callback, nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_QueryWindow")]
    internal static partial int QueryWindow(
        ulong handle,
        uint processId,
        uint threadId,
        WindowInfoCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_ControlWindow")]
    internal static partial int ControlWindow(
        ulong handle,
        uint processId,
        uint threadId,
        WindowControl control,
        StatusCallback callback,
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
        uint control,
        string serviceName,
        uint serviceNameLength,
        string? argument,
        uint argumentLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ConfigureService",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ConfigureService(
        string serviceName,
        uint serviceNameLength,
        uint startType,
        [MarshalAs(UnmanagedType.U1)] bool delayedAutoStart,
        string displayName,
        uint displayNameLength,
        string description,
        uint descriptionLength,
        string binaryPathName,
        uint binaryPathNameLength,
        string loadOrderGroup,
        uint loadOrderGroupLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ConfigureServiceRecovery",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ConfigureServiceRecovery(
        string serviceName,
        uint serviceNameLength,
        uint errorControl,
        [MarshalAs(UnmanagedType.U1)] bool failureActionsOnNonCrashFailures,
        uint resetPeriodSeconds,
        uint restartDelayMilliseconds,
        uint rebootDelayMilliseconds,
        uint firstFailureAction,
        uint secondFailureAction,
        uint thirdFailureAction,
        uint subsequentFailureAction,
        string rebootMessage,
        uint rebootMessageLength,
        string command,
        uint commandLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ConfigureServiceAccount",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ConfigureServiceAccount(
        string serviceName,
        uint serviceNameLength,
        string startName,
        uint startNameLength,
        string? password,
        uint passwordLength,
        [MarshalAs(UnmanagedType.U1)] bool passwordPresent,
        StatusCallback callback,
        nint context);
}
