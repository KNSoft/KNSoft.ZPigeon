#pragma once

#include <KNSoft.Quic.h>

#include <Wincrypt.h>

struct _ZP_CLIENT_OBJECT;

typedef struct _ZP_CLIENT_QUIC_TRANSPORT
{
    struct _ZP_CLIENT_OBJECT* Owner;
    LOGICAL Initialized;
    ULONG EndpointIndex;
    NTSTATUS ShutdownStatus;
    HQUIC Registration;
    HQUIC Configuration;
    HQUIC Connection;
    HQUIC Stream;
    HCERTSTORE RootStore;
    HCERTCHAINENGINE ChainEngine;
} ZP_CLIENT_QUIC_TRANSPORT, *PZP_CLIENT_QUIC_TRANSPORT;

VOID
ZpClientQuic_Configure(
    _Inout_ struct _ZP_CLIENT_OBJECT* Object);

VOID
ZpClientQuic_Uninitialize(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport);
