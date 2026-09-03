#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_BROWSER_MODULE_ID 12

#define ZP_BROWSER_OPERATION_ENUMERATE 1
#define ZP_BROWSER_OPERATION_QUERY 2
#define ZP_BROWSER_OPERATION_OPEN_DOCUMENT 3
#define ZP_BROWSER_OPERATION_QUERY_DOCUMENT_NODE 4
#define ZP_BROWSER_OPERATION_CLOSE_DOCUMENT 5
#define ZP_BROWSER_OPERATION_INSPECT_PROFILE 6

#define ZP_BROWSER_PAGE_SIZE 100
#define ZP_BROWSER_MAX_PAGE_RECORDS MAXUSHORT
#define ZP_BROWSER_DOCUMENT_MAX_SIZE 0x00800000UL
#define ZP_BROWSER_DOCUMENT_PAGE_SIZE 100

typedef BYTE ZP_BROWSER_DOCUMENT_TYPE;
#define ZpBrowserDocumentObject ((ZP_BROWSER_DOCUMENT_TYPE)1)
#define ZpBrowserDocumentArray ((ZP_BROWSER_DOCUMENT_TYPE)2)
#define ZpBrowserDocumentString ((ZP_BROWSER_DOCUMENT_TYPE)3)
#define ZpBrowserDocumentNumber ((ZP_BROWSER_DOCUMENT_TYPE)4)
#define ZpBrowserDocumentBoolean ((ZP_BROWSER_DOCUMENT_TYPE)5)
#define ZpBrowserDocumentNull ((ZP_BROWSER_DOCUMENT_TYPE)6)

#define ZP_BROWSER_DOCUMENT_NODE_HAS_CHILDREN 0x01

typedef BYTE ZP_BROWSER_TYPE, *PZP_BROWSER_TYPE;

#define ZpBrowserChrome ((ZP_BROWSER_TYPE)1)
#define ZpBrowserEdge ((ZP_BROWSER_TYPE)2)

typedef BYTE ZP_BROWSER_KIND, *PZP_BROWSER_KIND;

#define ZpBrowserKindBrowser ((ZP_BROWSER_KIND)1)
#define ZpBrowserKindProfile ((ZP_BROWSER_KIND)2)
#define ZpBrowserKindHistory ((ZP_BROWSER_KIND)3)
#define ZpBrowserKindDownload ((ZP_BROWSER_KIND)4)
#define ZpBrowserKindBookmark ((ZP_BROWSER_KIND)5)
#define ZpBrowserKindSetting ((ZP_BROWSER_KIND)6)
#define ZpBrowserKindExtension ((ZP_BROWSER_KIND)7)
#define ZpBrowserKindCookie ((ZP_BROWSER_KIND)8)
#define ZpBrowserKindPassword ((ZP_BROWSER_KIND)9)

#define ZP_BROWSER_FLAG_SECURE 0x00000001
#define ZP_BROWSER_FLAG_HTTP_ONLY 0x00000002
#define ZP_BROWSER_FLAG_ENCRYPTED 0x00000004
#define ZP_BROWSER_FLAG_APP_BOUND 0x00000008
#define ZP_BROWSER_COOKIE_FLAGS_MASK \
    (ZP_BROWSER_FLAG_SECURE | ZP_BROWSER_FLAG_HTTP_ONLY | ZP_BROWSER_FLAG_ENCRYPTED | \
     ZP_BROWSER_FLAG_APP_BOUND)
#define ZP_BROWSER_PASSWORD_FLAGS_MASK (ZP_BROWSER_FLAG_ENCRYPTED | ZP_BROWSER_FLAG_APP_BOUND)

typedef struct _ZP_BROWSER_HISTORY_DATA
{
    ULONGLONG LastVisitTime;
    ULONG VisitCount;
    ULONG TypedCount;
} ZP_BROWSER_HISTORY_DATA, *PZP_BROWSER_HISTORY_DATA;

typedef struct _ZP_BROWSER_DOWNLOAD_DATA
{
    ULONGLONG StartTime;
    ULONGLONG EndTime;
    ULONGLONG ReceivedBytes;
    ULONGLONG TotalBytes;
    ULONG State;
    ULONG InterruptReason;
} ZP_BROWSER_DOWNLOAD_DATA, *PZP_BROWSER_DOWNLOAD_DATA;

typedef struct _ZP_BROWSER_COOKIE_DATA
{
    ULONGLONG CreationTime;
    ULONGLONG ExpirationTime;
    ULONGLONG LastAccessTime;
    ULONG SameSite;
    ULONG Flags;
} ZP_BROWSER_COOKIE_DATA, *PZP_BROWSER_COOKIE_DATA;

typedef struct _ZP_BROWSER_PASSWORD_DATA
{
    ULONGLONG CreationTime;
    ULONG Flags;
} ZP_BROWSER_PASSWORD_DATA, *PZP_BROWSER_PASSWORD_DATA;

typedef union _ZP_BROWSER_RECORD_DATA
{
    ZP_BROWSER_HISTORY_DATA History;
    ZP_BROWSER_DOWNLOAD_DATA Download;
    ZP_BROWSER_COOKIE_DATA Cookie;
    ZP_BROWSER_PASSWORD_DATA Password;
} ZP_BROWSER_RECORD_DATA, *PZP_BROWSER_RECORD_DATA;

typedef const ZP_BROWSER_RECORD_DATA* PCZP_BROWSER_RECORD_DATA;

typedef struct _ZP_BROWSER_RECORD
{
    ZP_BROWSER_KIND Kind;
    ZP_BROWSER_TYPE Browser;
    ULONGLONG Id;
    ZP_BROWSER_RECORD_DATA Data;
    PCWCH Identity;
    ULONG IdentityLength;
    PCWCH Name;
    ULONG NameLength;
    PCWCH Location;
    ULONG LocationLength;
    PCWCH Detail;
    ULONG DetailLength;
} ZP_BROWSER_RECORD, *PZP_BROWSER_RECORD;

typedef const ZP_BROWSER_RECORD* PCZP_BROWSER_RECORD;

typedef struct _ZP_BROWSER_RECORD_VIEW
{
    ZP_BROWSER_KIND Kind;
    ZP_BROWSER_TYPE Browser;
    ULONGLONG Id;
    ZP_BROWSER_RECORD_DATA Data;
    ZP_STRING_VIEW Identity;
    ZP_STRING_VIEW Name;
    ZP_STRING_VIEW Location;
    ZP_STRING_VIEW Detail;
} ZP_BROWSER_RECORD_VIEW, *PZP_BROWSER_RECORD_VIEW;

typedef struct _ZP_BROWSER_PAGE_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
    ULONGLONG NextCursor;
} ZP_BROWSER_PAGE_VIEW, *PZP_BROWSER_PAGE_VIEW;

typedef const ZP_BROWSER_PAGE_VIEW* PCZP_BROWSER_PAGE_VIEW;

typedef struct _ZP_BROWSER_QUERY_VIEW
{
    ZP_BROWSER_TYPE Browser;
    ZP_BROWSER_KIND Kind;
    ULONGLONG Cursor;
    ULONG Limit;
    ZP_STRING_VIEW Profile;
    ZP_STRING_VIEW UserData;
} ZP_BROWSER_QUERY_VIEW, *PZP_BROWSER_QUERY_VIEW;

typedef const ZP_BROWSER_QUERY_VIEW* PCZP_BROWSER_QUERY_VIEW;

typedef struct _ZP_BROWSER_PROFILE_INSPECTION
{
    ULONGLONG ProfileSize;
    ULONGLONG AvailableSpace;
    BOOLEAN BrowserRunning;
} ZP_BROWSER_PROFILE_INSPECTION, *PZP_BROWSER_PROFILE_INSPECTION;

typedef const ZP_BROWSER_PROFILE_INSPECTION* PCZP_BROWSER_PROFILE_INSPECTION;

typedef struct _ZP_BROWSER_DOCUMENT_NODE
{
    ULONG Id;
    ZP_BROWSER_DOCUMENT_TYPE Type;
    BYTE Flags;
    PCWCH Name;
    ULONG NameLength;
    PCWCH Value;
    ULONG ValueLength;
} ZP_BROWSER_DOCUMENT_NODE, *PZP_BROWSER_DOCUMENT_NODE;

typedef const ZP_BROWSER_DOCUMENT_NODE* PCZP_BROWSER_DOCUMENT_NODE;

typedef struct _ZP_BROWSER_DOCUMENT_NODE_VIEW
{
    ULONG Id;
    ZP_BROWSER_DOCUMENT_TYPE Type;
    BYTE Flags;
    ZP_STRING_VIEW Name;
    ZP_STRING_VIEW Value;
} ZP_BROWSER_DOCUMENT_NODE_VIEW, *PZP_BROWSER_DOCUMENT_NODE_VIEW;

typedef struct _ZP_BROWSER_DOCUMENT_PAGE_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG SnapshotId;
    ULONG NextCursor;
    ULONG Count;
    ZP_BROWSER_DOCUMENT_TYPE ParentType;
} ZP_BROWSER_DOCUMENT_PAGE_VIEW, *PZP_BROWSER_DOCUMENT_PAGE_VIEW;

typedef const ZP_BROWSER_DOCUMENT_PAGE_VIEW* PCZP_BROWSER_DOCUMENT_PAGE_VIEW;

NTSTATUS
ZpBrowser_EncodeRecord(
    _In_ PCZP_BROWSER_RECORD Record,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpBrowser_EncodePageHeader(
    _In_ ULONGLONG NextCursor,
    _In_ ULONG RecordCount,
    _Out_writes_bytes_(sizeof(ULONGLONG) + sizeof(USHORT)) PVOID Buffer);

NTSTATUS
ZpBrowser_EncodePage(
    _In_reads_opt_(RecordCount) PCZP_BROWSER_RECORD Records,
    _In_ ULONG RecordCount,
    _In_ ULONGLONG NextCursor,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpBrowser_DecodePage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_BROWSER_PAGE_VIEW View);

NTSTATUS
ZpBrowser_GetNextRecord(
    _In_ PCZP_BROWSER_PAGE_VIEW Page,
    _Inout_ PULONG Offset,
    _Out_ PZP_BROWSER_RECORD_VIEW Record);

NTSTATUS
ZpBrowser_EncodeQuery(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ ZP_BROWSER_KIND Kind,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_reads_opt_(UserDataLength) PCWCH UserData,
    _In_ ULONG UserDataLength,
    _In_ ULONGLONG Cursor,
    _In_ ULONG Limit,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpBrowser_DecodeQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_BROWSER_QUERY_VIEW Query);

NTSTATUS
ZpBrowser_EncodeProfileInspectionRequest(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_reads_opt_(UserDataLength) PCWCH UserData,
    _In_ ULONG UserDataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpBrowser_DecodeProfileInspectionRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_BROWSER_TYPE Browser,
    _Out_ PZP_STRING_VIEW Profile,
    _Out_ PZP_STRING_VIEW UserData);

NTSTATUS
ZpBrowser_EncodeProfileInspection(
    _In_ PCZP_BROWSER_PROFILE_INSPECTION Inspection,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpBrowser_DecodeProfileInspection(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_BROWSER_PROFILE_INSPECTION Inspection);

NTSTATUS
ZpBrowser_EncodeDocumentQuery(
    _In_ ULONG SnapshotId,
    _In_ ULONG NodeId,
    _In_ ULONG Cursor,
    _In_ ULONG Limit,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpBrowser_DecodeDocumentQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG SnapshotId,
    _Out_ PULONG NodeId,
    _Out_ PULONG Cursor,
    _Out_ PULONG Limit);

NTSTATUS
ZpBrowser_EncodeDocumentClose(
    _In_ ULONG SnapshotId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpBrowser_DecodeDocumentClose(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG SnapshotId);

NTSTATUS
ZpBrowser_EncodeDocumentPage(
    _In_ ULONG SnapshotId,
    _In_ ZP_BROWSER_DOCUMENT_TYPE ParentType,
    _In_ ULONG NextCursor,
    _In_reads_opt_(NodeCount) PCZP_BROWSER_DOCUMENT_NODE Nodes,
    _In_ ULONG NodeCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpBrowser_DecodeDocumentPage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_BROWSER_DOCUMENT_PAGE_VIEW View);

NTSTATUS
ZpBrowser_GetNextDocumentNode(
    _In_ PCZP_BROWSER_DOCUMENT_PAGE_VIEW Page,
    _Inout_ PULONG Offset,
    _Out_ PZP_BROWSER_DOCUMENT_NODE_VIEW Node);

EXTERN_C_END
