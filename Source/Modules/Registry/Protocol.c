#include "Include/KNSoft/ZPigeon/Registry.h"

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
    return ZpCodec_WriteUInt16(Writer, (USHORT)Root);
}

static
NTSTATUS
ZpRegistry_ReadScope(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PZP_REGISTRY_ROOT Root)
{
    USHORT RootValue = 0;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt16(Reader, &RootValue);
    *Root = (ZP_REGISTRY_ROOT)RootValue;
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
    RequiredSize = sizeof(USHORT) + sizeof(ULONG) + sizeof(BYTE) +
                   2 * sizeof(ULONG) +
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
    ZP_CODEC_WRITER Writer;
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
            !ZpRegistry_IsStringValid(Records[Index].Name,
                                      Records[Index].NameLength))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += sizeof(ULONG) + sizeof(ULONGLONG) + sizeof(BYTE) +
                        (ULONGLONG)Records[Index].NameLength * sizeof(WCHAR);
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
    Status = ZpCodec_WriteBoolean(&Writer, HasMore);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, NextCursor, NextCursorLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteArrayCount(&Writer, RecordCount);
    }
    for (Index = 0; NT_SUCCESS(Status) && Index < RecordCount; Index++)
    {
        Status = ZpCodec_WriteString(&Writer,
                                     Records[Index].Name,
                                     Records[Index].NameLength);
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteUInt64(&Writer,
                                         Records[Index].LastWriteTime);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteBoolean(&Writer,
                                          Records[Index].HasChildren);
        }
    }
    return Status;
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
ZpRegistry_GetKeyRecord(
    _In_ PCZP_REGISTRY_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_REGISTRY_KEY_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    ULONG CurrentIndex;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Index >= List->Count)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeReader(&Reader, List->Buffer, List->Length);
    for (CurrentIndex = 0;
         NT_SUCCESS(Status) && CurrentIndex <= Index;
         CurrentIndex++)
    {
        Status = ZpRegistry_ReadKeyRecord(
            &Reader,
            CurrentIndex == Index ? Record : NULL);
    }
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
    ZP_CODEC_WRITER Writer;
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
    Status = ZpCodec_WriteBoolean(&Writer, HasMore);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, NextCursor, NextCursorLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteArrayCount(&Writer, RecordCount);
    }
    for (Index = 0; NT_SUCCESS(Status) && Index < RecordCount; Index++)
    {
        Status = ZpCodec_WriteString(&Writer,
                                     Records[Index].Name,
                                     Records[Index].NameLength);
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteUInt32(&Writer, Records[Index].Type);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteUInt32(&Writer,
                                         Records[Index].DataLength);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteByteString(&Writer,
                                             Records[Index].Preview,
                                             Records[Index].PreviewLength);
        }
    }
    return Status;
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
ZpRegistry_GetValueRecord(
    _In_ PCZP_REGISTRY_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_REGISTRY_VALUE_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    ULONG CurrentIndex;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Index >= List->Count)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeReader(&Reader, List->Buffer, List->Length);
    for (CurrentIndex = 0;
         NT_SUCCESS(Status) && CurrentIndex <= Index;
         CurrentIndex++)
    {
        Status = ZpRegistry_ReadValueRecord(
            &Reader,
            CurrentIndex == Index ? Record : NULL);
    }
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
    RequiredSize = sizeof(USHORT) + 2 * sizeof(ULONG) +
                   ((ULONGLONG)PathLength + ValueNameLength) * sizeof(WCHAR);
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
    RequiredSize = sizeof(USHORT) + 4 * sizeof(ULONG) +
                   ((ULONGLONG)PathLength + ValueNameLength) * sizeof(WCHAR) +
                   DataLength;
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
    RequiredSize = sizeof(USHORT) + sizeof(ULONG) +
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
    RequiredSize = sizeof(USHORT) + 3 * sizeof(ULONG) +
                   ((ULONGLONG)PathLength + NameLength + NewNameLength) *
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
        SddlLength > ZP_REGISTRY_PATH_MAX_LENGTH)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(USHORT) + 2 * sizeof(ULONG) +
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
    if (!NT_SUCCESS(Status) ||
        Request->Path.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Request->Sddl.Length > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}
