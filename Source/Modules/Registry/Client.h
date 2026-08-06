#pragma once

#include <KNSoft/ZPigeon/Registry.h>

NTSTATUS
ZpRegistry_Execute(
    _In_ BYTE OperationId,
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength);
