#pragma once

#include "Capture.h"

typedef struct _ZP_VIDEO_SHARED_CAPTURE ZP_VIDEO_SHARED_CAPTURE, *PZP_VIDEO_SHARED_CAPTURE;

HRESULT
ZpVideoShared_Open(
    _In_ PZP_VIDEO_STREAM_REQUEST_VIEW Request,
    _Out_ PZP_VIDEO_SHARED_CAPTURE* Capture);

VOID
ZpVideoShared_GetFormat(
    _In_ PZP_VIDEO_SHARED_CAPTURE Capture,
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PUSHORT FrameRate);

HRESULT
ZpVideoShared_NextSample(
    _Inout_ PZP_VIDEO_SHARED_CAPTURE Capture,
    _In_ HANDLE StopEvent,
    _Outptr_ IMFSample** Sample,
    _Out_ PLONGLONG Timestamp);

HRESULT
ZpVideoShared_Encode(
    _In_ PZP_VIDEO_SHARED_CAPTURE Capture,
    _In_ IMFSample* Sample,
    _In_ ULONG MaxDimension,
    _In_ USHORT Quality,
    _Out_ PZP_VIDEO_IMAGE Image);

VOID
ZpVideoShared_Close(
    _In_opt_ PZP_VIDEO_SHARED_CAPTURE Capture);
