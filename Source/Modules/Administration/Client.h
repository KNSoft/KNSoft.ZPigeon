#pragma once

#include <KNSoft/ZPigeon/Administration.h>

ZP_STATUS
ZpAdministration_Execute(
    _In_ BYTE OperationId,
    _In_reads_bytes_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength);
