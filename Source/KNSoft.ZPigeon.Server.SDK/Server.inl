#pragma once

#include <KNSoft/ZPigeon/Server.h>

#include "../Network/Transport.h"
#include "Network/Quic.h"

typedef struct _ZP_SERVER_OBJECT
{
    RTL_SRWLOCK Lock;
    ZP_SERVER_STATE State;
    ULONG CallbackCount;
    ZP_SERVER_CONFIG Config;
    PCZP_TRANSPORT_OPERATIONS TransportOperations;
    PVOID TransportContext;
    ZP_SERVER_QUIC_TRANSPORT QuicTransport;
} ZP_SERVER_OBJECT, *PZP_SERVER_OBJECT;

NTSTATUS
ZpServer_SetTransport(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ PCZP_TRANSPORT_OPERATIONS Operations,
    _In_opt_ PVOID Context);

NTSTATUS
ZpServer_NotifyState(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_SERVER_STATE State,
    _In_ NTSTATUS Status);

VOID
ZpServer_NotifyConnection(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_CONNECTION_PHASE Phase,
    _In_ NTSTATUS Status);
