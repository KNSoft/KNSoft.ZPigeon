#include "Udp.h"

#include "../Server.inl"
#include "../Core/Session.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#define ZP_SERVER_UDP_MAX_CONNECTIONS 1024

typedef struct _ZP_SERVER_UDP_CONNECTION
{
    ZP_CONNECTION_OBJECT Public;
    LIST_ENTRY ListEntry;
    LIST_ENTRY BucketEntry;
    PZP_SERVER_UDP_TRANSPORT Transport;
    ZP_SERVER_SESSION Session;
    ZP_UDP_CONNECTION Udp;
    volatile LONG Closing;
} ZP_SERVER_UDP_CONNECTION, *PZP_SERVER_UDP_CONNECTION;

static
ULONG
ZpServerUdp_GetConnectionBucket(
    _In_ ULONGLONG ConnectionId)
{
    return ((ULONG)ConnectionId ^ (ULONG)(ConnectionId >> 32)) &
           (ZP_SERVER_UDP_CONNECTION_BUCKET_COUNT - 1);
}

static
VOID
ZpServerUdp_InitializeConnectionLists(
    _Out_ PZP_SERVER_UDP_TRANSPORT Transport)
{
    ULONG Index;

    InitializeListHead(&Transport->Connections);
    for (Index = 0; Index < ZP_SERVER_UDP_CONNECTION_BUCKET_COUNT; Index++)
    {
        InitializeListHead(&Transport->ConnectionBuckets[Index]);
    }
}

static
VOID
ZpServerUdp_TryCompleteStop(
    _Inout_ PZP_SERVER_UDP_TRANSPORT Transport)
{
    LOGICAL Complete;

    RtlAcquireSRWLockExclusive(&Transport->Owner->Lock);
    Complete = Transport->Stopping &&
               Transport->ReceiveStopped &&
               Transport->ActiveConnectionCount == 0;
    if (Complete)
    {
        Transport->Stopping = FALSE;
    }
    RtlReleaseSRWLockExclusive(&Transport->Owner->Lock);
    if (Complete)
    {
        ZpServer_TransportStopped((ZP_SERVER_HANDLE)Transport->Owner,
                                  ZpTransportUdp,
                                  ZpStatus_FromNtStatus(STATUS_SUCCESS));
    }
}

static
NTSTATUS
NTAPI
ZpServerUdp_Send(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PZP_SERVER_UDP_CONNECTION UdpConnection = CONTAINING_RECORD(
        Connection,
        ZP_SERVER_UDP_CONNECTION,
        Public);
    NTSTATUS Status;

    Status = ZpUdpConnection_SendFrame(&UdpConnection->Udp,
                                       &UdpConnection->Session.Connection,
                                       MessageType,
                                       Body,
                                       BodyLength);
    WSASetEvent(UdpConnection->Transport->SocketEvent);
    return Status;
}

static
VOID
NTAPI
ZpServerUdp_DestroyConnection(
    _Inout_ PZP_CONNECTION_OBJECT Connection)
{
    PZP_SERVER_UDP_CONNECTION UdpConnection = CONTAINING_RECORD(
        Connection,
        ZP_SERVER_UDP_CONNECTION,
        Public);
    PZP_SERVER_UDP_TRANSPORT Transport = UdpConnection->Transport;

    ZpServerSession_Uninitialize(&UdpConnection->Session);
    ZpUdpConnection_Uninitialize(&UdpConnection->Udp);
    RtlAcquireSRWLockExclusive(&Transport->Owner->Lock);
    RemoveEntryList(&UdpConnection->ListEntry);
    RemoveEntryList(&UdpConnection->BucketEntry);
    Transport->ActiveConnectionCount--;
    RtlReleaseSRWLockExclusive(&Transport->Owner->Lock);
    Mem_Free(UdpConnection);
    ZpServerUdp_TryCompleteStop(Transport);
}

static
VOID
ZpServerUdp_CloseConnection(
    _Inout_ PZP_SERVER_UDP_CONNECTION Connection,
    _In_ ZP_STATUS Status)
{
    if (InterlockedCompareExchange(&Connection->Closing, TRUE, FALSE) != FALSE)
    {
        return;
    }
    ZpUdpConnection_Close(&Connection->Udp);
    ZpServerConnection_Close(&Connection->Public, Status);
    ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Connection->Transport->Owner,
                              (ZP_CONNECTION_HANDLE)&Connection->Public,
                              ZpConnectionPhaseClosed,
                              Status);
    ZpConnection_Release((ZP_CONNECTION_HANDLE)&Connection->Public);
}

static
ZP_STATUS
NTAPI
ZpServerUdp_Connected(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_UDP_CONNECTION UdpConnection = Context;

    UNREFERENCED_PARAMETER(Connection);
    ZpServerConnection_SetPhase(&UdpConnection->Public,
                                ZpConnectionPhaseAuthenticating);
    ZpServer_NotifyConnection((ZP_SERVER_HANDLE)UdpConnection->Transport->Owner,
                              (ZP_CONNECTION_HANDLE)&UdpConnection->Public,
                              ZpConnectionPhaseAuthenticating,
                              ZpStatus_FromNtStatus(STATUS_SUCCESS));
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
NTSTATUS
NTAPI
ZpServerUdp_Receive(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_UDP_CONNECTION UdpConnection = Context;

    UNREFERENCED_PARAMETER(Connection);
    return ZpServerSession_Receive(&UdpConnection->Session, Data, DataLength);
}

static
PZP_SERVER_UDP_CONNECTION
ZpServerUdp_FindConnection(
    _Inout_ PZP_SERVER_UDP_TRANSPORT Transport,
    _In_ ULONGLONG ConnectionId,
    _In_ const SOCKADDR_STORAGE* Address,
    _In_ INT AddressLength)
{
    PLIST_ENTRY Bucket, Entry;
    PZP_SERVER_UDP_CONNECTION Connection = NULL;

    Bucket = &Transport->ConnectionBuckets[ZpServerUdp_GetConnectionBucket(ConnectionId)];
    RtlAcquireSRWLockShared(&Transport->Owner->Lock);
    for (Entry = Bucket->Flink;
         Entry != Bucket;
         Entry = Entry->Flink)
    {
        PZP_SERVER_UDP_CONNECTION Candidate = CONTAINING_RECORD(
            Entry,
            ZP_SERVER_UDP_CONNECTION,
            BucketEntry);

        if (Candidate->Udp.ConnectionId == ConnectionId &&
            ZpUdp_IsSameAddress(&Candidate->Udp.RemoteAddress,
                                Candidate->Udp.RemoteAddressLength,
                                Address,
                                AddressLength))
        {
            ZpConnection_AddRef((ZP_CONNECTION_HANDLE)&Candidate->Public);
            Connection = Candidate;
            break;
        }
    }
    RtlReleaseSRWLockShared(&Transport->Owner->Lock);
    return Connection;
}

static
PZP_SERVER_UDP_CONNECTION
ZpServerUdp_CreateConnection(
    _Inout_ PZP_SERVER_UDP_TRANSPORT Transport,
    _In_ SOCKET Listener,
    _In_ ULONGLONG ConnectionId,
    _In_reads_bytes_(AddressLength) const SOCKADDR* Address,
    _In_ INT AddressLength)
{
    PZP_SERVER_UDP_CONNECTION Connection;
    NTSTATUS NtStatus;
    ZP_STATUS Status;

    RtlAcquireSRWLockShared(&Transport->Owner->Lock);
    if (Transport->ActiveConnectionCount == ZP_SERVER_UDP_MAX_CONNECTIONS ||
        Transport->Stopping)
    {
        RtlReleaseSRWLockShared(&Transport->Owner->Lock);
        return NULL;
    }
    RtlReleaseSRWLockShared(&Transport->Owner->Lock);
    Connection = Mem_Alloc(sizeof(*Connection));
    if (Connection == NULL)
    {
        return NULL;
    }
    RtlZeroMemory(Connection, sizeof(*Connection));
    Connection->Transport = Transport;
    NtStatus = ZpServerConnection_Initialize(&Connection->Public,
                                             Transport->Owner->Config.MaxRequestsPerConnection,
                                             Transport->Owner->Config.MaxChannelsPerConnection,
                                             ZpServerUdp_Send,
                                             ZpServerUdp_DestroyConnection);
    if (NT_SUCCESS(NtStatus))
    {
        NtStatus = ZpServerSession_Initialize(&Connection->Session,
                                              Transport->Owner,
                                              &Connection->Public);
    }
    if (NT_SUCCESS(NtStatus))
    {
        Status = ZpUdpConnection_Initialize(&Connection->Udp,
                                            Listener,
                                            Address,
                                            AddressLength,
                                            ConnectionId,
                                            ZpDtlsServer,
                                            &Transport->Credential,
                                            NULL,
                                            ZpServerUdp_Connected,
                                            ZpServerUdp_Receive,
                                            Connection);
        NtStatus = ZpStatus_IsSuccess(Status) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    }
    if (!NT_SUCCESS(NtStatus))
    {
        if (Connection->Session.ConnectionInitialized)
        {
            ZpServerSession_Uninitialize(&Connection->Session);
        }
        if (Connection->Public.Send != NULL)
        {
            RtlDeleteCriticalSection(&Connection->Public.RequestSendLock);
        }
        Mem_Free(Connection);
        return NULL;
    }
    RtlAcquireSRWLockExclusive(&Transport->Owner->Lock);
    InsertTailList(&Transport->Connections, &Connection->ListEntry);
    InsertTailList(&Transport->ConnectionBuckets[ZpServerUdp_GetConnectionBucket(ConnectionId)],
                   &Connection->BucketEntry);
    Transport->ActiveConnectionCount++;
    RtlReleaseSRWLockExclusive(&Transport->Owner->Lock);
    ZpConnection_AddRef((ZP_CONNECTION_HANDLE)&Connection->Public);
    ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Transport->Owner,
                              (ZP_CONNECTION_HANDLE)&Connection->Public,
                              ZpConnectionPhaseConnecting,
                              ZpStatus_FromNtStatus(STATUS_SUCCESS));
    return Connection;
}

static
LOGICAL
ZpServerUdp_ProcessDatagram(
    _Inout_ PZP_SERVER_UDP_TRANSPORT Transport,
    _In_ SOCKET Listener)
{
    BYTE Buffer[ZP_UDP_MAX_DATAGRAM_SIZE];
    SOCKADDR_STORAGE Address;
    PZP_SERVER_UDP_CONNECTION Connection;
    ULONGLONG ConnectionId;
    BYTE Type;
    INT AddressLength = sizeof(Address), Length;
    ZP_STATUS Status;

    Length = recvfrom(Listener,
                      (PCHAR)Buffer,
                      sizeof(Buffer),
                      0,
                      (SOCKADDR*)&Address,
                      &AddressLength);
    if (Length == SOCKET_ERROR)
    {
        return FALSE;
    }
    if (!ZpUdp_DecodeHeader(Buffer, Length, &Type, &ConnectionId))
    {
        return TRUE;
    }
    Connection = ZpServerUdp_FindConnection(Transport,
                                             ConnectionId,
                                             &Address,
                                             AddressLength);
    if (Connection == NULL && Type == ZP_UDP_PACKET_HANDSHAKE)
    {
        Connection = ZpServerUdp_CreateConnection(Transport,
                                                   Listener,
                                                   ConnectionId,
                                                   (SOCKADDR*)&Address,
                                                   AddressLength);
    }
    if (Connection == NULL)
    {
        return TRUE;
    }
    Status = ZpUdpConnection_ProcessDatagram(&Connection->Udp, Buffer, Length);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpServerUdp_CloseConnection(Connection, Status);
    }
    ZpConnection_Release((ZP_CONNECTION_HANDLE)&Connection->Public);
    return TRUE;
}

static
ULONG
ZpServerUdp_GetWaitMilliseconds(
    _Inout_ PZP_SERVER_UDP_TRANSPORT Transport)
{
    PZP_SERVER_UDP_CONNECTION Connections[ZP_SERVER_UDP_MAX_CONNECTIONS];
    PLIST_ENTRY Entry;
    ULONG Count = 0, Index, Wait = INFINITE;
    ULONGLONG TickCount = GetTickCount64();

    RtlAcquireSRWLockShared(&Transport->Owner->Lock);
    for (Entry = Transport->Connections.Flink;
         Entry != &Transport->Connections && Count < RTL_NUMBER_OF(Connections);
         Entry = Entry->Flink)
    {
        Connections[Count] = CONTAINING_RECORD(Entry,
                                               ZP_SERVER_UDP_CONNECTION,
                                               ListEntry);
        ZpConnection_AddRef((ZP_CONNECTION_HANDLE)&Connections[Count]->Public);
        Count++;
    }
    RtlReleaseSRWLockShared(&Transport->Owner->Lock);
    for (Index = 0; Index < Count; Index++)
    {
        Wait = min(Wait,
                   ZpUdpConnection_GetWaitMilliseconds(&Connections[Index]->Udp,
                                                       TickCount));
        ZpConnection_Release((ZP_CONNECTION_HANDLE)&Connections[Index]->Public);
    }
    return Wait;
}

static
VOID
ZpServerUdp_TickConnections(
    _Inout_ PZP_SERVER_UDP_TRANSPORT Transport)
{
    PZP_SERVER_UDP_CONNECTION Connections[ZP_SERVER_UDP_MAX_CONNECTIONS];
    PLIST_ENTRY Entry;
    ULONG Count = 0, Index;
    ULONGLONG TickCount = GetTickCount64();

    RtlAcquireSRWLockShared(&Transport->Owner->Lock);
    for (Entry = Transport->Connections.Flink;
         Entry != &Transport->Connections && Count < RTL_NUMBER_OF(Connections);
         Entry = Entry->Flink)
    {
        Connections[Count] = CONTAINING_RECORD(Entry,
                                               ZP_SERVER_UDP_CONNECTION,
                                               ListEntry);
        ZpConnection_AddRef((ZP_CONNECTION_HANDLE)&Connections[Count]->Public);
        Count++;
    }
    RtlReleaseSRWLockShared(&Transport->Owner->Lock);
    for (Index = 0; Index < Count; Index++)
    {
        ZP_STATUS Status = ZpUdpConnection_Tick(&Connections[Index]->Udp,
                                                TickCount);

        if (!ZpStatus_IsSuccess(Status))
        {
            ZpServerUdp_CloseConnection(Connections[Index], Status);
        }
        ZpConnection_Release((ZP_CONNECTION_HANDLE)&Connections[Index]->Public);
    }
}

static
DWORD
WINAPI
ZpServerUdp_Worker(
    _In_ PVOID Context)
{
    PZP_SERVER_UDP_TRANSPORT Transport = Context;
    HANDLE Events[] = { Transport->StopEvent, Transport->SocketEvent };
    WSANETWORKEVENTS NetworkEvents;
    DWORD Wait;
    INT Count;
    ULONG Index;

    for (;;)
    {
        Wait = WaitForMultipleObjects(RTL_NUMBER_OF(Events),
                                      Events,
                                      FALSE,
                                      ZpServerUdp_GetWaitMilliseconds(Transport));
        if (Wait == WAIT_OBJECT_0)
        {
            break;
        }
        if (Wait == WAIT_OBJECT_0 + 1)
        {
            for (Index = 0; Index < Transport->ListenerCount; Index++)
            {
                if (WSAEnumNetworkEvents(Transport->Listeners[Index],
                                         Transport->SocketEvent,
                                         &NetworkEvents) != SOCKET_ERROR &&
                    (NetworkEvents.lNetworkEvents & FD_READ) != 0 &&
                    NetworkEvents.iErrorCode[FD_READ_BIT] == 0)
                {
                    while (ZpServerUdp_ProcessDatagram(Transport,
                                                       Transport->Listeners[Index]))
                    {
                    }
                }
            }
            Count = WSAPoll(Transport->PollDescriptors,
                            Transport->ListenerCount,
                            0);
            if (Count > 0)
            {
                WSASetEvent(Transport->SocketEvent);
            }
        }
        else if (Wait != WAIT_TIMEOUT)
        {
            break;
        }
        if (Wait == WAIT_TIMEOUT)
        {
            ZpServerUdp_TickConnections(Transport);
        }
    }
    RtlAcquireSRWLockExclusive(&Transport->Owner->Lock);
    Transport->ReceiveStopped = TRUE;
    RtlReleaseSRWLockExclusive(&Transport->Owner->Lock);
    return 0;
}

static
VOID
ZpServerUdp_CloseListeners(
    _Inout_ PZP_SERVER_UDP_TRANSPORT Transport)
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
VOID
ZpServerUdp_CloseConnections(
    _Inout_ PZP_SERVER_UDP_TRANSPORT Transport)
{
    PZP_SERVER_UDP_CONNECTION Connections[ZP_SERVER_UDP_MAX_CONNECTIONS];
    PLIST_ENTRY Entry;
    ULONG Count = 0, Index;

    RtlAcquireSRWLockShared(&Transport->Owner->Lock);
    for (Entry = Transport->Connections.Flink;
         Entry != &Transport->Connections && Count < RTL_NUMBER_OF(Connections);
         Entry = Entry->Flink)
    {
        Connections[Count] = CONTAINING_RECORD(Entry,
                                               ZP_SERVER_UDP_CONNECTION,
                                               ListEntry);
        ZpConnection_AddRef((ZP_CONNECTION_HANDLE)&Connections[Count]->Public);
        Count++;
    }
    RtlReleaseSRWLockShared(&Transport->Owner->Lock);
    for (Index = 0; Index < Count; Index++)
    {
        ZpServerUdp_CloseConnection(Connections[Index],
                                    ZpStatus_FromNtStatus(STATUS_SUCCESS));
        ZpConnection_Release((ZP_CONNECTION_HANDLE)&Connections[Index]->Public);
    }
}

static
ZP_STATUS
NTAPI
ZpServerUdp_Start(
    _In_opt_ PVOID Context,
    _In_ ULONG EndpointIndex)
{
    PZP_SERVER_UDP_TRANSPORT Transport = Context;
    SOCKADDR_STORAGE Address;
    PCCERT_CONTEXT Certificates[ZP_DEPLOYMENT_MAX_COUNT];
    WSADATA WsaData;
    SOCKET Socket;
    ULONG Index, Exclusive = TRUE;
    INT AddressLength, Error;
    ZP_STATUS Status;

    UNREFERENCED_PARAMETER(EndpointIndex);
    ZpServerUdp_Uninitialize(Transport);
    ResetEvent(Transport->StopEvent);
    ZpServerUdp_InitializeConnectionLists(Transport);
    Error = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (Error != 0)
    {
        return ZpStatus_FromCode(ZpStatusWinsock, Error);
    }
    Transport->WsaInitialized = TRUE;
    Transport->SocketEvent = WSACreateEvent();
    if (Transport->SocketEvent == WSA_INVALID_EVENT)
    {
        Status = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
        goto Failed;
    }
    for (Index = 0; Index < Transport->Owner->Config.DeploymentCount; Index++)
    {
        Certificates[Index] = Transport->Owner->Config.Deployments[Index].Certificate;
    }
    Status = ZpDtls_AcquireServerCredentials(&Transport->Credential,
                                              Certificates,
                                              Transport->Owner->Config.DeploymentCount);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Failed;
    }
    Transport->CredentialInitialized = TRUE;
    for (Index = 0; Index < Transport->Owner->Config.ListenerCount; Index++)
    {
        if (Transport->Owner->Config.Listeners[Index].Transport != ZpTransportUdp)
        {
            continue;
        }
        Status = ZpSocket_ResolveAddress(Transport->Owner->Config.Listeners[Index].Host,
                                         Transport->Owner->Config.Listeners[Index].Port,
                                         TRUE,
                                         SOCK_DGRAM,
                                         IPPROTO_UDP,
                                         &Address,
                                         &AddressLength);
        if (!ZpStatus_IsSuccess(Status))
        {
            goto Failed;
        }
        Socket = socket(Address.ss_family, SOCK_DGRAM, IPPROTO_UDP);
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
            bind(Socket, (SOCKADDR*)&Address, AddressLength) == SOCKET_ERROR)
        {
            Error = WSAGetLastError();
            closesocket(Socket);
            Status = ZpStatus_FromCode(ZpStatusWinsock, Error);
            goto Failed;
        }
        if (WSAEventSelect(Socket, Transport->SocketEvent, FD_READ) == SOCKET_ERROR)
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
    Transport->WorkerThread = CreateThread(NULL,
                                           0,
                                           ZpServerUdp_Worker,
                                           Transport,
                                           0,
                                           NULL);
    if (Transport->WorkerThread == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Failed;
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);

Failed:
    SetEvent(Transport->StopEvent);
    ZpServerUdp_Uninitialize(Transport);
    return Status;
}

static
VOID
NTAPI
ZpServerUdp_Stop(
    _In_opt_ PVOID Context)
{
    PZP_SERVER_UDP_TRANSPORT Transport = Context;

    RtlAcquireSRWLockExclusive(&Transport->Owner->Lock);
    Transport->Stopping = TRUE;
    RtlReleaseSRWLockExclusive(&Transport->Owner->Lock);
    SetEvent(Transport->StopEvent);
    if (Transport->WorkerThread != NULL)
    {
        WaitForSingleObject(Transport->WorkerThread, INFINITE);
    }
    ZpServerUdp_CloseConnections(Transport);
    ZpServerUdp_TryCompleteStop(Transport);
}

static const ZP_TRANSPORT_OPERATIONS ZpServerUdpOperations = {
    ZpServerUdp_Start,
    ZpServerUdp_Stop,
    NULL
};

VOID
ZpServerUdp_Configure(
    _Inout_ PZP_SERVER_OBJECT Object)
{
    ULONG Index;

    Object->UdpTransport.Owner = Object;
    Object->UdpTransport.SocketEvent = WSA_INVALID_EVENT;
    ZpServerUdp_InitializeConnectionLists(&Object->UdpTransport);
    for (Index = 0; Index < ZP_LISTENER_MAX_COUNT; Index++)
    {
        Object->UdpTransport.Listeners[Index] = INVALID_SOCKET;
    }
    Object->UdpTransport.StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Object->UdpTransport.StopEvent == NULL)
    {
        return;
    }
    for (Index = 0; Index < Object->Config.ListenerCount; Index++)
    {
        if (Object->Config.Listeners[Index].Transport == ZpTransportUdp)
        {
            ZpServer_SetTransport((ZP_SERVER_HANDLE)Object,
                                  ZpTransportUdp,
                                  &ZpServerUdpOperations,
                                  &Object->UdpTransport);
            return;
        }
    }
}

VOID
ZpServerUdp_Uninitialize(
    _Inout_ PZP_SERVER_UDP_TRANSPORT Transport)
{
    if (Transport->StopEvent != NULL)
    {
        SetEvent(Transport->StopEvent);
    }
    if (Transport->WorkerThread != NULL)
    {
        WaitForSingleObject(Transport->WorkerThread, INFINITE);
        CloseHandle(Transport->WorkerThread);
        Transport->WorkerThread = NULL;
    }
    ZpServerUdp_CloseConnections(Transport);
    ZpServerUdp_CloseListeners(Transport);
    if (Transport->SocketEvent != WSA_INVALID_EVENT)
    {
        WSACloseEvent(Transport->SocketEvent);
        Transport->SocketEvent = WSA_INVALID_EVENT;
    }
    if (Transport->CredentialInitialized)
    {
        ZpDtls_FreeCredentials(&Transport->Credential);
        Transport->CredentialInitialized = FALSE;
    }
    if (Transport->WsaInitialized)
    {
        WSACleanup();
        Transport->WsaInitialized = FALSE;
    }
    Transport->Stopping = FALSE;
    Transport->ReceiveStopped = FALSE;
    if (Transport->StopEvent != NULL)
    {
        ResetEvent(Transport->StopEvent);
    }
}
