#pragma once

#include "Capture.h"

typedef struct _ZP_WINDOW_SHARED_CAPTURE ZP_WINDOW_SHARED_CAPTURE, *PZP_WINDOW_SHARED_CAPTURE;
typedef struct _ZP_WINDOW_SHARED_IMAGE ZP_WINDOW_SHARED_IMAGE, *PZP_WINDOW_SHARED_IMAGE;

struct _ZP_WINDOW_SHARED_IMAGE
{
    volatile LONG ReferenceCount;
    ZP_WINDOW_CAPTURE_IMAGE Value;
};

HRESULT
ZpWindowShared_Open(
    _In_ HWND Window,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _Out_ PZP_WINDOW_SHARED_CAPTURE* Capture);

_Success_(return == S_OK)
HRESULT
ZpWindowShared_Next(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_SHARED_IMAGE* Image);

HRESULT
ZpWindowShared_Update(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _In_ ULONG MaxDimension,
    _In_ BYTE FrameRate,
    _In_ BYTE Quality);

VOID
ZpWindowShared_RequestKeyFrame(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture);

VOID
ZpWindowShared_ReleaseImage(
    _In_ PZP_WINDOW_SHARED_IMAGE Image);

VOID
ZpWindowShared_GetFormat(
    _In_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _Out_ PULONG Width,
    _Out_ PULONG Height);

IMFDXGIDeviceManager*
ZpWindowShared_GetDeviceManager(
    _In_ PZP_WINDOW_SHARED_CAPTURE Capture);

_Success_(return == S_OK)
HRESULT
ZpWindowShared_NextSample(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Outptr_ IMFSample** Sample,
    _Out_ PULONGLONG Timestamp,
    _Out_opt_ PBYTE ChangeRate);

VOID
ZpWindowShared_Close(
    _In_opt_ PZP_WINDOW_SHARED_CAPTURE Capture);
