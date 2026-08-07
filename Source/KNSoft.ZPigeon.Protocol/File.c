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
ZpFile_EncodeEnumeratePageRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _In_ ULONG MaxEntries,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (PathLength == 0 ||
        PathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Path == NULL ||
        CursorLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (CursorLength != 0 && Cursor == NULL) ||
        MaxEntries == 0 ||
        MaxEntries > ZP_FILE_PAGE_MAX_COUNT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 3 * sizeof(ULONG) +
                   ((ULONGLONG)PathLength + CursorLength) * sizeof(WCHAR);
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
    Status = ZpCodec_WriteUInt32(&Writer, MaxEntries);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Cursor, CursorLength);
    }
    return Status;
}

NTSTATUS
ZpFile_DecodeEnumeratePageRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PZP_STRING_VIEW Cursor,
    _Out_ PULONG MaxEntries)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, MaxEntries);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, Path);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, Cursor);
    }
    if (!NT_SUCCESS(Status) ||
        *MaxEntries == 0 ||
        *MaxEntries > ZP_FILE_PAGE_MAX_COUNT ||
        Path->Length == 0 ||
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
    _In_ ULONGLONG ChannelId,
    _In_ ULONGLONG FileSize,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (ChannelId == 0 || (ChannelId & 1) != 0 || Offset > FileSize)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = 3 * sizeof(ULONGLONG);
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < *BytesWritten)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, ChannelId);
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
    _Out_ PULONGLONG ChannelId,
    _Out_ PULONGLONG FileSize,
    _Out_ PULONGLONG Offset)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != 3 * sizeof(ULONGLONG))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, ChannelId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, FileSize);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, Offset);
    }
    if (NT_SUCCESS(Status) &&
        (*ChannelId == 0 || (*ChannelId & 1) != 0 || *Offset > *FileSize))
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
    _In_ ULONGLONG ChannelId,
    _In_ ULONGLONG FileSize,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (ChannelId == 0 || (ChannelId & 1) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = 2 * sizeof(ULONGLONG);
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < *BytesWritten)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, ChannelId);
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
    _Out_ PULONGLONG ChannelId,
    _Out_ PULONGLONG FileSize)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != 2 * sizeof(ULONGLONG))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, ChannelId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, FileSize);
    }
    if (NT_SUCCESS(Status) && (*ChannelId == 0 || (*ChannelId & 1) != 0))
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
    return Algorithm == ZpFileHashSha256 ? ZP_FILE_SHA256_SIZE : 0;
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
        RequiredSize += 2 * sizeof(ULONG) +
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
    _In_reads_opt_(NextCursorLength) PCWCH NextCursor,
    _In_ ULONG NextCursorLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONG ListLength;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (NextCursorLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (NextCursorLength != 0 &&
         (NextCursor == NULL || FileCount == 0 ||
          Files[FileCount - 1].NameLength != NextCursorLength ||
          RtlCompareMemory(Files[FileCount - 1].Name,
                           NextCursor,
                           (SIZE_T)NextCursorLength * sizeof(WCHAR)) !=
              (SIZE_T)NextCursorLength * sizeof(WCHAR))))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpFile_EncodeList(Files, FileCount, NULL, 0, &ListLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    RequiredSize = sizeof(ULONG) +
                   (ULONGLONG)NextCursorLength * sizeof(WCHAR) +
                   ListLength;
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
    Status = ZpCodec_WriteString(&Writer, NextCursor, NextCursorLength);
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
    ZP_FILE_RECORD_VIEW LastRecord;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, &View->NextCursor);
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_DecodeList(Add2Ptr(Payload, Reader.Offset),
                                   PayloadLength - Reader.Offset,
                                   &View->Files);
    }
    if (NT_SUCCESS(Status) && View->NextCursor.Length != 0)
    {
        Status = View->Files.Count != 0 ?
                     ZpFile_GetRecord(&View->Files,
                                      View->Files.Count - 1,
                                      &LastRecord) :
                     STATUS_DATA_ERROR;
        if (NT_SUCCESS(Status) &&
            (LastRecord.Name.Length != View->NextCursor.Length ||
             RtlCompareMemory(LastRecord.Name.Buffer,
                              View->NextCursor.Buffer,
                              (SIZE_T)View->NextCursor.Length * sizeof(WCHAR)) !=
                 (SIZE_T)View->NextCursor.Length * sizeof(WCHAR)))
        {
            Status = STATUS_DATA_ERROR;
        }
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
