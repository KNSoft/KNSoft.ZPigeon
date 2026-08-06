#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_TUNNEL_MODULE_ID 11
#define ZP_TUNNEL_OPERATION_OPEN 1
#define ZP_TUNNEL_PROTOCOL_TCP 1
#define ZP_TUNNEL_PROTOCOL_UDP 2
#define ZP_TUNNEL_HOST_MAX_LENGTH 255
#define ZP_TUNNEL_DATAGRAM_MAX_SIZE 65507

typedef BYTE ZP_TUNNEL_PROTOCOL, *PZP_TUNNEL_PROTOCOL;

typedef struct _ZP_TUNNEL_OPEN_VIEW
{
    ZP_STRING_VIEW Host;
    USHORT Port;
    ZP_TUNNEL_PROTOCOL Protocol;
} ZP_TUNNEL_OPEN_VIEW, *PZP_TUNNEL_OPEN_VIEW;

NTSTATUS
ZpTunnel_EncodeOpen(
    _In_reads_(HostLength) PCWCH Host,
    _In_ ULONG HostLength,
    _In_ USHORT Port,
    _In_ ZP_TUNNEL_PROTOCOL Protocol,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpTunnel_DecodeOpen(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_TUNNEL_OPEN_VIEW View);

NTSTATUS
ZpTunnel_EncodeOpenResponse(
    _In_ ULONG ChannelId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpTunnel_DecodeOpenResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ChannelId);

EXTERN_C_END
