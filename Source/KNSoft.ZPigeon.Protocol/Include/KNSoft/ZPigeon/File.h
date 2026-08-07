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

typedef struct _ZP_FILE_RECORD
{
    ZP_FILE_INFO Info;
    PCWCH Name;
    ULONG NameLength;
} ZP_FILE_RECORD, *PZP_FILE_RECORD;

typedef const ZP_FILE_RECORD* PCZP_FILE_RECORD;

typedef struct _ZP_FILE_RECORD_VIEW
{
    ZP_FILE_INFO Info;
    ZP_STRING_VIEW Name;
} ZP_FILE_RECORD_VIEW, *PZP_FILE_RECORD_VIEW;

typedef struct _ZP_FILE_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_FILE_LIST_VIEW, *PZP_FILE_LIST_VIEW;

typedef const ZP_FILE_LIST_VIEW* PCZP_FILE_LIST_VIEW;

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

NTSTATUS
ZpFile_EncodeList(
    _In_reads_opt_(FileCount) PCZP_FILE_RECORD Files,
    _In_ ULONG FileCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_LIST_VIEW View);

NTSTATUS
ZpFile_GetRecord(
    _In_ PCZP_FILE_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_FILE_RECORD_VIEW Record);

EXTERN_C_END
