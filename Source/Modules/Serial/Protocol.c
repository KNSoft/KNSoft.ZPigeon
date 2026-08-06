#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Serial.h"

LOGICAL
ZpSerial_IsPortNameValid(
    _In_reads_(Length) PCWCH Name,
    _In_ ULONG Length)
{
    ULONG Index;

    if (Name == NULL || Length < 4 || Length > 8 ||
        (Name[0] != L'C' && Name[0] != L'c') ||
        (Name[1] != L'O' && Name[1] != L'o') ||
        (Name[2] != L'M' && Name[2] != L'm')) return FALSE;
    for (Index = 3; Index < Length; Index++)
    {
        if (Name[Index] < L'0' || Name[Index] > L'9') return FALSE;
    }
    return TRUE;
}

static
NTSTATUS
ZpSerial_ReadPort(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_SERIAL_PORT_VIEW Port)
{
    ZP_SERIAL_PORT_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadString(Reader, &Local.Name);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Device);
    if (NT_SUCCESS(Status) &&
        (!ZpSerial_IsPortNameValid((PCWCH)Local.Name.Buffer, Local.Name.Length) ||
         Local.Device.Length == 0 || Local.Device.Length > ZP_SERIAL_MAX_NAME_LENGTH))
    {
        return STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Port != NULL) *Port = Local;
    return Status;
}

NTSTATUS
ZpSerial_EncodePortList(
    _In_reads_opt_(Count) PCZP_SERIAL_PORT Ports,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONG Index;
    NTSTATUS Status;

    if (Count > ZP_SERIAL_MAX_PORTS || (Count != 0 && Ports == NULL)) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteArrayCount(&Writer, Count);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        PCZP_SERIAL_PORT Port = &Ports[Index];

        if (!ZpSerial_IsPortNameValid(Port->Name, Port->NameLength) || Port->Device == NULL ||
            Port->DeviceLength == 0 || Port->DeviceLength > ZP_SERIAL_MAX_NAME_LENGTH)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpCodec_WriteString(&Writer, Port->Name, Port->NameLength);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Port->Device, Port->DeviceLength);
    }
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpSerial_DecodePortList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERIAL_PORT_LIST_VIEW List)
{
    ZP_CODEC_READER Reader;
    ULONG Count, Index, Offset;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    if (!NT_SUCCESS(Status) || Count > ZP_SERIAL_MAX_PORTS) return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    Offset = Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++) Status = ZpSerial_ReadPort(&Reader, NULL);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength) return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    List->Buffer = Add2Ptr(Payload, Offset);
    List->Length = PayloadLength - Offset;
    List->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpSerial_GetNextPort(
    _In_ PCZP_SERIAL_PORT_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_SERIAL_PORT_VIEW Port)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpSerial_ReadPort(&Reader, Port);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

static
LOGICAL
ZpSerial_IsConfigurationValid(
    _In_reads_(PortLength) PCWCH Port,
    _In_ ULONG PortLength,
    _In_ ULONG BaudRate,
    _In_ BYTE DataBits,
    _In_ BYTE Parity,
    _In_ BYTE StopBits,
    _In_ BYTE FlowControl)
{
    return ZpSerial_IsPortNameValid(Port, PortLength) && BaudRate != 0 &&
           BaudRate <= ZP_SERIAL_MAX_BAUD_RATE && DataBits >= 5 && DataBits <= 8 &&
           Parity <= ZP_SERIAL_PARITY_SPACE && StopBits <= ZP_SERIAL_STOP_BITS_TWO &&
           FlowControl <= ZP_SERIAL_FLOW_DSR_DTR;
}

NTSTATUS
ZpSerial_EncodeOpenRequest(
    _In_reads_(PortLength) PCWCH Port,
    _In_ ULONG PortLength,
    _In_ ULONG BaudRate,
    _In_ BYTE DataBits,
    _In_ BYTE Parity,
    _In_ BYTE StopBits,
    _In_ BYTE FlowControl,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (!ZpSerial_IsConfigurationValid(Port, PortLength, BaudRate, DataBits, Parity, StopBits, FlowControl))
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteString(&Writer, Port, PortLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, BaudRate);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, DataBits);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, Parity);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, StopBits);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, FlowControl);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpSerial_DecodeOpenRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERIAL_OPEN_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, &Request->Port);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Request->BaudRate);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Request->DataBits);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Request->Parity);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Request->StopBits);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Request->FlowControl);
    return NT_SUCCESS(Status) &&
           (!ZpSerial_IsConfigurationValid((PCWCH)Request->Port.Buffer,
                                           Request->Port.Length,
                                           Request->BaudRate,
                                           Request->DataBits,
                                           Request->Parity,
                                           Request->StopBits,
                                           Request->FlowControl) ||
            Reader.Offset != PayloadLength) ? STATUS_DATA_ERROR : Status;
}

NTSTATUS
ZpSerial_EncodeChannel(
    _In_ ULONG ChannelId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;

    if (ChannelId == 0) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    *BytesWritten = sizeof(ChannelId);
    return ZpCodec_WriteUInt32(&Writer, ChannelId);
}

NTSTATUS
ZpSerial_DecodeChannel(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ChannelId)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != sizeof(*ChannelId)) return STATUS_DATA_ERROR;
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, ChannelId);
    return NT_SUCCESS(Status) && *ChannelId != 0 ? STATUS_SUCCESS : STATUS_DATA_ERROR;
}
