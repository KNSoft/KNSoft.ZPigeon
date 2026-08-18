#include <KNSoft/MakeLifeEasier/Memory/Core.h>
#include <KNSoft/ZPigeon/Server.h>

#include "../../KNSoft.ZPigeon.Server.SDK/Core/Channel.h"
#include "../../SDK/Channel.h"

typedef struct _ZP_SERVER_TUNNEL_CONTEXT
{
    PZP_CONNECTION_OBJECT Connection;
    ZP_TUNNEL_OPEN_CALLBACK OpenCallback;
    ZP_CHANNEL_DATA_CALLBACK DataCallback;
    ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback;
    ZP_CHANNEL_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_SERVER_TUNNEL_CONTEXT, *PZP_SERVER_TUNNEL_CONTEXT;

static
VOID
NTAPI
ZpServerTunnel_OpenComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_TUNNEL_CONTEXT TunnelContext = Context;
    PZP_SERVER_CHANNEL_OBJECT Channel = NULL;
    ULONGLONG ChannelId = 0;
    NTSTATUS ChannelStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpTunnel_DecodeOpenResponse(Payload->Buffer,
                                                                   Payload->Length,
                                                                   &ChannelId));
    }
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpServerChannel_Create(TunnelContext->Connection,
                                                              ChannelId,
                                                              ZP_TUNNEL_MODULE_ID,
                                                              FALSE,
                                                              0,
                                                              FALSE,
                                                              0,
                                                              TunnelContext->DataCallback,
                                                              TunnelContext->WritableCallback,
                                                              TunnelContext->CloseCallback,
                                                              TunnelContext->Context,
                                                              TRUE,
                                                              &Channel));
    }
    else
    {
        ZpServerChannel_ReleaseReservation(TunnelContext->Connection);
    }
    if (!ZpStatus_IsSuccess(Status) && ChannelId != 0)
    {
        ZpServerConnection_RejectChannel(TunnelContext->Connection, ChannelId, Status);
    }
    TunnelContext->OpenCallback(Request,
                                Status,
                                ZpStatus_IsSuccess(Status) ? (ZP_CHANNEL_HANDLE)Channel : NULL,
                                TunnelContext->Context);
    if (Channel != NULL)
    {
        ChannelStatus = ZpServerChannel_SendWindow(Channel, ZP_SERVER_DEFAULT_CHANNEL_WINDOW_SIZE);
        if (!NT_SUCCESS(ChannelStatus))
        {
            ZpServerChannel_Abort(Channel, ZpStatus_FromNtStatus(ChannelStatus));
        }
        ZpChannel_Close((ZP_CHANNEL_HANDLE)Channel);
    }
    Mem_Free(TunnelContext);
}

NTSTATUS
NTAPI
ZpServer_OpenTunnel(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(HostLength) PCWCH Host,
    _In_ ULONG HostLength,
    _In_ USHORT Port,
    _In_ USHORT Protocol,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_TUNNEL_OPEN_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_TUNNEL_CONTEXT TunnelContext;
    BYTE Payload[sizeof(ULONG) + ZP_TUNNEL_HOST_MAX_LENGTH * sizeof(WCHAR) + sizeof(USHORT) * 2];
    ULONG PayloadLength;
    NTSTATUS Status;
    LOGICAL Reserved = FALSE;

    if (Connection == NULL || OpenCallback == NULL || DataCallback == NULL ||
        WritableCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpTunnel_EncodeOpen(Host, HostLength, Port, Protocol, Payload, sizeof(Payload), &PayloadLength);
    TunnelContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*TunnelContext)) : NULL;
    if (NT_SUCCESS(Status) && TunnelContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerChannel_Reserve(Connection);
        Reserved = NT_SUCCESS(Status);
    }
    if (NT_SUCCESS(Status))
    {
        TunnelContext->Connection = Connection;
        TunnelContext->OpenCallback = OpenCallback;
        TunnelContext->DataCallback = DataCallback;
        TunnelContext->WritableCallback = WritableCallback;
        TunnelContext->CloseCallback = CloseCallback;
        TunnelContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_TUNNEL_MODULE_ID,
                                      ZP_TUNNEL_OPERATION_OPEN,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpServerTunnel_OpenComplete,
                                      TunnelContext,
                                      Request);
        if (!NT_SUCCESS(Status)) Mem_Free(TunnelContext);
        else Reserved = FALSE;
    }
    if (Reserved) ZpServerChannel_ReleaseReservation(Connection);
    return Status;
}
