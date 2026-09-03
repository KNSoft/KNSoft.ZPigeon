#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_EVENT_LOG_MODULE_ID 6
#define ZP_EVENT_LOG_OPERATION_QUERY_PAGE 1
#define ZP_EVENT_LOG_OPERATION_SET_CHANNEL_ENABLED 2
#define ZP_EVENT_LOG_OPERATION_CLEAR 3
#define ZP_EVENT_LOG_OPERATION_ENUMERATE_CHANNELS 4
#define ZP_EVENT_LOG_OPERATION_QUERY_CHANNEL_INFO 5
#define ZP_EVENT_LOG_OPERATION_CONFIGURE_CHANNEL 6
#define ZP_EVENT_LOG_PAGE_MAX_COUNT 256
#define ZP_EVENT_LOG_CHANNEL_MAX_COUNT 4096
#define ZP_EVENT_LOG_BOOKMARK_MAX_LENGTH 0x00010000UL
#define ZP_EVENT_LOG_XML_MAX_LENGTH 0x00100000UL

typedef BYTE ZP_EVENT_LOG_RETENTION_MODE, *PZP_EVENT_LOG_RETENTION_MODE;

#define ZpEventLogRetentionOverwrite ((ZP_EVENT_LOG_RETENTION_MODE)0)
#define ZpEventLogRetentionArchive ((ZP_EVENT_LOG_RETENTION_MODE)1)
#define ZpEventLogRetentionManual ((ZP_EVENT_LOG_RETENTION_MODE)2)

typedef BYTE ZP_EVENT_LOG_START_MODE, *PZP_EVENT_LOG_START_MODE;

#define ZpEventLogStartOldest ((ZP_EVENT_LOG_START_MODE)1)
#define ZpEventLogStartAfterBookmark ((ZP_EVENT_LOG_START_MODE)2)
#define ZpEventLogStartAfterBookmarkForward ((ZP_EVENT_LOG_START_MODE)3)
#define ZpEventLogStartForward ((ZP_EVENT_LOG_START_MODE)4)

typedef struct _ZP_EVENT_LOG_QUERY_VIEW
{
    ZP_EVENT_LOG_START_MODE StartMode;
    ULONG MaxEvents;
    ZP_STRING_VIEW ChannelPath;
    ZP_STRING_VIEW Query;
    ZP_STRING_VIEW Bookmark;
} ZP_EVENT_LOG_QUERY_VIEW, *PZP_EVENT_LOG_QUERY_VIEW;

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

typedef struct _ZP_EVENT_LOG_CHANNEL_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_EVENT_LOG_CHANNEL_LIST_VIEW, *PZP_EVENT_LOG_CHANNEL_LIST_VIEW;

typedef const ZP_EVENT_LOG_CHANNEL_LIST_VIEW* PCZP_EVENT_LOG_CHANNEL_LIST_VIEW;

typedef struct _ZP_EVENT_LOG_CHANNEL_INFO
{
    BOOLEAN Enabled;
    BYTE Type;
    ZP_EVENT_LOG_RETENTION_MODE RetentionMode;
    ULONGLONG MaximumSize;
    ULONGLONG FileSize;
    ULONGLONG CreationTime;
    ULONGLONG LastAccessTime;
    ULONGLONG LastWriteTime;
    PCWCH LogFilePath;
    ULONG LogFilePathLength;
} ZP_EVENT_LOG_CHANNEL_INFO, *PZP_EVENT_LOG_CHANNEL_INFO;

typedef const ZP_EVENT_LOG_CHANNEL_INFO* PCZP_EVENT_LOG_CHANNEL_INFO;

typedef struct _ZP_EVENT_LOG_CHANNEL_INFO_VIEW
{
    BOOLEAN Enabled;
    BYTE Type;
    ZP_EVENT_LOG_RETENTION_MODE RetentionMode;
    ULONGLONG MaximumSize;
    ULONGLONG FileSize;
    ULONGLONG CreationTime;
    ULONGLONG LastAccessTime;
    ULONGLONG LastWriteTime;
    ZP_STRING_VIEW LogFilePath;
} ZP_EVENT_LOG_CHANNEL_INFO_VIEW, *PZP_EVENT_LOG_CHANNEL_INFO_VIEW;

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
ZpEventLog_GetNextRecord(
    _In_ PCZP_EVENT_LOG_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_EVENT_LOG_RECORD_VIEW Record);

NTSTATUS
ZpEventLog_EncodeSetChannelEnabledRequest(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ BOOLEAN Enabled,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpEventLog_DecodeSetChannelEnabledRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW ChannelPath,
    _Out_ PBOOLEAN Enabled);

NTSTATUS
ZpEventLog_EncodeClearRequest(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpEventLog_DecodeClearRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW ChannelPath);

NTSTATUS
ZpEventLog_EncodeChannel(
    _In_reads_(ChannelLength) PCWCH Channel,
    _In_ ULONG ChannelLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpEventLog_EncodeChannelListHeader(
    _In_ ULONG ChannelCount,
    _Out_writes_bytes_(sizeof(USHORT)) PVOID Buffer);

NTSTATUS
ZpEventLog_EncodeChannels(
    _In_reads_opt_(ChannelCount) const ZP_STRING_VIEW* Channels,
    _In_ ULONG ChannelCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpEventLog_DecodeChannels(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_CHANNEL_LIST_VIEW View);

NTSTATUS
ZpEventLog_GetNextChannel(
    _In_ PCZP_EVENT_LOG_CHANNEL_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_STRING_VIEW Channel);

NTSTATUS
ZpEventLog_EncodeChannelInfo(
    _In_ PCZP_EVENT_LOG_CHANNEL_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpEventLog_DecodeChannelInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_CHANNEL_INFO_VIEW Info);

NTSTATUS
ZpEventLog_EncodeConfigureChannelRequest(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ BOOLEAN Enabled,
    _In_ ZP_EVENT_LOG_RETENTION_MODE RetentionMode,
    _In_ ULONGLONG MaximumSize,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpEventLog_DecodeConfigureChannelRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW ChannelPath,
    _Out_ PBOOLEAN Enabled,
    _Out_ PZP_EVENT_LOG_RETENTION_MODE RetentionMode,
    _Out_ PULONGLONG MaximumSize);

EXTERN_C_END
