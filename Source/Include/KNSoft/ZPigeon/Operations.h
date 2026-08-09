#pragma once

#include <KNSoft/ZPigeon/SDK.h>
#include <KNSoft/ZPigeon/EventLog.h>
#include <KNSoft/ZPigeon/File.h>
#include <KNSoft/ZPigeon/Process.h>
#include <KNSoft/ZPigeon/Registry.h>
#include <KNSoft/ZPigeon/Service.h>
#include <KNSoft/ZPigeon/System.h>

EXTERN_C_START

NTSTATUS
NTAPI
ZpRequest_Cancel(
    _In_ ZP_REQUEST_HANDLE Request);

VOID
NTAPI
ZpRequest_Close(
    _In_ ZP_REQUEST_HANDLE Request);

NTSTATUS
NTAPI
ZpChannel_Cancel(
    _In_ ZP_CHANNEL_HANDLE Channel);

NTSTATUS
NTAPI
ZpChannel_Send(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength);

VOID
NTAPI
ZpChannel_Close(
    _In_ ZP_CHANNEL_HANDLE Channel);

typedef
VOID
(NTAPI *ZP_EVENT_LOG_QUERY_PAGE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ const ZP_EVENT_LOG_PAGE_VIEW* Page,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_REQUEST_COMPLETE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_REQUEST_STATUS_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_TERMINAL_CREATE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG ProcessId,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_CHANNEL_DATA_CALLBACK)(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_CHANNEL_CLOSE_CALLBACK)(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_CHANNEL_WRITABLE_CALLBACK)(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_FILE_QUERY_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_FILE_INFO Info,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_FILE_ENUMERATE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_FILE_LIST_VIEW Files,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_FILE_ENUMERATE_PAGE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_FILE_PAGE_VIEW Page,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_FILE_HASH_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_FILE_HASH_VIEW Hash,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_FILE_OPEN_READ_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_ ULONGLONG Offset,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_FILE_OPEN_WRITE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_PROCESS_ENUMERATE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_PROCESS_LIST_VIEW Processes,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_PROCESS_QUERY_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ const ZP_PROCESS_INFO_VIEW* Info,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_SERVICE_ENUMERATE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_SERVICE_LIST_VIEW Services,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_SERVICE_QUERY_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ const ZP_SERVICE_INFO_VIEW* Info,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_REGISTRY_PAGE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_REGISTRY_PAGE_VIEW Page,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_REGISTRY_VALUE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_REGISTRY_VALUE_VIEW Value,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_SYSTEM_INFO_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ const ZP_SYSTEM_INFO_VIEW* Info,
    _In_opt_ PVOID Context);

EXTERN_C_END
