#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_FILE_MODULE_ID 4
#define ZP_FILE_MODULE_VERSION 1
#define ZP_FILE_OPERATION_QUERY 1
#define ZP_FILE_OPERATION_OPEN_READ 2
#define ZP_FILE_OPERATION_HASH 3
#define ZP_FILE_OPERATION_OPEN_WRITE 4
#define ZP_FILE_OPERATION_ENUMERATE_PAGE 5
#define ZP_FILE_OPERATION_DELETE 6
#define ZP_FILE_OPERATION_RENAME 7
#define ZP_FILE_OPERATION_SET_ATTRIBUTES 8
#define ZP_FILE_OPERATION_QUERY_VOLUME 9
#define ZP_FILE_OPERATION_SET_VOLUME_LABEL 10
#define ZP_FILE_OPERATION_QUERY_SECURITY 11
#define ZP_FILE_OPERATION_SET_SECURITY 12
#define ZP_FILE_OPERATION_RESOLVE_ACCOUNT 13
#define ZP_FILE_OPERATION_RESOLVE_SID 14
#define ZP_FILE_OPERATION_WRITE_RANGE 15
#define ZP_FILE_OPERATION_QUERY_OWNERS 16
#define ZP_FILE_OPERATION_CONTROL_OWNERS 17
#define ZP_FILE_RANGE_MAX_LENGTH 0x00010000UL
#define ZP_FILE_CRC32_SIZE 4
#define ZP_FILE_MD5_SIZE 16
#define ZP_FILE_SHA1_SIZE 20
#define ZP_FILE_SHA256_SIZE 32
#define ZP_FILE_PAGE_COUNT 100

typedef enum _ZP_FILE_CREATE_DISPOSITION
{
    ZpFileCreateNew = 1,
    ZpFileCreateAlways = 2
} ZP_FILE_CREATE_DISPOSITION, *PZP_FILE_CREATE_DISPOSITION;

typedef enum _ZP_FILE_HASH_ALGORITHM
{
    ZpFileHashCrc32 = 1,
    ZpFileHashMd5 = 2,
    ZpFileHashSha1 = 3,
    ZpFileHashSha256 = 4
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
    BOOLEAN HasChildren;
} ZP_FILE_INFO, *PZP_FILE_INFO;

typedef const ZP_FILE_INFO* PCZP_FILE_INFO;

typedef struct _ZP_FILE_VOLUME_INFO
{
    ULONGLONG TotalBytes;
    ULONGLONG FreeBytes;
    ULONG SerialNumber;
    ULONG MaximumComponentLength;
    ULONG FileSystemFlags;
    PCWCH Label;
    ULONG LabelLength;
    PCWCH FileSystem;
    ULONG FileSystemLength;
} ZP_FILE_VOLUME_INFO, *PZP_FILE_VOLUME_INFO;

typedef const ZP_FILE_VOLUME_INFO* PCZP_FILE_VOLUME_INFO;

typedef struct _ZP_FILE_VOLUME_INFO_VIEW
{
    ULONGLONG TotalBytes;
    ULONGLONG FreeBytes;
    ULONG SerialNumber;
    ULONG MaximumComponentLength;
    ULONG FileSystemFlags;
    ZP_STRING_VIEW Label;
    ZP_STRING_VIEW FileSystem;
} ZP_FILE_VOLUME_INFO_VIEW, *PZP_FILE_VOLUME_INFO_VIEW;

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
    ULONG EnumerationId;
    ZP_FILE_LIST_VIEW Files;
} ZP_FILE_PAGE_VIEW, *PZP_FILE_PAGE_VIEW;

typedef const ZP_FILE_PAGE_VIEW* PCZP_FILE_PAGE_VIEW;

typedef struct _ZP_FILE_WRITE_RANGE_VIEW
{
    ZP_STRING_VIEW Path;
    ULONGLONG Offset;
    ZP_BUFFER_VIEW Data;
} ZP_FILE_WRITE_RANGE_VIEW, *PZP_FILE_WRITE_RANGE_VIEW;

typedef const ZP_FILE_WRITE_RANGE_VIEW* PCZP_FILE_WRITE_RANGE_VIEW;

typedef enum _ZP_FILE_OWNER_CONTROL
{
    ZpFileOwnerTerminate = 1,
    ZpFileOwnerCloseHandles = 2
} ZP_FILE_OWNER_CONTROL, *PZP_FILE_OWNER_CONTROL;

typedef struct _ZP_FILE_OWNER_RECORD
{
    ULONG ProcessId;
    NTSTATUS ImagePathStatus;
    NTSTATUS CommandLineStatus;
    PCWCH ImageName;
    ULONG ImageNameLength;
    PCWCH ImagePath;
    ULONG ImagePathLength;
    PCWCH CommandLine;
    ULONG CommandLineLength;
    PCWCH ServiceNames;
    ULONG ServiceNamesLength;
} ZP_FILE_OWNER_RECORD, *PZP_FILE_OWNER_RECORD;

typedef const ZP_FILE_OWNER_RECORD* PCZP_FILE_OWNER_RECORD;

typedef struct _ZP_FILE_OWNER_RECORD_VIEW
{
    ULONG ProcessId;
    NTSTATUS ImagePathStatus;
    NTSTATUS CommandLineStatus;
    ZP_STRING_VIEW ImageName;
    ZP_STRING_VIEW ImagePath;
    ZP_STRING_VIEW CommandLine;
    ZP_STRING_VIEW ServiceNames;
} ZP_FILE_OWNER_RECORD_VIEW, *PZP_FILE_OWNER_RECORD_VIEW;

typedef struct _ZP_FILE_OWNER_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_FILE_OWNER_LIST_VIEW, *PZP_FILE_OWNER_LIST_VIEW;

typedef const ZP_FILE_OWNER_LIST_VIEW* PCZP_FILE_OWNER_LIST_VIEW;

typedef struct _ZP_FILE_OWNER_CONTROL_REQUEST_VIEW
{
    ZP_FILE_OWNER_CONTROL Control;
    ZP_STRING_VIEW Path;
    const BYTE* ProcessIds;
    ULONG ProcessCount;
} ZP_FILE_OWNER_CONTROL_REQUEST_VIEW, *PZP_FILE_OWNER_CONTROL_REQUEST_VIEW;

typedef const ZP_FILE_OWNER_CONTROL_REQUEST_VIEW* PCZP_FILE_OWNER_CONTROL_REQUEST_VIEW;

typedef struct _ZP_FILE_OWNER_CONTROL_RESULT
{
    ULONG ProcessId;
    NTSTATUS Status;
    ULONG AffectedHandleCount;
} ZP_FILE_OWNER_CONTROL_RESULT, *PZP_FILE_OWNER_CONTROL_RESULT;

typedef const ZP_FILE_OWNER_CONTROL_RESULT* PCZP_FILE_OWNER_CONTROL_RESULT;

typedef struct _ZP_FILE_OWNER_CONTROL_RESULT_VIEW
{
    const BYTE* Buffer;
    ULONG Count;
} ZP_FILE_OWNER_CONTROL_RESULT_VIEW, *PZP_FILE_OWNER_CONTROL_RESULT_VIEW;

typedef const ZP_FILE_OWNER_CONTROL_RESULT_VIEW* PCZP_FILE_OWNER_CONTROL_RESULT_VIEW;

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
ZpFile_EncodeRenameRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(NewPathLength) PCWCH NewPath,
    _In_ ULONG NewPathLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeRenameRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PZP_STRING_VIEW NewPath);

NTSTATUS
ZpFile_EncodeSetAttributesRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG Attributes,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeSetAttributesRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PULONG Attributes);

NTSTATUS
ZpFile_EncodeWriteRangeRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeWriteRangeRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_WRITE_RANGE_VIEW Request);

NTSTATUS
ZpFile_EncodeEnumeratePageRequest(
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG EnumerationId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeEnumeratePageRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path,
    _Out_ PULONG EnumerationId);

NTSTATUS
ZpFile_EncodePage(
    _In_reads_opt_(FileCount) PCZP_FILE_RECORD Files,
    _In_ ULONG FileCount,
    _In_ ULONG EnumerationId,
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
    _In_ ULONG ChannelId,
    _In_ ULONGLONG FileSize,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeOpenReadResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ChannelId,
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
    _In_ ULONG ChannelId,
    _In_ ULONGLONG FileSize,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeOpenWriteResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ChannelId,
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

NTSTATUS
ZpFile_EncodeVolumeInfo(
    _In_ PCZP_FILE_VOLUME_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeVolumeInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_VOLUME_INFO_VIEW Info);

NTSTATUS
ZpFile_EncodeOwnerList(
    _In_reads_opt_(OwnerCount) PCZP_FILE_OWNER_RECORD Owners,
    _In_ ULONG OwnerCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeOwnerList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_OWNER_LIST_VIEW View);

NTSTATUS
ZpFile_GetOwnerRecord(
    _In_ PCZP_FILE_OWNER_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_FILE_OWNER_RECORD_VIEW Record);

NTSTATUS
ZpFile_EncodeOwnerControlRequest(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_FILE_OWNER_CONTROL Control,
    _In_reads_(ProcessCount) const ULONG* ProcessIds,
    _In_ ULONG ProcessCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeOwnerControlRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_OWNER_CONTROL_REQUEST_VIEW Request);

NTSTATUS
ZpFile_GetOwnerControlProcessId(
    _In_ PCZP_FILE_OWNER_CONTROL_REQUEST_VIEW Request,
    _In_ ULONG Index,
    _Out_ PULONG ProcessId);

NTSTATUS
ZpFile_EncodeOwnerControlResults(
    _In_reads_(ResultCount) PCZP_FILE_OWNER_CONTROL_RESULT Results,
    _In_ ULONG ResultCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFile_DecodeOwnerControlResults(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_FILE_OWNER_CONTROL_RESULT_VIEW View);

NTSTATUS
ZpFile_GetOwnerControlResult(
    _In_ PCZP_FILE_OWNER_CONTROL_RESULT_VIEW View,
    _In_ ULONG Index,
    _Out_ PZP_FILE_OWNER_CONTROL_RESULT Result);

EXTERN_C_END
