#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_SERVICE_MODULE_ID 3
#define ZP_SERVICE_MODULE_VERSION 1
#define ZP_SERVICE_OPERATION_ENUMERATE 1
#define ZP_SERVICE_OPERATION_QUERY 2

typedef struct _ZP_SERVICE_RECORD
{
    ULONG ServiceType;
    ULONG CurrentState;
    ULONG ProcessId;
    PCWCH ServiceName;
    ULONG ServiceNameLength;
    PCWCH DisplayName;
    ULONG DisplayNameLength;
} ZP_SERVICE_RECORD, *PZP_SERVICE_RECORD;

typedef const ZP_SERVICE_RECORD* PCZP_SERVICE_RECORD;

typedef struct _ZP_SERVICE_RECORD_VIEW
{
    ULONG ServiceType;
    ULONG CurrentState;
    ULONG ProcessId;
    ZP_STRING_VIEW ServiceName;
    ZP_STRING_VIEW DisplayName;
} ZP_SERVICE_RECORD_VIEW, *PZP_SERVICE_RECORD_VIEW;

typedef struct _ZP_SERVICE_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_SERVICE_LIST_VIEW, *PZP_SERVICE_LIST_VIEW;

typedef const ZP_SERVICE_LIST_VIEW* PCZP_SERVICE_LIST_VIEW;

typedef struct _ZP_SERVICE_INFO
{
    ULONG ServiceType;
    ULONG CurrentState;
    ULONG ProcessId;
    ULONG StartType;
    ULONG ErrorControl;
    PCWCH ServiceName;
    ULONG ServiceNameLength;
    PCWCH DisplayName;
    ULONG DisplayNameLength;
    PCWCH BinaryPathName;
    ULONG BinaryPathNameLength;
    PCWCH StartName;
    ULONG StartNameLength;
} ZP_SERVICE_INFO, *PZP_SERVICE_INFO;

typedef const ZP_SERVICE_INFO* PCZP_SERVICE_INFO;

typedef struct _ZP_SERVICE_INFO_VIEW
{
    ULONG ServiceType;
    ULONG CurrentState;
    ULONG ProcessId;
    ULONG StartType;
    ULONG ErrorControl;
    ZP_STRING_VIEW ServiceName;
    ZP_STRING_VIEW DisplayName;
    ZP_STRING_VIEW BinaryPathName;
    ZP_STRING_VIEW StartName;
} ZP_SERVICE_INFO_VIEW, *PZP_SERVICE_INFO_VIEW;

NTSTATUS
ZpService_EncodeList(
    _In_reads_opt_(ServiceCount) PCZP_SERVICE_RECORD Services,
    _In_ ULONG ServiceCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpService_DecodeList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERVICE_LIST_VIEW View);

NTSTATUS
ZpService_GetRecord(
    _In_ PCZP_SERVICE_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_SERVICE_RECORD_VIEW Record);

NTSTATUS
ZpService_EncodeQuery(
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpService_DecodeQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW ServiceName);

NTSTATUS
ZpService_EncodeInfo(
    _In_ PCZP_SERVICE_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpService_DecodeInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERVICE_INFO_VIEW View);

EXTERN_C_END
