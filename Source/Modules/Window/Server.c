#include <KNSoft/ZPigeon/Server.h>

#include "../../KNSoft.ZPigeon.Server.SDK/Core/Channel.h"
#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef union _ZP_WINDOW_CALLBACK
{
    ZP_WINDOW_ENUMERATE_CALLBACK Enumerate;
    ZP_WINDOW_MONITOR_ENUMERATE_CALLBACK EnumerateMonitors;
    ZP_WINDOW_QUERY_CALLBACK Query;
    ZP_WINDOW_CAPTURE_CALLBACK Capture;
    ZP_REQUEST_STATUS_CALLBACK Status;
} ZP_WINDOW_CALLBACK;

typedef struct _ZP_WINDOW_CONTEXT
{
    ZP_WINDOW_CALLBACK Callback;
    PVOID Context;
} ZP_WINDOW_CONTEXT, *PZP_WINDOW_CONTEXT;

typedef struct _ZP_WINDOW_CAPTURE_CONTEXT
{
    ZP_CONNECTION_HANDLE Connection;
    ZP_WINDOW_CAPTURE_OPEN_CALLBACK OpenCallback;
    ZP_CHANNEL_DATA_CALLBACK DataCallback;
    ZP_CHANNEL_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_WINDOW_CAPTURE_CONTEXT, *PZP_WINDOW_CAPTURE_CONTEXT;

static
VOID
NTAPI
ZpWindow_EnumerateComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_WINDOW_CONTEXT WindowContext = Context;
    ZP_WINDOW_LIST_VIEW Windows;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpWindow_DecodeList(Payload->Buffer, Payload->Length, &Windows));
    }
    WindowContext->Callback.Enumerate(Request,
                                      Status,
                                      ZpStatus_IsSuccess(Status) ? &Windows : NULL,
                                      WindowContext->Context);
    Mem_Free(WindowContext);
}

static
VOID
NTAPI
ZpWindow_EnumerateMonitorsComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_WINDOW_CONTEXT WindowContext = Context;
    ZP_WINDOW_MONITOR_LIST_VIEW Monitors;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpWindow_DecodeMonitorList(Payload->Buffer, Payload->Length, &Monitors));
    }
    WindowContext->Callback.EnumerateMonitors(Request,
                                              Status,
                                              ZpStatus_IsSuccess(Status) ? &Monitors : NULL,
                                              WindowContext->Context);
    Mem_Free(WindowContext);
}

static
VOID
NTAPI
ZpWindow_QueryComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_WINDOW_CONTEXT WindowContext = Context;
    ZP_WINDOW_INFO_VIEW Info;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpWindow_DecodeInfo(Payload->Buffer, Payload->Length, &Info));
    }
    WindowContext->Callback.Query(Request,
                                  Status,
                                  ZpStatus_IsSuccess(Status) ? &Info : NULL,
                                  WindowContext->Context);
    Mem_Free(WindowContext);
}

static
VOID
NTAPI
ZpWindow_StatusComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_WINDOW_CONTEXT WindowContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    WindowContext->Callback.Status(Request, Status, WindowContext->Context);
    Mem_Free(WindowContext);
}

static
VOID
NTAPI
ZpWindow_CaptureComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_WINDOW_CONTEXT WindowContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length == 0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    WindowContext->Callback.Capture(Request,
                                    Status,
                                    ZpStatus_IsSuccess(Status) ? Payload : NULL,
                                    WindowContext->Context);
    Mem_Free(WindowContext);
}

static
NTSTATUS
ZpWindow_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Complete,
    _In_ ZP_WINDOW_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_WINDOW_CONTEXT WindowContext;
    NTSTATUS Status;

    WindowContext = Mem_Alloc(sizeof(*WindowContext));
    if (WindowContext == NULL) return STATUS_NO_MEMORY;
    WindowContext->Callback = Callback;
    WindowContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_WINDOW_MODULE_ID,
                                  OperationId,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  Complete,
                                  WindowContext,
                                  Request);
    if (!NT_SUCCESS(Status)) Mem_Free(WindowContext);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_EnumerateWindows(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WINDOW_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_WINDOW_CALLBACK WindowCallback;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    WindowCallback.Enumerate = Callback;
    return ZpWindow_Send(Connection,
                         ZP_WINDOW_OPERATION_ENUMERATE,
                         TimeoutMilliseconds,
                         NULL,
                         0,
                         ZpWindow_EnumerateComplete,
                         WindowCallback,
                         Context,
                         Request);
}

NTSTATUS
NTAPI
ZpServer_EnumerateMonitors(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WINDOW_MONITOR_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_WINDOW_CALLBACK WindowCallback;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    WindowCallback.EnumerateMonitors = Callback;
    return ZpWindow_Send(Connection,
                         ZP_WINDOW_OPERATION_ENUMERATE_MONITORS,
                         TimeoutMilliseconds,
                         NULL,
                         0,
                         ZpWindow_EnumerateMonitorsComplete,
                         WindowCallback,
                         Context,
                         Request);
}

NTSTATUS
NTAPI
ZpServer_QueryWindow(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WINDOW_QUERY_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_WINDOW_CALLBACK WindowCallback;
    BYTE Payload[sizeof(ULONGLONG) + 2 * sizeof(ULONG)];
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    WindowCallback.Query = Callback;
    Status = ZpWindow_EncodeIdentity(Handle,
                                     ProcessId,
                                     ThreadId,
                                     Payload,
                                     sizeof(Payload),
                                     &PayloadLength);
    return NT_SUCCESS(Status) ?
               ZpWindow_Send(Connection,
                             ZP_WINDOW_OPERATION_QUERY,
                             TimeoutMilliseconds,
                             Payload,
                             PayloadLength,
                             ZpWindow_QueryComplete,
                             WindowCallback,
                             Context,
                             Request) :
               Status;
}

NTSTATUS
NTAPI
ZpServer_ControlWindow(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ZP_WINDOW_CONTROL Control,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_WINDOW_CALLBACK WindowCallback;
    BYTE Payload[sizeof(ULONGLONG) + 2 * sizeof(ULONG) + sizeof(USHORT)];
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    WindowCallback.Status = Callback;
    Status = ZpWindow_EncodeControl(Handle,
                                    ProcessId,
                                    ThreadId,
                                    Control,
                                    Payload,
                                    sizeof(Payload),
                                    &PayloadLength);
    return NT_SUCCESS(Status) ?
               ZpWindow_Send(Connection,
                             ZP_WINDOW_OPERATION_CONTROL,
                             TimeoutMilliseconds,
                             Payload,
                             PayloadLength,
                             ZpWindow_StatusComplete,
                             WindowCallback,
                             Context,
                             Request) :
               Status;
}

NTSTATUS
NTAPI
ZpServer_UpdateWindow(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ PCZP_WINDOW_UPDATE Update,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_WINDOW_CALLBACK WindowCallback;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    WindowCallback.Status = Callback;
    Status = ZpWindow_EncodeUpdate(Update, NULL, 0, &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (!NT_SUCCESS(Status) || Payload == NULL)
    {
        return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    }
    Status = ZpWindow_EncodeUpdate(Update, Payload, PayloadLength, &PayloadLength);
    if (NT_SUCCESS(Status))
    {
        Status = ZpWindow_Send(Connection,
                               ZP_WINDOW_OPERATION_UPDATE,
                               TimeoutMilliseconds,
                               Payload,
                               PayloadLength,
                               ZpWindow_StatusComplete,
                               WindowCallback,
                               Context,
                               Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_CaptureWindow(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WINDOW_CAPTURE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_WINDOW_CALLBACK WindowCallback;
    BYTE Payload[ZP_WINDOW_CAPTURE_REQUEST_WIRE_SIZE];
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    WindowCallback.Capture = Callback;
    Status = ZpWindow_EncodeCaptureRequest(Options,
                                           Payload,
                                           sizeof(Payload),
                                           &PayloadLength);
    return NT_SUCCESS(Status) ?
               ZpWindow_Send(Connection,
                             ZP_WINDOW_OPERATION_CAPTURE,
                             TimeoutMilliseconds,
                             Payload,
                             PayloadLength,
                             ZpWindow_CaptureComplete,
                             WindowCallback,
                             Context,
                             Request) :
               Status;
}

static
VOID
NTAPI
ZpWindow_CaptureWritable(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Channel);
    UNREFERENCED_PARAMETER(CreditBytes);
    UNREFERENCED_PARAMETER(Context);
}

static
VOID
NTAPI
ZpWindow_OpenCaptureComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_WINDOW_CAPTURE_CONTEXT CaptureContext = Context;
    PZP_SERVER_CHANNEL_OBJECT Channel = NULL;
    ULONG ChannelId = 0;
    NTSTATUS ChannelStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpWindow_DecodeCaptureChannel(Payload->Buffer,
                                           Payload->Length,
                                           &ChannelId));
    }
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpServerChannel_Create(
                CaptureContext->Connection,
                ChannelId,
                ZP_WINDOW_MODULE_ID,
                FALSE,
                0,
                FALSE,
                0,
                CaptureContext->DataCallback,
                ZpWindow_CaptureWritable,
                CaptureContext->CloseCallback,
                CaptureContext->Context,
                TRUE,
                &Channel));
    }
    else
    {
        ZpServerChannel_ReleaseReservation(CaptureContext->Connection);
    }
    if (!ZpStatus_IsSuccess(Status) && ChannelId != 0)
    {
        ZpServerConnection_RejectChannel(CaptureContext->Connection,
                                         ChannelId,
                                         Status);
    }
    CaptureContext->OpenCallback(Request,
                                 Status,
                                 ZpStatus_IsSuccess(Status) ? (ZP_CHANNEL_HANDLE)Channel : NULL,
                                 CaptureContext->Context);
    if (Channel != NULL)
    {
        ChannelStatus = ZpServerChannel_SendWindow(Channel,
                                                   ZP_SERVER_DEFAULT_CHANNEL_WINDOW_SIZE);
        if (!NT_SUCCESS(ChannelStatus))
        {
            ZpServerChannel_Abort(Channel,
                                  ZpStatus_FromNtStatus(ChannelStatus));
        }
        ZpChannel_Close((ZP_CHANNEL_HANDLE)Channel);
    }
    Mem_Free(CaptureContext);
}

NTSTATUS
NTAPI
ZpServer_OpenWindowCapture(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WINDOW_CAPTURE_OPEN_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_WINDOW_CAPTURE_CONTEXT CaptureContext;
    BYTE Payload[ZP_WINDOW_CAPTURE_REQUEST_WIRE_SIZE];
    ULONG PayloadLength;
    NTSTATUS Status;
    LOGICAL Reserved = FALSE;

    if (OpenCallback == NULL || DataCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpWindow_EncodeCaptureRequest(Options,
                                           Payload,
                                           sizeof(Payload),
                                           &PayloadLength);
    CaptureContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*CaptureContext)) : NULL;
    if (NT_SUCCESS(Status) && CaptureContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerChannel_Reserve(Connection);
        Reserved = NT_SUCCESS(Status);
    }
    if (NT_SUCCESS(Status))
    {
        CaptureContext->Connection = Connection;
        CaptureContext->OpenCallback = OpenCallback;
        CaptureContext->DataCallback = DataCallback;
        CaptureContext->CloseCallback = CloseCallback;
        CaptureContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_WINDOW_MODULE_ID,
                                      ZP_WINDOW_OPERATION_OPEN_CAPTURE,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpWindow_OpenCaptureComplete,
                                      CaptureContext,
                                      Request);
        if (NT_SUCCESS(Status))
        {
            Reserved = FALSE;
        }
        else
        {
            Mem_Free(CaptureContext);
        }
    }
    if (Reserved) ZpServerChannel_ReleaseReservation(Connection);
    return Status;
}
