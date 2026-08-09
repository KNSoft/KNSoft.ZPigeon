#pragma once

#include <KNSoft.Quic.h>

struct _ZP_SERVER_OBJECT;
struct _ZP_SERVER_QUIC_TRANSPORT;

typedef struct _ZP_SERVER_QUIC_LISTENER
{
    struct _ZP_SERVER_QUIC_TRANSPORT* Transport;
    HQUIC Handle;
} ZP_SERVER_QUIC_LISTENER, *PZP_SERVER_QUIC_LISTENER;

typedef struct _ZP_SERVER_QUIC_TRANSPORT
{
    struct _ZP_SERVER_OBJECT* Owner;
    LOGICAL Initialized;
    LOGICAL Stopping;
    ULONG StartedListenerCount;
    ULONG StoppedListenerCount;
    ULONG ActiveConnectionCount;
    HQUIC Registration;
    HQUIC Configurations[ZP_DEPLOYMENT_MAX_COUNT];
    PSTR ServerNames[ZP_DEPLOYMENT_MAX_COUNT];
    ZP_SERVER_QUIC_LISTENER Listeners[ZP_LISTENER_MAX_COUNT];
} ZP_SERVER_QUIC_TRANSPORT, *PZP_SERVER_QUIC_TRANSPORT;

VOID
ZpServerQuic_Configure(
    _Inout_ struct _ZP_SERVER_OBJECT* Object);

VOID
ZpServerQuic_Uninitialize(
    _Inout_ PZP_SERVER_QUIC_TRANSPORT Transport);
