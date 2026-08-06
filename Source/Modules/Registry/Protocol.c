#include "Include/KNSoft/ZPigeon/Registry.h"

#include "../../KNSoft.ZPigeon.Protocol/Core/Protocol.inl"

static
LOGICAL
ZpRegistry_IsScopeValid(
    _In_ ZP_REGISTRY_ROOT Root)
{
    return Root >= ZpRegistryClassesRoot &&
           Root <= ZpRegistryCurrentConfig;
}

static
LOGICAL
ZpRegistry_IsStringValid(
    _In_reads_opt_(Length) PCWCH String,
    _In_ ULONG Length)
{
    return Length <= ZP_REGISTRY_PATH_MAX_LENGTH &&
           (Length == 0 || String != NULL);
}

static
NTSTATUS
ZpRegistry_WriteScope(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ ZP_REGISTRY_ROOT Root)
{
    return ZpCodec_WriteByte(Writer, Root);
}

static
NTSTATUS
ZpRegistry_ReadScope(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PZP_REGISTRY_ROOT Root)
{
    NTSTATUS Status;

    Status = ZpCodec_ReadByte(Reader, Root);
    if (NT_SUCCESS(Status) && !ZpRegistry_IsScopeValid(*Root))
    {
        Status = STATUS_DATA_ERROR;
    }
    return Status;
}

NTSTATUS
ZpRegistry_EncodeEnumerateRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ULONG MaxEntries,
    _In_ BOOLEAN CursorPresent,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (!ZpRegistry_IsScopeValid(Root) ||
        MaxEntries == 0 || MaxEntries > ZP_REGISTRY_PAGE_MAX_COUNT ||
        (CursorPresent != FALSE && CursorPresent != TRUE) ||
        !ZpRegistry_IsStringValid(Path, PathLength) ||
        !ZpRegistry_IsStringValid(Cursor, CursorLength) ||
        (!CursorPresent && CursorLength != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(BYTE) + sizeof(ULONG) + sizeof(BYTE) +
                   2 * sizeof(ULONG) +
                   ((ULONGLONG)PathLength + CursorLength) * sizeof(WCHAR);
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
    Status = ZpRegistry_WriteScope(&Writer, Root);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, MaxEntries);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteBoolean(&Writer, CursorPresent);
    }
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
ZpRegistry_DecodeEnumerateRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_ENUMERATE_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpRegistry_ReadScope(&Reader, &Request->Root);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &Request->MaxEntries);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadBoolean(&Reader, &Request->CursorPresent);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Request->Path);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Request->Cursor);
    }
    if (!NT_SUCCESS(Status) ||
        Request->MaxEntries == 0 ||
        Request->MaxEntries > ZP_REGISTRY_PAGE_MAX_COUNT ||
        Request->Path.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Request->Cursor.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        (!Request->CursorPresent && Request->Cursor.Length != 0) ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpRegistry_ReadKeyRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_REGISTRY_KEY_RECORD_VIEW Record)
{
    ZP_REGISTRY_KEY_RECORD_VIEW LocalRecord;
    NTSTATUS Status;

    Status = ZpCodec_ReadString(Reader, &LocalRecord.Name);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(Reader, &LocalRecord.LastWriteTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadBoolean(Reader, &LocalRecord.HasChildren);
    }
    if (NT_SUCCESS(Status) &&
        (LocalRecord.Name.Length == 0 ||
         LocalRecord.Name.Length > ZP_REGISTRY_PATH_MAX_LENGTH))
    {
        Status = STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Record != NULL)
    {
        *Record = LocalRecord;
    }
    return Status;
}

static
NTSTATUS
ZpRegistry_ReadValueRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_REGISTRY_VALUE_RECORD_VIEW Record)
{
    ZP_REGISTRY_VALUE_RECORD_VIEW LocalRecord;
    NTSTATUS Status;

    Status = ZpCodec_ReadString(Reader, &LocalRecord.Name);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(Reader, &LocalRecord.Type);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(Reader, &LocalRecord.DataLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadByteString(Reader, &LocalRecord.Preview);
    }
    if (NT_SUCCESS(Status) &&
        (LocalRecord.Name.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
         LocalRecord.Preview.Length >
             ZP_REGISTRY_VALUE_PREVIEW_MAX_LENGTH ||
         LocalRecord.Preview.Length > LocalRecord.DataLength))
    {
        Status = STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Record != NULL)
    {
        *Record = LocalRecord;
    }
    return Status;
}

static
NTSTATUS
ZpRegistry_ValidateNextCursor(
    _In_ BOOLEAN HasMore,
    _In_reads_opt_(RecordCount) PCWCH LastName,
    _In_ ULONG LastNameLength,
    _In_reads_opt_(NextCursorLength) PCWCH NextCursor,
    _In_ ULONG NextCursorLength,
    _In_ ULONG RecordCount)
{
    if ((HasMore != FALSE && HasMore != TRUE) ||
        !ZpRegistry_IsStringValid(NextCursor, NextCursorLength) ||
        (HasMore &&
         (RecordCount == 0 || LastNameLength != NextCursorLength)) ||
        (!HasMore && NextCursorLength != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (HasMore && NextCursorLength != 0 &&
        (LastName == NULL || NextCursor == NULL ||
         RtlCompareMemory(LastName,
                          NextCursor,
                          (SIZE_T)NextCursorLength * sizeof(WCHAR)) !=
             (SIZE_T)NextCursorLength * sizeof(WCHAR)))
    {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_EncodeKeyPage(
    _In_ BOOLEAN HasMore,
    _In_reads_opt_(RecordCount) PCZP_REGISTRY_KEY_RECORD Records,
    _In_ ULONG RecordCount,
    _In_reads_opt_(NextCursorLength) PCWCH NextCursor,
    _In_ ULONG NextCursorLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize;
    ULONG Index;
    NTSTATUS Status;

    if (RecordCount > ZP_REGISTRY_PAGE_MAX_COUNT ||
        (RecordCount != 0 && Records == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpRegistry_ValidateNextCursor(
        HasMore,
        RecordCount != 0 ? Records[RecordCount - 1].Name : NULL,
        RecordCount != 0 ? Records[RecordCount - 1].NameLength : 0,
        NextCursor,
        NextCursorLength,
        RecordCount);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    RequiredSize = sizeof(BYTE) + 2 * sizeof(ULONG) +
                   (ULONGLONG)NextCursorLength * sizeof(WCHAR);
    for (Index = 0; Index < RecordCount; Index++)
    {
        if (Records[Index].NameLength == 0 ||
            Records[Index].HasChildren > TRUE ||
            !ZpRegistry_IsStringValid(Records[Index].Name,
                                      Records[Index].NameLength))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += sizeof(ULONG) + sizeof(ULONGLONG) + sizeof(BYTE) +
                        (ULONGLONG)Records[Index].NameLength * sizeof(WCHAR);
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
    ZpWire_WriteByte(&Cursor, HasMore);
    ZpWire_WriteString(&Cursor, NextCursor, NextCursorLength);
    ZpWire_WriteUInt32(&Cursor, RecordCount);
    for (Index = 0; Index < RecordCount; Index++)
    {
        ZpWire_WriteString(&Cursor, Records[Index].Name, Records[Index].NameLength);
        ZpWire_WriteUInt64(&Cursor, Records[Index].LastWriteTime);
        ZpWire_WriteByte(&Cursor, Records[Index].HasChildren);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_DecodeKeyPage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_PAGE_VIEW Page)
{
    ZP_CODEC_READER Reader;
    ZP_REGISTRY_KEY_RECORD_VIEW LastRecord;
    ULONG Index;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadBoolean(&Reader, &Page->HasMore);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Page->NextCursor);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadArrayCount(&Reader, &Page->Records.Count);
    }
    Page->Records.Buffer = Add2Ptr(Payload, Reader.Offset);
    for (Index = 0;
         NT_SUCCESS(Status) && Index < Page->Records.Count;
         Index++)
    {
        Status = ZpRegistry_ReadKeyRecord(&Reader,
                                          Index + 1 == Page->Records.Count ?
                                              &LastRecord : NULL);
    }
    Page->Records.Length = PayloadLength -
                           (ULONG)(Page->Records.Buffer - (const BYTE*)Payload);
    if (!NT_SUCCESS(Status) ||
        Reader.Offset != PayloadLength ||
        Page->NextCursor.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        (Page->HasMore &&
         (Page->Records.Count == 0 ||
          LastRecord.Name.Length != Page->NextCursor.Length ||
          (Page->NextCursor.Length != 0 &&
           RtlCompareMemory(LastRecord.Name.Buffer,
                            Page->NextCursor.Buffer,
                            (SIZE_T)Page->NextCursor.Length * sizeof(WCHAR)) !=
               (SIZE_T)Page->NextCursor.Length * sizeof(WCHAR)))) ||
        (!Page->HasMore && Page->NextCursor.Length != 0))
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_GetNextKeyRecord(
    _In_ PCZP_REGISTRY_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_REGISTRY_KEY_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpRegistry_ReadKeyRecord(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

NTSTATUS
ZpRegistry_EncodeValuePage(
    _In_ BOOLEAN HasMore,
    _In_reads_opt_(RecordCount) PCZP_REGISTRY_VALUE_RECORD Records,
    _In_ ULONG RecordCount,
    _In_reads_opt_(NextCursorLength) PCWCH NextCursor,
    _In_ ULONG NextCursorLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize;
    ULONG Index;
    NTSTATUS Status;

    if (RecordCount > ZP_REGISTRY_PAGE_MAX_COUNT ||
        (RecordCount != 0 && Records == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpRegistry_ValidateNextCursor(
        HasMore,
        RecordCount != 0 ? Records[RecordCount - 1].Name : NULL,
        RecordCount != 0 ? Records[RecordCount - 1].NameLength : 0,
        NextCursor,
        NextCursorLength,
        RecordCount);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    RequiredSize = sizeof(BYTE) + 2 * sizeof(ULONG) +
                   (ULONGLONG)NextCursorLength * sizeof(WCHAR);
    for (Index = 0; Index < RecordCount; Index++)
    {
        if (!ZpRegistry_IsStringValid(Records[Index].Name,
                                      Records[Index].NameLength) ||
            Records[Index].PreviewLength >
                ZP_REGISTRY_VALUE_PREVIEW_MAX_LENGTH ||
            Records[Index].PreviewLength > Records[Index].DataLength ||
            (Records[Index].PreviewLength != 0 &&
             Records[Index].Preview == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 4 * sizeof(ULONG) +
                        (ULONGLONG)Records[Index].NameLength * sizeof(WCHAR) +
                        Records[Index].PreviewLength;
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
    ZpWire_WriteByte(&Cursor, HasMore);
    ZpWire_WriteString(&Cursor, NextCursor, NextCursorLength);
    ZpWire_WriteUInt32(&Cursor, RecordCount);
    for (Index = 0; Index < RecordCount; Index++)
    {
        ZpWire_WriteString(&Cursor, Records[Index].Name, Records[Index].NameLength);
        ZpWire_WriteUInt32(&Cursor, Records[Index].Type);
        ZpWire_WriteUInt32(&Cursor, Records[Index].DataLength);
        ZpWire_WriteByteString(&Cursor, Records[Index].Preview, Records[Index].PreviewLength);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_DecodeValuePage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_PAGE_VIEW Page)
{
    ZP_CODEC_READER Reader;
    ZP_REGISTRY_VALUE_RECORD_VIEW LastRecord;
    ULONG Index;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadBoolean(&Reader, &Page->HasMore);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Page->NextCursor);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadArrayCount(&Reader, &Page->Records.Count);
    }
    Page->Records.Buffer = Add2Ptr(Payload, Reader.Offset);
    for (Index = 0;
         NT_SUCCESS(Status) && Index < Page->Records.Count;
         Index++)
    {
        Status = ZpRegistry_ReadValueRecord(&Reader,
                                            Index + 1 == Page->Records.Count ?
                                                &LastRecord : NULL);
    }
    Page->Records.Length = PayloadLength -
                           (ULONG)(Page->Records.Buffer - (const BYTE*)Payload);
    if (!NT_SUCCESS(Status) ||
        Reader.Offset != PayloadLength ||
        Page->NextCursor.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        (Page->HasMore &&
         (Page->Records.Count == 0 ||
          LastRecord.Name.Length != Page->NextCursor.Length ||
          (Page->NextCursor.Length != 0 &&
           RtlCompareMemory(LastRecord.Name.Buffer,
                            Page->NextCursor.Buffer,
                            (SIZE_T)Page->NextCursor.Length * sizeof(WCHAR)) !=
               (SIZE_T)Page->NextCursor.Length * sizeof(WCHAR)))) ||
        (!Page->HasMore && Page->NextCursor.Length != 0))
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_GetNextValueRecord(
    _In_ PCZP_REGISTRY_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_REGISTRY_VALUE_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpRegistry_ReadValueRecord(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

NTSTATUS
ZpRegistry_EncodeValueRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (!ZpRegistry_IsScopeValid(Root) ||
        !ZpRegistry_IsStringValid(Path, PathLength) ||
        !ZpRegistry_IsStringValid(ValueName, ValueNameLength))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(BYTE) + 2 * sizeof(ULONG) +
                   ((ULONGLONG)PathLength + ValueNameLength) * sizeof(WCHAR);
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
    Status = ZpRegistry_WriteScope(&Writer, Root);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer,
                                     ValueName,
                                     ValueNameLength);
    }
    return Status;
}

NTSTATUS
ZpRegistry_DecodeValueRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_VALUE_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpRegistry_ReadScope(&Reader, &Request->Root);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Request->Path);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Request->ValueName);
    }
    if (!NT_SUCCESS(Status) ||
        Request->Path.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Request->ValueName.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_EncodeRangeRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (!ZpRegistry_IsScopeValid(Root) || !ZpRegistry_IsStringValid(Path, PathLength) ||
        !ZpRegistry_IsStringValid(ValueName, ValueNameLength) || Length == 0 ||
        Length > ZP_REGISTRY_RANGE_MAX_LENGTH)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(BYTE) + 4 * sizeof(ULONG) +
                   ((ULONGLONG)PathLength + ValueNameLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpRegistry_WriteScope(&Writer, Root);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Offset);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Length);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, ValueName, ValueNameLength);
    return Status;
}

NTSTATUS
ZpRegistry_DecodeRangeRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_RANGE_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpRegistry_ReadScope(&Reader, &Request->Root);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Request->Offset);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Request->Length);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->Path);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->ValueName);
    if (!NT_SUCCESS(Status) || Request->Path.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Request->ValueName.Length > ZP_REGISTRY_PATH_MAX_LENGTH || Request->Length == 0 ||
        Request->Length > ZP_REGISTRY_RANGE_MAX_LENGTH || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_EncodeRangeWriteRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _In_ ULONG Offset,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (!ZpRegistry_IsScopeValid(Root) || !ZpRegistry_IsStringValid(Path, PathLength) ||
        !ZpRegistry_IsStringValid(ValueName, ValueNameLength) || Data == NULL || DataLength == 0 ||
        DataLength > ZP_REGISTRY_RANGE_MAX_LENGTH)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(BYTE) + 4 * sizeof(ULONG) +
                   ((ULONGLONG)PathLength + ValueNameLength) * sizeof(WCHAR) + DataLength;
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpRegistry_WriteScope(&Writer, Root);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Offset);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, ValueName, ValueNameLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByteString(&Writer, Data, DataLength);
    return Status;
}

NTSTATUS
ZpRegistry_DecodeRangeWriteRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_RANGE_WRITE_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpRegistry_ReadScope(&Reader, &Request->Root);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Request->Offset);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->Path);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->ValueName);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByteString(&Reader, &Request->Data);
    if (!NT_SUCCESS(Status) || Request->Path.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Request->ValueName.Length > ZP_REGISTRY_PATH_MAX_LENGTH || Request->Data.Length == 0 ||
        Request->Data.Length > ZP_REGISTRY_RANGE_MAX_LENGTH || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_EncodeValue(
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (DataLength > ZP_REGISTRY_DATA_MAX_LENGTH ||
        (DataLength != 0 && Data == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = 2 * sizeof(ULONG) + DataLength;
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < *BytesWritten)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, Type);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteByteString(&Writer, Data, DataLength);
    }
    return Status;
}

NTSTATUS
ZpRegistry_DecodeValue(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_VALUE_VIEW Value)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &Value->Type);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadByteString(&Reader, &Value->Data);
    }
    if (!NT_SUCCESS(Status) ||
        Value->Data.Length > ZP_REGISTRY_DATA_MAX_LENGTH ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_EncodeRange(
    _In_ ULONG TotalLength,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (TotalLength > ZP_REGISTRY_DATA_MAX_LENGTH || DataLength > TotalLength ||
        DataLength > ZP_REGISTRY_RANGE_MAX_LENGTH || (DataLength != 0 && Data == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = 2 * sizeof(ULONG) + DataLength;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, TotalLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByteString(&Writer, Data, DataLength);
    return Status;
}

NTSTATUS
ZpRegistry_DecodeRange(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_RANGE_VIEW Range)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &Range->TotalLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByteString(&Reader, &Range->Data);
    if (!NT_SUCCESS(Status) || Range->TotalLength > ZP_REGISTRY_DATA_MAX_LENGTH ||
        Range->Data.Length > Range->TotalLength || Range->Data.Length > ZP_REGISTRY_RANGE_MAX_LENGTH ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_EncodeSetValueRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ULONG Type,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (!ZpRegistry_IsScopeValid(Root) ||
        !ZpRegistry_IsStringValid(Path, PathLength) ||
        !ZpRegistry_IsStringValid(ValueName, ValueNameLength) ||
        DataLength > ZP_REGISTRY_DATA_MAX_LENGTH ||
        (DataLength != 0 && Data == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(BYTE) + 4 * sizeof(ULONG) +
                   ((ULONGLONG)PathLength + ValueNameLength) * sizeof(WCHAR) +
                   DataLength;
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
    Status = ZpRegistry_WriteScope(&Writer, Root);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Type);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer,
                                     ValueName,
                                     ValueNameLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteByteString(&Writer, Data, DataLength);
    }
    return Status;
}

NTSTATUS
ZpRegistry_DecodeSetValueRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_SET_VALUE_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpRegistry_ReadScope(&Reader, &Request->Root);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &Request->Type);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Request->Path);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Request->ValueName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadByteString(&Reader, &Request->Data);
    }
    if (!NT_SUCCESS(Status) ||
        Request->Path.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Request->ValueName.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Request->Data.Length > ZP_REGISTRY_DATA_MAX_LENGTH ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_EncodeKeyRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (!ZpRegistry_IsScopeValid(Root) ||
        PathLength == 0 ||
        !ZpRegistry_IsStringValid(Path, PathLength))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(BYTE) + sizeof(ULONG) +
                   (ULONGLONG)PathLength * sizeof(WCHAR);
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
    Status = ZpRegistry_WriteScope(&Writer, Root);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    }
    return Status;
}

NTSTATUS
ZpRegistry_DecodeKeyRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_KEY_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpRegistry_ReadScope(&Reader, &Request->Root);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Request->Path);
    }
    if (!NT_SUCCESS(Status) ||
        Request->Path.Length == 0 ||
        Request->Path.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_EncodeRenameRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_reads_(NewNameLength) PCWCH NewName,
    _In_ ULONG NewNameLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (!ZpRegistry_IsScopeValid(Root) ||
        !ZpRegistry_IsStringValid(Path, PathLength) ||
        !ZpRegistry_IsStringValid(Name, NameLength) || NameLength == 0 ||
        !ZpRegistry_IsStringValid(NewName, NewNameLength) ||
        NewNameLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(BYTE) + 3 * sizeof(ULONG) +
                   ((ULONGLONG)PathLength + NameLength + NewNameLength) *
                       sizeof(WCHAR);
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
    Status = ZpRegistry_WriteScope(&Writer, Root);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Name, NameLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, NewName, NewNameLength);
    }
    return Status;
}

NTSTATUS
ZpRegistry_DecodeRenameRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_RENAME_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpRegistry_ReadScope(&Reader, &Request->Root);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Request->Path);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Request->Name);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Request->NewName);
    }
    if (!NT_SUCCESS(Status) ||
        Request->Path.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Request->Name.Length == 0 ||
        Request->Name.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Request->NewName.Length == 0 ||
        Request->NewName.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_EncodeSecurityRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(SddlLength) PCWCH Sddl,
    _In_ ULONG SddlLength,
    _In_ BOOLEAN DaclProtected,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (!ZpRegistry_IsScopeValid(Root) ||
        !ZpRegistry_IsStringValid(Path, PathLength) ||
        !ZpRegistry_IsStringValid(Sddl, SddlLength) ||
        PathLength > ZP_REGISTRY_PATH_MAX_LENGTH ||
        SddlLength > ZP_REGISTRY_PATH_MAX_LENGTH ||
        DaclProtected > TRUE)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 2 * sizeof(BYTE) + 2 * sizeof(ULONG) +
                   ((ULONGLONG)PathLength + SddlLength) * sizeof(WCHAR);
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
    Status = ZpRegistry_WriteScope(&Writer, Root);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Path, PathLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Sddl, SddlLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteBoolean(&Writer, DaclProtected);
    return Status;
}

NTSTATUS
ZpRegistry_DecodeSecurityRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_SECURITY_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpRegistry_ReadScope(&Reader, &Request->Root);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->Path);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->Sddl);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadBoolean(&Reader, &Request->DaclProtected);
    if (!NT_SUCCESS(Status) ||
        Request->Path.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Request->Sddl.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRegistry_EncodeSecurityDescriptor(
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

    if (Sddl == NULL || SddlLength == 0 || SddlLength > ZP_REGISTRY_PATH_MAX_LENGTH || DaclProtected > TRUE)
        return STATUS_INVALID_PARAMETER;
    RequiredSize = sizeof(BYTE) + sizeof(ULONG) + (ULONGLONG)SddlLength * sizeof(WCHAR);
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteBoolean(&Writer, DaclProtected);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Sddl, SddlLength);
    return Status;
}

NTSTATUS
ZpRegistry_DecodeSecurityDescriptor(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SECURITY_DESCRIPTOR_VIEW Descriptor)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadBoolean(&Reader, &Descriptor->DaclProtected);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Descriptor->Sddl);
    if (!NT_SUCCESS(Status) || Descriptor->Sddl.Length == 0 ||
        Descriptor->Sddl.Length > ZP_REGISTRY_PATH_MAX_LENGTH || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}
