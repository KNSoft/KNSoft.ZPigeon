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
ZpFile_EncodeRenameRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(NewPathLength) PCWCH NewPath,
    _In_ ULONG NewPathLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Path == NULL || PathLength == 0 || NewPath == NULL || NewPathLength == 0 ||
        PathLength > ZP_CODEC_MAX_ELEMENT_COUNT || NewPathLength > ZP_CODEC_MAX_ELEMENT_COUNT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 2 * sizeof(ULONG) +
                   ((ULONGLONG)PathLength + NewPathLength) * sizeof(WCHAR);
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
    Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    return NT_SUCCESS(Status) ?
               ZpCodec_WriteString(&Writer, NewPath, NewPathLength) :
               Status;
}

NTSTATUS
ZpFile_DecodeRenameRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PZP_STRING_VIEW NewPath)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, Path);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, NewPath);
    }
    if (!NT_SUCCESS(Status) || Path->Length == 0 || NewPath->Length == 0 || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_EncodeSetAttributesRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG Attributes,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONG PathSize;
    NTSTATUS Status;

    Status = ZpFile_EncodePath(Path, PathLength, NULL, 0, &PathSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    *BytesWritten = sizeof(ULONG) + PathSize;
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < *BytesWritten)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, Attributes);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeSetAttributesRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PULONG Attributes)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, Attributes);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, Path);
    }
    if (!NT_SUCCESS(Status) || Path->Length == 0 || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_EncodeWriteRangeRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Path == NULL || PathLength == 0 || PathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Data == NULL || DataLength == 0 || DataLength > ZP_FILE_RANGE_MAX_LENGTH)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(ULONG) + (ULONGLONG)PathLength * sizeof(WCHAR) +
                   sizeof(ULONGLONG) + sizeof(ULONG) + DataLength;
    if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Offset);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByteString(&Writer, Data, DataLength);
    return Status;
}

NTSTATUS
ZpFile_DecodeWriteRangeRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_WRITE_RANGE_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, &Request->Path);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Request->Offset);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByteString(&Reader, &Request->Data);
    if (!NT_SUCCESS(Status) || Request->Path.Length == 0 || Request->Data.Length == 0 ||
        Request->Data.Length > ZP_FILE_RANGE_MAX_LENGTH || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_EncodeEnumeratePageRequest(
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG EnumerationId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (PathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (EnumerationId != 0 && PathLength != 0) ||
        (PathLength != 0 && Path == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 2 * sizeof(ULONG) +
                   (ULONGLONG)PathLength * sizeof(WCHAR);
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
    Status = ZpCodec_WriteUInt32(&Writer, EnumerationId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeEnumeratePageRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PULONG EnumerationId)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, EnumerationId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, Path);
    }
    if (!NT_SUCCESS(Status) ||
        (*EnumerationId != 0 && Path->Length != 0) ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_EncodeOpenReadRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (PathLength == 0 ||
        PathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Path == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(ULONGLONG) +
                   sizeof(ULONG) +
                   (ULONGLONG)PathLength * sizeof(WCHAR);
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
    Status = ZpCodec_WriteUInt64(&Writer, Offset);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeOpenReadRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PULONGLONG Offset)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, Offset);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, Path);
    }
    if (!NT_SUCCESS(Status) || Path->Length == 0 || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_EncodeOpenReadResponse(
    _In_ ULONG ChannelId,
    _In_ ULONGLONG FileSize,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (ChannelId == 0 || Offset > FileSize)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = sizeof(ULONG) + 2 * sizeof(ULONGLONG);
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
        Status = ZpCodec_WriteUInt64(&Writer, FileSize);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, Offset);
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeOpenReadResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ChannelId,
    _Out_ PULONGLONG FileSize,
    _Out_ PULONGLONG Offset)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != sizeof(ULONG) + 2 * sizeof(ULONGLONG))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, ChannelId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, FileSize);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, Offset);
    }
    if (NT_SUCCESS(Status) &&
        (*ChannelId == 0 || *Offset > *FileSize))
    {
        Status = STATUS_DATA_ERROR;
    }
    return Status;
}

NTSTATUS
ZpFile_EncodeOpenWriteRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG FileSize,
    _In_ ZP_FILE_CREATE_DISPOSITION Disposition,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if ((Disposition != ZpFileCreateNew &&
         Disposition != ZpFileCreateAlways) ||
        PathLength == 0 ||
        PathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Path == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(USHORT) + sizeof(ULONGLONG) + sizeof(ULONG) +
                   (ULONGLONG)PathLength * sizeof(WCHAR);
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
    Status = ZpCodec_WriteUInt16(&Writer, (USHORT)Disposition);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, FileSize);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeOpenWriteRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PULONGLONG FileSize,
    _Out_ PZP_FILE_CREATE_DISPOSITION Disposition)
{
    ZP_CODEC_READER Reader;
    USHORT DispositionValue;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt16(&Reader, &DispositionValue);
    if (NT_SUCCESS(Status))
    {
        *Disposition = (ZP_FILE_CREATE_DISPOSITION)DispositionValue;
        Status = ZpCodec_ReadUInt64(&Reader, FileSize);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, Path);
    }
    if (!NT_SUCCESS(Status) ||
        (DispositionValue != ZpFileCreateNew &&
         DispositionValue != ZpFileCreateAlways) ||
        Path->Length == 0 ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_EncodeOpenWriteResponse(
    _In_ ULONG ChannelId,
    _In_ ULONGLONG FileSize,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (ChannelId == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = sizeof(ULONG) + sizeof(ULONGLONG);
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
        Status = ZpCodec_WriteUInt64(&Writer, FileSize);
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeOpenWriteResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ChannelId,
    _Out_ PULONGLONG FileSize)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != sizeof(ULONG) + sizeof(ULONGLONG))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, ChannelId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, FileSize);
    }
    if (NT_SUCCESS(Status) && *ChannelId == 0)
    {
        Status = STATUS_DATA_ERROR;
    }
    return Status;
}

static
ULONG
ZpFile_GetDigestLength(
    _In_ ZP_FILE_HASH_ALGORITHM Algorithm)
{
    switch (Algorithm)
    {
    case ZpFileHashCrc32:
        return ZP_FILE_CRC32_SIZE;
    case ZpFileHashMd5:
        return ZP_FILE_MD5_SIZE;
    case ZpFileHashSha1:
        return ZP_FILE_SHA1_SIZE;
    case ZpFileHashSha256:
        return ZP_FILE_SHA256_SIZE;
    default:
        return 0;
    }
}

NTSTATUS
ZpFile_EncodeHashRequest(
    _In_ ZP_FILE_HASH_ALGORITHM Algorithm,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (ZpFile_GetDigestLength(Algorithm) == 0 ||
        PathLength == 0 ||
        PathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Path == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(USHORT) + sizeof(ULONG) +
                   (ULONGLONG)PathLength * sizeof(WCHAR);
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
    Status = ZpCodec_WriteUInt16(&Writer, (USHORT)Algorithm);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeHashRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_HASH_ALGORITHM Algorithm,
    _Out_ PZP_STRING_VIEW Path)
{
    ZP_CODEC_READER Reader;
    USHORT AlgorithmValue;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt16(&Reader, &AlgorithmValue);
    if (NT_SUCCESS(Status))
    {
        *Algorithm = (ZP_FILE_HASH_ALGORITHM)AlgorithmValue;
        Status = ZpCodec_ReadString(&Reader, Path);
    }
    if (!NT_SUCCESS(Status) ||
        ZpFile_GetDigestLength((ZP_FILE_HASH_ALGORITHM)AlgorithmValue) == 0 ||
        Path->Length == 0 ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_EncodeHashResponse(
    _In_ ZP_FILE_HASH_ALGORITHM Algorithm,
    _In_ ULONGLONG FileSize,
    _In_reads_bytes_(DigestLength) const BYTE* Digest,
    _In_ ULONG DigestLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (DigestLength != ZpFile_GetDigestLength(Algorithm) || Digest == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = sizeof(USHORT) + sizeof(ULONGLONG) + DigestLength;
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < *BytesWritten)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt16(&Writer, (USHORT)Algorithm);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, FileSize);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteData(&Writer, Digest, DigestLength);
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeHashResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_HASH_VIEW View)
{
    ZP_CODEC_READER Reader;
    USHORT AlgorithmValue;
    ULONG DigestLength;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt16(&Reader, &AlgorithmValue);
    if (NT_SUCCESS(Status))
    {
        View->Algorithm = (ZP_FILE_HASH_ALGORITHM)AlgorithmValue;
        DigestLength = ZpFile_GetDigestLength(View->Algorithm);
        Status = ZpCodec_ReadUInt64(&Reader, &View->FileSize);
    }
    else
    {
        DigestLength = 0;
    }
    if (NT_SUCCESS(Status) && DigestLength != 0)
    {
        Status = ZpCodec_ReadData(&Reader, DigestLength, &View->Digest);
    }
    if (!NT_SUCCESS(Status) ||
        DigestLength == 0 ||
        Reader.Offset != PayloadLength)
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

    *BytesWritten = sizeof(ULONG) + 4 * sizeof(ULONGLONG) + sizeof(BYTE);
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
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteBoolean(&Writer, Info->HasChildren);
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

    if (PayloadLength != sizeof(ULONG) + 4 * sizeof(ULONGLONG) + sizeof(BYTE))
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
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadBoolean(&Reader, &Info->HasChildren);
    }
    return Status;
}

static
NTSTATUS
ZpFile_ReadRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_FILE_RECORD_VIEW Record)
{
    ZP_FILE_RECORD_VIEW LocalRecord;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt32(Reader, &LocalRecord.Info.Attributes);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(Reader, &LocalRecord.Info.Size);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(Reader, &LocalRecord.Info.CreationTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(Reader, &LocalRecord.Info.LastAccessTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(Reader, &LocalRecord.Info.LastWriteTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadBoolean(Reader, &LocalRecord.Info.HasChildren);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(Reader, &LocalRecord.Name);
    }
    if (NT_SUCCESS(Status) && Record != NULL)
    {
        *Record = LocalRecord;
    }
    return Status;
}

NTSTATUS
ZpFile_EncodeList(
    _In_reads_opt_(FileCount) PCZP_FILE_RECORD Files,
    _In_ ULONG FileCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize = sizeof(ULONG);
    NTSTATUS Status;
    ULONG Index;

    if (FileCount > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (FileCount != 0 && Files == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < FileCount; Index++)
    {
        if (Files[Index].NameLength == 0 ||
            Files[Index].NameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            Files[Index].Name == NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 2 * sizeof(ULONG) + sizeof(BYTE) +
                         4 * sizeof(ULONGLONG) +
                        (ULONGLONG)Files[Index].NameLength * sizeof(WCHAR);
        if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12)
        {
            return STATUS_BUFFER_OVERFLOW;
        }
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
    Status = ZpCodec_WriteArrayCount(&Writer, FileCount);
    for (Index = 0; NT_SUCCESS(Status) && Index < FileCount; Index++)
    {
        Status = ZpCodec_WriteUInt32(&Writer, Files[Index].Info.Attributes);
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteUInt64(&Writer, Files[Index].Info.Size);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteUInt64(&Writer, Files[Index].Info.CreationTime);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteUInt64(&Writer, Files[Index].Info.LastAccessTime);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteUInt64(&Writer, Files[Index].Info.LastWriteTime);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteBoolean(&Writer,
                                          Files[Index].Info.HasChildren);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteString(&Writer,
                                         Files[Index].Name,
                                         Files[Index].NameLength);
        }
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;
    ULONG Count, Index;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpFile_ReadRecord(&Reader, NULL);
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Buffer = Add2Ptr(Payload, sizeof(ULONG));
    View->Length = PayloadLength - sizeof(ULONG);
    View->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_EncodePage(
    _In_reads_opt_(FileCount) PCZP_FILE_RECORD Files,
    _In_ ULONG FileCount,
    _In_ ULONG EnumerationId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONG ListLength;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (FileCount > ZP_FILE_PAGE_COUNT ||
        (EnumerationId != 0 && FileCount != ZP_FILE_PAGE_COUNT))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpFile_EncodeList(Files, FileCount, NULL, 0, &ListLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    RequiredSize = sizeof(ULONG) + ListLength;
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
    Status = ZpCodec_WriteUInt32(&Writer, EnumerationId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeList(Files,
                                   FileCount,
                                   Add2Ptr(Buffer, Writer.Offset),
                                   BufferSize - Writer.Offset,
                                   &ListLength);
    }
    return Status;
}

NTSTATUS
ZpFile_DecodePage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_PAGE_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &View->EnumerationId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_DecodeList(Add2Ptr(Payload, Reader.Offset),
                                   PayloadLength - Reader.Offset,
                                   &View->Files);
    }
    if (NT_SUCCESS(Status) &&
        (View->Files.Count > ZP_FILE_PAGE_COUNT ||
         (View->EnumerationId != 0 &&
          View->Files.Count != ZP_FILE_PAGE_COUNT)))
    {
        Status = STATUS_DATA_ERROR;
    }
    return Status;
}

NTSTATUS
ZpFile_GetRecord(
    _In_ PCZP_FILE_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_FILE_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG CurrentIndex;

    if (Index >= List->Count)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeReader(&Reader, List->Buffer, List->Length);
    for (CurrentIndex = 0;
         NT_SUCCESS(Status) && CurrentIndex <= Index;
         CurrentIndex++)
    {
        Status = ZpFile_ReadRecord(&Reader,
                                   CurrentIndex == Index ? Record : NULL);
    }
    return Status;
}

NTSTATUS
ZpFile_EncodeVolumeInfo(
    _In_ PCZP_FILE_VOLUME_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if ((Info->LabelLength != 0 && Info->Label == NULL) ||
        (Info->FileSystemLength != 0 && Info->FileSystem == NULL) ||
        Info->LabelLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->FileSystemLength > ZP_CODEC_MAX_ELEMENT_COUNT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 9 * sizeof(ULONG) +
                   ((ULONGLONG)Info->LabelLength + Info->FileSystemLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, Info->TotalBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->FreeBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->SerialNumber);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->MaximumComponentLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->FileSystemFlags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Info->Label, Info->LabelLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Info->FileSystem, Info->FileSystemLength);
    return Status;
}

NTSTATUS
ZpFile_DecodeVolumeInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_VOLUME_INFO_VIEW Info)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, &Info->TotalBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Info->FreeBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Info->SerialNumber);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Info->MaximumComponentLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Info->FileSystemFlags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Info->Label);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Info->FileSystem);
    return NT_SUCCESS(Status) && Reader.Offset != PayloadLength ? STATUS_DATA_ERROR : Status;
}

static
NTSTATUS
ZpFile_ReadOwner(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_FILE_OWNER_RECORD_VIEW Owner)
{
    ZP_FILE_OWNER_RECORD_VIEW Local;
    ULONG Value;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt32(Reader, &Local.ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Value);
    if (NT_SUCCESS(Status)) Local.ImagePathStatus = (NTSTATUS)Value;
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Value);
    if (NT_SUCCESS(Status)) Local.CommandLineStatus = (NTSTATUS)Value;
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.ImageName);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.ImagePath);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.CommandLine);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.ServiceNames);
    if (NT_SUCCESS(Status) && Owner != NULL) *Owner = Local;
    return Status;
}

NTSTATUS
ZpFile_EncodeOwnerList(
    _In_reads_opt_(OwnerCount) PCZP_FILE_OWNER_RECORD Owners,
    _In_ ULONG OwnerCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize = sizeof(ULONG);
    NTSTATUS Status;
    ULONG Index;

    if (OwnerCount > ZP_CODEC_MAX_ELEMENT_COUNT || (OwnerCount != 0 && Owners == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < OwnerCount; Index++)
    {
        if (Owners[Index].ImageNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            Owners[Index].ImagePathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            Owners[Index].CommandLineLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            Owners[Index].ServiceNamesLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            (Owners[Index].ImageNameLength != 0 && Owners[Index].ImageName == NULL) ||
            (Owners[Index].ImagePathLength != 0 && Owners[Index].ImagePath == NULL) ||
            (Owners[Index].CommandLineLength != 0 && Owners[Index].CommandLine == NULL) ||
            (Owners[Index].ServiceNamesLength != 0 && Owners[Index].ServiceNames == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 7 * sizeof(ULONG) +
                        ((ULONGLONG)Owners[Index].ImageNameLength + Owners[Index].ImagePathLength +
                         Owners[Index].CommandLineLength + Owners[Index].ServiceNamesLength) * sizeof(WCHAR);
        if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12) return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteArrayCount(&Writer, OwnerCount);
    for (Index = 0; NT_SUCCESS(Status) && Index < OwnerCount; Index++)
    {
        Status = ZpCodec_WriteUInt32(&Writer, Owners[Index].ProcessId);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Owners[Index].ImagePathStatus);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Owners[Index].CommandLineStatus);
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteString(&Writer, Owners[Index].ImageName, Owners[Index].ImageNameLength);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteString(&Writer, Owners[Index].ImagePath, Owners[Index].ImagePathLength);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteString(&Writer, Owners[Index].CommandLine, Owners[Index].CommandLineLength);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteString(&Writer, Owners[Index].ServiceNames, Owners[Index].ServiceNamesLength);
        }
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeOwnerList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_OWNER_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;
    ULONG Count, Index;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++) Status = ZpFile_ReadOwner(&Reader, NULL);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Buffer = Add2Ptr(Payload, sizeof(ULONG));
    View->Length = PayloadLength - sizeof(ULONG);
    View->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_GetOwnerRecord(
    _In_ PCZP_FILE_OWNER_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_FILE_OWNER_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Current;

    if (Index >= List->Count) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, List->Buffer, List->Length);
    for (Current = 0; NT_SUCCESS(Status) && Current <= Index; Current++)
    {
        Status = ZpFile_ReadOwner(&Reader, Current == Index ? Record : NULL);
    }
    return Status;
}

NTSTATUS
ZpFile_EncodeOwnerControlRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_FILE_OWNER_CONTROL Control,
    _In_reads_(ProcessCount) const ULONG* ProcessIds,
    _In_ ULONG ProcessCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;
    ULONG Index;

    if (Path == NULL || PathLength == 0 || PathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (Control != ZpFileOwnerTerminate && Control != ZpFileOwnerCloseHandles) ||
        ProcessIds == NULL || ProcessCount == 0 || ProcessCount > ZP_CODEC_MAX_ELEMENT_COUNT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 3 * sizeof(ULONG) + (ULONGLONG)PathLength * sizeof(WCHAR) +
                   (ULONGLONG)ProcessCount * sizeof(ULONG);
    if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Control);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteArrayCount(&Writer, ProcessCount);
    for (Index = 0; NT_SUCCESS(Status) && Index < ProcessCount; Index++)
    {
        Status = ZpCodec_WriteUInt32(&Writer, ProcessIds[Index]);
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeOwnerControlRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_OWNER_CONTROL_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    ULONG Control;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &Control);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->Path);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadArrayCount(&Reader, &Request->ProcessCount);
    if (!NT_SUCCESS(Status)) return Status;
    if ((ULONGLONG)Reader.Offset + (ULONGLONG)Request->ProcessCount * sizeof(ULONG) != PayloadLength ||
        Request->Path.Length == 0 ||
        Request->ProcessCount == 0 || (Control != ZpFileOwnerTerminate && Control != ZpFileOwnerCloseHandles))
    {
        return STATUS_DATA_ERROR;
    }
    Request->Control = (ZP_FILE_OWNER_CONTROL)Control;
    Request->ProcessIds = Add2Ptr(Payload, Reader.Offset);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_GetOwnerControlProcessId(
    _In_ PCZP_FILE_OWNER_CONTROL_REQUEST_VIEW Request,
    _In_ ULONG Index,
    _Out_ PULONG ProcessId)
{
    ZP_CODEC_READER Reader;

    if (Index >= Request->ProcessCount) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Request->ProcessIds, Request->ProcessCount * sizeof(ULONG));
    Reader.Offset = Index * sizeof(ULONG);
    return ZpCodec_ReadUInt32(&Reader, ProcessId);
}

NTSTATUS
ZpFile_EncodeOwnerControlResults(
    _In_reads_(ResultCount) PCZP_FILE_OWNER_CONTROL_RESULT Results,
    _In_ ULONG ResultCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;
    ULONG Index;

    if (Results == NULL || ResultCount == 0 || ResultCount > ZP_CODEC_MAX_ELEMENT_COUNT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = sizeof(ULONG) + ResultCount * 3 * sizeof(ULONG);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteArrayCount(&Writer, ResultCount);
    for (Index = 0; NT_SUCCESS(Status) && Index < ResultCount; Index++)
    {
        Status = ZpCodec_WriteUInt32(&Writer, Results[Index].ProcessId);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Results[Index].Status);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Results[Index].AffectedHandleCount);
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeOwnerControlResults(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_OWNER_CONTROL_RESULT_VIEW View)
{
    ZP_CODEC_READER Reader;
    ULONG Count;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    if (!NT_SUCCESS(Status)) return Status;
    if (Count == 0 || sizeof(ULONG) + (ULONGLONG)Count * 3 * sizeof(ULONG) != PayloadLength)
    {
        return STATUS_DATA_ERROR;
    }
    View->Buffer = Add2Ptr(Payload, sizeof(ULONG));
    View->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_GetOwnerControlResult(
    _In_ PCZP_FILE_OWNER_CONTROL_RESULT_VIEW View,
    _In_ ULONG Index,
    _Out_ PZP_FILE_OWNER_CONTROL_RESULT Result)
{
    ZP_CODEC_READER Reader;
    ULONG Value;
    NTSTATUS Status;

    if (Index >= View->Count) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, View->Buffer, View->Count * 3 * sizeof(ULONG));
    Reader.Offset = Index * 3 * sizeof(ULONG);
    Status = ZpCodec_ReadUInt32(&Reader, &Result->ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Value);
    if (NT_SUCCESS(Status)) Result->Status = (NTSTATUS)Value;
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Result->AffectedHandleCount);
    return Status;
}
