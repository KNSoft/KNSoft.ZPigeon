#pragma once

#include "Capture.h"

typedef struct _ZP_WINDOW_SHARED_CAPTURE ZP_WINDOW_SHARED_CAPTURE, *PZP_WINDOW_SHARED_CAPTURE;

HRESULT
ZpWindowShared_Open(
    _In_ HWND Window,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _Out_ PZP_WINDOW_SHARED_CAPTURE* Capture);

HRESULT
ZpWindowShared_Next(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_CAPTURE_IMAGE Image);

VOID
ZpWindowShared_GetFormat(
    _In_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _Out_ PULONG Width,
    _Out_ PULONG Height);

IMFDXGIDeviceManager*
ZpWindowShared_GetDeviceManager(
    _In_ PZP_WINDOW_SHARED_CAPTURE Capture);

HRESULT
ZpWindowShared_NextSample(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Outptr_ IMFSample** Sample,
    _Out_ PULONGLONG Timestamp);

VOID
ZpWindowShared_Close(
    _In_opt_ PZP_WINDOW_SHARED_CAPTURE Capture);
