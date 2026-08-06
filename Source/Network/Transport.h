#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

typedef BYTE ZP_SEND_FLAGS, *PZP_SEND_FLAGS;

#define ZP_SEND_FLAG_COMPRESSIBLE 0x01
#define ZP_SEND_FLAG_BULK 0x02
#define ZP_SEND_FLAG_SENSITIVE 0x04
#define ZP_SEND_FLAG_INTERACTIVE 0x08
#define ZP_SEND_VALID_FLAGS \
    (ZP_SEND_FLAG_COMPRESSIBLE | ZP_SEND_FLAG_BULK | ZP_SEND_FLAG_SENSITIVE | \
     ZP_SEND_FLAG_INTERACTIVE)

typedef
ZP_STATUS
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
    _In_ ZP_SEND_FLAGS SendFlags,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength);

typedef struct _ZP_TRANSPORT_OPERATIONS
{
    ZP_TRANSPORT_START_ROUTINE Start;
    ZP_TRANSPORT_STOP_ROUTINE Stop;
    ZP_TRANSPORT_SEND_ROUTINE Send;
} ZP_TRANSPORT_OPERATIONS;

typedef const ZP_TRANSPORT_OPERATIONS* PCZP_TRANSPORT_OPERATIONS;
