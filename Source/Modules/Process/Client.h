#pragma once

#include <KNSoft/ZPigeon/Process.h>

ZP_STATUS
ZpProcess_Execute(
    _In_ USHORT OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength);
