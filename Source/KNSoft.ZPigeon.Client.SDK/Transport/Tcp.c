#include "Tcp.h"

#include "../Client.inl"

static
VOID
ZpClientTcp_UninitializeAttempt(
    _Inout_ PZP_CLIENT_TCP_TRANSPORT Transport)
{
    if (Transport->ConnectThread != NULL)
    {
        WaitForSingleObject(Transport->ConnectThread, INFINITE);
        CloseHandle(Transport->ConnectThread);
        Transport->ConnectThread = NULL;
    }
    if (Transport->WorkerThread != NULL)
    {
        WaitForSingleObject(Transport->WorkerThread, INFINITE);
        CloseHandle(Transport->WorkerThread);
        Transport->WorkerThread = NULL;
    }
    if (Transport->CompletionPort != NULL)
    {
        CloseHandle(Transport->CompletionPort);
        Transport->CompletionPort = NULL;
    }
    ZpClientSession_Uninitialize(&Transport->Session);
    ZpCertificateValidator_Uninitialize(&Transport->CertificateValidator);
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
    Transport->ConnectionInitialized = FALSE;
}

static
VOID
ZpClientTcp_SetShutdownStatus(
    _Inout_ PZP_CLIENT_TCP_TRANSPORT Transport,
    _In_ ZP_STATUS Status)
{
    RtlAcquireSRWLockExclusive(&Transport->Owner->Lock);
    if (ZpStatus_IsSuccess(Transport->ShutdownStatus))
    {
        Transport->ShutdownStatus = Status;
    }
    RtlReleaseSRWLockExclusive(&Transport->Owner->Lock);
}

static
VOID
NTAPI
ZpClientTcp_SessionFailure(
    _In_opt_ PVOID Context,
    _In_ ZP_STATUS Status)
{
    ZpClientTcp_SetShutdownStatus(Context, Status);
}

static
NTSTATUS
NTAPI
ZpClientTcp_Send(
    _In_opt_ PVOID Context,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PZP_CLIENT_TCP_TRANSPORT Transport = Context;

    return ZpTcpConnection_SendFrame(&Transport->Connection,
                                     &Transport->Session.Connection,
                                     MessageType,
                                     Body,
                                     BodyLength);
}

static
ZP_STATUS
NTAPI
ZpClientTcp_Connected(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_TCP_TRANSPORT Transport = Context;
    PCCERT_CONTEXT Certificate;
    ZP_STATUS Status;

    Status = ZpTls_GetRemoteCertificate(&Connection->Tls, &Certificate);
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpCertificateValidator_ValidateServer(
            &Transport->CertificateValidator,
            Certificate,
            Transport->Owner->Config.Endpoints[Transport->EndpointIndex].ServerName);
        CertFreeCertificateContext(Certificate);
    }
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpClientTcp_SetShutdownStatus(Transport, Status);
        return Status;
    }
    return ZpStatus_FromNtStatus(ZpClientSession_Start(&Transport->Session));
}

static
NTSTATUS
NTAPI
ZpClientTcp_Receive(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_TCP_TRANSPORT Transport = Context;

    UNREFERENCED_PARAMETER(Connection);
    return ZpClientSession_Receive(&Transport->Session, Data, DataLength);
}

static
VOID
NTAPI
ZpClientTcp_Closed(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_TCP_TRANSPORT Transport = Context;

    UNREFERENCED_PARAMETER(Connection);
    if (!ZpStatus_IsSuccess(Transport->ShutdownStatus))
    {
        Status = Transport->ShutdownStatus;
    }
    Transport->ConnectionInitialized = FALSE;
    PostQueuedCompletionStatus(Transport->CompletionPort, 0, 0, NULL);
    ZpClient_TransportShutdown((ZP_CLIENT_HANDLE)Transport->Owner, Status);
}

static
DWORD
WINAPI
ZpClientTcp_ConnectThread(
    _In_ PVOID Context)
{
    PZP_CLIENT_TCP_TRANSPORT Transport = Context;
    const ZP_ENDPOINT* Endpoint = &Transport->Owner->Config.Endpoints[Transport->EndpointIndex];
    SOCKADDR_STORAGE Address;
    WSAPOLLFD Poll;
    SOCKET Socket = INVALID_SOCKET;
    ULONGLONG Deadline;
    ULONG Remaining;
    INT AddressLength, Error, NonBlocking;
    ZP_STATUS Status;

    Status = ZpTcp_ResolveAddress(Endpoint->Host,
                                  Endpoint->Port,
                                  FALSE,
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
    NonBlocking = TRUE;
    if (ioctlsocket(Socket, FIONBIO, (PULONG)&NonBlocking) == SOCKET_ERROR)
    {
        Status = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
        goto Failed;
    }
    Error = connect(Socket, (SOCKADDR*)&Address, AddressLength);
    if (Error == SOCKET_ERROR && (Error = WSAGetLastError()) != WSAEWOULDBLOCK)
    {
        Status = ZpStatus_FromCode(ZpStatusWinsock, Error);
        goto Failed;
    }
    Deadline = GetTickCount64() + Transport->Owner->Config.ConnectTimeoutMilliseconds;
    Poll.fd = Socket;
    Poll.events = POLLWRNORM;
    Poll.revents = 0;
    while (Error == WSAEWOULDBLOCK)
    {
        if (WaitForSingleObject(Transport->StopEvent, 0) == WAIT_OBJECT_0)
        {
            Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
            goto Failed;
        }
        Remaining = (ULONG)min(Deadline > GetTickCount64() ?
                                  Deadline - GetTickCount64() : 0,
                              100);
        if (Remaining == 0)
        {
            Status = ZpStatus_FromNtStatus(STATUS_IO_TIMEOUT);
            goto Failed;
        }
        Error = WSAPoll(&Poll, 1, Remaining);
        if (Error == SOCKET_ERROR)
        {
            Status = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
            goto Failed;
        }
        if (Error != 0)
        {
            INT SocketError;
            INT Size = sizeof(SocketError);

            if (getsockopt(Socket,
                           SOL_SOCKET,
                           SO_ERROR,
                           (PCHAR)&SocketError,
                           &Size) == SOCKET_ERROR || SocketError != 0)
            {
                Status = ZpStatus_FromCode(ZpStatusWinsock,
                                           SocketError != 0 ?
                                               SocketError : WSAGetLastError());
                goto Failed;
            }
            Error = 0;
        }
    }
    NonBlocking = FALSE;
    ioctlsocket(Socket, FIONBIO, (PULONG)&NonBlocking);
    Status = ZpTcpConnection_Initialize(&Transport->Connection,
                                        Transport->CompletionPort,
                                        Socket,
                                        ZpTlsClient,
                                        &Transport->Credential,
                                        Endpoint->ServerName,
                                        ZpClientTcp_Connected,
                                        ZpClientTcp_Receive,
                                        ZpClientTcp_Closed,
                                        Transport);
    Socket = INVALID_SOCKET;
    if (ZpStatus_IsSuccess(Status))
    {
        Transport->ConnectionInitialized = TRUE;
    }
    return 0;

Failed:
    if (Socket != INVALID_SOCKET)
    {
        closesocket(Socket);
    }
    ZpClient_TransportShutdown((ZP_CLIENT_HANDLE)Transport->Owner, Status);
    return 0;
}

static
ZP_STATUS
NTAPI
ZpClientTcp_Start(
    _In_opt_ PVOID Context,
    _In_ ULONG EndpointIndex)
{
    PZP_CLIENT_TCP_TRANSPORT Transport = Context;
    WSADATA WsaData;
    ZP_STATUS Status;
    INT Error;

    ZpClientTcp_UninitializeAttempt(Transport);
    ResetEvent(Transport->StopEvent);
    Transport->EndpointIndex = EndpointIndex;
    Transport->ShutdownStatus = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Error = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (Error != 0)
    {
        return ZpStatus_FromCode(ZpStatusWinsock, Error);
    }
    Transport->WsaInitialized = TRUE;
    Status = ZpTls_AcquireClientCredentials(&Transport->Credential);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Failed;
    }
    Transport->CredentialInitialized = TRUE;
    Status = ZpCertificateValidator_Initialize(
        &Transport->CertificateValidator,
        Transport->Owner->Config.DeploymentRootCertificate,
        Transport->Owner->Config.DeploymentRootCertificateLength);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Failed;
    }
    Status = ZpClientSession_Prepare(&Transport->Session,
                                     Transport->Owner,
                                     ZpClientTcp_Send,
                                     ZpClientTcp_SessionFailure,
                                     Transport,
                                     Transport->Owner->ExternalIdentityKey);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Failed;
    }
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
    Transport->ConnectThread = CreateThread(NULL,
                                            0,
                                            ZpClientTcp_ConnectThread,
                                            Transport,
                                            0,
                                            NULL);
    if (Transport->ConnectThread == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Failed;
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);

Failed:
    if (Transport->CompletionPort != NULL)
    {
        PostQueuedCompletionStatus(Transport->CompletionPort, 0, 0, NULL);
    }
    ZpClientTcp_UninitializeAttempt(Transport);
    return Status;
}

static
VOID
NTAPI
ZpClientTcp_Stop(
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_TCP_TRANSPORT Transport = Context;

    SetEvent(Transport->StopEvent);
    if (Transport->ConnectionInitialized)
    {
        ZpTcpConnection_Close(&Transport->Connection,
                              ZpStatus_FromNtStatus(STATUS_SUCCESS));
    }
}

static const ZP_TRANSPORT_OPERATIONS ZpClientTcpOperations = {
    ZpClientTcp_Start,
    ZpClientTcp_Stop,
    ZpClientTcp_Send
};

VOID
ZpClientTcp_Configure(
    _Inout_ PZP_CLIENT_OBJECT Object)
{
    Object->TcpTransport.Owner = Object;
    Object->TcpTransport.StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Object->TcpTransport.StopEvent != NULL)
    {
        ZpClient_SetTransport((ZP_CLIENT_HANDLE)Object,
                              ZpTransportTcp,
                              &ZpClientTcpOperations,
                              &Object->TcpTransport);
    }
}

VOID
ZpClientTcp_Uninitialize(
    _Inout_ PZP_CLIENT_TCP_TRANSPORT Transport)
{
    SetEvent(Transport->StopEvent);
    if (Transport->ConnectionInitialized)
    {
        ZpTcpConnection_Close(&Transport->Connection,
                              ZpStatus_FromNtStatus(STATUS_SUCCESS));
    }
    ZpClientTcp_UninitializeAttempt(Transport);
    if (Transport->StopEvent != NULL)
    {
        CloseHandle(Transport->StopEvent);
        Transport->StopEvent = NULL;
    }
}
