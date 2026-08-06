#pragma once

#include <KNSoft.Quic.h>

#include <Wincrypt.h>
#include <Ncrypt.h>

#include "../Core/Certificate.h"
#include "../Core/Session.h"

struct _ZP_CLIENT_OBJECT;

typedef struct _ZP_CLIENT_QUIC_TRANSPORT
{
    struct _ZP_CLIENT_OBJECT* Owner;
    BOOLEAN Initialized;
    ULONG EndpointIndex;
    ZP_STATUS ShutdownStatus;
    HQUIC Registration;
    HQUIC Configuration;
    HQUIC Connection;
    HQUIC Stream;
    ZP_CERTIFICATE_VALIDATOR CertificateValidator;
    ZP_CLIENT_SESSION Session;
} ZP_CLIENT_QUIC_TRANSPORT, *PZP_CLIENT_QUIC_TRANSPORT;

VOID
ZpClientQuic_Configure(
    _Inout_ struct _ZP_CLIENT_OBJECT* Object);

VOID
ZpClientQuic_Uninitialize(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport);
