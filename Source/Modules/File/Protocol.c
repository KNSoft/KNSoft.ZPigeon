#include "Include/KNSoft/ZPigeon/File.h"

#include "../../KNSoft.ZPigeon.Protocol/Core/Protocol.inl"

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
    NTSTATUS Status;

    if (PathLength == 0 ||
        PathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Path == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = (ULONGLONG)PathLength * sizeof(WCHAR);
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE)
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
    Status = ZpCodec_WriteTailString(&Writer, Path, PathLength);
    return Status;
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
    Status = ZpCodec_ReadTailString(&Reader, Path);
    return NT_SUCCESS(Status) && Path->Length != 0 ? STATUS_SUCCESS :
           NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
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
    RequiredSize = sizeof(ULONG) +
                   ((ULONGLONG)PathLength + NewPathLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_REQUEST_MAX_PAYLOAD_SIZE)
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
               ZpCodec_WriteTailString(&Writer, NewPath, NewPathLength) :
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
        Status = ZpCodec_ReadTailString(&Reader, NewPath);
    }
    if (!NT_SUCCESS(Status) || Path->Length == 0 || NewPath->Length == 0 || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_EncodeSecurityDescriptor(
    _In_reads_(SddlLength) PCWCH Sddl,
    _In_ ULONG SddlLength,
    _In_ BOOLEAN DaclProtected,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Sddl == NULL || SddlLength == 0 || SddlLength > ZP_CODEC_MAX_ELEMENT_COUNT || DaclProtected > TRUE)
        return STATUS_INVALID_PARAMETER;
    RequiredSize = sizeof(BYTE) + (ULONGLONG)SddlLength * sizeof(WCHAR);
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteBoolean(&Writer, DaclProtected);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteTailString(&Writer, Sddl, SddlLength);
    return Status;
}

NTSTATUS
ZpFile_DecodeSecurityDescriptor(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SECURITY_DESCRIPTOR_VIEW Descriptor)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadBoolean(&Reader, &Descriptor->DaclProtected);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadTailString(&Reader, &Descriptor->Sddl);
    if (!NT_SUCCESS(Status) || Descriptor->Sddl.Length == 0 || Reader.Offset != PayloadLength)
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_EncodeSecurityRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(SddlLength) PCWCH Sddl,
    _In_ ULONG SddlLength,
    _In_ BOOLEAN DaclProtected,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Path == NULL || PathLength == 0 || PathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Sddl == NULL || SddlLength == 0 || SddlLength > ZP_CODEC_MAX_ELEMENT_COUNT || DaclProtected > TRUE)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(BYTE) + sizeof(ULONG) +
                   ((ULONGLONG)PathLength + SddlLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_REQUEST_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteBoolean(&Writer, DaclProtected);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteTailString(&Writer, Sddl, SddlLength);
    return Status;
}

NTSTATUS
ZpFile_DecodeSecurityRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_SECURITY_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadBoolean(&Reader, &Request->DaclProtected);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->Path);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadTailString(&Reader, &Request->Sddl);
    if (!NT_SUCCESS(Status) || Request->Path.Length == 0 || Request->Sddl.Length == 0 ||
        Reader.Offset != PayloadLength)
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
        Status = ZpCodec_WriteTailString(&Writer, Path, PathLength);
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
    return NT_SUCCESS(Status) ?
               ZpFile_DecodePath(Reader.Buffer + Reader.Offset,
                                 Reader.Size - Reader.Offset,
                                 Path) :
               Status;
}

NTSTATUS
ZpFile_EncodeImagePreviewRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_FILE_IMAGE_PREVIEW_QUALITY Quality,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONG PathSize;
    NTSTATUS Status;

    if (Quality < ZpFileImagePreviewLow || Quality > ZpFileImagePreviewHigh)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpFile_EncodePath(Path, PathLength, NULL, 0, &PathSize);
    if (!NT_SUCCESS(Status)) return Status;
    *BytesWritten = sizeof(BYTE) + PathSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteByte(&Writer, Quality);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteTailString(&Writer, Path, PathLength);
    return Status;
}

NTSTATUS
ZpFile_DecodeImagePreviewRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PZP_FILE_IMAGE_PREVIEW_QUALITY Quality)
{
    const BYTE* Buffer = Payload;
    NTSTATUS Status;

    if (PayloadLength <= sizeof(BYTE) || Buffer[0] < ZpFileImagePreviewLow ||
        Buffer[0] > ZpFileImagePreviewHigh)
    {
        return STATUS_DATA_ERROR;
    }
    Status = ZpFile_DecodePath(Buffer + sizeof(BYTE), PayloadLength - sizeof(BYTE), Path);
    if (NT_SUCCESS(Status)) *Quality = Buffer[0];
    return Status;
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
                   sizeof(ULONGLONG) + DataLength;
    if (RequiredSize > ZP_REQUEST_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Offset);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteData(&Writer, Data, DataLength);
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
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadData(&Reader, Reader.Size - Reader.Offset, &Request->Data);
    }
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
    RequiredSize = sizeof(ULONG) +
                   (ULONGLONG)PathLength * sizeof(WCHAR);
    if (RequiredSize > ZP_REQUEST_MAX_PAYLOAD_SIZE)
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
        Status = ZpCodec_WriteTailString(&Writer, Path, PathLength);
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
        Status = ZpCodec_ReadTailString(&Reader, Path);
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
ZpFile_EncodeFilteredPageRequest(
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(FilterLength) PCWCH Filter,
    _In_ ULONG FilterLength,
    _In_ WCHAR Group,
    _In_ ULONG EnumerationId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (PathLength > ZP_CODEC_MAX_ELEMENT_COUNT || FilterLength > MAX_PATH ||
        (PathLength != 0 && Path == NULL) || (FilterLength != 0 && Filter == NULL) ||
        (Group != UNICODE_NULL && Group != L'#' && (Group < L'A' || Group > L'Z')) ||
        (EnumerationId != 0 && (PathLength != 0 || FilterLength != 0 || Group != UNICODE_NULL)))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 2 * sizeof(ULONG) + sizeof(BYTE) +
                   (ULONGLONG)(PathLength + FilterLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_REQUEST_MAX_PAYLOAD_SIZE)
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
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, (BYTE)Group);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Filter, FilterLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteTailString(&Writer, Path, PathLength);
    return Status;
}

NTSTATUS
ZpFile_DecodeFilteredPageRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PZP_STRING_VIEW Filter,
    _Out_ PWCHAR Group,
    _Out_ PULONG EnumerationId)
{
    ZP_CODEC_READER Reader;
    BYTE Value;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, EnumerationId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Value);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, Filter);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadTailString(&Reader, Path);
    if (!NT_SUCCESS(Status) || Filter->Length > MAX_PATH ||
        (Value != UNICODE_NULL && Value != L'#' && (Value < L'A' || Value > L'Z')) ||
        (*EnumerationId != 0 && (Path->Length != 0 || Filter->Length != 0 || Value != UNICODE_NULL)) ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    *Group = (WCHAR)Value;
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
                   (ULONGLONG)PathLength * sizeof(WCHAR);
    if (RequiredSize > ZP_REQUEST_MAX_PAYLOAD_SIZE)
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
        Status = ZpCodec_WriteTailString(&Writer, Path, PathLength);
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
        Status = ZpCodec_ReadTailString(&Reader, Path);
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
    RequiredSize = sizeof(BYTE) + sizeof(ULONGLONG) +
                   (ULONGLONG)PathLength * sizeof(WCHAR);
    if (RequiredSize > ZP_REQUEST_MAX_PAYLOAD_SIZE)
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
    Status = ZpCodec_WriteByte(&Writer, Disposition);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, FileSize);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteTailString(&Writer, Path, PathLength);
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
    ZP_FILE_CREATE_DISPOSITION DispositionValue;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &DispositionValue);
    if (NT_SUCCESS(Status))
    {
        *Disposition = DispositionValue;
        Status = ZpCodec_ReadUInt64(&Reader, FileSize);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadTailString(&Reader, Path);
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
    RequiredSize = sizeof(BYTE) +
                   (ULONGLONG)PathLength * sizeof(WCHAR);
    if (RequiredSize > ZP_REQUEST_MAX_PAYLOAD_SIZE)
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
    Status = ZpCodec_WriteByte(&Writer, Algorithm);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteTailString(&Writer, Path, PathLength);
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
    ZP_FILE_HASH_ALGORITHM AlgorithmValue;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &AlgorithmValue);
    if (NT_SUCCESS(Status))
    {
        *Algorithm = AlgorithmValue;
        Status = ZpCodec_ReadTailString(&Reader, Path);
    }
    if (!NT_SUCCESS(Status) ||
        ZpFile_GetDigestLength(AlgorithmValue) == 0 ||
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
    *BytesWritten = sizeof(BYTE) + sizeof(ULONGLONG) + DigestLength;
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < *BytesWritten)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteByte(&Writer, Algorithm);
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
    ZP_FILE_HASH_ALGORITHM AlgorithmValue;
    ULONG DigestLength;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &AlgorithmValue);
    if (NT_SUCCESS(Status))
    {
        View->Algorithm = AlgorithmValue;
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
    PBYTE Cursor;
    ULONGLONG RequiredSize = sizeof(BYTE);
    ULONG Index;

    if (FileCount > ZP_FILE_PAGE_COUNT ||
        (FileCount != 0 && Files == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < FileCount; Index++)
    {
        if (Files[Index].NameLength == 0 ||
            Files[Index].NameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            Files[Index].Info.HasChildren > TRUE ||
            Files[Index].Name == NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 2 * sizeof(ULONG) + sizeof(BYTE) +
                         4 * sizeof(ULONGLONG) +
                        (ULONGLONG)Files[Index].NameLength * sizeof(WCHAR);
        if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE)
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

    Cursor = Buffer;
    ZpWire_WriteByte(&Cursor, (BYTE)FileCount);
    for (Index = 0; Index < FileCount; Index++)
    {
        ZpWire_WriteUInt32(&Cursor, Files[Index].Info.Attributes);
        ZpWire_WriteUInt64(&Cursor, Files[Index].Info.Size);
        ZpWire_WriteUInt64(&Cursor, Files[Index].Info.CreationTime);
        ZpWire_WriteUInt64(&Cursor, Files[Index].Info.LastAccessTime);
        ZpWire_WriteUInt64(&Cursor, Files[Index].Info.LastWriteTime);
        ZpWire_WriteByte(&Cursor, Files[Index].Info.HasChildren);
        ZpWire_WriteString(&Cursor, Files[Index].Name, Files[Index].NameLength);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_DecodeList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;
    BYTE Count;
    ULONG Index;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &Count);
    if (NT_SUCCESS(Status) && Count > ZP_FILE_PAGE_COUNT) Status = STATUS_DATA_ERROR;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpFile_ReadRecord(&Reader, NULL);
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Buffer = Add2Ptr(Payload, sizeof(BYTE));
    View->Length = PayloadLength - sizeof(BYTE);
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
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE)
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
ZpFile_GetNextRecord(
    _In_ PCZP_FILE_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_FILE_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpFile_ReadRecord(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
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
    RequiredSize = 8 * sizeof(ULONG) +
                   ((ULONGLONG)Info->LabelLength + Info->FileSystemLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
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
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteTailString(&Writer, Info->FileSystem, Info->FileSystemLength);
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
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadTailString(&Reader, &Info->FileSystem);
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
    PBYTE Cursor;
    ULONGLONG RequiredSize = sizeof(ULONG);
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
        if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, OwnerCount);
    for (Index = 0; Index < OwnerCount; Index++)
    {
        ZpWire_WriteUInt32(&Cursor, Owners[Index].ProcessId);
        ZpWire_WriteUInt32(&Cursor, (ULONG)Owners[Index].ImagePathStatus);
        ZpWire_WriteUInt32(&Cursor, (ULONG)Owners[Index].CommandLineStatus);
        ZpWire_WriteString(&Cursor, Owners[Index].ImageName, Owners[Index].ImageNameLength);
        ZpWire_WriteString(&Cursor, Owners[Index].ImagePath, Owners[Index].ImagePathLength);
        ZpWire_WriteString(&Cursor, Owners[Index].CommandLine, Owners[Index].CommandLineLength);
        ZpWire_WriteString(&Cursor, Owners[Index].ServiceNames, Owners[Index].ServiceNamesLength);
    }
    return STATUS_SUCCESS;
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
ZpFile_GetNextOwnerRecord(
    _In_ PCZP_FILE_OWNER_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_FILE_OWNER_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpFile_ReadOwner(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
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
    RequiredSize = sizeof(BYTE) + sizeof(ULONG) + (ULONGLONG)PathLength * sizeof(WCHAR) +
                   (ULONGLONG)ProcessCount * sizeof(ULONG);
    if (RequiredSize > ZP_REQUEST_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteByte(&Writer, Control);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Path, PathLength);
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
    ZP_FILE_OWNER_CONTROL Control;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &Control);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->Path);
    if (NT_SUCCESS(Status) && (Reader.Size - Reader.Offset) % sizeof(ULONG) != 0)
    {
        Status = STATUS_DATA_ERROR;
    }
    if (!NT_SUCCESS(Status)) return Status;
    Request->ProcessCount = (Reader.Size - Reader.Offset) / sizeof(ULONG);
    if (Request->Path.Length == 0 || Request->ProcessCount == 0 ||
        Request->ProcessCount > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (Control != ZpFileOwnerTerminate && Control != ZpFileOwnerCloseHandles))
    {
        return STATUS_DATA_ERROR;
    }
    Request->Control = Control;
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
    *BytesWritten = ResultCount * 3 * sizeof(ULONG);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = STATUS_SUCCESS;
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
    ULONG Count;

    if (PayloadLength == 0 || PayloadLength % (3 * sizeof(ULONG)) != 0)
    {
        return STATUS_DATA_ERROR;
    }
    Count = PayloadLength / (3 * sizeof(ULONG));
    if (Count > ZP_CODEC_MAX_ELEMENT_COUNT) return STATUS_DATA_ERROR;
    View->Buffer = Payload;
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

NTSTATUS
ZpFile_EncodeDownloadRequest(
    _In_ PCZP_FILE_DOWNLOAD_REQUEST Request,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Request == NULL ||
        (Request->Engine != ZpFileDownloadBits && Request->Engine != ZpFileDownloadWinHttp) ||
        (Request->Flags & ~ZP_FILE_DOWNLOAD_FLAG_OVERWRITE) != 0 ||
        Request->Id == NULL || Request->IdLength != ZP_FILE_DOWNLOAD_ID_LENGTH ||
        Request->Url == NULL || Request->UrlLength == 0 ||
        Request->UrlLength > ZP_FILE_DOWNLOAD_URL_MAX_LENGTH ||
        Request->Path == NULL || Request->PathLength == 0 ||
        Request->PathLength > ZP_FILE_DOWNLOAD_PATH_MAX_LENGTH)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 2 * sizeof(BYTE) + sizeof(ULONG) +
                   ((ULONGLONG)Request->IdLength + Request->UrlLength + Request->PathLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_REQUEST_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteByte(&Writer, Request->Engine);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, Request->Flags);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteData(&Writer,
                                   Request->Id,
                                   ZP_FILE_DOWNLOAD_ID_LENGTH * sizeof(WCHAR));
    }
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Request->Url, Request->UrlLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteTailString(&Writer, Request->Path, Request->PathLength);
    return Status;
}

NTSTATUS
ZpFile_DecodeDownloadRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_DOWNLOAD_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    ZP_BUFFER_VIEW Id;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &Request->Engine);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Request->Flags);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadData(&Reader,
                                  ZP_FILE_DOWNLOAD_ID_LENGTH * sizeof(WCHAR),
                                  &Id);
    }
    if (NT_SUCCESS(Status))
    {
        Request->Id.Buffer = (PCWCH)Id.Buffer;
        Request->Id.Length = ZP_FILE_DOWNLOAD_ID_LENGTH;
    }
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->Url);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadTailString(&Reader, &Request->Path);
    if (!NT_SUCCESS(Status)) return Status;
    if ((Request->Engine != ZpFileDownloadBits && Request->Engine != ZpFileDownloadWinHttp) ||
        (Request->Flags & ~ZP_FILE_DOWNLOAD_FLAG_OVERWRITE) != 0 ||
        Request->Id.Length != ZP_FILE_DOWNLOAD_ID_LENGTH ||
        Request->Url.Length == 0 || Request->Url.Length > ZP_FILE_DOWNLOAD_URL_MAX_LENGTH ||
        Request->Path.Length == 0 || Request->Path.Length > ZP_FILE_DOWNLOAD_PATH_MAX_LENGTH ||
        Reader.Offset != PayloadLength)
    {
        return STATUS_DATA_ERROR;
    }
    return STATUS_SUCCESS;
}

static
VOID
ZpFile_WriteDownloadRecord(
    _Inout_ PBYTE* Cursor,
    _In_ PCZP_FILE_DOWNLOAD_RECORD Record)
{
    ZpWire_WriteByte(Cursor, Record->Engine);
    ZpWire_WriteByte(Cursor, Record->State);
    ZpWire_WriteUInt32(Cursor, Record->Result);
    ZpWire_WriteUInt64(Cursor, Record->TransferredBytes);
    ZpWire_WriteUInt64(Cursor, Record->TotalBytes);
    ZpWire_WriteData(Cursor,
                    Record->Id,
                    ZP_FILE_DOWNLOAD_ID_LENGTH * sizeof(WCHAR));
    ZpWire_WriteString(Cursor, Record->Url, Record->UrlLength);
    ZpWire_WriteString(Cursor, Record->Path, Record->PathLength);
    ZpWire_WriteString(Cursor, Record->ErrorText, Record->ErrorTextLength);
}

static
NTSTATUS
ZpFile_ReadDownloadRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_FILE_DOWNLOAD_RECORD_VIEW Record)
{
    ZP_FILE_DOWNLOAD_RECORD_VIEW Local;
    ZP_BUFFER_VIEW Id;
    NTSTATUS Status;

    Status = ZpCodec_ReadByte(Reader, &Local.Engine);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(Reader, &Local.State);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Result);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.TransferredBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.TotalBytes);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadData(Reader,
                                  ZP_FILE_DOWNLOAD_ID_LENGTH * sizeof(WCHAR),
                                  &Id);
    }
    if (NT_SUCCESS(Status))
    {
        Local.Id.Buffer = (PCWCH)Id.Buffer;
        Local.Id.Length = ZP_FILE_DOWNLOAD_ID_LENGTH;
    }
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Url);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Path);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.ErrorText);
    if (NT_SUCCESS(Status) &&
        ((Local.Engine != ZpFileDownloadBits && Local.Engine != ZpFileDownloadWinHttp) ||
         Local.State < ZpFileDownloadQueued || Local.State > ZpFileDownloadCanceled ||
         Local.Id.Length != ZP_FILE_DOWNLOAD_ID_LENGTH ||
         Local.Url.Length == 0 || Local.Url.Length > ZP_FILE_DOWNLOAD_URL_MAX_LENGTH ||
         Local.Path.Length == 0 || Local.Path.Length > ZP_FILE_DOWNLOAD_PATH_MAX_LENGTH))
    {
        Status = STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Record != NULL) *Record = Local;
    return Status;
}

NTSTATUS
ZpFile_EncodeDownloadRecords(
    _In_reads_opt_(Count) PCZP_FILE_DOWNLOAD_RECORD Records,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = sizeof(BYTE);
    ULONG Index;

    if (Count > ZP_FILE_DOWNLOAD_MAX_COUNT || (Count != 0 && Records == NULL)) return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < Count; Index++)
    {
        PCZP_FILE_DOWNLOAD_RECORD Record = &Records[Index];

        if ((Record->Engine != ZpFileDownloadBits && Record->Engine != ZpFileDownloadWinHttp) ||
            Record->State < ZpFileDownloadQueued || Record->State > ZpFileDownloadCanceled ||
            Record->Id == NULL || Record->IdLength != ZP_FILE_DOWNLOAD_ID_LENGTH ||
            Record->Url == NULL || Record->UrlLength == 0 ||
            Record->UrlLength > ZP_FILE_DOWNLOAD_URL_MAX_LENGTH ||
            Record->Path == NULL || Record->PathLength == 0 ||
            Record->PathLength > ZP_FILE_DOWNLOAD_PATH_MAX_LENGTH ||
            Record->ErrorTextLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            (Record->ErrorTextLength != 0 && Record->ErrorText == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 2 * sizeof(BYTE) + sizeof(ULONG) + 2 * sizeof(ULONGLONG) + 3 * sizeof(ULONG) +
                        ((ULONGLONG)Record->IdLength + Record->UrlLength + Record->PathLength +
                         Record->ErrorTextLength) * sizeof(WCHAR);
    }
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteByte(&Cursor, (BYTE)Count);
    for (Index = 0; Index < Count; Index++) ZpFile_WriteDownloadRecord(&Cursor, &Records[Index]);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_DecodeDownloadRecords(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_DOWNLOAD_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    BYTE Count;
    ULONG Index;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &Count);
    if (!NT_SUCCESS(Status)) return Status;
    if (Count > ZP_FILE_DOWNLOAD_MAX_COUNT) return STATUS_DATA_ERROR;
    View->Count = Count;
    View->Buffer = Add2Ptr(Payload, Reader.Offset);
    for (Index = 0; NT_SUCCESS(Status) && Index < View->Count; Index++)
    {
        Status = ZpFile_ReadDownloadRecord(&Reader, NULL);
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Length = PayloadLength - sizeof(BYTE);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFile_GetNextDownloadRecord(
    _In_ PCZP_FILE_DOWNLOAD_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_FILE_DOWNLOAD_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpFile_ReadDownloadRecord(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}
