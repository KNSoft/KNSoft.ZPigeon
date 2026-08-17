#include <KNSoft/MakeLifeEasier/Memory/Core.h>
#include <KNSoft/ZPigeon/Server.h>

typedef struct _ZP_SERVER_EVENT_LOG_QUERY_CONTEXT
{
    ZP_EVENT_LOG_QUERY_PAGE_CALLBACK Callback;
    PVOID Context;
} ZP_SERVER_EVENT_LOG_QUERY_CONTEXT,
  *PZP_SERVER_EVENT_LOG_QUERY_CONTEXT;

static
VOID
NTAPI
ZpServerEventLog_QueryComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_EVENT_LOG_QUERY_CONTEXT EventContext = Context;
    ZP_EVENT_LOG_PAGE_VIEW Page;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpEventLog_DecodePage(Payload->Buffer,
                                  Payload->Length,
                                  &Page));
    }
    EventContext->Callback(Request,
                           Status,
                           ZpStatus_IsSuccess(Status) ? &Page : NULL,
                           EventContext->Context);
    Mem_Free(EventContext);
}

NTSTATUS
NTAPI
ZpServer_QueryEventLogPage(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_EVENT_LOG_START_MODE StartMode,
    _In_ ULONG MaxEvents,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_reads_opt_(BookmarkLength) PCWCH Bookmark,
    _In_ ULONG BookmarkLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EVENT_LOG_QUERY_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_EVENT_LOG_QUERY_CONTEXT EventContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpEventLog_EncodeQueryPageRequest(StartMode,
                                               MaxEvents,
                                               ChannelPath,
                                               ChannelPathLength,
                                               Query,
                                               QueryLength,
                                               Bookmark,
                                               BookmarkLength,
                                               NULL,
                                               0,
                                               &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpEventLog_EncodeQueryPageRequest(StartMode,
                                                   MaxEvents,
                                                   ChannelPath,
                                                   ChannelPathLength,
                                                   Query,
                                                   QueryLength,
                                                   Bookmark,
                                                   BookmarkLength,
                                                   Payload,
                                                   PayloadLength,
                                                   &PayloadLength);
    }
    EventContext = NT_SUCCESS(Status) ?
                       Mem_Alloc(sizeof(*EventContext)) : NULL;
    if (NT_SUCCESS(Status) && EventContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        EventContext->Callback = Callback;
        EventContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_EVENT_LOG_MODULE_ID,
                                      ZP_EVENT_LOG_OPERATION_QUERY_PAGE,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpServerEventLog_QueryComplete,
                                      EventContext,
                                      Request);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(EventContext);
        }
    }
    Mem_Free(Payload);
    return Status;
}

typedef struct _ZP_SERVER_EVENT_LOG_CHANNELS_CONTEXT
{
    ZP_EVENT_LOG_CHANNELS_CALLBACK Callback;
    PVOID Context;
} ZP_SERVER_EVENT_LOG_CHANNELS_CONTEXT, *PZP_SERVER_EVENT_LOG_CHANNELS_CONTEXT;

static
VOID
NTAPI
ZpServerEventLog_ChannelsComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_EVENT_LOG_CHANNELS_CONTEXT EventContext = Context;
    ZP_EVENT_LOG_CHANNEL_LIST_VIEW Channels;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpEventLog_DecodeChannels(Payload->Buffer,
                                                                 Payload->Length,
                                                                 &Channels));
    }
    EventContext->Callback(Request,
                           Status,
                           ZpStatus_IsSuccess(Status) ? &Channels : NULL,
                           EventContext->Context);
    Mem_Free(EventContext);
}

NTSTATUS
NTAPI
ZpServer_EnumerateEventLogChannels(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EVENT_LOG_CHANNELS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_EVENT_LOG_CHANNELS_CONTEXT EventContext;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    EventContext = Mem_Alloc(sizeof(*EventContext));
    if (EventContext == NULL) return STATUS_NO_MEMORY;
    EventContext->Callback = Callback;
    EventContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_EVENT_LOG_MODULE_ID,
                                  ZP_EVENT_LOG_OPERATION_ENUMERATE_CHANNELS,
                                  TimeoutMilliseconds,
                                  NULL,
                                  0,
                                  ZpServerEventLog_ChannelsComplete,
                                  EventContext,
                                  Request);
    if (!NT_SUCCESS(Status)) Mem_Free(EventContext);
    return Status;
}

typedef struct _ZP_SERVER_EVENT_LOG_INFO_CONTEXT
{
    ZP_EVENT_LOG_CHANNEL_INFO_CALLBACK Callback;
    PVOID Context;
} ZP_SERVER_EVENT_LOG_INFO_CONTEXT, *PZP_SERVER_EVENT_LOG_INFO_CONTEXT;

static
VOID
NTAPI
ZpServerEventLog_InfoComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_EVENT_LOG_INFO_CONTEXT EventContext = Context;
    ZP_EVENT_LOG_CHANNEL_INFO_VIEW Info;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpEventLog_DecodeChannelInfo(Payload->Buffer,
                                                                    Payload->Length,
                                                                    &Info));
    }
    EventContext->Callback(Request,
                           Status,
                           ZpStatus_IsSuccess(Status) ? &Info : NULL,
                           EventContext->Context);
    Mem_Free(EventContext);
}

NTSTATUS
NTAPI
ZpServer_QueryEventLogChannelInfo(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EVENT_LOG_CHANNEL_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_EVENT_LOG_INFO_CONTEXT EventContext;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpEventLog_EncodeClearRequest(ChannelPath,
                                           ChannelPathLength,
                                           NULL,
                                           0,
                                           &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpEventLog_EncodeClearRequest(ChannelPath,
                                               ChannelPathLength,
                                               Payload,
                                               PayloadLength,
                                               &PayloadLength);
    }
    EventContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*EventContext)) : NULL;
    if (NT_SUCCESS(Status) && EventContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        EventContext->Callback = Callback;
        EventContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_EVENT_LOG_MODULE_ID,
                                      ZP_EVENT_LOG_OPERATION_QUERY_CHANNEL_INFO,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpServerEventLog_InfoComplete,
                                      EventContext,
                                      Request);
        if (!NT_SUCCESS(Status)) Mem_Free(EventContext);
    }
    Mem_Free(Payload);
    return Status;
}

typedef struct _ZP_SERVER_EVENT_LOG_STATUS_CONTEXT
{
    ZP_REQUEST_STATUS_CALLBACK Callback;
    PVOID Context;
} ZP_SERVER_EVENT_LOG_STATUS_CONTEXT,
  *PZP_SERVER_EVENT_LOG_STATUS_CONTEXT;

static
VOID
NTAPI
ZpServerEventLog_StatusComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_EVENT_LOG_STATUS_CONTEXT EventContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    EventContext->Callback(Request, Status, EventContext->Context);
    Mem_Free(EventContext);
}

static
NTSTATUS
ZpServerEventLog_SendControl(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ USHORT OperationId,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ BOOLEAN Enabled,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_EVENT_LOG_STATUS_CONTEXT EventContext;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = OperationId == ZP_EVENT_LOG_OPERATION_SET_CHANNEL_ENABLED ?
                 ZpEventLog_EncodeSetChannelEnabledRequest(
                     ChannelPath,
                     ChannelPathLength,
                     Enabled,
                     NULL,
                     0,
                     &PayloadLength) :
                 ZpEventLog_EncodeClearRequest(ChannelPath,
                                               ChannelPathLength,
                                               NULL,
                                               0,
                                               &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = OperationId == ZP_EVENT_LOG_OPERATION_SET_CHANNEL_ENABLED ?
                     ZpEventLog_EncodeSetChannelEnabledRequest(
                         ChannelPath,
                         ChannelPathLength,
                         Enabled,
                         Payload,
                         PayloadLength,
                         &PayloadLength) :
                     ZpEventLog_EncodeClearRequest(ChannelPath,
                                                   ChannelPathLength,
                                                   Payload,
                                                   PayloadLength,
                                                   &PayloadLength);
    }
    EventContext = NT_SUCCESS(Status) ?
                       Mem_Alloc(sizeof(*EventContext)) : NULL;
    if (NT_SUCCESS(Status) && EventContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        EventContext->Callback = Callback;
        EventContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_EVENT_LOG_MODULE_ID,
                                      OperationId,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpServerEventLog_StatusComplete,
                                      EventContext,
                                      Request);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(EventContext);
        }
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_SetEventLogChannelEnabled(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ BOOLEAN Enabled,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpServerEventLog_SendControl(
        Connection,
        ZP_EVENT_LOG_OPERATION_SET_CHANNEL_ENABLED,
        ChannelPath,
        ChannelPathLength,
        Enabled,
        TimeoutMilliseconds,
        Callback,
        Context,
        Request);
}

NTSTATUS
NTAPI
ZpServer_ClearEventLog(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpServerEventLog_SendControl(Connection,
                                        ZP_EVENT_LOG_OPERATION_CLEAR,
                                        ChannelPath,
                                        ChannelPathLength,
                                        FALSE,
                                        TimeoutMilliseconds,
                                        Callback,
                                        Context,
                                        Request);
}

NTSTATUS
NTAPI
ZpServer_ConfigureEventLogChannel(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ BOOLEAN Enabled,
    _In_ ZP_EVENT_LOG_RETENTION_MODE RetentionMode,
    _In_ ULONGLONG MaximumSize,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_EVENT_LOG_STATUS_CONTEXT EventContext;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpEventLog_EncodeConfigureChannelRequest(ChannelPath,
                                                       ChannelPathLength,
                                                       Enabled,
                                                       RetentionMode,
                                                       MaximumSize,
                                                       NULL,
                                                       0,
                                                       &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpEventLog_EncodeConfigureChannelRequest(ChannelPath,
                                                           ChannelPathLength,
                                                           Enabled,
                                                           RetentionMode,
                                                           MaximumSize,
                                                           Payload,
                                                           PayloadLength,
                                                           &PayloadLength);
    }
    EventContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*EventContext)) : NULL;
    if (NT_SUCCESS(Status) && EventContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        EventContext->Callback = Callback;
        EventContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_EVENT_LOG_MODULE_ID,
                                      ZP_EVENT_LOG_OPERATION_CONFIGURE_CHANNEL,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpServerEventLog_StatusComplete,
                                      EventContext,
                                      Request);
        if (!NT_SUCCESS(Status)) Mem_Free(EventContext);
    }
    Mem_Free(Payload);
    return Status;
}
