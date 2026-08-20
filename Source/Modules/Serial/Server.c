#include <KNSoft/MakeLifeEasier/Memory/Core.h>
#include <KNSoft/ZPigeon/Server.h>

#include "../../KNSoft.ZPigeon.Server.SDK/Core/Channel.h"

typedef struct _ZP_SERIAL_PORTS_CONTEXT
{
    ZP_SERIAL_PORTS_CALLBACK Callback;
    PVOID Context;
} ZP_SERIAL_PORTS_CONTEXT, *PZP_SERIAL_PORTS_CONTEXT;

typedef struct _ZP_SERIAL_OPEN_CONTEXT
{
    PZP_CONNECTION_OBJECT Connection;
    ZP_SERIAL_OPEN_CALLBACK OpenCallback;
    ZP_CHANNEL_DATA_CALLBACK DataCallback;
    ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback;
    ZP_CHANNEL_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_SERIAL_OPEN_CONTEXT, *PZP_SERIAL_OPEN_CONTEXT;

static
VOID
NTAPI
ZpSerial_PortsComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_ PVOID Context)
{
    PZP_SERIAL_PORTS_CONTEXT SerialContext = Context;
    ZP_SERIAL_PORT_LIST_VIEW Ports;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpSerial_DecodePortList(Payload->Buffer, Payload->Length, &Ports));
    }
    SerialContext->Callback(Request,
                            Status,
                            ZpStatus_IsSuccess(Status) ? &Ports : NULL,
                            SerialContext->Context);
    Mem_Free(SerialContext);
}

NTSTATUS
NTAPI
ZpServer_EnumerateSerialPorts(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_SERIAL_PORTS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERIAL_PORTS_CONTEXT SerialContext;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    SerialContext = Mem_Alloc(sizeof(*SerialContext));
    if (SerialContext == NULL) return STATUS_NO_MEMORY;
    SerialContext->Callback = Callback;
    SerialContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_SERIAL_MODULE_ID,
                                  ZP_SERIAL_OPERATION_ENUMERATE,
                                  TimeoutMilliseconds,
                                  NULL,
                                  0,
                                  ZpSerial_PortsComplete,
                                  SerialContext,
                                  Request);
    if (!NT_SUCCESS(Status)) Mem_Free(SerialContext);
    return Status;
}

static
VOID
NTAPI
ZpSerial_OpenComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_ PVOID Context)
{
    PZP_SERIAL_OPEN_CONTEXT SerialContext = Context;
    PZP_SERVER_CHANNEL_OBJECT Channel = NULL;
    ULONG ChannelId = 0;
    NTSTATUS ChannelStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpSerial_DecodeChannel(Payload->Buffer, Payload->Length, &ChannelId));
    }
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpServerChannel_Create(SerialContext->Connection,
                                                              ChannelId,
                                                              ZP_SERIAL_MODULE_ID,
                                                              FALSE,
                                                              0,
                                                              FALSE,
                                                              0,
                                                              SerialContext->DataCallback,
                                                              SerialContext->WritableCallback,
                                                              SerialContext->CloseCallback,
                                                              SerialContext->Context,
                                                              TRUE,
                                                              &Channel));
    }
    else
    {
        ZpServerChannel_ReleaseReservation(SerialContext->Connection);
    }
    if (!ZpStatus_IsSuccess(Status) && ChannelId != 0)
    {
        ZpServerConnection_RejectChannel(SerialContext->Connection, ChannelId, Status);
    }
    SerialContext->OpenCallback(Request,
                                Status,
                                ZpStatus_IsSuccess(Status) ? (ZP_CHANNEL_HANDLE)Channel : NULL,
                                SerialContext->Context);
    if (Channel != NULL)
    {
        ChannelStatus = ZpServerChannel_SendWindow(Channel, ZP_SERVER_DEFAULT_CHANNEL_WINDOW_SIZE);
        if (!NT_SUCCESS(ChannelStatus)) ZpServerChannel_Abort(Channel, ZpStatus_FromNtStatus(ChannelStatus));
        ZpChannel_Close((ZP_CHANNEL_HANDLE)Channel);
    }
    Mem_Free(SerialContext);
}

NTSTATUS
NTAPI
ZpServer_OpenSerialPort(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PortLength) PCWCH Port,
    _In_ ULONG PortLength,
    _In_ ULONG BaudRate,
    _In_ BYTE DataBits,
    _In_ BYTE Parity,
    _In_ BYTE StopBits,
    _In_ BYTE FlowControl,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_SERIAL_OPEN_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERIAL_OPEN_CONTEXT SerialContext;
    BYTE Payload[sizeof(ULONG) * 2 + ZP_SERIAL_MAX_NAME_LENGTH * sizeof(WCHAR) + 4];
    ULONG PayloadLength;
    NTSTATUS Status;
    LOGICAL Reserved = FALSE;

    if (OpenCallback == NULL || DataCallback == NULL || WritableCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpSerial_EncodeOpenRequest(Port,
                                        PortLength,
                                        BaudRate,
                                        DataBits,
                                        Parity,
                                        StopBits,
                                        FlowControl,
                                        Payload,
                                        sizeof(Payload),
                                        &PayloadLength);
    SerialContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*SerialContext)) : NULL;
    if (NT_SUCCESS(Status) && SerialContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerChannel_Reserve(Connection);
        Reserved = NT_SUCCESS(Status);
    }
    if (NT_SUCCESS(Status))
    {
        SerialContext->Connection = Connection;
        SerialContext->OpenCallback = OpenCallback;
        SerialContext->DataCallback = DataCallback;
        SerialContext->WritableCallback = WritableCallback;
        SerialContext->CloseCallback = CloseCallback;
        SerialContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_SERIAL_MODULE_ID,
                                      ZP_SERIAL_OPERATION_OPEN,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpSerial_OpenComplete,
                                      SerialContext,
                                      Request);
        if (!NT_SUCCESS(Status)) Mem_Free(SerialContext);
        else Reserved = FALSE;
    }
    if (Reserved) ZpServerChannel_ReleaseReservation(Connection);
    return Status;
}
