#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Administration.h"

static
LOGICAL
ZpAdministration_IsKindValid(
    _In_ ZP_ADMINISTRATION_KIND Kind)
{
    return Kind >= ZpAdministrationKindUser && Kind <= ZpAdministrationKindClipboardState;
}

static
LOGICAL
ZpAdministration_IsActionValid(
    _In_ ZP_ADMINISTRATION_ACTION Action)
{
    return Action >= ZpAdministrationActionCreate && Action <= ZpAdministrationActionDisconnect;
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
ZpAdministration_WriteRecord(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ PCZP_ADMINISTRATION_RECORD Record)
{
    NTSTATUS Status;

    Status = ZpCodec_WriteUInt16(Writer, Record->Kind);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->State);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(Writer, Record->Value);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(Writer, Record->Identity, Record->IdentityLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(Writer, Record->Name, Record->NameLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(Writer, Record->Description, Record->DescriptionLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(Writer, Record->Detail, Record->DetailLength);
    return Status;
}

static
NTSTATUS
ZpAdministration_ReadRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_ADMINISTRATION_RECORD_VIEW Record)
{
    ZP_ADMINISTRATION_RECORD_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt16(Reader, &Local.Kind);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.State);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.Value);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Identity);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Name);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Description);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Detail);
    if (NT_SUCCESS(Status) && !ZpAdministration_IsKindValid(Local.Kind)) return STATUS_DATA_ERROR;
    if (NT_SUCCESS(Status) && Record != NULL) *Record = Local;
    return Status;
}

NTSTATUS
ZpAdministration_EncodeList(
    _In_reads_opt_(RecordCount) PCZP_ADMINISTRATION_RECORD Records,
    _In_ ULONG RecordCount,
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
    Status = ZpCodec_WriteArrayCount(&Writer, RecordCount);
    for (Index = 0; NT_SUCCESS(Status) && Index < RecordCount; Index++)
    {
        if (!ZpAdministration_IsKindValid(Records[Index].Kind) ||
            !ZpAdministration_IsStringValid(Records[Index].Identity, Records[Index].IdentityLength) ||
            !ZpAdministration_IsStringValid(Records[Index].Name, Records[Index].NameLength) ||
            !ZpAdministration_IsStringValid(Records[Index].Description, Records[Index].DescriptionLength) ||
            !ZpAdministration_IsStringValid(Records[Index].Detail, Records[Index].DetailLength))
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpAdministration_WriteRecord(&Writer, &Records[Index]);
    }
    *BytesWritten = Writer.Offset;
    return Status;
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
ZpAdministration_GetRecord(
    _In_ PCZP_ADMINISTRATION_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_ADMINISTRATION_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Current;

    if (Index >= List->Count) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, List->Buffer, List->Length);
    for (Current = 0; NT_SUCCESS(Status) && Current <= Index; Current++)
    {
        Status = ZpAdministration_ReadRecord(&Reader, Current == Index ? Record : NULL);
    }
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
    Status = ZpCodec_WriteUInt16(&Writer, Action);
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
    Status = ZpCodec_ReadUInt16(&Reader, &Control->Action);
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
