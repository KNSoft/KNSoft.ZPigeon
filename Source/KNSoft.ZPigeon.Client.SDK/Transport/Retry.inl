#pragma once

#include <KNSoft/ZPigeon/Client.h>

#define ZP_CLIENT_RETRY_INITIAL_MILLISECONDS 1000

static
ULONG
ZpClientRetry_GetBaseDelay(
    _In_ ULONG FailureRound)
{
    ULONG Delay;

    if (FailureRound >= 6)
    {
        return ZP_CLIENT_DEFAULT_RETRY_MAX_MILLISECONDS;
    }
    Delay = ZP_CLIENT_RETRY_INITIAL_MILLISECONDS << FailureRound;
    return min(Delay, ZP_CLIENT_DEFAULT_RETRY_MAX_MILLISECONDS);
}

static
ULONG
ZpClientRetry_GetDelay(
    _In_ ULONG FailureRound,
    _In_ ULONG RandomValue)
{
    ULONG BaseDelay = ZpClientRetry_GetBaseDelay(FailureRound);
    ULONG Jitter = BaseDelay * ZP_CLIENT_DEFAULT_RETRY_JITTER_PERCENT / 100;

    return BaseDelay - Jitter + RandomValue % (Jitter * 2 + 1);
}
