#pragma once

#include <KNSoft/ZPigeon/EventLog.h>

ZP_STATUS
ZpEventLog_Execute(
    _In_ USHORT OperationId,
    _In_reads_bytes_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _In_ volatile LONG* Pending,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength);
