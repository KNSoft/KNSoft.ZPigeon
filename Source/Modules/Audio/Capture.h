#pragma once

#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Audio.h"

typedef
NTSTATUS
(NTAPI *ZP_AUDIO_CAPTURE_CALLBACK)(
    _In_ USHORT Channels,
    _In_ ULONG SampleRate,
    _In_ ULONG FrameCount,
    _In_reads_(FrameCount * Channels) const SHORT* Samples,
    _In_ ULONGLONG Timestamp,
    _In_opt_ PVOID Context);

HRESULT
ZpAudioCapture_QueryFormat(
    _In_ ZP_AUDIO_FLOW Flow,
    _In_reads_opt_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _Out_ PUSHORT Channels,
    _Out_ PULONG SampleRate);

HRESULT
ZpAudioCapture_Run(
    _In_ ZP_AUDIO_FLOW Flow,
    _In_reads_opt_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ HANDLE StopEvent,
    _In_ ZP_AUDIO_CAPTURE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ PNTSTATUS CaptureStatus);
