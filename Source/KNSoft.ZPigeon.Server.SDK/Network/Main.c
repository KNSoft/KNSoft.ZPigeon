#include "../Server.inl"
#include "../../Network/Config.inl"

static
NTSTATUS
ZpServer_ValidateConfig(
    _In_ PCZP_SERVER_CONFIG Config,
    _Out_ PSIZE_T AllocationSize)
{
    NTSTATUS Status;
    SIZE_T Size, StringSize;
    ULONG Index, PreviousIndex;
    UNICODE_STRING ServerName, PreviousServerName;

    if (Config->Size != sizeof(*Config) ||
        Config->ListenerCount > ZP_LISTENER_MAX_COUNT ||
        (Config->ListenerCount != 0 && Config->Listeners == NULL) ||
        Config->DeploymentCount > ZP_DEPLOYMENT_MAX_COUNT ||
        (Config->DeploymentCount != 0 && Config->Deployments == NULL) ||
        Config->StateCallback == NULL ||
        Config->ConnectionCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpConfig_ValidateModules(Config->Modules, Config->ModuleCount);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Size = sizeof(ZP_SERVER_OBJECT) +
           (SIZE_T)Config->ListenerCount * sizeof(ZP_LISTENER_ENDPOINT) +
           (SIZE_T)Config->DeploymentCount * sizeof(ZP_SERVER_DEPLOYMENT) +
           (SIZE_T)Config->ModuleCount * sizeof(ZP_MODULE_RECORD);
    for (Index = 0; Index < Config->ListenerCount; Index++)
    {
        PCZP_LISTENER_ENDPOINT Listener = &Config->Listeners[Index];

        if (!ZpConfig_IsTransportValid(Listener->Transport) || Listener->Port == 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpConfig_AddStringSize(&Size, Listener->Host, FALSE, &StringSize);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if (StringSize != 0 && Listener->Host[0] == UNICODE_NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpConfig_AddStringSize(&Size, Listener->WssPath, FALSE, &StringSize);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if ((Listener->Transport != ZpTransportWss && StringSize != 0) ||
            (StringSize != 0 && Listener->WssPath[0] != L'/'))
        {
            return STATUS_INVALID_PARAMETER;
        }
    }
    for (Index = 0; Index < Config->DeploymentCount; Index++)
    {
        Status = ZpConfig_AddStringSize(&Size,
                                        Config->Deployments[Index].ServerName,
                                        TRUE,
                                        &StringSize);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if (Config->Deployments[Index].Certificate == NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }
        RtlInitUnicodeString(&ServerName, Config->Deployments[Index].ServerName);
        for (PreviousIndex = 0; PreviousIndex < Index; PreviousIndex++)
        {
            RtlInitUnicodeString(&PreviousServerName,
                                 Config->Deployments[PreviousIndex].ServerName);
            if (RtlEqualUnicodeString(&ServerName, &PreviousServerName, TRUE))
            {
                return STATUS_INVALID_PARAMETER;
            }
        }
    }
    *AllocationSize = Size;
    return STATUS_SUCCESS;
}

static
VOID
ZpServer_Free(
    _Inout_ PZP_SERVER_OBJECT Object)
{
    ULONG Index;

    for (Index = 0; Index < Object->Config.DeploymentCount; Index++)
    {
        if (Object->Config.Deployments[Index].Certificate != NULL)
        {
            CertFreeCertificateContext(Object->Config.Deployments[Index].Certificate);
        }
    }
    Mem_Free(Object);
}

NTSTATUS
NTAPI
ZpServer_Create(
    _In_ PCZP_SERVER_CONFIG Config,
    _Out_ ZP_SERVER_HANDLE* Server)
{
    NTSTATUS Status;
    SIZE_T AllocationSize;
    PBYTE Cursor;
    PZP_SERVER_OBJECT Object;
    PZP_LISTENER_ENDPOINT Listeners;
    PZP_SERVER_DEPLOYMENT Deployments;
    PZP_MODULE_RECORD Modules;
    ULONG Index;

    Status = ZpServer_ValidateConfig(Config, &AllocationSize);
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
    Object->State = ZpServerStateStopped;
    Object->Config = *Config;

    Cursor = Add2Ptr(Object, sizeof(*Object));
    Listeners = (PZP_LISTENER_ENDPOINT)Cursor;
    Cursor += (SIZE_T)Config->ListenerCount * sizeof(*Listeners);
    Deployments = (PZP_SERVER_DEPLOYMENT)Cursor;
    Cursor += (SIZE_T)Config->DeploymentCount * sizeof(*Deployments);
    Modules = (PZP_MODULE_RECORD)Cursor;
    Cursor += (SIZE_T)Config->ModuleCount * sizeof(*Modules);
    Object->Config.Listeners = Config->ListenerCount != 0 ? Listeners : NULL;
    Object->Config.Deployments = Config->DeploymentCount != 0 ? Deployments : NULL;
    Object->Config.Modules = Config->ModuleCount != 0 ? Modules : NULL;
    if (Config->ModuleCount != 0)
    {
        RtlCopyMemory(Modules, Config->Modules, (SIZE_T)Config->ModuleCount * sizeof(*Modules));
    }

    for (Index = 0; Index < Config->ListenerCount; Index++)
    {
        Listeners[Index] = Config->Listeners[Index];
        ZpConfig_CopyString(&Cursor, Config->Listeners[Index].Host, &Listeners[Index].Host);
        ZpConfig_CopyString(&Cursor, Config->Listeners[Index].WssPath, &Listeners[Index].WssPath);
    }
    for (Index = 0; Index < Config->DeploymentCount; Index++)
    {
        ZpConfig_CopyString(&Cursor,
                            Config->Deployments[Index].ServerName,
                            &Deployments[Index].ServerName);
        Deployments[Index].Certificate = CertDuplicateCertificateContext(Config->Deployments[Index].Certificate);
        if (Deployments[Index].Certificate == NULL)
        {
            Object->Config.DeploymentCount = Index;
            ZpServer_Free(Object);
            return STATUS_UNSUCCESSFUL;
        }
    }
    *Server = (ZP_SERVER_HANDLE)Object;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ZpServer_Start(
    _In_ ZP_SERVER_HANDLE Server)
{
    NTSTATUS Status;
    PZP_SERVER_OBJECT Object = (PZP_SERVER_OBJECT)Server;
    PCZP_TRANSPORT_OPERATIONS Operations;
    PVOID TransportContext;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpServerStateStopped)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Object->Config.ListenerCount == 0 || Object->Config.DeploymentCount == 0)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_PARAMETER;
    }
    if (Object->TransportOperations == NULL)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_NOT_SUPPORTED;
    }
    Object->State = ZpServerStateStarting;
    Object->CallbackCount++;
    Operations = Object->TransportOperations;
    TransportContext = Object->TransportContext;
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Object->Config.StateCallback(Server,
                                 ZpServerStateStarting,
                                 STATUS_SUCCESS,
                                 Object->Config.CallbackContext);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    if (Object->State != ZpServerStateStarting)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_SUCCESS;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Status = Operations->Start(TransportContext);
    if (!NT_SUCCESS(Status))
    {
        ZpServer_NotifyState(Server, ZpServerStateStopped, Status);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpServer_Stop(
    _In_ ZP_SERVER_HANDLE Server)
{
    PZP_SERVER_OBJECT Object = (PZP_SERVER_OBJECT)Server;
    PCZP_TRANSPORT_OPERATIONS Operations;
    PVOID TransportContext;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State == ZpServerStateStopped || Object->State == ZpServerStateStopping)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_SUCCESS;
    }
    Object->State = ZpServerStateStopping;
    Object->CallbackCount++;
    Operations = Object->TransportOperations;
    TransportContext = Object->TransportContext;
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Object->Config.StateCallback(Server,
                                 ZpServerStateStopping,
                                 STATUS_SUCCESS,
                                 Object->Config.CallbackContext);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    Operations->Stop(TransportContext);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpServer_SetTransport(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ PCZP_TRANSPORT_OPERATIONS Operations,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_OBJECT Object = (PZP_SERVER_OBJECT)Server;

    if (Operations == NULL || Operations->Start == NULL || Operations->Stop == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpServerStateStopped)
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
ZpServer_IsTransitionValid(
    _In_ ZP_SERVER_STATE CurrentState,
    _In_ ZP_SERVER_STATE State)
{
    switch (CurrentState)
    {
        case ZpServerStateStarting:
            return State == ZpServerStateRunning || State == ZpServerStateStopped;

        case ZpServerStateRunning:
            return State == ZpServerStateStopped;

        case ZpServerStateStopping:
            return State == ZpServerStateStopped;
    }
    return FALSE;
}

NTSTATUS
ZpServer_NotifyState(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_SERVER_STATE State,
    _In_ NTSTATUS Status)
{
    PZP_SERVER_OBJECT Object = (PZP_SERVER_OBJECT)Server;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ZpServer_IsTransitionValid(Object->State, State))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Object->State = State;
    Object->CallbackCount++;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    Object->Config.StateCallback(Server, State, Status, Object->Config.CallbackContext);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ZpServer_Close(
    _In_ ZP_SERVER_HANDLE Server)
{
    PZP_SERVER_OBJECT Object = (PZP_SERVER_OBJECT)Server;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpServerStateStopped || Object->CallbackCount != 0)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_DEVICE_BUSY;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    ZpServer_Free(Object);
    return STATUS_SUCCESS;
}
