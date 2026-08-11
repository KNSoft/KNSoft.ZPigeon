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
    DWORD ReturnedCount = 0, Error;
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
    QueryHandle = EvtQuery(NULL,
                           ChannelPath->Buffer,
                           QueryString != NULL ? QueryString->Buffer : NULL,
                           EvtQueryChannelPath | EvtQueryForwardDirection);
    if (QueryHandle == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    if (Query->StartMode == ZpEventLogStartAfterBookmark)
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
                     1,
                     BookmarkHandle,
                     0,
                     EvtSeekRelativeToBookmark | EvtSeekStrict))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
        EvtClose(BookmarkHandle);
        BookmarkHandle = NULL;
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
    return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
}
