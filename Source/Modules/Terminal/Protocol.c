#include "Include/KNSoft/ZPigeon/Terminal.h"

NTSTATUS
ZpTerminal_EncodeCreate(
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _In_reads_(CommandLineLength) PCWCH CommandLine,
    _In_ ULONG CommandLineLength,
    _In_reads_opt_(WorkingDirectoryLength) PCWCH WorkingDirectory,
    _In_ ULONG WorkingDirectoryLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Columns == 0 ||
        Rows == 0 ||
        CommandLineLength == 0 ||
        CommandLineLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        CommandLine == NULL ||
        WorkingDirectoryLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (WorkingDirectoryLength != 0 && WorkingDirectory == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 2 * sizeof(USHORT) +
                   2 * sizeof(ULONG) +
                   ((ULONGLONG)CommandLineLength + WorkingDirectoryLength) *
                       sizeof(WCHAR);
    if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12)
    {
        return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < RequiredSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt16(&Writer, Columns);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt16(&Writer, Rows);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer,
                                     CommandLine,
                                     CommandLineLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer,
                                     WorkingDirectory,
                                     WorkingDirectoryLength);
    }
    return Status;
}

NTSTATUS
ZpTerminal_DecodeCreate(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_TERMINAL_CREATE_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt16(&Reader, &View->Columns);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt16(&Reader, &View->Rows);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->CommandLine);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->WorkingDirectory);
    }
    if (!NT_SUCCESS(Status) ||
        View->Columns == 0 ||
        View->Rows == 0 ||
        View->CommandLine.Length == 0 ||
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
    _In_ ULONG Shells,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;

    if ((Shells & ~ZP_TERMINAL_SHELL_MASK) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = sizeof(ULONG);
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < sizeof(ULONG))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteUInt32(&Writer, Shells);
}

NTSTATUS
ZpTerminal_DecodeShells(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG Shells)
{
    ZP_CODEC_READER Reader;

    if (PayloadLength != sizeof(ULONG))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    if (!NT_SUCCESS(ZpCodec_ReadUInt32(&Reader, Shells)) ||
        (*Shells & ~ZP_TERMINAL_SHELL_MASK) != 0)
    {
        return STATUS_DATA_ERROR;
    }
    return STATUS_SUCCESS;
}
