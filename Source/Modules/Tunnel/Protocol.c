#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Tunnel.h"

static
LOGICAL
ZpTunnel_IsHostValid(
    _In_reads_(HostLength) PCWCH Host,
    _In_ ULONG HostLength)
{
    ULONG Index;

    if (Host == NULL || HostLength == 0 || HostLength > ZP_TUNNEL_HOST_MAX_LENGTH) return FALSE;
    for (Index = 0; Index < HostLength; Index++)
    {
        if (Host[Index] == UNICODE_NULL) return FALSE;
    }
    return TRUE;
}

NTSTATUS
ZpTunnel_EncodeOpen(
    _In_reads_(HostLength) PCWCH Host,
    _In_ ULONG HostLength,
    _In_ USHORT Port,
    _In_ USHORT Protocol,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (!ZpTunnel_IsHostValid(Host, HostLength) || Port == 0 ||
        Protocol != ZP_TUNNEL_PROTOCOL_TCP && Protocol != ZP_TUNNEL_PROTOCOL_UDP)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteString(&Writer, Host, HostLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, Port);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, Protocol);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpTunnel_DecodeOpen(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_TUNNEL_OPEN_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, &View->Host);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &View->Port);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &View->Protocol);
    return NT_SUCCESS(Status) &&
           (!ZpTunnel_IsHostValid((PCWCH)View->Host.Buffer, View->Host.Length) || View->Port == 0 ||
            View->Protocol != ZP_TUNNEL_PROTOCOL_TCP && View->Protocol != ZP_TUNNEL_PROTOCOL_UDP ||
            Reader.Offset != PayloadLength) ?
               STATUS_DATA_ERROR : Status;
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
