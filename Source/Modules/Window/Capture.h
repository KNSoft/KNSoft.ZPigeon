#pragma once

#include <KNSoft/ZPigeon/Window.h>

#include <mfidl.h>

typedef struct _ZP_WINDOW_CAPTURE ZP_WINDOW_CAPTURE, *PZP_WINDOW_CAPTURE;
typedef struct _ZP_WINDOW_CAPTURE_FRAME ZP_WINDOW_CAPTURE_FRAME, *PZP_WINDOW_CAPTURE_FRAME;

typedef struct _ZP_WINDOW_CAPTURE_IMAGE
{
    ZP_WINDOW_CAPTURE_RECORD Record;
    PBYTE Data;
} ZP_WINDOW_CAPTURE_IMAGE, *PZP_WINDOW_CAPTURE_IMAGE;

HRESULT
ZpWindowCapture_CheckSupport(VOID);

HRESULT
ZpWindowCapture_ResolveMonitor(
    _In_ ULONG MonitorIndex,
    _Out_ HMONITOR* Monitor,
    _Out_opt_ PRECT MonitorRect);

HRESULT
ZpWindowCapture_Create(
    _In_ HWND Window,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _Out_ PZP_WINDOW_CAPTURE* Capture);

HRESULT
ZpWindowCapture_UpdateOptions(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options);

HRESULT
ZpWindowCapture_Next(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_CAPTURE_IMAGE Image);

HRESULT
ZpWindowCapture_NextFrame(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_CAPTURE_FRAME* Frame);

VOID
ZpWindowCapture_AddRefFrame(
    _In_ PZP_WINDOW_CAPTURE_FRAME Frame);

HRESULT
ZpWindowCapture_EncodeFrame(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ PZP_WINDOW_CAPTURE_FRAME Frame,
    _Out_ PZP_WINDOW_CAPTURE_IMAGE Image);

HRESULT
ZpWindowCapture_CreateSample(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ PZP_WINDOW_CAPTURE_FRAME Frame,
    _Outptr_ IMFSample** Sample,
    _Out_ PULONGLONG Timestamp);

VOID
ZpWindowCapture_ReleaseFrame(
    _In_ PZP_WINDOW_CAPTURE_FRAME Frame);

VOID
ZpWindowCapture_GetFormat(
    _In_ PZP_WINDOW_CAPTURE Capture,
    _Out_ PULONG Width,
    _Out_ PULONG Height);

IMFDXGIDeviceManager*
ZpWindowCapture_GetDeviceManager(
    _In_ PZP_WINDOW_CAPTURE Capture);

HRESULT
ZpWindowCapture_NextSample(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Outptr_ IMFSample** Sample,
    _Out_ PULONGLONG Timestamp);

VOID
ZpWindowCapture_FreeImage(
    _Inout_ PZP_WINDOW_CAPTURE_IMAGE Image);

VOID
ZpWindowCapture_Close(
    _In_opt_ PZP_WINDOW_CAPTURE Capture);
