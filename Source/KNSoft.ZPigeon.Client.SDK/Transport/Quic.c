#include "../Client.inl"
#include "../../Modules/File/Client.h"
#include "../../Network/Quic.inl"

#define ZP_CLIENT_QUIC_KEEP_ALIVE_INTERVAL_MILLISECONDS 20000

static const QUIC_REGISTRATION_CONFIG ZpClientQuicRegistrationConfig = {
    "KNSoft.ZPigeon.Client",
    QUIC_EXECUTION_PROFILE_LOW_LATENCY
};

static
VOID
ZpClientQuic_UninitializeAttempt(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport);

static
NTSTATUS
NTAPI
ZpClientQuic_Send(
    _In_opt_ PVOID Context,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength);

static
VOID
ZpClientQuic_SetShutdownStatus(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport,
    _In_ ZP_STATUS Status)
{
    RtlAcquireSRWLockExclusive(&Transport->Owner->Lock);
    if (Transport->ShutdownStatus.Type == ZpStatusNone)
    {
        Transport->ShutdownStatus = Status;
    }
    RtlReleaseSRWLockExclusive(&Transport->Owner->Lock);
}

static
ZP_STATUS
ZpClientQuic_GetShutdownStatus(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport)
{
    ZP_STATUS Status;

    RtlAcquireSRWLockShared(&Transport->Owner->Lock);
    Status = Transport->ShutdownStatus;
    RtlReleaseSRWLockShared(&Transport->Owner->Lock);
    return Status;
}

static
VOID
NTAPI
ZpClientQuic_SessionFailure(
    _In_opt_ PVOID Context,
    _In_ ZP_STATUS Status)
{
    ZpClientQuic_SetShutdownStatus(Context, Status);
}

static
NTSTATUS
NTAPI
ZpClientQuic_Send(
    _In_opt_ PVOID Context,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;
    QUIC_STATUS QuicStatus = QUIC_STATUS_SUCCESS;
    NTSTATUS Status;

    if (Transport->Stream == NULL)
    {
        return STATUS_CONNECTION_DISCONNECTED;
    }
    Status = ZpQuic_SendFrame(Transport->Stream,
                              &Transport->Session.Connection,
                              MessageType,
                              Body,
                              BodyLength,
                              &QuicStatus);
    if (!NT_SUCCESS(Status))
    {
        ZpClientQuic_SetShutdownStatus(
            Transport,
            QUIC_FAILED(QuicStatus) ?
                ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus) :
                ZpStatus_FromNtStatus(Status));
        MsQuicConnectionShutdown(Transport->Connection,
                                 QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                 0);
    }
    return Status;
}

static
ZP_STATUS
ZpClientQuic_ValidateCertificate(
    _In_ PZP_CLIENT_QUIC_TRANSPORT Transport,
    _In_ PCCERT_CONTEXT Certificate)
{
    return ZpCertificateValidator_ValidateServer(
        &Transport->CertificateValidator,
        Certificate,
        Transport->Owner->Config.Endpoints[Transport->EndpointIndex].ServerName);
}

static
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS
QUIC_API
ZpClientQuic_StreamCallback(
    _In_ HQUIC Stream,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_STREAM_EVENT* Event)
{
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;
    PZP_CLIENT_OBJECT Object;
    NTSTATUS Status;
    ULONG Index;

    if (Transport == NULL)
    {
        return QUIC_STATUS_INVALID_PARAMETER;
    }
    Object = Transport->Owner;
    switch (Event->Type)
    {
        case QUIC_STREAM_EVENT_START_COMPLETE:
            if (QUIC_SUCCEEDED(Event->START_COMPLETE.Status))
            {
                Status = ZpClientSession_Start(&Transport->Session);
                if (!NT_SUCCESS(Status))
                {
                    ZpClientQuic_SetShutdownStatus(
                        Transport,
                        ZpStatus_FromNtStatus(Status));
                    MsQuicConnectionShutdown(Transport->Connection,
                                             QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                             0);
                }
            }
            else
            {
                ZpClientQuic_SetShutdownStatus(
                    Transport,
                    ZpStatus_FromCode(ZpStatusQuic,
                                      (ULONG)Event->START_COMPLETE.Status));
                MsQuicConnectionShutdown(Transport->Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
            }
            break;

        case QUIC_STREAM_EVENT_SEND_COMPLETE:
            ZpQuic_CompleteSend(Event->SEND_COMPLETE.ClientContext);
            break;

        case QUIC_STREAM_EVENT_RECEIVE:
            Status = STATUS_SUCCESS;
            for (Index = 0;
                 NT_SUCCESS(Status) && Index < Event->RECEIVE.BufferCount;
                 Index++)
            {
                Status = ZpClientSession_Receive(&Transport->Session,
                                                 Event->RECEIVE.Buffers[Index].Buffer,
                                                 Event->RECEIVE.Buffers[Index].Length);
            }
            if (!NT_SUCCESS(Status))
            {
                ZpClientQuic_SetShutdownStatus(
                    Transport,
                    ZpStatus_FromNtStatus(Status));
                MsQuicConnectionShutdown(Transport->Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
            }
            break;

        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
            ZpClientQuic_SetShutdownStatus(
                Transport,
                ZpStatus_FromNtStatus(STATUS_CONNECTION_DISCONNECTED));
            MsQuicConnectionShutdown(Transport->Connection,
                                     QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                     0);
            break;

        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            RtlAcquireSRWLockExclusive(&Object->Lock);
            if (Transport->Stream == Stream)
            {
                Transport->Stream = NULL;
            }
            RtlReleaseSRWLockExclusive(&Object->Lock);
            if (!Event->SHUTDOWN_COMPLETE.AppCloseInProgress)
            {
                MsQuicStreamClose(Stream);
            }
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

static
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS
QUIC_API
ZpClientQuic_ConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event)
{
    QUIC_STATUS QuicStatus;
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;
    PZP_CLIENT_OBJECT Object;
    HQUIC Stream;
    ZP_STATUS ShutdownStatus;
    ZP_STATUS CertificateStatus;
    BOOLEAN Valid;

    if (Transport == NULL)
    {
        return QUIC_STATUS_INVALID_PARAMETER;
    }
    Object = Transport->Owner;
    switch (Event->Type)
    {
        case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:
            CertificateStatus = ZpClientQuic_ValidateCertificate(
                Transport,
                (PCCERT_CONTEXT)Event->PEER_CERTIFICATE_RECEIVED.Certificate);
            if (!ZpStatus_IsSuccess(CertificateStatus))
            {
                ZpClientQuic_SetShutdownStatus(Transport,
                                               CertificateStatus);
            }
            Valid = ZpStatus_IsSuccess(CertificateStatus) ? TRUE : FALSE;
            MsQuicConnectionCertificateValidationComplete(
                Connection,
                Valid,
                Valid ?
                    QUIC_TLS_ALERT_CODE_SUCCESS :
                    QUIC_TLS_ALERT_CODE_BAD_CERTIFICATE);
            return QUIC_STATUS_PENDING;

        case QUIC_CONNECTION_EVENT_CONNECTED:
            QuicStatus = MsQuicStreamOpen(Connection,
                                          QUIC_STREAM_OPEN_FLAG_NONE,
                                          ZpClientQuic_StreamCallback,
                                          Transport,
                                          &Stream);
            if (QUIC_SUCCEEDED(QuicStatus))
            {
                RtlAcquireSRWLockExclusive(&Object->Lock);
                Transport->Stream = Stream;
                RtlReleaseSRWLockExclusive(&Object->Lock);
                QuicStatus = MsQuicStreamStart(Stream,
                                               QUIC_STREAM_START_FLAG_IMMEDIATE |
                                                   QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL);
            }
            if (QUIC_FAILED(QuicStatus))
            {
                ZpClientQuic_SetShutdownStatus(
                    Transport,
                    ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus));
                MsQuicConnectionShutdown(Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
            }
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            ZpClientQuic_SetShutdownStatus(
                Transport,
                ZpStatus_FromCode(
                    ZpStatusQuic,
                    (ULONG)Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status));
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
            ZpClientQuic_SetShutdownStatus(
                Transport,
                ZpStatus_FromNtStatus(STATUS_CONNECTION_DISCONNECTED));
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            RtlAcquireSRWLockExclusive(&Object->Lock);
            if (Transport->Connection == Connection)
            {
                Transport->Connection = NULL;
            }
            RtlReleaseSRWLockExclusive(&Object->Lock);
            ShutdownStatus = ZpClientQuic_GetShutdownStatus(Transport);
            MsQuicConnectionClose(Connection);
            ZpClient_TransportShutdown((ZP_CLIENT_HANDLE)Object,
                                       ShutdownStatus);
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

static
ZP_STATUS
ZpClientQuic_StartEndpoint(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport)
{
    QUIC_STATUS QuicStatus;
    PZP_CLIENT_OBJECT Object = Transport->Owner;
    const ZP_ENDPOINT* Endpoint = &Object->Config.Endpoints[Transport->EndpointIndex];
    QUIC_SETTINGS Settings = { 0 };
    QUIC_CREDENTIAL_CONFIG Credentials = { 0 };
    QUIC_ADDR Address;
    ZP_STATUS Status;
    PSTR ServerName;
    ULONG ServerNameSize;

    ZpClientQuic_UninitializeAttempt(Transport);
    Transport->Owner = Object;
    Transport->ShutdownStatus = ZpStatus_FromNtStatus(STATUS_SUCCESS);

    QuicStatus = KNSoftQuicInitialize();
    if (QUIC_FAILED(QuicStatus))
    {
        return ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
    }
    Transport->Initialized = TRUE;
    QuicStatus = MsQuicRegistrationOpen(&ZpClientQuicRegistrationConfig,
                                        &Transport->Registration);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
        goto Cleanup;
    }
    Status = ZpCertificateValidator_Initialize(
        &Transport->CertificateValidator,
        Object->Config.DeploymentRootCertificate,
        Object->Config.DeploymentRootCertificateLength);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Cleanup;
    }
    Status = ZpClientSession_Prepare(&Transport->Session,
                                     Object,
                                     ZpClientQuic_Send,
                                     ZpClientQuic_SessionFailure,
                                     Transport,
                                     Transport->Owner->ExternalIdentityKey);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Cleanup;
    }

    Settings.HandshakeIdleTimeoutMs = Object->Config.ConnectTimeoutMilliseconds;
    Settings.IsSet.HandshakeIdleTimeoutMs = TRUE;
    Settings.KeepAliveIntervalMs =
        ZP_CLIENT_QUIC_KEEP_ALIVE_INTERVAL_MILLISECONDS;
    Settings.IsSet.KeepAliveIntervalMs = TRUE;
    Credentials.Type = QUIC_CREDENTIAL_TYPE_NONE;
    Credentials.Flags = QUIC_CREDENTIAL_FLAG_CLIENT |
                        QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED |
                        QUIC_CREDENTIAL_FLAG_DEFER_CERTIFICATE_VALIDATION;
    QuicStatus = MsQuicConfigurationOpen(Transport->Registration,
                                         &ZpQuicAlpn,
                                         1,
                                         &Settings,
                                         sizeof(Settings),
                                         NULL,
                                         &Transport->Configuration);
    if (QUIC_FAILED(QuicStatus) ||
        QUIC_FAILED(QuicStatus = MsQuicConfigurationLoadCredential(
                        Transport->Configuration,
                        &Credentials)))
    {
        Status = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
        goto Cleanup;
    }
    QuicStatus = MsQuicConnectionOpen(Transport->Registration,
                                      ZpClientQuic_ConnectionCallback,
                                      Transport,
                                      &Transport->Connection);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
        goto Cleanup;
    }
    Status = ZpQuic_ResolveAddress(Endpoint->Host, Endpoint->Port, &Address);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Cleanup;
    }
    QuicStatus = MsQuicSetParam(Transport->Connection,
                                QUIC_PARAM_CONN_REMOTE_ADDRESS,
                                sizeof(Address),
                                &Address);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
        goto Cleanup;
    }
    ServerNameSize = Str_UnicodeToUtf8(NULL, 0, Endpoint->ServerName);
    ServerName = Mem_Alloc(ServerNameSize);
    if (ServerName == NULL ||
        Str_UnicodeToUtf8(ServerName, ServerNameSize, Endpoint->ServerName) == 0)
    {
        Mem_Free(ServerName);
        Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto Cleanup;
    }
    QuicStatus = MsQuicConnectionStart(Transport->Connection,
                                       Transport->Configuration,
                                       Address.si_family,
                                       ServerName,
                                       Endpoint->Port);
    Mem_Free(ServerName);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
        goto Cleanup;
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);

Cleanup:
    ZpClientQuic_UninitializeAttempt(Transport);
    Transport->Owner = Object;
    return Status;
}

static
ZP_STATUS
NTAPI
ZpClientQuic_Start(
    _In_opt_ PVOID Context,
    _In_ ULONG EndpointIndex)
{
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;

    Transport->EndpointIndex = EndpointIndex;
    return ZpClientQuic_StartEndpoint(Transport);
}

static
VOID
NTAPI
ZpClientQuic_Stop(
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;
    PZP_CLIENT_OBJECT Object = Transport->Owner;
    HQUIC Connection;
    LOGICAL StartPending;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Connection = Transport->Connection;
    StartPending = Object->StartPending;
    if (Connection != NULL)
    {
        Transport->ShutdownStatus = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Connection != NULL)
    {
        MsQuicConnectionShutdown(Connection,
                                 QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                 0);
        return;
    }
    if (!StartPending)
    {
        ZpClient_NotifyState((ZP_CLIENT_HANDLE)Object,
                             ZpClientStateStopped,
                             ZpStatus_FromNtStatus(STATUS_SUCCESS));
    }
}

static const ZP_TRANSPORT_OPERATIONS ZpClientQuicOperations = {
    ZpClientQuic_Start,
    ZpClientQuic_Stop,
    ZpClientQuic_Send
};

VOID
ZpClientQuic_Configure(
    _Inout_ PZP_CLIENT_OBJECT Object)
{
    Object->QuicTransport.Owner = Object;
    ZpClient_SetTransport((ZP_CLIENT_HANDLE)Object,
                          ZpTransportQuic,
                          &ZpClientQuicOperations,
                          &Object->QuicTransport);
}

static
VOID
ZpClientQuic_UninitializeAttempt(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport)
{
    if (Transport->Stream != NULL)
    {
        MsQuicStreamClose(Transport->Stream);
        Transport->Stream = NULL;
    }
    if (Transport->Connection != NULL)
    {
        MsQuicConnectionClose(Transport->Connection);
        Transport->Connection = NULL;
    }
    if (Transport->Configuration != NULL)
    {
        MsQuicConfigurationClose(Transport->Configuration);
        Transport->Configuration = NULL;
    }
    if (Transport->Registration != NULL)
    {
        MsQuicRegistrationClose(Transport->Registration);
        Transport->Registration = NULL;
    }
    ZpCertificateValidator_Uninitialize(&Transport->CertificateValidator);
    ZpClientSession_Uninitialize(&Transport->Session);
    if (Transport->Initialized)
    {
        KNSoftQuicUninitialize();
        Transport->Initialized = FALSE;
    }
}

VOID
ZpClientQuic_Uninitialize(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport)
{
    ZpClientQuic_UninitializeAttempt(Transport);
}
