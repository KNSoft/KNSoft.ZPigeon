#pragma once

#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/PortableDevice.h"

typedef struct _ZP_CLIENT_OBJECT ZP_CLIENT_OBJECT, *PZP_CLIENT_OBJECT;
typedef struct _ZP_CLIENT_PORTABLE_CHANNEL ZP_CLIENT_PORTABLE_CHANNEL, *PZP_CLIENT_PORTABLE_CHANNEL;

ZP_STATUS
ZpPortable_Execute(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Inout_ volatile LONG* Pending,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_PORTABLE_CHANNEL* Channel);

VOID
ZpPortable_CommitChannel(
    _Inout_ PZP_CLIENT_PORTABLE_CHANNEL Channel,
    _In_ LOGICAL ResponseSent);
