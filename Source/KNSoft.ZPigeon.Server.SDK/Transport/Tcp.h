#pragma once

#include <KNSoft/ZPigeon/SDK.h>

#include "../../Network/Tcp.h"

struct _ZP_SERVER_OBJECT;

typedef struct _ZP_SERVER_TCP_TRANSPORT
{
    struct _ZP_SERVER_OBJECT* Owner;
    LOGICAL WsaInitialized;
    LOGICAL CredentialInitialized;
    LOGICAL Stopping;
    LOGICAL AcceptStopped;
    ULONG ActiveConnectionCount;
    HANDLE StopEvent;
    HANDLE CompletionPort;
    HANDLE WorkerThread;
    HANDLE AcceptThread;
    CredHandle Credential;
    SOCKET Listeners[ZP_LISTENER_MAX_COUNT];
    WSAPOLLFD PollDescriptors[ZP_LISTENER_MAX_COUNT];
    ULONG ListenerCount;
    LIST_ENTRY Connections;
} ZP_SERVER_TCP_TRANSPORT, *PZP_SERVER_TCP_TRANSPORT;

VOID
ZpServerTcp_Configure(
    _Inout_ struct _ZP_SERVER_OBJECT* Object);

VOID
ZpServerTcp_Uninitialize(
    _Inout_ PZP_SERVER_TCP_TRANSPORT Transport);
