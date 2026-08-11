#pragma once

#include <KNSoft/ZPigeon/SDK.h>

EXTERN_C_START

#define ZP_CLIENT_DEFAULT_CONNECT_TIMEOUT_MILLISECONDS 10000
#define ZP_CLIENT_DEFAULT_RETRY_MAX_MILLISECONDS 60000
#define ZP_CLIENT_DEFAULT_STABLE_RESET_MILLISECONDS 60000
#define ZP_CLIENT_DEFAULT_RETRY_JITTER_PERCENT 20
#define ZP_CLIENT_DEFAULT_KEY_NAME L"KNSoft.ZPigeon.Client"
#define ZP_CLIENT_DEFAULT_CHANNEL_WINDOW_SIZE ZP_CHANNEL_DATA_MAX_SIZE
#define ZP_CLIENT_DEFAULT_MAX_REQUESTS_PER_CONNECTION 64
#define ZP_CLIENT_MAX_REQUESTS_PER_CONNECTION 4096
#define ZP_CLIENT_DEFAULT_MAX_REQUEST_PAYLOAD_BYTES_PER_CONNECTION 0x04000000UL
#define ZP_CLIENT_MAX_REQUEST_PAYLOAD_BYTES_PER_CONNECTION 0x40000000UL
#define ZP_CLIENT_DEFAULT_MAX_CHANNELS_PER_CONNECTION 16
#define ZP_CLIENT_MAX_CHANNELS_PER_CONNECTION 1024

typedef enum _ZP_CLIENT_STATE
{
    ZpClientStateStopped,
    ZpClientStateConnecting,
    ZpClientStateAuthenticating,
    ZpClientStateReady,
    ZpClientStateRetryWait,
    ZpClientStateStopping
} ZP_CLIENT_STATE, *PZP_CLIENT_STATE;

typedef enum _ZP_CLIENT_KEY_SCOPE
{
    ZpClientKeyMachine,
    ZpClientKeyUser
} ZP_CLIENT_KEY_SCOPE, *PZP_CLIENT_KEY_SCOPE;

typedef
VOID
(NTAPI *ZP_CLIENT_STATE_CALLBACK)(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_CLIENT_PONG_CALLBACK)(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONGLONG Token,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_CLIENT_OPERATION_CALLBACK)(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ USHORT ModuleId,
    _In_ USHORT OperationId,
    _In_ ZP_STATUS Status,
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
    ZP_CLIENT_OPERATION_CALLBACK OperationCallback;
    PVOID CallbackContext;
    ULONG MaxRequestsPerConnection;
    ULONG MaxRequestPayloadBytesPerConnection;
    ULONG MaxChannelsPerConnection;
    ZP_CLIENT_KEY_SCOPE ClientKeyScope;
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
ZpClient_Close(
    _In_ ZP_CLIENT_HANDLE Client);

EXTERN_C_END
