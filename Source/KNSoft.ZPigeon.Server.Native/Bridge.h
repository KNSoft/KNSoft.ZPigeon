#pragma once

#include <KNSoft/ZPigeon/Server.h>
#include <KNSoft/ZPigeon/Terminal.h>

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
(NTAPI *ZP_NATIVE_EVENT_LOG_CALLBACK)(
    _In_ ZP_STATUS Status,
    _In_ BOOLEAN HasMore,
    _In_reads_opt_(NextBookmarkLength) PCWCH NextBookmark,
    _In_ ULONG NextBookmarkLength,
    _In_reads_opt_(RecordCount) PCZP_EVENT_LOG_RECORD Records,
    _In_ ULONG RecordCount,
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
ZpNative_TerminateProcess(
    _In_ ULONG ProcessId,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
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
ZpNative_QueryEventLogPage(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_reads_opt_(BookmarkLength) PCWCH Bookmark,
    _In_ ULONG BookmarkLength,
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
ZpNative_ExecuteRegistryStatus(
    _In_ USHORT OperationId,
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
