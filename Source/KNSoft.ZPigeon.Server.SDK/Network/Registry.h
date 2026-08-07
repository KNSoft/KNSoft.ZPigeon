#pragma once

#include <KNSoft/ZPigeon/Registry.h>

NTSTATUS
ZpServerRegistry_ProcessRequest(
    _In_ USHORT OperationId,
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength);
