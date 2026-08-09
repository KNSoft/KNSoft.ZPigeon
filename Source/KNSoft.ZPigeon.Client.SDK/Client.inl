#pragma once

#include <KNSoft/ZPigeon/Client.h>
#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#include "../Network/Transport.h"
#include "Transport/Quic.h"

typedef struct _ZP_CLIENT_OBJECT
{
    RTL_SRWLOCK Lock;
    ZP_CLIENT_STATE State;
    ULONG CallbackCount;
    LIST_ENTRY InboundRequests;
    LIST_ENTRY LocalChannels;
    ULONGLONG HighestInboundRequestId;
    ULONGLONG NextLocalChannelId;
    ULONG InboundRequestCount;
    ULONGLONG InboundRequestPayloadBytes;
    ULONG LocalChannelCount;
    ZP_CLIENT_CONFIG Config;
    PCZP_TRANSPORT_OPERATIONS TransportOperations[ZpTransportWss + 1];
    PVOID TransportContexts[ZpTransportWss + 1];
    ZP_TRANSPORT_TYPE ActiveTransport;
    ULONG EndpointIndex;
    ULONG NextEndpointIndex;
    ULONG FailureRound;
    ULONGLONG ReadyTickCount;
    PTP_TIMER RetryTimer;
    LOGICAL StartPending;
    LOGICAL RetryPending;
    ULONG RetryDelay;
    ZP_CLIENT_QUIC_TRANSPORT QuicTransport;
} ZP_CLIENT_OBJECT, *PZP_CLIENT_OBJECT;

NTSTATUS
ZpClient_SetTransport(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_TRANSPORT_TYPE Transport,
    _In_ PCZP_TRANSPORT_OPERATIONS Operations,
    _In_opt_ PVOID Context);

VOID
ZpClient_TransportShutdown(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ NTSTATUS Status);

NTSTATUS
ZpClient_NotifyState(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ NTSTATUS Status);

NTSTATUS
ZpClient_NotifyPong(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONGLONG Token);

NTSTATUS
ZpClient_QueueRequest(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ PCZP_REQUEST_VIEW Request);

NTSTATUS
ZpClient_CancelInboundRequest(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONGLONG RequestId);

VOID
ZpClient_CloseInboundRequests(
    _In_ ZP_CLIENT_HANDLE Client);
