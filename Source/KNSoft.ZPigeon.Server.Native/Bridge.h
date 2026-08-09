#pragma once

#include <KNSoft/ZPigeon/Server.h>
#include <KNSoft/ZPigeon/Terminal.h>

EXTERN_C_START

typedef
VOID
(NTAPI *ZP_NATIVE_SYSTEM_INFO_CALLBACK)(
    _In_ NTSTATUS Status,
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
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_NATIVE_EVENT_LOG_CALLBACK)(
    _In_ NTSTATUS Status,
    _In_ BOOLEAN HasMore,
    _In_reads_opt_(NextBookmarkLength) PCWCH NextBookmark,
    _In_ ULONG NextBookmarkLength,
    _In_reads_opt_(RecordCount) PCZP_EVENT_LOG_RECORD Records,
    _In_ ULONG RecordCount,
    _In_opt_ PVOID Context);

__declspec(dllexport)
NTSTATUS
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

EXTERN_C_END
