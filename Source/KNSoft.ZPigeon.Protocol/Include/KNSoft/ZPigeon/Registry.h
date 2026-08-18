#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_REGISTRY_MODULE_ID 7
#define ZP_REGISTRY_MODULE_VERSION 1
#define ZP_REGISTRY_OPERATION_ENUMERATE_KEYS_PAGE 1
#define ZP_REGISTRY_OPERATION_ENUMERATE_VALUES_PAGE 2
#define ZP_REGISTRY_OPERATION_QUERY_VALUE 3
#define ZP_REGISTRY_OPERATION_SET_VALUE 4
#define ZP_REGISTRY_OPERATION_DELETE_VALUE 5
#define ZP_REGISTRY_OPERATION_CREATE_KEY 6
#define ZP_REGISTRY_OPERATION_DELETE_KEY 7
#define ZP_REGISTRY_OPERATION_RENAME_KEY 8
#define ZP_REGISTRY_OPERATION_RENAME_VALUE 9
#define ZP_REGISTRY_OPERATION_QUERY_SECURITY 10
#define ZP_REGISTRY_OPERATION_SET_SECURITY 11
#define ZP_REGISTRY_PAGE_MAX_COUNT 4096
#define ZP_REGISTRY_PATH_MAX_LENGTH 32767
#define ZP_REGISTRY_DATA_MAX_LENGTH 0x00100000UL
#define ZP_REGISTRY_VALUE_PREVIEW_MAX_LENGTH 256

typedef enum _ZP_REGISTRY_ROOT
{
    ZpRegistryClassesRoot = 1,
    ZpRegistryCurrentUser = 2,
    ZpRegistryLocalMachine = 3,
    ZpRegistryUsers = 4,
    ZpRegistryCurrentConfig = 5
} ZP_REGISTRY_ROOT, *PZP_REGISTRY_ROOT;

typedef struct _ZP_REGISTRY_ENUMERATE_VIEW
{
    ZP_REGISTRY_ROOT Root;
    ULONG MaxEntries;
    BOOLEAN CursorPresent;
    ZP_STRING_VIEW Path;
    ZP_STRING_VIEW Cursor;
} ZP_REGISTRY_ENUMERATE_VIEW, *PZP_REGISTRY_ENUMERATE_VIEW;

typedef const ZP_REGISTRY_ENUMERATE_VIEW* PCZP_REGISTRY_ENUMERATE_VIEW;

typedef struct _ZP_REGISTRY_VALUE_REQUEST_VIEW
{
    ZP_REGISTRY_ROOT Root;
    ZP_STRING_VIEW Path;
    ZP_STRING_VIEW ValueName;
} ZP_REGISTRY_VALUE_REQUEST_VIEW, *PZP_REGISTRY_VALUE_REQUEST_VIEW;

typedef const ZP_REGISTRY_VALUE_REQUEST_VIEW* PCZP_REGISTRY_VALUE_REQUEST_VIEW;

typedef struct _ZP_REGISTRY_SET_VALUE_VIEW
{
    ZP_REGISTRY_ROOT Root;
    ULONG Type;
    ZP_STRING_VIEW Path;
    ZP_STRING_VIEW ValueName;
    ZP_BUFFER_VIEW Data;
} ZP_REGISTRY_SET_VALUE_VIEW, *PZP_REGISTRY_SET_VALUE_VIEW;

typedef const ZP_REGISTRY_SET_VALUE_VIEW* PCZP_REGISTRY_SET_VALUE_VIEW;

typedef struct _ZP_REGISTRY_KEY_REQUEST_VIEW
{
    ZP_REGISTRY_ROOT Root;
    ZP_STRING_VIEW Path;
} ZP_REGISTRY_KEY_REQUEST_VIEW, *PZP_REGISTRY_KEY_REQUEST_VIEW;

typedef const ZP_REGISTRY_KEY_REQUEST_VIEW* PCZP_REGISTRY_KEY_REQUEST_VIEW;

typedef struct _ZP_REGISTRY_RENAME_REQUEST_VIEW
{
    ZP_REGISTRY_ROOT Root;
    ZP_STRING_VIEW Path;
    ZP_STRING_VIEW Name;
    ZP_STRING_VIEW NewName;
} ZP_REGISTRY_RENAME_REQUEST_VIEW, *PZP_REGISTRY_RENAME_REQUEST_VIEW;

typedef const ZP_REGISTRY_RENAME_REQUEST_VIEW*
    PCZP_REGISTRY_RENAME_REQUEST_VIEW;

typedef struct _ZP_REGISTRY_SECURITY_REQUEST_VIEW
{
    ZP_REGISTRY_ROOT Root;
    ZP_STRING_VIEW Path;
    ZP_STRING_VIEW Sddl;
} ZP_REGISTRY_SECURITY_REQUEST_VIEW, *PZP_REGISTRY_SECURITY_REQUEST_VIEW;

typedef const ZP_REGISTRY_SECURITY_REQUEST_VIEW*
    PCZP_REGISTRY_SECURITY_REQUEST_VIEW;

typedef struct _ZP_REGISTRY_KEY_RECORD
{
    PCWCH Name;
    ULONG NameLength;
    ULONGLONG LastWriteTime;
    BOOLEAN HasChildren;
} ZP_REGISTRY_KEY_RECORD, *PZP_REGISTRY_KEY_RECORD;

typedef const ZP_REGISTRY_KEY_RECORD* PCZP_REGISTRY_KEY_RECORD;

typedef struct _ZP_REGISTRY_KEY_RECORD_VIEW
{
    ZP_STRING_VIEW Name;
    ULONGLONG LastWriteTime;
    BOOLEAN HasChildren;
} ZP_REGISTRY_KEY_RECORD_VIEW, *PZP_REGISTRY_KEY_RECORD_VIEW;

typedef struct _ZP_REGISTRY_VALUE_RECORD
{
    PCWCH Name;
    ULONG NameLength;
    ULONG Type;
    ULONG DataLength;
    const VOID* Preview;
    ULONG PreviewLength;
} ZP_REGISTRY_VALUE_RECORD, *PZP_REGISTRY_VALUE_RECORD;

typedef const ZP_REGISTRY_VALUE_RECORD* PCZP_REGISTRY_VALUE_RECORD;

typedef struct _ZP_REGISTRY_VALUE_RECORD_VIEW
{
    ZP_STRING_VIEW Name;
    ULONG Type;
    ULONG DataLength;
    ZP_BUFFER_VIEW Preview;
} ZP_REGISTRY_VALUE_RECORD_VIEW, *PZP_REGISTRY_VALUE_RECORD_VIEW;

typedef struct _ZP_REGISTRY_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_REGISTRY_LIST_VIEW, *PZP_REGISTRY_LIST_VIEW;

typedef const ZP_REGISTRY_LIST_VIEW* PCZP_REGISTRY_LIST_VIEW;

typedef struct _ZP_REGISTRY_PAGE_VIEW
{
    BOOLEAN HasMore;
    ZP_STRING_VIEW NextCursor;
    ZP_REGISTRY_LIST_VIEW Records;
} ZP_REGISTRY_PAGE_VIEW, *PZP_REGISTRY_PAGE_VIEW;

typedef const ZP_REGISTRY_PAGE_VIEW* PCZP_REGISTRY_PAGE_VIEW;

typedef struct _ZP_REGISTRY_VALUE_VIEW
{
    ULONG Type;
    ZP_BUFFER_VIEW Data;
} ZP_REGISTRY_VALUE_VIEW, *PZP_REGISTRY_VALUE_VIEW;

typedef const ZP_REGISTRY_VALUE_VIEW* PCZP_REGISTRY_VALUE_VIEW;

NTSTATUS
ZpRegistry_EncodeEnumerateRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ULONG MaxEntries,
    _In_ BOOLEAN CursorPresent,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRegistry_DecodeEnumerateRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_ENUMERATE_VIEW Request);

NTSTATUS
ZpRegistry_EncodeKeyPage(
    _In_ BOOLEAN HasMore,
    _In_reads_opt_(RecordCount) PCZP_REGISTRY_KEY_RECORD Records,
    _In_ ULONG RecordCount,
    _In_reads_opt_(NextCursorLength) PCWCH NextCursor,
    _In_ ULONG NextCursorLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRegistry_DecodeKeyPage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_PAGE_VIEW Page);

NTSTATUS
ZpRegistry_GetKeyRecord(
    _In_ PCZP_REGISTRY_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_REGISTRY_KEY_RECORD_VIEW Record);

NTSTATUS
ZpRegistry_EncodeValuePage(
    _In_ BOOLEAN HasMore,
    _In_reads_opt_(RecordCount) PCZP_REGISTRY_VALUE_RECORD Records,
    _In_ ULONG RecordCount,
    _In_reads_opt_(NextCursorLength) PCWCH NextCursor,
    _In_ ULONG NextCursorLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRegistry_DecodeValuePage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_PAGE_VIEW Page);

NTSTATUS
ZpRegistry_GetValueRecord(
    _In_ PCZP_REGISTRY_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_REGISTRY_VALUE_RECORD_VIEW Record);

NTSTATUS
ZpRegistry_EncodeValueRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRegistry_DecodeValueRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_VALUE_REQUEST_VIEW Request);

NTSTATUS
ZpRegistry_EncodeValue(
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRegistry_DecodeValue(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_VALUE_VIEW Value);

NTSTATUS
ZpRegistry_EncodeSetValueRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ULONG Type,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRegistry_DecodeSetValueRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_SET_VALUE_VIEW Request);

NTSTATUS
ZpRegistry_EncodeKeyRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRegistry_DecodeKeyRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_KEY_REQUEST_VIEW Request);

NTSTATUS
ZpRegistry_EncodeRenameRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_reads_(NewNameLength) PCWCH NewName,
    _In_ ULONG NewNameLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRegistry_DecodeRenameRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_RENAME_REQUEST_VIEW Request);

NTSTATUS
ZpRegistry_EncodeSecurityRequest(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(SddlLength) PCWCH Sddl,
    _In_ ULONG SddlLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRegistry_DecodeSecurityRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_REGISTRY_SECURITY_REQUEST_VIEW Request);

EXTERN_C_END
