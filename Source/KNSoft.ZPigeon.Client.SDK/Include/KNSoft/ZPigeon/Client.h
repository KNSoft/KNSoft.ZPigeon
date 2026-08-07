#pragma once

#include <KNSoft/ZPigeon/SDK.h>
#include <KNSoft/ZPigeon/Process.h>
#include <KNSoft/ZPigeon/System.h>

EXTERN_C_START

#define ZP_CLIENT_DEFAULT_CONNECT_TIMEOUT_MILLISECONDS 10000
#define ZP_CLIENT_DEFAULT_RETRY_MAX_MILLISECONDS 60000
#define ZP_CLIENT_DEFAULT_STABLE_RESET_MILLISECONDS 60000
#define ZP_CLIENT_DEFAULT_RETRY_JITTER_PERCENT 20
#define ZP_CLIENT_DEFAULT_KEY_NAME L"KNSoft.ZPigeon.Client"

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
ZpRequest_Cancel(
    _In_ ZP_REQUEST_HANDLE Request);

VOID
NTAPI
ZpRequest_Close(
    _In_ ZP_REQUEST_HANDLE Request);

NTSTATUS
NTAPI
ZpClient_Close(
    _In_ ZP_CLIENT_HANDLE Client);

EXTERN_C_END
