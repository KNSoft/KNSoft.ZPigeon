#pragma once

#include <KNSoft/ZPigeon/SDK.h>
#include <KNSoft/ZPigeon/Administration.h>
#include <KNSoft/ZPigeon/Audio.h>
#include <KNSoft/ZPigeon/Browser.h>
#include <KNSoft/ZPigeon/Wmi.h>
#include <KNSoft/ZPigeon/EventLog.h>
#include <KNSoft/ZPigeon/Execution.h>
#include <KNSoft/ZPigeon/File.h>
#include <KNSoft/ZPigeon/Process.h>
#include <KNSoft/ZPigeon/Registry.h>
#include <KNSoft/ZPigeon/Service.h>
#include <KNSoft/ZPigeon/System.h>
#include <KNSoft/ZPigeon/Tunnel.h>
#include <KNSoft/ZPigeon/Window.h>
#include <KNSoft/ZPigeon/Video.h>
#include <KNSoft/ZPigeon/Rtc.h>
#include <KNSoft/ZPigeon/Serial.h>

EXTERN_C_START

typedef
VOID
(NTAPI *ZP_VIDEO_DEVICES_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_VIDEO_DEVICE_LIST_VIEW Devices,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_VIDEO_STREAM_OPEN_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_AUDIO_DEVICES_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_AUDIO_DEVICE_LIST_VIEW Devices,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_AUDIO_SESSIONS_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_AUDIO_SESSION_LIST_VIEW Sessions,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_AUDIO_STREAM_OPEN_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_TUNNEL_OPEN_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_SERIAL_PORTS_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_SERIAL_PORT_LIST_VIEW Ports,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_SERIAL_OPEN_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_opt_ PVOID Context);

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
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_EVENT_LOG_PAGE_VIEW* Page,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_EVENT_LOG_CHANNELS_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_EVENT_LOG_CHANNEL_LIST_VIEW Channels,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_EVENT_LOG_CHANNEL_INFO_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_EVENT_LOG_CHANNEL_INFO_VIEW* Info,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_EXECUTION_SESSIONS_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_EXECUTION_SESSION_LIST_VIEW Sessions,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_EXECUTION_JOBS_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_EXECUTION_JOB_LIST_VIEW Jobs,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_EXECUTION_STAGING_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_STRING_VIEW Path,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_REQUEST_COMPLETE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_REQUEST_STATUS_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_TERMINAL_CREATE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG ProcessId,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_TERMINAL_SHELLS_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ ULONG Shells,
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
    _In_ ZP_STATUS Status,
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
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_INFO Info,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_STRING_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_STRING_VIEW Value,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_FILE_VOLUME_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_FILE_VOLUME_INFO_VIEW* Info,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_FILE_ENUMERATE_PAGE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_PAGE_VIEW Page,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_FILE_HASH_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_HASH_VIEW Hash,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_FILE_OPEN_READ_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_ ULONGLONG Offset,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_FILE_OPEN_WRITE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_PROCESS_ENUMERATE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_PROCESS_LIST_VIEW Processes,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_PROCESS_QUERY_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_PROCESS_INFO_VIEW* Info,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_PROCESS_DUMP_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_STRING_VIEW Path,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_PROCESS_MEMORY_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_SERVICE_ENUMERATE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_SERVICE_LIST_VIEW Services,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_SERVICE_QUERY_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_SERVICE_INFO_VIEW* Info,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_REGISTRY_PAGE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_PAGE_VIEW Page,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_REGISTRY_VALUE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_VALUE_VIEW Value,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_REGISTRY_RANGE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_RANGE_VIEW Range,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_SYSTEM_INFO_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_SYSTEM_INFO_VIEW* Info,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_WINDOW_ENUMERATE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_WINDOW_LIST_VIEW Windows,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_WINDOW_QUERY_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_WINDOW_INFO_VIEW* Info,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_WINDOW_CAPTURE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BUFFER_VIEW Image,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_WINDOW_CAPTURE_OPEN_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_ADMINISTRATION_ENUMERATE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_ADMINISTRATION_LIST_VIEW Records,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_BROWSER_PAGE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BROWSER_PAGE_VIEW Page,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_WMI_PAGE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_WMI_PAGE_VIEW Page,
    _In_opt_ PVOID Context);

EXTERN_C_END
