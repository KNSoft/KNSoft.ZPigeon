#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_FILE_MODULE_ID 4
#define ZP_FILE_MODULE_VERSION 1
#define ZP_FILE_OPERATION_ENUMERATE 1
#define ZP_FILE_OPERATION_QUERY 2

typedef struct _ZP_FILE_INFO
{
    ULONG Attributes;
    ULONGLONG Size;
    ULONGLONG CreationTime;
    ULONGLONG LastAccessTime;
    ULONGLONG LastWriteTime;
} ZP_FILE_INFO, *PZP_FILE_INFO;

typedef const ZP_FILE_INFO* PCZP_FILE_INFO;

NTSTATUS
ZpFile_EncodePath(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodePath(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path);

NTSTATUS
ZpFile_EncodeInfo(
    _In_ PCZP_FILE_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_INFO Info);

EXTERN_C_END
