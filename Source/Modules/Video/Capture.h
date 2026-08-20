#pragma once

#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Video.h"

#include <mfidl.h>

typedef struct _ZP_VIDEO_CAPTURE ZP_VIDEO_CAPTURE, *PZP_VIDEO_CAPTURE;

typedef struct _ZP_VIDEO_IMAGE
{
    ZP_VIDEO_FRAME Frame;
    PBYTE Data;
} ZP_VIDEO_IMAGE, *PZP_VIDEO_IMAGE;

HRESULT
ZpVideoCapture_Enumerate(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength);

HRESULT
ZpVideoCapture_SelectFormat(
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ULONG MaximumDimension,
    _In_ USHORT FrameRate,
    _Out_ PZP_VIDEO_FORMAT Format);

HRESULT
ZpVideoCapture_Create(
    _In_ PZP_VIDEO_STREAM_REQUEST_VIEW Request,
    _Out_ PZP_VIDEO_CAPTURE* Capture);

HRESULT
ZpVideoCapture_NextSample(
    _Inout_ PZP_VIDEO_CAPTURE Capture,
    _Outptr_ IMFSample** Sample,
    _Out_ PLONGLONG Timestamp);

HRESULT
ZpVideoCapture_EncodeSample(
    _In_ PZP_VIDEO_CAPTURE Capture,
    _In_ IMFSample* Sample,
    _In_ USHORT Quality,
    _Out_ PZP_VIDEO_IMAGE Image);

VOID
ZpVideoCapture_FreeImage(
    _Inout_ PZP_VIDEO_IMAGE Image);

VOID
ZpVideoCapture_Close(
    _In_opt_ PZP_VIDEO_CAPTURE Capture);
