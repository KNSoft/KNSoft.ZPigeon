#pragma once

#include "../Core/Certificate.h"
#include "../Core/Session.h"
#include "../../Network/Udp.h"

struct _ZP_CLIENT_OBJECT;

typedef struct _ZP_CLIENT_UDP_TRANSPORT
{
    struct _ZP_CLIENT_OBJECT* Owner;
    ULONG EndpointIndex;
    ZP_STATUS ShutdownStatus;
    HANDLE StopEvent;
    HANDLE WorkerThread;
    SOCKET Socket;
    CredHandle Credential;
    LOGICAL CredentialInitialized;
    LOGICAL WsaInitialized;
    LOGICAL ConnectionInitialized;
    ZP_CERTIFICATE_VALIDATOR CertificateValidator;
    ZP_CLIENT_SESSION Session;
    ZP_UDP_CONNECTION Connection;
} ZP_CLIENT_UDP_TRANSPORT, *PZP_CLIENT_UDP_TRANSPORT;

VOID
ZpClientUdp_Configure(
    _Inout_ struct _ZP_CLIENT_OBJECT* Object);

VOID
ZpClientUdp_Uninitialize(
    _Inout_ PZP_CLIENT_UDP_TRANSPORT Transport);
