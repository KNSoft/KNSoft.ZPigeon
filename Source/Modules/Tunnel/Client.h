#pragma once

#include <KNSoft/ZPigeon/Tunnel.h>

typedef struct _ZP_CLIENT_TUNNEL_CHANNEL ZP_CLIENT_TUNNEL_CHANNEL, *PZP_CLIENT_TUNNEL_CHANNEL;
typedef struct _ZP_CLIENT_LOCAL_CHANNEL ZP_CLIENT_LOCAL_CHANNEL, *PZP_CLIENT_LOCAL_CHANNEL;

ZP_STATUS
ZpTunnel_Execute(
    _Inout_ struct _ZP_CLIENT_OBJECT* Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _In_ ULONG TimeoutMilliseconds,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_LOCAL_CHANNEL* Channel);

VOID
ZpTunnel_CommitChannel(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ LOGICAL ResponseSent);
