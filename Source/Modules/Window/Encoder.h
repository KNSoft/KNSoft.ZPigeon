#pragma once

#include <KNSoft/ZPigeon/Window.h>

#include <mfidl.h>

typedef struct _ZP_WINDOW_VIDEO_ENCODER ZP_WINDOW_VIDEO_ENCODER, *PZP_WINDOW_VIDEO_ENCODER;

typedef struct _ZP_WINDOW_VIDEO_FRAME
{
    PBYTE Data;
    ULONG Length;
    LOGICAL KeyFrame;
} ZP_WINDOW_VIDEO_FRAME, *PZP_WINDOW_VIDEO_FRAME;

HRESULT
ZpWindowVideoEncoder_Create(
    _In_ ZP_WINDOW_VIDEO_CODEC Codec,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ BYTE FrameRate,
    _In_ ULONG BitRate,
    _In_ IMFDXGIDeviceManager* DeviceManager,
    _Out_ PZP_WINDOW_VIDEO_ENCODER* Encoder);

HRESULT
ZpWindowVideoEncoder_Encode(
    _Inout_ PZP_WINDOW_VIDEO_ENCODER Encoder,
    _In_ IMFSample* Sample,
    _In_ ULONGLONG Timestamp,
    _In_ LOGICAL ForceKeyFrame,
    _Out_ PZP_WINDOW_VIDEO_FRAME Frame);

VOID
ZpWindowVideoEncoder_FreeFrame(
    _Inout_ PZP_WINDOW_VIDEO_FRAME Frame);

VOID
ZpWindowVideoEncoder_Close(
    _In_opt_ PZP_WINDOW_VIDEO_ENCODER Encoder);
