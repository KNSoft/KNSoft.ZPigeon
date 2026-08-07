#include "Include/KNSoft/ZPigeon/File.h"

NTSTATUS
ZpFile_EncodePath(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;

    if (PathLength == 0 ||
        PathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Path == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(ULONG) + (ULONGLONG)PathLength * sizeof(WCHAR);
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
    return ZpCodec_WriteString(&Writer, Path, PathLength);
}

NTSTATUS
ZpFile_DecodePath(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, Path);
    if (!NT_SUCCESS(Status) || Path->Length == 0 || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_EncodeInfo(
    _In_ PCZP_FILE_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    *BytesWritten = sizeof(ULONG) + 4 * sizeof(ULONGLONG);
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < *BytesWritten)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, Info->Attributes);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, Info->Size);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, Info->CreationTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, Info->LastAccessTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, Info->LastWriteTime);
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_INFO Info)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != sizeof(ULONG) + 4 * sizeof(ULONGLONG))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &Info->Attributes);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, &Info->Size);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, &Info->CreationTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, &Info->LastAccessTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, &Info->LastWriteTime);
    }
    return Status;
}
