#include "Client.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <winevt.h>

#pragma comment(lib, "Wevtapi.lib")

static
PUNICODE_STRING
ZpEventLog_CopyString(
    _In_ PCZP_STRING_VIEW View)
{
    PUNICODE_STRING String;

    if (View->Length > MAXUSHORT / sizeof(WCHAR))
    {
        return NULL;
    }
    String = NT_AllocStringW((USHORT)View->Length);
    if (String != NULL)
    {
        RtlCopyMemory(String->Buffer,
                      View->Buffer,
                      (SIZE_T)View->Length * sizeof(WCHAR));
        String->Buffer[View->Length] = UNICODE_NULL;
    }
    return String;
}

static
ZP_STATUS
ZpEventLog_RenderString(
    _In_opt_ EVT_HANDLE Context,
    _In_ EVT_HANDLE Fragment,
    _In_ DWORD Flags,
    _In_ ULONG MaximumLength,
    _Outptr_result_buffer_(*Length) PWCHAR* String,
    _Out_ PULONG Length)
{
    PWCHAR Buffer;
    DWORD BufferUsed = 0, PropertyCount = 0, Error;

    if (EvtRender(Context,
                  Fragment,
                  Flags,
                  0,
                  NULL,
                  &BufferUsed,
                  &PropertyCount))
    {
        return ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    Error = GetLastError();
    if (Error != ERROR_INSUFFICIENT_BUFFER)
    {
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    if (BufferUsed < sizeof(WCHAR) ||
        BufferUsed % sizeof(WCHAR) != 0 ||
        BufferUsed / sizeof(WCHAR) - 1 > MaximumLength)
    {
        return ZpStatus_FromNtStatus(STATUS_BUFFER_OVERFLOW);
    }
    Buffer = Mem_Alloc(BufferUsed);
    if (Buffer == NULL)
    {
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    if (!EvtRender(Context,
                   Fragment,
                   Flags,
                   BufferUsed,
                   Buffer,
                   &BufferUsed,
                   &PropertyCount))
    {
        Error = GetLastError();
        Mem_Free(Buffer);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    if (BufferUsed < sizeof(WCHAR) ||
        BufferUsed % sizeof(WCHAR) != 0 ||
        Buffer[BufferUsed / sizeof(WCHAR) - 1] != UNICODE_NULL)
    {
        Mem_Free(Buffer);
        return ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    *String = Buffer;
    *Length = BufferUsed / sizeof(WCHAR) - 1;
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ZP_STATUS
ZpEventLog_GetChannelProperty(
    _In_ EVT_HANDLE Config,
    _In_ EVT_CHANNEL_CONFIG_PROPERTY_ID PropertyId,
    _In_ DWORD Type,
    _Out_ PEVT_VARIANT Value)
{
    DWORD Used;

    if (!EvtGetChannelConfigProperty(Config, PropertyId, 0, sizeof(*Value), Value, &Used))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    return Value->Type == Type ?
               ZpStatus_FromNtStatus(STATUS_SUCCESS) :
               ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
}

static
ZP_STATUS
ZpEventLog_GetChannelStringProperty(
    _In_ EVT_HANDLE Config,
    _In_ EVT_CHANNEL_CONFIG_PROPERTY_ID PropertyId,
    _Outptr_ PEVT_VARIANT* Value)
{
    PEVT_VARIANT Buffer;
    DWORD Used, Error;

    if (EvtGetChannelConfigProperty(Config, PropertyId, 0, 0, NULL, &Used))
    {
        return ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    Error = GetLastError();
    if (Error != ERROR_INSUFFICIENT_BUFFER)
    {
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    Buffer = Mem_Alloc(Used);
    if (Buffer == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    if (!EvtGetChannelConfigProperty(Config, PropertyId, 0, Used, Buffer, &Used))
    {
        Error = GetLastError();
        Mem_Free(Buffer);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    if (Buffer->Type != EvtVarTypeString || Buffer->StringVal == NULL)
    {
        Mem_Free(Buffer);
        return ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    *Value = Buffer;
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ZP_STATUS
ZpEventLog_GetQueryDirection(
    _In_ PCWSTR ChannelPath,
    _Out_ PDWORD Direction)
{
    EVT_HANDLE Config;
    EVT_VARIANT Value;
    ZP_STATUS Status;

    Config = EvtOpenChannelConfig(NULL, ChannelPath, 0);
    if (Config == NULL) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    Status = ZpEventLog_GetChannelProperty(Config, EvtChannelConfigType, EvtVarTypeUInt32, &Value);
    EvtClose(Config);
    if (ZpStatus_IsSuccess(Status))
    {
        *Direction = Value.UInt32Val == EvtChannelTypeAnalytic || Value.UInt32Val == EvtChannelTypeDebug ?
                         EvtQueryForwardDirection :
                         EvtQueryReverseDirection;
    }
    return Status;
}

static
ZP_STATUS
ZpEventLog_QueryPage(
    _In_ const ZP_EVENT_LOG_QUERY_VIEW* Query,
    _In_ volatile LONG* Pending,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    EVT_HANDLE QueryHandle = NULL, BookmarkHandle = NULL;
    EVT_HANDLE Events[ZP_EVENT_LOG_PAGE_MAX_COUNT + 1] = { 0 };
    ZP_EVENT_LOG_RECORD Records[ZP_EVENT_LOG_PAGE_MAX_COUNT] = { 0 };
    PUNICODE_STRING ChannelPath = NULL, QueryString = NULL;
    PUNICODE_STRING BookmarkString = NULL;
    PCWCH NextBookmark;
    ULONG NextBookmarkLength;
    DWORD ReturnedCount = 0, Error, Direction;
    ULONG RecordCount = 0, TargetCount, Index;
    ULONGLONG EncodedRecordsLength = 0, CandidateLength;
    BOOLEAN HasMore;
    NTSTATUS CodecStatus;
    ZP_STATUS Status = { 0 };

    ChannelPath = ZpEventLog_CopyString(&Query->ChannelPath);
    if (ChannelPath == NULL)
    {
        Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto Cleanup;
    }
    if (Query->Query.Length != 0)
    {
        QueryString = ZpEventLog_CopyString(&Query->Query);
        if (QueryString == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            goto Cleanup;
        }
    }
    if (Query->StartMode == ZpEventLogStartAfterBookmarkForward ||
        Query->StartMode == ZpEventLogStartForward)
    {
        Direction = EvtQueryForwardDirection;
    }
    else
    {
        Status = ZpEventLog_GetQueryDirection(ChannelPath->Buffer, &Direction);
        if (!ZpStatus_IsSuccess(Status)) goto Cleanup;
    }
    QueryHandle = EvtQuery(NULL,
                           ChannelPath->Buffer,
                           QueryString != NULL ? QueryString->Buffer : NULL,
                           EvtQueryChannelPath | Direction);
    if (QueryHandle == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    if (Query->StartMode == ZpEventLogStartAfterBookmark ||
        Query->StartMode == ZpEventLogStartAfterBookmarkForward)
    {
        BookmarkString = ZpEventLog_CopyString(&Query->Bookmark);
        if (BookmarkString == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            goto Cleanup;
        }
        BookmarkHandle = EvtCreateBookmark(BookmarkString->Buffer);
        if (BookmarkHandle == NULL)
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
        if (!EvtSeek(QueryHandle,
                     Query->StartMode == ZpEventLogStartAfterBookmarkForward ? 0 : 1,
                     BookmarkHandle,
                     0,
                     EvtSeekRelativeToBookmark | EvtSeekStrict))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
        EvtClose(BookmarkHandle);
        BookmarkHandle = NULL;
        if (Query->StartMode == ZpEventLogStartAfterBookmarkForward)
        {
            if (!EvtNext(QueryHandle,
                         1,
                         Events,
                         INFINITE,
                         0,
                         &ReturnedCount) || ReturnedCount != 1)
            {
                Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
                goto Cleanup;
            }
            EvtClose(Events[0]);
            Events[0] = NULL;
            ReturnedCount = 0;
        }
    }
    if (!InterlockedCompareExchange(Pending, TRUE, TRUE))
    {
        Status = ZpStatus_FromNtStatus(STATUS_CANCELLED);
        goto Cleanup;
    }
    if (!EvtNext(QueryHandle,
                 Query->MaxEvents + 1,
                 Events,
                 INFINITE,
                 0,
                 &ReturnedCount))
    {
        Error = GetLastError();
        if (Error != ERROR_NO_MORE_ITEMS && Error != ERROR_TIMEOUT)
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, Error);
            goto Cleanup;
        }
    }
    HasMore = ReturnedCount > Query->MaxEvents;
    TargetCount = min(ReturnedCount, Query->MaxEvents);
    if (TargetCount != 0)
    {
        BookmarkHandle = EvtCreateBookmark(NULL);
        if (BookmarkHandle == NULL)
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
    }
    for (Index = 0; Index < TargetCount; Index++)
    {
        if (!InterlockedCompareExchange(Pending, TRUE, TRUE))
        {
            Status = ZpStatus_FromNtStatus(STATUS_CANCELLED);
            goto Cleanup;
        }
        if (!EvtUpdateBookmark(BookmarkHandle, Events[Index]))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
        Status = ZpEventLog_RenderString(
            NULL,
            BookmarkHandle,
            EvtRenderBookmark,
            ZP_EVENT_LOG_BOOKMARK_MAX_LENGTH,
            (PWCHAR*)&Records[Index].Bookmark,
            &Records[Index].BookmarkLength);
        if (ZpStatus_IsSuccess(Status))
        {
            Status = ZpEventLog_RenderString(
                NULL,
                Events[Index],
                EvtRenderEventXml,
                ZP_EVENT_LOG_XML_MAX_LENGTH,
                (PWCHAR*)&Records[Index].Xml,
                &Records[Index].XmlLength);
        }
        if (!ZpStatus_IsSuccess(Status))
        {
            goto Cleanup;
        }
        CandidateLength = sizeof(BYTE) + 2 * sizeof(ULONG) +
                          EncodedRecordsLength + 2 * sizeof(ULONG) +
                          ((ULONGLONG)Records[Index].BookmarkLength * 2 +
                           Records[Index].XmlLength) * sizeof(WCHAR);
        if (CandidateLength > ZP_FRAME_MAX_BODY_SIZE - 12)
        {
            Mem_Free((PVOID)Records[Index].Xml);
            Mem_Free((PVOID)Records[Index].Bookmark);
            Records[Index].Xml = NULL;
            Records[Index].Bookmark = NULL;
            if (RecordCount == 0)
            {
                Status = ZpStatus_FromNtStatus(STATUS_BUFFER_OVERFLOW);
                goto Cleanup;
            }
            HasMore = TRUE;
            break;
        }
        EncodedRecordsLength += 2 * sizeof(ULONG) +
                                ((ULONGLONG)Records[Index].BookmarkLength +
                                 Records[Index].XmlLength) * sizeof(WCHAR);
        RecordCount++;
    }
    if (RecordCount != 0)
    {
        NextBookmark = Records[RecordCount - 1].Bookmark;
        NextBookmarkLength = Records[RecordCount - 1].BookmarkLength;
    }
    else
    {
        NextBookmark = (PCWCH)Query->Bookmark.Buffer;
        NextBookmarkLength = Query->Bookmark.Length;
    }
    CodecStatus = ZpEventLog_EncodePage(HasMore,
                                        Records,
                                        RecordCount,
                                        NextBookmark,
                                        NextBookmarkLength,
                                        NULL,
                                        0,
                                        ResponseLength);
    *Response = NT_SUCCESS(CodecStatus) ? Mem_Alloc(*ResponseLength) : NULL;
    if (NT_SUCCESS(CodecStatus) && *Response == NULL)
    {
        CodecStatus = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(CodecStatus))
    {
        CodecStatus = ZpEventLog_EncodePage(HasMore,
                                            Records,
                                            RecordCount,
                                            NextBookmark,
                                            NextBookmarkLength,
                                            *Response,
                                            *ResponseLength,
                                            ResponseLength);
    }
    Status = ZpStatus_FromNtStatus(CodecStatus);

Cleanup:
    for (Index = 0; Index < ARRAYSIZE(Events); Index++)
    {
        if (Events[Index] != NULL)
        {
            EvtClose(Events[Index]);
        }
    }
    for (Index = 0; Index < ARRAYSIZE(Records); Index++)
    {
        Mem_Free((PVOID)Records[Index].Xml);
        Mem_Free((PVOID)Records[Index].Bookmark);
    }
    if (BookmarkHandle != NULL)
    {
        EvtClose(BookmarkHandle);
    }
    if (QueryHandle != NULL)
    {
        EvtClose(QueryHandle);
    }
    if (BookmarkString != NULL)
    {
        NT_FreeStringW(BookmarkString);
    }
    if (QueryString != NULL)
    {
        NT_FreeStringW(QueryString);
    }
    if (ChannelPath != NULL)
    {
        NT_FreeStringW(ChannelPath);
    }
    if (!ZpStatus_IsSuccess(Status))
    {
        Mem_Free(*Response);
        *Response = NULL;
        *ResponseLength = 0;
    }
    return Status;
}

static
ZP_STATUS
ZpEventLog_QueryChannelInfo(
    _In_ PCZP_STRING_VIEW ChannelPath,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    EVT_HANDLE Config = NULL, Log = NULL;
    EVT_VARIANT Value, LogValue;
    PEVT_VARIANT PathValue = NULL;
    PUNICODE_STRING Path;
    PBYTE Output = NULL;
    ZP_EVENT_LOG_CHANNEL_INFO Info;
    DWORD Used;
    NTSTATUS CodecStatus;
    ZP_STATUS Status;

    Path = ZpEventLog_CopyString(ChannelPath);
    if (Path == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Config = EvtOpenChannelConfig(NULL, Path->Buffer, 0);
    if (Config == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
#define ZP_EVENT_LOG_GET_CONFIG(Property, Type) \
    Status = ZpEventLog_GetChannelProperty(Config, Property, Type, &Value); \
    if (!ZpStatus_IsSuccess(Status)) goto Cleanup
    ZP_EVENT_LOG_GET_CONFIG(EvtChannelConfigEnabled, EvtVarTypeBoolean);
    Info.Enabled = Value.BooleanVal;
    ZP_EVENT_LOG_GET_CONFIG(EvtChannelConfigType, EvtVarTypeUInt32);
    Info.Type = Value.UInt32Val;
    ZP_EVENT_LOG_GET_CONFIG(EvtChannelLoggingConfigRetention, EvtVarTypeBoolean);
    Info.RetentionMode = Value.BooleanVal ? ZpEventLogRetentionManual : ZpEventLogRetentionOverwrite;
    ZP_EVENT_LOG_GET_CONFIG(EvtChannelLoggingConfigAutoBackup, EvtVarTypeBoolean);
    if (Value.BooleanVal) Info.RetentionMode = ZpEventLogRetentionArchive;
    ZP_EVENT_LOG_GET_CONFIG(EvtChannelLoggingConfigMaxSize, EvtVarTypeUInt64);
    Info.MaximumSize = Value.UInt64Val;
#undef ZP_EVENT_LOG_GET_CONFIG
    Status = ZpEventLog_GetChannelStringProperty(Config,
                                                  EvtChannelLoggingConfigLogFilePath,
                                                  &PathValue);
    if (!ZpStatus_IsSuccess(Status)) goto Cleanup;
    Info.LogFilePath = PathValue->StringVal;
    Info.LogFilePathLength = (ULONG)wcslen(Info.LogFilePath);
    Info.FileSize = Info.CreationTime = Info.LastAccessTime = Info.LastWriteTime = 0;
    Log = EvtOpenLog(NULL, Path->Buffer, EvtOpenChannelPath);
    if (Log != NULL)
    {
#define ZP_EVENT_LOG_GET_LOG(Property, ExpectedType, Field, ValueField) \
        if (EvtGetLogInfo(Log, Property, sizeof(LogValue), &LogValue, &Used) && \
            LogValue.Type == ExpectedType) \
        { \
            Info.Field = LogValue.ValueField; \
        }
        ZP_EVENT_LOG_GET_LOG(EvtLogFileSize, EvtVarTypeUInt64, FileSize, UInt64Val);
        ZP_EVENT_LOG_GET_LOG(EvtLogCreationTime, EvtVarTypeFileTime, CreationTime, FileTimeVal);
        ZP_EVENT_LOG_GET_LOG(EvtLogLastAccessTime, EvtVarTypeFileTime, LastAccessTime, FileTimeVal);
        ZP_EVENT_LOG_GET_LOG(EvtLogLastWriteTime, EvtVarTypeFileTime, LastWriteTime, FileTimeVal);
#undef ZP_EVENT_LOG_GET_LOG
    }
    CodecStatus = ZpEventLog_EncodeChannelInfo(&Info, NULL, 0, ResponseLength);
    Output = NT_SUCCESS(CodecStatus) ? Mem_Alloc(*ResponseLength) : NULL;
    if (NT_SUCCESS(CodecStatus) && Output == NULL) CodecStatus = STATUS_NO_MEMORY;
    if (NT_SUCCESS(CodecStatus))
    {
        CodecStatus = ZpEventLog_EncodeChannelInfo(&Info,
                                                   Output,
                                                   *ResponseLength,
                                                   ResponseLength);
    }
    Status = ZpStatus_FromNtStatus(CodecStatus);

Cleanup:
    if (Log != NULL) EvtClose(Log);
    Mem_Free(PathValue);
    if (Config != NULL) EvtClose(Config);
    NT_FreeStringW(Path);
    if (!ZpStatus_IsSuccess(Status))
    {
        Mem_Free(Output);
    }
    else *Response = Output;
    return Status;
}

static
ZP_STATUS
ZpEventLog_ConfigureChannel(
    _In_ PCZP_STRING_VIEW ChannelPath,
    _In_ BOOLEAN Enabled,
    _In_ ZP_EVENT_LOG_RETENTION_MODE RetentionMode,
    _In_ ULONGLONG MaximumSize)
{
    EVT_HANDLE Config;
    EVT_VARIANT Value = { 0 };
    PUNICODE_STRING Path;
    ZP_STATUS Status;

    Path = ZpEventLog_CopyString(ChannelPath);
    if (Path == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Config = EvtOpenChannelConfig(NULL, Path->Buffer, 0);
    NT_FreeStringW(Path);
    if (Config == NULL) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    Value.Type = EvtVarTypeBoolean;
    Value.BooleanVal = Enabled;
    if (!EvtSetChannelConfigProperty(Config, EvtChannelConfigEnabled, 0, &Value)) goto Error;
    Value.Type = EvtVarTypeUInt64;
    Value.UInt64Val = MaximumSize;
    if (!EvtSetChannelConfigProperty(Config, EvtChannelLoggingConfigMaxSize, 0, &Value)) goto Error;
    Value.Type = EvtVarTypeBoolean;
    Value.BooleanVal = RetentionMode != ZpEventLogRetentionOverwrite;
    if (!EvtSetChannelConfigProperty(Config, EvtChannelLoggingConfigRetention, 0, &Value)) goto Error;
    Value.BooleanVal = RetentionMode == ZpEventLogRetentionArchive;
    if (!EvtSetChannelConfigProperty(Config, EvtChannelLoggingConfigAutoBackup, 0, &Value) ||
        !EvtSaveChannelConfig(Config, 0))
    {
        goto Error;
    }
    Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    EvtClose(Config);
    return Status;

Error:
    Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    EvtClose(Config);
    return Status;
}

static
ZP_STATUS
ZpEventLog_SetChannelEnabled(
    _In_ PCZP_STRING_VIEW ChannelPath,
    _In_ BOOLEAN Enabled)
{
    EVT_HANDLE Config;
    EVT_VARIANT Value = { 0 };
    PUNICODE_STRING Path;
    ZP_STATUS Status;

    Path = ZpEventLog_CopyString(ChannelPath);
    if (Path == NULL)
    {
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    Config = EvtOpenChannelConfig(NULL, Path->Buffer, 0);
    NT_FreeStringW(Path);
    if (Config == NULL)
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    Value.BooleanVal = Enabled;
    Value.Type = EvtVarTypeBoolean;
    if (!EvtSetChannelConfigProperty(Config,
                                     EvtChannelConfigEnabled,
                                     0,
                                     &Value) ||
        !EvtSaveChannelConfig(Config, 0))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    else
    {
        Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }
    EvtClose(Config);
    return Status;
}

static
ZP_STATUS
ZpEventLog_Clear(
    _In_ PCZP_STRING_VIEW ChannelPath)
{
    PUNICODE_STRING Path;
    ZP_STATUS Status;

    Path = ZpEventLog_CopyString(ChannelPath);
    if (Path == NULL)
    {
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    Status = EvtClearLog(NULL, Path->Buffer, NULL, 0) ?
                 ZpStatus_FromNtStatus(STATUS_SUCCESS) :
                 ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    NT_FreeStringW(Path);
    return Status;
}

static
ZP_STATUS
ZpEventLog_EnumerateChannels(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    EVT_HANDLE Enumerator;
    ZP_STRING_VIEW* Channels = NULL;
    ZP_STRING_VIEW* Values;
    PWSTR Buffer = NULL;
    PBYTE Output = NULL;
    DWORD Error = ERROR_SUCCESS, Required;
    ULONG Count = 0, Index;
    NTSTATUS Status = STATUS_SUCCESS;

    Enumerator = EvtOpenChannelEnum(NULL, 0);
    if (Enumerator == NULL) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    for (;;)
    {
        Required = 0;
        if (EvtNextChannelPath(Enumerator, 0, NULL, &Required))
        {
            Status = STATUS_DATA_ERROR;
            break;
        }
        Error = GetLastError();
        if (Error == ERROR_NO_MORE_ITEMS)
        {
            Error = ERROR_SUCCESS;
            break;
        }
        if (Error != ERROR_INSUFFICIENT_BUFFER) break;
        Buffer = Mem_Alloc((SIZE_T)Required * sizeof(WCHAR));
        if (Buffer == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        if (!EvtNextChannelPath(Enumerator, Required, Buffer, &Required))
        {
            Error = GetLastError();
            break;
        }
        Error = ERROR_SUCCESS;
        if (Required <= 1)
        {
            Status = STATUS_DATA_ERROR;
            break;
        }
        if (Count == ZP_EVENT_LOG_CHANNEL_MAX_COUNT)
        {
            Status = STATUS_QUOTA_EXCEEDED;
            break;
        }
        Values = Mem_ReAlloc(Channels, ((SIZE_T)Count + 1) * sizeof(*Values));
        if (Values == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        Channels = Values;
        Channels[Count].Buffer = (const BYTE*)Buffer;
        Channels[Count++].Length = Required - 1;
        Buffer = NULL;
    }
    if (Error == ERROR_SUCCESS && NT_SUCCESS(Status))
    {
        Status = ZpEventLog_EncodeChannels(Channels, Count, NULL, 0, ResponseLength);
        Output = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
        if (NT_SUCCESS(Status) && Output == NULL) Status = STATUS_NO_MEMORY;
        if (NT_SUCCESS(Status))
        {
            Status = ZpEventLog_EncodeChannels(Channels,
                                               Count,
                                               Output,
                                               *ResponseLength,
                                               ResponseLength);
        }
    }
    Mem_Free(Buffer);
    for (Index = 0; Index < Count; Index++) Mem_Free((PVOID)Channels[Index].Buffer);
    Mem_Free(Channels);
    EvtClose(Enumerator);
    if (Error == ERROR_SUCCESS && NT_SUCCESS(Status)) *Response = Output;
    else Mem_Free(Output);
    return Error == ERROR_SUCCESS ?
               ZpStatus_FromNtStatus(Status) :
               ZpStatus_FromCode(ZpStatusWin32, Error);
}

ZP_STATUS
ZpEventLog_Execute(
    _In_ USHORT OperationId,
    _In_reads_bytes_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _In_ volatile LONG* Pending,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_EVENT_LOG_QUERY_VIEW Query;
    ZP_STRING_VIEW ChannelPath;
    BOOLEAN Enabled;
    ZP_EVENT_LOG_RETENTION_MODE RetentionMode;
    ULONGLONG MaximumSize;
    NTSTATUS Status;

    if (OperationId == ZP_EVENT_LOG_OPERATION_QUERY_PAGE)
    {
        Status = ZpEventLog_DecodeQueryPageRequest(Request,
                                                   RequestLength,
                                                   &Query);
        return NT_SUCCESS(Status) ?
                   ZpEventLog_QueryPage(&Query,
                                        Pending,
                                        Response,
                                        ResponseLength) :
                   ZpStatus_FromNtStatus(Status);
    }
    if (!InterlockedCompareExchange(Pending, TRUE, TRUE))
    {
        return ZpStatus_FromNtStatus(STATUS_CANCELLED);
    }
    if (OperationId == ZP_EVENT_LOG_OPERATION_ENUMERATE_CHANNELS)
    {
        return RequestLength == 0 ?
                   ZpEventLog_EnumerateChannels(Response, ResponseLength) :
                   ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    if (OperationId == ZP_EVENT_LOG_OPERATION_QUERY_CHANNEL_INFO)
    {
        Status = ZpEventLog_DecodeClearRequest(Request, RequestLength, &ChannelPath);
        return NT_SUCCESS(Status) ?
                   ZpEventLog_QueryChannelInfo(&ChannelPath, Response, ResponseLength) :
                   ZpStatus_FromNtStatus(Status);
    }
    if (OperationId == ZP_EVENT_LOG_OPERATION_SET_CHANNEL_ENABLED)
    {
        Status = ZpEventLog_DecodeSetChannelEnabledRequest(Request,
                                                           RequestLength,
                                                           &ChannelPath,
                                                           &Enabled);
        return NT_SUCCESS(Status) ?
                   ZpEventLog_SetChannelEnabled(&ChannelPath, Enabled) :
                   ZpStatus_FromNtStatus(Status);
    }
    if (OperationId == ZP_EVENT_LOG_OPERATION_CLEAR)
    {
        Status = ZpEventLog_DecodeClearRequest(Request,
                                               RequestLength,
                                               &ChannelPath);
        return NT_SUCCESS(Status) ?
                   ZpEventLog_Clear(&ChannelPath) :
                   ZpStatus_FromNtStatus(Status);
    }
    if (OperationId == ZP_EVENT_LOG_OPERATION_CONFIGURE_CHANNEL)
    {
        Status = ZpEventLog_DecodeConfigureChannelRequest(Request,
                                                          RequestLength,
                                                          &ChannelPath,
                                                          &Enabled,
                                                          &RetentionMode,
                                                          &MaximumSize);
        return NT_SUCCESS(Status) ?
                   ZpEventLog_ConfigureChannel(&ChannelPath, Enabled, RetentionMode, MaximumSize) :
                   ZpStatus_FromNtStatus(Status);
    }
    return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
}
