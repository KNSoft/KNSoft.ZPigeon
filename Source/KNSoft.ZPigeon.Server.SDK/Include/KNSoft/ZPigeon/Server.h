#pragma once

#include <Wincrypt.h>

#include <KNSoft/ZPigeon/SDK.h>

EXTERN_C_START

typedef enum _ZP_SERVER_STATE
{
    ZpServerStateStopped,
    ZpServerStateStarting,
    ZpServerStateRunning,
    ZpServerStateStopping
} ZP_SERVER_STATE, *PZP_SERVER_STATE;

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
    ZP_SERVER_STATE_CALLBACK StateCallback;
    ZP_SERVER_CONNECTION_CALLBACK ConnectionCallback;
    PVOID CallbackContext;
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
