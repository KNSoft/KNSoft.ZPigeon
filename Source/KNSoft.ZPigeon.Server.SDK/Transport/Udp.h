#pragma once

#include <KNSoft/ZPigeon/SDK.h>

#include "../../Network/Udp.h"

#define ZP_SERVER_UDP_CONNECTION_BUCKET_COUNT 256

struct _ZP_SERVER_OBJECT;

typedef struct _ZP_SERVER_UDP_TRANSPORT
{
    struct _ZP_SERVER_OBJECT* Owner;
    BOOLEAN WsaInitialized;
    BOOLEAN CredentialInitialized;
    BOOLEAN Stopping;
    BOOLEAN ReceiveStopped;
    ULONG ActiveConnectionCount;
    HANDLE StopEvent;
    WSAEVENT SocketEvent;
    HANDLE WorkerThread;
    CredHandle Credential;
    SOCKET Listeners[ZP_LISTENER_MAX_COUNT];
    WSAPOLLFD PollDescriptors[ZP_LISTENER_MAX_COUNT];
    ULONG ListenerCount;
    LIST_ENTRY Connections;
    LIST_ENTRY ConnectionBuckets[ZP_SERVER_UDP_CONNECTION_BUCKET_COUNT];
} ZP_SERVER_UDP_TRANSPORT, *PZP_SERVER_UDP_TRANSPORT;

VOID
ZpServerUdp_Configure(
    _Inout_ struct _ZP_SERVER_OBJECT* Object);

VOID
ZpServerUdp_Uninitialize(
    _Inout_ PZP_SERVER_UDP_TRANSPORT Transport);
