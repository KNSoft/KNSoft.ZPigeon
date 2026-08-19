#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_ENDPOINT_MAX_COUNT 64
#define ZP_LISTENER_MAX_COUNT 64
#define ZP_DEPLOYMENT_MAX_COUNT 64
#define ZP_CERTIFICATE_MAX_SIZE 0x00100000UL

typedef struct _ZP_CLIENT_OBJECT* ZP_CLIENT_HANDLE;
typedef struct _ZP_SERVER_OBJECT* ZP_SERVER_HANDLE;
typedef struct _ZP_CONNECTION_OBJECT* ZP_CONNECTION_HANDLE;
typedef struct _ZP_REQUEST_OBJECT* ZP_REQUEST_HANDLE;
typedef struct _ZP_CHANNEL_OBJECT* ZP_CHANNEL_HANDLE;

typedef enum _ZP_TRANSPORT_TYPE
{
    ZpTransportQuic = 1,
    ZpTransportTcp,
    ZpTransportUdp,
    ZpTransportWss,
    ZpTransportCount
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
