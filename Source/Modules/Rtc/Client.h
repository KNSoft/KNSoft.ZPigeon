#pragma once

#include <KNSoft/ZPigeon/Rtc.h>

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"

EXTERN_C_START

ZP_STATUS
ZpRtc_Execute(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength);

NTSTATUS
ZpRtc_Send(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ ULONG StreamId,
    _In_reads_bytes_(Length) const VOID* Data,
    _In_ ULONG Length);

VOID
ZpRtc_Close(
    _Inout_ PZP_CLIENT_OBJECT Client);

EXTERN_C_END
