#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Audio.h"

static
LOGICAL
ZpAudio_IsFlowValid(
    _In_ ZP_AUDIO_FLOW Flow)
{
    return Flow == ZpAudioFlowRender || Flow == ZpAudioFlowCapture;
}

static
NTSTATUS
ZpAudio_ReadDevice(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_AUDIO_DEVICE_VIEW Device)
{
    ZP_AUDIO_DEVICE_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt16(Reader, &Local.Flow);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.State);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Volume);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Id);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Name);
    if (NT_SUCCESS(Status) && (!ZpAudio_IsFlowValid(Local.Flow) || Local.Volume > ZP_AUDIO_VOLUME_MAX ||
        Local.Id.Length == 0 || Local.Id.Length > ZP_AUDIO_MAX_ID_LENGTH ||
        Local.Name.Length > ZP_AUDIO_MAX_NAME_LENGTH))
    {
        return STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Device != NULL) *Device = Local;
    return Status;
}

NTSTATUS
ZpAudio_EncodeDeviceList(
    _In_reads_opt_(Count) PCZP_AUDIO_DEVICE Devices,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONG Index;
    NTSTATUS Status;

    if (Count > ZP_AUDIO_MAX_DEVICES || (Count != 0 && Devices == NULL)) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteArrayCount(&Writer, Count);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        PCZP_AUDIO_DEVICE Device = &Devices[Index];

        if (!ZpAudio_IsFlowValid(Device->Flow) || Device->Volume > ZP_AUDIO_VOLUME_MAX ||
            Device->Id == NULL || Device->IdLength == 0 || Device->IdLength > ZP_AUDIO_MAX_ID_LENGTH ||
            Device->NameLength > ZP_AUDIO_MAX_NAME_LENGTH || (Device->NameLength != 0 && Device->Name == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpCodec_WriteUInt16(&Writer, Device->Flow);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Device->State);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Device->Flags);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Device->Volume);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Device->Id, Device->IdLength);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Device->Name, Device->NameLength);
    }
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpAudio_DecodeDeviceList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_AUDIO_DEVICE_LIST_VIEW List)
{
    ZP_CODEC_READER Reader;
    ULONG Count, Index, Offset;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    if (!NT_SUCCESS(Status) || Count > ZP_AUDIO_MAX_DEVICES)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    Offset = Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++) Status = ZpAudio_ReadDevice(&Reader, NULL);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    List->Buffer = Add2Ptr(Payload, Offset);
    List->Length = PayloadLength - Offset;
    List->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpAudio_GetNextDevice(
    _In_ PCZP_AUDIO_DEVICE_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_AUDIO_DEVICE_VIEW Device)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpAudio_ReadDevice(&Reader, Device);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

static
NTSTATUS
ZpAudio_ReadSession(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_AUDIO_SESSION_VIEW Session)
{
    ZP_AUDIO_SESSION_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt32(Reader, &Local.ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.State);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Volume);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.DeviceId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Id);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Name);
    if (NT_SUCCESS(Status) && (Local.Volume > ZP_AUDIO_VOLUME_MAX || Local.DeviceId.Length == 0 ||
        Local.DeviceId.Length > ZP_AUDIO_MAX_ID_LENGTH || Local.Id.Length == 0 ||
        Local.Id.Length > ZP_AUDIO_MAX_ID_LENGTH || Local.Name.Length > ZP_AUDIO_MAX_NAME_LENGTH))
    {
        return STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Session != NULL) *Session = Local;
    return Status;
}

NTSTATUS
ZpAudio_EncodeSessionList(
    _In_reads_opt_(Count) PCZP_AUDIO_SESSION Sessions,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONG Index;
    NTSTATUS Status;

    if (Count > ZP_AUDIO_MAX_SESSIONS || (Count != 0 && Sessions == NULL)) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteArrayCount(&Writer, Count);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        PCZP_AUDIO_SESSION Session = &Sessions[Index];

        if (Session->Volume > ZP_AUDIO_VOLUME_MAX || Session->DeviceId == NULL ||
            Session->DeviceIdLength == 0 || Session->DeviceIdLength > ZP_AUDIO_MAX_ID_LENGTH ||
            Session->Id == NULL || Session->IdLength == 0 || Session->IdLength > ZP_AUDIO_MAX_ID_LENGTH ||
            Session->NameLength > ZP_AUDIO_MAX_NAME_LENGTH || (Session->NameLength != 0 && Session->Name == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpCodec_WriteUInt32(&Writer, Session->ProcessId);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Session->State);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Session->Flags);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Session->Volume);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Session->DeviceId, Session->DeviceIdLength);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Session->Id, Session->IdLength);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Session->Name, Session->NameLength);
    }
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpAudio_DecodeSessionList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_AUDIO_SESSION_LIST_VIEW List)
{
    ZP_CODEC_READER Reader;
    ULONG Count, Index, Offset;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    if (!NT_SUCCESS(Status) || Count > ZP_AUDIO_MAX_SESSIONS)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    Offset = Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++) Status = ZpAudio_ReadSession(&Reader, NULL);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    List->Buffer = Add2Ptr(Payload, Offset);
    List->Length = PayloadLength - Offset;
    List->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpAudio_GetNextSession(
    _In_ PCZP_AUDIO_SESSION_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_AUDIO_SESSION_VIEW Session)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpAudio_ReadSession(&Reader, Session);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

NTSTATUS
ZpAudio_EncodeEndpointControl(
    _In_ ZP_AUDIO_FLOW Flow,
    _In_ ZP_AUDIO_ENDPOINT_CONTROL Control,
    _In_ ULONG Value,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (!ZpAudio_IsFlowValid(Flow) || Control < ZpAudioEndpointSetVolume ||
        Control > ZpAudioEndpointSetEnabled || DeviceId == NULL || DeviceIdLength == 0 ||
        DeviceIdLength > ZP_AUDIO_MAX_ID_LENGTH ||
        (Control == ZpAudioEndpointSetVolume && Value > ZP_AUDIO_VOLUME_MAX) ||
        ((Control == ZpAudioEndpointSetMute || Control == ZpAudioEndpointSetEnabled) && Value > 1) ||
        (Control == ZpAudioEndpointSetDefault && Value != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt16(&Writer, Flow);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, Control);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Value);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, DeviceId, DeviceIdLength);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpAudio_DecodeEndpointControl(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_AUDIO_ENDPOINT_CONTROL_VIEW Control)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt16(&Reader, &Control->Flow);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &Control->Control);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Control->Value);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Control->DeviceId);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength || !ZpAudio_IsFlowValid(Control->Flow) ||
        Control->Control < ZpAudioEndpointSetVolume || Control->Control > ZpAudioEndpointSetEnabled ||
        Control->DeviceId.Length == 0 || Control->DeviceId.Length > ZP_AUDIO_MAX_ID_LENGTH ||
        (Control->Control == ZpAudioEndpointSetVolume && Control->Value > ZP_AUDIO_VOLUME_MAX) ||
        ((Control->Control == ZpAudioEndpointSetMute || Control->Control == ZpAudioEndpointSetEnabled) &&
         Control->Value > 1) ||
        (Control->Control == ZpAudioEndpointSetDefault && Control->Value != 0))
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpAudio_EncodeSessionControl(
    _In_ ZP_AUDIO_SESSION_CONTROL Control,
    _In_ ULONG Value,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(SessionIdLength) PCWCH SessionId,
    _In_ ULONG SessionIdLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (Control < ZpAudioSessionSetVolume || Control > ZpAudioSessionSetMute ||
        DeviceId == NULL || DeviceIdLength == 0 || DeviceIdLength > ZP_AUDIO_MAX_ID_LENGTH ||
        SessionId == NULL || SessionIdLength == 0 || SessionIdLength > ZP_AUDIO_MAX_ID_LENGTH ||
        (Control == ZpAudioSessionSetVolume && Value > ZP_AUDIO_VOLUME_MAX) ||
        (Control == ZpAudioSessionSetMute && Value > 1))
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt16(&Writer, Control);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Value);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, DeviceId, DeviceIdLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, SessionId, SessionIdLength);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpAudio_DecodeSessionControl(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_AUDIO_SESSION_CONTROL_VIEW Control)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt16(&Reader, &Control->Control);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Control->Value);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Control->DeviceId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Control->SessionId);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength ||
        Control->Control < ZpAudioSessionSetVolume || Control->Control > ZpAudioSessionSetMute ||
        Control->DeviceId.Length == 0 || Control->DeviceId.Length > ZP_AUDIO_MAX_ID_LENGTH ||
        Control->SessionId.Length == 0 || Control->SessionId.Length > ZP_AUDIO_MAX_ID_LENGTH ||
        (Control->Control == ZpAudioSessionSetVolume && Control->Value > ZP_AUDIO_VOLUME_MAX) ||
        (Control->Control == ZpAudioSessionSetMute && Control->Value > 1))
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpAudio_EncodeStreamRequest(
    _In_ ZP_AUDIO_FLOW Flow,
    _In_reads_opt_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (!ZpAudio_IsFlowValid(Flow) || DeviceIdLength > ZP_AUDIO_MAX_ID_LENGTH ||
        (DeviceIdLength != 0 && DeviceId == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt16(&Writer, Flow);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, DeviceId, DeviceIdLength);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpAudio_DecodeStreamRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_AUDIO_STREAM_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt16(&Reader, &Request->Flow);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->DeviceId);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength || !ZpAudio_IsFlowValid(Request->Flow) ||
        Request->DeviceId.Length > ZP_AUDIO_MAX_ID_LENGTH)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpAudio_EncodeChannel(
    _In_ ULONGLONG ChannelId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;

    if (ChannelId == 0) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    *BytesWritten = sizeof(ChannelId);
    return ZpCodec_WriteUInt64(&Writer, ChannelId);
}

NTSTATUS
ZpAudio_DecodeChannel(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG ChannelId)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != sizeof(*ChannelId)) return STATUS_DATA_ERROR;
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, ChannelId);
    return NT_SUCCESS(Status) && *ChannelId != 0 ? STATUS_SUCCESS : STATUS_DATA_ERROR;
}

NTSTATUS
ZpAudio_EncodePacket(
    _In_ PCZP_AUDIO_PACKET Packet,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (Packet->Format != ZP_AUDIO_FORMAT_PCM16 || Packet->Channels == 0 ||
        Packet->Channels > ZP_AUDIO_MAX_CHANNELS || Packet->SampleRate == 0 ||
        Packet->SampleRate > ZP_AUDIO_MAX_SAMPLE_RATE || Packet->FrameCount == 0 ||
        Packet->FrameCount > ZP_AUDIO_MAX_PACKET_FRAMES ||
        Packet->DataLength != Packet->FrameCount * Packet->Channels * sizeof(SHORT))
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt16(&Writer, Packet->Format);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, Packet->Channels);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Packet->SampleRate);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Packet->FrameCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Packet->DataLength);
    *BytesWritten = Writer.Offset;
    return Status;
}
