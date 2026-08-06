#include "Tcp.h"

#include "../Server.inl"
#include "../Core/Session.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef struct _ZP_SERVER_TCP_CONNECTION
{
    ZP_CONNECTION_OBJECT Public;
    LIST_ENTRY ListEntry;
    PZP_SERVER_TCP_TRANSPORT Transport;
    ZP_SERVER_SESSION Session;
    ZP_TCP_CONNECTION Tcp;
} ZP_SERVER_TCP_CONNECTION, *PZP_SERVER_TCP_CONNECTION;

static
VOID
ZpServerTcp_TryCompleteStop(
    _Inout_ PZP_SERVER_TCP_TRANSPORT Transport)
{
    LOGICAL Complete;

    RtlAcquireSRWLockExclusive(&Transport->Owner->Lock);
    Complete = Transport->Stopping &&
               Transport->AcceptStopped &&
               Transport->ActiveConnectionCount == 0;
    if (Complete)
    {
        Transport->Stopping = FALSE;
    }
    RtlReleaseSRWLockExclusive(&Transport->Owner->Lock);
    if (Complete)
    {
        PostQueuedCompletionStatus(Transport->CompletionPort, 0, 0, NULL);
        ZpServer_TransportStopped((ZP_SERVER_HANDLE)Transport->Owner,
                                  ZpTransportTcp,
                                  ZpStatus_FromNtStatus(STATUS_SUCCESS));
    }
}

static
NTSTATUS
NTAPI
ZpServerTcp_Disconnect(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_STATUS Status)
{
    PZP_SERVER_TCP_CONNECTION TcpConnection = CONTAINING_RECORD(
        Connection,
        ZP_SERVER_TCP_CONNECTION,
        Public);

    ZpTcpConnection_Close(&TcpConnection->Tcp, Status);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
ZpServerTcp_Send(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_SEND_FLAGS SendFlags,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength)
{
    PZP_SERVER_TCP_CONNECTION TcpConnection = CONTAINING_RECORD(
        Connection,
        ZP_SERVER_TCP_CONNECTION,
        Public);

    return ZpTcpConnection_SendFrame(&TcpConnection->Tcp,
                                     &TcpConnection->Session.Connection,
                                     SendFlags,
                                     MessageType,
                                     Body,
                                     BodyLength,
                                     Payload,
                                     PayloadLength);
}

static
VOID
NTAPI
ZpServerTcp_DestroyConnection(
    _Inout_ PZP_CONNECTION_OBJECT Connection)
{
    PZP_SERVER_TCP_CONNECTION TcpConnection = CONTAINING_RECORD(
        Connection,
        ZP_SERVER_TCP_CONNECTION,
        Public);
    PZP_SERVER_TCP_TRANSPORT Transport = TcpConnection->Transport;

    ZpServerSession_Uninitialize(&TcpConnection->Session);
    RtlAcquireSRWLockExclusive(&Transport->Owner->Lock);
    RemoveEntryList(&TcpConnection->ListEntry);
    Transport->ActiveConnectionCount--;
    RtlReleaseSRWLockExclusive(&Transport->Owner->Lock);
    Mem_Free(TcpConnection);
    ZpServerTcp_TryCompleteStop(Transport);
}

static
ZP_STATUS
NTAPI
ZpServerTcp_Connected(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_TCP_CONNECTION TcpConnection = Context;

    UNREFERENCED_PARAMETER(Connection);
    ZpServerConnection_SetPhase(&TcpConnection->Public,
                                ZpConnectionPhaseAuthenticating);
    ZpServer_NotifyConnection((ZP_SERVER_HANDLE)TcpConnection->Transport->Owner,
                              (ZP_CONNECTION_HANDLE)&TcpConnection->Public,
                              ZpConnectionPhaseAuthenticating,
                              ZpStatus_FromNtStatus(STATUS_SUCCESS));
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
NTSTATUS
NTAPI
ZpServerTcp_Receive(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_TCP_CONNECTION TcpConnection = Context;

    UNREFERENCED_PARAMETER(Connection);
    return ZpServerSession_Receive(&TcpConnection->Session, Data, DataLength);
}

static
VOID
NTAPI
ZpServerTcp_Closed(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_TCP_CONNECTION TcpConnection = Context;

    UNREFERENCED_PARAMETER(Connection);
    ZpServerConnection_Close(&TcpConnection->Public, Status);
    ZpServer_NotifyConnection((ZP_SERVER_HANDLE)TcpConnection->Transport->Owner,
                              (ZP_CONNECTION_HANDLE)&TcpConnection->Public,
                              ZpConnectionPhaseClosed,
                              Status);
    ZpConnection_Release((ZP_CONNECTION_HANDLE)&TcpConnection->Public);
}

static
VOID
ZpServerTcp_Accept(
    _Inout_ PZP_SERVER_TCP_TRANSPORT Transport,
    _In_ SOCKET Listener)
{
    PZP_SERVER_TCP_CONNECTION Connection;
    SOCKADDR_INET RemoteAddress;
    SOCKET Socket;
    INT RemoteAddressLength;
    ULONG NonBlocking = FALSE;
    NTSTATUS NtStatus;
    ZP_STATUS Status;

    for (;;)
    {
        RemoteAddressLength = sizeof(RemoteAddress);
        Socket = accept(Listener, (SOCKADDR*)&RemoteAddress, &RemoteAddressLength);
        if (Socket == INVALID_SOCKET)
        {
            return;
        }
        if (ioctlsocket(Socket, FIONBIO, &NonBlocking) == SOCKET_ERROR)
        {
            closesocket(Socket);
            continue;
        }
        Connection = Mem_Alloc(sizeof(*Connection));
        if (Connection == NULL)
        {
            closesocket(Socket);
            continue;
        }
        RtlZeroMemory(Connection, sizeof(*Connection));
        Connection->Transport = Transport;
        NtStatus = ZpServerConnection_Initialize(
            &Connection->Public,
            ZpTransportTcp,
            Transport->Owner->Config.MaxRequestsPerConnection,
            Transport->Owner->Config.MaxChannelsPerConnection,
            (SOCKADDR*)&RemoteAddress,
            RemoteAddressLength,
            ZpServerTcp_Send,
            ZpServerTcp_Disconnect,
            ZpServerTcp_DestroyConnection);
        if (NT_SUCCESS(NtStatus))
        {
            NtStatus = ZpServerSession_Initialize(&Connection->Session,
                                                  Transport->Owner,
                                                  &Connection->Public);
        }
        if (!NT_SUCCESS(NtStatus))
        {
            if (Connection->Public.Send != NULL)
            {
                RtlDeleteCriticalSection(&Connection->Public.RequestSendLock);
            }
            Mem_Free(Connection);
            closesocket(Socket);
            continue;
        }
        RtlAcquireSRWLockExclusive(&Transport->Owner->Lock);
        InsertTailList(&Transport->Connections, &Connection->ListEntry);
        Transport->ActiveConnectionCount++;
        RtlReleaseSRWLockExclusive(&Transport->Owner->Lock);
        ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Transport->Owner,
                                  (ZP_CONNECTION_HANDLE)&Connection->Public,
                                  ZpConnectionPhaseConnecting,
                                  ZpStatus_FromNtStatus(STATUS_SUCCESS));
        Status = ZpTcpConnection_Initialize(&Connection->Tcp,
                                            Transport->CompletionPort,
                                            Socket,
                                            ZpTlsServer,
                                            &Transport->Credential,
                                            NULL,
                                            ZpServerTcp_Connected,
                                            ZpServerTcp_Receive,
                                            ZpServerTcp_Closed,
                                            Connection);
        if (!ZpStatus_IsSuccess(Status))
        {
            continue;
        }
    }
}

static
DWORD
WINAPI
ZpServerTcp_AcceptThread(
    _In_ PVOID Context)
{
    PZP_SERVER_TCP_TRANSPORT Transport = Context;
    INT Count;
    ULONG Index;

    while (WaitForSingleObject(Transport->StopEvent, 0) != WAIT_OBJECT_0)
    {
        Count = WSAPoll(Transport->PollDescriptors,
                        Transport->ListenerCount,
                        100);
        if (Count <= 0)
        {
            continue;
        }
        for (Index = 0; Index < Transport->ListenerCount; Index++)
        {
            if ((Transport->PollDescriptors[Index].revents & POLLRDNORM) != 0)
            {
                ZpServerTcp_Accept(Transport,
                                   Transport->PollDescriptors[Index].fd);
            }
            Transport->PollDescriptors[Index].revents = 0;
        }
    }
    RtlAcquireSRWLockExclusive(&Transport->Owner->Lock);
    Transport->AcceptStopped = TRUE;
    RtlReleaseSRWLockExclusive(&Transport->Owner->Lock);
    ZpServerTcp_TryCompleteStop(Transport);
    return 0;
}

static
VOID
ZpServerTcp_CloseListeners(
    _Inout_ PZP_SERVER_TCP_TRANSPORT Transport)
{
    ULONG Index;

    for (Index = 0; Index < Transport->ListenerCount; Index++)
    {
        if (Transport->Listeners[Index] != INVALID_SOCKET)
        {
            closesocket(Transport->Listeners[Index]);
            Transport->Listeners[Index] = INVALID_SOCKET;
        }
    }
    Transport->ListenerCount = 0;
}

static
ZP_STATUS
NTAPI
ZpServerTcp_Start(
    _In_opt_ PVOID Context,
    _In_ ULONG EndpointIndex)
{
    PZP_SERVER_TCP_TRANSPORT Transport = Context;
    SOCKADDR_STORAGE Address;
    PCCERT_CONTEXT Certificates[ZP_DEPLOYMENT_MAX_COUNT];
    WSADATA WsaData;
    SOCKET Socket;
    ULONG Index, Exclusive = TRUE, NonBlocking = TRUE;
    INT AddressLength, Error;
    ZP_STATUS Status;

    UNREFERENCED_PARAMETER(EndpointIndex);
    ZpServerTcp_Uninitialize(Transport);
    ResetEvent(Transport->StopEvent);
    InitializeListHead(&Transport->Connections);
    Error = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (Error != 0)
    {
        return ZpStatus_FromCode(ZpStatusWinsock, Error);
    }
    Transport->WsaInitialized = TRUE;
    for (Index = 0; Index < Transport->Owner->Config.DeploymentCount; Index++)
    {
        Certificates[Index] = Transport->Owner->Config.Deployments[Index].Certificate;
    }
    Status = ZpTls_AcquireServerCredentials(
        &Transport->Credential,
        Certificates,
        Transport->Owner->Config.DeploymentCount);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Failed;
    }
    Transport->CredentialInitialized = TRUE;
    Transport->CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
                                                        NULL,
                                                        0,
                                                        0);
    if (Transport->CompletionPort == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Failed;
    }
    Transport->WorkerThread = CreateThread(NULL,
                                           0,
                                           ZpTcp_Worker,
                                           Transport->CompletionPort,
                                           0,
                                           NULL);
    if (Transport->WorkerThread == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Failed;
    }
    for (Index = 0; Index < Transport->Owner->Config.ListenerCount; Index++)
    {
        if (Transport->Owner->Config.Listeners[Index].Transport != ZpTransportTcp)
        {
            continue;
        }
        Status = ZpSocket_ResolveAddress(Transport->Owner->Config.Listeners[Index].Host,
                                         Transport->Owner->Config.Listeners[Index].Port,
                                         TRUE,
                                         SOCK_STREAM,
                                         IPPROTO_TCP,
                                         &Address,
                                         &AddressLength);
        if (!ZpStatus_IsSuccess(Status))
        {
            goto Failed;
        }
        Socket = WSASocketW(Address.ss_family,
                            SOCK_STREAM,
                            IPPROTO_TCP,
                            NULL,
                            0,
                            WSA_FLAG_OVERLAPPED);
        if (Socket == INVALID_SOCKET)
        {
            Status = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
            goto Failed;
        }
        if (setsockopt(Socket,
                       SOL_SOCKET,
                       SO_EXCLUSIVEADDRUSE,
                       (PCSTR)&Exclusive,
                       sizeof(Exclusive)) == SOCKET_ERROR ||
            bind(Socket, (SOCKADDR*)&Address, AddressLength) == SOCKET_ERROR ||
            listen(Socket, SOMAXCONN) == SOCKET_ERROR ||
            ioctlsocket(Socket, FIONBIO, &NonBlocking) == SOCKET_ERROR)
        {
            Error = WSAGetLastError();
            closesocket(Socket);
            Status = ZpStatus_FromCode(ZpStatusWinsock, Error);
            goto Failed;
        }
        Transport->Listeners[Transport->ListenerCount] = Socket;
        Transport->PollDescriptors[Transport->ListenerCount].fd = Socket;
        Transport->PollDescriptors[Transport->ListenerCount].events = POLLRDNORM;
        Transport->PollDescriptors[Transport->ListenerCount].revents = 0;
        Transport->ListenerCount++;
    }
    Transport->AcceptThread = CreateThread(NULL,
                                           0,
                                           ZpServerTcp_AcceptThread,
                                           Transport,
                                           0,
                                           NULL);
    if (Transport->AcceptThread == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Failed;
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);

Failed:
    SetEvent(Transport->StopEvent);
    ZpServerTcp_CloseListeners(Transport);
    if (Transport->CompletionPort != NULL)
    {
        PostQueuedCompletionStatus(Transport->CompletionPort, 0, 0, NULL);
    }
    ZpServerTcp_Uninitialize(Transport);
    return Status;
}

static
VOID
NTAPI
ZpServerTcp_Stop(
    _In_opt_ PVOID Context)
{
    PZP_SERVER_TCP_TRANSPORT Transport = Context;
    PLIST_ENTRY Entry;

    RtlAcquireSRWLockExclusive(&Transport->Owner->Lock);
    Transport->Stopping = TRUE;
    RtlReleaseSRWLockExclusive(&Transport->Owner->Lock);
    SetEvent(Transport->StopEvent);
    ZpServerTcp_CloseListeners(Transport);
    RtlAcquireSRWLockShared(&Transport->Owner->Lock);
    for (Entry = Transport->Connections.Flink;
         Entry != &Transport->Connections;
         Entry = Entry->Flink)
    {
        PZP_SERVER_TCP_CONNECTION Connection = CONTAINING_RECORD(
            Entry,
            ZP_SERVER_TCP_CONNECTION,
            ListEntry);

        ZpTcpConnection_Close(&Connection->Tcp,
                              ZpStatus_FromNtStatus(STATUS_SUCCESS));
    }
    RtlReleaseSRWLockShared(&Transport->Owner->Lock);
    ZpServerTcp_TryCompleteStop(Transport);
}

static const ZP_TRANSPORT_OPERATIONS ZpServerTcpOperations = {
    ZpServerTcp_Start,
    ZpServerTcp_Stop,
    NULL
};

VOID
ZpServerTcp_Configure(
    _Inout_ PZP_SERVER_OBJECT Object)
{
    ULONG Index;

    Object->TcpTransport.Owner = Object;
    InitializeListHead(&Object->TcpTransport.Connections);
    for (Index = 0; Index < ZP_LISTENER_MAX_COUNT; Index++)
    {
        Object->TcpTransport.Listeners[Index] = INVALID_SOCKET;
    }
    Object->TcpTransport.StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Object->TcpTransport.StopEvent == NULL)
    {
        return;
    }
    for (Index = 0; Index < Object->Config.ListenerCount; Index++)
    {
        if (Object->Config.Listeners[Index].Transport == ZpTransportTcp)
        {
            ZpServer_SetTransport((ZP_SERVER_HANDLE)Object,
                                  ZpTransportTcp,
                                  &ZpServerTcpOperations,
                                  &Object->TcpTransport);
            return;
        }
    }
}

VOID
ZpServerTcp_Uninitialize(
    _Inout_ PZP_SERVER_TCP_TRANSPORT Transport)
{
    if (Transport->StopEvent != NULL)
    {
        SetEvent(Transport->StopEvent);
    }
    ZpServerTcp_CloseListeners(Transport);
    if (Transport->AcceptThread != NULL)
    {
        WaitForSingleObject(Transport->AcceptThread, INFINITE);
        CloseHandle(Transport->AcceptThread);
        Transport->AcceptThread = NULL;
    }
    if (Transport->CompletionPort != NULL && Transport->WorkerThread != NULL)
    {
        PostQueuedCompletionStatus(Transport->CompletionPort, 0, 0, NULL);
        WaitForSingleObject(Transport->WorkerThread, INFINITE);
        CloseHandle(Transport->WorkerThread);
        Transport->WorkerThread = NULL;
    }
    if (Transport->CompletionPort != NULL)
    {
        CloseHandle(Transport->CompletionPort);
        Transport->CompletionPort = NULL;
    }
    if (Transport->CredentialInitialized)
    {
        ZpTls_FreeCredentials(&Transport->Credential);
        Transport->CredentialInitialized = FALSE;
    }
    if (Transport->WsaInitialized)
    {
        WSACleanup();
        Transport->WsaInitialized = FALSE;
    }
    Transport->Stopping = FALSE;
    Transport->AcceptStopped = FALSE;
    if (Transport->StopEvent != NULL)
    {
        ResetEvent(Transport->StopEvent);
    }
}
