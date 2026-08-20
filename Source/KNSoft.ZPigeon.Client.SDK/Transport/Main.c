#include "../Client.inl"
#include "../Core/Channel.h"
#include "../../Modules/Execution/Client.h"
#include "../../Modules/File/Client.h"
#include "../../Network/Config.inl"
#include "Retry.inl"

#define ZP_CLIENT_DEBUG_RETRY_MILLISECONDS 5000

#include <Bcrypt.h>

static
NTSTATUS
ZpClient_ValidateConfig(
    _In_ PCZP_CLIENT_CONFIG Config,
    _Out_ PSIZE_T AllocationSize)
{
    NTSTATUS Status;
    SIZE_T Size, StringSize;
    ULONG Index;

    if (Config->EndpointCount == 0 ||
        Config->EndpointCount > ZP_ENDPOINT_MAX_COUNT ||
        Config->Endpoints == NULL ||
        Config->DeploymentRootCertificate == NULL ||
        Config->DeploymentRootCertificateLength == 0 ||
        Config->DeploymentRootCertificateLength > ZP_CERTIFICATE_MAX_SIZE ||
        Config->MaxRequestsPerConnection > ZP_CLIENT_MAX_REQUESTS_PER_CONNECTION ||
        Config->MaxRequestPayloadBytesPerConnection >
            ZP_CLIENT_MAX_REQUEST_PAYLOAD_BYTES_PER_CONNECTION ||
        Config->MaxChannelsPerConnection >
            ZP_CLIENT_MAX_CHANNELS_PER_CONNECTION ||
        Config->ClientKeyScope > ZpClientKeyUser ||
        Config->StateCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpConfig_ValidateModules(Config->Modules, Config->ModuleCount);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Size = sizeof(ZP_CLIENT_OBJECT) +
           (SIZE_T)Config->EndpointCount * sizeof(ZP_ENDPOINT) +
           (SIZE_T)Config->ModuleCount * sizeof(ZP_MODULE_RECORD) +
           Config->DeploymentRootCertificateLength;
    Status = ZpConfig_AddStringSize(&Size, Config->ClientKeyName, FALSE, &StringSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (StringSize != 0 && Config->ClientKeyName[0] == UNICODE_NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (Index = 0; Index < Config->EndpointCount; Index++)
    {
        PCZP_ENDPOINT Endpoint = &Config->Endpoints[Index];

        if (!ZpConfig_IsTransportValid(Endpoint->Transport) || Endpoint->Port == 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpConfig_AddStringSize(&Size, Endpoint->Host, TRUE, &StringSize);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        Status = ZpConfig_AddStringSize(&Size, Endpoint->ServerName, TRUE, &StringSize);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }
    *AllocationSize = Size;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ZpClient_Create(
    _In_ PCZP_CLIENT_CONFIG Config,
    _Out_ ZP_CLIENT_HANDLE* Client)
{
    NTSTATUS Status;
    SIZE_T AllocationSize;
    PBYTE Cursor;
    PZP_CLIENT_OBJECT Object;
    PZP_ENDPOINT Endpoints;
    PZP_MODULE_RECORD Modules;
    ULONG Index;

    Status = ZpClient_ValidateConfig(Config, &AllocationSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Object = Mem_Alloc(AllocationSize);
    if (Object == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Object, sizeof(*Object));
    RtlInitializeSRWLock(&Object->Lock);
    RtlInitializeSRWLock(&Object->FileEnumerationLock);
    RtlInitializeSRWLock(&Object->ExecutionLock);
    InitializeListHead(&Object->InboundRequests);
    InitializeListHead(&Object->LocalChannels);
    InitializeListHead(&Object->ExecutionJobs);
    InitializeListHead(&Object->FileEnumerations);
    Object->NextLocalChannelId = 1;
    Object->NextFileEnumerationId = 1;
    Object->NextExecutionJobId = 1;
    Object->State = ZpClientStateStopped;
    Object->Config = *Config;
    Object->Config.ConnectTimeoutMilliseconds = Config->ConnectTimeoutMilliseconds != 0 ?
                                                    Config->ConnectTimeoutMilliseconds :
                                                    ZP_CLIENT_DEFAULT_CONNECT_TIMEOUT_MILLISECONDS;
    Object->Config.MaxRequestsPerConnection = Config->MaxRequestsPerConnection != 0 ?
                                                  Config->MaxRequestsPerConnection :
                                                  ZP_CLIENT_DEFAULT_MAX_REQUESTS_PER_CONNECTION;
    Object->Config.MaxRequestPayloadBytesPerConnection =
        Config->MaxRequestPayloadBytesPerConnection != 0 ?
            Config->MaxRequestPayloadBytesPerConnection :
            ZP_CLIENT_DEFAULT_MAX_REQUEST_PAYLOAD_BYTES_PER_CONNECTION;
    Object->Config.MaxChannelsPerConnection =
        Config->MaxChannelsPerConnection != 0 ?
            Config->MaxChannelsPerConnection :
            ZP_CLIENT_DEFAULT_MAX_CHANNELS_PER_CONNECTION;
    Cursor = Add2Ptr(Object, sizeof(*Object));
    Endpoints = (PZP_ENDPOINT)Cursor;
    Cursor += (SIZE_T)Config->EndpointCount * sizeof(*Endpoints);
    Modules = (PZP_MODULE_RECORD)Cursor;
    Cursor += (SIZE_T)Config->ModuleCount * sizeof(*Modules);
    Object->Config.Endpoints = Endpoints;
    Object->Config.Modules = Config->ModuleCount != 0 ? Modules : NULL;
    if (Config->ModuleCount != 0)
    {
        RtlCopyMemory(Modules, Config->Modules, (SIZE_T)Config->ModuleCount * sizeof(*Modules));
    }
    ZpConfig_CopyString(&Cursor, Config->ClientKeyName, &Object->Config.ClientKeyName);

    for (Index = 0; Index < Config->EndpointCount; Index++)
    {
        Endpoints[Index] = Config->Endpoints[Index];
        ZpConfig_CopyString(&Cursor, Config->Endpoints[Index].Host, &Endpoints[Index].Host);
        ZpConfig_CopyString(&Cursor, Config->Endpoints[Index].ServerName, &Endpoints[Index].ServerName);
    }
    Object->Config.DeploymentRootCertificate = Cursor;
    RtlCopyMemory(Cursor,
                  Config->DeploymentRootCertificate,
                  Config->DeploymentRootCertificateLength);
    for (Index = 0; Index < Config->EndpointCount; Index++)
    {
        ZP_TRANSPORT_TYPE Transport = Config->Endpoints[Index].Transport;

        if (Object->TransportOperations[Transport] != NULL)
        {
            continue;
        }
        switch (Transport)
        {
            case ZpTransportQuic:
                ZpClientQuic_Configure(Object);
                break;

            case ZpTransportTcp:
                ZpClientTcp_Configure(Object);
                break;

            case ZpTransportUdp:
                ZpClientUdp_Configure(Object);
                break;

            default:
                break;
        }
    }
    *Client = (ZP_CLIENT_HANDLE)Object;
    return STATUS_SUCCESS;
}

static
LOGICAL
ZpClient_FindNextEndpoint(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ULONG StartIndex,
    _Out_ PULONG EndpointIndex)
{
    ULONG Index;

    for (Index = StartIndex; Index < Object->Config.EndpointCount; Index++)
    {
        if (Object->TransportOperations[Object->Config.Endpoints[Index].Transport] != NULL)
        {
            *EndpointIndex = Index;
            return TRUE;
        }
    }
    return FALSE;
}

static
VOID
ZpClient_ScheduleRetry(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_STATUS Status,
    _In_ LOGICAL RestartRound)
{
    LARGE_INTEGER DueTime;
    ULONG Delay, EndpointIndex;
#ifndef _DEBUG
    ULONG RandomValue;
#endif

    if (RestartRound)
    {
        Object->NextEndpointIndex = 0;
    }
    if (ZpClient_FindNextEndpoint(Object,
                                  Object->NextEndpointIndex,
                                  &EndpointIndex))
    {
        Object->NextEndpointIndex = EndpointIndex;
#ifdef _DEBUG
        Delay = ZP_CLIENT_DEBUG_RETRY_MILLISECONDS;
#else
        Delay = 1;
#endif
    }
    else
    {
        Object->NextEndpointIndex = 0;
#ifdef _DEBUG
        Delay = ZP_CLIENT_DEBUG_RETRY_MILLISECONDS;
#else
        if (!NT_SUCCESS(BCryptGenRandom(NULL,
                                        (PUCHAR)&RandomValue,
                                        sizeof(RandomValue),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        {
            RandomValue = ZpClientRetry_GetBaseDelay(Object->FailureRound) *
                          ZP_CLIENT_DEFAULT_RETRY_JITTER_PERCENT / 100;
        }
        Delay = ZpClientRetry_GetDelay(Object->FailureRound, RandomValue);
#endif
        if (Object->FailureRound != MAXULONG)
        {
            Object->FailureRound++;
        }
    }
    if (!NT_SUCCESS(ZpClient_NotifyState((ZP_CLIENT_HANDLE)Object,
                                         ZpClientStateRetryWait,
                                         Status)))
    {
        return;
    }

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State == ZpClientStateRetryWait)
    {
        Object->RetryPending = TRUE;
        Object->RetryDelay = Delay;
        if (!Object->StartPending)
        {
            DueTime.QuadPart = -(LONGLONG)Delay * 10000;
            SetThreadpoolTimer(Object->RetryTimer,
                               (PFILETIME)&DueTime,
                               0,
                               0);
        }
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
}

static
ZP_STATUS
ZpClient_StartEndpoints(
    _Inout_ PZP_CLIENT_OBJECT Object)
{
    PCZP_TRANSPORT_OPERATIONS Operations;
    PVOID TransportContext;
    ZP_STATUS Status = ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    ULONG Index;

    for (Index = Object->NextEndpointIndex;
         Index < Object->Config.EndpointCount;
         Index++)
    {
        Operations = Object->TransportOperations[Object->Config.Endpoints[Index].Transport];
        if (Operations == NULL)
        {
            continue;
        }
        TransportContext = Object->TransportContexts[Object->Config.Endpoints[Index].Transport];
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Object->ActiveTransport = Object->Config.Endpoints[Index].Transport;
        Object->EndpointIndex = Index;
        Object->NextEndpointIndex = Index + 1;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Status = Operations->Start(TransportContext, Index);
        if (ZpStatus_IsSuccess(Status))
        {
            return Status;
        }
    }
    return Status;
}

static
VOID
ZpClient_CompleteStart(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_STATUS Status)
{
    LARGE_INTEGER DueTime;
    PCZP_TRANSPORT_OPERATIONS Operations;
    PVOID TransportContext;
    ZP_CLIENT_STATE State;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->StartPending = FALSE;
    State = Object->State;
    Operations = Object->TransportOperations[Object->ActiveTransport];
    TransportContext = Object->TransportContexts[Object->ActiveTransport];
    if (State == ZpClientStateRetryWait && Object->RetryPending)
    {
        DueTime.QuadPart = -(LONGLONG)Object->RetryDelay * 10000;
        SetThreadpoolTimer(Object->RetryTimer,
                           (PFILETIME)&DueTime,
                           0,
                           0);
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);

    if (State == ZpClientStateStopping)
    {
        if (ZpStatus_IsSuccess(Status))
        {
            Operations->Stop(TransportContext);
        }
        else
        {
            ZpClient_NotifyState((ZP_CLIENT_HANDLE)Object,
                                 ZpClientStateStopped,
                                 ZpStatus_FromNtStatus(STATUS_SUCCESS));
        }
    }
    else if (!ZpStatus_IsSuccess(Status) &&
             State == ZpClientStateConnecting)
    {
        ZpClient_ScheduleRetry(Object, Status, FALSE);
    }
}

static
VOID
CALLBACK
ZpClient_RetryTimerCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context,
    _Inout_ PTP_TIMER Timer)
{
    PZP_CLIENT_OBJECT Object = Context;
    NTSTATUS Status;
    ZP_STATUS StartStatus = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    LOGICAL Start;

    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Timer);
    if (Object == NULL)
    {
        return;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Start = Object->State == ZpClientStateRetryWait &&
            Object->RetryPending &&
            !Object->StartPending;
    if (Start)
    {
        Object->RetryPending = FALSE;
        Object->StartPending = TRUE;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (!Start)
    {
        return;
    }

    Status = ZpClient_NotifyState((ZP_CLIENT_HANDLE)Object,
                                  ZpClientStateConnecting,
                                  ZpStatus_FromNtStatus(STATUS_SUCCESS));
    if (NT_SUCCESS(Status))
    {
        RtlAcquireSRWLockShared(&Object->Lock);
        Start = Object->State == ZpClientStateConnecting;
        RtlReleaseSRWLockShared(&Object->Lock);
        if (Start)
        {
            StartStatus = ZpClient_StartEndpoints(Object);
        }
    }
    else
    {
        StartStatus = ZpStatus_FromNtStatus(Status);
    }
    ZpClient_CompleteStart(Object, StartStatus);
}

NTSTATUS
NTAPI
ZpClient_Start(
    _In_ ZP_CLIENT_HANDLE Client)
{
    ZP_STATUS Status;
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    ULONG EndpointIndex;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpClientStateStopped)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (!ZpClient_FindNextEndpoint(Object, 0, &EndpointIndex))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_NOT_SUPPORTED;
    }
    if (Object->RetryTimer == NULL)
    {
        Object->RetryTimer = CreateThreadpoolTimer(ZpClient_RetryTimerCallback,
                                                    Object,
                                                    NULL);
        if (Object->RetryTimer == NULL)
        {
            RtlReleaseSRWLockExclusive(&Object->Lock);
            return STATUS_NO_MEMORY;
        }
    }
    Object->State = ZpClientStateConnecting;
    Object->CallbackCount++;
    Object->ActiveTransport = Object->Config.Endpoints[EndpointIndex].Transport;
    Object->EndpointIndex = EndpointIndex;
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Object->Config.StateCallback(Client,
                                 ZpClientStateConnecting,
                                 ZpStatus_FromNtStatus(STATUS_SUCCESS),
                                 Object->Config.CallbackContext);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    if (Object->State != ZpClientStateConnecting)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_SUCCESS;
    }
    Object->NextEndpointIndex = 0;
    Object->FailureRound = 0;
    Object->ReadyTickCount = 0;
    Object->StartPending = TRUE;
    Object->RetryPending = FALSE;
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Status = ZpClient_StartEndpoints(Object);
    ZpClient_CompleteStart(Object, Status);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ZpClient_Stop(
    _In_ ZP_CLIENT_HANDLE Client)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    PCZP_TRANSPORT_OPERATIONS Operations;
    PVOID TransportContext;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State == ZpClientStateStopped || Object->State == ZpClientStateStopping)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_SUCCESS;
    }
    Object->State = ZpClientStateStopping;
    Object->CallbackCount++;
    Object->RetryPending = FALSE;
    if (Object->RetryTimer != NULL)
    {
        SetThreadpoolTimer(Object->RetryTimer, NULL, 0, 0);
    }
    Operations = Object->TransportOperations[Object->ActiveTransport];
    TransportContext = Object->TransportContexts[Object->ActiveTransport];
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Object->Config.StateCallback(Client,
                                 ZpClientStateStopping,
                                 ZpStatus_FromNtStatus(STATUS_SUCCESS),
                                 Object->Config.CallbackContext);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    Operations->Stop(TransportContext);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ZpClient_Ping(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONGLONG Token)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    PCZP_TRANSPORT_OPERATIONS Operations;
    PVOID TransportContext;
    BYTE Body[sizeof(Token)];
    ULONG BodyLength;
    NTSTATUS Status;

    RtlAcquireSRWLockShared(&Object->Lock);
    if (Object->State != ZpClientStateReady)
    {
        RtlReleaseSRWLockShared(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Operations = Object->TransportOperations[Object->ActiveTransport];
    TransportContext = Object->TransportContexts[Object->ActiveTransport];
    if (Operations->Send == NULL)
    {
        RtlReleaseSRWLockShared(&Object->Lock);
        return STATUS_NOT_SUPPORTED;
    }
    Status = ZpMessage_EncodePing(Token, Body, sizeof(Body), &BodyLength);
    if (NT_SUCCESS(Status))
    {
        Status = Operations->Send(TransportContext,
                                  ZpMessagePing,
                                  Body,
                                  BodyLength);
    }
    RtlReleaseSRWLockShared(&Object->Lock);
    return Status;
}

NTSTATUS
ZpClient_SetTransport(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_TRANSPORT_TYPE Transport,
    _In_ PCZP_TRANSPORT_OPERATIONS Operations,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;

    if (!ZpConfig_IsTransportValid(Transport) ||
        Operations == NULL ||
        Operations->Start == NULL ||
        Operations->Stop == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpClientStateStopped)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Object->TransportOperations[Transport] = Operations;
    Object->TransportContexts[Transport] = Context;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return STATUS_SUCCESS;
}

static
LOGICAL
ZpClient_IsTransitionValid(
    _In_ ZP_CLIENT_STATE CurrentState,
    _In_ ZP_CLIENT_STATE State)
{
    switch (CurrentState)
    {
        case ZpClientStateConnecting:
            return State == ZpClientStateAuthenticating ||
                   State == ZpClientStateRetryWait ||
                   State == ZpClientStateStopped;

        case ZpClientStateAuthenticating:
            return State == ZpClientStateReady ||
                   State == ZpClientStateRetryWait ||
                   State == ZpClientStateStopped;

        case ZpClientStateReady:
            return State == ZpClientStateRetryWait || State == ZpClientStateStopped;

        case ZpClientStateRetryWait:
            return State == ZpClientStateConnecting || State == ZpClientStateStopped;

        case ZpClientStateStopping:
            return State == ZpClientStateStopped;
    }
    return FALSE;
}

NTSTATUS
ZpClient_NotifyState(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ZpClient_IsTransitionValid(Object->State, State))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Object->State = State;
    if (State == ZpClientStateAuthenticating)
    {
        Object->HighestInboundRequestId = 0;
        RtlZeroMemory(Object->ActiveModuleMask, sizeof(Object->ActiveModuleMask));
    }
    else if (State == ZpClientStateReady)
    {
        Object->ReadyTickCount = GetTickCount64();
        Object->NextLocalChannelId = 1;
    }
    Object->CallbackCount++;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    Object->Config.StateCallback(Client, State, Status, Object->Config.CallbackContext);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpClient_NotifyPong(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONGLONG Token)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpClientStateReady)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Object->Config.PongCallback == NULL)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_SUCCESS;
    }
    Object->CallbackCount++;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    Object->Config.PongCallback(Client, Token, Object->Config.CallbackContext);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return STATUS_SUCCESS;
}

VOID
ZpClient_TransportShutdown(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    ZP_CLIENT_STATE State;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    State = Object->State;
    if (State == ZpClientStateReady)
    {
        if (GetTickCount64() - Object->ReadyTickCount >=
            ZP_CLIENT_DEFAULT_STABLE_RESET_MILLISECONDS)
        {
            Object->FailureRound = 0;
        }
        Object->NextEndpointIndex = 0;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);

    ZpClient_CloseInboundRequests(Client);
    ZpFile_ResetEnumeration(Object);
    ZpClientLocalChannel_CloseAll(Object, Status);
    if (State == ZpClientStateStopping)
    {
        ZpClient_NotifyState(Client, ZpClientStateStopped, Status);
        return;
    }
    else if (State == ZpClientStateConnecting ||
             State == ZpClientStateAuthenticating ||
             State == ZpClientStateReady)
    {
        ZpClient_ScheduleRetry(Object,
                               Status,
                               State == ZpClientStateReady);
    }
}

NTSTATUS
NTAPI
ZpClient_Close(
    _In_ ZP_CLIENT_HANDLE Client)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpClientStateStopped ||
        Object->CallbackCount != 0 ||
        !IsListEmpty(&Object->LocalChannels))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_DEVICE_BUSY;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Object->RetryTimer != NULL)
    {
        SetThreadpoolTimer(Object->RetryTimer, NULL, 0, 0);
        WaitForThreadpoolTimerCallbacks(Object->RetryTimer, TRUE);
        CloseThreadpoolTimer(Object->RetryTimer);
        Object->RetryTimer = NULL;
    }
    if (Object->TransportOperations[ZpTransportQuic] != NULL)
    {
        ZpClientQuic_Uninitialize(&Object->QuicTransport);
    }
    if (Object->TransportOperations[ZpTransportTcp] != NULL)
    {
        ZpClientTcp_Uninitialize(&Object->TcpTransport);
    }
    if (Object->TransportOperations[ZpTransportUdp] != NULL)
    {
        ZpClientUdp_Uninitialize(&Object->UdpTransport);
    }
    ZpExecution_Cleanup(Object);
    Mem_Free(Object);
    return STATUS_SUCCESS;
}
