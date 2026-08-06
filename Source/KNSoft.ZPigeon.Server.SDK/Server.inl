#pragma once

#include <KNSoft/ZPigeon/Server.h>

#include "../Network/Transport.h"
#include "Transport/Quic.h"
#include "Transport/Tcp.h"
#include "Transport/Udp.h"

typedef struct _ZP_SERVER_OBJECT
{
    RTL_SRWLOCK Lock;
    ZP_SERVER_STATE State;
    ULONG CallbackCount;
    ZP_SERVER_CONFIG Config;
    PCZP_TRANSPORT_OPERATIONS TransportOperations[ZpTransportCount];
    PVOID TransportContexts[ZpTransportCount];
    ULONG StartedTransports;
    ZP_STATUS StopStatus;
    ZP_SERVER_QUIC_TRANSPORT QuicTransport;
    ZP_SERVER_TCP_TRANSPORT TcpTransport;
    ZP_SERVER_UDP_TRANSPORT UdpTransport;
} ZP_SERVER_OBJECT, *PZP_SERVER_OBJECT;

NTSTATUS
ZpServer_SetTransport(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_TRANSPORT_TYPE Transport,
    _In_ PCZP_TRANSPORT_OPERATIONS Operations,
    _In_opt_ PVOID Context);

VOID
ZpServer_TransportStopped(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_TRANSPORT_TYPE Transport,
    _In_ ZP_STATUS Status);

NTSTATUS
ZpServer_NotifyState(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_SERVER_STATE State,
    _In_ ZP_STATUS Status);

VOID
ZpServer_NotifyConnection(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_CONNECTION_PHASE Phase,
    _In_ ZP_STATUS Status);
