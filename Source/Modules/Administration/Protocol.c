#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Administration.h"

#include "../../KNSoft.ZPigeon.Protocol/Core/Protocol.inl"

#define ZP_ADMINISTRATION_RECORD_STATE 0x01
#define ZP_ADMINISTRATION_RECORD_FLAGS 0x02
#define ZP_ADMINISTRATION_RECORD_VALUE 0x04
#define ZP_ADMINISTRATION_RECORD_IDENTITY 0x08
#define ZP_ADMINISTRATION_RECORD_NAME 0x10
#define ZP_ADMINISTRATION_RECORD_DESCRIPTION 0x20
#define ZP_ADMINISTRATION_RECORD_DETAIL 0x40
#define ZP_ADMINISTRATION_RECORD_DATA 0x80
#define ZP_ADMINISTRATION_RECORD_VALID_MASK 0xFF

static
LOGICAL
ZpAdministration_IsKindValid(
    _In_ ZP_ADMINISTRATION_KIND Kind)
{
    return Kind >= ZpAdministrationKindUser && Kind <= ZpAdministrationKindClipboardFile;
}

static
LOGICAL
ZpAdministration_IsActionValid(
    _In_ ZP_ADMINISTRATION_ACTION Action)
{
    return Action >= ZpAdministrationActionCreate && Action <= ZpAdministrationActionUnlock;
}

static
LOGICAL
ZpAdministration_IsStringValid(
    _In_reads_opt_(Length) PCWCH String,
    _In_ ULONG Length)
{
    return Length <= ZP_CODEC_MAX_ELEMENT_COUNT && (Length == 0 || String != NULL);
}

static
NTSTATUS
ZpAdministration_GetRecordSize(
    _In_ PCZP_ADMINISTRATION_RECORD Record,
    _Out_ PULONG Size)
{
    ULONG StringCount;
    ULONGLONG RequiredSize;

    if (!ZpAdministration_IsKindValid(Record->Kind) ||
        !ZpAdministration_IsStringValid(Record->Identity, Record->IdentityLength) ||
        !ZpAdministration_IsStringValid(Record->Name, Record->NameLength) ||
        !ZpAdministration_IsStringValid(Record->Description, Record->DescriptionLength) ||
        !ZpAdministration_IsStringValid(Record->Detail, Record->DetailLength) ||
        Record->DataLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (Record->DataLength != 0 && Record->Data == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    StringCount = (Record->IdentityLength != 0) + (Record->NameLength != 0) +
                  (Record->DescriptionLength != 0) + (Record->DetailLength != 0);
    RequiredSize = 2 + (Record->State != 0 ? sizeof(ULONG) : 0) +
                   (Record->Flags != 0 ? sizeof(ULONG) : 0) +
                   (Record->Value != 0 ? sizeof(ULONGLONG) : 0) +
                   (ULONGLONG)StringCount * sizeof(ULONG) +
                   ((ULONGLONG)Record->IdentityLength + Record->NameLength +
                    Record->DescriptionLength + Record->DetailLength) * sizeof(WCHAR) +
                   (Record->DataLength != 0 ? sizeof(ULONG) + Record->DataLength : 0);
    if (RequiredSize > MAXULONG)
    {
        return STATUS_BUFFER_OVERFLOW;
    }
    *Size = (ULONG)RequiredSize;
    return STATUS_SUCCESS;
}

static
VOID
ZpAdministration_WriteRecord(
    _Inout_ PBYTE* Cursor,
    _In_ PCZP_ADMINISTRATION_RECORD Record)
{
    BYTE Fields = (Record->State != 0 ? ZP_ADMINISTRATION_RECORD_STATE : 0) |
                  (Record->Flags != 0 ? ZP_ADMINISTRATION_RECORD_FLAGS : 0) |
                  (Record->Value != 0 ? ZP_ADMINISTRATION_RECORD_VALUE : 0) |
                  (Record->IdentityLength != 0 ? ZP_ADMINISTRATION_RECORD_IDENTITY : 0) |
                  (Record->NameLength != 0 ? ZP_ADMINISTRATION_RECORD_NAME : 0) |
                  (Record->DescriptionLength != 0 ? ZP_ADMINISTRATION_RECORD_DESCRIPTION : 0) |
                  (Record->DetailLength != 0 ? ZP_ADMINISTRATION_RECORD_DETAIL : 0) |
                  (Record->DataLength != 0 ? ZP_ADMINISTRATION_RECORD_DATA : 0);

    ZpWire_WriteByte(Cursor, (BYTE)Record->Kind);
    ZpWire_WriteByte(Cursor, Fields);
    if (FlagOn(Fields, ZP_ADMINISTRATION_RECORD_STATE)) ZpWire_WriteUInt32(Cursor, Record->State);
    if (FlagOn(Fields, ZP_ADMINISTRATION_RECORD_FLAGS)) ZpWire_WriteUInt32(Cursor, Record->Flags);
    if (FlagOn(Fields, ZP_ADMINISTRATION_RECORD_VALUE)) ZpWire_WriteUInt64(Cursor, Record->Value);
    if (FlagOn(Fields, ZP_ADMINISTRATION_RECORD_IDENTITY))
        ZpWire_WriteString(Cursor, Record->Identity, Record->IdentityLength);
    if (FlagOn(Fields, ZP_ADMINISTRATION_RECORD_NAME))
        ZpWire_WriteString(Cursor, Record->Name, Record->NameLength);
    if (FlagOn(Fields, ZP_ADMINISTRATION_RECORD_DESCRIPTION))
        ZpWire_WriteString(Cursor, Record->Description, Record->DescriptionLength);
    if (FlagOn(Fields, ZP_ADMINISTRATION_RECORD_DETAIL))
        ZpWire_WriteString(Cursor, Record->Detail, Record->DetailLength);
    if (FlagOn(Fields, ZP_ADMINISTRATION_RECORD_DATA))
        ZpWire_WriteByteString(Cursor, Record->Data, Record->DataLength);
}

static
NTSTATUS
ZpAdministration_ReadRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_ADMINISTRATION_RECORD_VIEW Record)
{
    ZP_ADMINISTRATION_RECORD_VIEW Local = { 0 };
    BYTE Fields, Kind;
    NTSTATUS Status;

    Status = ZpCodec_ReadByte(Reader, &Kind);
    if (NT_SUCCESS(Status))
    {
        Local.Kind = Kind;
        Status = ZpCodec_ReadByte(Reader, &Fields);
    }
    if (NT_SUCCESS(Status) && (Fields & ~ZP_ADMINISTRATION_RECORD_VALID_MASK) != 0) return STATUS_DATA_ERROR;
    if (NT_SUCCESS(Status) && FlagOn(Fields, ZP_ADMINISTRATION_RECORD_STATE))
        Status = ZpCodec_ReadUInt32(Reader, &Local.State);
    if (NT_SUCCESS(Status) && FlagOn(Fields, ZP_ADMINISTRATION_RECORD_FLAGS))
        Status = ZpCodec_ReadUInt32(Reader, &Local.Flags);
    if (NT_SUCCESS(Status) && FlagOn(Fields, ZP_ADMINISTRATION_RECORD_VALUE))
        Status = ZpCodec_ReadUInt64(Reader, &Local.Value);
    if (NT_SUCCESS(Status) && FlagOn(Fields, ZP_ADMINISTRATION_RECORD_IDENTITY))
        Status = ZpCodec_ReadString(Reader, &Local.Identity);
    if (NT_SUCCESS(Status) && FlagOn(Fields, ZP_ADMINISTRATION_RECORD_NAME))
        Status = ZpCodec_ReadString(Reader, &Local.Name);
    if (NT_SUCCESS(Status) && FlagOn(Fields, ZP_ADMINISTRATION_RECORD_DESCRIPTION))
        Status = ZpCodec_ReadString(Reader, &Local.Description);
    if (NT_SUCCESS(Status) && FlagOn(Fields, ZP_ADMINISTRATION_RECORD_DETAIL))
        Status = ZpCodec_ReadString(Reader, &Local.Detail);
    if (NT_SUCCESS(Status) && FlagOn(Fields, ZP_ADMINISTRATION_RECORD_DATA))
        Status = ZpCodec_ReadByteString(Reader, &Local.Data);
    if (NT_SUCCESS(Status) && !ZpAdministration_IsKindValid(Local.Kind)) return STATUS_DATA_ERROR;
    if (NT_SUCCESS(Status) && Record != NULL) *Record = Local;
    return Status;
}

NTSTATUS
ZpAdministration_EncodeRecord(
    _In_ PCZP_ADMINISTRATION_RECORD Record,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor = Buffer;
    NTSTATUS Status;

    Status = ZpAdministration_GetRecordSize(Record, BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }
    if (BufferSize < *BytesWritten)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpAdministration_WriteRecord(&Cursor, Record);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpAdministration_EncodeList(
    _In_reads_opt_(RecordCount) PCZP_ADMINISTRATION_RECORD Records,
    _In_ ULONG RecordCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = sizeof(ULONG);
    ULONG Index;

    if (RecordCount > ZP_CODEC_MAX_ELEMENT_COUNT || (RecordCount != 0 && Records == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < RecordCount; Index++)
    {
        ULONG RecordSize;
        NTSTATUS Status = ZpAdministration_GetRecordSize(&Records[Index], &RecordSize);

        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        RequiredSize += RecordSize;
        if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, RecordCount);
    for (Index = 0; Index < RecordCount; Index++)
    {
        ZpAdministration_WriteRecord(&Cursor, &Records[Index]);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpAdministration_DecodeList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_ADMINISTRATION_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;
    ULONG Count, Index;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpAdministration_ReadRecord(&Reader, NULL);
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
ZpAdministration_GetNextRecord(
    _In_ PCZP_ADMINISTRATION_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_ADMINISTRATION_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpAdministration_ReadRecord(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

NTSTATUS
ZpAdministration_EncodeControl(
    _In_ ZP_ADMINISTRATION_ACTION Action,
    _In_reads_opt_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_reads_opt_(ArgumentLength) PCWCH Argument,
    _In_ ULONG ArgumentLength,
    _In_reads_opt_(SecretLength) PCWCH Secret,
    _In_ ULONG SecretLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (!ZpAdministration_IsActionValid(Action) ||
        (IdentityLength == 0 && Action != ZpAdministrationActionRefresh &&
         Action != ZpAdministrationActionCheck) ||
        !ZpAdministration_IsStringValid(Identity, IdentityLength) ||
        !ZpAdministration_IsStringValid(Argument, ArgumentLength) ||
        !ZpAdministration_IsStringValid(Secret, SecretLength))
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteByte(&Writer, (BYTE)Action);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Identity, IdentityLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Argument, ArgumentLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Secret, SecretLength);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpAdministration_DecodeControl(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    {
        BYTE Action;

        Status = ZpCodec_ReadByte(&Reader, &Action);
        Control->Action = Action;
    }
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Control->Identity);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Control->Argument);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Control->Secret);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength ||
        !ZpAdministration_IsActionValid(Control->Action) ||
        (Control->Identity.Length == 0 && Control->Action != ZpAdministrationActionRefresh &&
         Control->Action != ZpAdministrationActionCheck))
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpAdministration_EncodeDataControl(
    _In_ ZP_ADMINISTRATION_ACTION Action,
    _In_ ULONG Flags,
    _In_reads_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (!ZpAdministration_IsActionValid(Action) || IdentityLength == 0 ||
        !ZpAdministration_IsStringValid(Identity, IdentityLength) ||
        DataLength > ZP_CODEC_MAX_ELEMENT_COUNT || (DataLength != 0 && Data == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteByte(&Writer, (BYTE)Action);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Identity, IdentityLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByteString(&Writer, Data, DataLength);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpAdministration_DecodeDataControl(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_ADMINISTRATION_DATA_CONTROL_VIEW Control)
{
    ZP_CODEC_READER Reader;
    BYTE Action;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &Action);
    Control->Action = Action;
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Control->Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Control->Identity);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByteString(&Reader, &Control->Data);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength ||
        !ZpAdministration_IsActionValid(Control->Action) || Control->Identity.Length == 0)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpAdministration_EncodeQuery(
    _In_reads_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (!ZpAdministration_IsStringValid(Identity, IdentityLength) || IdentityLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteString(&Writer, Identity, IdentityLength);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpAdministration_DecodeQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Identity)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, Identity);
    return NT_SUCCESS(Status) && Reader.Offset == PayloadLength && Identity->Length != 0 ?
               STATUS_SUCCESS :
               NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
}
