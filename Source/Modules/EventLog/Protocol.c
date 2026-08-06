#include "Include/KNSoft/ZPigeon/EventLog.h"

#include "../../KNSoft.ZPigeon.Protocol/Core/Protocol.inl"

static
LOGICAL
ZpEventLog_IsStartModeValid(
    _In_ ZP_EVENT_LOG_START_MODE StartMode,
    _In_ ULONG BookmarkLength)
{
    if (StartMode == ZpEventLogStartAfterBookmark ||
        StartMode == ZpEventLogStartAfterBookmarkForward)
    {
        return BookmarkLength != 0;
    }
    if (BookmarkLength != 0)
    {
        return FALSE;
    }
    return StartMode == ZpEventLogStartOldest ||
           StartMode == ZpEventLogStartForward;
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
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (!ZpEventLog_IsStartModeValid(StartMode, BookmarkLength) ||
        MaxEvents == 0 || MaxEvents > ZP_EVENT_LOG_PAGE_MAX_COUNT ||
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
    RequiredSize = sizeof(BYTE) +
                   sizeof(ULONG) +
                   3 * sizeof(ULONG) +
                   ((ULONGLONG)ChannelPathLength +
                    QueryLength +
                    BookmarkLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE)
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
    Status = ZpCodec_WriteByte(&Writer, StartMode);
    if (NT_SUCCESS(Status))
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
    _Out_ PZP_EVENT_LOG_START_MODE StartMode,
    _Out_ PULONG MaxEvents,
    _Out_ PZP_STRING_VIEW ChannelPath,
    _Out_ PZP_STRING_VIEW Query,
    _Out_ PZP_STRING_VIEW Bookmark)
{
    ZP_CODEC_READER Reader;
    ZP_EVENT_LOG_START_MODE LocalStartMode;
    ULONG LocalMaxEvents;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &LocalStartMode);
    if (NT_SUCCESS(Status))
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
    if (!NT_SUCCESS(Status) ||
        !ZpEventLog_IsStartModeValid(LocalStartMode, Bookmark->Length) ||
        LocalMaxEvents == 0 ||
        LocalMaxEvents > ZP_EVENT_LOG_PAGE_MAX_COUNT ||
        ChannelPath->Length == 0 ||
        Bookmark->Length > ZP_EVENT_LOG_BOOKMARK_MAX_LENGTH ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    *StartMode = LocalStartMode;
    *MaxEvents = LocalMaxEvents;
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
    PBYTE Cursor;
    ULONGLONG RequiredSize;
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
        if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE)
        {
            return STATUS_BUFFER_OVERFLOW;
        }
    }
    if (RecordCount != 0 &&
        Records[RecordCount - 1].BookmarkLength != NextBookmarkLength)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (RecordCount != 0 && NextBookmarkLength != 0 &&
        (NextBookmark == NULL ||
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
    Cursor = Buffer;
    ZpWire_WriteByte(&Cursor, HasMore);
    ZpWire_WriteString(&Cursor, NextBookmark, NextBookmarkLength);
    ZpWire_WriteUInt32(&Cursor, RecordCount);
    for (Index = 0; Index < RecordCount; Index++)
    {
        ZpWire_WriteString(&Cursor, Records[Index].Bookmark, Records[Index].BookmarkLength);
        ZpWire_WriteString(&Cursor, Records[Index].Xml, Records[Index].XmlLength);
    }
    return STATUS_SUCCESS;
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
ZpEventLog_GetNextRecord(
    _In_ PCZP_EVENT_LOG_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_EVENT_LOG_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpEventLog_ReadRecord(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

static
NTSTATUS
ZpEventLog_EncodeChannelRequest(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_opt_ PBOOLEAN Enabled,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (!ZpEventLog_IsStringValid(ChannelPath,
                                  ChannelPathLength,
                                  ZP_CODEC_MAX_ELEMENT_COUNT,
                                  TRUE))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(ULONG) +
                   (ULONGLONG)ChannelPathLength * sizeof(WCHAR) +
                   (Enabled != NULL ? sizeof(BYTE) : 0);
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE)
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
    Status = ZpCodec_WriteString(&Writer, ChannelPath, ChannelPathLength);
    if (NT_SUCCESS(Status) && Enabled != NULL)
    {
        Status = ZpCodec_WriteBoolean(&Writer, *Enabled);
    }
    return Status;
}

static
NTSTATUS
ZpEventLog_DecodeChannelRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW ChannelPath,
    _Out_opt_ PBOOLEAN Enabled)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, ChannelPath);
    if (NT_SUCCESS(Status) && Enabled != NULL)
    {
        Status = ZpCodec_ReadBoolean(&Reader, Enabled);
    }
    if (!NT_SUCCESS(Status) ||
        ChannelPath->Length == 0 ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpEventLog_EncodeSetChannelEnabledRequest(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ BOOLEAN Enabled,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    return ZpEventLog_EncodeChannelRequest(ChannelPath,
                                           ChannelPathLength,
                                           &Enabled,
                                           Buffer,
                                           BufferSize,
                                           BytesWritten);
}

NTSTATUS
ZpEventLog_DecodeSetChannelEnabledRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW ChannelPath,
    _Out_ PBOOLEAN Enabled)
{
    return ZpEventLog_DecodeChannelRequest(Payload,
                                            PayloadLength,
                                            ChannelPath,
                                            Enabled);
}

NTSTATUS
ZpEventLog_EncodeClearRequest(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    return ZpEventLog_EncodeChannelRequest(ChannelPath,
                                           ChannelPathLength,
                                           NULL,
                                           Buffer,
                                           BufferSize,
                                           BytesWritten);
}

NTSTATUS
ZpEventLog_DecodeClearRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW ChannelPath)
{
    return ZpEventLog_DecodeChannelRequest(Payload,
                                            PayloadLength,
                                            ChannelPath,
                                            NULL);
}

NTSTATUS
ZpEventLog_EncodeChannel(
    _In_reads_(ChannelLength) PCWCH Channel,
    _In_ ULONG ChannelLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize;

    if (Channel == NULL || ChannelLength == 0 || ChannelLength > ZP_CODEC_MAX_ELEMENT_COUNT)
        return STATUS_INVALID_PARAMETER;
    RequiredSize = sizeof(ULONG) + (ULONGLONG)ChannelLength * sizeof(WCHAR);
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteString(&Cursor, Channel, ChannelLength);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpEventLog_EncodeChannelListHeader(
    _In_ ULONG ChannelCount,
    _Out_writes_bytes_(sizeof(ULONG)) PVOID Buffer)
{
    PBYTE Cursor = Buffer;

    if (ChannelCount > ZP_EVENT_LOG_CHANNEL_MAX_COUNT || Buffer == NULL) return STATUS_INVALID_PARAMETER;
    ZpWire_WriteUInt32(&Cursor, ChannelCount);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpEventLog_EncodeChannels(
    _In_reads_opt_(ChannelCount) const ZP_STRING_VIEW* Channels,
    _In_ ULONG ChannelCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = sizeof(ULONG);
    ULONG ChannelSize, Index;

    if (ChannelCount > ZP_EVENT_LOG_CHANNEL_MAX_COUNT || (ChannelCount != 0 && Channels == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < ChannelCount; Index++)
    {
        NTSTATUS Status = ZpEventLog_EncodeChannel((PCWCH)Channels[Index].Buffer,
                                                   Channels[Index].Length,
                                                   NULL,
                                                   0,
                                                   &ChannelSize);

        if (!NT_SUCCESS(Status)) return Status;
        RequiredSize += ChannelSize;
    }
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpEventLog_EncodeChannelListHeader(ChannelCount, Buffer);
    Cursor = Add2Ptr(Buffer, sizeof(ULONG));
    for (Index = 0; Index < ChannelCount; Index++)
    {
        NTSTATUS Status = ZpEventLog_EncodeChannel((PCWCH)Channels[Index].Buffer,
                                                   Channels[Index].Length,
                                                   Cursor,
                                                   BufferSize - (ULONG)(Cursor - (PBYTE)Buffer),
                                                   &ChannelSize);

        if (!NT_SUCCESS(Status)) return Status;
        Cursor += ChannelSize;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpEventLog_DecodeChannels(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_CHANNEL_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    ZP_STRING_VIEW Channel;
    ULONG Index, Offset;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &View->Count);
    if (!NT_SUCCESS(Status) || View->Count > ZP_EVENT_LOG_CHANNEL_MAX_COUNT)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    Offset = Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < View->Count; Index++)
    {
        Status = ZpCodec_ReadString(&Reader, &Channel);
        if (NT_SUCCESS(Status) && Channel.Length == 0) Status = STATUS_DATA_ERROR;
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Buffer = Add2Ptr(Payload, Offset);
    View->Length = PayloadLength - Offset;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpEventLog_GetNextChannel(
    _In_ PCZP_EVENT_LOG_CHANNEL_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_STRING_VIEW Channel)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpCodec_ReadString(&Reader, Channel);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

NTSTATUS
ZpEventLog_EncodeChannelInfo(
    _In_ PCZP_EVENT_LOG_CHANNEL_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Info->Type > 3 || Info->RetentionMode > ZpEventLogRetentionManual ||
        !ZpEventLog_IsStringValid(Info->LogFilePath,
                                  Info->LogFilePathLength,
                                  ZP_CODEC_MAX_ELEMENT_COUNT,
                                  TRUE))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 3 * sizeof(BYTE) + 5 * sizeof(ULONGLONG) +
                   sizeof(ULONG) + (ULONGLONG)Info->LogFilePathLength * sizeof(WCHAR);
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteBoolean(&Writer, Info->Enabled);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, Info->Type);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, Info->RetentionMode);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->MaximumSize);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->FileSize);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->CreationTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->LastAccessTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->LastWriteTime);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Info->LogFilePath, Info->LogFilePathLength);
    }
    return Status;
}

NTSTATUS
ZpEventLog_DecodeChannelInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EVENT_LOG_CHANNEL_INFO_VIEW Info)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadBoolean(&Reader, &Info->Enabled);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Info->Type);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Info->RetentionMode);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Info->MaximumSize);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Info->FileSize);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Info->CreationTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Info->LastAccessTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Info->LastWriteTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Info->LogFilePath);
    if (!NT_SUCCESS(Status) || Info->Type > 3 || Info->RetentionMode > ZpEventLogRetentionManual ||
        Info->LogFilePath.Length == 0 || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpEventLog_EncodeConfigureChannelRequest(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ BOOLEAN Enabled,
    _In_ ZP_EVENT_LOG_RETENTION_MODE RetentionMode,
    _In_ ULONGLONG MaximumSize,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (!ZpEventLog_IsStringValid(ChannelPath,
                                  ChannelPathLength,
                                  ZP_CODEC_MAX_ELEMENT_COUNT,
                                  TRUE) ||
        RetentionMode > ZpEventLogRetentionManual || MaximumSize == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(ULONG) + (ULONGLONG)ChannelPathLength * sizeof(WCHAR) +
                    2 * sizeof(BYTE) + sizeof(ULONGLONG);
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteString(&Writer, ChannelPath, ChannelPathLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteBoolean(&Writer, Enabled);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, RetentionMode);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, MaximumSize);
    return Status;
}

NTSTATUS
ZpEventLog_DecodeConfigureChannelRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW ChannelPath,
    _Out_ PBOOLEAN Enabled,
    _Out_ PZP_EVENT_LOG_RETENTION_MODE RetentionMode,
    _Out_ PULONGLONG MaximumSize)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, ChannelPath);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadBoolean(&Reader, Enabled);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, RetentionMode);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, MaximumSize);
    if (!NT_SUCCESS(Status) || ChannelPath->Length == 0 ||
        *RetentionMode > ZpEventLogRetentionManual || *MaximumSize == 0 || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}
