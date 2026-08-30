#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Browser.h"

#include "../../KNSoft.ZPigeon.Protocol/Core/Protocol.inl"

static
LOGICAL
ZpBrowser_IsTypeValid(
    _In_ ZP_BROWSER_TYPE Browser)
{
    return Browser >= ZpBrowserChrome && Browser <= ZpBrowserEdge;
}

static
LOGICAL
ZpBrowser_IsKindValid(
    _In_ ZP_BROWSER_KIND Kind)
{
    return Kind >= ZpBrowserKindBrowser && Kind <= ZpBrowserKindPassword;
}

static
LOGICAL
ZpBrowser_IsStringValid(
    _In_reads_opt_(Length) PCWCH String,
    _In_ ULONG Length)
{
    return Length <= ZP_CODEC_MAX_ELEMENT_COUNT && (Length == 0 || String != NULL);
}

#define ZP_BROWSER_RECORD_ID 0x01
#define ZP_BROWSER_RECORD_NAME 0x02
#define ZP_BROWSER_RECORD_LOCATION 0x04
#define ZP_BROWSER_RECORD_DETAIL 0x08
#define ZP_BROWSER_RECORD_VALID_MASK 0x0F

static
ULONG
ZpBrowser_GetRecordDataSize(
    _In_ ZP_BROWSER_KIND Kind)
{
    switch (Kind)
    {
        case ZpBrowserKindHistory:
            return sizeof(ULONGLONG) + sizeof(ULONG) * 2;
        case ZpBrowserKindDownload:
            return sizeof(ULONGLONG) * 4 + sizeof(ULONG) * 2;
        case ZpBrowserKindCookie:
            return sizeof(ULONGLONG) * 3 + sizeof(ULONG) * 2;
        case ZpBrowserKindPassword:
            return sizeof(ULONGLONG) + sizeof(ULONG);
        default:
            return 0;
    }
}

static
VOID
ZpBrowser_WriteRecordData(
    _Inout_ PBYTE* Cursor,
    _In_ PCZP_BROWSER_RECORD Record)
{
    switch (Record->Kind)
    {
        case ZpBrowserKindHistory:
            ZpWire_WriteUInt64(Cursor, Record->Data.History.LastVisitTime);
            ZpWire_WriteUInt32(Cursor, Record->Data.History.VisitCount);
            ZpWire_WriteUInt32(Cursor, Record->Data.History.TypedCount);
            break;
        case ZpBrowserKindDownload:
            ZpWire_WriteUInt64(Cursor, Record->Data.Download.StartTime);
            ZpWire_WriteUInt64(Cursor, Record->Data.Download.EndTime);
            ZpWire_WriteUInt64(Cursor, Record->Data.Download.ReceivedBytes);
            ZpWire_WriteUInt64(Cursor, Record->Data.Download.TotalBytes);
            ZpWire_WriteUInt32(Cursor, Record->Data.Download.State);
            ZpWire_WriteUInt32(Cursor, Record->Data.Download.InterruptReason);
            break;
        case ZpBrowserKindCookie:
            ZpWire_WriteUInt64(Cursor, Record->Data.Cookie.CreationTime);
            ZpWire_WriteUInt64(Cursor, Record->Data.Cookie.ExpirationTime);
            ZpWire_WriteUInt64(Cursor, Record->Data.Cookie.LastAccessTime);
            ZpWire_WriteUInt32(Cursor, Record->Data.Cookie.SameSite);
            ZpWire_WriteUInt32(Cursor, Record->Data.Cookie.Flags);
            break;
        case ZpBrowserKindPassword:
            ZpWire_WriteUInt64(Cursor, Record->Data.Password.CreationTime);
            ZpWire_WriteUInt32(Cursor, Record->Data.Password.Flags);
            break;
    }
}

static
NTSTATUS
ZpBrowser_ReadRecordData(
    _Inout_ PZP_CODEC_READER Reader,
    _Inout_ PZP_BROWSER_RECORD_VIEW Record)
{
    NTSTATUS Status = STATUS_SUCCESS;

    switch (Record->Kind)
    {
        case ZpBrowserKindHistory:
            Status = ZpCodec_ReadUInt64(Reader, &Record->Data.History.LastVisitTime);
            if (NT_SUCCESS(Status))
                Status = ZpCodec_ReadUInt32(Reader, &Record->Data.History.VisitCount);
            if (NT_SUCCESS(Status))
                Status = ZpCodec_ReadUInt32(Reader, &Record->Data.History.TypedCount);
            break;
        case ZpBrowserKindDownload:
            Status = ZpCodec_ReadUInt64(Reader, &Record->Data.Download.StartTime);
            if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Record->Data.Download.EndTime);
            if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Record->Data.Download.ReceivedBytes);
            if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Record->Data.Download.TotalBytes);
            if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Record->Data.Download.State);
            if (NT_SUCCESS(Status))
                Status = ZpCodec_ReadUInt32(Reader, &Record->Data.Download.InterruptReason);
            break;
        case ZpBrowserKindCookie:
            Status = ZpCodec_ReadUInt64(Reader, &Record->Data.Cookie.CreationTime);
            if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Record->Data.Cookie.ExpirationTime);
            if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Record->Data.Cookie.LastAccessTime);
            if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Record->Data.Cookie.SameSite);
            if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Record->Data.Cookie.Flags);
            break;
        case ZpBrowserKindPassword:
            Status = ZpCodec_ReadUInt64(Reader, &Record->Data.Password.CreationTime);
            if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Record->Data.Password.Flags);
            break;
    }
    return Status;
}

static
NTSTATUS
ZpBrowser_GetRecordSize(
    _In_ PCZP_BROWSER_RECORD Record,
    _Out_ PULONG Size,
    _Out_ PBYTE Fields)
{
    ULONGLONG RequiredSize;
    BYTE LocalFields = 0;

    if (!ZpBrowser_IsKindValid(Record->Kind) ||
        !ZpBrowser_IsTypeValid(Record->Browser) ||
        !ZpBrowser_IsStringValid(Record->Identity, Record->IdentityLength) ||
        !ZpBrowser_IsStringValid(Record->Name, Record->NameLength) ||
        !ZpBrowser_IsStringValid(Record->Location, Record->LocationLength) ||
        !ZpBrowser_IsStringValid(Record->Detail, Record->DetailLength))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Record->Id != 0) LocalFields |= ZP_BROWSER_RECORD_ID;
    if (Record->NameLength != 0) LocalFields |= ZP_BROWSER_RECORD_NAME;
    if (Record->LocationLength != 0) LocalFields |= ZP_BROWSER_RECORD_LOCATION;
    if (Record->DetailLength != 0) LocalFields |= ZP_BROWSER_RECORD_DETAIL;
    RequiredSize = 3 + ZpBrowser_GetRecordDataSize(Record->Kind) + sizeof(ULONG) +
                   (ULONGLONG)Record->IdentityLength * sizeof(WCHAR);
    if (FlagOn(LocalFields, ZP_BROWSER_RECORD_ID)) RequiredSize += sizeof(ULONGLONG);
    if (FlagOn(LocalFields, ZP_BROWSER_RECORD_NAME))
        RequiredSize += sizeof(ULONG) + (ULONGLONG)Record->NameLength * sizeof(WCHAR);
    if (FlagOn(LocalFields, ZP_BROWSER_RECORD_LOCATION))
        RequiredSize += sizeof(ULONG) + (ULONGLONG)Record->LocationLength * sizeof(WCHAR);
    if (FlagOn(LocalFields, ZP_BROWSER_RECORD_DETAIL))
        RequiredSize += sizeof(ULONG) + (ULONGLONG)Record->DetailLength * sizeof(WCHAR);
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *Size = (ULONG)RequiredSize;
    *Fields = LocalFields;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpBrowser_EncodeRecord(
    _In_ PCZP_BROWSER_RECORD Record,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    BYTE Fields;
    NTSTATUS Status;

    Status = ZpBrowser_GetRecordSize(Record, BytesWritten, &Fields);
    if (!NT_SUCCESS(Status) || Buffer == NULL) return Status;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteByte(&Cursor, Record->Kind);
    ZpWire_WriteByte(&Cursor, Record->Browser);
    ZpWire_WriteByte(&Cursor, Fields);
    if (FlagOn(Fields, ZP_BROWSER_RECORD_ID)) ZpWire_WriteUInt64(&Cursor, Record->Id);
    ZpBrowser_WriteRecordData(&Cursor, Record);
    ZpWire_WriteString(&Cursor, Record->Identity, Record->IdentityLength);
    if (FlagOn(Fields, ZP_BROWSER_RECORD_NAME))
        ZpWire_WriteString(&Cursor, Record->Name, Record->NameLength);
    if (FlagOn(Fields, ZP_BROWSER_RECORD_LOCATION))
        ZpWire_WriteString(&Cursor, Record->Location, Record->LocationLength);
    if (FlagOn(Fields, ZP_BROWSER_RECORD_DETAIL))
        ZpWire_WriteString(&Cursor, Record->Detail, Record->DetailLength);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpBrowser_EncodePageHeader(
    _In_ ULONGLONG NextCursor,
    _In_ ULONG RecordCount,
    _Out_writes_bytes_(sizeof(ULONGLONG) + sizeof(ULONG)) PVOID Buffer)
{
    PBYTE Cursor = Buffer;

    if (RecordCount > ZP_CODEC_MAX_ELEMENT_COUNT || Buffer == NULL) return STATUS_INVALID_PARAMETER;
    ZpWire_WriteUInt64(&Cursor, NextCursor);
    ZpWire_WriteUInt32(&Cursor, RecordCount);
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpBrowser_ReadRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_BROWSER_RECORD_VIEW Record)
{
    ZP_BROWSER_RECORD_VIEW Local = { 0 };
    BYTE Fields;
    NTSTATUS Status;

    Status = ZpCodec_ReadByte(Reader, &Local.Kind);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(Reader, &Local.Browser);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(Reader, &Fields);
    if (NT_SUCCESS(Status) && (Fields & ~ZP_BROWSER_RECORD_VALID_MASK) != 0) return STATUS_DATA_ERROR;
    if (NT_SUCCESS(Status) && FlagOn(Fields, ZP_BROWSER_RECORD_ID))
        Status = ZpCodec_ReadUInt64(Reader, &Local.Id);
    if (NT_SUCCESS(Status)) Status = ZpBrowser_ReadRecordData(Reader, &Local);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Identity);
    if (NT_SUCCESS(Status) && FlagOn(Fields, ZP_BROWSER_RECORD_NAME))
        Status = ZpCodec_ReadString(Reader, &Local.Name);
    if (NT_SUCCESS(Status) && FlagOn(Fields, ZP_BROWSER_RECORD_LOCATION))
        Status = ZpCodec_ReadString(Reader, &Local.Location);
    if (NT_SUCCESS(Status) && FlagOn(Fields, ZP_BROWSER_RECORD_DETAIL))
        Status = ZpCodec_ReadString(Reader, &Local.Detail);
    if (NT_SUCCESS(Status) &&
        (!ZpBrowser_IsKindValid(Local.Kind) || !ZpBrowser_IsTypeValid(Local.Browser)))
    {
        return STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Record != NULL) *Record = Local;
    return Status;
}

NTSTATUS
ZpBrowser_EncodePage(
    _In_reads_opt_(RecordCount) PCZP_BROWSER_RECORD Records,
    _In_ ULONG RecordCount,
    _In_ ULONGLONG NextCursor,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = sizeof(ULONGLONG) + sizeof(ULONG);
    ULONG Index, RecordSize;
    BYTE Fields;

    if (RecordCount > ZP_CODEC_MAX_ELEMENT_COUNT || (RecordCount != 0 && Records == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < RecordCount; Index++)
    {
        NTSTATUS Status = ZpBrowser_GetRecordSize(&Records[Index], &RecordSize, &Fields);

        if (!NT_SUCCESS(Status)) return Status;
        RequiredSize += RecordSize;
        if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpBrowser_EncodePageHeader(NextCursor, RecordCount, Buffer);
    Cursor = Add2Ptr(Buffer, sizeof(ULONGLONG) + sizeof(ULONG));
    for (Index = 0; Index < RecordCount; Index++)
    {
        NTSTATUS Status = ZpBrowser_EncodeRecord(&Records[Index],
                                                  Cursor,
                                                  BufferSize - (ULONG)(Cursor - (PBYTE)Buffer),
                                                  &RecordSize);

        if (!NT_SUCCESS(Status)) return Status;
        Cursor += RecordSize;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpBrowser_DecodePage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_BROWSER_PAGE_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;
    ULONG Count, Index;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, &View->NextCursor);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    View->Buffer = Add2Ptr(Payload, Reader.Offset);
    View->Length = PayloadLength - Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpBrowser_ReadRecord(&Reader, NULL);
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpBrowser_GetNextRecord(
    _In_ PCZP_BROWSER_PAGE_VIEW Page,
    _Inout_ PULONG Offset,
    _Out_ PZP_BROWSER_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= Page->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(Page->Buffer, *Offset), Page->Length - *Offset);
    Status = ZpBrowser_ReadRecord(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

NTSTATUS
ZpBrowser_EncodeQuery(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ ZP_BROWSER_KIND Kind,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_reads_opt_(UserDataLength) PCWCH UserData,
    _In_ ULONG UserDataLength,
    _In_ ULONGLONG Cursor,
    _In_ ULONG Limit,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (!ZpBrowser_IsTypeValid(Browser) ||
        Kind < ZpBrowserKindHistory || Kind > ZpBrowserKindPassword ||
        !ZpBrowser_IsStringValid(Profile, ProfileLength) || ProfileLength == 0 ||
        !ZpBrowser_IsStringValid(UserData, UserDataLength) ||
        Limit == 0 || Limit > ZP_BROWSER_PAGE_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteByte(&Writer, Browser);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, Kind);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Cursor);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Limit);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Profile, ProfileLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, UserData, UserDataLength);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpBrowser_DecodeQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_BROWSER_QUERY_VIEW Query)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &Query->Browser);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Query->Kind);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Query->Cursor);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Query->Limit);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Query->Profile);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Query->UserData);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength ||
        !ZpBrowser_IsTypeValid(Query->Browser) ||
        Query->Kind < ZpBrowserKindHistory || Query->Kind > ZpBrowserKindPassword ||
        Query->Profile.Length == 0 || Query->Limit == 0 || Query->Limit > ZP_BROWSER_PAGE_SIZE)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpBrowser_EncodeProfileInspectionRequest(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_reads_opt_(UserDataLength) PCWCH UserData,
    _In_ ULONG UserDataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (!ZpBrowser_IsTypeValid(Browser) ||
        !ZpBrowser_IsStringValid(Profile, ProfileLength) || ProfileLength == 0 ||
        !ZpBrowser_IsStringValid(UserData, UserDataLength))
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteByte(&Writer, Browser);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Profile, ProfileLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, UserData, UserDataLength);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpBrowser_DecodeProfileInspectionRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_BROWSER_TYPE Browser,
    _Out_ PZP_STRING_VIEW Profile,
    _Out_ PZP_STRING_VIEW UserData)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, Browser);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, Profile);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, UserData);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength ||
        !ZpBrowser_IsTypeValid(*Browser) || Profile->Length == 0)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpBrowser_EncodeProfileInspection(
    _In_ PCZP_BROWSER_PROFILE_INSPECTION Inspection,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (Inspection == NULL || Inspection->BrowserRunning > TRUE) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, Inspection->ProfileSize);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Inspection->AvailableSpace);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, Inspection->BrowserRunning);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpBrowser_DecodeProfileInspection(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_BROWSER_PROFILE_INSPECTION Inspection)
{
    ZP_CODEC_READER Reader;
    BYTE BrowserRunning;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, &Inspection->ProfileSize);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Inspection->AvailableSpace);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &BrowserRunning);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength || BrowserRunning > TRUE)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    Inspection->BrowserRunning = BrowserRunning;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpBrowser_EncodeDocumentQuery(
    _In_ ULONG SnapshotId,
    _In_ ULONG NodeId,
    _In_ ULONG Cursor,
    _In_ ULONG Limit,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (SnapshotId == 0 || NodeId == 0 || Limit == 0 || Limit > ZP_BROWSER_DOCUMENT_PAGE_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, SnapshotId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, NodeId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Cursor);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Limit);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpBrowser_DecodeDocumentQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG SnapshotId,
    _Out_ PULONG NodeId,
    _Out_ PULONG Cursor,
    _Out_ PULONG Limit)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, SnapshotId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, NodeId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, Cursor);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, Limit);
    if (NT_SUCCESS(Status) &&
        (Reader.Offset != PayloadLength || *SnapshotId == 0 || *NodeId == 0 ||
         *Limit == 0 || *Limit > ZP_BROWSER_DOCUMENT_PAGE_SIZE))
    {
        return STATUS_DATA_ERROR;
    }
    return Status;
}

NTSTATUS
ZpBrowser_EncodeDocumentClose(
    _In_ ULONG SnapshotId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor = Buffer;

    *BytesWritten = sizeof(ULONG);
    if (SnapshotId == 0) return STATUS_INVALID_PARAMETER;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < sizeof(ULONG)) return STATUS_BUFFER_TOO_SMALL;
    ZpWire_WriteUInt32(&Cursor, SnapshotId);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpBrowser_DecodeDocumentClose(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG SnapshotId)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, SnapshotId);
    return NT_SUCCESS(Status) && (Reader.Offset != PayloadLength || *SnapshotId == 0) ?
               STATUS_DATA_ERROR : Status;
}

static
LOGICAL
ZpBrowser_IsDocumentTypeValid(
    _In_ ZP_BROWSER_DOCUMENT_TYPE Type)
{
    return Type >= ZpBrowserDocumentObject && Type <= ZpBrowserDocumentNull;
}

static
NTSTATUS
ZpBrowser_ReadDocumentNode(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_BROWSER_DOCUMENT_NODE_VIEW Node)
{
    ZP_BROWSER_DOCUMENT_NODE_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt32(Reader, &Local.Id);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(Reader, &Local.Type);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(Reader, &Local.Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Name);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Value);
    if (NT_SUCCESS(Status) &&
        (Local.Id == 0 || !ZpBrowser_IsDocumentTypeValid(Local.Type) ||
         (Local.Flags & ~ZP_BROWSER_DOCUMENT_NODE_HAS_CHILDREN) != 0))
    {
        return STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Node != NULL) *Node = Local;
    return Status;
}

NTSTATUS
ZpBrowser_EncodeDocumentPage(
    _In_ ULONG SnapshotId,
    _In_ ZP_BROWSER_DOCUMENT_TYPE ParentType,
    _In_ ULONG NextCursor,
    _In_reads_opt_(NodeCount) PCZP_BROWSER_DOCUMENT_NODE Nodes,
    _In_ ULONG NodeCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = 3 * sizeof(ULONG) + sizeof(BYTE);
    ULONG Index;

    if (SnapshotId == 0 || !ZpBrowser_IsDocumentTypeValid(ParentType) ||
        NodeCount > ZP_BROWSER_DOCUMENT_PAGE_SIZE || (NodeCount != 0 && Nodes == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < NodeCount; Index++)
    {
        if (Nodes[Index].Id == 0 || !ZpBrowser_IsDocumentTypeValid(Nodes[Index].Type) ||
            (Nodes[Index].Flags & ~ZP_BROWSER_DOCUMENT_NODE_HAS_CHILDREN) != 0 ||
            !ZpBrowser_IsStringValid(Nodes[Index].Name, Nodes[Index].NameLength) ||
            !ZpBrowser_IsStringValid(Nodes[Index].Value, Nodes[Index].ValueLength))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 3 * sizeof(ULONG) + 2 * sizeof(BYTE) +
                        ((ULONGLONG)Nodes[Index].NameLength + Nodes[Index].ValueLength) * sizeof(WCHAR);
        if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, SnapshotId);
    ZpWire_WriteByte(&Cursor, ParentType);
    ZpWire_WriteUInt32(&Cursor, NextCursor);
    ZpWire_WriteUInt32(&Cursor, NodeCount);
    for (Index = 0; Index < NodeCount; Index++)
    {
        ZpWire_WriteUInt32(&Cursor, Nodes[Index].Id);
        ZpWire_WriteByte(&Cursor, Nodes[Index].Type);
        ZpWire_WriteByte(&Cursor, Nodes[Index].Flags);
        ZpWire_WriteString(&Cursor, Nodes[Index].Name, Nodes[Index].NameLength);
        ZpWire_WriteString(&Cursor, Nodes[Index].Value, Nodes[Index].ValueLength);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpBrowser_DecodeDocumentPage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_BROWSER_DOCUMENT_PAGE_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;
    ULONG Count, Index;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &View->SnapshotId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &View->ParentType);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->NextCursor);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    if (NT_SUCCESS(Status) && Count > ZP_BROWSER_DOCUMENT_PAGE_SIZE) return STATUS_DATA_ERROR;
    View->Buffer = Add2Ptr(Payload, Reader.Offset);
    View->Length = PayloadLength - Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpBrowser_ReadDocumentNode(&Reader, NULL);
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength || View->SnapshotId == 0 ||
        !ZpBrowser_IsDocumentTypeValid(View->ParentType))
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpBrowser_GetNextDocumentNode(
    _In_ PCZP_BROWSER_DOCUMENT_PAGE_VIEW Page,
    _Inout_ PULONG Offset,
    _Out_ PZP_BROWSER_DOCUMENT_NODE_VIEW Node)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= Page->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(Page->Buffer, *Offset), Page->Length - *Offset);
    Status = ZpBrowser_ReadDocumentNode(&Reader, Node);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}
