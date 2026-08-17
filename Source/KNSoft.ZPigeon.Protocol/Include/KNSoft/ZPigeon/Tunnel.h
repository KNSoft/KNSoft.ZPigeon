#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_TUNNEL_MODULE_ID 11
#define ZP_TUNNEL_MODULE_VERSION 1
#define ZP_TUNNEL_OPERATION_OPEN 1

NTSTATUS
ZpTunnel_EncodeOpen(
    _In_ USHORT Port,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpTunnel_DecodeOpen(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PUSHORT Port);

NTSTATUS
ZpTunnel_EncodeOpenResponse(
    _In_ ULONGLONG ChannelId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpTunnel_DecodeOpenResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG ChannelId);

EXTERN_C_END
