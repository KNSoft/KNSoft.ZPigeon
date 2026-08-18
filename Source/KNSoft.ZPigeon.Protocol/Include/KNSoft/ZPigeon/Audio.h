#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_AUDIO_MODULE_ID 14
#define ZP_AUDIO_MODULE_VERSION 1

#define ZP_AUDIO_OPERATION_ENUMERATE_DEVICES 1
#define ZP_AUDIO_OPERATION_ENUMERATE_SESSIONS 2
#define ZP_AUDIO_OPERATION_CONTROL_ENDPOINT 3
#define ZP_AUDIO_OPERATION_CONTROL_SESSION 4
#define ZP_AUDIO_OPERATION_OPEN_STREAM 5

typedef USHORT ZP_AUDIO_FLOW, *PZP_AUDIO_FLOW;

#define ZpAudioFlowRender ((ZP_AUDIO_FLOW)1)
#define ZpAudioFlowCapture ((ZP_AUDIO_FLOW)2)

typedef USHORT ZP_AUDIO_ENDPOINT_CONTROL, *PZP_AUDIO_ENDPOINT_CONTROL;

#define ZpAudioEndpointSetVolume ((ZP_AUDIO_ENDPOINT_CONTROL)1)
#define ZpAudioEndpointSetMute ((ZP_AUDIO_ENDPOINT_CONTROL)2)
#define ZpAudioEndpointSetDefault ((ZP_AUDIO_ENDPOINT_CONTROL)3)
#define ZpAudioEndpointSetEnabled ((ZP_AUDIO_ENDPOINT_CONTROL)4)

typedef USHORT ZP_AUDIO_SESSION_CONTROL, *PZP_AUDIO_SESSION_CONTROL;

#define ZpAudioSessionSetVolume ((ZP_AUDIO_SESSION_CONTROL)1)
#define ZpAudioSessionSetMute ((ZP_AUDIO_SESSION_CONTROL)2)

#define ZP_AUDIO_ENDPOINT_MUTED 0x00000001UL
#define ZP_AUDIO_ENDPOINT_VOLUME_AVAILABLE 0x00000002UL
#define ZP_AUDIO_ENDPOINT_DEFAULT_CONSOLE 0x00000004UL
#define ZP_AUDIO_ENDPOINT_DEFAULT_MULTIMEDIA 0x00000008UL
#define ZP_AUDIO_ENDPOINT_DEFAULT_COMMUNICATIONS 0x00000010UL

#define ZP_AUDIO_SESSION_MUTED 0x00000001UL
#define ZP_AUDIO_SESSION_SYSTEM_SOUNDS 0x00000002UL

#define ZP_AUDIO_VOLUME_MAX 10000
#define ZP_AUDIO_MAX_DEVICES 256
#define ZP_AUDIO_MAX_SESSIONS 2048
#define ZP_AUDIO_MAX_ID_LENGTH 1024
#define ZP_AUDIO_MAX_NAME_LENGTH 512
#define ZP_AUDIO_MAX_CHANNELS 8
#define ZP_AUDIO_MAX_SAMPLE_RATE 192000
#define ZP_AUDIO_MAX_PACKET_FRAMES 32768
#define ZP_AUDIO_MAX_PACKET_SIZE (ZP_AUDIO_MAX_PACKET_FRAMES * ZP_AUDIO_MAX_CHANNELS * sizeof(SHORT))

#define ZP_AUDIO_FORMAT_PCM16 1

typedef struct _ZP_AUDIO_DEVICE
{
    ZP_AUDIO_FLOW Flow;
    ULONG State;
    ULONG Flags;
    ULONG Volume;
    PCWCH Id;
    ULONG IdLength;
    PCWCH Name;
    ULONG NameLength;
} ZP_AUDIO_DEVICE, *PZP_AUDIO_DEVICE;

typedef const ZP_AUDIO_DEVICE* PCZP_AUDIO_DEVICE;

typedef struct _ZP_AUDIO_DEVICE_VIEW
{
    ZP_AUDIO_FLOW Flow;
    ULONG State;
    ULONG Flags;
    ULONG Volume;
    ZP_STRING_VIEW Id;
    ZP_STRING_VIEW Name;
} ZP_AUDIO_DEVICE_VIEW, *PZP_AUDIO_DEVICE_VIEW;

typedef struct _ZP_AUDIO_DEVICE_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_AUDIO_DEVICE_LIST_VIEW, *PZP_AUDIO_DEVICE_LIST_VIEW;

typedef const ZP_AUDIO_DEVICE_LIST_VIEW* PCZP_AUDIO_DEVICE_LIST_VIEW;

typedef struct _ZP_AUDIO_SESSION
{
    ULONG ProcessId;
    ULONG State;
    ULONG Flags;
    ULONG Volume;
    PCWCH DeviceId;
    ULONG DeviceIdLength;
    PCWCH Id;
    ULONG IdLength;
    PCWCH Name;
    ULONG NameLength;
} ZP_AUDIO_SESSION, *PZP_AUDIO_SESSION;

typedef const ZP_AUDIO_SESSION* PCZP_AUDIO_SESSION;

typedef struct _ZP_AUDIO_SESSION_VIEW
{
    ULONG ProcessId;
    ULONG State;
    ULONG Flags;
    ULONG Volume;
    ZP_STRING_VIEW DeviceId;
    ZP_STRING_VIEW Id;
    ZP_STRING_VIEW Name;
} ZP_AUDIO_SESSION_VIEW, *PZP_AUDIO_SESSION_VIEW;

typedef struct _ZP_AUDIO_SESSION_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_AUDIO_SESSION_LIST_VIEW, *PZP_AUDIO_SESSION_LIST_VIEW;

typedef const ZP_AUDIO_SESSION_LIST_VIEW* PCZP_AUDIO_SESSION_LIST_VIEW;

typedef struct _ZP_AUDIO_ENDPOINT_CONTROL_VIEW
{
    ZP_AUDIO_FLOW Flow;
    ZP_AUDIO_ENDPOINT_CONTROL Control;
    ULONG Value;
    ZP_STRING_VIEW DeviceId;
} ZP_AUDIO_ENDPOINT_CONTROL_VIEW, *PZP_AUDIO_ENDPOINT_CONTROL_VIEW;

typedef struct _ZP_AUDIO_SESSION_CONTROL_VIEW
{
    ZP_AUDIO_SESSION_CONTROL Control;
    ULONG Value;
    ZP_STRING_VIEW DeviceId;
    ZP_STRING_VIEW SessionId;
} ZP_AUDIO_SESSION_CONTROL_VIEW, *PZP_AUDIO_SESSION_CONTROL_VIEW;

typedef struct _ZP_AUDIO_STREAM_REQUEST_VIEW
{
    ZP_AUDIO_FLOW Flow;
    ZP_STRING_VIEW DeviceId;
} ZP_AUDIO_STREAM_REQUEST_VIEW, *PZP_AUDIO_STREAM_REQUEST_VIEW;

typedef struct _ZP_AUDIO_PACKET
{
    USHORT Format;
    USHORT Channels;
    ULONG SampleRate;
    ULONG FrameCount;
    ULONG DataLength;
} ZP_AUDIO_PACKET, *PZP_AUDIO_PACKET;

typedef const ZP_AUDIO_PACKET* PCZP_AUDIO_PACKET;

NTSTATUS
ZpAudio_EncodeDeviceList(
    _In_reads_opt_(Count) PCZP_AUDIO_DEVICE Devices,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpAudio_DecodeDeviceList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_AUDIO_DEVICE_LIST_VIEW List);

NTSTATUS
ZpAudio_GetNextDevice(
    _In_ PCZP_AUDIO_DEVICE_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_AUDIO_DEVICE_VIEW Device);

NTSTATUS
ZpAudio_EncodeSessionList(
    _In_reads_opt_(Count) PCZP_AUDIO_SESSION Sessions,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpAudio_DecodeSessionList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_AUDIO_SESSION_LIST_VIEW List);

NTSTATUS
ZpAudio_GetNextSession(
    _In_ PCZP_AUDIO_SESSION_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_AUDIO_SESSION_VIEW Session);

NTSTATUS
ZpAudio_EncodeEndpointControl(
    _In_ ZP_AUDIO_FLOW Flow,
    _In_ ZP_AUDIO_ENDPOINT_CONTROL Control,
    _In_ ULONG Value,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpAudio_DecodeEndpointControl(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_AUDIO_ENDPOINT_CONTROL_VIEW Control);

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
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpAudio_DecodeSessionControl(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_AUDIO_SESSION_CONTROL_VIEW Control);

NTSTATUS
ZpAudio_EncodeStreamRequest(
    _In_ ZP_AUDIO_FLOW Flow,
    _In_reads_opt_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpAudio_DecodeStreamRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_AUDIO_STREAM_REQUEST_VIEW Request);

NTSTATUS
ZpAudio_EncodeChannel(
    _In_ ULONGLONG ChannelId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpAudio_DecodeChannel(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG ChannelId);

NTSTATUS
ZpAudio_EncodePacket(
    _In_ PCZP_AUDIO_PACKET Packet,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

EXTERN_C_END
