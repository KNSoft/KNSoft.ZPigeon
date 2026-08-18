#pragma once

#include <KNSoft/ZPigeon/Window.h>

typedef struct _ZP_WINDOW_CAPTURE ZP_WINDOW_CAPTURE, *PZP_WINDOW_CAPTURE;

typedef struct _ZP_WINDOW_CAPTURE_IMAGE
{
    ZP_WINDOW_CAPTURE_RECORD Record;
    PBYTE Data;
} ZP_WINDOW_CAPTURE_IMAGE, *PZP_WINDOW_CAPTURE_IMAGE;

HRESULT
ZpWindowCapture_CheckSupport(VOID);

HRESULT
ZpWindowCapture_Create(
    _In_ HWND Window,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _Out_ PZP_WINDOW_CAPTURE* Capture);

HRESULT
ZpWindowCapture_Next(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_CAPTURE_IMAGE Image);

VOID
ZpWindowCapture_FreeImage(
    _Inout_ PZP_WINDOW_CAPTURE_IMAGE Image);

VOID
ZpWindowCapture_Close(
    _In_opt_ PZP_WINDOW_CAPTURE Capture);
