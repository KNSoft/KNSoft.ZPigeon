#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_FILE_MODULE_ID 4
#define ZP_FILE_MODULE_VERSION 1
#define ZP_FILE_OPERATION_ENUMERATE 1
#define ZP_FILE_OPERATION_QUERY 2
#define ZP_FILE_OPERATION_OPEN_READ 3
#define ZP_FILE_OPERATION_HASH 4
#define ZP_FILE_OPERATION_OPEN_WRITE 5
#define ZP_FILE_OPERATION_ENUMERATE_PAGE 6
#define ZP_FILE_SHA256_SIZE 32
#define ZP_FILE_PAGE_MAX_COUNT 4096

typedef enum _ZP_FILE_CREATE_DISPOSITION
{
    ZpFileCreateNew = 1,
    ZpFileCreateAlways = 2
} ZP_FILE_CREATE_DISPOSITION, *PZP_FILE_CREATE_DISPOSITION;

typedef enum _ZP_FILE_HASH_ALGORITHM
{
    ZpFileHashSha256 = 1
} ZP_FILE_HASH_ALGORITHM, *PZP_FILE_HASH_ALGORITHM;

typedef struct _ZP_FILE_HASH_VIEW
{
    ZP_FILE_HASH_ALGORITHM Algorithm;
    ULONGLONG FileSize;
    ZP_BUFFER_VIEW Digest;
} ZP_FILE_HASH_VIEW, *PZP_FILE_HASH_VIEW;

typedef const ZP_FILE_HASH_VIEW* PCZP_FILE_HASH_VIEW;

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

typedef struct _ZP_FILE_PAGE_VIEW
{
    ZP_STRING_VIEW NextCursor;
    ZP_FILE_LIST_VIEW Files;
} ZP_FILE_PAGE_VIEW, *PZP_FILE_PAGE_VIEW;

typedef const ZP_FILE_PAGE_VIEW* PCZP_FILE_PAGE_VIEW;

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
ZpFile_EncodeEnumeratePageRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _In_ ULONG MaxEntries,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeEnumeratePageRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PZP_STRING_VIEW Cursor,
    _Out_ PULONG MaxEntries);

NTSTATUS
ZpFile_EncodePage(
    _In_reads_opt_(FileCount) PCZP_FILE_RECORD Files,
    _In_ ULONG FileCount,
    _In_reads_opt_(NextCursorLength) PCWCH NextCursor,
    _In_ ULONG NextCursorLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodePage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_PAGE_VIEW View);

NTSTATUS
ZpFile_EncodeOpenReadRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeOpenReadRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PULONGLONG Offset);

NTSTATUS
ZpFile_EncodeOpenReadResponse(
    _In_ ULONGLONG ChannelId,
    _In_ ULONGLONG FileSize,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeOpenReadResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG ChannelId,
    _Out_ PULONGLONG FileSize,
    _Out_ PULONGLONG Offset);

NTSTATUS
ZpFile_EncodeOpenWriteRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG FileSize,
    _In_ ZP_FILE_CREATE_DISPOSITION Disposition,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeOpenWriteRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PULONGLONG FileSize,
    _Out_ PZP_FILE_CREATE_DISPOSITION Disposition);

NTSTATUS
ZpFile_EncodeOpenWriteResponse(
    _In_ ULONGLONG ChannelId,
    _In_ ULONGLONG FileSize,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeOpenWriteResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG ChannelId,
    _Out_ PULONGLONG FileSize);

NTSTATUS
ZpFile_EncodeHashRequest(
    _In_ ZP_FILE_HASH_ALGORITHM Algorithm,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeHashRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_HASH_ALGORITHM Algorithm,
    _Out_ PZP_STRING_VIEW Path);

NTSTATUS
ZpFile_EncodeHashResponse(
    _In_ ZP_FILE_HASH_ALGORITHM Algorithm,
    _In_ ULONGLONG FileSize,
    _In_reads_bytes_(DigestLength) const BYTE* Digest,
    _In_ ULONG DigestLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeHashResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_HASH_VIEW View);

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
