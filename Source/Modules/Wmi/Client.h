#pragma once

#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Wmi.h"

ZP_STATUS
ZpWmi_Execute(
    _In_ USHORT OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength);
