#pragma once

#include <KNSoft/ZPigeon/Execution.h>

struct _ZP_CLIENT_OBJECT;

ZP_STATUS
ZpExecution_Execute(
    _Inout_ struct _ZP_CLIENT_OBJECT* Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength);

VOID
ZpExecution_Cleanup(
    _Inout_ struct _ZP_CLIENT_OBJECT* Client);
