#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_BROWSER_MODULE_ID 12
#define ZP_BROWSER_MODULE_VERSION 1

#define ZP_BROWSER_OPERATION_ENUMERATE 1
#define ZP_BROWSER_OPERATION_QUERY 2

#define ZP_BROWSER_PAGE_SIZE 100
#define ZP_BROWSER_DOCUMENT_MAX_SIZE 0x00800000UL

typedef USHORT ZP_BROWSER_TYPE, *PZP_BROWSER_TYPE;

#define ZpBrowserChrome ((ZP_BROWSER_TYPE)1)
#define ZpBrowserEdge ((ZP_BROWSER_TYPE)2)

typedef USHORT ZP_BROWSER_KIND, *PZP_BROWSER_KIND;

#define ZpBrowserKindBrowser ((ZP_BROWSER_KIND)1)
#define ZpBrowserKindProfile ((ZP_BROWSER_KIND)2)
#define ZpBrowserKindHistory ((ZP_BROWSER_KIND)3)
#define ZpBrowserKindDownload ((ZP_BROWSER_KIND)4)
#define ZpBrowserKindBookmark ((ZP_BROWSER_KIND)5)
#define ZpBrowserKindSetting ((ZP_BROWSER_KIND)6)
#define ZpBrowserKindExtension ((ZP_BROWSER_KIND)7)
#define ZpBrowserKindCookie ((ZP_BROWSER_KIND)8)

typedef struct _ZP_BROWSER_RECORD
{
    ZP_BROWSER_KIND Kind;
    ZP_BROWSER_TYPE Browser;
    ULONG State;
    ULONG Flags;
    ULONGLONG Id;
    ULONGLONG Time;
    ULONGLONG Value;
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
    ULONG State;
    ULONG Flags;
    ULONGLONG Id;
    ULONGLONG Time;
    ULONGLONG Value;
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
} ZP_BROWSER_QUERY_VIEW, *PZP_BROWSER_QUERY_VIEW;

typedef const ZP_BROWSER_QUERY_VIEW* PCZP_BROWSER_QUERY_VIEW;

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
ZpBrowser_GetRecord(
    _In_ PCZP_BROWSER_PAGE_VIEW Page,
    _In_ ULONG Index,
    _Out_ PZP_BROWSER_RECORD_VIEW Record);

NTSTATUS
ZpBrowser_EncodeQuery(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ ZP_BROWSER_KIND Kind,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
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

EXTERN_C_END
