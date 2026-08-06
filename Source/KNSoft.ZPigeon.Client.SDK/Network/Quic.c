#include "../Client.inl"
#include "../../Network/Quic.inl"

static const QUIC_REGISTRATION_CONFIG ZpClientQuicRegistrationConfig = {
    "KNSoft.ZPigeon.Client",
    QUIC_EXECUTION_PROFILE_LOW_LATENCY
};

static
BOOLEAN
ZpClientQuic_ValidateCertificate(
    _In_ PZP_CLIENT_QUIC_TRANSPORT Transport,
    _In_ PCCERT_CONTEXT Certificate)
{
    CERT_CHAIN_PARA ChainParameters = { sizeof(ChainParameters) };
    CERT_CHAIN_POLICY_PARA PolicyParameters = { sizeof(PolicyParameters) };
    CERT_CHAIN_POLICY_STATUS PolicyStatus = { sizeof(PolicyStatus) };
    SSL_EXTRA_CERT_CHAIN_POLICY_PARA SslParameters = { sizeof(SslParameters) };
    PCCERT_CHAIN_CONTEXT Chain;
    BOOLEAN Valid;

    if (!CertGetCertificateChain(Transport->ChainEngine,
                                 Certificate,
                                 NULL,
                                 Certificate->hCertStore,
                                 &ChainParameters,
                                 CERT_CHAIN_CACHE_END_CERT,
                                 NULL,
                                 &Chain))
    {
        return FALSE;
    }
    SslParameters.dwAuthType = AUTHTYPE_SERVER;
    SslParameters.pwszServerName = (PWSTR)Transport->Owner->Config.Endpoints[
        Transport->EndpointIndex].ServerName;
    PolicyParameters.pvExtraPolicyPara = &SslParameters;
    Valid = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL,
                                             Chain,
                                             &PolicyParameters,
                                             &PolicyStatus) &&
            PolicyStatus.dwError == ERROR_SUCCESS;
    CertFreeCertificateChain(Chain);
    return Valid;
}

static
QUIC_STATUS
QUIC_API
ZpClientQuic_StreamCallback(
    _In_ HQUIC Stream,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_STREAM_EVENT* Event)
{
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;
    PZP_CLIENT_OBJECT Object = Transport->Owner;

    switch (Event->Type)
    {
        case QUIC_STREAM_EVENT_START_COMPLETE:
            if (QUIC_SUCCEEDED(Event->START_COMPLETE.Status))
            {
                ZpClient_NotifyState((ZP_CLIENT_HANDLE)Object,
                                     ZpClientStateAuthenticating,
                                     STATUS_SUCCESS);
            }
            else
            {
                InterlockedExchange((volatile LONG*)&Transport->ShutdownStatus,
                                    ZpQuic_StatusToNtStatus(Event->START_COMPLETE.Status));
                MsQuicConnectionShutdown(Transport->Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
            }
            break;

        case QUIC_STREAM_EVENT_RECEIVE:
            InterlockedExchange((volatile LONG*)&Transport->ShutdownStatus,
                                STATUS_PROTOCOL_UNREACHABLE);
            MsQuicConnectionShutdown(Transport->Connection,
                                     QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                     0);
            break;

        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
            InterlockedExchange((volatile LONG*)&Transport->ShutdownStatus,
                                STATUS_CONNECTION_DISCONNECTED);
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
QUIC_STATUS
QUIC_API
ZpClientQuic_ConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event)
{
    QUIC_STATUS QuicStatus;
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;
    PZP_CLIENT_OBJECT Object = Transport->Owner;
    HQUIC Stream;
    NTSTATUS Status;
    BOOLEAN Valid;

    switch (Event->Type)
    {
        case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:
            Valid = ZpClientQuic_ValidateCertificate(
                Transport,
                (PCCERT_CONTEXT)Event->PEER_CERTIFICATE_RECEIVED.Certificate);
            MsQuicConnectionCertificateValidationComplete(
                Connection,
                Valid,
                Valid ? QUIC_TLS_ALERT_CODE_SUCCESS : QUIC_TLS_ALERT_CODE_BAD_CERTIFICATE);
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
                InterlockedExchange((volatile LONG*)&Transport->ShutdownStatus,
                                    ZpQuic_StatusToNtStatus(QuicStatus));
                MsQuicConnectionShutdown(Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
            }
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            InterlockedExchange(
                (volatile LONG*)&Transport->ShutdownStatus,
                ZpQuic_StatusToNtStatus(Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status));
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
            InterlockedExchange((volatile LONG*)&Transport->ShutdownStatus,
                                STATUS_CONNECTION_DISCONNECTED);
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            RtlAcquireSRWLockExclusive(&Object->Lock);
            if (Transport->Connection == Connection)
            {
                Transport->Connection = NULL;
            }
            Status = Transport->ShutdownStatus;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            MsQuicConnectionClose(Connection);
            ZpClient_NotifyState((ZP_CLIENT_HANDLE)Object,
                                 ZpClientStateStopped,
                                 Status);
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

static
NTSTATUS
ZpClientQuic_CreateRootChainEngine(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport)
{
    CERT_CHAIN_ENGINE_CONFIG EngineConfig = { sizeof(EngineConfig) };
    PZP_CLIENT_OBJECT Object = Transport->Owner;
    PCCERT_CONTEXT RootCertificate;

    RootCertificate = CertCreateCertificateContext(
        X509_ASN_ENCODING,
        Object->Config.DeploymentRootCertificate,
        Object->Config.DeploymentRootCertificateLength);
    if (RootCertificate == NULL)
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    Transport->RootStore = CertOpenStore(CERT_STORE_PROV_MEMORY,
                                         X509_ASN_ENCODING,
                                         0,
                                         CERT_STORE_CREATE_NEW_FLAG,
                                         NULL);
    if (Transport->RootStore == NULL ||
        !CertAddCertificateContextToStore(Transport->RootStore,
                                          RootCertificate,
                                          CERT_STORE_ADD_ALWAYS,
                                          NULL))
    {
        CertFreeCertificateContext(RootCertificate);
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    CertFreeCertificateContext(RootCertificate);
    EngineConfig.hExclusiveRoot = Transport->RootStore;
    if (!CertCreateCertificateChainEngine(&EngineConfig, &Transport->ChainEngine))
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
ZpClientQuic_Start(
    _In_opt_ PVOID Context)
{
    QUIC_STATUS QuicStatus;
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;
    PZP_CLIENT_OBJECT Object = Transport->Owner;
    const ZP_ENDPOINT* Endpoint = &Object->Config.Endpoints[Transport->EndpointIndex];
    QUIC_SETTINGS Settings = { 0 };
    QUIC_CREDENTIAL_CONFIG Credentials = { 0 };
    QUIC_ADDR Address;
    NTSTATUS Status;
    PSTR ServerName;
    ULONG ServerNameSize;

    ZpClientQuic_Uninitialize(Transport);
    Transport->Owner = Object;
    Transport->ShutdownStatus = STATUS_SUCCESS;

    QuicStatus = KNSoftQuicInitialize();
    if (QUIC_FAILED(QuicStatus))
    {
        return ZpQuic_StatusToNtStatus(QuicStatus);
    }
    Transport->Initialized = TRUE;
    QuicStatus = MsQuicRegistrationOpen(&ZpClientQuicRegistrationConfig,
                                        &Transport->Registration);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpQuic_StatusToNtStatus(QuicStatus);
        goto Cleanup;
    }
    Status = ZpClientQuic_CreateRootChainEngine(Transport);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    Settings.HandshakeIdleTimeoutMs = Object->Config.ConnectTimeoutMilliseconds;
    Settings.IsSet.HandshakeIdleTimeoutMs = TRUE;
    Credentials.Type = QUIC_CREDENTIAL_TYPE_NONE;
    Credentials.Flags = QUIC_CREDENTIAL_FLAG_CLIENT |
                        QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED |
                        QUIC_CREDENTIAL_FLAG_DEFER_CERTIFICATE_VALIDATION |
                        QUIC_CREDENTIAL_FLAG_INPROC_PEER_CERTIFICATE;
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
        Status = ZpQuic_StatusToNtStatus(QuicStatus);
        goto Cleanup;
    }
    QuicStatus = MsQuicConnectionOpen(Transport->Registration,
                                      ZpClientQuic_ConnectionCallback,
                                      Transport,
                                      &Transport->Connection);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpQuic_StatusToNtStatus(QuicStatus);
        goto Cleanup;
    }
    Status = ZpQuic_ResolveAddress(Endpoint->Host, Endpoint->Port, &Address);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    QuicStatus = MsQuicSetParam(Transport->Connection,
                                QUIC_PARAM_CONN_REMOTE_ADDRESS,
                                sizeof(Address),
                                &Address);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpQuic_StatusToNtStatus(QuicStatus);
        goto Cleanup;
    }
    ServerNameSize = Str_UnicodeToUtf8(NULL, 0, Endpoint->ServerName);
    ServerName = Mem_Alloc(ServerNameSize);
    if (ServerName == NULL ||
        Str_UnicodeToUtf8(ServerName, ServerNameSize, Endpoint->ServerName) == 0)
    {
        Mem_Free(ServerName);
        Status = STATUS_NO_MEMORY;
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
        Status = ZpQuic_StatusToNtStatus(QuicStatus);
        goto Cleanup;
    }
    return STATUS_SUCCESS;

Cleanup:
    ZpClientQuic_Uninitialize(Transport);
    Transport->Owner = Object;
    return Status;
}

static
VOID
NTAPI
ZpClientQuic_Stop(
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;
    PZP_CLIENT_OBJECT Object = Transport->Owner;

    RtlAcquireSRWLockShared(&Object->Lock);
    if (Transport->Connection != NULL)
    {
        InterlockedExchange((volatile LONG*)&Transport->ShutdownStatus, STATUS_SUCCESS);
        MsQuicConnectionShutdown(Transport->Connection,
                                 QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                 0);
        RtlReleaseSRWLockShared(&Object->Lock);
        return;
    }
    RtlReleaseSRWLockShared(&Object->Lock);
    ZpClient_NotifyState((ZP_CLIENT_HANDLE)Object,
                         ZpClientStateStopped,
                         STATUS_SUCCESS);
}

static const ZP_TRANSPORT_OPERATIONS ZpClientQuicOperations = {
    ZpClientQuic_Start,
    ZpClientQuic_Stop
};

VOID
ZpClientQuic_Configure(
    _Inout_ PZP_CLIENT_OBJECT Object)
{
    ULONG Index;

    Object->QuicTransport.Owner = Object;
    for (Index = 0; Index < min(Object->Config.EndpointCount, 1UL); Index++)
    {
        if (Object->Config.Endpoints[Index].Transport == ZpTransportQuic)
        {
            Object->QuicTransport.EndpointIndex = Index;
            ZpClient_SetTransport((ZP_CLIENT_HANDLE)Object,
                                  &ZpClientQuicOperations,
                                  &Object->QuicTransport);
            break;
        }
    }
}

VOID
ZpClientQuic_Uninitialize(
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
    if (Transport->ChainEngine != NULL)
    {
        CertFreeCertificateChainEngine(Transport->ChainEngine);
        Transport->ChainEngine = NULL;
    }
    if (Transport->RootStore != NULL)
    {
        CertCloseStore(Transport->RootStore, 0);
        Transport->RootStore = NULL;
    }
    if (Transport->Initialized)
    {
        KNSoftQuicUninitialize();
        Transport->Initialized = FALSE;
    }
}
