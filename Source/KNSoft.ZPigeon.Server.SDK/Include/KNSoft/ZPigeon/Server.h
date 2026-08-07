#pragma once

#include <KNSoft/ZPigeon/SDK.h>

#include <Wincrypt.h>

#pragma comment(lib, "Crypt32.lib")

EXTERN_C_START

#define ZP_SERVER_DEFAULT_MAX_REQUESTS_PER_CONNECTION 64
#define ZP_SERVER_MAX_REQUESTS_PER_CONNECTION 4096
#define ZP_SERVER_DEFAULT_MAX_REQUEST_PAYLOAD_BYTES_PER_CONNECTION 0x04000000UL
#define ZP_SERVER_MAX_REQUEST_PAYLOAD_BYTES_PER_CONNECTION 0x40000000UL
#define ZP_SERVER_DEFAULT_MAX_CHANNELS_PER_CONNECTION 16
#define ZP_SERVER_MAX_CHANNELS_PER_CONNECTION 1024
#define ZP_SERVER_DEFAULT_MAX_SUBSCRIPTIONS_PER_CONNECTION 16
#define ZP_SERVER_MAX_SUBSCRIPTIONS_PER_CONNECTION 1024

typedef enum _ZP_SERVER_STATE
{
    ZpServerStateStopped,
    ZpServerStateStarting,
    ZpServerStateRunning,
    ZpServerStateStopping
} ZP_SERVER_STATE, *PZP_SERVER_STATE;

typedef enum _ZP_REQUEST_ACCESS
{
    ZpRequestAccessRead = 1,
    ZpRequestAccessControl = 2
} ZP_REQUEST_ACCESS, *PZP_REQUEST_ACCESS;

typedef
VOID
(NTAPI *ZP_SERVER_STATE_CALLBACK)(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_SERVER_STATE State,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_SERVER_CONNECTION_CALLBACK)(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_CONNECTION_PHASE Phase,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context);

typedef
NTSTATUS
(NTAPI *ZP_SERVER_AUTHORIZE_CALLBACK)(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ZP_CLIENT_ID_SIZE) const BYTE ClientId[ZP_CLIENT_ID_SIZE],
    _In_ ZP_REQUEST_ACCESS Access,
    _In_ USHORT ModuleId,
    _In_ USHORT OperationId,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context);

typedef struct _ZP_SERVER_DEPLOYMENT
{
    PCWSTR ServerName;
    PCCERT_CONTEXT Certificate;
} ZP_SERVER_DEPLOYMENT, *PZP_SERVER_DEPLOYMENT;

typedef const ZP_SERVER_DEPLOYMENT* PCZP_SERVER_DEPLOYMENT;

typedef struct _ZP_SERVER_CONFIG
{
    ULONG Size;
    PCZP_LISTENER_ENDPOINT Listeners;
    ULONG ListenerCount;
    PCZP_SERVER_DEPLOYMENT Deployments;
    ULONG DeploymentCount;
    PCZP_MODULE_RECORD Modules;
    USHORT ModuleCount;
    ULONG MaxRequestsPerConnection;
    ULONG MaxRequestPayloadBytesPerConnection;
    ULONG MaxChannelsPerConnection;
    ULONG MaxSubscriptionsPerConnection;
    ZP_SERVER_STATE_CALLBACK StateCallback;
    ZP_SERVER_CONNECTION_CALLBACK ConnectionCallback;
    PVOID CallbackContext;
    ZP_SERVER_AUTHORIZE_CALLBACK AuthorizeCallback;
} ZP_SERVER_CONFIG, *PZP_SERVER_CONFIG;

typedef const ZP_SERVER_CONFIG* PCZP_SERVER_CONFIG;

NTSTATUS
NTAPI
ZpServer_Create(
    _In_ PCZP_SERVER_CONFIG Config,
    _Out_ ZP_SERVER_HANDLE* Server);

NTSTATUS
NTAPI
ZpServer_Start(
    _In_ ZP_SERVER_HANDLE Server);

NTSTATUS
NTAPI
ZpServer_Stop(
    _In_ ZP_SERVER_HANDLE Server);

NTSTATUS
NTAPI
ZpServer_Close(
    _In_ ZP_SERVER_HANDLE Server);

EXTERN_C_END
