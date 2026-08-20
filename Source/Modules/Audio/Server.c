#include <KNSoft/ZPigeon/Server.h>

#include "../../KNSoft.ZPigeon.Server.SDK/Core/Channel.h"
#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef union _ZP_AUDIO_CALLBACK
{
    ZP_AUDIO_DEVICES_CALLBACK Devices;
    ZP_AUDIO_SESSIONS_CALLBACK Sessions;
    ZP_REQUEST_STATUS_CALLBACK Status;
} ZP_AUDIO_CALLBACK;

typedef struct _ZP_AUDIO_CONTEXT
{
    ZP_AUDIO_CALLBACK Callback;
    PVOID Context;
} ZP_AUDIO_CONTEXT, *PZP_AUDIO_CONTEXT;

typedef struct _ZP_AUDIO_STREAM_CONTEXT
{
    ZP_CONNECTION_HANDLE Connection;
    ZP_AUDIO_STREAM_OPEN_CALLBACK OpenCallback;
    ZP_CHANNEL_DATA_CALLBACK DataCallback;
    ZP_CHANNEL_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_AUDIO_STREAM_CONTEXT, *PZP_AUDIO_STREAM_CONTEXT;

static
VOID
NTAPI
ZpAudio_DevicesComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_AUDIO_CONTEXT AudioContext = Context;
    ZP_AUDIO_DEVICE_LIST_VIEW Devices;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpAudio_DecodeDeviceList(Payload->Buffer, Payload->Length, &Devices));
    }
    AudioContext->Callback.Devices(Request,
                                   Status,
                                   ZpStatus_IsSuccess(Status) ? &Devices : NULL,
                                   AudioContext->Context);
    Mem_Free(AudioContext);
}

static
VOID
NTAPI
ZpAudio_SessionsComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_AUDIO_CONTEXT AudioContext = Context;
    ZP_AUDIO_SESSION_LIST_VIEW Sessions;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpAudio_DecodeSessionList(Payload->Buffer, Payload->Length, &Sessions));
    }
    AudioContext->Callback.Sessions(Request,
                                    Status,
                                    ZpStatus_IsSuccess(Status) ? &Sessions : NULL,
                                    AudioContext->Context);
    Mem_Free(AudioContext);
}

static
VOID
NTAPI
ZpAudio_StatusComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_AUDIO_CONTEXT AudioContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0) Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    AudioContext->Callback.Status(Request, Status, AudioContext->Context);
    Mem_Free(AudioContext);
}

static
NTSTATUS
ZpAudio_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Complete,
    _In_ ZP_AUDIO_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_AUDIO_CONTEXT AudioContext;
    NTSTATUS Status;

    AudioContext = Mem_Alloc(sizeof(*AudioContext));
    if (AudioContext == NULL) return STATUS_NO_MEMORY;
    AudioContext->Callback = Callback;
    AudioContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_AUDIO_MODULE_ID,
                                  OperationId,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  Complete,
                                  AudioContext,
                                  Request);
    if (!NT_SUCCESS(Status)) Mem_Free(AudioContext);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_EnumerateAudioDevices(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_AUDIO_DEVICES_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_AUDIO_CALLBACK AudioCallback;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    AudioCallback.Devices = Callback;
    return ZpAudio_Send(Connection,
                        ZP_AUDIO_OPERATION_ENUMERATE_DEVICES,
                        TimeoutMilliseconds,
                        NULL,
                        0,
                        ZpAudio_DevicesComplete,
                        AudioCallback,
                        Context,
                        Request);
}

NTSTATUS
NTAPI
ZpServer_EnumerateAudioSessions(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_AUDIO_SESSIONS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_AUDIO_CALLBACK AudioCallback;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    AudioCallback.Sessions = Callback;
    return ZpAudio_Send(Connection,
                        ZP_AUDIO_OPERATION_ENUMERATE_SESSIONS,
                        TimeoutMilliseconds,
                        NULL,
                        0,
                        ZpAudio_SessionsComplete,
                        AudioCallback,
                        Context,
                        Request);
}

static
NTSTATUS
ZpAudio_SendControl(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_AUDIO_CALLBACK AudioCallback;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    AudioCallback.Status = Callback;
    return ZpAudio_Send(Connection,
                        OperationId,
                        TimeoutMilliseconds,
                        Payload,
                        PayloadLength,
                        ZpAudio_StatusComplete,
                        AudioCallback,
                        Context,
                        Request);
}

NTSTATUS
NTAPI
ZpServer_ControlAudioEndpoint(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_AUDIO_FLOW Flow,
    _In_ ZP_AUDIO_ENDPOINT_CONTROL Control,
    _In_ ULONG Value,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    Status = ZpAudio_EncodeEndpointControl(Flow, Control, Value, DeviceId, DeviceIdLength, NULL, 0, &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (!NT_SUCCESS(Status) || Payload == NULL) return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    Status = ZpAudio_EncodeEndpointControl(Flow,
                                           Control,
                                           Value,
                                           DeviceId,
                                           DeviceIdLength,
                                           Payload,
                                           PayloadLength,
                                           &PayloadLength);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAudio_SendControl(Connection,
                                     ZP_AUDIO_OPERATION_CONTROL_ENDPOINT,
                                     Payload,
                                     PayloadLength,
                                     TimeoutMilliseconds,
                                     Callback,
                                     Context,
                                     Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_ControlAudioSession(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_AUDIO_SESSION_CONTROL Control,
    _In_ ULONG Value,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(SessionIdLength) PCWCH SessionId,
    _In_ ULONG SessionIdLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    Status = ZpAudio_EncodeSessionControl(Control,
                                          Value,
                                          DeviceId,
                                          DeviceIdLength,
                                          SessionId,
                                          SessionIdLength,
                                          NULL,
                                          0,
                                          &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (!NT_SUCCESS(Status) || Payload == NULL) return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    Status = ZpAudio_EncodeSessionControl(Control,
                                          Value,
                                          DeviceId,
                                          DeviceIdLength,
                                          SessionId,
                                          SessionIdLength,
                                          Payload,
                                          PayloadLength,
                                          &PayloadLength);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAudio_SendControl(Connection,
                                     ZP_AUDIO_OPERATION_CONTROL_SESSION,
                                     Payload,
                                     PayloadLength,
                                     TimeoutMilliseconds,
                                     Callback,
                                     Context,
                                     Request);
    }
    Mem_Free(Payload);
    return Status;
}

static
VOID
NTAPI
ZpAudio_OpenComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_AUDIO_STREAM_CONTEXT StreamContext = Context;
    PZP_SERVER_CHANNEL_OBJECT Channel = NULL;
    ULONG ChannelId = 0;
    NTSTATUS ChannelStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpAudio_DecodeChannel(Payload->Buffer, Payload->Length, &ChannelId));
    }
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpServerChannel_Create(StreamContext->Connection,
                                                              ChannelId,
                                                              ZP_AUDIO_MODULE_ID,
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
ZpServer_OpenAudioStream(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_AUDIO_FLOW Flow,
    _In_ ULONG DirectStreamId,
    _In_reads_opt_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_AUDIO_STREAM_OPEN_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_AUDIO_STREAM_CONTEXT StreamContext;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;
    LOGICAL Reserved = FALSE;

    if (OpenCallback == NULL || DataCallback == NULL || CloseCallback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpAudio_EncodeStreamRequest(Flow,
                                         DirectStreamId,
                                         DeviceId,
                                         DeviceIdLength,
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
    Status = ZpAudio_EncodeStreamRequest(Flow,
                                         DirectStreamId,
                                         DeviceId,
                                         DeviceIdLength,
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
                                      ZP_AUDIO_MODULE_ID,
                                      ZP_AUDIO_OPERATION_OPEN_STREAM,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpAudio_OpenComplete,
                                      StreamContext,
                                      Request);
        if (NT_SUCCESS(Status)) Reserved = FALSE;
    }
    Mem_Free(Payload);
    if (!NT_SUCCESS(Status)) Mem_Free(StreamContext);
    if (Reserved) ZpServerChannel_ReleaseReservation(Connection);
    return Status;
}
