#pragma once

#include <KNSoft/ZPigeon/Client.h>

#include "../Network/Transport.h"
#include "Network/Quic.h"

typedef struct _ZP_CLIENT_OBJECT
{
    RTL_SRWLOCK Lock;
    ZP_CLIENT_STATE State;
    ULONG CallbackCount;
    ZP_CLIENT_CONFIG Config;
    PCZP_TRANSPORT_OPERATIONS TransportOperations;
    PVOID TransportContext;
    ZP_CLIENT_QUIC_TRANSPORT QuicTransport;
} ZP_CLIENT_OBJECT, *PZP_CLIENT_OBJECT;

NTSTATUS
ZpClient_SetTransport(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ PCZP_TRANSPORT_OPERATIONS Operations,
    _In_opt_ PVOID Context);

NTSTATUS
ZpClient_NotifyState(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ NTSTATUS Status);
