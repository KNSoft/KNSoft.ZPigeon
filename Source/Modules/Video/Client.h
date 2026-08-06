#pragma once

#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Video.h"

typedef struct _ZP_CLIENT_VIDEO_CHANNEL ZP_CLIENT_VIDEO_CHANNEL, *PZP_CLIENT_VIDEO_CHANNEL;
typedef struct _ZP_CLIENT_LOCAL_CHANNEL ZP_CLIENT_LOCAL_CHANNEL, *PZP_CLIENT_LOCAL_CHANNEL;

ZP_STATUS
ZpVideo_Execute(
    _Inout_ struct _ZP_CLIENT_OBJECT* Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_LOCAL_CHANNEL* Channel);

VOID
ZpVideo_CommitChannel(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ LOGICAL ResponseSent);
