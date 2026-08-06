#pragma once

#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Recording.h"

#include <mfidl.h>

typedef struct _ZP_MEDIA_WRITER ZP_MEDIA_WRITER, *PZP_MEDIA_WRITER;

HRESULT
ZpMediaWriter_QueryCodecs(
    _Out_ PULONG Codecs);

HRESULT
ZpMediaWriter_CreateAudio(
    _In_ PCWSTR Path,
    _In_ ZP_RECORDING_CODEC Codec,
    _In_ USHORT Channels,
    _In_ ULONG SampleRate,
    _In_ ULONG BitRate,
    _Out_ PZP_MEDIA_WRITER* Writer);

HRESULT
ZpMediaWriter_CreateVideo(
    _In_ PCWSTR Path,
    _In_ ZP_RECORDING_CODEC Codec,
    _In_ REFGUID InputSubtype,
    _In_ ULONG InputWidth,
    _In_ ULONG InputHeight,
    _In_ ULONG OutputWidth,
    _In_ ULONG OutputHeight,
    _In_ USHORT FrameRate,
    _In_ ULONG VideoBitRate,
    _In_ USHORT AudioChannels,
    _In_ ULONG AudioSampleRate,
    _In_ ULONG AudioBitRate,
    _In_opt_ IUnknown* DeviceManager,
    _Out_ PZP_MEDIA_WRITER* Writer);

HRESULT
ZpMediaWriter_WriteAudio(
    _Inout_ PZP_MEDIA_WRITER Writer,
    _In_reads_(FrameCount * Channels) const SHORT* Samples,
    _In_ USHORT Channels,
    _In_ ULONG SampleRate,
    _In_ ULONG FrameCount,
    _In_ ULONGLONG Timestamp);

HRESULT
ZpMediaWriter_WriteVideo(
    _Inout_ PZP_MEDIA_WRITER Writer,
    _In_ IMFSample* Sample,
    _In_ ULONGLONG Timestamp,
    _In_ ULONGLONG Duration);

HRESULT
ZpMediaWriter_WriteVideoBytes(
    _Inout_ PZP_MEDIA_WRITER Writer,
    _In_reads_bytes_(Length) const VOID* Data,
    _In_ ULONG Length,
    _In_ ULONGLONG Timestamp,
    _In_ ULONGLONG Duration);

HRESULT
ZpMediaWriter_Finalize(
    _Inout_ PZP_MEDIA_WRITER Writer);

HRESULT
ZpMediaWriter_FillAudioSilence(
    _Inout_ PZP_MEDIA_WRITER Writer,
    _In_ ULONGLONG Duration);

VOID
ZpMediaWriter_Close(
    _In_opt_ PZP_MEDIA_WRITER Writer);
