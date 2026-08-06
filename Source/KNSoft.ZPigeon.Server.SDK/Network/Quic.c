#include "../Server.inl"
#include "../../Network/Quic.inl"

typedef struct _ZP_SERVER_QUIC_CONNECTION
{
    PZP_SERVER_QUIC_TRANSPORT Transport;
    HQUIC Connection;
    HQUIC Stream;
    NTSTATUS ShutdownStatus;
} ZP_SERVER_QUIC_CONNECTION, *PZP_SERVER_QUIC_CONNECTION;

static const QUIC_REGISTRATION_CONFIG ZpServerQuicRegistrationConfig = {
    "KNSoft.ZPigeon.Server",
    QUIC_EXECUTION_PROFILE_LOW_LATENCY
};

static
VOID
ZpServerQuic_TryCompleteStop(
    _Inout_ PZP_SERVER_QUIC_TRANSPORT Transport)
{
    PZP_SERVER_OBJECT Object = Transport->Owner;
    LOGICAL Complete;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Complete = Transport->Stopping &&
               Transport->ActiveConnectionCount == 0 &&
               Transport->StoppedListenerCount == Transport->StartedListenerCount;
    if (Complete)
    {
        Transport->Stopping = FALSE;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Complete)
    {
        ZpServer_NotifyState((ZP_SERVER_HANDLE)Object,
                             ZpServerStateStopped,
                             STATUS_SUCCESS);
    }
}

static
QUIC_STATUS
QUIC_API
ZpServerQuic_StreamCallback(
    _In_ HQUIC Stream,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_STREAM_EVENT* Event)
{
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Context;

    switch (Event->Type)
    {
        case QUIC_STREAM_EVENT_RECEIVE:
            InterlockedExchange((volatile LONG*)&QuicConnection->ShutdownStatus,
                                STATUS_PROTOCOL_UNREACHABLE);
            MsQuicConnectionShutdown(QuicConnection->Connection,
                                     QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                     0);
            break;

        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
            InterlockedExchange((volatile LONG*)&QuicConnection->ShutdownStatus,
                                STATUS_CONNECTION_DISCONNECTED);
            MsQuicConnectionShutdown(QuicConnection->Connection,
                                     QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                     0);
            break;

        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            if (QuicConnection->Stream == Stream)
            {
                QuicConnection->Stream = NULL;
            }
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
ZpServerQuic_ConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event)
{
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Context;
    PZP_SERVER_QUIC_TRANSPORT Transport = QuicConnection->Transport;
    PZP_SERVER_OBJECT Object = Transport->Owner;
    NTSTATUS Status;

    switch (Event->Type)
    {
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
            if ((Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) != 0 ||
                QuicConnection->Stream != NULL)
            {
                InterlockedExchange((volatile LONG*)&QuicConnection->ShutdownStatus,
                                    STATUS_PROTOCOL_UNREACHABLE);
                MsQuicConnectionShutdown(Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
                break;
            }
            QuicConnection->Stream = Event->PEER_STREAM_STARTED.Stream;
            MsQuicSetCallbackHandler(QuicConnection->Stream,
                                     ZpServerQuic_StreamCallback,
                                     QuicConnection);
            ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Object,
                                      (ZP_CONNECTION_HANDLE)QuicConnection,
                                      ZpConnectionPhaseAuthenticating,
                                      STATUS_SUCCESS);
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            InterlockedExchange(
                (volatile LONG*)&QuicConnection->ShutdownStatus,
                ZpQuic_StatusToNtStatus(Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status));
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
            InterlockedExchange((volatile LONG*)&QuicConnection->ShutdownStatus,
                                STATUS_CONNECTION_DISCONNECTED);
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            Status = QuicConnection->ShutdownStatus;
            MsQuicConnectionClose(Connection);
            QuicConnection->Connection = NULL;
            ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Object,
                                      (ZP_CONNECTION_HANDLE)QuicConnection,
                                      ZpConnectionPhaseClosed,
                                      Status);
            RtlAcquireSRWLockExclusive(&Object->Lock);
            Transport->ActiveConnectionCount--;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            ZpServerQuic_TryCompleteStop(Transport);
            Mem_Free(QuicConnection);
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

static
LONG
ZpServerQuic_FindDeployment(
    _In_ PZP_SERVER_QUIC_TRANSPORT Transport,
    _In_ const QUIC_NEW_CONNECTION_INFO* Info)
{
    ULONG Index;
    SIZE_T Length;

    if (Info->ServerName == NULL || Info->ServerNameLength == 0)
    {
        return -1;
    }
    for (Index = 0; Index < Transport->Owner->Config.DeploymentCount; Index++)
    {
        Length = strlen(Transport->ServerNames[Index]);
        if (Length == Info->ServerNameLength &&
            _strnicmp(Transport->ServerNames[Index],
                      Info->ServerName,
                      Info->ServerNameLength) == 0)
        {
            return (LONG)Index;
        }
    }
    return -1;
}

static
QUIC_STATUS
QUIC_API
ZpServerQuic_ListenerCallback(
    _In_ HQUIC Listener,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_LISTENER_EVENT* Event)
{
    PZP_SERVER_QUIC_LISTENER QuicListener = Context;
    PZP_SERVER_QUIC_TRANSPORT Transport = QuicListener->Transport;
    PZP_SERVER_OBJECT Object = Transport->Owner;
    PZP_SERVER_QUIC_CONNECTION QuicConnection;
    QUIC_STATUS QuicStatus;
    LONG DeploymentIndex;

    switch (Event->Type)
    {
        case QUIC_LISTENER_EVENT_NEW_CONNECTION:
            DeploymentIndex = ZpServerQuic_FindDeployment(Transport,
                                                           Event->NEW_CONNECTION.Info);
            if (DeploymentIndex < 0)
            {
                return QUIC_STATUS_NOT_SUPPORTED;
            }
            QuicConnection = Mem_Alloc(sizeof(*QuicConnection));
            if (QuicConnection == NULL)
            {
                return QUIC_STATUS_OUT_OF_MEMORY;
            }
            RtlZeroMemory(QuicConnection, sizeof(*QuicConnection));
            QuicConnection->Transport = Transport;
            QuicConnection->Connection = Event->NEW_CONNECTION.Connection;
            QuicConnection->ShutdownStatus = STATUS_SUCCESS;
            MsQuicSetCallbackHandler(QuicConnection->Connection,
                                     ZpServerQuic_ConnectionCallback,
                                     QuicConnection);
            RtlAcquireSRWLockExclusive(&Object->Lock);
            Transport->ActiveConnectionCount++;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Object,
                                      (ZP_CONNECTION_HANDLE)QuicConnection,
                                      ZpConnectionPhaseConnecting,
                                      STATUS_SUCCESS);
            QuicStatus = MsQuicConnectionSetConfiguration(
                QuicConnection->Connection,
                Transport->Configurations[DeploymentIndex]);
            if (QUIC_FAILED(QuicStatus))
            {
                ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Object,
                                          (ZP_CONNECTION_HANDLE)QuicConnection,
                                          ZpConnectionPhaseClosed,
                                          ZpQuic_StatusToNtStatus(QuicStatus));
                RtlAcquireSRWLockExclusive(&Object->Lock);
                Transport->ActiveConnectionCount--;
                RtlReleaseSRWLockExclusive(&Object->Lock);
                Mem_Free(QuicConnection);
                return QuicStatus;
            }
            return QUIC_STATUS_SUCCESS;

        case QUIC_LISTENER_EVENT_STOP_COMPLETE:
            RtlAcquireSRWLockExclusive(&Object->Lock);
            if (QuicListener->Handle == Listener)
            {
                QuicListener->Handle = NULL;
                Transport->StoppedListenerCount++;
            }
            RtlReleaseSRWLockExclusive(&Object->Lock);
            if (!Event->STOP_COMPLETE.AppCloseInProgress)
            {
                MsQuicListenerClose(Listener);
            }
            ZpServerQuic_TryCompleteStop(Transport);
            return QUIC_STATUS_SUCCESS;
    }
    return QUIC_STATUS_SUCCESS;
}

static
NTSTATUS
ZpServerQuic_CreateConfigurations(
    _Inout_ PZP_SERVER_QUIC_TRANSPORT Transport)
{
    PZP_SERVER_OBJECT Object = Transport->Owner;
    QUIC_SETTINGS Settings = { 0 };
    QUIC_CREDENTIAL_CONFIG Credentials = { 0 };
    QUIC_STATUS QuicStatus;
    ULONG Index, ServerNameSize;

    Settings.PeerBidiStreamCount = 1;
    Settings.IsSet.PeerBidiStreamCount = TRUE;
    for (Index = 0; Index < Object->Config.DeploymentCount; Index++)
    {
        ServerNameSize = Str_UnicodeToUtf8(NULL,
                                           0,
                                           Object->Config.Deployments[Index].ServerName);
        Transport->ServerNames[Index] = Mem_Alloc(ServerNameSize);
        if (Transport->ServerNames[Index] == NULL ||
            Str_UnicodeToUtf8(Transport->ServerNames[Index],
                              ServerNameSize,
                              Object->Config.Deployments[Index].ServerName) == 0)
        {
            return STATUS_NO_MEMORY;
        }
        QuicStatus = MsQuicConfigurationOpen(Transport->Registration,
                                             &ZpQuicAlpn,
                                             1,
                                             &Settings,
                                             sizeof(Settings),
                                             NULL,
                                             &Transport->Configurations[Index]);
        if (QUIC_FAILED(QuicStatus))
        {
            return ZpQuic_StatusToNtStatus(QuicStatus);
        }
        RtlZeroMemory(&Credentials, sizeof(Credentials));
        Credentials.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_CONTEXT;
        Credentials.CertificateContext = (QUIC_CERTIFICATE*)Object->Config.Deployments[
            Index].Certificate;
        QuicStatus = MsQuicConfigurationLoadCredential(
            Transport->Configurations[Index],
            &Credentials);
        if (QUIC_FAILED(QuicStatus))
        {
            return ZpQuic_StatusToNtStatus(QuicStatus);
        }
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
ZpServerQuic_Start(
    _In_opt_ PVOID Context)
{
    PZP_SERVER_QUIC_TRANSPORT Transport = Context;
    PZP_SERVER_OBJECT Object = Transport->Owner;
    QUIC_STATUS QuicStatus;
    QUIC_ADDR Address;
    NTSTATUS Status;
    ULONG Index;

    ZpServerQuic_Uninitialize(Transport);
    Transport->Owner = Object;
    QuicStatus = KNSoftQuicInitialize();
    if (QUIC_FAILED(QuicStatus))
    {
        return ZpQuic_StatusToNtStatus(QuicStatus);
    }
    Transport->Initialized = TRUE;
    QuicStatus = MsQuicRegistrationOpen(&ZpServerQuicRegistrationConfig,
                                        &Transport->Registration);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpQuic_StatusToNtStatus(QuicStatus);
        goto Cleanup;
    }
    Status = ZpServerQuic_CreateConfigurations(Transport);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    for (Index = 0; Index < Object->Config.ListenerCount; Index++)
    {
        Transport->Listeners[Index].Transport = Transport;
        Transport->Listeners[Index].Index = Index;
        QuicStatus = MsQuicListenerOpen(Transport->Registration,
                                        ZpServerQuic_ListenerCallback,
                                        &Transport->Listeners[Index],
                                        &Transport->Listeners[Index].Handle);
        if (QUIC_FAILED(QuicStatus))
        {
            Status = ZpQuic_StatusToNtStatus(QuicStatus);
            goto Cleanup;
        }
        Status = ZpQuic_ResolveAddress(Object->Config.Listeners[Index].Host,
                                       Object->Config.Listeners[Index].Port,
                                       &Address);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
        QuicStatus = MsQuicListenerStart(Transport->Listeners[Index].Handle,
                                         &ZpQuicAlpn,
                                         1,
                                         &Address);
        if (QUIC_FAILED(QuicStatus))
        {
            Status = ZpQuic_StatusToNtStatus(QuicStatus);
            goto Cleanup;
        }
        Transport->StartedListenerCount++;
    }
    Status = ZpServer_NotifyState((ZP_SERVER_HANDLE)Object,
                                  ZpServerStateRunning,
                                  STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        return STATUS_SUCCESS;
    }

Cleanup:
    ZpServerQuic_Uninitialize(Transport);
    Transport->Owner = Object;
    return Status;
}

static
VOID
NTAPI
ZpServerQuic_Stop(
    _In_opt_ PVOID Context)
{
    PZP_SERVER_QUIC_TRANSPORT Transport = Context;
    PZP_SERVER_OBJECT Object = Transport->Owner;
    ULONG Index;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Transport->Stopping = TRUE;
    Transport->StoppedListenerCount = 0;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    MsQuicRegistrationShutdown(Transport->Registration,
                               QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                               0);
    for (Index = 0; Index < Transport->StartedListenerCount; Index++)
    {
        if (Transport->Listeners[Index].Handle != NULL)
        {
            MsQuicListenerStop(Transport->Listeners[Index].Handle);
        }
    }
    ZpServerQuic_TryCompleteStop(Transport);
}

static const ZP_TRANSPORT_OPERATIONS ZpServerQuicOperations = {
    ZpServerQuic_Start,
    ZpServerQuic_Stop
};

VOID
ZpServerQuic_Configure(
    _Inout_ PZP_SERVER_OBJECT Object)
{
    ULONG Index;

    Object->QuicTransport.Owner = Object;
    for (Index = 0; Index < Object->Config.ListenerCount; Index++)
    {
        if (Object->Config.Listeners[Index].Transport != ZpTransportQuic)
        {
            return;
        }
    }
    if (Object->Config.ListenerCount != 0 && Object->Config.DeploymentCount != 0)
    {
        ZpServer_SetTransport((ZP_SERVER_HANDLE)Object,
                              &ZpServerQuicOperations,
                              &Object->QuicTransport);
    }
}

VOID
ZpServerQuic_Uninitialize(
    _Inout_ PZP_SERVER_QUIC_TRANSPORT Transport)
{
    ULONG Index;

    if (Transport->Registration != NULL)
    {
        MsQuicRegistrationShutdown(Transport->Registration,
                                   QUIC_CONNECTION_SHUTDOWN_FLAG_SILENT,
                                   0);
    }
    for (Index = 0; Index < Transport->Owner->Config.ListenerCount; Index++)
    {
        if (Transport->Listeners[Index].Handle != NULL)
        {
            MsQuicListenerClose(Transport->Listeners[Index].Handle);
            Transport->Listeners[Index].Handle = NULL;
        }
    }
    for (Index = 0; Index < Transport->Owner->Config.DeploymentCount; Index++)
    {
        if (Transport->Configurations[Index] != NULL)
        {
            MsQuicConfigurationClose(Transport->Configurations[Index]);
            Transport->Configurations[Index] = NULL;
        }
    }
    if (Transport->Registration != NULL)
    {
        MsQuicRegistrationClose(Transport->Registration);
        Transport->Registration = NULL;
    }
    for (Index = 0; Index < Transport->Owner->Config.DeploymentCount; Index++)
    {
        Mem_Free(Transport->ServerNames[Index]);
        Transport->ServerNames[Index] = NULL;
    }
    if (Transport->Initialized)
    {
        KNSoftQuicUninitialize();
        Transport->Initialized = FALSE;
    }
    Transport->Stopping = FALSE;
    Transport->StartedListenerCount = 0;
    Transport->StoppedListenerCount = 0;
    Transport->ActiveConnectionCount = 0;
}
