#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

DECLARE_HANDLE(ZP_CLIENT_HANDLE);
DECLARE_HANDLE(ZP_SERVER_HANDLE);
DECLARE_HANDLE(ZP_CONNECTION_HANDLE);
DECLARE_HANDLE(ZP_REQUEST_HANDLE);
DECLARE_HANDLE(ZP_CHANNEL_HANDLE);

typedef enum _ZP_TRANSPORT_TYPE
{
    ZpTransportQuic = 1,
    ZpTransportTlsTcp = 2,
    ZpTransportWss = 3
} ZP_TRANSPORT_TYPE, *PZP_TRANSPORT_TYPE;

typedef enum _ZP_CONNECTION_PHASE
{
    ZpConnectionPhaseConnecting,
    ZpConnectionPhaseAuthenticating,
    ZpConnectionPhaseReady,
    ZpConnectionPhaseClosed
} ZP_CONNECTION_PHASE, *PZP_CONNECTION_PHASE;

typedef struct _ZP_ENDPOINT
{
    ZP_TRANSPORT_TYPE Transport;
    PCWSTR Host;
    USHORT Port;
    PCWSTR ServerName;
    PCWSTR WssPath;
} ZP_ENDPOINT, *PZP_ENDPOINT;

typedef const ZP_ENDPOINT* PCZP_ENDPOINT;

typedef struct _ZP_LISTENER_ENDPOINT
{
    ZP_TRANSPORT_TYPE Transport;
    PCWSTR Host;
    USHORT Port;
    PCWSTR WssPath;
} ZP_LISTENER_ENDPOINT, *PZP_LISTENER_ENDPOINT;

typedef const ZP_LISTENER_ENDPOINT* PCZP_LISTENER_ENDPOINT;

EXTERN_C_END
