#pragma once

#include <KNSoft/ZPigeon/SDK.h>
#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef
NTSTATUS
(NTAPI *ZP_CHANNEL_CANCEL_ROUTINE)(
    _In_ ZP_CHANNEL_HANDLE Channel);

typedef
NTSTATUS
(NTAPI *ZP_CHANNEL_SEND_ROUTINE)(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength);

typedef
VOID
(NTAPI *ZP_CHANNEL_CLOSE_ROUTINE)(
    _In_ ZP_CHANNEL_HANDLE Channel);

typedef struct _ZP_CHANNEL_HEADER
{
    ZP_CHANNEL_CANCEL_ROUTINE Cancel;
    ZP_CHANNEL_SEND_ROUTINE Send;
    ZP_CHANNEL_CLOSE_ROUTINE Close;
    volatile LONG ReferenceCount;
} ZP_CHANNEL_HEADER, *PZP_CHANNEL_HEADER;

FORCEINLINE
VOID
ZpChannel_Release(
    _Inout_ PZP_CHANNEL_HEADER Channel)
{
    if (InterlockedDecrement(&Channel->ReferenceCount) == 0)
    {
        Mem_Free(Channel);
    }
}
