namespace KNSoft.ZPigeon.Server.Managed;

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
public sealed record SecurityDescriptor(string Sddl, bool DaclProtected);

public enum FileImagePreviewQuality : uint
{
    Low = 1,
    Medium,
    High
}
public sealed record FileOwnerRecord(
    uint ProcessId,
    int ImagePathStatus,
    int CommandLineStatus,
    string ImageName,
    string ImagePath,
    string CommandLine,
    string[] ServiceNames);
public sealed record FileOwnerControlResult(uint ProcessId, int Status, uint AffectedHandleCount);
public enum FileOwnerControl : byte
{
    Terminate = 1,
    CloseHandles
}
public sealed record FileVolumeInfo(
    ulong TotalBytes,
    ulong FreeBytes,
    uint SerialNumber,
    uint MaximumComponentLength,
    uint FileSystemFlags,
    string Label,
    string FileSystem);
public enum FileHashAlgorithm : byte
{
    Crc32 = 1,
    Md5 = 2,
    Sha1 = 3,
    Sha256 = 4
}
public sealed record FileDownloadRecord(
    string Id,
    string Url,
    string Path,
    string ErrorText,
    FileDownloadEngine Engine,
    FileDownloadState State,
    uint Result,
    ulong TransferredBytes,
    ulong TotalBytes);
public enum FileDownloadEngine : byte
{
    Bits = 1,
    WinHttp
}
public enum FileDownloadState : byte
{
    Queued = 1,
    Transferring,
    TransientError,
    Completed,
    Failed,
    Canceled
}
public sealed record ProcessRecord(
    uint ProcessId,
    uint ParentProcessId,
    uint SessionId,
    uint ThreadCount,
    uint HandleCount,
    uint Flags,
    ushort MachineType,
    byte PriorityClass,
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
    byte PriorityClass,
    DateTime CreateTime,
    ulong UserTime,
    ulong KernelTime,
    ulong WorkingSetBytes,
    ulong PrivateBytes,
    int ImageBaseStatus,
    ulong ImageBase,
    string ImageName,
    string UserName,
    int ImagePathStatus,
    string ImagePath,
    int CommandLineStatus,
    string CommandLine);
public sealed record ProcessModuleList(ushort MachineType, byte MachineBits, ProcessModule[] Modules);
public sealed record ProcessModule(
    bool MainImage,
    string BaseAddress,
    string EntryPoint,
    uint SizeOfImage,
    uint LoadReason,
    DateTime? LoadTime,
    string Path);
public sealed record ProcessHandle(string HandleValue, string TypeName, string ObjectName);
public sealed record ProcessMemoryMap(uint SnapshotId, ProcessMemoryAllocation[] Allocations);
public sealed record ProcessMemoryAllocation(
    string AllocationBase,
    string RegionSize,
    string CommitSize,
    string WorkingSetBytes,
    string PrivateWorkingSetBytes,
    string SharedWorkingSetBytes,
    string ShareableWorkingSetBytes,
    string LockedWorkingSetBytes,
    string SharedOriginalBytes,
    uint Type,
    uint AllocationProtect,
    uint RegionType,
    uint Priority,
    uint RegionCount,
    int RegionStatus,
    int WorkingSetStatus,
    int MappedPathStatus,
    string MappedPath);
public sealed record ProcessMemoryRegion(
    string BaseAddress,
    string RegionSize,
    string CommitSize,
    string WorkingSetBytes,
    string PrivateWorkingSetBytes,
    string SharedWorkingSetBytes,
    string ShareableWorkingSetBytes,
    string LockedWorkingSetBytes,
    string SharedOriginalBytes,
    uint State,
    uint Protect,
    uint Priority,
    int WorkingSetStatus);
public enum ProcessControl : byte
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
public sealed record WindowMonitor(
    uint Index,
    bool Primary,
    int Left,
    int Top,
    int Right,
    int Bottom,
    int WorkLeft,
    int WorkTop,
    int WorkRight,
    int WorkBottom,
    string Device);
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
    ushort CreatorVersion,
    string PreviousHandle,
    string NextHandle,
    string FirstChildHandle,
    string FirstSiblingHandle,
    string LastSiblingHandle,
    int MonitorLeft,
    int MonitorTop,
    int MonitorRight,
    int MonitorBottom,
    string MonitorDevice);
public enum WindowControl : byte
{
    Show = 1,
    Hide = 2,
    Minimize = 3,
    Maximize = 4,
    Restore = 5,
    Foreground = 6,
    Close = 7,
    Highlight = 8,
    Enable = 9,
    Disable = 10,
    Topmost = 11,
    NotTopmost = 12
}
[Flags]
public enum WindowUpdateFields : uint
{
    Caption = 1,
    Rect = 2,
    Style = 4,
    ExStyle = 8
}
public sealed record WindowUpdate(
    WindowUpdateFields Fields,
    string Caption,
    int Left,
    int Top,
    int Right,
    int Bottom,
    uint Style,
    uint ExStyle);
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
public enum ServiceControl : byte
{
    Start = 1,
    Stop,
    Pause,
    Continue,
    Restart
}
