#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_PROCESS_MODULE_ID 2
#define ZP_PROCESS_MODULE_VERSION 1
#define ZP_PROCESS_OPERATION_ENUMERATE 1

typedef struct _ZP_PROCESS_RECORD
{
    ULONG ProcessId;
    ULONG SessionId;
    PCWCH ImageName;
    ULONG ImageNameLength;
} ZP_PROCESS_RECORD, *PZP_PROCESS_RECORD;

typedef const ZP_PROCESS_RECORD* PCZP_PROCESS_RECORD;

typedef struct _ZP_PROCESS_RECORD_VIEW
{
    ULONG ProcessId;
    ULONG SessionId;
    ZP_STRING_VIEW ImageName;
} ZP_PROCESS_RECORD_VIEW, *PZP_PROCESS_RECORD_VIEW;

typedef struct _ZP_PROCESS_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_PROCESS_LIST_VIEW, *PZP_PROCESS_LIST_VIEW;

typedef const ZP_PROCESS_LIST_VIEW* PCZP_PROCESS_LIST_VIEW;

NTSTATUS
ZpProcess_EncodeList(
    _In_reads_opt_(ProcessCount) PCZP_PROCESS_RECORD Processes,
    _In_ ULONG ProcessCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpProcess_DecodeList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PROCESS_LIST_VIEW View);

NTSTATUS
ZpProcess_GetRecord(
    _In_ PCZP_PROCESS_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_PROCESS_RECORD_VIEW Record);

EXTERN_C_END
