#include "Include/KNSoft/ZPigeon/EventLog.h"

static
LOGICAL
ZpEventLog_IsStartModeValid(
    _In_ ZP_EVENT_LOG_START_MODE StartMode,
    _In_ LOGICAL AllowFuture,
    _In_ ULONG BookmarkLength)
{
    if (StartMode == ZpEventLogStartAfterBookmark)
    {
        return BookmarkLength != 0;
    }
    if (BookmarkLength != 0)
    {
        return FALSE;
    }
    return StartMode == ZpEventLogStartOldest ||
           (AllowFuture && StartMode == ZpEventLogStartFuture);
}

static
LOGICAL
ZpEventLog_IsStringValid(
    _In_reads_opt_(Length) PCWCH String,
    _In_ ULONG Length,
    _In_ ULONG MaximumLength,
    _In_ LOGICAL Required)
{
    return Length <= MaximumLength &&
           (!Required || Length != 0) &&
           (Length == 0 || String != NULL);
}

static
NTSTATUS
ZpEventLog_WriteQuery(
    _In_ ZP_EVENT_LOG_START_MODE StartMode,
    _In_ ULONG MaxEvents,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_reads_opt_(BookmarkLength) PCWCH Bookmark,
    _In_ ULONG BookmarkLength,
    _In_ LOGICAL IncludeMaxEvents,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (!ZpEventLog_IsStartModeValid(StartMode,
                                     !IncludeMaxEvents,
                                     BookmarkLength) ||
        (IncludeMaxEvents &&
         (MaxEvents == 0 || MaxEvents > ZP_EVENT_LOG_PAGE_MAX_COUNT)) ||
        !ZpEventLog_IsStringValid(ChannelPath,
                                  ChannelPathLength,
                                  ZP_CODEC_MAX_ELEMENT_COUNT,
                                  TRUE) ||
        !ZpEventLog_IsStringValid(Query,
                                  QueryLength,
                                  ZP_CODEC_MAX_ELEMENT_COUNT,
                                  FALSE) ||
        !ZpEventLog_IsStringValid(Bookmark,
                                  BookmarkLength,
                                  ZP_EVENT_LOG_BOOKMARK_MAX_LENGTH,
                                  FALSE))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(USHORT) +
                   (IncludeMaxEvents ? sizeof(ULONG) : 0) +
                   3 * sizeof(ULONG) +
                   ((ULONGLONG)ChannelPathLength +
                    QueryLength +
                    BookmarkLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 16)
    {
        return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < RequiredSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt16(&Writer, (USHORT)StartMode);
    if (NT_SUCCESS(Status) && IncludeMaxEvents)
    {
        Status = ZpCodec_WriteUInt32(&Writer, MaxEvents);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, ChannelPath, ChannelPathLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Query, QueryLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Bookmark, BookmarkLength);
    }
    return Status;
}

static
NTSTATUS
ZpEventLog_ReadQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ LOGICAL IncludeMaxEvents,
    _Out_ PZP_EVENT_LOG_START_MODE StartMode,
    _Out_opt_ PULONG MaxEvents,
    _Out_ PZP_STRING_VIEW ChannelPath,
    _Out_ PZP_STRING_VIEW Query,
    _Out_ PZP_STRING_VIEW Bookmark)
{
    ZP_CODEC_READER Reader;
    USHORT StartModeValue = 0;
    ULONG LocalMaxEvents = 0;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt16(&Reader, &StartModeValue);
    if (NT_SUCCESS(Status) && IncludeMaxEvents)
    {
        Status = ZpCodec_ReadUInt32(&Reader, &LocalMaxEvents);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, ChannelPath);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, Query);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, Bookmark);
    }
    *StartMode = (ZP_EVENT_LOG_START_MODE)StartModeValue;
    if (!NT_SUCCESS(Status) ||
        !ZpEventLog_IsStartModeValid(*StartMode,
                                     !IncludeMaxEvents,
                                     Bookmark->Length) ||
        (IncludeMaxEvents &&
         (LocalMaxEvents == 0 ||
          LocalMaxEvents > ZP_EVENT_LOG_PAGE_MAX_COUNT)) ||
        ChannelPath->Length == 0 ||
        Bookmark->Length > ZP_EVENT_LOG_BOOKMARK_MAX_LENGTH ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    if (MaxEvents != NULL)
    {
        *MaxEvents = LocalMaxEvents;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpEventLog_ReadRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_EVENT_LOG_RECORD_VIEW Record)
{
    ZP_EVENT_LOG_RECORD_VIEW LocalRecord;
    NTSTATUS Status;

    Status = ZpCodec_ReadString(Reader, &LocalRecord.Bookmark);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(Reader, &LocalRecord.Xml);
    }
    if (NT_SUCCESS(Status) &&
        (LocalRecord.Bookmark.Length == 0 ||
         LocalRecord.Bookmark.Length > ZP_EVENT_LOG_BOOKMARK_MAX_LENGTH ||
         LocalRecord.Xml.Length == 0 ||
         LocalRecord.Xml.Length > ZP_EVENT_LOG_XML_MAX_LENGTH))
    {
        Status = STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Record != NULL)
    {
        *Record = LocalRecord;
    }
    return Status;
}

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
    _Out_ PULONG BytesWritten)
{
    return ZpEventLog_WriteQuery(StartMode,
                                 MaxEvents,
                                 ChannelPath,
                                 ChannelPathLength,
                                 Query,
                                 QueryLength,
                                 Bookmark,
                                 BookmarkLength,
                                 TRUE,
                                 Buffer,
                                 BufferSize,
                                 BytesWritten);
}

NTSTATUS
ZpEventLog_DecodeQueryPageRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_QUERY_VIEW View)
{
    return ZpEventLog_ReadQuery(Payload,
                                PayloadLength,
                                TRUE,
                                &View->StartMode,
                                &View->MaxEvents,
                                &View->ChannelPath,
                                &View->Query,
                                &View->Bookmark);
}

NTSTATUS
ZpEventLog_EncodePage(
    _In_ BOOLEAN HasMore,
    _In_reads_opt_(RecordCount) PCZP_EVENT_LOG_RECORD Records,
    _In_ ULONG RecordCount,
    _In_reads_opt_(NextBookmarkLength) PCWCH NextBookmark,
    _In_ ULONG NextBookmarkLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;
    ULONG Index;

    if ((HasMore != FALSE && HasMore != TRUE) ||
        RecordCount > ZP_EVENT_LOG_PAGE_MAX_COUNT ||
        (RecordCount != 0 && Records == NULL) ||
        (HasMore && RecordCount == 0) ||
        !ZpEventLog_IsStringValid(NextBookmark,
                                  NextBookmarkLength,
                                  ZP_EVENT_LOG_BOOKMARK_MAX_LENGTH,
                                  FALSE))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(BYTE) + 2 * sizeof(ULONG) +
                   (ULONGLONG)NextBookmarkLength * sizeof(WCHAR);
    for (Index = 0; Index < RecordCount; Index++)
    {
        if (!ZpEventLog_IsStringValid(Records[Index].Bookmark,
                                      Records[Index].BookmarkLength,
                                      ZP_EVENT_LOG_BOOKMARK_MAX_LENGTH,
                                      TRUE) ||
            !ZpEventLog_IsStringValid(Records[Index].Xml,
                                      Records[Index].XmlLength,
                                      ZP_EVENT_LOG_XML_MAX_LENGTH,
                                      TRUE))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 2 * sizeof(ULONG) +
                        ((ULONGLONG)Records[Index].BookmarkLength +
                         Records[Index].XmlLength) * sizeof(WCHAR);
        if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12)
        {
            return STATUS_BUFFER_OVERFLOW;
        }
    }
    if (RecordCount != 0 &&
        (Records[RecordCount - 1].BookmarkLength != NextBookmarkLength ||
         RtlCompareMemory(Records[RecordCount - 1].Bookmark,
                          NextBookmark,
                          (SIZE_T)NextBookmarkLength * sizeof(WCHAR)) !=
             (SIZE_T)NextBookmarkLength * sizeof(WCHAR)))
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < RequiredSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteBoolean(&Writer, HasMore);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer,
                                     NextBookmark,
                                     NextBookmarkLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteArrayCount(&Writer, RecordCount);
    }
    for (Index = 0; NT_SUCCESS(Status) && Index < RecordCount; Index++)
    {
        Status = ZpCodec_WriteString(&Writer,
                                     Records[Index].Bookmark,
                                     Records[Index].BookmarkLength);
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteString(&Writer,
                                         Records[Index].Xml,
                                         Records[Index].XmlLength);
        }
    }
    return Status;
}

NTSTATUS
ZpEventLog_DecodePage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_PAGE_VIEW View)
{
    ZP_CODEC_READER Reader;
    ZP_EVENT_LOG_RECORD_VIEW Record;
    ULONG Count, Index, ListOffset;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadBoolean(&Reader, &View->HasMore);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->NextBookmark);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadArrayCount(&Reader, &Count);
        if (NT_SUCCESS(Status) && Count > ZP_EVENT_LOG_PAGE_MAX_COUNT)
        {
            Status = STATUS_DATA_ERROR;
        }
    }
    else
    {
        Count = 0;
    }
    ListOffset = Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpEventLog_ReadRecord(&Reader, &Record);
    }
    if (!NT_SUCCESS(Status) ||
        (View->HasMore && Count == 0) ||
        View->NextBookmark.Length > ZP_EVENT_LOG_BOOKMARK_MAX_LENGTH ||
        (Count != 0 &&
         (Record.Bookmark.Length != View->NextBookmark.Length ||
          RtlCompareMemory(Record.Bookmark.Buffer,
                           View->NextBookmark.Buffer,
                           (SIZE_T)Record.Bookmark.Length * sizeof(WCHAR)) !=
              (SIZE_T)Record.Bookmark.Length * sizeof(WCHAR))) ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Records.Buffer = Add2Ptr(Payload, ListOffset);
    View->Records.Length = PayloadLength - ListOffset;
    View->Records.Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpEventLog_GetRecord(
    _In_ PCZP_EVENT_LOG_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_EVENT_LOG_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Current;

    if (Index >= List->Count)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeReader(&Reader, List->Buffer, List->Length);
    for (Current = 0; NT_SUCCESS(Status) && Current <= Index; Current++)
    {
        Status = ZpEventLog_ReadRecord(&Reader,
                                      Current == Index ? Record : NULL);
    }
    return Status;
}

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
    _Out_ PULONG BytesWritten)
{
    return ZpEventLog_WriteQuery(StartMode,
                                 0,
                                 ChannelPath,
                                 ChannelPathLength,
                                 Query,
                                 QueryLength,
                                 Bookmark,
                                 BookmarkLength,
                                 FALSE,
                                 Buffer,
                                 BufferSize,
                                 BytesWritten);
}

NTSTATUS
ZpEventLog_DecodeSubscribeRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_SUBSCRIBE_VIEW View)
{
    return ZpEventLog_ReadQuery(Payload,
                                PayloadLength,
                                FALSE,
                                &View->StartMode,
                                NULL,
                                &View->ChannelPath,
                                &View->Query,
                                &View->Bookmark);
}

static
NTSTATUS
ZpEventLog_EncodeSubscriptionId(
    _In_ ULONGLONG SubscriptionId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;

    if (SubscriptionId == 0 || (SubscriptionId & 1) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = sizeof(SubscriptionId);
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < sizeof(SubscriptionId))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteUInt64(&Writer, SubscriptionId);
}

static
NTSTATUS
ZpEventLog_DecodeSubscriptionId(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG SubscriptionId)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != sizeof(*SubscriptionId))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, SubscriptionId);
    if (NT_SUCCESS(Status) &&
        (*SubscriptionId == 0 || (*SubscriptionId & 1) != 0))
    {
        Status = STATUS_DATA_ERROR;
    }
    return Status;
}

NTSTATUS
ZpEventLog_EncodeSubscribeResponse(
    _In_ ULONGLONG SubscriptionId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    return ZpEventLog_EncodeSubscriptionId(SubscriptionId,
                                           Buffer,
                                           BufferSize,
                                           BytesWritten);
}

NTSTATUS
ZpEventLog_DecodeSubscribeResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG SubscriptionId)
{
    return ZpEventLog_DecodeSubscriptionId(Payload,
                                           PayloadLength,
                                           SubscriptionId);
}

NTSTATUS
ZpEventLog_EncodeUnsubscribeRequest(
    _In_ ULONGLONG SubscriptionId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    return ZpEventLog_EncodeSubscriptionId(SubscriptionId,
                                           Buffer,
                                           BufferSize,
                                           BytesWritten);
}

NTSTATUS
ZpEventLog_DecodeUnsubscribeRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG SubscriptionId)
{
    return ZpEventLog_DecodeSubscriptionId(Payload,
                                           PayloadLength,
                                           SubscriptionId);
}

NTSTATUS
ZpEventLog_EncodeRecordEvent(
    _In_ ULONGLONG Sequence,
    _In_reads_(BookmarkLength) PCWCH Bookmark,
    _In_ ULONG BookmarkLength,
    _In_reads_(XmlLength) PCWCH Xml,
    _In_ ULONG XmlLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Sequence == 0 ||
        !ZpEventLog_IsStringValid(Bookmark,
                                  BookmarkLength,
                                  ZP_EVENT_LOG_BOOKMARK_MAX_LENGTH,
                                  TRUE) ||
        !ZpEventLog_IsStringValid(Xml,
                                  XmlLength,
                                  ZP_EVENT_LOG_XML_MAX_LENGTH,
                                  TRUE))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(Sequence) + 2 * sizeof(ULONG) +
                   ((ULONGLONG)BookmarkLength + XmlLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12)
    {
        return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < RequiredSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, Sequence);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Bookmark, BookmarkLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Xml, XmlLength);
    }
    return Status;
}

NTSTATUS
ZpEventLog_DecodeRecordEvent(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_EVENT_RECORD_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, &View->Sequence);
    if (NT_SUCCESS(Status))
    {
        Status = ZpEventLog_ReadRecord(&Reader, &View->Record);
    }
    if (!NT_SUCCESS(Status) ||
        View->Sequence == 0 ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpEventLog_EncodeTerminalEvent(
    _In_ ULONGLONG NextSequence,
    _In_ NTSTATUS Status,
    _In_reads_opt_(LastBookmarkLength) PCWCH LastBookmark,
    _In_ ULONG LastBookmarkLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS EncodeStatus;

    if (NextSequence == 0 ||
        NT_SUCCESS(Status) ||
        !ZpEventLog_IsStringValid(LastBookmark,
                                  LastBookmarkLength,
                                  ZP_EVENT_LOG_BOOKMARK_MAX_LENGTH,
                                  FALSE))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(NextSequence) + 2 * sizeof(ULONG) +
                   (ULONGLONG)LastBookmarkLength * sizeof(WCHAR);
    if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12)
    {
        return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < RequiredSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    EncodeStatus = ZpCodec_WriteUInt64(&Writer, NextSequence);
    if (NT_SUCCESS(EncodeStatus))
    {
        EncodeStatus = ZpCodec_WriteUInt32(&Writer, (ULONG)Status);
    }
    if (NT_SUCCESS(EncodeStatus))
    {
        EncodeStatus = ZpCodec_WriteString(&Writer,
                                           LastBookmark,
                                           LastBookmarkLength);
    }
    return EncodeStatus;
}

NTSTATUS
ZpEventLog_DecodeTerminalEvent(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_EVENT_TERMINAL_VIEW View)
{
    ZP_CODEC_READER Reader;
    ULONG StatusValue;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, &View->NextSequence);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &StatusValue);
    }
    if (NT_SUCCESS(Status))
    {
        View->Status = (NTSTATUS)StatusValue;
        Status = ZpCodec_ReadString(&Reader, &View->LastBookmark);
    }
    if (!NT_SUCCESS(Status) ||
        View->NextSequence == 0 ||
        NT_SUCCESS(View->Status) ||
        View->LastBookmark.Length > ZP_EVENT_LOG_BOOKMARK_MAX_LENGTH ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}
