#pragma once

#include "Capture.h"

typedef struct _ZP_AUDIO_SHARED_CAPTURE ZP_AUDIO_SHARED_CAPTURE, *PZP_AUDIO_SHARED_CAPTURE;

typedef struct _ZP_AUDIO_SHARED_FRAME
{
    LONG ReferenceCount;
    USHORT Channels;
    ULONG SampleRate;
    ULONG FrameCount;
    ULONGLONG Timestamp;
    SHORT Samples[ANYSIZE_ARRAY];
} ZP_AUDIO_SHARED_FRAME, *PZP_AUDIO_SHARED_FRAME;

HRESULT
ZpAudioShared_Open(
    _In_ ZP_AUDIO_FLOW Flow,
    _In_reads_opt_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _Out_ PZP_AUDIO_SHARED_CAPTURE* Capture);

VOID
ZpAudioShared_GetFormat(
    _In_ PZP_AUDIO_SHARED_CAPTURE Capture,
    _Out_ PUSHORT Channels,
    _Out_ PULONG SampleRate);

HRESULT
ZpAudioShared_Next(
    _Inout_ PZP_AUDIO_SHARED_CAPTURE Capture,
    _In_ HANDLE StopEvent,
    _Out_ PZP_AUDIO_SHARED_FRAME* Frame);

VOID
ZpAudioShared_ReleaseFrame(
    _In_ PZP_AUDIO_SHARED_FRAME Frame);

VOID
ZpAudioShared_Close(
    _In_opt_ PZP_AUDIO_SHARED_CAPTURE Capture);
