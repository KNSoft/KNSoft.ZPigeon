#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Video.h"

static
NTSTATUS
ZpVideo_ReadDevice(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_VIDEO_DEVICE_VIEW Device)
{
    ZP_VIDEO_DEVICE_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadString(Reader, &Local.Id);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Name);
    if (NT_SUCCESS(Status) && (Local.Id.Length == 0 || Local.Id.Length > ZP_VIDEO_MAX_ID_LENGTH ||
        Local.Name.Length > ZP_VIDEO_MAX_NAME_LENGTH)) return STATUS_DATA_ERROR;
    if (NT_SUCCESS(Status) && Device != NULL) *Device = Local;
    return Status;
}

NTSTATUS
ZpVideo_EncodeDeviceList(
    _In_reads_opt_(Count) PCZP_VIDEO_DEVICE Devices,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONG Index;
    NTSTATUS Status;

    if (Count > ZP_VIDEO_MAX_DEVICES || (Count != 0 && Devices == NULL)) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteArrayCount(&Writer, Count);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        PCZP_VIDEO_DEVICE Device = &Devices[Index];

        if (Device->Id == NULL || Device->IdLength == 0 || Device->IdLength > ZP_VIDEO_MAX_ID_LENGTH ||
            Device->NameLength > ZP_VIDEO_MAX_NAME_LENGTH || (Device->NameLength != 0 && Device->Name == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpCodec_WriteString(&Writer, Device->Id, Device->IdLength);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Device->Name, Device->NameLength);
    }
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpVideo_DecodeDeviceList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_VIDEO_DEVICE_LIST_VIEW List)
{
    ZP_CODEC_READER Reader;
    ULONG Count, Index, Offset;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    if (!NT_SUCCESS(Status) || Count > ZP_VIDEO_MAX_DEVICES) return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    Offset = Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++) Status = ZpVideo_ReadDevice(&Reader, NULL);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength) return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    List->Buffer = Add2Ptr(Payload, Offset);
    List->Length = PayloadLength - Offset;
    List->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpVideo_GetNextDevice(
    _In_ PCZP_VIDEO_DEVICE_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_VIDEO_DEVICE_VIEW Device)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpVideo_ReadDevice(&Reader, Device);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

NTSTATUS
ZpVideo_EncodeStreamRequest(
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ULONG MaxDimension,
    _In_ USHORT FrameRate,
    _In_ USHORT Quality,
    _In_ ULONG DirectStreamId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (DeviceId == NULL || DeviceIdLength == 0 || DeviceIdLength > ZP_VIDEO_MAX_ID_LENGTH ||
        MaxDimension == 0 || MaxDimension > ZP_VIDEO_MAX_DIMENSION || FrameRate == 0 ||
        FrameRate > ZP_VIDEO_MAX_FRAME_RATE || Quality == 0 || Quality > 100) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, MaxDimension);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, DirectStreamId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, FrameRate);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, Quality);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, DeviceId, DeviceIdLength);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpVideo_DecodeStreamRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_VIDEO_STREAM_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &Request->MaxDimension);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Request->DirectStreamId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &Request->FrameRate);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &Request->Quality);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->DeviceId);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength || Request->DeviceId.Length == 0 ||
        Request->DeviceId.Length > ZP_VIDEO_MAX_ID_LENGTH || Request->MaxDimension == 0 ||
        Request->MaxDimension > ZP_VIDEO_MAX_DIMENSION || Request->FrameRate == 0 ||
        Request->FrameRate > ZP_VIDEO_MAX_FRAME_RATE || Request->Quality == 0 || Request->Quality > 100)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpVideo_EncodeChannel(
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
ZpVideo_DecodeChannel(
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

NTSTATUS
ZpVideo_EncodeFrame(
    _In_ PCZP_VIDEO_FRAME Frame,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (Frame->Width == 0 || Frame->Height == 0 || Frame->Width > ZP_VIDEO_MAX_DIMENSION ||
        Frame->Height > ZP_VIDEO_MAX_DIMENSION || Frame->DataLength == 0 ||
        Frame->DataLength > ZP_VIDEO_MAX_FRAME_SIZE) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, Frame->Width);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Frame->Height);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Frame->DataLength);
    *BytesWritten = Writer.Offset;
    return Status;
}
