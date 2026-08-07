#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

typedef
NTSTATUS
(NTAPI *ZP_TRANSPORT_START_ROUTINE)(
    _In_opt_ PVOID Context,
    _In_ ULONG EndpointIndex);

typedef
VOID
(NTAPI *ZP_TRANSPORT_STOP_ROUTINE)(
    _In_opt_ PVOID Context);

typedef
NTSTATUS
(NTAPI *ZP_TRANSPORT_SEND_ROUTINE)(
    _In_opt_ PVOID Context,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength);

typedef struct _ZP_TRANSPORT_OPERATIONS
{
    ZP_TRANSPORT_START_ROUTINE Start;
    ZP_TRANSPORT_STOP_ROUTINE Stop;
    ZP_TRANSPORT_SEND_ROUTINE Send;
} ZP_TRANSPORT_OPERATIONS, *PZP_TRANSPORT_OPERATIONS;

typedef const ZP_TRANSPORT_OPERATIONS* PCZP_TRANSPORT_OPERATIONS;
