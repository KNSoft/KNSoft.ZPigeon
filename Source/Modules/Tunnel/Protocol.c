#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Tunnel.h"

NTSTATUS
ZpTunnel_EncodeOpen(
    _In_ USHORT Port,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;

    if (Port == 0) return STATUS_INVALID_PARAMETER;
    *BytesWritten = sizeof(Port);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < sizeof(Port)) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteUInt16(&Writer, Port);
}

NTSTATUS
ZpTunnel_DecodeOpen(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PUSHORT Port)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt16(&Reader, Port);
    return NT_SUCCESS(Status) && (*Port == 0 || Reader.Offset != PayloadLength) ? STATUS_DATA_ERROR : Status;
}

NTSTATUS
ZpTunnel_EncodeOpenResponse(
    _In_ ULONGLONG ChannelId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;

    if (ChannelId == 0) return STATUS_INVALID_PARAMETER;
    *BytesWritten = sizeof(ChannelId);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < sizeof(ChannelId)) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteUInt64(&Writer, ChannelId);
}

NTSTATUS
ZpTunnel_DecodeOpenResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG ChannelId)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, ChannelId);
    return NT_SUCCESS(Status) && (*ChannelId == 0 || Reader.Offset != PayloadLength) ? STATUS_DATA_ERROR : Status;
}
