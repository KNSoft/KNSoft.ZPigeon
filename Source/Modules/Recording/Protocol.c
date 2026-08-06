#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Recording.h"

static
LOGICAL
ZpRecording_IsSourceValid(
    _In_ ZP_RECORDING_SOURCE Source)
{
    return Source >= ZpRecordingSourceAudioOutput && Source <= ZpRecordingSourceWindow;
}

static
LOGICAL
ZpRecording_IsCodecValid(
    _In_ ZP_RECORDING_CODEC Codec)
{
    return Codec >= ZpRecordingCodecAuto && Codec <= ZpRecordingCodecWmvScreen;
}

NTSTATUS
ZpRecording_EncodeCapabilities(
    _In_ ULONG Codecs,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;

    *BytesWritten = sizeof(ULONG);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < sizeof(ULONG)) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteUInt32(&Writer, Codecs);
}

NTSTATUS
ZpRecording_DecodeCapabilities(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG Codecs)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, Codecs);
    return NT_SUCCESS(Status) && Reader.Offset == PayloadLength ? STATUS_SUCCESS :
                                                                  NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
}

NTSTATUS
ZpRecording_EncodeStart(
    _In_ PCZP_RECORDING_START Start,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (!ZpRecording_IsSourceValid(Start->Source) || !ZpRecording_IsCodecValid(Start->Codec) ||
        Start->AudioSource > ZpRecordingAudioInput ||
        (Start->Flags & ~ZP_RECORDING_FLAG_CAPTURE_CURSOR) != 0 ||
        (Start->SourceIdLength != 0 && Start->SourceId == NULL) ||
        (Start->AudioDeviceIdLength != 0 && Start->AudioDeviceId == NULL) ||
        Start->SourceIdLength > ZP_RECORDING_MAX_DEVICE_ID_LENGTH ||
        Start->AudioDeviceIdLength > ZP_RECORDING_MAX_DEVICE_ID_LENGTH)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 5 * sizeof(BYTE) + 5 * sizeof(ULONG) + sizeof(ULONGLONG) +
                   ((ULONGLONG)Start->SourceIdLength + Start->AudioDeviceIdLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteByte(&Writer, Start->Source);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, Start->Codec);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, Start->FrameRate);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, Start->AudioSource);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, Start->Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Start->MaxDimension);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Start->VideoBitRate);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Start->AudioBitRate);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Start->WindowHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Start->SourceId, Start->SourceIdLength);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Start->AudioDeviceId, Start->AudioDeviceIdLength);
    }
    return Status;
}

NTSTATUS
ZpRecording_DecodeStart(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_RECORDING_START_VIEW Start)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &Start->Source);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Start->Codec);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Start->FrameRate);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Start->AudioSource);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Start->Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Start->MaxDimension);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Start->VideoBitRate);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Start->AudioBitRate);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Start->WindowHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Start->SourceId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Start->AudioDeviceId);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength || Start->AudioSource > ZpRecordingAudioInput ||
        !ZpRecording_IsSourceValid(Start->Source) || !ZpRecording_IsCodecValid(Start->Codec) ||
        (Start->Flags & ~ZP_RECORDING_FLAG_CAPTURE_CURSOR) != 0 ||
        Start->SourceId.Length > ZP_RECORDING_MAX_DEVICE_ID_LENGTH ||
        Start->AudioDeviceId.Length > ZP_RECORDING_MAX_DEVICE_ID_LENGTH)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpRecording_WriteRecord(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ PCZP_RECORDING_RECORD Record)
{
    NTSTATUS Status;

    Status = ZpCodec_WriteUInt32(Writer, Record->RecordingId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(Writer, Record->Source);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(Writer, Record->Codec);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(Writer, Record->State);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(Writer, Record->Status.Type);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->Status.Code);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(Writer, Record->StartTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(Writer, Record->Duration);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(Writer, Record->FileSize);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(Writer, Record->Path, Record->PathLength);
    return Status;
}

static
NTSTATUS
ZpRecording_ReadRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_RECORDING_RECORD_VIEW Record)
{
    ZP_RECORDING_RECORD_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt32(Reader, &Local.RecordingId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(Reader, &Local.Source);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(Reader, &Local.Codec);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(Reader, &Local.State);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(Reader, &Local.Status.Type);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Status.Code);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.StartTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.Duration);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.FileSize);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Path);
    if (NT_SUCCESS(Status) &&
        (Local.RecordingId == 0 || !ZpRecording_IsSourceValid(Local.Source) ||
         !ZpRecording_IsCodecValid(Local.Codec) || Local.State < ZpRecordingStateRecording ||
         Local.State > ZpRecordingStateFailed || Local.Path.Length > ZP_RECORDING_MAX_PATH_LENGTH))
    {
        Status = STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Record != NULL) *Record = Local;
    return Status;
}

NTSTATUS
ZpRecording_EncodeRecords(
    _In_reads_opt_(Count) PCZP_RECORDING_RECORD Records,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize = sizeof(ULONG);
    ULONG Index;
    NTSTATUS Status;

    if (Count > ZP_RECORDING_MAX_JOBS || (Count != 0 && Records == NULL)) return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < Count; Index++)
    {
        if (Records[Index].RecordingId == 0 || !ZpRecording_IsSourceValid(Records[Index].Source) ||
            !ZpRecording_IsCodecValid(Records[Index].Codec) ||
            Records[Index].State < ZpRecordingStateRecording || Records[Index].State > ZpRecordingStateFailed ||
            Records[Index].PathLength > ZP_RECORDING_MAX_PATH_LENGTH ||
            (Records[Index].PathLength != 0 && Records[Index].Path == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += sizeof(ULONG) * 3 + sizeof(USHORT) + 3 * sizeof(BYTE) + sizeof(ULONGLONG) * 3 +
                        (ULONGLONG)Records[Index].PathLength * sizeof(WCHAR);
    }
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteArrayCount(&Writer, Count);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpRecording_WriteRecord(&Writer, &Records[Index]);
    }
    return Status;
}

NTSTATUS
ZpRecording_DecodeRecords(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_RECORDING_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    ULONG Index;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &View->Count);
    if (NT_SUCCESS(Status) && View->Count > ZP_RECORDING_MAX_JOBS) Status = STATUS_DATA_ERROR;
    if (NT_SUCCESS(Status)) View->Buffer = Add2Ptr(Payload, Reader.Offset);
    for (Index = 0; NT_SUCCESS(Status) && Index < View->Count; Index++)
    {
        Status = ZpRecording_ReadRecord(&Reader, NULL);
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength) return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    View->Length = PayloadLength - sizeof(ULONG);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRecording_GetNextRecord(
    _In_ PCZP_RECORDING_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_RECORDING_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpRecording_ReadRecord(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

NTSTATUS
ZpRecording_EncodeId(
    _In_ ULONG RecordingId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;

    if (RecordingId == 0) return STATUS_INVALID_PARAMETER;
    *BytesWritten = sizeof(ULONG);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < sizeof(ULONG)) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteUInt32(&Writer, RecordingId);
}

NTSTATUS
ZpRecording_DecodeId(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG RecordingId)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, RecordingId);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength || *RecordingId == 0)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}
