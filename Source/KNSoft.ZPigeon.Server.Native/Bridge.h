#pragma once

#include <KNSoft/ZPigeon/Server.h>
#include <KNSoft/ZPigeon/Terminal.h>
#include <KNSoft/ZPigeon/Tunnel.h>

EXTERN_C_START

typedef
VOID
(NTAPI *ZP_NATIVE_SYSTEM_INFO_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ ZP_SYSTEM_ARCHITECTURE Architecture,
    _In_ ULONG MajorVersion,
    _In_ ULONG MinorVersion,
    _In_ ULONG BuildNumber,
    _In_ ULONG ProcessorCount,
    _In_ ULONGLONG PhysicalMemoryBytes,
    _In_reads_opt_(ComputerNameLength) PCWCH ComputerName,
    _In_ ULONG ComputerNameLength,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_STATUS_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_STRING_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(ValueLength) PCWCH Value,
    _In_ ULONG ValueLength,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_FILE_RECORD
{
    ULONG Attributes;
    ULONGLONG Size;
    ULONGLONG CreationTime;
    ULONGLONG LastAccessTime;
    ULONGLONG LastWriteTime;
    PCWCH Name;
    ULONG NameLength;
    BOOLEAN HasChildren;
} ZP_NATIVE_FILE_RECORD, *PZP_NATIVE_FILE_RECORD;

typedef const ZP_NATIVE_FILE_RECORD* PCZP_NATIVE_FILE_RECORD;

typedef
VOID
(NTAPI *ZP_NATIVE_FILE_PAGE_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ ULONG EnumerationId,
    _In_reads_opt_(RecordCount) PCZP_NATIVE_FILE_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_FILE_INFO_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ ULONG Attributes,
    _In_ ULONGLONG Size,
    _In_ ULONGLONG CreationTime,
    _In_ ULONGLONG LastAccessTime,
    _In_ ULONGLONG LastWriteTime,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_FILE_HASH_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ ULONGLONG FileSize,
    _In_reads_bytes_opt_(DigestLength) const VOID* Digest,
    _In_ ULONG DigestLength,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_FILE_TRANSFER* ZP_NATIVE_FILE_TRANSFER_HANDLE;

typedef
VOID
(NTAPI *ZP_NATIVE_FILE_OPEN_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_NATIVE_FILE_TRANSFER_HANDLE Transfer,
    _In_ ULONGLONG FileSize,
    _In_opt_ PVOID Context);

typedef
LOGICAL
(NTAPI *ZP_NATIVE_FILE_DATA_CALLBACK)(
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_FILE_WRITABLE_CALLBACK)(
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_FILE_CLOSE_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_PROCESS_RECORD
{
    ULONG ProcessId;
    ULONG ParentProcessId;
    ULONG SessionId;
    ULONG ThreadCount;
    ULONG HandleCount;
    ULONG Flags;
    USHORT MachineType;
    USHORT PriorityClass;
    ULONGLONG CreateTime;
    ULONGLONG UserTime;
    ULONGLONG KernelTime;
    ULONGLONG WorkingSetBytes;
    ULONGLONG PrivateBytes;
    PCWCH ImageName;
    ULONG ImageNameLength;
    PCWCH UserName;
    ULONG UserNameLength;
    PCWCH ImagePath;
    ULONG ImagePathLength;
    PCWCH ServiceNames;
    ULONG ServiceNamesLength;
} ZP_NATIVE_PROCESS_RECORD, *PZP_NATIVE_PROCESS_RECORD;

typedef const ZP_NATIVE_PROCESS_RECORD* PCZP_NATIVE_PROCESS_RECORD;

typedef
VOID
(NTAPI *ZP_NATIVE_PROCESS_LIST_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(RecordCount) PCZP_NATIVE_PROCESS_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_PROCESS_INFO_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_PROCESS_INFO_VIEW* Info,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_PROCESS_DUMP_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_FILE_VOLUME_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ ULONGLONG TotalBytes,
    _In_ ULONGLONG FreeBytes,
    _In_ ULONG SerialNumber,
    _In_ ULONG MaximumComponentLength,
    _In_ ULONG FileSystemFlags,
    _In_reads_opt_(LabelLength) PCWCH Label,
    _In_ ULONG LabelLength,
    _In_reads_opt_(FileSystemLength) PCWCH FileSystem,
    _In_ ULONG FileSystemLength,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_EXECUTION_SESSION_RECORD
{
    ULONG SessionId;
    ULONG State;
    ULONG Flags;
    PCWCH StationName;
    ULONG StationNameLength;
    PCWCH UserName;
    ULONG UserNameLength;
} ZP_NATIVE_EXECUTION_SESSION_RECORD, *PZP_NATIVE_EXECUTION_SESSION_RECORD;

typedef const ZP_NATIVE_EXECUTION_SESSION_RECORD* PCZP_NATIVE_EXECUTION_SESSION_RECORD;

typedef
VOID
(NTAPI *ZP_NATIVE_EXECUTION_SESSIONS_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(RecordCount) PCZP_NATIVE_EXECUTION_SESSION_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_EXECUTION_JOB_RECORD
{
    ULONG JobId;
    ULONGLONG CreateTime;
    ULONGLONG ExitTime;
    ULONG ProcessId;
    ULONG SessionId;
    ULONG ExitCode;
    ULONG Flags;
    USHORT Engine;
    USHORT Identity;
    USHORT State;
    PCWCH FileName;
    ULONG FileNameLength;
} ZP_NATIVE_EXECUTION_JOB_RECORD, *PZP_NATIVE_EXECUTION_JOB_RECORD;

typedef const ZP_NATIVE_EXECUTION_JOB_RECORD* PCZP_NATIVE_EXECUTION_JOB_RECORD;

typedef
VOID
(NTAPI *ZP_NATIVE_EXECUTION_JOBS_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(RecordCount) PCZP_NATIVE_EXECUTION_JOB_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_EXECUTION_STAGING_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_WINDOW_RECORD
{
    ULONGLONG Handle;
    ULONGLONG ParentHandle;
    ULONG ProcessId;
    ULONG ThreadId;
    ULONG Style;
    ULONG ExStyle;
    ULONG Flags;
    PCWCH Caption;
    ULONG CaptionLength;
    PCWCH ClassName;
    ULONG ClassNameLength;
} ZP_NATIVE_WINDOW_RECORD, *PZP_NATIVE_WINDOW_RECORD;

typedef const ZP_NATIVE_WINDOW_RECORD* PCZP_NATIVE_WINDOW_RECORD;

typedef struct _ZP_NATIVE_AUDIO_DEVICE_RECORD
{
    USHORT Flow;
    ULONG State;
    ULONG Flags;
    ULONG Volume;
    PCWCH Id;
    ULONG IdLength;
    PCWCH Name;
    ULONG NameLength;
} ZP_NATIVE_AUDIO_DEVICE_RECORD, *PZP_NATIVE_AUDIO_DEVICE_RECORD;

typedef const ZP_NATIVE_AUDIO_DEVICE_RECORD* PCZP_NATIVE_AUDIO_DEVICE_RECORD;

typedef struct _ZP_NATIVE_AUDIO_SESSION_RECORD
{
    ULONG ProcessId;
    ULONG State;
    ULONG Flags;
    ULONG Volume;
    PCWCH DeviceId;
    ULONG DeviceIdLength;
    PCWCH Id;
    ULONG IdLength;
    PCWCH Name;
    ULONG NameLength;
} ZP_NATIVE_AUDIO_SESSION_RECORD, *PZP_NATIVE_AUDIO_SESSION_RECORD;

typedef const ZP_NATIVE_AUDIO_SESSION_RECORD* PCZP_NATIVE_AUDIO_SESSION_RECORD;

typedef struct _ZP_NATIVE_AUDIO_STREAM* ZP_NATIVE_AUDIO_STREAM_HANDLE;

typedef struct _ZP_NATIVE_VIDEO_DEVICE_RECORD
{
    PCWCH Id;
    ULONG IdLength;
    PCWCH Name;
    ULONG NameLength;
} ZP_NATIVE_VIDEO_DEVICE_RECORD, *PZP_NATIVE_VIDEO_DEVICE_RECORD;

typedef const ZP_NATIVE_VIDEO_DEVICE_RECORD* PCZP_NATIVE_VIDEO_DEVICE_RECORD;

typedef struct _ZP_NATIVE_VIDEO_STREAM* ZP_NATIVE_VIDEO_STREAM_HANDLE;

typedef
VOID
(NTAPI *ZP_NATIVE_VIDEO_DEVICES_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(RecordCount) PCZP_NATIVE_VIDEO_DEVICE_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_VIDEO_STREAM_OPEN_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_NATIVE_VIDEO_STREAM_HANDLE Stream,
    _In_opt_ PVOID Context);

typedef
BOOLEAN
(NTAPI *ZP_NATIVE_VIDEO_STREAM_DATA_CALLBACK)(
    _In_reads_bytes_(DataLength) const BYTE* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_VIDEO_STREAM_CLOSE_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_AUDIO_DEVICES_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(RecordCount) PCZP_NATIVE_AUDIO_DEVICE_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_AUDIO_SESSIONS_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(RecordCount) PCZP_NATIVE_AUDIO_SESSION_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_AUDIO_STREAM_OPEN_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_NATIVE_AUDIO_STREAM_HANDLE Stream,
    _In_opt_ PVOID Context);

typedef
BOOLEAN
(NTAPI *ZP_NATIVE_AUDIO_STREAM_DATA_CALLBACK)(
    _In_reads_bytes_(DataLength) const BYTE* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_AUDIO_STREAM_CLOSE_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_WINDOW_CAPTURE_STREAM* ZP_NATIVE_WINDOW_CAPTURE_STREAM_HANDLE;

typedef
VOID
(NTAPI *ZP_NATIVE_WINDOW_LIST_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(RecordCount) PCZP_NATIVE_WINDOW_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_WINDOW_INFO_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_WINDOW_INFO_VIEW* Info,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_WINDOW_CAPTURE_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_bytes_opt_(ImageLength) const BYTE* Image,
    _In_ ULONG ImageLength,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_WINDOW_CAPTURE_OPEN_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_NATIVE_WINDOW_CAPTURE_STREAM_HANDLE Stream,
    _In_opt_ PVOID Context);

typedef
BOOLEAN
(NTAPI *ZP_NATIVE_WINDOW_CAPTURE_DATA_CALLBACK)(
    _In_reads_bytes_(DataLength) const BYTE* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_WINDOW_CAPTURE_CLOSE_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_SERVICE_RECORD
{
    ULONG ServiceType;
    ULONG CurrentState;
    ULONG ControlsAccepted;
    ULONG ProcessId;
    ULONG StartType;
    PCWCH ServiceName;
    ULONG ServiceNameLength;
    PCWCH DisplayName;
    ULONG DisplayNameLength;
    PCWCH Description;
    ULONG DescriptionLength;
    PCWCH StartName;
    ULONG StartNameLength;
} ZP_NATIVE_SERVICE_RECORD, *PZP_NATIVE_SERVICE_RECORD;

typedef const ZP_NATIVE_SERVICE_RECORD* PCZP_NATIVE_SERVICE_RECORD;

typedef
VOID
(NTAPI *ZP_NATIVE_SERVICE_LIST_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(RecordCount) PCZP_NATIVE_SERVICE_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_SERVICE_INFO_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_SERVICE_INFO_VIEW* Info,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_EVENT_LOG_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ BOOLEAN HasMore,
    _In_reads_opt_(NextBookmarkLength) PCWCH NextBookmark,
    _In_ ULONG NextBookmarkLength,
    _In_reads_opt_(RecordCount) PCZP_EVENT_LOG_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_EVENT_LOG_CHANNELS_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(ChannelCount) const ZP_STRING_VIEW* Channels,
    _In_ ULONG ChannelCount,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_EVENT_LOG_CHANNEL_INFO_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ BOOLEAN Enabled,
    _In_ ULONG Type,
    _In_ ZP_EVENT_LOG_RETENTION_MODE RetentionMode,
    _In_ ULONGLONG MaximumSize,
    _In_ ULONGLONG FileSize,
    _In_ ULONGLONG CreationTime,
    _In_ ULONGLONG LastAccessTime,
    _In_ ULONGLONG LastWriteTime,
    _In_reads_opt_(LogFilePathLength) PCWCH LogFilePath,
    _In_ ULONG LogFilePathLength,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_ADMINISTRATION_RECORD
{
    USHORT Kind;
    ULONG State;
    ULONG Flags;
    ULONGLONG Value;
    PCWCH Identity;
    ULONG IdentityLength;
    PCWCH Name;
    ULONG NameLength;
    PCWCH Description;
    ULONG DescriptionLength;
    PCWCH Detail;
    ULONG DetailLength;
} ZP_NATIVE_ADMINISTRATION_RECORD, *PZP_NATIVE_ADMINISTRATION_RECORD;

typedef const ZP_NATIVE_ADMINISTRATION_RECORD* PCZP_NATIVE_ADMINISTRATION_RECORD;

typedef
VOID
(NTAPI *ZP_NATIVE_ADMINISTRATION_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(RecordCount) PCZP_NATIVE_ADMINISTRATION_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_BROWSER_RECORD
{
    USHORT Kind;
    USHORT Browser;
    ULONG State;
    ULONG Flags;
    ULONGLONG Id;
    ULONGLONG Time;
    ULONGLONG Value;
    PCWCH Identity;
    ULONG IdentityLength;
    PCWCH Name;
    ULONG NameLength;
    PCWCH Location;
    ULONG LocationLength;
    PCWCH Detail;
    ULONG DetailLength;
} ZP_NATIVE_BROWSER_RECORD, *PZP_NATIVE_BROWSER_RECORD;

typedef const ZP_NATIVE_BROWSER_RECORD* PCZP_NATIVE_BROWSER_RECORD;

typedef
VOID
(NTAPI *ZP_NATIVE_BROWSER_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ ULONGLONG NextCursor,
    _In_reads_opt_(RecordCount) PCZP_NATIVE_BROWSER_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_WMI_CELL
{
    ULONG Type;
    PCWCH Name;
    ULONG NameLength;
    PCWCH Value;
    ULONG ValueLength;
} ZP_NATIVE_WMI_CELL, *PZP_NATIVE_WMI_CELL;

typedef const ZP_NATIVE_WMI_CELL* PCZP_NATIVE_WMI_CELL;

typedef struct _ZP_NATIVE_WMI_ROW
{
    PCZP_NATIVE_WMI_CELL Cells;
    ULONG CellCount;
} ZP_NATIVE_WMI_ROW, *PZP_NATIVE_WMI_ROW;

typedef const ZP_NATIVE_WMI_ROW* PCZP_NATIVE_WMI_ROW;

typedef
VOID
(NTAPI *ZP_NATIVE_WMI_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_opt_(RowCount) PCZP_NATIVE_WMI_ROW Rows,
    _In_ ULONG RowCount,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_REGISTRY_KEY_RECORD
{
    PCWCH Name;
    ULONG NameLength;
    ULONGLONG LastWriteTime;
    BOOLEAN HasChildren;
} ZP_NATIVE_REGISTRY_KEY_RECORD, *PZP_NATIVE_REGISTRY_KEY_RECORD;

typedef const ZP_NATIVE_REGISTRY_KEY_RECORD*
    PCZP_NATIVE_REGISTRY_KEY_RECORD;

typedef struct _ZP_NATIVE_REGISTRY_VALUE_RECORD
{
    PCWCH Name;
    ULONG NameLength;
    ULONG Type;
    ULONG DataLength;
    const VOID* Preview;
    ULONG PreviewLength;
} ZP_NATIVE_REGISTRY_VALUE_RECORD, *PZP_NATIVE_REGISTRY_VALUE_RECORD;

typedef const ZP_NATIVE_REGISTRY_VALUE_RECORD*
    PCZP_NATIVE_REGISTRY_VALUE_RECORD;

typedef
VOID
(NTAPI *ZP_NATIVE_REGISTRY_KEY_PAGE_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ BOOLEAN HasMore,
    _In_reads_opt_(NextCursorLength) PCWCH NextCursor,
    _In_ ULONG NextCursorLength,
    _In_reads_opt_(RecordCount) PCZP_NATIVE_REGISTRY_KEY_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_REGISTRY_VALUE_PAGE_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ BOOLEAN HasMore,
    _In_reads_opt_(NextCursorLength) PCWCH NextCursor,
    _In_ ULONG NextCursorLength,
    _In_reads_opt_(RecordCount) PCZP_NATIVE_REGISTRY_VALUE_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_REGISTRY_VALUE_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_REGISTRY_RANGE_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ ULONG TotalLength,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_TERMINAL* ZP_NATIVE_TERMINAL_HANDLE;

typedef
VOID
(NTAPI *ZP_NATIVE_TERMINAL_SHELLS_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ ULONG Shells,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_TERMINAL_CREATE_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_NATIVE_TERMINAL_HANDLE Terminal,
    _In_ ULONG ProcessId,
    _In_opt_ PVOID Context);

typedef
LOGICAL
(NTAPI *ZP_NATIVE_TERMINAL_DATA_CALLBACK)(
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_TERMINAL_WRITABLE_CALLBACK)(
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_TERMINAL_CLOSE_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context);

__declspec(dllexport)
ZP_STATUS
NTAPI
ZpNative_Start(
    _In_ PCCERT_CONTEXT Certificate,
    _In_ USHORT Port);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_Stop(VOID);

__declspec(dllexport)
ZP_SERVER_STATE
NTAPI
ZpNative_GetState(VOID);

__declspec(dllexport)
LOGICAL
NTAPI
ZpNative_IsClientConnected(VOID);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_GetSystemInfo(
    _In_ ZP_NATIVE_SYSTEM_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateFilesPage(
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG EnumerationId,
    _In_ ZP_NATIVE_FILE_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryFile(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_FILE_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryFileSecurity(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_SetFileSecurity(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(SddlLength) PCWCH Sddl,
    _In_ ULONG SddlLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ResolveAccountName(
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ZP_NATIVE_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ResolveAccountSid(
    _In_reads_(SidLength) PCWCH Sid,
    _In_ ULONG SidLength,
    _In_ ZP_NATIVE_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_HashFile(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_FILE_HASH_ALGORITHM Algorithm,
    _In_ ZP_NATIVE_FILE_HASH_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_DeleteFile(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_RenameFile(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(NewPathLength) PCWCH NewPath,
    _In_ ULONG NewPathLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_SetFileAttributes(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG Attributes,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_OpenFileRead(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG Offset,
    _In_ ZP_NATIVE_FILE_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_FILE_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_FILE_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_PROCESS_MEMORY_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_WriteFileRange(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_OpenFileWrite(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG FileSize,
    _In_ LOGICAL Overwrite,
    _In_ ZP_NATIVE_FILE_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_FILE_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_NATIVE_FILE_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_FileSend(
    _In_ ZP_NATIVE_FILE_TRANSFER_HANDLE Transfer,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_CloseFileTransfer(
    _In_ ZP_NATIVE_FILE_TRANSFER_HANDLE Transfer);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateProcesses(
    _In_ ZP_NATIVE_PROCESS_LIST_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryProcess(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ZP_NATIVE_PROCESS_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ControlProcess(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ZP_PROCESS_CONTROL Control,
    _In_ ULONG Value,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_CreateProcessDump(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONG DumpType,
    _In_ ZP_NATIVE_PROCESS_DUMP_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ReadProcessMemory(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONGLONG Address,
    _In_ ULONG Length,
    _In_ ZP_NATIVE_PROCESS_MEMORY_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_WriteProcessMemory(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONGLONG Address,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

typedef struct _ZP_NATIVE_TUNNEL* ZP_NATIVE_TUNNEL_HANDLE;

typedef
VOID
(NTAPI *ZP_NATIVE_TUNNEL_OPEN_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_NATIVE_TUNNEL_HANDLE Tunnel,
    _In_opt_ PVOID Context);

typedef
LOGICAL
(NTAPI *ZP_NATIVE_TUNNEL_DATA_CALLBACK)(
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_TUNNEL_WRITABLE_CALLBACK)(
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_TUNNEL_CLOSE_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryFileVolume(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_FILE_VOLUME_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_SetFileVolumeLabel(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(LabelLength) PCWCH Label,
    _In_ ULONG LabelLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateExecutionSessions(
    _In_ ZP_NATIVE_EXECUTION_SESSIONS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_StartExecution(
    _In_ USHORT Engine,
    _In_ USHORT Identity,
    _In_ ULONG SessionId,
    _In_ ULONG Flags,
    _In_reads_(FileNameLength) PCWCH FileName,
    _In_ ULONG FileNameLength,
    _In_reads_opt_(ArgumentsLength) PCWCH Arguments,
    _In_ ULONG ArgumentsLength,
    _In_reads_opt_(WorkingDirectoryLength) PCWCH WorkingDirectory,
    _In_ ULONG WorkingDirectoryLength,
    _In_reads_opt_(VerbLength) PCWCH Verb,
    _In_ ULONG VerbLength,
    _In_reads_opt_(UserNameLength) PCWCH UserName,
    _In_ ULONG UserNameLength,
    _In_reads_opt_(PasswordLength) PCWCH Password,
    _In_ ULONG PasswordLength,
    _In_ ZP_NATIVE_EXECUTION_JOBS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateExecutionJobs(
    _In_ ZP_NATIVE_EXECUTION_JOBS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_TerminateExecution(
    _In_ ULONG JobId,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_CreateExecutionStaging(
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ZP_NATIVE_EXECUTION_STAGING_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateWindows(
    _In_ ZP_NATIVE_WINDOW_LIST_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryWindow(
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ZP_NATIVE_WINDOW_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ControlWindow(
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ZP_WINDOW_CONTROL Control,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_UpdateWindow(
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ULONG Fields,
    _In_reads_opt_(CaptionLength) PCWCH Caption,
    _In_ ULONG CaptionLength,
    _In_ LONG Left,
    _In_ LONG Top,
    _In_ LONG Right,
    _In_ LONG Bottom,
    _In_ ULONG Style,
    _In_ ULONG ExStyle,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_CaptureWindow(
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ULONG Flags,
    _In_ ULONG MaxDimension,
    _In_ USHORT FrameRate,
    _In_ USHORT Quality,
    _In_ ZP_NATIVE_WINDOW_CAPTURE_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_OpenWindowCapture(
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ULONG Flags,
    _In_ ULONG MaxDimension,
    _In_ USHORT FrameRate,
    _In_ USHORT Quality,
    _In_ ULONG DirectStreamId,
    _In_ ZP_NATIVE_WINDOW_CAPTURE_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_WINDOW_CAPTURE_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_WINDOW_CAPTURE_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_CloseWindowCapture(
    _In_ ZP_NATIVE_WINDOW_CAPTURE_STREAM_HANDLE Stream);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateAudioDevices(
    _In_ ZP_NATIVE_AUDIO_DEVICES_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateAudioSessions(
    _In_ ZP_NATIVE_AUDIO_SESSIONS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ControlAudioEndpoint(
    _In_ USHORT Flow,
    _In_ USHORT Control,
    _In_ ULONG Value,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ControlAudioSession(
    _In_ USHORT Control,
    _In_ ULONG Value,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(SessionIdLength) PCWCH SessionId,
    _In_ ULONG SessionIdLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_OpenAudioStream(
    _In_ USHORT Flow,
    _In_ ULONG DirectStreamId,
    _In_reads_opt_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ZP_NATIVE_AUDIO_STREAM_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_AUDIO_STREAM_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_AUDIO_STREAM_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_CloseAudioStream(
    _In_ ZP_NATIVE_AUDIO_STREAM_HANDLE Stream);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateVideoDevices(
    _In_ ZP_NATIVE_VIDEO_DEVICES_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_OpenVideoStream(
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ULONG MaxDimension,
    _In_ USHORT FrameRate,
    _In_ USHORT Quality,
    _In_ ULONG DirectStreamId,
    _In_ ZP_NATIVE_VIDEO_STREAM_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_VIDEO_STREAM_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_VIDEO_STREAM_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_CloseVideoStream(
    _In_ ZP_NATIVE_VIDEO_STREAM_HANDLE Stream);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_OpenRtc(
    _In_reads_(ZP_RTC_SESSION_ID_SIZE) const BYTE* SessionId,
    _In_reads_(OfferLength) PCWCH Offer,
    _In_ ULONG OfferLength,
    _In_reads_opt_(IceServersLength) PCWCH IceServers,
    _In_ ULONG IceServersLength,
    _In_ ZP_NATIVE_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_CloseRtc(
    _In_reads_(ZP_RTC_SESSION_ID_SIZE) const BYTE* SessionId,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateServices(
    _In_ ZP_NATIVE_SERVICE_LIST_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryService(
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ZP_NATIVE_SERVICE_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ControlService(
    _In_ ULONG Control,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_reads_opt_(ArgumentLength) PCWCH Argument,
    _In_ ULONG ArgumentLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ConfigureService(
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG StartType,
    _In_ BOOLEAN DelayedAutoStart,
    _In_reads_(DisplayNameLength) PCWCH DisplayName,
    _In_ ULONG DisplayNameLength,
    _In_reads_opt_(DescriptionLength) PCWCH Description,
    _In_ ULONG DescriptionLength,
    _In_reads_(BinaryPathNameLength) PCWCH BinaryPathName,
    _In_ ULONG BinaryPathNameLength,
    _In_reads_opt_(LoadOrderGroupLength) PCWCH LoadOrderGroup,
    _In_ ULONG LoadOrderGroupLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ConfigureServiceRecovery(
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG ErrorControl,
    _In_ BOOLEAN FailureActionsOnNonCrashFailures,
    _In_ ULONG ResetPeriodSeconds,
    _In_ ULONG RestartDelayMilliseconds,
    _In_ ULONG RebootDelayMilliseconds,
    _In_ ULONG FirstFailureAction,
    _In_ ULONG SecondFailureAction,
    _In_ ULONG ThirdFailureAction,
    _In_ ULONG SubsequentFailureAction,
    _In_reads_opt_(RebootMessageLength) PCWCH RebootMessage,
    _In_ ULONG RebootMessageLength,
    _In_reads_opt_(CommandLength) PCWCH Command,
    _In_ ULONG CommandLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ConfigureServiceAccount(
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_reads_(StartNameLength) PCWCH StartName,
    _In_ ULONG StartNameLength,
    _In_reads_opt_(PasswordLength) PCWCH Password,
    _In_ ULONG PasswordLength,
    _In_ BOOLEAN PasswordPresent,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateAdministration(
    _In_ BYTE OperationId,
    _In_ ZP_NATIVE_ADMINISTRATION_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryAdministration(
    _In_ BYTE OperationId,
    _In_reads_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_ ZP_NATIVE_ADMINISTRATION_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ControlAdministration(
    _In_ BYTE OperationId,
    _In_ USHORT Action,
    _In_reads_opt_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_reads_opt_(ArgumentLength) PCWCH Argument,
    _In_ ULONG ArgumentLength,
    _In_reads_opt_(SecretLength) PCWCH Secret,
    _In_ ULONG SecretLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateBrowsers(
    _In_ ZP_NATIVE_BROWSER_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryBrowser(
    _In_ USHORT Browser,
    _In_ USHORT Kind,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_ ULONGLONG Cursor,
    _In_ ULONG Limit,
    _In_ ZP_NATIVE_BROWSER_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateWmiNamespaces(
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_ ZP_NATIVE_WMI_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateWmiClasses(
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_ ZP_NATIVE_WMI_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryWmi(
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_reads_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_ ULONG Limit,
    _In_ ULONG Flags,
    _In_ ZP_NATIVE_WMI_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryTerminalShells(
    _In_ ZP_NATIVE_TERMINAL_SHELLS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_CreateTerminal(
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _In_reads_(CommandLineLength) PCWCH CommandLine,
    _In_ ULONG CommandLineLength,
    _In_reads_opt_(WorkingDirectoryLength) PCWCH WorkingDirectory,
    _In_ ULONG WorkingDirectoryLength,
    _In_ ZP_NATIVE_TERMINAL_CREATE_CALLBACK CreateCallback,
    _In_ ZP_NATIVE_TERMINAL_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_TERMINAL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_NATIVE_TERMINAL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_TerminalSend(
    _In_ ZP_NATIVE_TERMINAL_HANDLE Terminal,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ResizeTerminal(
    _In_ ZP_NATIVE_TERMINAL_HANDLE Terminal,
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_CloseTerminal(
    _In_ ZP_NATIVE_TERMINAL_HANDLE Terminal);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_OpenTunnel(
    _In_reads_(HostLength) PCWCH Host,
    _In_ ULONG HostLength,
    _In_ USHORT Port,
    _In_ USHORT Protocol,
    _In_ ZP_NATIVE_TUNNEL_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_TUNNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_TUNNEL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_NATIVE_TUNNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_TunnelSend(
    _In_ ZP_NATIVE_TUNNEL_HANDLE Tunnel,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_CloseTunnel(
    _In_ ZP_NATIVE_TUNNEL_HANDLE Tunnel);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateEventLogChannels(
    _In_ ZP_NATIVE_EVENT_LOG_CHANNELS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryEventLogChannelInfo(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ ZP_NATIVE_EVENT_LOG_CHANNEL_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryEventLogPage(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_reads_opt_(BookmarkLength) PCWCH Bookmark,
    _In_ ULONG BookmarkLength,
    _In_ BOOLEAN Forward,
    _In_ ULONG MaxEvents,
    _In_ ZP_NATIVE_EVENT_LOG_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_SetEventLogChannelEnabled(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ BOOLEAN Enabled,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ClearEventLog(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ConfigureEventLogChannel(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ BOOLEAN Enabled,
    _In_ ZP_EVENT_LOG_RETENTION_MODE RetentionMode,
    _In_ ULONGLONG MaximumSize,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateRegistryKeysPage(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _In_ ULONG MaxEntries,
    _In_ ZP_NATIVE_REGISTRY_KEY_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_EnumerateRegistryValuesPage(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _In_ ULONG MaxEntries,
    _In_ ZP_NATIVE_REGISTRY_VALUE_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryRegistryValue(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ZP_NATIVE_REGISTRY_VALUE_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryRegistryValueRange(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _In_ ZP_NATIVE_REGISTRY_RANGE_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_WriteRegistryValueRange(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONG Offset,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_QueryRegistrySecurity(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_REGISTRY_VALUE_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_SetRegistrySecurity(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(SddlLength) PCWCH Sddl,
    _In_ ULONG SddlLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ExecuteRegistryStatus(
    _In_ BYTE OperationId,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_reads_opt_(NewNameLength) PCWCH NewName,
    _In_ ULONG NewNameLength,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context);

EXTERN_C_END
