using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void FilePageCallback(
        ZpStatus status,
        uint enumerationId,
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
    internal delegate void FileOwnersCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void FileOwnerControlCallback(ZpStatus status, nint results, uint resultCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void FileDownloadsCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void StringCallback(ZpStatus status, nint value, uint valueLength, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void SecurityDescriptorCallback(
        ZpStatus status,
        nint sddl,
        uint sddlLength,
        [MarshalAs(UnmanagedType.U1)] bool daclProtected,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ProcessListCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ProcessInfoCallback(ZpStatus status, nint info, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ProcessModulesCallback(
        ZpStatus status,
        ushort machineType,
        byte machineBits,
        nint modules,
        uint moduleCount,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ProcessHandlesCallback(ZpStatus status, nint handles, uint handleCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ProcessDumpCallback(ZpStatus status, nint path, uint pathLength, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ProcessMemoryCallback(ZpStatus status, nint data, uint dataLength, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void ProcessMemoryAllocationsCallback(
        ZpStatus status,
        uint snapshotId,
        nint allocations,
        uint allocationCount,
        nint context);

    internal delegate void ProcessMemoryRegionsCallback(
        ZpStatus status,
        nint regions,
        uint regionCount,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void WindowListCallback(ZpStatus status, nint records, uint recordCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void WindowMonitorsCallback(ZpStatus status, nint monitors, uint monitorCount, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void WindowInfoCallback(ZpStatus status, nint info, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void WindowCaptureCallback(ZpStatus status, nint bitmap, uint bitmapLength, nint context);

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
    internal readonly struct FileOwnerRecord
    {
        internal readonly uint ProcessId;
        internal readonly int ImagePathStatus;
        internal readonly int CommandLineStatus;
        internal readonly nint ImageName;
        internal readonly uint ImageNameLength;
        internal readonly nint ImagePath;
        internal readonly uint ImagePathLength;
        internal readonly nint CommandLine;
        internal readonly uint CommandLineLength;
        internal readonly nint ServiceNames;
        internal readonly uint ServiceNamesLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct FileOwnerControlResult
    {
        internal readonly uint ProcessId;
        internal readonly int Status;
        internal readonly uint AffectedHandleCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct FileDownloadRecord
    {
        internal readonly FileDownloadEngine Engine;
        internal readonly FileDownloadState State;
        internal readonly uint Result;
        internal readonly ulong TransferredBytes;
        internal readonly ulong TotalBytes;
        internal readonly nint Id;
        internal readonly uint IdLength;
        internal readonly nint Url;
        internal readonly uint UrlLength;
        internal readonly nint Path;
        internal readonly uint PathLength;
        internal readonly nint ErrorText;
        internal readonly uint ErrorTextLength;
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
        internal readonly byte PriorityClass;
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
        internal readonly byte PriorityClass;
        internal readonly ulong CreateTime;
        internal readonly ulong UserTime;
        internal readonly ulong KernelTime;
        internal readonly ulong WorkingSetBytes;
        internal readonly ulong PrivateBytes;
        internal readonly int ImageBaseStatus;
        internal readonly ulong ImageBase;
        internal readonly StringView ImageName;
        internal readonly StringView UserName;
        internal readonly int ImagePathStatus;
        internal readonly StringView ImagePath;
        internal readonly int CommandLineStatus;
        internal readonly StringView CommandLine;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ProcessModule
    {
        internal readonly ulong BaseAddress;
        internal readonly ulong EntryPoint;
        internal readonly ulong LoadTime;
        internal readonly uint SizeOfImage;
        internal readonly uint LoadReason;
        internal readonly nint Path;
        internal readonly uint PathLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ProcessHandle
    {
        internal readonly ulong HandleValue;
        internal readonly nint TypeName;
        internal readonly uint TypeNameLength;
        internal readonly nint ObjectName;
        internal readonly uint ObjectNameLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ProcessMemoryAllocation
    {
        internal readonly ulong AllocationBase;
        internal readonly ulong RegionSize;
        internal readonly ulong CommitSize;
        internal readonly ulong WorkingSetBytes;
        internal readonly ulong PrivateWorkingSetBytes;
        internal readonly ulong SharedWorkingSetBytes;
        internal readonly ulong ShareableWorkingSetBytes;
        internal readonly ulong LockedWorkingSetBytes;
        internal readonly ulong SharedOriginalBytes;
        internal readonly uint Type;
        internal readonly uint AllocationProtect;
        internal readonly uint RegionType;
        internal readonly uint Priority;
        internal readonly uint RegionCount;
        internal readonly int RegionStatus;
        internal readonly int WorkingSetStatus;
        internal readonly int MappedPathStatus;
        internal readonly nint MappedPath;
        internal readonly uint MappedPathLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ProcessMemoryRegion
    {
        internal readonly ulong BaseAddress;
        internal readonly ulong RegionSize;
        internal readonly ulong CommitSize;
        internal readonly ulong WorkingSetBytes;
        internal readonly ulong PrivateWorkingSetBytes;
        internal readonly ulong SharedWorkingSetBytes;
        internal readonly ulong ShareableWorkingSetBytes;
        internal readonly ulong LockedWorkingSetBytes;
        internal readonly ulong SharedOriginalBytes;
        internal readonly uint State;
        internal readonly uint Protect;
        internal readonly uint Priority;
        internal readonly int WorkingSetStatus;
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
    internal readonly struct WindowMonitor
    {
        internal readonly uint Index;
        internal readonly uint Flags;
        internal readonly int Left;
        internal readonly int Top;
        internal readonly int Right;
        internal readonly int Bottom;
        internal readonly int WorkLeft;
        internal readonly int WorkTop;
        internal readonly int WorkRight;
        internal readonly int WorkBottom;
        internal readonly nint Device;
        internal readonly uint DeviceLength;
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
        internal readonly ulong PreviousHandle;
        internal readonly ulong NextHandle;
        internal readonly ulong FirstChildHandle;
        internal readonly ulong FirstSiblingHandle;
        internal readonly ulong LastSiblingHandle;
        internal readonly int MonitorLeft;
        internal readonly int MonitorTop;
        internal readonly int MonitorRight;
        internal readonly int MonitorBottom;
        internal readonly StringView MonitorDevice;
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
        ulong clientId,
        string? path,
        uint pathLength,
        uint enumerationId,
        FilePageCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_EnumerateFilteredFilesPage",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int EnumerateFilteredFilesPage(
        ulong clientId,
        string? path,
        uint pathLength,
        string? filter,
        uint filterLength,
        char group,
        uint enumerationId,
        FilePageCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CloseFileEnumeration")]
    internal static partial int CloseFileEnumeration(
        ulong clientId,
        uint enumerationId,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_EnumerateArchivePage",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int EnumerateArchivePage(
        ulong clientId,
        string? path,
        uint pathLength,
        uint enumerationId,
        FilePageCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryShortcut",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryShortcut(
        ulong clientId,
        string path,
        uint pathLength,
        StringCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_PreviewImage",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int PreviewImage(
        ulong clientId,
        string path,
        uint pathLength,
        FileImagePreviewQuality quality,
        DataCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryFile",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryFile(
        ulong clientId,
        string path,
        uint pathLength,
        FileInfoCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryFileSecurity",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryFileSecurity(
        ulong clientId,
        string path,
        uint pathLength,
        SecurityDescriptorCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_SetFileSecurity",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int SetFileSecurity(
        ulong clientId,
        string path,
        uint pathLength,
        string sddl,
        uint sddlLength,
        [MarshalAs(UnmanagedType.U1)] bool daclProtected,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ResolveAccountName",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ResolveAccountName(
        ulong clientId,
        string name,
        uint nameLength,
        StringCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ResolveAccountSid",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ResolveAccountSid(
        ulong clientId,
        string sid,
        uint sidLength,
        StringCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryFileVolume",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryFileVolume(
        ulong clientId,
        string path,
        uint pathLength,
        FileVolumeCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_SetFileVolumeLabel",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int SetFileVolumeLabel(
        ulong clientId,
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
        ulong clientId,
        string path,
        uint pathLength,
        FileHashAlgorithm algorithm,
        FileHashCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_DeleteFile",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int DeleteFile(
        ulong clientId,
        string path,
        uint pathLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_RenameFile",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int RenameFile(
        ulong clientId,
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
        ulong clientId,
        string path,
        uint pathLength,
        uint attributes,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryFileOwners",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryFileOwners(
        ulong clientId,
        string path,
        uint pathLength,
        FileOwnersCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ControlFileOwners",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ControlFileOwners(
        ulong clientId,
        string path,
        uint pathLength,
        FileOwnerControl control,
        nint processIds,
        uint processCount,
        FileOwnerControlCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_StartFileDownload",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int StartFileDownload(
        ulong clientId,
        FileDownloadEngine engine,
        byte flags,
        string id,
        uint idLength,
        string url,
        uint urlLength,
        string path,
        uint pathLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateFileDownloads")]
    internal static partial int EnumerateFileDownloads(
        ulong clientId,
        FileDownloadsCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_CancelFileDownload",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int CancelFileDownload(
        ulong clientId,
        string id,
        uint idLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_WriteFileRange",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int WriteFileRange(
        ulong clientId,
        string path,
        uint pathLength,
        ulong offset,
        nint data,
        uint dataLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateProcesses")]
    internal static partial int EnumerateProcesses(ulong clientId, ProcessListCallback callback, nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_QueryProcess")]
    internal static partial int QueryProcess(
        ulong clientId,
        uint processId,
        ulong createTime,
        ProcessInfoCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateProcessModules")]
    internal static partial int EnumerateProcessModules(
        ulong clientId,
        uint processId,
        ulong createTime,
        ProcessModulesCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateProcessHandles")]
    internal static partial int EnumerateProcessHandles(
        ulong clientId,
        uint processId,
        ulong createTime,
        ProcessHandlesCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_ControlProcess")]
    internal static partial int ControlProcess(
        ulong clientId,
        uint processId,
        ulong createTime,
        ProcessControl control,
        uint value,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CreateProcessDump")]
    internal static partial int CreateProcessDump(
        ulong clientId,
        uint processId,
        ulong createTime,
        uint dumpType,
        ProcessDumpCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_ReadProcessMemory")]
    internal static partial int ReadProcessMemory(
        ulong clientId,
        uint processId,
        ulong createTime,
        ulong address,
        uint length,
        ProcessMemoryCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_WriteProcessMemory")]
    internal static partial int WriteProcessMemory(
        ulong clientId,
        uint processId,
        ulong createTime,
        ulong address,
        nint data,
        uint dataLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_QueryProcessMemoryMap")]
    internal static partial int QueryProcessMemoryMap(
        ulong clientId,
        uint processId,
        ulong createTime,
        ProcessMemoryAllocationsCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_QueryProcessMemoryRegions")]
    internal static partial int QueryProcessMemoryRegions(
        ulong clientId,
        uint snapshotId,
        uint allocationIndex,
        ProcessMemoryRegionsCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CloseProcessMemoryMap")]
    internal static partial int CloseProcessMemoryMap(
        ulong clientId,
        uint snapshotId,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateWindows")]
    internal static partial int EnumerateWindows(ulong clientId, WindowListCallback callback, nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateMonitors")]
    internal static partial int EnumerateMonitors(ulong clientId, WindowMonitorsCallback callback, nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_QueryWindow")]
    internal static partial int QueryWindow(
        ulong clientId,
        ulong handle,
        uint processId,
        uint threadId,
        WindowInfoCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_ControlWindow")]
    internal static partial int ControlWindow(
        ulong clientId,
        ulong handle,
        uint processId,
        uint threadId,
        WindowControl control,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_UpdateWindow",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int UpdateWindow(
        ulong clientId,
        ulong handle,
        uint processId,
        uint threadId,
        WindowUpdateFields fields,
        string caption,
        uint captionLength,
        int left,
        int top,
        int right,
        int bottom,
        uint style,
        uint exStyle,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CaptureWindow")]
    internal static partial int CaptureWindow(
        ulong clientId,
        ulong handle,
        uint processId,
        uint threadId,
        uint flags,
        uint maxDimension,
        byte frameRate,
        byte quality,
        uint monitorIndex,
        WindowCaptureCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateServices")]
    internal static partial int EnumerateServices(ulong clientId, ServiceListCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryService",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryService(
        ulong clientId,
        string serviceName,
        uint serviceNameLength,
        ServiceInfoCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ControlService",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ControlService(
        ulong clientId,
        byte control,
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
        ulong clientId,
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
        ulong clientId,
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
        ulong clientId,
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
