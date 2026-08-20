#include "Client.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <Ws2tcpip.h>

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"

#pragma comment(lib, "Ws2_32.lib")

#define ZP_TUNNEL_CHUNK_SIZE 0x00010000UL
#define ZP_TUNNEL_WINDOW_SIZE 0x00100000UL
#define ZP_TUNNEL_DATAGRAM_FRAME_SIZE (ZP_TUNNEL_DATAGRAM_MAX_SIZE + 1)

struct _ZP_CLIENT_TUNNEL_CHANNEL
{
    ZP_CLIENT_LOCAL_CHANNEL Header;
    RTL_SRWLOCK SendLock;
    volatile LONG Closed;
    LOGICAL WorkerActive;
    ULONGLONG Credit;
    ULONGLONG ReceiveCredit;
    USHORT Protocol;
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
    BYTE Body[sizeof(ULONG) + ZP_STATUS_WIRE_SIZE];
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
    BYTE Body[2 * sizeof(ULONG)];
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
    PBYTE Body = Mem_Alloc(sizeof(ULONG) + ZP_TUNNEL_CHUNK_SIZE);
    ZP_STATUS Completion = ZpStatus_Make(ZpStatusNone, 0);
    ULONG ReadLength, ReservedLength, DataLength, BodyLength;
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
        if (Pending && (Channel->Credit == 0 ||
                        Channel->Protocol == ZP_TUNNEL_PROTOCOL_UDP &&
                        Channel->Credit < ZP_TUNNEL_DATAGRAM_FRAME_SIZE))
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
        ReadLength = Channel->Protocol == ZP_TUNNEL_PROTOCOL_UDP ?
                         ZP_TUNNEL_DATAGRAM_MAX_SIZE : (ULONG)min(Channel->Credit, ZP_TUNNEL_CHUNK_SIZE);
        ReservedLength = Channel->Protocol == ZP_TUNNEL_PROTOCOL_UDP ?
                             ZP_TUNNEL_DATAGRAM_FRAME_SIZE : ReadLength;
        Channel->Credit -= ReservedLength;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Received = recv(Channel->Socket,
                        Add2Ptr(Body,
                                sizeof(ULONG) +
                                    (Channel->Protocol == ZP_TUNNEL_PROTOCOL_UDP ? sizeof(BYTE) : 0)),
                        ReadLength,
                        0);
        if (Received == SOCKET_ERROR)
        {
            Completion = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
            break;
        }
        if (Received == 0 && Channel->Protocol == ZP_TUNNEL_PROTOCOL_TCP)
        {
            Completion = ZpStatus_Make(ZpStatusNone, 0);
            break;
        }
        DataLength = (ULONG)Received;
        if (Channel->Protocol == ZP_TUNNEL_PROTOCOL_UDP)
        {
            *(PBYTE)Add2Ptr(Body, sizeof(ULONG)) = 0;
            DataLength++;
        }
        Status = ZpMessage_EncodeChannelData(Channel->Header.ChannelId,
                                             Add2Ptr(Body, sizeof(ULONG)),
                                             DataLength,
                                             Body,
                                             sizeof(ULONG) + ZP_TUNNEL_CHUNK_SIZE,
                                             &BodyLength);
        if (!NT_SUCCESS(Status)) break;
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Channel->Credit += ReservedLength - DataLength;
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
    if (Channel->Protocol == ZP_TUNNEL_PROTOCOL_UDP)
    {
        if (Message->Data.Length == 0 || Message->Data.Length > ZP_TUNNEL_DATAGRAM_FRAME_SIZE ||
            *(PBYTE)Message->Data.Buffer != 0)
        {
            Completion = ZpStatus_FromNtStatus(STATUS_INVALID_BUFFER_SIZE);
        }
        else
        {
            Sent = send(Channel->Socket,
                        Add2Ptr(Message->Data.Buffer, sizeof(BYTE)),
                        Message->Data.Length - sizeof(BYTE),
                        0);
            if (Sent == SOCKET_ERROR)
            {
                Completion = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
            }
            else if ((ULONG)Sent != Message->Data.Length - sizeof(BYTE))
            {
                Completion = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
            }
        }
    }
    while (Channel->Protocol == ZP_TUNNEL_PROTOCOL_TCP && Offset < Message->Data.Length)
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
INT
ZpTunnel_ConnectSocket(
    _In_ SOCKET Socket,
    _In_ const SOCKADDR* Address,
    _In_ INT AddressLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ULONGLONG StartTickCount)
{
    WSAPOLLFD Poll = { Socket, POLLWRNORM, 0 };
    ULONG Nonblocking = TRUE;
    INT Result, Error, ErrorLength = sizeof(Error), PollTimeout;
    ULONGLONG Elapsed;

    if (ioctlsocket(Socket, FIONBIO, &Nonblocking) == SOCKET_ERROR) return WSAGetLastError();
    if (connect(Socket, Address, AddressLength) == SOCKET_ERROR)
    {
        Result = WSAGetLastError();
        if (Result != WSAEWOULDBLOCK) return Result;
        if (TimeoutMilliseconds == 0)
        {
            PollTimeout = -1;
        }
        else
        {
            Elapsed = GetTickCount64() - StartTickCount;
            if (Elapsed >= TimeoutMilliseconds) return WSAETIMEDOUT;
            PollTimeout = (INT)min(TimeoutMilliseconds - Elapsed, MAXINT);
        }
        Result = WSAPoll(&Poll, 1, PollTimeout);
        if (Result == 0) return WSAETIMEDOUT;
        if (Result == SOCKET_ERROR) return WSAGetLastError();
        if (getsockopt(Socket, SOL_SOCKET, SO_ERROR, (PSTR)&Error, &ErrorLength) == SOCKET_ERROR)
        {
            return WSAGetLastError();
        }
        if (Error != ERROR_SUCCESS) return Error;
    }
    Nonblocking = FALSE;
    return ioctlsocket(Socket, FIONBIO, &Nonblocking) == 0 ? ERROR_SUCCESS : WSAGetLastError();
}

static
ZP_STATUS
ZpTunnel_Connect(
    _In_ PCZP_STRING_VIEW Host,
    _In_ USHORT Port,
    _In_ USHORT Protocol,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ SOCKET* ConnectedSocket)
{
    ADDRINFOW Hints = { 0 }, *Addresses, *Address;
    PWSTR HostName;
    SOCKET Socket;
    INT Result;
    ULONGLONG StartTickCount = GetTickCount64();

    HostName = Mem_Alloc(((SIZE_T)Host->Length + 1) * sizeof(WCHAR));
    if (HostName == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    RtlCopyMemory(HostName, Host->Buffer, (SIZE_T)Host->Length * sizeof(WCHAR));
    HostName[Host->Length] = UNICODE_NULL;
    Hints.ai_family = AF_UNSPEC;
    Hints.ai_socktype = Protocol == ZP_TUNNEL_PROTOCOL_TCP ? SOCK_STREAM : SOCK_DGRAM;
    Hints.ai_protocol = Protocol == ZP_TUNNEL_PROTOCOL_TCP ? IPPROTO_TCP : IPPROTO_UDP;
    Result = GetAddrInfoW(HostName, NULL, &Hints, &Addresses);
    Mem_Free(HostName);
    if (Result != 0) return ZpStatus_FromCode(ZpStatusWinsock, Result);
    Result = WSAEHOSTUNREACH;
    for (Address = Addresses; Address != NULL; Address = Address->ai_next)
    {
        if (Address->ai_family == AF_INET)
        {
            ((SOCKADDR_IN*)Address->ai_addr)->sin_port = htons(Port);
        }
        else if (Address->ai_family == AF_INET6)
        {
            ((SOCKADDR_IN6*)Address->ai_addr)->sin6_port = htons(Port);
        }
        else
        {
            continue;
        }
        Socket = WSASocketW(Address->ai_family,
                            Hints.ai_socktype,
                            Hints.ai_protocol,
                            NULL,
                            0,
                            WSA_FLAG_OVERLAPPED);
        if (Socket == INVALID_SOCKET)
        {
            Result = WSAGetLastError();
            continue;
        }
        Result = ZpTunnel_ConnectSocket(Socket,
                                       Address->ai_addr,
                                       (INT)Address->ai_addrlen,
                                       TimeoutMilliseconds,
                                       StartTickCount);
        if (Result == ERROR_SUCCESS)
        {
            *ConnectedSocket = Socket;
            break;
        }
        closesocket(Socket);
    }
    FreeAddrInfoW(Addresses);
    return ZpStatus_FromCode(ZpStatusWinsock, Result);
}

static
ZP_STATUS
ZpTunnel_Open(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ const ZP_TUNNEL_OPEN_VIEW* View,
    _In_ ULONG TimeoutMilliseconds,
    _Outptr_ PZP_CLIENT_TUNNEL_CHANNEL* OpenedChannel)
{
    WSADATA WsaData;
    PZP_CLIENT_TUNNEL_CHANNEL Channel;
    ZP_STATUS Result = ZpStatus_Make(ZpStatusNone, 0);
    INT WinsockStatus;
    NTSTATUS Status;

    WinsockStatus = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (WinsockStatus != ERROR_SUCCESS) return ZpStatus_FromCode(ZpStatusWinsock, WinsockStatus);
    Channel = Mem_Alloc(sizeof(*Channel));
    if (Channel == NULL)
    {
        WSACleanup();
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    RtlZeroMemory(Channel, sizeof(*Channel));
    Channel->Socket = INVALID_SOCKET;
    Channel->Protocol = View->Protocol;
    Status = NtCreateEvent(&Channel->CreditEvent,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           NULL,
                           SynchronizationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Result = ZpTunnel_Connect(&View->Host,
                              View->Port,
                              View->Protocol,
                              TimeoutMilliseconds,
                              &Channel->Socket);
    if (!ZpStatus_IsSuccess(Result)) goto Cleanup;
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
    return !ZpStatus_IsSuccess(Result) ? Result : ZpStatus_FromNtStatus(Status);
}

ZP_STATUS
ZpTunnel_Execute(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _In_ ULONG TimeoutMilliseconds,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_TUNNEL_CHANNEL* Channel)
{
    PZP_CLIENT_TUNNEL_CHANNEL OpenedChannel = NULL;
    ZP_TUNNEL_OPEN_VIEW View;
    NTSTATUS Status;
    ZP_STATUS Result;

    *Response = NULL;
    *ResponseLength = 0;
    *Channel = NULL;
    if (OperationId != ZP_TUNNEL_OPERATION_OPEN) return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    Status = ZpTunnel_DecodeOpen(Request, RequestLength, &View);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Result = ZpTunnel_Open(Client, &View, TimeoutMilliseconds, &OpenedChannel);
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
