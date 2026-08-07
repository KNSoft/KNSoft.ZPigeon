#pragma once

#include <KNSoft/ZPigeon/SDK.h>
#include <KNSoft/ZPigeon/EventLog.h>
#include <KNSoft/ZPigeon/File.h>
#include <KNSoft/ZPigeon/Process.h>
#include <KNSoft/ZPigeon/Service.h>
#include <KNSoft/ZPigeon/System.h>
#include <KNSoft/ZPigeon/Terminal.h>

EXTERN_C_START

#define ZP_CLIENT_DEFAULT_CONNECT_TIMEOUT_MILLISECONDS 10000
#define ZP_CLIENT_DEFAULT_RETRY_MAX_MILLISECONDS 60000
#define ZP_CLIENT_DEFAULT_STABLE_RESET_MILLISECONDS 60000
#define ZP_CLIENT_DEFAULT_RETRY_JITTER_PERCENT 20
#define ZP_CLIENT_DEFAULT_KEY_NAME L"KNSoft.ZPigeon.Client"
#define ZP_CLIENT_DEFAULT_CHANNEL_WINDOW_SIZE ZP_CHANNEL_DATA_MAX_SIZE

typedef enum _ZP_CLIENT_STATE
{
    ZpClientStateStopped,
    ZpClientStateConnecting,
    ZpClientStateAuthenticating,
    ZpClientStateReady,
    ZpClientStateRetryWait,
    ZpClientStateStopping
} ZP_CLIENT_STATE, *PZP_CLIENT_STATE;

typedef
VOID
(NTAPI *ZP_CLIENT_STATE_CALLBACK)(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_CLIENT_PONG_CALLBACK)(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONGLONG Token,
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
(NTAPI *ZP_SYSTEM_INFO_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ const ZP_SYSTEM_INFO_VIEW* Info,
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
(NTAPI *ZP_EVENT_LOG_QUERY_PAGE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ const ZP_EVENT_LOG_PAGE_VIEW* Page,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_EVENT_LOG_SUBSCRIBE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ ZP_SUBSCRIPTION_HANDLE Subscription,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_EVENT_LOG_RECORD_CALLBACK)(
    _In_ ZP_SUBSCRIPTION_HANDLE Subscription,
    _In_ ULONGLONG Sequence,
    _In_ const ZP_EVENT_LOG_RECORD_VIEW* Record,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_EVENT_LOG_TERMINAL_CALLBACK)(
    _In_ ZP_SUBSCRIPTION_HANDLE Subscription,
    _In_ ULONGLONG NextSequence,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_STRING_VIEW LastBookmark,
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
(NTAPI *ZP_TERMINAL_CREATE_CALLBACK)(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG ProcessId,
    _In_opt_ PVOID Context);

typedef struct _ZP_CLIENT_CONFIG
{
    ULONG Size;
    PCZP_ENDPOINT Endpoints;
    ULONG EndpointCount;
    const BYTE* DeploymentRootCertificate;
    ULONG DeploymentRootCertificateLength;
    PCWSTR ClientKeyName;
    PCZP_MODULE_RECORD Modules;
    USHORT ModuleCount;
    ULONG ConnectTimeoutMilliseconds;
    ZP_CLIENT_STATE_CALLBACK StateCallback;
    ZP_CLIENT_PONG_CALLBACK PongCallback;
    PVOID CallbackContext;
} ZP_CLIENT_CONFIG, *PZP_CLIENT_CONFIG;

typedef const ZP_CLIENT_CONFIG* PCZP_CLIENT_CONFIG;

NTSTATUS
NTAPI
ZpClient_Create(
    _In_ PCZP_CLIENT_CONFIG Config,
    _Out_ ZP_CLIENT_HANDLE* Client);

NTSTATUS
NTAPI
ZpClient_Start(
    _In_ ZP_CLIENT_HANDLE Client);

NTSTATUS
NTAPI
ZpClient_Stop(
    _In_ ZP_CLIENT_HANDLE Client);

NTSTATUS
NTAPI
ZpClient_Ping(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONGLONG Token);

NTSTATUS
NTAPI
ZpClient_SendRequest(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ USHORT ModuleId,
    _In_ USHORT OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_GetSystemInfo(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_SYSTEM_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_EnumerateProcesses(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_PROCESS_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_QueryProcess(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONG ProcessId,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_PROCESS_QUERY_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_TerminateProcess(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONG ProcessId,
    _In_ ULONG ExitCode,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_EnumerateServices(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_SERVICE_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_QueryService(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_SERVICE_QUERY_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_StartService(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_StopService(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_QueryFile(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_QUERY_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_EnumerateFiles(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_EnumerateFilesPage(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _In_ ULONG MaxEntries,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_ENUMERATE_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_HashFile(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_FILE_HASH_ALGORITHM Algorithm,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_HASH_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_QueryEventLogPage(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_EVENT_LOG_START_MODE StartMode,
    _In_ ULONG MaxEvents,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_reads_opt_(BookmarkLength) PCWCH Bookmark,
    _In_ ULONG BookmarkLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EVENT_LOG_QUERY_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_SubscribeEventLog(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_EVENT_LOG_START_MODE StartMode,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_reads_opt_(BookmarkLength) PCWCH Bookmark,
    _In_ ULONG BookmarkLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EVENT_LOG_SUBSCRIBE_CALLBACK SubscribeCallback,
    _In_ ZP_EVENT_LOG_RECORD_CALLBACK RecordCallback,
    _In_ ZP_EVENT_LOG_TERMINAL_CALLBACK TerminalCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_OpenFileRead(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG Offset,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_OPEN_READ_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_OpenFileWrite(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG FileSize,
    _In_ ZP_FILE_CREATE_DISPOSITION Disposition,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_OPEN_WRITE_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_CreateTerminal(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _In_reads_(CommandLineLength) PCWCH CommandLine,
    _In_ ULONG CommandLineLength,
    _In_reads_opt_(WorkingDirectoryLength) PCWCH WorkingDirectory,
    _In_ ULONG WorkingDirectoryLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_TERMINAL_CREATE_CALLBACK CreateCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpClient_ResizeTerminal(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

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

NTSTATUS
NTAPI
ZpSubscription_Cancel(
    _In_ ZP_SUBSCRIPTION_HANDLE Subscription);

VOID
NTAPI
ZpSubscription_Close(
    _In_ ZP_SUBSCRIPTION_HANDLE Subscription);

NTSTATUS
NTAPI
ZpClient_Close(
    _In_ ZP_CLIENT_HANDLE Client);

EXTERN_C_END
