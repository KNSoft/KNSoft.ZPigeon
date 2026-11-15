#pragma once

#include <KNSoft/ZPigeon/Window.h>

static inline
ULONG
ZpWindowCapture_GetVideoBitRate(
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _In_ ULONG BitRateLimit,
    _In_ BYTE RatePercent,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ZP_WINDOW_VIDEO_CODEC Codec)
{
    ULONGLONG BitRate = (ULONGLONG)Width * Height * Options->FrameRate *
                        (Options->Quality + 40) / 1000;

    if (Codec == ZpWindowVideoCodecH265) BitRate = BitRate * 7 / 10;
    BitRate = max(256000, min(BitRate, BitRateLimit));
    return (ULONG)max(256000, BitRate * RatePercent / 100);
}
