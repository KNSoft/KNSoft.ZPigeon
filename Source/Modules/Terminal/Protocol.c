#include "Include/KNSoft/ZPigeon/Terminal.h"

#include "../../KNSoft.ZPigeon.Protocol/Core/Protocol.inl"

NTSTATUS
ZpTerminal_EncodeCreate(
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _In_ PCZP_EXECUTION_START Start,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONG StartLength;
    NTSTATUS Status;

    if (Columns == 0 || Rows == 0 || Start == NULL || Start->Engine != ZpExecutionEngineCreateProcess)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpExecution_EncodeStart(Start, NULL, 0, &StartLength);
    if (!NT_SUCCESS(Status)) return Status;
    if (StartLength > ZP_RESPONSE_MAX_PAYLOAD_SIZE - 2 * sizeof(USHORT)) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = 2 * sizeof(USHORT) + StartLength;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteUInt16(&Cursor, Columns);
    ZpWire_WriteUInt16(&Cursor, Rows);
    return ZpExecution_EncodeStart(Start,
                                   Cursor,
                                   BufferSize - 2 * sizeof(USHORT),
                                   &StartLength);
}

NTSTATUS
ZpTerminal_DecodeCreate(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_TERMINAL_CREATE_VIEW View)
{
    ZP_CODEC_READER Reader;
    ZP_BUFFER_VIEW Start;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt16(&Reader, &View->Columns);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt16(&Reader, &View->Rows);
    }
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadData(&Reader, PayloadLength - Reader.Offset, &Start);
    if (NT_SUCCESS(Status)) Status = ZpExecution_DecodeStart(Start.Buffer, Start.Length, &View->Start);
    if (!NT_SUCCESS(Status) ||
        View->Columns == 0 ||
        View->Rows == 0 ||
        View->Start.Engine != ZpExecutionEngineCreateProcess ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpTerminal_EncodeCreateResponse(
    _In_ ULONG ChannelId,
    _In_ ULONG ProcessId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (ChannelId == 0 || ProcessId == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = 2 * sizeof(ULONG);
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < *BytesWritten)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, ChannelId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, ProcessId);
    }
    return Status;
}

NTSTATUS
ZpTerminal_DecodeCreateResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ChannelId,
    _Out_ PULONG ProcessId)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != 2 * sizeof(ULONG))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, ChannelId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, ProcessId);
    }
    if (NT_SUCCESS(Status) &&
        (*ChannelId == 0 || *ProcessId == 0))
    {
        Status = STATUS_DATA_ERROR;
    }
    return Status;
}

NTSTATUS
ZpTerminal_EncodeResize(
    _In_ ULONG ChannelId,
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (ChannelId == 0 || Columns == 0 || Rows == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = sizeof(ULONG) + 2 * sizeof(USHORT);
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < *BytesWritten)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, ChannelId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt16(&Writer, Columns);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt16(&Writer, Rows);
    }
    return Status;
}

NTSTATUS
ZpTerminal_DecodeResize(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ChannelId,
    _Out_ PUSHORT Columns,
    _Out_ PUSHORT Rows)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != sizeof(ULONG) + 2 * sizeof(USHORT))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, ChannelId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt16(&Reader, Columns);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt16(&Reader, Rows);
    }
    if (NT_SUCCESS(Status) &&
        (*ChannelId == 0 ||
         *Columns == 0 ||
         *Rows == 0))
    {
        Status = STATUS_DATA_ERROR;
    }
    return Status;
}

NTSTATUS
ZpTerminal_EncodeShells(
    _In_ BYTE Shells,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;

    if ((Shells & ~ZP_TERMINAL_SHELL_MASK) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = sizeof(BYTE);
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < sizeof(BYTE))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteByte(&Writer, Shells);
}

NTSTATUS
ZpTerminal_DecodeShells(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PBYTE Shells)
{
    ZP_CODEC_READER Reader;

    if (PayloadLength != sizeof(BYTE))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    if (!NT_SUCCESS(ZpCodec_ReadByte(&Reader, Shells)) ||
        (*Shells & ~ZP_TERMINAL_SHELL_MASK) != 0)
    {
        return STATUS_DATA_ERROR;
    }
    return STATUS_SUCCESS;
}
