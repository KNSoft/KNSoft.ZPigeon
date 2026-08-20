#pragma once

#include <KNSoft/ZPigeon/File.h>

typedef struct _ZP_CLIENT_FILE_CHANNEL ZP_CLIENT_FILE_CHANNEL,
  *PZP_CLIENT_FILE_CHANNEL;

VOID
ZpFile_ResetEnumeration(
    _Inout_ struct _ZP_CLIENT_OBJECT* Client);

NTSTATUS
ZpFile_Execute(
    _Inout_opt_ struct _ZP_CLIENT_OBJECT* Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _In_ volatile LONG* Pending,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_FILE_CHANNEL* Channel);

VOID
ZpFile_CommitChannel(
    _Inout_ PZP_CLIENT_FILE_CHANNEL Channel,
    _In_ LOGICAL ResponseSent);
