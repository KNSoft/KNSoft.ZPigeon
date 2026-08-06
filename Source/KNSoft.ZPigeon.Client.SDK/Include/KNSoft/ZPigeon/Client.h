#pragma once

#include <KNSoft/ZPigeon/SDK.h>

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
ZpClient_Close(
    _In_ ZP_CLIENT_HANDLE Client);

EXTERN_C_END
