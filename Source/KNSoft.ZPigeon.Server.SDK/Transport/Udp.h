#pragma once

#include <KNSoft/ZPigeon/SDK.h>

#include "../../Network/Udp.h"

struct _ZP_SERVER_OBJECT;

typedef struct _ZP_SERVER_UDP_TRANSPORT
{
    struct _ZP_SERVER_OBJECT* Owner;
    LOGICAL WsaInitialized;
    LOGICAL CredentialInitialized;
    LOGICAL Stopping;
    LOGICAL ReceiveStopped;
    ULONG ActiveConnectionCount;
    HANDLE StopEvent;
    HANDLE WorkerThread;
    CredHandle Credential;
    SOCKET Listeners[ZP_LISTENER_MAX_COUNT];
    WSAPOLLFD PollDescriptors[ZP_LISTENER_MAX_COUNT];
    ULONG ListenerCount;
    LIST_ENTRY Connections;
} ZP_SERVER_UDP_TRANSPORT, *PZP_SERVER_UDP_TRANSPORT;

VOID
ZpServerUdp_Configure(
    _Inout_ struct _ZP_SERVER_OBJECT* Object);

VOID
ZpServerUdp_Uninitialize(
    _Inout_ PZP_SERVER_UDP_TRANSPORT Transport);
