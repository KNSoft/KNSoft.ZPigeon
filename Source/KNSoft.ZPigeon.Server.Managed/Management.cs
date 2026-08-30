using System.Globalization;
using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer
{
    private static readonly NativeMethods.FilePageCallback FilePageCallback = CompleteFilePage;
    private static readonly NativeMethods.FileInfoCallback FileInfoCallback = CompleteFileInfo;
    private static readonly NativeMethods.FileHashCallback FileHashCallback = CompleteFileHash;
    private static readonly NativeMethods.FileVolumeCallback FileVolumeCallback = CompleteFileVolume;
    private static readonly NativeMethods.FileOwnersCallback FileOwnersCallback = CompleteFileOwners;
    private static readonly NativeMethods.FileOwnerControlCallback FileOwnerControlCallback = CompleteFileOwnerControl;
    private static readonly NativeMethods.FileDownloadsCallback FileDownloadsCallback = CompleteFileDownloads;
    private static readonly NativeMethods.StringCallback StringCallback = CompleteString;
    private static readonly NativeMethods.SecurityDescriptorCallback SecurityDescriptorCallback =
        CompleteSecurityDescriptor;
    private static readonly NativeMethods.ProcessListCallback ProcessListCallback = CompleteProcessList;
    private static readonly NativeMethods.ProcessInfoCallback ProcessInfoCallback = CompleteProcessInfo;
    private static readonly NativeMethods.ProcessModulesCallback ProcessModulesCallback = CompleteProcessModules;
    private static readonly NativeMethods.ProcessHandlesCallback ProcessHandlesCallback = CompleteProcessHandles;
    private static readonly NativeMethods.ProcessDumpCallback ProcessDumpCallback = CompleteProcessDump;
    private static readonly NativeMethods.ProcessMemoryCallback ProcessMemoryCallback = CompleteProcessMemory;
    private static readonly NativeMethods.ProcessMemoryAllocationsCallback ProcessMemoryAllocationsCallback =
        CompleteProcessMemoryAllocations;
    private static readonly NativeMethods.ProcessMemoryRegionsCallback ProcessMemoryRegionsCallback =
        CompleteProcessMemoryRegions;
    private static readonly NativeMethods.WindowListCallback WindowListCallback = CompleteWindowList;
    private static readonly NativeMethods.WindowMonitorsCallback WindowMonitorsCallback = CompleteWindowMonitors;
    private static readonly NativeMethods.WindowInfoCallback WindowInfoCallback = CompleteWindowInfo;
    private static readonly NativeMethods.WindowCaptureCallback WindowCaptureCallback = CompleteWindowCapture;
    private static readonly NativeMethods.ServiceListCallback ServiceListCallback = CompleteServiceList;
    private static readonly NativeMethods.ServiceInfoCallback ServiceInfoCallback = CompleteServiceInfo;

    public Task<FilePage> EnumerateFilesPageAsync(string? path, uint enumerationId) =>
        RunManagementAsync<FilePage>((context) => NativeMethods.EnumerateFilesPage(ClientId,
            path,
            (uint)(path?.Length ?? 0),
            enumerationId,
            FilePageCallback,
            context));

    public Task<FilePage> EnumerateFilteredFilesPageAsync(
        string? path,
        string? filter,
        char group,
        uint enumerationId) =>
        RunManagementAsync<FilePage>(context => NativeMethods.EnumerateFilteredFilesPage(ClientId,
            path,
            (uint)(path?.Length ?? 0),
            filter,
            (uint)(filter?.Length ?? 0),
            group,
            enumerationId,
            FilePageCallback,
            context));

    public Task CloseFileEnumerationAsync(uint enumerationId) =>
        RunStatusAsync((callback, context) => NativeMethods.CloseFileEnumeration(ClientId,
            enumerationId,
            callback,
            context));

    public Task<FilePage> EnumerateArchivePageAsync(string? path, uint enumerationId) =>
        RunManagementAsync<FilePage>((context) => NativeMethods.EnumerateArchivePage(ClientId,
            path,
            (uint)(path?.Length ?? 0),
            enumerationId,
            FilePageCallback,
            context));

    public Task<string> QueryShortcutAsync(string path) =>
        RunManagementAsync<string>((context) => NativeMethods.QueryShortcut(ClientId,
            path,
            (uint)path.Length,
            StringCallback,
            context));

    public Task<byte[]> PreviewImageAsync(string path, FileImagePreviewQuality quality) =>
        RunManagementAsync<byte[]>((context) => NativeMethods.PreviewImage(ClientId,
            path,
            (uint)path.Length,
            quality,
            BinaryDataCallback,
            context));

    public Task<FileInfo> QueryFileAsync(string path) =>
        RunManagementAsync<FileInfo>((context) => NativeMethods.QueryFile(ClientId,
            path,
            (uint)path.Length,
            FileInfoCallback,
            context));

    public Task<SecurityDescriptor> QueryFileSecurityAsync(string path) =>
        RunManagementAsync<SecurityDescriptor>(context => NativeMethods.QueryFileSecurity(ClientId,
            path,
            (uint)path.Length,
            SecurityDescriptorCallback,
            context));

    public Task SetFileSecurityAsync(string path, string sddl, bool daclProtected) =>
        RunStatusAsync((callback, context) => NativeMethods.SetFileSecurity(ClientId,
            path,
            (uint)path.Length,
            sddl,
            (uint)sddl.Length,
            daclProtected,
            callback,
            context));

    public Task<string> ResolveAccountNameAsync(string name) =>
        RunManagementAsync<string>(context => NativeMethods.ResolveAccountName(ClientId,
            name,
            (uint)name.Length,
            StringCallback,
            context));

    public Task<string> ResolveAccountSidAsync(string sid) =>
        RunManagementAsync<string>(context => NativeMethods.ResolveAccountSid(ClientId,
            sid,
            (uint)sid.Length,
            StringCallback,
            context));

    public Task<FileHash> HashFileAsync(string path, FileHashAlgorithm algorithm) =>
        RunManagementAsync<FileHash>((context) => NativeMethods.HashFile(ClientId,
            path,
            (uint)path.Length,
            algorithm,
            FileHashCallback,
            context));

    public Task DeleteFileAsync(string path) =>
        RunStatusAsync((callback, context) => NativeMethods.DeleteFile(ClientId,
            path,
            (uint)path.Length,
            callback,
            context));

    public Task RenameFileAsync(string path, string newPath) =>
        RunStatusAsync((callback, context) => NativeMethods.RenameFile(ClientId,
            path,
            (uint)path.Length,
            newPath,
            (uint)newPath.Length,
            callback,
            context));

    public Task SetFileAttributesAsync(string path, uint attributes) =>
        RunStatusAsync((callback, context) => NativeMethods.SetFileAttributes(ClientId,
            path,
            (uint)path.Length,
            attributes,
            callback,
            context));

    public Task<FileOwnerRecord[]> QueryFileOwnersAsync(string path) =>
        RunManagementAsync<FileOwnerRecord[]>(context => NativeMethods.QueryFileOwners(ClientId,
            path,
            (uint)path.Length,
            FileOwnersCallback,
            context));

    public unsafe Task<FileOwnerControlResult[]> ControlFileOwnersAsync(
        string path,
        FileOwnerControl control,
        uint[] processIds)
    {
        fixed (uint* pointer = processIds)
        {
            var data = (nint)pointer;
            return RunOperationAsync<FileOwnerControlResult[]>(context =>
                NativeMethods.ControlFileOwners(ClientId, path,
                                                (uint)path.Length,
                                                control,
                                                data,
                                                (uint)processIds.Length,
                                                FileOwnerControlCallback,
                                                context));
        }
    }

    public unsafe Task WriteFileRangeAsync(string path, ulong offset, byte[] data)
    {
        fixed (byte* pointer = data)
        {
            var buffer = (nint)pointer;
            return RunStatusAsync((callback, context) =>
                NativeMethods.WriteFileRange(ClientId,
                    path,
                    (uint)path.Length,
                    offset,
                    buffer,
                    (uint)data.Length,
                    callback,
                    context));
        }
    }

    public Task<FileVolumeInfo> QueryFileVolumeAsync(string path) =>
        RunManagementAsync<FileVolumeInfo>(context => NativeMethods.QueryFileVolume(ClientId,
            path,
            (uint)path.Length,
            FileVolumeCallback,
            context));

    public Task SetFileVolumeLabelAsync(string path, string label) =>
        RunStatusAsync((callback, context) => NativeMethods.SetFileVolumeLabel(ClientId,
            path,
            (uint)path.Length,
            label,
            (uint)label.Length,
            callback,
            context));

    public Task StartFileDownloadAsync(
        Guid id,
        string url,
        string path,
        FileDownloadEngine engine,
        bool overwrite)
    {
        var value = id.ToString("D");
        return RunStatusAsync((callback, context) => NativeMethods.StartFileDownload(ClientId,
            engine,
            overwrite ? (byte)1 : (byte)0,
            value,
            (uint)value.Length,
            url,
            (uint)url.Length,
            path,
            (uint)path.Length,
            callback,
            context));
    }

    public Task<FileDownloadRecord[]> EnumerateFileDownloadsAsync() =>
        RunManagementAsync<FileDownloadRecord[]>(context =>
            NativeMethods.EnumerateFileDownloads(ClientId, FileDownloadsCallback, context));

    public Task CancelFileDownloadAsync(Guid id)
    {
        var value = id.ToString("D");
        return RunStatusAsync((callback, context) => NativeMethods.CancelFileDownload(ClientId,
            value,
            (uint)value.Length,
            callback,
            context));
    }

    public Task<ProcessRecord[]> EnumerateProcessesAsync() =>
        RunManagementAsync<ProcessRecord[]>((context) =>
            NativeMethods.EnumerateProcesses(ClientId, ProcessListCallback, context));

    public Task<ProcessInfo> QueryProcessAsync(uint processId, ulong createTime) =>
        RunManagementAsync<ProcessInfo>((context) =>
            NativeMethods.QueryProcess(ClientId, processId, createTime, ProcessInfoCallback, context));

    public Task<ProcessModuleList> EnumerateProcessModulesAsync(uint processId, ulong createTime) =>
        RunManagementAsync<ProcessModuleList>(context => NativeMethods.EnumerateProcessModules(ClientId,
            processId,
            createTime,
            ProcessModulesCallback,
            context));

    public Task<ProcessHandle[]> EnumerateProcessHandlesAsync(uint processId, ulong createTime) =>
        RunManagementAsync<ProcessHandle[]>(context => NativeMethods.EnumerateProcessHandles(ClientId,
            processId,
            createTime,
            ProcessHandlesCallback,
            context));

    public Task ControlProcessAsync(uint processId, ulong createTime, ProcessControl control, uint value = 0) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.ControlProcess(ClientId, processId, createTime, control, value, callback, context));

    public Task<string> CreateProcessDumpAsync(uint processId, ulong createTime, uint dumpType) =>
        RunManagementAsync<string>((context) =>
            NativeMethods.CreateProcessDump(ClientId, processId, createTime, dumpType, ProcessDumpCallback, context));

    public Task<byte[]> ReadProcessMemoryAsync(uint processId, ulong createTime, ulong address, uint length) =>
        RunManagementAsync<byte[]>(context => NativeMethods.ReadProcessMemory(ClientId,
            processId,
            createTime,
            address,
            length,
            ProcessMemoryCallback,
            context));

    public unsafe Task WriteProcessMemoryAsync(uint processId, ulong createTime, ulong address, byte[] data)
    {
        fixed (byte* pointer = data)
        {
            var buffer = (nint)pointer;
            return RunStatusAsync((callback, context) =>
                NativeMethods.WriteProcessMemory(ClientId, processId,
                                                 createTime,
                                                 address,
                                                 buffer,
                                                 (uint)data.Length,
                                                 callback,
                                                 context));
        }
    }

    public Task<ProcessMemoryMap> QueryProcessMemoryMapAsync(uint processId, ulong createTime) =>
        RunManagementAsync<ProcessMemoryMap>(context => NativeMethods.QueryProcessMemoryMap(ClientId,
            processId,
            createTime,
            ProcessMemoryAllocationsCallback,
            context));

    public Task<ProcessMemoryRegion[]> QueryProcessMemoryRegionsAsync(uint snapshotId, uint allocationIndex) =>
        RunManagementAsync<ProcessMemoryRegion[]>(context => NativeMethods.QueryProcessMemoryRegions(ClientId,
            snapshotId,
            allocationIndex,
            ProcessMemoryRegionsCallback,
            context));

    public Task CloseProcessMemoryMapAsync(uint snapshotId) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.CloseProcessMemoryMap(ClientId, snapshotId, callback, context));

    public Task<WindowRecord[]> EnumerateWindowsAsync() =>
        RunManagementAsync<WindowRecord[]>((context) =>
            NativeMethods.EnumerateWindows(ClientId, WindowListCallback, context));

    public Task<WindowMonitor[]> EnumerateMonitorsAsync() =>
        RunManagementAsync<WindowMonitor[]>((context) =>
            NativeMethods.EnumerateMonitors(ClientId, WindowMonitorsCallback, context));

    public Task<WindowInfo> QueryWindowAsync(ulong handle, uint processId, uint threadId) =>
        RunManagementAsync<WindowInfo>((context) =>
            NativeMethods.QueryWindow(ClientId, handle, processId, threadId, WindowInfoCallback, context));

    public Task ControlWindowAsync(
        ulong handle,
        uint processId,
        uint threadId,
        WindowControl control) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.ControlWindow(ClientId, handle, processId, threadId, control, callback, context));

    public Task UpdateWindowAsync(
        ulong handle,
        uint processId,
        uint threadId,
        WindowUpdate update) =>
        RunStatusAsync((callback, context) => NativeMethods.UpdateWindow(ClientId,
            handle,
            processId,
            threadId,
            update.Fields,
            update.Caption,
            (uint)update.Caption.Length,
            update.Left,
            update.Top,
            update.Right,
            update.Bottom,
            update.Style,
            update.ExStyle,
            callback,
            context));

    public Task<byte[]> CaptureWindowAsync(
        ulong handle,
        uint processId,
        uint threadId,
        WindowCaptureOptions options)
    {
        ValidateWindowCaptureOptions(options);
        return RunManagementAsync<byte[]>((context) =>
            NativeMethods.CaptureWindow(ClientId, handle,
                                        processId,
                                        threadId,
                                        options.Flags,
                                        options.MaxDimension,
                                        options.FrameRate,
                                        options.ImageQuality,
                                        options.MonitorIndex,
                                        WindowCaptureCallback,
                                        context));
    }

    public Task<ServiceRecord[]> EnumerateServicesAsync() =>
        RunManagementAsync<ServiceRecord[]>((context) =>
            NativeMethods.EnumerateServices(ClientId, ServiceListCallback, context));

    public Task<ServiceInfo> QueryServiceAsync(string serviceName) =>
        RunManagementAsync<ServiceInfo>((context) => NativeMethods.QueryService(ClientId,
            serviceName,
            (uint)serviceName.Length,
            ServiceInfoCallback,
            context));

    public Task ControlServiceAsync(string serviceName, ServiceControl control, string? argument = null) =>
        RunStatusAsync((callback, context) => NativeMethods.ControlService(ClientId,
            (byte)control,
            serviceName,
            (uint)serviceName.Length,
            argument,
            (uint)(argument?.Length ?? 0),
            callback,
            context));

    public Task ConfigureServiceAsync(ServiceConfig config) =>
        RunStatusAsync((callback, context) => NativeMethods.ConfigureService(ClientId,
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
        RunStatusAsync((callback, context) => NativeMethods.ConfigureServiceRecovery(ClientId,
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
        RunStatusAsync((callback, context) => NativeMethods.ConfigureServiceAccount(ClientId,
            config.ServiceName,
            (uint)config.ServiceName.Length,
            config.StartName,
            (uint)config.StartName.Length,
            config.Password,
            (uint)(config.Password?.Length ?? 0),
            config.Password is not null,
            callback,
            context));

    private Task<T> RunManagementAsync<T>(Func<nint, int> start) => RunOperationAsync<T>(start);

    private static TaskCompletionSource<T> GetCompletion<T>(nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var target = handle.Target;

        if (target is Operation<T> operation)
        {
            operation.Release();
            return operation.Completion;
        }
        handle.Free();
        return (TaskCompletionSource<T>)target!;
    }

    private static void CompleteFilePage(
        ZpStatus status,
        uint enumerationId,
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
                ReadString(record.Name, record.NameLength),
                record.Attributes,
                record.Size,
                FileTime(record.CreationTime),
                FileTime(record.LastAccessTime),
                FileTime(record.LastWriteTime),
                record.HasChildren);
        }
        completion.SetResult(new FilePage(
            enumerationId == 0 ? null : enumerationId.ToString(CultureInfo.InvariantCulture),
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
        completion.SetResult(ReadString(value, valueLength));
    }

    private static void CompleteSecurityDescriptor(
        ZpStatus status,
        nint sddl,
        uint sddlLength,
        bool daclProtected,
        nint context)
    {
        var completion = GetCompletion<SecurityDescriptor>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        completion.SetResult(new SecurityDescriptor(ReadString(sddl, sddlLength), daclProtected));
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
            ReadString(label, labelLength),
            ReadString(fileSystem, fileSystemLength)));
    }

    private static void CompleteFileOwners(ZpStatus status, nint records, uint recordCount, nint context)
    {
        var completion = GetCompletion<FileOwnerRecord[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new FileOwnerRecord[recordCount];
        var size = Marshal.SizeOf<NativeMethods.FileOwnerRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.FileOwnerRecord>(records + index * size);
            result[index] = new FileOwnerRecord(
                record.ProcessId,
                record.ImagePathStatus,
                record.CommandLineStatus,
                ReadString(record.ImageName, record.ImageNameLength),
                ReadString(record.ImagePath, record.ImagePathLength),
                ReadString(record.CommandLine, record.CommandLineLength),
                ReadString(record.ServiceNames, record.ServiceNamesLength)
                    .Split('\0', StringSplitOptions.RemoveEmptyEntries));
        }
        completion.SetResult(result);
    }

    private static void CompleteFileOwnerControl(
        ZpStatus status,
        nint results,
        uint resultCount,
        nint context)
    {
        var completion = GetCompletion<FileOwnerControlResult[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var values = new FileOwnerControlResult[resultCount];
        var size = Marshal.SizeOf<NativeMethods.FileOwnerControlResult>();
        for (var index = 0; index < values.Length; index++)
        {
            var result = Marshal.PtrToStructure<NativeMethods.FileOwnerControlResult>(results + index * size);
            values[index] = new FileOwnerControlResult(result.ProcessId, result.Status, result.AffectedHandleCount);
        }
        completion.SetResult(values);
    }

    private static void CompleteFileDownloads(ZpStatus status, nint records, uint recordCount, nint context)
    {
        var completion = GetCompletion<FileDownloadRecord[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var values = new FileDownloadRecord[recordCount];
        var size = Marshal.SizeOf<NativeMethods.FileDownloadRecord>();
        for (var index = 0; index < values.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.FileDownloadRecord>(records + index * size);
            values[index] = new FileDownloadRecord(
                ReadString(record.Id, record.IdLength),
                ReadString(record.Url, record.UrlLength),
                ReadString(record.Path, record.PathLength),
                ReadString(record.ErrorText, record.ErrorTextLength),
                record.Engine,
                record.State,
                record.Result,
                record.TransferredBytes,
                record.TotalBytes);
        }
        completion.SetResult(values);
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
                ReadString(record.ImageName, record.ImageNameLength),
                ReadString(record.UserName, record.UserNameLength),
                ReadString(record.ImagePath, record.ImagePathLength),
                ReadString(record.ServiceNames, record.ServiceNamesLength)
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
            value.ImageBaseStatus,
            value.ImageBase,
            String(value.ImageName),
            String(value.UserName),
            value.ImagePathStatus,
            String(value.ImagePath),
            value.CommandLineStatus,
            String(value.CommandLine)));
    }

    private static void CompleteProcessModules(
        ZpStatus status,
        ushort machineType,
        byte machineBits,
        nint modules,
        uint moduleCount,
        nint context)
    {
        var completion = GetCompletion<ProcessModuleList>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new ProcessModule[moduleCount];
        var size = Marshal.SizeOf<NativeMethods.ProcessModule>();
        for (var index = 0; index < result.Length; index++)
        {
            var module = Marshal.PtrToStructure<NativeMethods.ProcessModule>(modules + index * size);
            result[index] = new(index == 0,
                                module.BaseAddress.ToString(CultureInfo.InvariantCulture),
                                module.EntryPoint.ToString(CultureInfo.InvariantCulture),
                                module.SizeOfImage,
                                module.LoadReason,
                                module.LoadTime == 0 ? null : FileTime(module.LoadTime),
                                ReadString(module.Path, module.PathLength));
        }
        completion.SetResult(new(machineType, machineBits, result));
    }

    private static void CompleteProcessHandles(ZpStatus status, nint handles, uint handleCount, nint context)
    {
        var completion = GetCompletion<ProcessHandle[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new ProcessHandle[handleCount];
        var size = Marshal.SizeOf<NativeMethods.ProcessHandle>();
        for (var index = 0; index < result.Length; index++)
        {
            var handle = Marshal.PtrToStructure<NativeMethods.ProcessHandle>(handles + index * size);
            result[index] = new(handle.HandleValue.ToString(CultureInfo.InvariantCulture),
                                ReadString(handle.TypeName, handle.TypeNameLength),
                                ReadString(handle.ObjectName, handle.ObjectNameLength));
        }
        completion.SetResult(result);
    }

    private static void CompleteProcessDump(ZpStatus status, nint path, uint pathLength, nint context)
    {
        var completion = GetCompletion<string>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        completion.SetResult(ReadString(path, pathLength));
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
                record.Handle.ToString(CultureInfo.InvariantCulture),
                record.ParentHandle.ToString(CultureInfo.InvariantCulture),
                record.ProcessId,
                record.ThreadId,
                record.Style,
                record.ExStyle,
                record.Flags,
                ReadString(record.Caption, record.CaptionLength),
                ReadString(record.ClassName, record.ClassNameLength));
        }
        completion.SetResult(result);
    }

    private static void CompleteWindowMonitors(
        ZpStatus status,
        nint monitors,
        uint monitorCount,
        nint context)
    {
        var completion = GetCompletion<WindowMonitor[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new WindowMonitor[monitorCount];
        var size = Marshal.SizeOf<NativeMethods.WindowMonitor>();
        for (var index = 0; index < result.Length; index++)
        {
            var monitor = Marshal.PtrToStructure<NativeMethods.WindowMonitor>(monitors + index * size);
            result[index] = new WindowMonitor(
                monitor.Index,
                (monitor.Flags & 1) != 0,
                monitor.Left,
                monitor.Top,
                monitor.Right,
                monitor.Bottom,
                monitor.WorkLeft,
                monitor.WorkTop,
                monitor.WorkRight,
                monitor.WorkBottom,
                ReadString(monitor.Device, monitor.DeviceLength));
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
            value.Handle.ToString(CultureInfo.InvariantCulture),
            value.ParentHandle.ToString(CultureInfo.InvariantCulture),
            value.OwnerHandle.ToString(CultureInfo.InvariantCulture),
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
            value.CreatorVersion,
            value.PreviousHandle.ToString(CultureInfo.InvariantCulture),
            value.NextHandle.ToString(CultureInfo.InvariantCulture),
            value.FirstChildHandle.ToString(CultureInfo.InvariantCulture),
            value.FirstSiblingHandle.ToString(CultureInfo.InvariantCulture),
            value.LastSiblingHandle.ToString(CultureInfo.InvariantCulture),
            value.MonitorLeft,
            value.MonitorTop,
            value.MonitorRight,
            value.MonitorBottom,
            String(value.MonitorDevice)));
    }

    private static void CompleteWindowCapture(
        ZpStatus status,
        nint bitmap,
        uint bitmapLength,
        nint context)
    {
        var completion = GetCompletion<byte[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new byte[bitmapLength];
        Marshal.Copy(bitmap, result, 0, result.Length);
        completion.SetResult(result);
    }

    private static void CompleteProcessMemory(ZpStatus status, nint data, uint dataLength, nint context)
    {
        var completion = GetCompletion<byte[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new byte[dataLength];
        Marshal.Copy(data, result, 0, result.Length);
        completion.SetResult(result);
    }

    private static void CompleteProcessMemoryAllocations(
        ZpStatus status,
        uint snapshotId,
        nint allocations,
        uint allocationCount,
        nint context)
    {
        var completion = GetCompletion<ProcessMemoryMap>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new ProcessMemoryAllocation[allocationCount];
        var size = Marshal.SizeOf<NativeMethods.ProcessMemoryAllocation>();
        for (var index = 0; index < result.Length; index++)
        {
            var allocation = Marshal.PtrToStructure<NativeMethods.ProcessMemoryAllocation>(
                allocations + index * size);
            result[index] = new(allocation.AllocationBase.ToString(CultureInfo.InvariantCulture),
                                allocation.RegionSize.ToString(CultureInfo.InvariantCulture),
                                allocation.CommitSize.ToString(CultureInfo.InvariantCulture),
                                allocation.WorkingSetBytes.ToString(CultureInfo.InvariantCulture),
                                allocation.PrivateWorkingSetBytes.ToString(CultureInfo.InvariantCulture),
                                allocation.SharedWorkingSetBytes.ToString(CultureInfo.InvariantCulture),
                                allocation.ShareableWorkingSetBytes.ToString(CultureInfo.InvariantCulture),
                                allocation.LockedWorkingSetBytes.ToString(CultureInfo.InvariantCulture),
                                allocation.SharedOriginalBytes.ToString(CultureInfo.InvariantCulture),
                                allocation.Type,
                                allocation.AllocationProtect,
                                allocation.RegionType,
                                allocation.Priority,
                                allocation.RegionCount,
                                allocation.RegionStatus,
                                allocation.WorkingSetStatus,
                                allocation.MappedPathStatus,
                                ReadString(allocation.MappedPath, allocation.MappedPathLength));
        }
        completion.SetResult(new(snapshotId, result));
    }

    private static void CompleteProcessMemoryRegions(
        ZpStatus status,
        nint regions,
        uint regionCount,
        nint context)
    {
        var completion = GetCompletion<ProcessMemoryRegion[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new ProcessMemoryRegion[regionCount];
        var size = Marshal.SizeOf<NativeMethods.ProcessMemoryRegion>();
        for (var index = 0; index < result.Length; index++)
        {
            var region = Marshal.PtrToStructure<NativeMethods.ProcessMemoryRegion>(regions + index * size);
            result[index] = new(region.BaseAddress.ToString(CultureInfo.InvariantCulture),
                                region.RegionSize.ToString(CultureInfo.InvariantCulture),
                                region.CommitSize.ToString(CultureInfo.InvariantCulture),
                                region.WorkingSetBytes.ToString(CultureInfo.InvariantCulture),
                                region.PrivateWorkingSetBytes.ToString(CultureInfo.InvariantCulture),
                                region.SharedWorkingSetBytes.ToString(CultureInfo.InvariantCulture),
                                region.ShareableWorkingSetBytes.ToString(CultureInfo.InvariantCulture),
                                region.LockedWorkingSetBytes.ToString(CultureInfo.InvariantCulture),
                                region.SharedOriginalBytes.ToString(CultureInfo.InvariantCulture),
                                region.State,
                                region.Protect,
                                region.Priority,
                                region.WorkingSetStatus);
        }
        completion.SetResult(result);
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
                ReadString(record.ServiceName, record.ServiceNameLength),
                ReadString(record.DisplayName, record.DisplayNameLength),
                ReadString(record.Description, record.DescriptionLength),
                ReadString(record.StartName, record.StartNameLength));
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
        ReadString(value.Buffer, value.Length);

    private static string[] Strings(NativeMethods.StringView value) =>
        String(value).Split('\0', StringSplitOptions.RemoveEmptyEntries);
}
