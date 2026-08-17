#include "Client.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <Ws2tcpip.h>

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"

#pragma comment(lib, "Ws2_32.lib")

#define ZP_TUNNEL_CHUNK_SIZE 0x00010000UL
#define ZP_TUNNEL_WINDOW_SIZE 0x00100000UL

struct _ZP_CLIENT_TUNNEL_CHANNEL
{
    ZP_CLIENT_LOCAL_CHANNEL Header;
    RTL_SRWLOCK SendLock;
    volatile LONG Closed;
    LOGICAL WorkerActive;
    ULONGLONG Credit;
    ULONGLONG ReceiveCredit;
    SOCKET Socket;
    HANDLE WorkerThread;
    HANDLE CreditEvent;
};

static
NTSTATUS
ZpTunnel_SendLocked(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PCZP_TRANSPORT_OPERATIONS Operations = Object->TransportOperations[Object->ActiveTransport];

    return Object->State == ZpClientStateReady && Operations->Send != NULL ?
               Operations->Send(Object->TransportContexts[Object->ActiveTransport], MessageType, Body, BodyLength) :
               STATUS_CONNECTION_DISCONNECTED;
}

static
NTSTATUS
ZpTunnel_SendCloseLocked(
    _Inout_ PZP_CLIENT_TUNNEL_CHANNEL Channel,
    _In_ ZP_STATUS CloseStatus)
{
    BYTE Body[sizeof(ULONGLONG) + ZP_STATUS_WIRE_SIZE];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelClose(Channel->Header.ChannelId,
                                          CloseStatus,
                                          Body,
                                          sizeof(Body),
                                          &BodyLength);
    return NT_SUCCESS(Status) ?
               ZpTunnel_SendLocked(Channel->Header.Owner, ZpMessageChannelClose, Body, BodyLength) : Status;
}

static
NTSTATUS
ZpTunnel_SendWindowLocked(
    _Inout_ PZP_CLIENT_TUNNEL_CHANNEL Channel,
    _In_ ULONG CreditBytes)
{
    BYTE Body[sizeof(ULONGLONG) + sizeof(ULONG)];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelWindow(Channel->Header.ChannelId,
                                           CreditBytes,
                                           Body,
                                           sizeof(Body),
                                           &BodyLength);
    if (NT_SUCCESS(Status))
    {
        Channel->ReceiveCredit += CreditBytes;
        Status = ZpTunnel_SendLocked(Channel->Header.Owner, ZpMessageChannelWindow, Body, BodyLength);
        if (!NT_SUCCESS(Status)) Channel->ReceiveCredit -= CreditBytes;
    }
    return Status;
}

static
VOID
ZpTunnel_CloseSocket(
    _Inout_ PZP_CLIENT_TUNNEL_CHANNEL Channel)
{
    if (!InterlockedExchange(&Channel->Closed, TRUE)) shutdown(Channel->Socket, SD_BOTH);
    if (Channel->CreditEvent != NULL) NtSetEvent(Channel->CreditEvent, NULL);
}

static
VOID
ZpTunnel_ChannelDestroy(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel)
{
    PZP_CLIENT_TUNNEL_CHANNEL Channel = (PZP_CLIENT_TUNNEL_CHANNEL)LocalChannel;

    ZpTunnel_CloseSocket(Channel);
    if (Channel->Socket != INVALID_SOCKET) closesocket(Channel->Socket);
    if (Channel->WorkerThread != NULL) NtClose(Channel->WorkerThread);
    if (Channel->CreditEvent != NULL) NtClose(Channel->CreditEvent);
    WSACleanup();
    Mem_Free(Channel);
}

static
VOID
ZpTunnel_ChannelAbort(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    UNREFERENCED_PARAMETER(Status);
    ZpTunnel_CloseSocket((PZP_CLIENT_TUNNEL_CHANNEL)LocalChannel);
}

static
VOID
ZpTunnel_FinishWorker(
    _Inout_ PZP_CLIENT_TUNNEL_CHANNEL Channel,
    _In_ ZP_STATUS Status,
    _In_ LOGICAL Notify)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    if (Removed && Notify) ZpTunnel_SendCloseLocked(Channel, Status);
    Channel->WorkerActive = FALSE;
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    ZpTunnel_CloseSocket(Channel);
    if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
    ZpClientLocalChannel_Release(&Channel->Header);
}

static
NTSTATUS
NTAPI
ZpTunnel_ReceiveThread(
    _In_ PVOID Context)
{
    PZP_CLIENT_TUNNEL_CHANNEL Channel = Context;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    PBYTE Body = Mem_Alloc(sizeof(ULONGLONG) + ZP_TUNNEL_CHUNK_SIZE);
    ZP_STATUS Completion = ZpStatus_Make(ZpStatusNone, 0);
    ULONG ReadLength, ReservedLength, BodyLength;
    INT Received;
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL Pending, Removed, Notify = TRUE;

    if (Body == NULL)
    {
        ZpTunnel_FinishWorker(Channel, ZpStatus_FromNtStatus(STATUS_NO_MEMORY), TRUE);
        return STATUS_NO_MEMORY;
    }
    for (;;)
    {
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Pending = Channel->Header.Pending;
        if (Pending && Channel->Credit == 0)
        {
            RtlReleaseSRWLockExclusive(&Object->Lock);
            Status = NtWaitForSingleObject(Channel->CreditEvent, FALSE, NULL);
            if (!NT_SUCCESS(Status)) break;
            continue;
        }
        if (!Pending)
        {
            RtlReleaseSRWLockExclusive(&Object->Lock);
            Notify = FALSE;
            break;
        }
        ReadLength = (ULONG)min(Channel->Credit, ZP_TUNNEL_CHUNK_SIZE);
        ReservedLength = ReadLength;
        Channel->Credit -= ReservedLength;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Received = recv(Channel->Socket, Add2Ptr(Body, sizeof(ULONGLONG)), ReadLength, 0);
        if (Received == SOCKET_ERROR)
        {
            Completion = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
            break;
        }
        if (Received == 0)
        {
            Completion = ZpStatus_Make(ZpStatusNone, 0);
            break;
        }
        Status = ZpMessage_EncodeChannelData(Channel->Header.ChannelId,
                                             Add2Ptr(Body, sizeof(ULONGLONG)),
                                             (ULONG)Received,
                                             Body,
                                             sizeof(ULONGLONG) + ZP_TUNNEL_CHUNK_SIZE,
                                             &BodyLength);
        if (!NT_SUCCESS(Status)) break;
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Channel->Credit += ReservedLength - Received;
        Status = Channel->Header.Pending ?
                     ZpTunnel_SendLocked(Object, ZpMessageChannelData, Body, BodyLength) : STATUS_SUCCESS;
        Removed = !NT_SUCCESS(Status) && ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        RtlReleaseSRWLockExclusive(&Object->Lock);
        if (Removed)
        {
            Notify = FALSE;
            ZpClientLocalChannel_Release(&Channel->Header);
            break;
        }
    }
    Mem_Free(Body);
    if (Completion.Type == ZpStatusNone && !NT_SUCCESS(Status)) Completion = ZpStatus_FromNtStatus(Status);
    ZpTunnel_FinishWorker(Channel, Completion, Notify);
    return Status;
}

static
NTSTATUS
ZpTunnel_StartWorker(
    _Inout_ PZP_CLIENT_TUNNEL_CHANNEL Channel)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending || Channel->WorkerActive)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Channel->WorkerActive = TRUE;
    ZpClientLocalChannel_AddRef(&Channel->Header);
    Object->CallbackCount++;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    Status = PS_CreateThread(NtCurrentProcess(), TRUE, ZpTunnel_ReceiveThread, Channel, &Channel->WorkerThread, NULL);
    if (NT_SUCCESS(Status)) Status = NtResumeThread(Channel->WorkerThread, NULL);
    if (!NT_SUCCESS(Status)) ZpTunnel_FinishWorker(Channel, ZpStatus_FromNtStatus(Status), TRUE);
    return Status;
}

static
NTSTATUS
ZpTunnel_ChannelWindow(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ULONG CreditBytes)
{
    PZP_CLIENT_TUNNEL_CHANNEL Channel = (PZP_CLIENT_TUNNEL_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending || MAXULONGLONG - Channel->Credit < CreditBytes)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->Credit += CreditBytes;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return NtSetEvent(Channel->CreditEvent, NULL);
}

static
NTSTATUS
ZpTunnel_ChannelData(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message)
{
    PZP_CLIENT_TUNNEL_CHANNEL Channel = (PZP_CLIENT_TUNNEL_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    ULONG Offset = 0;
    INT Sent;
    NTSTATUS Status = STATUS_SUCCESS;
    ZP_STATUS Completion = ZpStatus_Make(ZpStatusNone, 0);
    LOGICAL Removed = FALSE;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending || Message->Data.Length > Channel->ReceiveCredit)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->ReceiveCredit -= Message->Data.Length;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    RtlAcquireSRWLockExclusive(&Channel->SendLock);
    while (Offset < Message->Data.Length)
    {
        Sent = send(Channel->Socket,
                    Add2Ptr(Message->Data.Buffer, Offset),
                    Message->Data.Length - Offset,
                    0);
        if (Sent == SOCKET_ERROR)
        {
            Completion = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
            break;
        }
        Offset += Sent;
    }
    RtlReleaseSRWLockExclusive(&Channel->SendLock);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Channel->Header.Pending && Completion.Type == ZpStatusNone)
    {
        Status = ZpTunnel_SendWindowLocked(Channel, Message->Data.Length);
        if (!NT_SUCCESS(Status))
        {
            Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        }
    }
    if (Channel->Header.Pending && Completion.Type != ZpStatusNone)
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        Status = ZpTunnel_SendCloseLocked(Channel, Completion);
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        ZpTunnel_CloseSocket(Channel);
        ZpClientLocalChannel_Release(&Channel->Header);
    }
    return Status;
}

static
NTSTATUS
ZpTunnel_ChannelClose(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_TUNNEL_CHANNEL Channel = (PZP_CLIENT_TUNNEL_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed;

    UNREFERENCED_PARAMETER(Status);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (!Removed) return STATUS_PROTOCOL_UNREACHABLE;
    ZpTunnel_CloseSocket(Channel);
    ZpClientLocalChannel_Release(&Channel->Header);
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpTunnel_Open(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ USHORT Port,
    _Outptr_ PZP_CLIENT_TUNNEL_CHANNEL* OpenedChannel)
{
    WSADATA WsaData;
    SOCKADDR_IN Address = { 0 };
    PZP_CLIENT_TUNNEL_CHANNEL Channel;
    INT Result = 0;
    NTSTATUS Status;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (Result != 0) return ZpStatus_FromCode(ZpStatusWinsock, Result);
    Channel = Mem_Alloc(sizeof(*Channel));
    if (Channel == NULL)
    {
        WSACleanup();
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    RtlZeroMemory(Channel, sizeof(*Channel));
    Channel->Socket = INVALID_SOCKET;
    Status = NtCreateEvent(&Channel->CreditEvent,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           NULL,
                           SynchronizationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Channel->Socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (Channel->Socket == INVALID_SOCKET)
    {
        Result = WSAGetLastError();
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }
    Address.sin_family = AF_INET;
    Address.sin_port = htons(Port);
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(Channel->Socket, (SOCKADDR*)&Address, sizeof(Address)) == SOCKET_ERROR)
    {
        Result = WSAGetLastError();
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }
    Status = ZpClientLocalChannel_Insert(Client,
                                         &Channel->Header,
                                         ZP_TUNNEL_MODULE_ID,
                                         ZpTunnel_ChannelData,
                                         ZpTunnel_ChannelWindow,
                                         ZpTunnel_ChannelClose,
                                         ZpTunnel_ChannelAbort,
                                         ZpTunnel_ChannelDestroy);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    *OpenedChannel = Channel;
    return ZpStatus_Make(ZpStatusNone, 0);

Cleanup:
    if (Channel->Socket != INVALID_SOCKET) closesocket(Channel->Socket);
    if (Channel->CreditEvent != NULL) NtClose(Channel->CreditEvent);
    Mem_Free(Channel);
    WSACleanup();
    return Result != 0 ? ZpStatus_FromCode(ZpStatusWinsock, Result) : ZpStatus_FromNtStatus(Status);
}

ZP_STATUS
ZpTunnel_Execute(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ USHORT OperationId,
    _In_reads_bytes_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_TUNNEL_CHANNEL* Channel)
{
    PZP_CLIENT_TUNNEL_CHANNEL OpenedChannel = NULL;
    USHORT Port;
    NTSTATUS Status;
    ZP_STATUS Result;

    *Response = NULL;
    *ResponseLength = 0;
    *Channel = NULL;
    if (OperationId != ZP_TUNNEL_OPERATION_OPEN) return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    Status = ZpTunnel_DecodeOpen(Request, RequestLength, &Port);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Result = ZpTunnel_Open(Client, Port, &OpenedChannel);
    if (!ZpStatus_IsSuccess(Result)) return Result;
    Status = ZpTunnel_EncodeOpenResponse(OpenedChannel->Header.ChannelId, NULL, 0, ResponseLength);
    *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
    if (NT_SUCCESS(Status) && *Response == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpTunnel_EncodeOpenResponse(OpenedChannel->Header.ChannelId,
                                             *Response,
                                             *ResponseLength,
                                             ResponseLength);
    }
    if (!NT_SUCCESS(Status))
    {
        ZpTunnel_CommitChannel(OpenedChannel, FALSE);
        Mem_Free(*Response);
        *Response = NULL;
        return ZpStatus_FromNtStatus(Status);
    }
    *Channel = OpenedChannel;
    return Result;
}

VOID
ZpTunnel_CommitChannel(
    _Inout_ PZP_CLIENT_TUNNEL_CHANNEL Channel,
    _In_ LOGICAL ResponseSent)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed = FALSE, StartWorker = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ResponseSent)
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    }
    else
    {
        Status = ZpTunnel_SendWindowLocked(Channel, ZP_TUNNEL_WINDOW_SIZE);
        if (!NT_SUCCESS(Status)) Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        else StartWorker = TRUE;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
    else if (StartWorker) ZpTunnel_StartWorker(Channel);
}
