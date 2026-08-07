#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_EVENT_LOG_MODULE_ID 6
#define ZP_EVENT_LOG_MODULE_VERSION 1
#define ZP_EVENT_LOG_OPERATION_QUERY_PAGE 1
#define ZP_EVENT_LOG_OPERATION_SUBSCRIBE 2
#define ZP_EVENT_LOG_OPERATION_UNSUBSCRIBE 3
#define ZP_EVENT_LOG_EVENT_RECORD 1
#define ZP_EVENT_LOG_EVENT_TERMINAL 2
#define ZP_EVENT_LOG_PAGE_MAX_COUNT 256
#define ZP_EVENT_LOG_BOOKMARK_MAX_LENGTH 0x00010000UL
#define ZP_EVENT_LOG_XML_MAX_LENGTH 0x00100000UL

typedef enum _ZP_EVENT_LOG_START_MODE
{
    ZpEventLogStartFuture = 1,
    ZpEventLogStartOldest = 2,
    ZpEventLogStartAfterBookmark = 3
} ZP_EVENT_LOG_START_MODE, *PZP_EVENT_LOG_START_MODE;

typedef struct _ZP_EVENT_LOG_QUERY_VIEW
{
    ZP_EVENT_LOG_START_MODE StartMode;
    ULONG MaxEvents;
    ZP_STRING_VIEW ChannelPath;
    ZP_STRING_VIEW Query;
    ZP_STRING_VIEW Bookmark;
} ZP_EVENT_LOG_QUERY_VIEW, *PZP_EVENT_LOG_QUERY_VIEW;

typedef struct _ZP_EVENT_LOG_SUBSCRIBE_VIEW
{
    ZP_EVENT_LOG_START_MODE StartMode;
    ZP_STRING_VIEW ChannelPath;
    ZP_STRING_VIEW Query;
    ZP_STRING_VIEW Bookmark;
} ZP_EVENT_LOG_SUBSCRIBE_VIEW, *PZP_EVENT_LOG_SUBSCRIBE_VIEW;

typedef struct _ZP_EVENT_LOG_RECORD
{
    PCWCH Bookmark;
    ULONG BookmarkLength;
    PCWCH Xml;
    ULONG XmlLength;
} ZP_EVENT_LOG_RECORD, *PZP_EVENT_LOG_RECORD;

typedef const ZP_EVENT_LOG_RECORD* PCZP_EVENT_LOG_RECORD;

typedef struct _ZP_EVENT_LOG_RECORD_VIEW
{
    ZP_STRING_VIEW Bookmark;
    ZP_STRING_VIEW Xml;
} ZP_EVENT_LOG_RECORD_VIEW, *PZP_EVENT_LOG_RECORD_VIEW;

typedef struct _ZP_EVENT_LOG_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_EVENT_LOG_LIST_VIEW, *PZP_EVENT_LOG_LIST_VIEW;

typedef const ZP_EVENT_LOG_LIST_VIEW* PCZP_EVENT_LOG_LIST_VIEW;

typedef struct _ZP_EVENT_LOG_PAGE_VIEW
{
    BOOLEAN HasMore;
    ZP_STRING_VIEW NextBookmark;
    ZP_EVENT_LOG_LIST_VIEW Records;
} ZP_EVENT_LOG_PAGE_VIEW, *PZP_EVENT_LOG_PAGE_VIEW;

typedef struct _ZP_EVENT_LOG_EVENT_RECORD_VIEW
{
    ULONGLONG Sequence;
    ZP_EVENT_LOG_RECORD_VIEW Record;
} ZP_EVENT_LOG_EVENT_RECORD_VIEW, *PZP_EVENT_LOG_EVENT_RECORD_VIEW;

typedef struct _ZP_EVENT_LOG_EVENT_TERMINAL_VIEW
{
    ULONGLONG NextSequence;
    NTSTATUS Status;
    ZP_STRING_VIEW LastBookmark;
} ZP_EVENT_LOG_EVENT_TERMINAL_VIEW, *PZP_EVENT_LOG_EVENT_TERMINAL_VIEW;

NTSTATUS
ZpEventLog_EncodeQueryPageRequest(
    _In_ ZP_EVENT_LOG_START_MODE StartMode,
    _In_ ULONG MaxEvents,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_reads_opt_(BookmarkLength) PCWCH Bookmark,
    _In_ ULONG BookmarkLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpEventLog_DecodeQueryPageRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_QUERY_VIEW View);

NTSTATUS
ZpEventLog_EncodePage(
    _In_ BOOLEAN HasMore,
    _In_reads_opt_(RecordCount) PCZP_EVENT_LOG_RECORD Records,
    _In_ ULONG RecordCount,
    _In_reads_opt_(NextBookmarkLength) PCWCH NextBookmark,
    _In_ ULONG NextBookmarkLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpEventLog_DecodePage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_PAGE_VIEW View);

NTSTATUS
ZpEventLog_GetRecord(
    _In_ PCZP_EVENT_LOG_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_EVENT_LOG_RECORD_VIEW Record);

NTSTATUS
ZpEventLog_EncodeSubscribeRequest(
    _In_ ZP_EVENT_LOG_START_MODE StartMode,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_reads_opt_(BookmarkLength) PCWCH Bookmark,
    _In_ ULONG BookmarkLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpEventLog_DecodeSubscribeRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_SUBSCRIBE_VIEW View);

NTSTATUS
ZpEventLog_EncodeSubscribeResponse(
    _In_ ULONGLONG SubscriptionId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpEventLog_DecodeSubscribeResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG SubscriptionId);

NTSTATUS
ZpEventLog_EncodeUnsubscribeRequest(
    _In_ ULONGLONG SubscriptionId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpEventLog_DecodeUnsubscribeRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG SubscriptionId);

NTSTATUS
ZpEventLog_EncodeRecordEvent(
    _In_ ULONGLONG Sequence,
    _In_reads_(BookmarkLength) PCWCH Bookmark,
    _In_ ULONG BookmarkLength,
    _In_reads_(XmlLength) PCWCH Xml,
    _In_ ULONG XmlLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpEventLog_DecodeRecordEvent(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_EVENT_RECORD_VIEW View);

NTSTATUS
ZpEventLog_EncodeTerminalEvent(
    _In_ ULONGLONG NextSequence,
    _In_ NTSTATUS Status,
    _In_reads_opt_(LastBookmarkLength) PCWCH LastBookmark,
    _In_ ULONG LastBookmarkLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpEventLog_DecodeTerminalEvent(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_EVENT_TERMINAL_VIEW View);

EXTERN_C_END
