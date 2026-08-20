#include <KNSoft/ZPigeon/Server.h>

#include "../../KNSoft.ZPigeon.Server.SDK/Core/Channel.h"
#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef union _ZP_VIDEO_CALLBACK
{
    ZP_VIDEO_DEVICES_CALLBACK Devices;
    ZP_REQUEST_STATUS_CALLBACK Status;
} ZP_VIDEO_CALLBACK;

typedef struct _ZP_VIDEO_CONTEXT
{
    ZP_VIDEO_CALLBACK Callback;
    PVOID Context;
} ZP_VIDEO_CONTEXT, *PZP_VIDEO_CONTEXT;

typedef struct _ZP_VIDEO_STREAM_CONTEXT
{
    ZP_CONNECTION_HANDLE Connection;
    ZP_VIDEO_STREAM_OPEN_CALLBACK OpenCallback;
    ZP_CHANNEL_DATA_CALLBACK DataCallback;
    ZP_CHANNEL_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_VIDEO_STREAM_CONTEXT, *PZP_VIDEO_STREAM_CONTEXT;

static
VOID
NTAPI
ZpVideo_DevicesComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_ PVOID Context)
{
    PZP_VIDEO_CONTEXT VideoContext = Context;
    ZP_VIDEO_DEVICE_LIST_VIEW Devices;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpVideo_DecodeDeviceList(Payload->Buffer, Payload->Length, &Devices));
    }
    VideoContext->Callback.Devices(Request,
                                   Status,
                                   ZpStatus_IsSuccess(Status) ? &Devices : NULL,
                                   VideoContext->Context);
    Mem_Free(VideoContext);
}

static
VOID
NTAPI
ZpVideo_StatusComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_VIDEO_CONTEXT VideoContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    VideoContext->Callback.Status(Request, Status, VideoContext->Context);
    Mem_Free(VideoContext);
}

NTSTATUS
NTAPI
ZpServer_EnumerateVideoDevices(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_VIDEO_DEVICES_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_VIDEO_CONTEXT VideoContext;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    VideoContext = Mem_Alloc(sizeof(*VideoContext));
    if (VideoContext == NULL) return STATUS_NO_MEMORY;
    VideoContext->Callback.Devices = Callback;
    VideoContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_VIDEO_MODULE_ID,
                                  ZP_VIDEO_OPERATION_ENUMERATE_DEVICES,
                                  TimeoutMilliseconds,
                                  NULL,
                                  0,
                                  ZpVideo_DevicesComplete,
                                  VideoContext,
                                  Request);
    if (!NT_SUCCESS(Status)) Mem_Free(VideoContext);
    return Status;
}

static
VOID
NTAPI
ZpVideo_OpenComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_ PVOID Context)
{
    PZP_VIDEO_STREAM_CONTEXT StreamContext = Context;
    PZP_SERVER_CHANNEL_OBJECT Channel = NULL;
    ULONG ChannelId = 0;
    NTSTATUS ChannelStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpVideo_DecodeChannel(Payload->Buffer, Payload->Length, &ChannelId));
    }
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpServerChannel_Create(StreamContext->Connection,
                                                              ChannelId,
                                                              ZP_VIDEO_MODULE_ID,
                                                              FALSE,
                                                              0,
                                                              FALSE,
                                                              0,
                                                              StreamContext->DataCallback,
                                                              NULL,
                                                              StreamContext->CloseCallback,
                                                              StreamContext->Context,
                                                              TRUE,
                                                              &Channel));
    }
    else
    {
        ZpServerChannel_ReleaseReservation(StreamContext->Connection);
    }
    if (!ZpStatus_IsSuccess(Status) && ChannelId != 0)
    {
        ZpServerConnection_RejectChannel(StreamContext->Connection, ChannelId, Status);
    }
    StreamContext->OpenCallback(Request,
                                Status,
                                ZpStatus_IsSuccess(Status) ? (ZP_CHANNEL_HANDLE)Channel : NULL,
                                StreamContext->Context);
    if (Channel != NULL)
    {
        ChannelStatus = ZpServerChannel_SendWindow(Channel, ZP_SERVER_DEFAULT_CHANNEL_WINDOW_SIZE);
        if (!NT_SUCCESS(ChannelStatus)) ZpServerChannel_Abort(Channel, ZpStatus_FromNtStatus(ChannelStatus));
        ZpChannel_Close((ZP_CHANNEL_HANDLE)Channel);
    }
    Mem_Free(StreamContext);
}

NTSTATUS
NTAPI
ZpServer_OpenVideoStream(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ PCZP_VIDEO_FORMAT Format,
    _In_ USHORT Quality,
    _In_ ULONG DirectStreamId,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_VIDEO_STREAM_OPEN_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_VIDEO_STREAM_CONTEXT StreamContext;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;
    LOGICAL Reserved = FALSE;

    if (OpenCallback == NULL || DataCallback == NULL || CloseCallback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpVideo_EncodeStreamRequest(DeviceId,
                                         DeviceIdLength,
                                         Format,
                                         Quality,
                                         DirectStreamId,
                                         NULL,
                                         0,
                                         &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    StreamContext = Payload != NULL ? Mem_Alloc(sizeof(*StreamContext)) : NULL;
    if (!NT_SUCCESS(Status) || Payload == NULL || StreamContext == NULL)
    {
        Mem_Free(StreamContext);
        Mem_Free(Payload);
        return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    }
    Status = ZpVideo_EncodeStreamRequest(DeviceId,
                                         DeviceIdLength,
                                         Format,
                                         Quality,
                                         DirectStreamId,
                                         Payload,
                                         PayloadLength,
                                         &PayloadLength);
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerChannel_Reserve(Connection);
        Reserved = NT_SUCCESS(Status);
    }
    if (NT_SUCCESS(Status))
    {
        StreamContext->Connection = Connection;
        StreamContext->OpenCallback = OpenCallback;
        StreamContext->DataCallback = DataCallback;
        StreamContext->CloseCallback = CloseCallback;
        StreamContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_VIDEO_MODULE_ID,
                                      ZP_VIDEO_OPERATION_OPEN_STREAM,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpVideo_OpenComplete,
                                      StreamContext,
                                      Request);
        if (NT_SUCCESS(Status)) Reserved = FALSE;
    }
    Mem_Free(Payload);
    if (!NT_SUCCESS(Status)) Mem_Free(StreamContext);
    if (Reserved) ZpServerChannel_ReleaseReservation(Connection);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_UpdateVideoStream(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_VIDEO_FORMAT Format,
    _In_ USHORT Quality,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_VIDEO_STREAM_UPDATE Update;
    PZP_VIDEO_CONTEXT VideoContext;
    BYTE Payload[sizeof(ULONG) * 5 + sizeof(USHORT)];
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Format == NULL || Callback == NULL) return STATUS_INVALID_PARAMETER;
    VideoContext = Mem_Alloc(sizeof(*VideoContext));
    if (VideoContext == NULL) return STATUS_NO_MEMORY;
    VideoContext->Callback.Status = Callback;
    VideoContext->Context = Context;
    Status = ZpServerChannel_GetId((PZP_SERVER_CHANNEL_OBJECT)Channel,
                                   Connection,
                                   ZP_VIDEO_MODULE_ID,
                                   &Update.ChannelId);
    if (NT_SUCCESS(Status))
    {
        Update.Format = *Format;
        Update.Quality = Quality;
        Status = ZpVideo_EncodeStreamUpdate(&Update,
                                            Payload,
                                            sizeof(Payload),
                                            &PayloadLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpServer_SendRequest(Connection,
                                      ZP_VIDEO_MODULE_ID,
                                      ZP_VIDEO_OPERATION_UPDATE_STREAM,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpVideo_StatusComplete,
                                      VideoContext,
                                      Request);
    }
    if (!NT_SUCCESS(Status)) Mem_Free(VideoContext);
    return Status;
}
