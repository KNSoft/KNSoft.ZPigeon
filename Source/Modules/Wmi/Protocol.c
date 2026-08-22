#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Wmi.h"

#include "../../KNSoft.ZPigeon.Protocol/Core/Protocol.inl"

static
VOID
ZpWmi_WriteCell(
    _Inout_ PBYTE* Cursor,
    _In_ PCZP_WMI_CELL Cell)
{
    ZpWire_WriteUInt32(Cursor, Cell->Type);
    ZpWire_WriteString(Cursor, Cell->Name, Cell->NameLength);
    ZpWire_WriteString(Cursor, Cell->Value, Cell->ValueLength);
}

static
NTSTATUS
ZpWmi_ReadCell(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_WMI_CELL Cell)
{
    ZP_WMI_CELL Local;
    ZP_STRING_VIEW Name, Value;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt32(Reader, &Local.Type);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Name);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Value);
    if (NT_SUCCESS(Status) && (Name.Length == 0 || Name.Length > ZP_WMI_MAX_CELL_LENGTH ||
        Value.Length > ZP_WMI_MAX_CELL_LENGTH))
    {
        return STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Cell != NULL)
    {
        Local.Name = (PCWCH)Name.Buffer;
        Local.NameLength = Name.Length;
        Local.Value = (PCWCH)Value.Buffer;
        Local.ValueLength = Value.Length;
        *Cell = Local;
    }
    return Status;
}

static
NTSTATUS
ZpWmi_ReadRow(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_WMI_ROW_VIEW Row)
{
    const BYTE* Buffer;
    ULONG Count, Index, Offset;
    NTSTATUS Status;

    Status = ZpCodec_ReadArrayCount(Reader, &Count);
    if (!NT_SUCCESS(Status) || Count > ZP_WMI_MAX_CELLS)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    Buffer = Add2Ptr(Reader->Buffer, Reader->Offset);
    Offset = Reader->Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpWmi_ReadCell(Reader, NULL);
    }
    if (NT_SUCCESS(Status) && Row != NULL)
    {
        Row->Buffer = Buffer;
        Row->Length = Reader->Offset - Offset;
        Row->CellCount = Count;
    }
    return Status;
}

NTSTATUS
ZpWmi_EncodePage(
    _In_reads_opt_(RowCount) PCZP_WMI_ROW Rows,
    _In_ ULONG RowCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = sizeof(ULONG);
    ULONG RowIndex, CellIndex;

    if (RowCount > ZP_WMI_MAX_ROWS || (RowCount != 0 && Rows == NULL)) return STATUS_INVALID_PARAMETER;
    for (RowIndex = 0; RowIndex < RowCount; RowIndex++)
    {
        if (Rows[RowIndex].CellCount > ZP_WMI_MAX_CELLS ||
            (Rows[RowIndex].CellCount != 0 && Rows[RowIndex].Cells == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += sizeof(ULONG);
        for (CellIndex = 0; CellIndex < Rows[RowIndex].CellCount; CellIndex++)
        {
            PCZP_WMI_CELL Cell = &Rows[RowIndex].Cells[CellIndex];

            if (Cell->NameLength == 0 || Cell->NameLength > ZP_WMI_MAX_CELL_LENGTH ||
                Cell->ValueLength > ZP_WMI_MAX_CELL_LENGTH || Cell->Name == NULL ||
                (Cell->ValueLength != 0 && Cell->Value == NULL))
            {
                return STATUS_INVALID_PARAMETER;
            }
            RequiredSize += 3 * sizeof(ULONG) +
                            ((ULONGLONG)Cell->NameLength + Cell->ValueLength) * sizeof(WCHAR);
            if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12) return STATUS_BUFFER_OVERFLOW;
        }
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, RowCount);
    for (RowIndex = 0; RowIndex < RowCount; RowIndex++)
    {
        ZpWire_WriteUInt32(&Cursor, Rows[RowIndex].CellCount);
        for (CellIndex = 0; CellIndex < Rows[RowIndex].CellCount; CellIndex++)
        {
            ZpWmi_WriteCell(&Cursor, &Rows[RowIndex].Cells[CellIndex]);
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpWmi_DecodePage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_WMI_PAGE_VIEW Page)
{
    ZP_CODEC_READER Reader;
    ULONG Count, Index;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    if (!NT_SUCCESS(Status) || Count > ZP_WMI_MAX_ROWS)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    Page->Buffer = Add2Ptr(Payload, Reader.Offset);
    Page->Length = PayloadLength - Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpWmi_ReadRow(&Reader, NULL);
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    Page->RowCount = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpWmi_GetNextRow(
    _In_ PCZP_WMI_PAGE_VIEW Page,
    _Inout_ PULONG Offset,
    _Out_ PZP_WMI_ROW_VIEW Row)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= Page->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(Page->Buffer, *Offset), Page->Length - *Offset);
    Status = ZpWmi_ReadRow(&Reader, Row);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

NTSTATUS
ZpWmi_GetNextCell(
    _In_ PCZP_WMI_ROW_VIEW Row,
    _Inout_ PULONG Offset,
    _Out_ PZP_WMI_CELL Cell)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= Row->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(Row->Buffer, *Offset), Row->Length - *Offset);
    Status = ZpWmi_ReadCell(&Reader, Cell);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

NTSTATUS
ZpWmi_EncodeRequest(
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_ ULONG Limit,
    _In_ ULONG Flags,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (Namespace == NULL || NamespaceLength == 0 || NamespaceLength > ZP_WMI_MAX_NAMESPACE_LENGTH ||
        QueryLength > ZP_WMI_MAX_QUERY_LENGTH || (QueryLength != 0 && Query == NULL) ||
        Limit == 0 || Limit > ZP_WMI_MAX_ROWS || Flags & ~ZP_WMI_FLAG_SYSTEM_PROPERTIES)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, Limit);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Namespace, NamespaceLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Query, QueryLength);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpWmi_DecodeRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_WMI_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &Request->Limit);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Request->Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->Namespace);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->Query);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength || Request->Namespace.Length == 0 ||
        Request->Namespace.Length > ZP_WMI_MAX_NAMESPACE_LENGTH ||
        Request->Query.Length > ZP_WMI_MAX_QUERY_LENGTH || Request->Limit == 0 ||
        Request->Limit > ZP_WMI_MAX_ROWS || Request->Flags & ~ZP_WMI_FLAG_SYSTEM_PROPERTIES)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}
