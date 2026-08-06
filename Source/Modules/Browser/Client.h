#pragma once

#include <KNSoft/ZPigeon/Browser.h>

struct _ZP_CLIENT_OBJECT;

ZP_STATUS
ZpBrowser_Execute(
    _Inout_ struct _ZP_CLIENT_OBJECT* Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength);
