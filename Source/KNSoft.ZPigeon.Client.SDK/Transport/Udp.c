#include "Udp.h"

#include "../Client.inl"

#include <Bcrypt.h>

static
VOID
ZpClientUdp_UninitializeAttempt(
    _Inout_ PZP_CLIENT_UDP_TRANSPORT Transport)
{
    if (Transport->WorkerThread != NULL)
    {
        WaitForSingleObject(Transport->WorkerThread, INFINITE);
        CloseHandle(Transport->WorkerThread);
        Transport->WorkerThread = NULL;
    }
    if (Transport->ConnectionInitialized)
    {
        ZpUdpConnection_Uninitialize(&Transport->Connection);
        Transport->ConnectionInitialized = FALSE;
    }
    if (Transport->Socket != INVALID_SOCKET)
    {
        closesocket(Transport->Socket);
        Transport->Socket = INVALID_SOCKET;
    }
    ZpClientSession_Uninitialize(&Transport->Session);
    ZpCertificateValidator_Uninitialize(&Transport->CertificateValidator);
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
}

static
VOID
ZpClientUdp_SetShutdownStatus(
    _Inout_ PZP_CLIENT_UDP_TRANSPORT Transport,
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
ZpClientUdp_SessionFailure(
    _In_opt_ PVOID Context,
    _In_ ZP_STATUS Status)
{
    ZpClientUdp_SetShutdownStatus(Context, Status);
}

static
NTSTATUS
NTAPI
ZpClientUdp_Send(
    _In_opt_ PVOID Context,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PZP_CLIENT_UDP_TRANSPORT Transport = Context;

    return ZpUdpConnection_SendFrame(&Transport->Connection,
                                     &Transport->Session.Connection,
                                     MessageType,
                                     Body,
                                     BodyLength);
}

static
ZP_STATUS
NTAPI
ZpClientUdp_Connected(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_UDP_TRANSPORT Transport = Context;
    PCCERT_CONTEXT Certificate;
    ZP_STATUS Status;

    Status = ZpDtls_GetRemoteCertificate(&Connection->Dtls, &Certificate);
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
        ZpClientUdp_SetShutdownStatus(Transport, Status);
        return Status;
    }
    return ZpStatus_FromNtStatus(ZpClientSession_Start(&Transport->Session));
}

static
NTSTATUS
NTAPI
ZpClientUdp_Receive(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_UDP_TRANSPORT Transport = Context;

    UNREFERENCED_PARAMETER(Connection);
    return ZpClientSession_Receive(&Transport->Session, Data, DataLength);
}

static
DWORD
WINAPI
ZpClientUdp_Worker(
    _In_ PVOID Context)
{
    PZP_CLIENT_UDP_TRANSPORT Transport = Context;
    BYTE Buffer[ZP_UDP_MAX_DATAGRAM_SIZE];
    WSAPOLLFD Poll = { Transport->Socket, POLLRDNORM, 0 };
    INT Result;
    ZP_STATUS Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);

    while (WaitForSingleObject(Transport->StopEvent, 0) != WAIT_OBJECT_0)
    {
        Result = WSAPoll(&Poll, 1, 50);
        if (Result == SOCKET_ERROR)
        {
            Status = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
            break;
        }
        if (Result > 0 && (Poll.revents & POLLRDNORM) != 0)
        {
            Result = recv(Transport->Socket, (PCHAR)Buffer, sizeof(Buffer), 0);
            if (Result == SOCKET_ERROR)
            {
                Status = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
                break;
            }
            Status = ZpUdpConnection_ProcessDatagram(&Transport->Connection,
                                                     Buffer,
                                                     Result);
            if (!ZpStatus_IsSuccess(Status))
            {
                break;
            }
        }
        Poll.revents = 0;
        Status = ZpUdpConnection_Tick(&Transport->Connection, GetTickCount64());
        if (!ZpStatus_IsSuccess(Status))
        {
            break;
        }
    }
    if (!ZpStatus_IsSuccess(Transport->ShutdownStatus))
    {
        Status = Transport->ShutdownStatus;
    }
    ZpClient_TransportShutdown((ZP_CLIENT_HANDLE)Transport->Owner, Status);
    return 0;
}

static
ZP_STATUS
NTAPI
ZpClientUdp_Start(
    _In_opt_ PVOID Context,
    _In_ ULONG EndpointIndex)
{
    PZP_CLIENT_UDP_TRANSPORT Transport = Context;
    const ZP_ENDPOINT* Endpoint = &Transport->Owner->Config.Endpoints[EndpointIndex];
    SOCKADDR_STORAGE Address;
    WSADATA WsaData;
    ULONGLONG ConnectionId;
    INT AddressLength, Error;
    ZP_STATUS Status;

    ZpClientUdp_UninitializeAttempt(Transport);
    ResetEvent(Transport->StopEvent);
    Transport->ShutdownStatus = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Transport->EndpointIndex = EndpointIndex;
    Error = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (Error != 0)
    {
        return ZpStatus_FromCode(ZpStatusWinsock, Error);
    }
    Transport->WsaInitialized = TRUE;
    Status = ZpDtls_AcquireClientCredentials(&Transport->Credential);
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
                                     ZpClientUdp_Send,
                                     ZpClientUdp_SessionFailure,
                                     Transport,
                                     Transport->Owner->ExternalIdentityKey);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Failed;
    }
    Status = ZpUdp_ResolveAddress(Endpoint->Host,
                                  Endpoint->Port,
                                  FALSE,
                                  &Address,
                                  &AddressLength);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Failed;
    }
    Transport->Socket = socket(Address.ss_family, SOCK_DGRAM, IPPROTO_UDP);
    if (Transport->Socket == INVALID_SOCKET)
    {
        Status = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
        goto Failed;
    }
    if (connect(Transport->Socket, (SOCKADDR*)&Address, AddressLength) == SOCKET_ERROR)
    {
        Status = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
        goto Failed;
    }
    do
    {
        Error = BCryptGenRandom(NULL,
                                (PUCHAR)&ConnectionId,
                                sizeof(ConnectionId),
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    } while (NT_SUCCESS(Error) && ConnectionId == 0);
    if (!NT_SUCCESS(Error))
    {
        Status = ZpStatus_FromNtStatus(Error);
        goto Failed;
    }
    Status = ZpUdpConnection_Initialize(&Transport->Connection,
                                        Transport->Socket,
                                        (SOCKADDR*)&Address,
                                        AddressLength,
                                        ConnectionId,
                                        ZpDtlsClient,
                                        &Transport->Credential,
                                        Endpoint->ServerName,
                                        ZpClientUdp_Connected,
                                        ZpClientUdp_Receive,
                                        Transport);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Failed;
    }
    Transport->ConnectionInitialized = TRUE;
    Transport->WorkerThread = CreateThread(NULL,
                                           0,
                                           ZpClientUdp_Worker,
                                           Transport,
                                           0,
                                           NULL);
    if (Transport->WorkerThread == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Failed;
    }
    Status = ZpUdpConnection_StartHandshake(&Transport->Connection);
    if (ZpStatus_IsSuccess(Status))
    {
        return Status;
    }

Failed:
    SetEvent(Transport->StopEvent);
    ZpClientUdp_UninitializeAttempt(Transport);
    return Status;
}

static
VOID
NTAPI
ZpClientUdp_Stop(
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_UDP_TRANSPORT Transport = Context;

    SetEvent(Transport->StopEvent);
}

static const ZP_TRANSPORT_OPERATIONS ZpClientUdpOperations = {
    ZpClientUdp_Start,
    ZpClientUdp_Stop,
    ZpClientUdp_Send
};

VOID
ZpClientUdp_Configure(
    _Inout_ PZP_CLIENT_OBJECT Object)
{
    Object->UdpTransport.Owner = Object;
    Object->UdpTransport.Socket = INVALID_SOCKET;
    Object->UdpTransport.StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Object->UdpTransport.StopEvent != NULL)
    {
        ZpClient_SetTransport((ZP_CLIENT_HANDLE)Object,
                              ZpTransportUdp,
                              &ZpClientUdpOperations,
                              &Object->UdpTransport);
    }
}

VOID
ZpClientUdp_Uninitialize(
    _Inout_ PZP_CLIENT_UDP_TRANSPORT Transport)
{
    SetEvent(Transport->StopEvent);
    ZpClientUdp_UninitializeAttempt(Transport);
    if (Transport->StopEvent != NULL)
    {
        CloseHandle(Transport->StopEvent);
        Transport->StopEvent = NULL;
    }
}
