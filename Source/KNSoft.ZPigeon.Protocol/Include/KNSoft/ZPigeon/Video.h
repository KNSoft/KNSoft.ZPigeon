#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_VIDEO_MODULE_ID 15
#define ZP_VIDEO_MODULE_VERSION 1

#define ZP_VIDEO_OPERATION_ENUMERATE_DEVICES 1
#define ZP_VIDEO_OPERATION_OPEN_STREAM 2
#define ZP_VIDEO_OPERATION_UPDATE_STREAM 3

#define ZP_VIDEO_MAX_DEVICES 64
#define ZP_VIDEO_MAX_ID_LENGTH 1024
#define ZP_VIDEO_MAX_NAME_LENGTH 512
#define ZP_VIDEO_MAX_FORMATS 256
#define ZP_VIDEO_MAX_FRAME_SIZE 0x01000000UL
#define ZP_VIDEO_MAX_DIMENSION 3840
#define ZP_VIDEO_MAX_FRAME_RATE 120

typedef struct _ZP_VIDEO_FORMAT
{
    ULONG Width;
    ULONG Height;
    ULONG FrameRateNumerator;
    ULONG FrameRateDenominator;
} ZP_VIDEO_FORMAT, *PZP_VIDEO_FORMAT;

typedef const ZP_VIDEO_FORMAT* PCZP_VIDEO_FORMAT;

typedef struct _ZP_VIDEO_DEVICE
{
    PCWCH Id;
    ULONG IdLength;
    PCWCH Name;
    ULONG NameLength;
    PCZP_VIDEO_FORMAT Formats;
    ULONG FormatCount;
} ZP_VIDEO_DEVICE, *PZP_VIDEO_DEVICE;

typedef const ZP_VIDEO_DEVICE* PCZP_VIDEO_DEVICE;

typedef struct _ZP_VIDEO_DEVICE_VIEW
{
    ZP_STRING_VIEW Id;
    ZP_STRING_VIEW Name;
    const BYTE* Formats;
    ULONG FormatCount;
} ZP_VIDEO_DEVICE_VIEW, *PZP_VIDEO_DEVICE_VIEW;

typedef const ZP_VIDEO_DEVICE_VIEW* PCZP_VIDEO_DEVICE_VIEW;

typedef struct _ZP_VIDEO_DEVICE_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_VIDEO_DEVICE_LIST_VIEW, *PZP_VIDEO_DEVICE_LIST_VIEW;

typedef const ZP_VIDEO_DEVICE_LIST_VIEW* PCZP_VIDEO_DEVICE_LIST_VIEW;

typedef struct _ZP_VIDEO_STREAM_REQUEST_VIEW
{
    ULONG Width;
    ULONG Height;
    ULONG FrameRateNumerator;
    ULONG FrameRateDenominator;
    ULONG DirectStreamId;
    USHORT Quality;
    ZP_STRING_VIEW DeviceId;
} ZP_VIDEO_STREAM_REQUEST_VIEW, *PZP_VIDEO_STREAM_REQUEST_VIEW;

typedef struct _ZP_VIDEO_STREAM_UPDATE
{
    ULONG ChannelId;
    ZP_VIDEO_FORMAT Format;
    USHORT Quality;
} ZP_VIDEO_STREAM_UPDATE, *PZP_VIDEO_STREAM_UPDATE;

typedef const ZP_VIDEO_STREAM_UPDATE* PCZP_VIDEO_STREAM_UPDATE;

typedef struct _ZP_VIDEO_FRAME
{
    ULONG Width;
    ULONG Height;
    ULONG DataLength;
} ZP_VIDEO_FRAME, *PZP_VIDEO_FRAME;

typedef const ZP_VIDEO_FRAME* PCZP_VIDEO_FRAME;

NTSTATUS
ZpVideo_EncodeDeviceList(
    _In_reads_opt_(Count) PCZP_VIDEO_DEVICE Devices,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpVideo_DecodeDeviceList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_VIDEO_DEVICE_LIST_VIEW List);

NTSTATUS
ZpVideo_GetNextDevice(
    _In_ PCZP_VIDEO_DEVICE_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_VIDEO_DEVICE_VIEW Device);

NTSTATUS
ZpVideo_GetNextFormat(
    _In_ PCZP_VIDEO_DEVICE_VIEW Device,
    _Inout_ PULONG Offset,
    _Out_ PZP_VIDEO_FORMAT Format);

NTSTATUS
ZpVideo_EncodeStreamRequest(
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ PCZP_VIDEO_FORMAT Format,
    _In_ USHORT Quality,
    _In_ ULONG DirectStreamId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpVideo_DecodeStreamRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_VIDEO_STREAM_REQUEST_VIEW Request);

NTSTATUS
ZpVideo_EncodeStreamUpdate(
    _In_ PCZP_VIDEO_STREAM_UPDATE Update,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpVideo_DecodeStreamUpdate(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_VIDEO_STREAM_UPDATE Update);

NTSTATUS
ZpVideo_EncodeChannel(
    _In_ ULONG ChannelId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpVideo_DecodeChannel(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ChannelId);

NTSTATUS
ZpVideo_EncodeFrame(
    _In_ PCZP_VIDEO_FRAME Frame,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

EXTERN_C_END
