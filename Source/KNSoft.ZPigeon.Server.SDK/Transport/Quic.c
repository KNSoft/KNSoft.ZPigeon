#include "../Server.inl"
#include "../Core/Session.h"
#include "../../Network/Quic.inl"

typedef struct _ZP_SERVER_QUIC_CONNECTION
{
    ZP_CONNECTION_OBJECT Public;
    PZP_SERVER_QUIC_TRANSPORT Transport;
    HQUIC Connection;
    HQUIC Stream;
    ZP_STATUS ShutdownStatus;
    ZP_SERVER_SESSION Session;
} ZP_SERVER_QUIC_CONNECTION, *PZP_SERVER_QUIC_CONNECTION;

static
VOID
ZpServerQuic_SetShutdownStatus(
    _Inout_ PZP_SERVER_QUIC_CONNECTION Connection,
    _In_ ZP_STATUS Status)
{
    RtlAcquireSRWLockExclusive(&Connection->Public.Lock);
    if (Connection->ShutdownStatus.Type == ZpStatusNone)
    {
        Connection->ShutdownStatus = Status;
    }
    RtlReleaseSRWLockExclusive(&Connection->Public.Lock);
}

static
ZP_STATUS
ZpServerQuic_GetShutdownStatus(
    _Inout_ PZP_SERVER_QUIC_CONNECTION Connection)
{
    ZP_STATUS Status;

    RtlAcquireSRWLockShared(&Connection->Public.Lock);
    Status = Connection->ShutdownStatus;
    RtlReleaseSRWLockShared(&Connection->Public.Lock);
    return Status;
}

static
VOID
ZpServerQuic_TryCompleteStop(
    _Inout_ PZP_SERVER_QUIC_TRANSPORT Transport);

static
VOID
NTAPI
ZpServerQuic_DestroyConnection(
    _Inout_ PZP_CONNECTION_OBJECT Connection);

static
NTSTATUS
NTAPI
ZpServerQuic_Send(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PZP_SERVER_QUIC_CONNECTION QuicConnection = CONTAINING_RECORD(
        Connection,
        ZP_SERVER_QUIC_CONNECTION,
        Public);

    QUIC_STATUS QuicStatus = QUIC_STATUS_SUCCESS;
    NTSTATUS Status;

    if (QuicConnection->Stream == NULL)
    {
        return STATUS_CONNECTION_DISCONNECTED;
    }
    Status = ZpQuic_SendFrame(QuicConnection->Stream,
                              &QuicConnection->Session.Connection,
                              MessageType,
                              Body,
                              BodyLength,
                              &QuicStatus);
    if (!NT_SUCCESS(Status))
    {
        ZpServerQuic_SetShutdownStatus(
            QuicConnection,
            QUIC_FAILED(QuicStatus) ?
                ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus) :
                ZpStatus_FromNtStatus(Status));
        MsQuicConnectionShutdown(QuicConnection->Connection,
                                 QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                 0);
    }
    return Status;
}

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
        ZpServer_TransportStopped((ZP_SERVER_HANDLE)Object,
                                  ZpTransportQuic,
                                  ZpStatus_FromNtStatus(STATUS_SUCCESS));
    }
}

static
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS
QUIC_API
ZpServerQuic_StreamCallback(
    _In_ HQUIC Stream,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_STREAM_EVENT* Event)
{
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Context;
    NTSTATUS Status;
    ULONG Index;

    if (QuicConnection == NULL)
    {
        return QUIC_STATUS_INVALID_PARAMETER;
    }
    switch (Event->Type)
    {
        case QUIC_STREAM_EVENT_SEND_COMPLETE:
            ZpQuic_CompleteSend(Event->SEND_COMPLETE.ClientContext);
            break;

        case QUIC_STREAM_EVENT_RECEIVE:
            Status = STATUS_SUCCESS;
            for (Index = 0;
                 NT_SUCCESS(Status) && Index < Event->RECEIVE.BufferCount;
                 Index++)
            {
                Status = ZpServerSession_Receive(&QuicConnection->Session,
                                                 Event->RECEIVE.Buffers[Index].Buffer,
                                                 Event->RECEIVE.Buffers[Index].Length);
            }
            if (!NT_SUCCESS(Status))
            {
                ZpServerQuic_SetShutdownStatus(
                    QuicConnection,
                    ZpStatus_FromNtStatus(Status));
                MsQuicConnectionShutdown(QuicConnection->Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
            }
            break;

        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
            ZpServerQuic_SetShutdownStatus(
                QuicConnection,
                ZpStatus_FromNtStatus(STATUS_CONNECTION_DISCONNECTED));
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
VOID
NTAPI
ZpServerQuic_DestroyConnection(
    _Inout_ PZP_CONNECTION_OBJECT Connection)
{
    PZP_SERVER_QUIC_CONNECTION QuicConnection = CONTAINING_RECORD(
        Connection,
        ZP_SERVER_QUIC_CONNECTION,
        Public);
    PZP_SERVER_QUIC_TRANSPORT Transport;
    PZP_SERVER_OBJECT Object;

    Transport = QuicConnection->Transport;
    Object = Transport->Owner;
    ZpServerSession_Uninitialize(&QuicConnection->Session);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Transport->ActiveConnectionCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    Mem_Free(QuicConnection);
    ZpServerQuic_TryCompleteStop(Transport);
}

static
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS
QUIC_API
ZpServerQuic_ConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event)
{
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Context;
    PZP_SERVER_QUIC_TRANSPORT Transport;
    PZP_SERVER_OBJECT Object;
    NTSTATUS Status;
    ZP_STATUS ShutdownStatus;

    if (QuicConnection == NULL)
    {
        return QUIC_STATUS_INVALID_PARAMETER;
    }
    Transport = QuicConnection->Transport;
    Object = Transport->Owner;
    switch (Event->Type)
    {
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
            if ((Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) != 0 ||
                QuicConnection->Stream != NULL)
            {
                ZpServerQuic_SetShutdownStatus(
                    QuicConnection,
                    ZpStatus_FromNtStatus(STATUS_PROTOCOL_UNREACHABLE));
                MsQuicConnectionShutdown(Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
                break;
            }
            Status = ZpServerSession_Initialize(&QuicConnection->Session,
                                                Object,
                                                &QuicConnection->Public);
            if (!NT_SUCCESS(Status))
            {
                ZpServerQuic_SetShutdownStatus(
                    QuicConnection,
                    ZpStatus_FromNtStatus(Status));
                MsQuicConnectionShutdown(Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
                break;
            }
            QuicConnection->Stream = Event->PEER_STREAM_STARTED.Stream;
            MsQuicSetCallbackHandler(QuicConnection->Stream,
                                     ZpServerQuic_StreamCallback,
                                     QuicConnection);
            ZpServerConnection_SetPhase(&QuicConnection->Public,
                                        ZpConnectionPhaseAuthenticating);
            ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Object,
                                       (ZP_CONNECTION_HANDLE)&QuicConnection->Public,
                                       ZpConnectionPhaseAuthenticating,
                                       ZpStatus_FromNtStatus(STATUS_SUCCESS));
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            ZpServerQuic_SetShutdownStatus(
                QuicConnection,
                ZpStatus_FromCode(
                    ZpStatusQuic,
                    (ULONG)Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status));
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
            ZpServerQuic_SetShutdownStatus(
                QuicConnection,
                ZpStatus_FromNtStatus(STATUS_CONNECTION_DISCONNECTED));
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            ShutdownStatus = ZpServerQuic_GetShutdownStatus(QuicConnection);
            ZpServerConnection_Close(&QuicConnection->Public, ShutdownStatus);
            MsQuicConnectionClose(Connection);
            QuicConnection->Connection = NULL;
            ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Object,
                                       (ZP_CONNECTION_HANDLE)&QuicConnection->Public,
                                      ZpConnectionPhaseClosed,
                                       ShutdownStatus);
            ZpConnection_Release((ZP_CONNECTION_HANDLE)&QuicConnection->Public);
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
_Function_class_(QUIC_LISTENER_CALLBACK)
QUIC_STATUS
QUIC_API
ZpServerQuic_ListenerCallback(
    _In_ HQUIC Listener,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_LISTENER_EVENT* Event)
{
    PZP_SERVER_QUIC_LISTENER QuicListener = Context;
    PZP_SERVER_QUIC_TRANSPORT Transport;
    PZP_SERVER_OBJECT Object;
    PZP_SERVER_QUIC_CONNECTION QuicConnection;
    QUIC_STATUS QuicStatus;
    NTSTATUS Status;
    LONG DeploymentIndex;

    if (QuicListener == NULL)
    {
        return QUIC_STATUS_INVALID_PARAMETER;
    }
    Transport = QuicListener->Transport;
    Object = Transport->Owner;
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
            Status = ZpServerConnection_Initialize(
                         &QuicConnection->Public,
                         Object->Config.MaxRequestsPerConnection,
                         Object->Config.MaxChannelsPerConnection,
                         ZpServerQuic_Send,
                         ZpServerQuic_DestroyConnection);
            if (!NT_SUCCESS(Status))
            {
                Mem_Free(QuicConnection);
                return QUIC_STATUS_OUT_OF_MEMORY;
            }
            QuicConnection->Connection = Event->NEW_CONNECTION.Connection;
            QuicConnection->ShutdownStatus = ZpStatus_FromNtStatus(STATUS_SUCCESS);
            MsQuicSetCallbackHandler(QuicConnection->Connection,
                                     ZpServerQuic_ConnectionCallback,
                                     QuicConnection);
            RtlAcquireSRWLockExclusive(&Object->Lock);
            Transport->ActiveConnectionCount++;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Object,
                                       (ZP_CONNECTION_HANDLE)&QuicConnection->Public,
                                       ZpConnectionPhaseConnecting,
                                       ZpStatus_FromNtStatus(STATUS_SUCCESS));
            QuicStatus = MsQuicConnectionSetConfiguration(
                QuicConnection->Connection,
                Transport->Configurations[DeploymentIndex]);
            if (QUIC_FAILED(QuicStatus))
            {
                ZpServerConnection_SetPhase(&QuicConnection->Public,
                                            ZpConnectionPhaseClosed);
                ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Object,
                                           (ZP_CONNECTION_HANDLE)&QuicConnection->Public,
                                           ZpConnectionPhaseClosed,
                                           ZpStatus_FromCode(ZpStatusQuic,
                                                             (ULONG)QuicStatus));
                ZpConnection_Release((ZP_CONNECTION_HANDLE)&QuicConnection->Public);
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
ZP_STATUS
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
            return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
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
            return ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
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
            return ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
        }
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ZP_STATUS
NTAPI
ZpServerQuic_Start(
    _In_opt_ PVOID Context,
    _In_ ULONG EndpointIndex)
{
    PZP_SERVER_QUIC_TRANSPORT Transport = Context;
    PZP_SERVER_OBJECT Object = Transport->Owner;
    QUIC_STATUS QuicStatus;
    QUIC_ADDR Address;
    ZP_STATUS Status;
    ULONG Index;

    UNREFERENCED_PARAMETER(EndpointIndex);

    ZpServerQuic_Uninitialize(Transport);
    Transport->Owner = Object;
    QuicStatus = KNSoftQuicInitialize();
    if (QUIC_FAILED(QuicStatus))
    {
        return ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
    }
    Transport->Initialized = TRUE;
    QuicStatus = MsQuicRegistrationOpen(&ZpServerQuicRegistrationConfig,
                                        &Transport->Registration);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
        goto Cleanup;
    }
    Status = ZpServerQuic_CreateConfigurations(Transport);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Cleanup;
    }
    for (Index = 0; Index < Object->Config.ListenerCount; Index++)
    {
        if (Object->Config.Listeners[Index].Transport != ZpTransportQuic)
        {
            continue;
        }
        Transport->Listeners[Index].Transport = Transport;
        QuicStatus = MsQuicListenerOpen(Transport->Registration,
                                        ZpServerQuic_ListenerCallback,
                                        &Transport->Listeners[Index],
                                        &Transport->Listeners[Index].Handle);
        if (QUIC_FAILED(QuicStatus))
        {
            Status = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
            goto Cleanup;
        }
        Status = ZpQuic_ResolveAddress(Object->Config.Listeners[Index].Host,
                                       Object->Config.Listeners[Index].Port,
                                       &Address);
        if (!ZpStatus_IsSuccess(Status))
        {
            goto Cleanup;
        }
        QuicStatus = MsQuicListenerStart(Transport->Listeners[Index].Handle,
                                         &ZpQuicAlpn,
                                         1,
                                         &Address);
        if (QUIC_FAILED(QuicStatus))
        {
            Status = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
            goto Cleanup;
        }
        Transport->StartedListenerCount++;
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);

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
    for (Index = 0; Index < Object->Config.ListenerCount; Index++)
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
    ZpServerQuic_Stop,
    NULL
};

VOID
ZpServerQuic_Configure(
    _Inout_ PZP_SERVER_OBJECT Object)
{
    ULONG Index;

    Object->QuicTransport.Owner = Object;
    for (Index = 0; Index < Object->Config.ListenerCount; Index++)
    {
        if (Object->Config.Listeners[Index].Transport == ZpTransportQuic)
        {
            ZpServer_SetTransport((ZP_SERVER_HANDLE)Object,
                                  ZpTransportQuic,
                                  &ZpServerQuicOperations,
                                  &Object->QuicTransport);
            return;
        }
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
