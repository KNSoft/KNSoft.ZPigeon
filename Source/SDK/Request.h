#pragma once

#include <KNSoft/ZPigeon/SDK.h>
#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef
NTSTATUS
(NTAPI *ZP_REQUEST_CANCEL_ROUTINE)(
    _In_ ZP_REQUEST_HANDLE Request);

typedef struct _ZP_REQUEST_HEADER
{
    ZP_REQUEST_CANCEL_ROUTINE Cancel;
    volatile LONG ReferenceCount;
} ZP_REQUEST_HEADER, *PZP_REQUEST_HEADER;

FORCEINLINE
VOID
ZpRequest_Release(
    _Inout_ PZP_REQUEST_HEADER Request)
{
    if (InterlockedDecrement(&Request->ReferenceCount) == 0)
    {
        Mem_Free(Request);
    }
}
