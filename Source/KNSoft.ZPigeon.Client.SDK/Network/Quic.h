#pragma once

#include <KNSoft.Quic.h>

#include <Wincrypt.h>
#include <Ncrypt.h>

#include "../../Network/Connection.h"

struct _ZP_CLIENT_OBJECT;

typedef struct _ZP_CLIENT_QUIC_TRANSPORT
{
    struct _ZP_CLIENT_OBJECT* Owner;
    LOGICAL Initialized;
    ULONG EndpointIndex;
    ULONG NextEndpointIndex;
    ULONG FailureRound;
    ULONGLONG ReadyTickCount;
    PTP_TIMER RetryTimer;
    LOGICAL StartPending;
    LOGICAL RetryPending;
    ULONG RetryDelay;
    NTSTATUS ShutdownStatus;
    HQUIC Registration;
    HQUIC Configuration;
    HQUIC Connection;
    HQUIC Stream;
    HCERTSTORE RootStore;
    HCERTCHAINENGINE ChainEngine;
    NCRYPT_PROV_HANDLE KeyProvider;
    NCRYPT_KEY_HANDLE Key;
    NCRYPT_KEY_HANDLE ExternalKey;
    LOGICAL KeyOwned;
    BYTE PublicKey[ZP_CLIENT_PUBLIC_KEY_SIZE];
    ZP_CONNECTION ProtocolConnection;
    LOGICAL ProtocolConnectionInitialized;
} ZP_CLIENT_QUIC_TRANSPORT, *PZP_CLIENT_QUIC_TRANSPORT;

VOID
ZpClientQuic_Configure(
    _Inout_ struct _ZP_CLIENT_OBJECT* Object);

VOID
ZpClientQuic_Uninitialize(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport);
