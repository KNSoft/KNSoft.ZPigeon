#include "../Client.inl"
#include "../../Network/Config.inl"
#include "Retry.inl"

#include <Bcrypt.h>

static
VOID
CALLBACK
ZpClient_RequestTimerCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context,
    _Inout_ PTP_TIMER Timer);

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
    InitializeListHead(&Object->Requests);
    Object->NextRequestId = 1;
    Object->RequestTimer = CreateThreadpoolTimer(ZpClient_RequestTimerCallback,
                                                  Object,
                                                  NULL);
    if (Object->RequestTimer == NULL)
    {
        Mem_Free(Object);
        return STATUS_NO_MEMORY;
    }
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
    _In_ NTSTATUS Status,
    _In_ LOGICAL RestartRound)
{
    LARGE_INTEGER DueTime;
    ULONG Delay, EndpointIndex, RandomValue;

    if (RestartRound)
    {
        Object->NextEndpointIndex = 0;
    }
    if (ZpClient_FindNextEndpoint(Object,
                                  Object->NextEndpointIndex,
                                  &EndpointIndex))
    {
        Object->NextEndpointIndex = EndpointIndex;
        Delay = 1;
    }
    else
    {
        Object->NextEndpointIndex = 0;
        if (!NT_SUCCESS(BCryptGenRandom(NULL,
                                        (PUCHAR)&RandomValue,
                                        sizeof(RandomValue),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        {
            RandomValue = ZpClientRetry_GetBaseDelay(Object->FailureRound) *
                          ZP_CLIENT_DEFAULT_RETRY_JITTER_PERCENT / 100;
        }
        Delay = ZpClientRetry_GetDelay(Object->FailureRound, RandomValue);
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
NTSTATUS
ZpClient_StartEndpoints(
    _Inout_ PZP_CLIENT_OBJECT Object)
{
    PCZP_TRANSPORT_OPERATIONS Operations;
    PVOID TransportContext;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;
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
        if (NT_SUCCESS(Status))
        {
            return STATUS_SUCCESS;
        }
    }
    return Status;
}

static
VOID
ZpClient_CompleteStart(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ NTSTATUS Status)
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
        if (NT_SUCCESS(Status))
        {
            Operations->Stop(TransportContext);
        }
        else
        {
            ZpClient_NotifyState((ZP_CLIENT_HANDLE)Object,
                                 ZpClientStateStopped,
                                 STATUS_SUCCESS);
        }
    }
    else if (!NT_SUCCESS(Status) && State == ZpClientStateConnecting)
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
    LOGICAL Start;

    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Timer);
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
                                  STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        RtlAcquireSRWLockShared(&Object->Lock);
        Start = Object->State == ZpClientStateConnecting;
        RtlReleaseSRWLockShared(&Object->Lock);
        if (Start)
        {
            Status = ZpClient_StartEndpoints(Object);
        }
    }
    ZpClient_CompleteStart(Object, Status);
}

NTSTATUS
NTAPI
ZpClient_Start(
    _In_ ZP_CLIENT_HANDLE Client)
{
    NTSTATUS Status;
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    ULONG EndpointIndex;

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
                                 STATUS_SUCCESS,
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
                                 STATUS_SUCCESS,
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

static
VOID
ZpClient_ReleaseRequest(
    _Inout_ PZP_REQUEST_OBJECT Request)
{
    if (InterlockedDecrement(&Request->ReferenceCount) == 0)
    {
        Mem_Free(Request);
    }
}

static
VOID
ZpClient_InvokeRequestCallback(
    _Inout_ PZP_REQUEST_OBJECT Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_BUFFER_VIEW Payload)
{
    static const ZP_BUFFER_VIEW EmptyPayload = { NULL, 0 };
    PZP_CLIENT_OBJECT Object = Request->Owner;

    Request->Callback((ZP_REQUEST_HANDLE)Request,
                      Status,
                      Payload != NULL ? Payload : &EmptyPayload,
                      Request->Context);
    Request->Owner = NULL;
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    ZpClient_ReleaseRequest(Request);
}

static
VOID
ZpClient_ArmRequestTimer(
    _Inout_ PZP_CLIENT_OBJECT Object)
{
    PZP_REQUEST_OBJECT Request;
    LARGE_INTEGER DueTime;
    ULONGLONG Deadline = 0;
    ULONGLONG Now;
    PLIST_ENTRY Entry;
    ULONG Delay;

    for (Entry = Object->Requests.Flink;
         Entry != &Object->Requests;
         Entry = Entry->Flink)
    {
        Request = CONTAINING_RECORD(Entry, ZP_REQUEST_OBJECT, ListEntry);
        if (Request->DeadlineTickCount != 0 &&
            (Deadline == 0 || Request->DeadlineTickCount < Deadline))
        {
            Deadline = Request->DeadlineTickCount;
        }
    }
    if (Deadline == 0)
    {
        SetThreadpoolTimer(Object->RequestTimer, NULL, 0, 0);
        return;
    }
    Now = GetTickCount64();
    Delay = Deadline > Now ? (ULONG)min(Deadline - Now, MAXULONG) : 1;
    DueTime.QuadPart = -(LONGLONG)Delay * 10000;
    SetThreadpoolTimer(Object->RequestTimer,
                       (PFILETIME)&DueTime,
                       0,
                       0);
}

static
VOID
ZpClient_SendCancel(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ULONGLONG RequestId)
{
    PCZP_TRANSPORT_OPERATIONS Operations;
    PVOID TransportContext;
    BYTE Body[sizeof(RequestId)];
    ULONG BodyLength;

    RtlAcquireSRWLockShared(&Object->Lock);
    if (Object->State != ZpClientStateReady)
    {
        RtlReleaseSRWLockShared(&Object->Lock);
        return;
    }
    Operations = Object->TransportOperations[Object->ActiveTransport];
    TransportContext = Object->TransportContexts[Object->ActiveTransport];
    if (Operations->Send != NULL &&
        NT_SUCCESS(ZpMessage_EncodeCancel(RequestId,
                                          Body,
                                          sizeof(Body),
                                          &BodyLength)))
    {
        Operations->Send(TransportContext,
                         ZpMessageCancel,
                         Body,
                         BodyLength);
    }
    RtlReleaseSRWLockShared(&Object->Lock);
}

static
VOID
CALLBACK
ZpClient_RequestTimerCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context,
    _Inout_ PTP_TIMER Timer)
{
    PZP_CLIENT_OBJECT Object = Context;
    PZP_REQUEST_OBJECT Request;
    PLIST_ENTRY Entry;
    ULONGLONG Now;

    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Timer);
    for (;;)
    {
        Request = NULL;
        Now = GetTickCount64();
        RtlAcquireSRWLockExclusive(&Object->Lock);
        for (Entry = Object->Requests.Flink;
             Entry != &Object->Requests;
             Entry = Entry->Flink)
        {
            Request = CONTAINING_RECORD(Entry,
                                        ZP_REQUEST_OBJECT,
                                        ListEntry);
            if (Request->DeadlineTickCount != 0 &&
                Request->DeadlineTickCount <= Now)
            {
                break;
            }
            Request = NULL;
        }
        if (Request == NULL)
        {
            ZpClient_ArmRequestTimer(Object);
            RtlReleaseSRWLockExclusive(&Object->Lock);
            return;
        }
        InterlockedExchange(&Request->Pending, FALSE);
        RemoveEntryList(&Request->ListEntry);
        Object->CallbackCount++;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        ZpClient_SendCancel(Object, Request->RequestId);
        ZpClient_InvokeRequestCallback(Request, STATUS_IO_TIMEOUT, NULL);
    }
}

NTSTATUS
NTAPI
ZpClient_SendRequest(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ USHORT ModuleId,
    _In_ USHORT OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    PZP_REQUEST_OBJECT RequestObject;
    PCZP_TRANSPORT_OPERATIONS Operations;
    ZP_REQUEST Message;
    PVOID TransportContext;
    PBYTE Body;
    ULONG BodyLength;
    NTSTATUS Status;

    if (ModuleId == 0 || OperationId == 0 || Callback == NULL || Request == NULL ||
        PayloadLength > ZP_FRAME_MAX_BODY_SIZE - 16 ||
        (PayloadLength != 0 && Payload == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequestObject = Mem_Alloc(sizeof(*RequestObject));
    if (RequestObject == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(RequestObject, sizeof(*RequestObject));
    RequestObject->Owner = Object;
    RequestObject->ReferenceCount = 2;
    RequestObject->Pending = TRUE;
    RequestObject->Callback = Callback;
    RequestObject->Context = Context;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpClientStateReady)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Mem_Free(RequestObject);
        return STATUS_INVALID_DEVICE_STATE;
    }
    RequestObject->RequestId = Object->NextRequestId++;
    if (Object->NextRequestId == 0)
    {
        Object->NextRequestId = 1;
    }
    InsertTailList(&Object->Requests, &RequestObject->ListEntry);
    Operations = Object->TransportOperations[Object->ActiveTransport];
    TransportContext = Object->TransportContexts[Object->ActiveTransport];
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Message.RequestId = RequestObject->RequestId;
    Message.ModuleId = ModuleId;
    Message.OperationId = OperationId;
    Message.TimeoutMilliseconds = TimeoutMilliseconds;
    Message.Payload = Payload;
    Message.PayloadLength = PayloadLength;
    Status = ZpMessage_EncodeRequest(&Message, NULL, 0, &BodyLength);
    Body = NT_SUCCESS(Status) ? Mem_Alloc(BodyLength) : NULL;
    if (NT_SUCCESS(Status) && Body == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpMessage_EncodeRequest(&Message,
                                         Body,
                                         BodyLength,
                                         &BodyLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = Operations->Send != NULL ?
                     Operations->Send(TransportContext,
                                      ZpMessageRequest,
                                      Body,
                                      BodyLength) :
                     STATUS_NOT_SUPPORTED;
    }
    Mem_Free(Body);
    if (!NT_SUCCESS(Status))
    {
        RtlAcquireSRWLockExclusive(&Object->Lock);
        if (InterlockedExchange(&RequestObject->Pending, FALSE))
        {
            RemoveEntryList(&RequestObject->ListEntry);
            ZpClient_ArmRequestTimer(Object);
        }
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Mem_Free(RequestObject);
        return Status;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (RequestObject->Pending)
    {
        RequestObject->DeadlineTickCount = TimeoutMilliseconds != 0 ?
                                               GetTickCount64() + TimeoutMilliseconds :
                                               0;
        ZpClient_ArmRequestTimer(Object);
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    *Request = (ZP_REQUEST_HANDLE)RequestObject;
    return STATUS_SUCCESS;
}

typedef struct _ZP_CLIENT_SYSTEM_INFO_CONTEXT
{
    ZP_SYSTEM_INFO_CALLBACK Callback;
    PVOID Context;
} ZP_CLIENT_SYSTEM_INFO_CONTEXT, *PZP_CLIENT_SYSTEM_INFO_CONTEXT;

static
VOID
NTAPI
ZpClient_SystemInfoComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_SYSTEM_INFO_CONTEXT SystemContext = Context;
    ZP_SYSTEM_INFO_VIEW Info;

    if (NT_SUCCESS(Status))
    {
        Status = ZpSystem_DecodeInfo(Payload->Buffer,
                                     Payload->Length,
                                     &Info);
    }
    SystemContext->Callback(Request,
                            Status,
                            NT_SUCCESS(Status) ? &Info : NULL,
                            SystemContext->Context);
    Mem_Free(SystemContext);
}

NTSTATUS
NTAPI
ZpClient_GetSystemInfo(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_SYSTEM_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_CLIENT_SYSTEM_INFO_CONTEXT SystemContext;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    SystemContext = Mem_Alloc(sizeof(*SystemContext));
    if (SystemContext == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    SystemContext->Callback = Callback;
    SystemContext->Context = Context;
    Status = ZpClient_SendRequest(Client,
                                  ZP_SYSTEM_MODULE_ID,
                                  ZP_SYSTEM_OPERATION_INFO,
                                  TimeoutMilliseconds,
                                  NULL,
                                  0,
                                  ZpClient_SystemInfoComplete,
                                  SystemContext,
                                  Request);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(SystemContext);
    }
    return Status;
}

typedef struct _ZP_CLIENT_PROCESS_ENUMERATE_CONTEXT
{
    ZP_PROCESS_ENUMERATE_CALLBACK Callback;
    PVOID Context;
} ZP_CLIENT_PROCESS_ENUMERATE_CONTEXT, *PZP_CLIENT_PROCESS_ENUMERATE_CONTEXT;

static
VOID
NTAPI
ZpClient_ProcessEnumerateComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_PROCESS_ENUMERATE_CONTEXT ProcessContext = Context;
    ZP_PROCESS_LIST_VIEW Processes;

    if (NT_SUCCESS(Status))
    {
        Status = ZpProcess_DecodeList(Payload->Buffer,
                                      Payload->Length,
                                      &Processes);
    }
    ProcessContext->Callback(Request,
                             Status,
                             NT_SUCCESS(Status) ? &Processes : NULL,
                             ProcessContext->Context);
    Mem_Free(ProcessContext);
}

NTSTATUS
NTAPI
ZpClient_EnumerateProcesses(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_PROCESS_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_CLIENT_PROCESS_ENUMERATE_CONTEXT ProcessContext;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ProcessContext = Mem_Alloc(sizeof(*ProcessContext));
    if (ProcessContext == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    ProcessContext->Callback = Callback;
    ProcessContext->Context = Context;
    Status = ZpClient_SendRequest(Client,
                                  ZP_PROCESS_MODULE_ID,
                                  ZP_PROCESS_OPERATION_ENUMERATE,
                                  TimeoutMilliseconds,
                                  NULL,
                                  0,
                                  ZpClient_ProcessEnumerateComplete,
                                  ProcessContext,
                                  Request);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(ProcessContext);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpRequest_Cancel(
    _In_ ZP_REQUEST_HANDLE Request)
{
    PZP_REQUEST_OBJECT RequestObject = (PZP_REQUEST_OBJECT)Request;
    PZP_CLIENT_OBJECT Object = RequestObject->Owner;

    if (Object == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!InterlockedExchange(&RequestObject->Pending, FALSE))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    RemoveEntryList(&RequestObject->ListEntry);
    ZpClient_ArmRequestTimer(Object);
    Object->CallbackCount++;
    RtlReleaseSRWLockExclusive(&Object->Lock);

    ZpClient_SendCancel(Object, RequestObject->RequestId);
    ZpClient_InvokeRequestCallback(RequestObject, STATUS_CANCELLED, NULL);
    return STATUS_SUCCESS;
}

VOID
NTAPI
ZpRequest_Close(
    _In_ ZP_REQUEST_HANDLE Request)
{
    ZpClient_ReleaseRequest((PZP_REQUEST_OBJECT)Request);
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
    if (State == ZpClientStateReady)
    {
        Object->ReadyTickCount = GetTickCount64();
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

NTSTATUS
ZpClient_CompleteResponse(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ PCZP_RESPONSE_VIEW Response)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    PZP_REQUEST_OBJECT Request = NULL;
    PLIST_ENTRY Entry;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    for (Entry = Object->Requests.Flink;
         Entry != &Object->Requests;
         Entry = Entry->Flink)
    {
        Request = CONTAINING_RECORD(Entry, ZP_REQUEST_OBJECT, ListEntry);
        if (Request->RequestId == Response->RequestId)
        {
            break;
        }
        Request = NULL;
    }
    if (Request == NULL || !InterlockedExchange(&Request->Pending, FALSE))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    RemoveEntryList(&Request->ListEntry);
    ZpClient_ArmRequestTimer(Object);
    Object->CallbackCount++;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    ZpClient_InvokeRequestCallback(Request,
                                   Response->Status,
                                   &Response->Payload);
    return STATUS_SUCCESS;
}

static
VOID
ZpClient_CompleteRequests(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ NTSTATUS Status)
{
    PZP_REQUEST_OBJECT Request;

    for (;;)
    {
        RtlAcquireSRWLockExclusive(&Object->Lock);
        if (IsListEmpty(&Object->Requests))
        {
            ZpClient_ArmRequestTimer(Object);
            RtlReleaseSRWLockExclusive(&Object->Lock);
            return;
        }
        Request = CONTAINING_RECORD(Object->Requests.Flink,
                                    ZP_REQUEST_OBJECT,
                                    ListEntry);
        InterlockedExchange(&Request->Pending, FALSE);
        RemoveEntryList(&Request->ListEntry);
        Object->CallbackCount++;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        ZpClient_InvokeRequestCallback(Request, Status, NULL);
    }
}

VOID
ZpClient_TransportShutdown(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ NTSTATUS Status)
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

    if (State == ZpClientStateStopping)
    {
        ZpClient_CompleteRequests(Object, Status);
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
    ZpClient_CompleteRequests(Object, Status);
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
        !IsListEmpty(&Object->Requests))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_DEVICE_BUSY;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    SetThreadpoolTimer(Object->RequestTimer, NULL, 0, 0);
    WaitForThreadpoolTimerCallbacks(Object->RequestTimer, TRUE);
    CloseThreadpoolTimer(Object->RequestTimer);
    Object->RequestTimer = NULL;
    if (Object->RetryTimer != NULL)
    {
        SetThreadpoolTimer(Object->RetryTimer, NULL, 0, 0);
        WaitForThreadpoolTimerCallbacks(Object->RetryTimer, TRUE);
        CloseThreadpoolTimer(Object->RetryTimer);
        Object->RetryTimer = NULL;
    }
    ZpClientQuic_Uninitialize(&Object->QuicTransport);
    Mem_Free(Object);
    return STATUS_SUCCESS;
}
