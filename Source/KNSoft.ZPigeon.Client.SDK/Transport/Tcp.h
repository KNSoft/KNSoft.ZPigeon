#pragma once

#include "../Core/Certificate.h"
#include "../Core/Session.h"
#include "../../Network/Tcp.h"

struct _ZP_CLIENT_OBJECT;

typedef struct _ZP_CLIENT_TCP_TRANSPORT
{
    struct _ZP_CLIENT_OBJECT* Owner;
    ULONG EndpointIndex;
    ZP_STATUS ShutdownStatus;
    HANDLE StopEvent;
    HANDLE ConnectThread;
    HANDLE CompletionPort;
    HANDLE WorkerThread;
    CredHandle Credential;
    BOOLEAN CredentialInitialized;
    BOOLEAN WsaInitialized;
    BOOLEAN ConnectionInitialized;
    ZP_CERTIFICATE_VALIDATOR CertificateValidator;
    ZP_CLIENT_SESSION Session;
    ZP_TCP_CONNECTION Connection;
} ZP_CLIENT_TCP_TRANSPORT, *PZP_CLIENT_TCP_TRANSPORT;

VOID
ZpClientTcp_Configure(
    _Inout_ struct _ZP_CLIENT_OBJECT* Object);

VOID
ZpClientTcp_Uninitialize(
    _Inout_ PZP_CLIENT_TCP_TRANSPORT Transport);
