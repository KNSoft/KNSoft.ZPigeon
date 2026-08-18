#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Browser.h"

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
    return Kind >= ZpBrowserKindBrowser && Kind <= ZpBrowserKindCookie;
}

static
LOGICAL
ZpBrowser_IsStringValid(
    _In_reads_opt_(Length) PCWCH String,
    _In_ ULONG Length)
{
    return Length <= ZP_CODEC_MAX_ELEMENT_COUNT && (Length == 0 || String != NULL);
}

static
NTSTATUS
ZpBrowser_WriteRecord(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ PCZP_BROWSER_RECORD Record)
{
    NTSTATUS Status;

    Status = ZpCodec_WriteUInt16(Writer, Record->Kind);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(Writer, Record->Browser);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->State);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(Writer, Record->Id);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(Writer, Record->Time);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(Writer, Record->Value);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(Writer, Record->Identity, Record->IdentityLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(Writer, Record->Name, Record->NameLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(Writer, Record->Location, Record->LocationLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(Writer, Record->Detail, Record->DetailLength);
    return Status;
}

static
NTSTATUS
ZpBrowser_ReadRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_BROWSER_RECORD_VIEW Record)
{
    ZP_BROWSER_RECORD_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt16(Reader, &Local.Kind);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(Reader, &Local.Browser);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.State);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.Id);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.Time);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.Value);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Identity);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Name);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Location);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Detail);
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
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;
    ULONG Index;

    if (RecordCount > ZP_CODEC_MAX_ELEMENT_COUNT || (RecordCount != 0 && Records == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, NextCursor);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteArrayCount(&Writer, RecordCount);
    for (Index = 0; NT_SUCCESS(Status) && Index < RecordCount; Index++)
    {
        if (!ZpBrowser_IsKindValid(Records[Index].Kind) ||
            !ZpBrowser_IsTypeValid(Records[Index].Browser) ||
            !ZpBrowser_IsStringValid(Records[Index].Identity, Records[Index].IdentityLength) ||
            !ZpBrowser_IsStringValid(Records[Index].Name, Records[Index].NameLength) ||
            !ZpBrowser_IsStringValid(Records[Index].Location, Records[Index].LocationLength) ||
            !ZpBrowser_IsStringValid(Records[Index].Detail, Records[Index].DetailLength))
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpBrowser_WriteRecord(&Writer, &Records[Index]);
    }
    *BytesWritten = Writer.Offset;
    return Status;
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
ZpBrowser_GetRecord(
    _In_ PCZP_BROWSER_PAGE_VIEW Page,
    _In_ ULONG Index,
    _Out_ PZP_BROWSER_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Current;

    if (Index >= Page->Count) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Page->Buffer, Page->Length);
    for (Current = 0; NT_SUCCESS(Status) && Current <= Index; Current++)
    {
        Status = ZpBrowser_ReadRecord(&Reader, Current == Index ? Record : NULL);
    }
    return Status;
}

NTSTATUS
ZpBrowser_EncodeQuery(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ ZP_BROWSER_KIND Kind,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_ ULONGLONG Cursor,
    _In_ ULONG Limit,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (!ZpBrowser_IsTypeValid(Browser) ||
        Kind < ZpBrowserKindHistory || Kind > ZpBrowserKindCookie ||
        !ZpBrowser_IsStringValid(Profile, ProfileLength) || ProfileLength == 0 ||
        Limit == 0 || Limit > ZP_BROWSER_PAGE_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt16(&Writer, Browser);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, Kind);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Cursor);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Limit);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Profile, ProfileLength);
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
    Status = ZpCodec_ReadUInt16(&Reader, &Query->Browser);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &Query->Kind);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Query->Cursor);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Query->Limit);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Query->Profile);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength ||
        !ZpBrowser_IsTypeValid(Query->Browser) ||
        Query->Kind < ZpBrowserKindHistory || Query->Kind > ZpBrowserKindCookie ||
        Query->Profile.Length == 0 || Query->Limit == 0 || Query->Limit > ZP_BROWSER_PAGE_SIZE)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}
