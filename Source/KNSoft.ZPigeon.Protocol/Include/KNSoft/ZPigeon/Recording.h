#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_RECORDING_MODULE_ID 18

#define ZP_RECORDING_OPERATION_QUERY_CAPABILITIES 1
#define ZP_RECORDING_OPERATION_START 2
#define ZP_RECORDING_OPERATION_ENUMERATE 3
#define ZP_RECORDING_OPERATION_STOP 4
#define ZP_RECORDING_OPERATION_DELETE 5

#define ZP_RECORDING_MAX_JOBS 64
#define ZP_RECORDING_MAX_DEVICE_ID_LENGTH 1024
#define ZP_RECORDING_MAX_PATH_LENGTH 32767

typedef BYTE ZP_RECORDING_SOURCE, *PZP_RECORDING_SOURCE;

#define ZpRecordingSourceAudioOutput ((ZP_RECORDING_SOURCE)1)
#define ZpRecordingSourceAudioInput ((ZP_RECORDING_SOURCE)2)
#define ZpRecordingSourceCamera ((ZP_RECORDING_SOURCE)3)
#define ZpRecordingSourceWindow ((ZP_RECORDING_SOURCE)4)

typedef BYTE ZP_RECORDING_CODEC, *PZP_RECORDING_CODEC;

#define ZpRecordingCodecAuto ((ZP_RECORDING_CODEC)1)
#define ZpRecordingCodecPcm ((ZP_RECORDING_CODEC)2)
#define ZpRecordingCodecAac ((ZP_RECORDING_CODEC)3)
#define ZpRecordingCodecH264 ((ZP_RECORDING_CODEC)4)
#define ZpRecordingCodecH265 ((ZP_RECORDING_CODEC)5)
#define ZpRecordingCodecWmvScreen ((ZP_RECORDING_CODEC)6)

#define ZP_RECORDING_CODEC_CAPABILITY(Codec) (1UL << ((Codec) - 1))
#define ZP_RECORDING_CODEC_CAPABILITIES_MASK 0x3F

typedef BYTE ZP_RECORDING_AUDIO_SOURCE, *PZP_RECORDING_AUDIO_SOURCE;

#define ZpRecordingAudioNone ((ZP_RECORDING_AUDIO_SOURCE)0)
#define ZpRecordingAudioOutput ((ZP_RECORDING_AUDIO_SOURCE)1)
#define ZpRecordingAudioInput ((ZP_RECORDING_AUDIO_SOURCE)2)

#define ZP_RECORDING_FLAG_CAPTURE_CURSOR 0x0001

typedef BYTE ZP_RECORDING_STATE, *PZP_RECORDING_STATE;

#define ZpRecordingStateRecording ((ZP_RECORDING_STATE)1)
#define ZpRecordingStateFinalizing ((ZP_RECORDING_STATE)2)
#define ZpRecordingStateCompleted ((ZP_RECORDING_STATE)3)
#define ZpRecordingStateInterrupted ((ZP_RECORDING_STATE)4)
#define ZpRecordingStateFailed ((ZP_RECORDING_STATE)5)

typedef struct _ZP_RECORDING_START
{
    ZP_RECORDING_SOURCE Source;
    ZP_RECORDING_CODEC Codec;
    BYTE FrameRate;
    ZP_RECORDING_AUDIO_SOURCE AudioSource;
    BYTE Flags;
    ULONG MaxDimension;
    ULONG VideoBitRate;
    ULONG AudioBitRate;
    ULONGLONG WindowHandle;
    PCWCH SourceId;
    ULONG SourceIdLength;
    PCWCH AudioDeviceId;
    ULONG AudioDeviceIdLength;
} ZP_RECORDING_START, *PZP_RECORDING_START;

typedef const ZP_RECORDING_START* PCZP_RECORDING_START;

typedef struct _ZP_RECORDING_START_VIEW
{
    ZP_RECORDING_SOURCE Source;
    ZP_RECORDING_CODEC Codec;
    BYTE FrameRate;
    ZP_RECORDING_AUDIO_SOURCE AudioSource;
    BYTE Flags;
    ULONG MaxDimension;
    ULONG VideoBitRate;
    ULONG AudioBitRate;
    ULONGLONG WindowHandle;
    ZP_STRING_VIEW SourceId;
    ZP_STRING_VIEW AudioDeviceId;
} ZP_RECORDING_START_VIEW, *PZP_RECORDING_START_VIEW;

typedef const ZP_RECORDING_START_VIEW* PCZP_RECORDING_START_VIEW;

typedef struct _ZP_RECORDING_RECORD
{
    ULONG RecordingId;
    ZP_RECORDING_SOURCE Source;
    ZP_RECORDING_CODEC Codec;
    ZP_RECORDING_STATE State;
    ZP_STATUS Status;
    ULONGLONG StartTime;
    ULONGLONG Duration;
    ULONGLONG FileSize;
    PCWCH Path;
    ULONG PathLength;
} ZP_RECORDING_RECORD, *PZP_RECORDING_RECORD;

typedef const ZP_RECORDING_RECORD* PCZP_RECORDING_RECORD;

typedef struct _ZP_RECORDING_RECORD_VIEW
{
    ULONG RecordingId;
    ZP_RECORDING_SOURCE Source;
    ZP_RECORDING_CODEC Codec;
    ZP_RECORDING_STATE State;
    ZP_STATUS Status;
    ULONGLONG StartTime;
    ULONGLONG Duration;
    ULONGLONG FileSize;
    ZP_STRING_VIEW Path;
} ZP_RECORDING_RECORD_VIEW, *PZP_RECORDING_RECORD_VIEW;

typedef struct _ZP_RECORDING_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_RECORDING_LIST_VIEW, *PZP_RECORDING_LIST_VIEW;

typedef const ZP_RECORDING_LIST_VIEW* PCZP_RECORDING_LIST_VIEW;

NTSTATUS
ZpRecording_EncodeCapabilities(
    _In_ ULONG Codecs,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRecording_DecodeCapabilities(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG Codecs);

NTSTATUS
ZpRecording_EncodeStart(
    _In_ PCZP_RECORDING_START Start,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRecording_DecodeStart(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_RECORDING_START_VIEW Start);

NTSTATUS
ZpRecording_EncodeRecords(
    _In_reads_opt_(Count) PCZP_RECORDING_RECORD Records,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRecording_DecodeRecords(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_RECORDING_LIST_VIEW View);

NTSTATUS
ZpRecording_GetNextRecord(
    _In_ PCZP_RECORDING_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_RECORDING_RECORD_VIEW Record);

NTSTATUS
ZpRecording_EncodeId(
    _In_ ULONG RecordingId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRecording_DecodeId(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG RecordingId);

EXTERN_C_END
