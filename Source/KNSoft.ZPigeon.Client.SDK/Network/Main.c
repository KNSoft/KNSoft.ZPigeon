#include "../Client.inl"
#include "../../Network/Config.inl"

static
NTSTATUS
ZpClient_ValidateConfig(
    _In_ PCZP_CLIENT_CONFIG Config,
    _Out_ PSIZE_T AllocationSize)
{
    NTSTATUS Status;
    SIZE_T Size, StringSize;
    ULONG Index;

    if (Config->Size != sizeof(*Config) ||
        Config->EndpointCount > ZP_ENDPOINT_MAX_COUNT ||
        (Config->EndpointCount != 0 && Config->Endpoints == NULL) ||
        Config->DeploymentRootCertificate == NULL ||
        Config->DeploymentRootCertificateLength == 0 ||
        Config->DeploymentRootCertificateLength > ZP_CERTIFICATE_MAX_SIZE ||
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
        Status = ZpConfig_AddStringSize(&Size, Endpoint->WssPath, FALSE, &StringSize);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if ((Endpoint->Transport != ZpTransportWss && StringSize != 0) ||
            (StringSize != 0 && Endpoint->WssPath[0] != L'/'))
        {
            return STATUS_INVALID_PARAMETER;
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
    Object->State = ZpClientStateStopped;
    Object->Config = *Config;
    Object->Config.ConnectTimeoutMilliseconds = Config->ConnectTimeoutMilliseconds != 0 ?
                                                    Config->ConnectTimeoutMilliseconds :
                                                    ZP_CLIENT_DEFAULT_CONNECT_TIMEOUT_MILLISECONDS;

    Cursor = Add2Ptr(Object, sizeof(*Object));
    Endpoints = (PZP_ENDPOINT)Cursor;
    Cursor += (SIZE_T)Config->EndpointCount * sizeof(*Endpoints);
    Modules = (PZP_MODULE_RECORD)Cursor;
    Cursor += (SIZE_T)Config->ModuleCount * sizeof(*Modules);
    Object->Config.Endpoints = Config->EndpointCount != 0 ? Endpoints : NULL;
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
        ZpConfig_CopyString(&Cursor, Config->Endpoints[Index].WssPath, &Endpoints[Index].WssPath);
    }
    Object->Config.DeploymentRootCertificate = Cursor;
    RtlCopyMemory(Cursor,
                  Config->DeploymentRootCertificate,
                  Config->DeploymentRootCertificateLength);
    ZpClientQuic_Configure(Object);
    *Client = (ZP_CLIENT_HANDLE)Object;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ZpClient_Start(
    _In_ ZP_CLIENT_HANDLE Client)
{
    NTSTATUS Status;
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    PCZP_TRANSPORT_OPERATIONS Operations;
    PVOID TransportContext;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpClientStateStopped)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Object->Config.EndpointCount == 0)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_PARAMETER;
    }
    if (Object->TransportOperations == NULL)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_NOT_SUPPORTED;
    }
    Object->State = ZpClientStateConnecting;
    Object->CallbackCount++;
    Operations = Object->TransportOperations;
    TransportContext = Object->TransportContext;
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Object->Config.StateCallback(Client,
                                 ZpClientStateConnecting,
                                 STATUS_SUCCESS,
                                 Object->Config.CallbackContext);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    if (Object->State != ZpClientStateConnecting)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_SUCCESS;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Status = Operations->Start(TransportContext);
    if (!NT_SUCCESS(Status))
    {
        ZpClient_NotifyState(Client, ZpClientStateStopped, Status);
    }
    return Status;
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
    Operations = Object->TransportOperations;
    TransportContext = Object->TransportContext;
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Object->Config.StateCallback(Client,
                                 ZpClientStateStopping,
                                 STATUS_SUCCESS,
                                 Object->Config.CallbackContext);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    Operations->Stop(TransportContext);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpClient_SetTransport(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ PCZP_TRANSPORT_OPERATIONS Operations,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;

    if (Operations == NULL || Operations->Start == NULL || Operations->Stop == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpClientStateStopped)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Object->TransportOperations = Operations;
    Object->TransportContext = Context;
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
    _In_ NTSTATUS Status)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ZpClient_IsTransitionValid(Object->State, State))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Object->State = State;
    Object->CallbackCount++;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    Object->Config.StateCallback(Client, State, Status, Object->Config.CallbackContext);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ZpClient_Close(
    _In_ ZP_CLIENT_HANDLE Client)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpClientStateStopped || Object->CallbackCount != 0)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_DEVICE_BUSY;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    ZpClientQuic_Uninitialize(&Object->QuicTransport);
    Mem_Free(Object);
    return STATUS_SUCCESS;
}
