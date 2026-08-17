#include "../Client.inl"
#include "../../Modules/Administration/Client.h"
#include "../../Modules/EventLog/Client.h"
#include "../../Modules/Execution/Client.h"
#include "../../Modules/File/Client.h"
#include "../../Modules/Process/Client.h"
#include "../../Modules/Registry/Client.h"
#include "../../Modules/Service/Client.h"
#include "../../Modules/System/Client.h"
#include "../../Modules/Terminal/Client.h"
#include "../../Modules/Tunnel/Client.h"
#include "../../Modules/Window/Client.h"

typedef struct _ZP_CLIENT_INBOUND_REQUEST
{
    LIST_ENTRY ListEntry;
    PZP_CLIENT_OBJECT Owner;
    volatile LONG ReferenceCount;
    volatile LONG Pending;
    ULONGLONG RequestId;
    USHORT ModuleId;
    USHORT OperationId;
    ULONG TimeoutMilliseconds;
    ULONGLONG ReceivedTickCount;
    ULONG PayloadLength;
    BYTE Payload[ANYSIZE_ARRAY];
} ZP_CLIENT_INBOUND_REQUEST, *PZP_CLIENT_INBOUND_REQUEST;

static
VOID
ZpClientInbound_ReleaseRequest(
    _Inout_ PZP_CLIENT_INBOUND_REQUEST Request)
{
    if (InterlockedDecrement(&Request->ReferenceCount) == 0)
    {
        if ((Request->ModuleId == ZP_SERVICE_MODULE_ID &&
             Request->OperationId == ZP_SERVICE_OPERATION_CONFIGURE_ACCOUNT) ||
            (Request->ModuleId == ZP_ADMINISTRATION_MODULE_ID &&
             Request->OperationId == ZP_ADMINISTRATION_OPERATION_CONTROL_USER) ||
            (Request->ModuleId == ZP_EXECUTION_MODULE_ID &&
             Request->OperationId == ZP_EXECUTION_OPERATION_START))
        {
            RtlSecureZeroMemory(Request->Payload, Request->PayloadLength);
        }
        Mem_Free(Request);
    }
}

static
NTSTATUS
ZpClientInbound_SendResponse(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ULONGLONG RequestId,
    _In_ ZP_STATUS ResponseStatus,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength)
{
    ZP_RESPONSE Response = {
        RequestId,
        ResponseStatus,
        Payload,
        PayloadLength
    };
    PCZP_TRANSPORT_OPERATIONS Operations;
    PVOID TransportContext;
    PBYTE Body;
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeResponse(&Response, NULL, 0, &BodyLength);
    Body = NT_SUCCESS(Status) ? Mem_Alloc(BodyLength) : NULL;
    if (!NT_SUCCESS(Status) || Body == NULL)
    {
        return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    }
    Status = ZpMessage_EncodeResponse(&Response,
                                      Body,
                                      BodyLength,
                                      &BodyLength);
    if (NT_SUCCESS(Status))
    {
        RtlAcquireSRWLockShared(&Object->Lock);
        if (Object->State == ZpClientStateReady)
        {
            Operations = Object->TransportOperations[Object->ActiveTransport];
            TransportContext = Object->TransportContexts[Object->ActiveTransport];
            Status = Operations->Send(TransportContext,
                                      ZpMessageResponse,
                                      Body,
                                      BodyLength);
        }
        else
        {
            Status = STATUS_INVALID_DEVICE_STATE;
        }
        RtlReleaseSRWLockShared(&Object->Lock);
    }
    Mem_Free(Body);
    return Status;
}

static
LOGICAL
ZpClientInbound_HasModule(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ USHORT ModuleId)
{
    USHORT Index;

    for (Index = 0; Index < Object->QuicTransport.ModuleCount; Index++)
    {
        if (Object->QuicTransport.Modules[Index].ModuleId == ModuleId)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static
VOID
CALLBACK
ZpClientInbound_RequestCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_INBOUND_REQUEST Request = Context;
    PZP_CLIENT_OBJECT Object = Request->Owner;
    BYTE Payload[30 + (MAX_COMPUTERNAME_LENGTH + 1) * sizeof(WCHAR)];
    const VOID* Response = Payload;
    PBYTE AllocatedResponse = NULL;
    PZP_CLIENT_FILE_CHANNEL FileChannel = NULL;
    PZP_CLIENT_TERMINAL_CHANNEL TerminalChannel = NULL;
    PZP_CLIENT_TUNNEL_CHANNEL TunnelChannel = NULL;
    ULONG PayloadLength = 0;
    NTSTATUS ModuleStatus, SendStatus = STATUS_CANCELLED;
    ZP_STATUS Status;
    LOGICAL Respond;

    UNREFERENCED_PARAMETER(Instance);
    if (Request->TimeoutMilliseconds != 0 &&
        GetTickCount64() - Request->ReceivedTickCount >= Request->TimeoutMilliseconds)
    {
        Status = ZpStatus_FromNtStatus(STATUS_IO_TIMEOUT);
    }
    else if (Request->ModuleId == ZP_SYSTEM_MODULE_ID &&
             Request->OperationId == ZP_SYSTEM_OPERATION_INFO)
    {
        Status = Request->PayloadLength == 0 ?
                     ZpSystem_ExecuteInfo(Payload,
                                          sizeof(Payload),
                                          &PayloadLength) :
                     ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    else if (Request->ModuleId == ZP_FILE_MODULE_ID)
    {
        ModuleStatus = ZpFile_Execute(Object,
                                      Request->OperationId,
                                      Request->Payload,
                                      Request->PayloadLength,
                                      &Request->Pending,
                                      &AllocatedResponse,
                                      &PayloadLength,
                                      &FileChannel);
        Status = ZpStatus_FromNtStatus(ModuleStatus);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_PROCESS_MODULE_ID)
    {
        Status = ZpProcess_Execute(Request->OperationId,
                                   Request->Payload,
                                   Request->PayloadLength,
                                   &AllocatedResponse,
                                   &PayloadLength);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_SERVICE_MODULE_ID)
    {
        Status = ZpService_Execute(Request->OperationId,
                                   Request->Payload,
                                   Request->PayloadLength,
                                   &AllocatedResponse,
                                   &PayloadLength);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_REGISTRY_MODULE_ID)
    {
        ModuleStatus = ZpRegistry_Execute(Request->OperationId,
                                           Request->Payload,
                                           Request->PayloadLength,
                                           &AllocatedResponse,
                                           &PayloadLength);
        Status = ZpStatus_FromNtStatus(ModuleStatus);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_EVENT_LOG_MODULE_ID)
    {
        Status = ZpEventLog_Execute(Request->OperationId,
                                    Request->Payload,
                                    Request->PayloadLength,
                                    &Request->Pending,
                                    &AllocatedResponse,
                                    &PayloadLength);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_EXECUTION_MODULE_ID)
    {
        Status = ZpExecution_Execute(Object,
                                     Request->OperationId,
                                     Request->Payload,
                                     Request->PayloadLength,
                                     &AllocatedResponse,
                                     &PayloadLength);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_TERMINAL_MODULE_ID)
    {
        Status = ZpTerminal_Execute(Object,
                                    Request->OperationId,
                                    Request->Payload,
                                    Request->PayloadLength,
                                    &AllocatedResponse,
                                    &PayloadLength,
                                    &TerminalChannel);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_WINDOW_MODULE_ID)
    {
        Status = ZpWindow_Execute(Request->OperationId,
                                  Request->Payload,
                                  Request->PayloadLength,
                                  &AllocatedResponse,
                                  &PayloadLength);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_TUNNEL_MODULE_ID)
    {
        Status = ZpTunnel_Execute(Object,
                                  Request->OperationId,
                                  Request->Payload,
                                  Request->PayloadLength,
                                  &AllocatedResponse,
                                  &PayloadLength,
                                  &TunnelChannel);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_ADMINISTRATION_MODULE_ID)
    {
        Status = ZpAdministration_Execute(Request->OperationId,
                                          Request->Payload,
                                          Request->PayloadLength,
                                          &AllocatedResponse,
                                          &PayloadLength);
        Response = AllocatedResponse;
    }
    else
    {
        Status = ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }

    if (Object->Config.OperationCallback != NULL)
    {
        Object->Config.OperationCallback((ZP_CLIENT_HANDLE)Object,
                                         Request->ModuleId,
                                         Request->OperationId,
                                         Status,
                                         Object->Config.CallbackContext);
    }

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Respond = InterlockedExchange(&Request->Pending, FALSE);
    if (Respond)
    {
        RemoveEntryList(&Request->ListEntry);
        Object->InboundRequestCount--;
        Object->InboundRequestPayloadBytes -= Request->PayloadLength;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Respond)
    {
        SendStatus = ZpClientInbound_SendResponse(
            Object,
            Request->RequestId,
            Status,
            ZpStatus_IsSuccess(Status) ? Response : NULL,
            ZpStatus_IsSuccess(Status) ? PayloadLength : 0);
        ZpClientInbound_ReleaseRequest(Request);
    }
    if (FileChannel != NULL)
    {
        ZpFile_CommitChannel(FileChannel,
                             Respond && NT_SUCCESS(SendStatus));
    }
    if (TerminalChannel != NULL)
    {
        ZpTerminal_CommitChannel(TerminalChannel,
                                 Respond && NT_SUCCESS(SendStatus));
    }
    if (TunnelChannel != NULL)
    {
        ZpTunnel_CommitChannel(TunnelChannel,
                               Respond && NT_SUCCESS(SendStatus));
    }
    if (AllocatedResponse != NULL)
    {
        Mem_Free(AllocatedResponse);
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    ZpClientInbound_ReleaseRequest(Request);
}

NTSTATUS
ZpClient_QueueRequest(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ PCZP_REQUEST_VIEW Request)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    PZP_CLIENT_INBOUND_REQUEST RequestObject;
    SIZE_T AllocationSize = FIELD_OFFSET(ZP_CLIENT_INBOUND_REQUEST, Payload) +
                            Request->Payload.Length;

    if (Request->RequestId == 0)
    {
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ZpClientInbound_HasModule(Object, Request->ModuleId))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    if (Request->RequestId <= Object->HighestInboundRequestId)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Object->HighestInboundRequestId = Request->RequestId;
    if (Object->InboundRequestCount == Object->Config.MaxRequestsPerConnection ||
        Request->Payload.Length >
            Object->Config.MaxRequestPayloadBytesPerConnection -
            Object->InboundRequestPayloadBytes)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return ZpClientInbound_SendResponse(Object,
                                            Request->RequestId,
                                            ZpStatus_FromNtStatus(
                                                STATUS_QUOTA_EXCEEDED),
                                            NULL,
                                            0);
    }
    RequestObject = Mem_Alloc(AllocationSize);
    if (RequestObject == NULL)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return ZpClientInbound_SendResponse(Object,
                                            Request->RequestId,
                                            ZpStatus_FromNtStatus(
                                                STATUS_NO_MEMORY),
                                            NULL,
                                            0);
    }
    RtlZeroMemory(RequestObject, FIELD_OFFSET(ZP_CLIENT_INBOUND_REQUEST, Payload));
    RequestObject->Owner = Object;
    RequestObject->ReferenceCount = 2;
    RequestObject->Pending = TRUE;
    RequestObject->RequestId = Request->RequestId;
    RequestObject->ModuleId = Request->ModuleId;
    RequestObject->OperationId = Request->OperationId;
    RequestObject->TimeoutMilliseconds = Request->TimeoutMilliseconds;
    RequestObject->ReceivedTickCount = GetTickCount64();
    RequestObject->PayloadLength = Request->Payload.Length;
    RtlCopyMemory(RequestObject->Payload,
                  Request->Payload.Buffer,
                  Request->Payload.Length);
    InsertTailList(&Object->InboundRequests, &RequestObject->ListEntry);
    Object->InboundRequestCount++;
    Object->InboundRequestPayloadBytes += Request->Payload.Length;
    Object->CallbackCount++;
    RtlReleaseSRWLockExclusive(&Object->Lock);

    if (!TrySubmitThreadpoolCallback(ZpClientInbound_RequestCallback,
                                     RequestObject,
                                     NULL))
    {
        ULONG Error = GetLastError();
        RtlAcquireSRWLockExclusive(&Object->Lock);
        InterlockedExchange(&RequestObject->Pending, FALSE);
        RemoveEntryList(&RequestObject->ListEntry);
        Object->InboundRequestCount--;
        Object->InboundRequestPayloadBytes -= RequestObject->PayloadLength;
        Object->CallbackCount--;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Mem_Free(RequestObject);
        return ZpClientInbound_SendResponse(Object,
                                            Request->RequestId,
                                            ZpStatus_FromCode(ZpStatusWin32,
                                                              Error),
                                            NULL,
                                            0);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpClient_CancelInboundRequest(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONGLONG RequestId)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    PZP_CLIENT_INBOUND_REQUEST Request = NULL;
    PLIST_ENTRY Entry;

    if (RequestId == 0)
    {
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    for (Entry = Object->InboundRequests.Flink;
         Entry != &Object->InboundRequests;
         Entry = Entry->Flink)
    {
        Request = CONTAINING_RECORD(Entry,
                                    ZP_CLIENT_INBOUND_REQUEST,
                                    ListEntry);
        if (Request->RequestId == RequestId)
        {
            break;
        }
        Request = NULL;
    }
    if (Request == NULL)
    {
        NTSTATUS Status = RequestId <= Object->HighestInboundRequestId ?
                              STATUS_SUCCESS : STATUS_PROTOCOL_UNREACHABLE;

        RtlReleaseSRWLockExclusive(&Object->Lock);
        return Status;
    }
    InterlockedExchange(&Request->Pending, FALSE);
    RemoveEntryList(&Request->ListEntry);
    Object->InboundRequestCount--;
    Object->InboundRequestPayloadBytes -= Request->PayloadLength;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    ZpClientInbound_ReleaseRequest(Request);
    return STATUS_SUCCESS;
}

VOID
ZpClient_CloseInboundRequests(
    _In_ ZP_CLIENT_HANDLE Client)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    PZP_CLIENT_INBOUND_REQUEST Request;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    while (!IsListEmpty(&Object->InboundRequests))
    {
        Request = CONTAINING_RECORD(Object->InboundRequests.Flink,
                                    ZP_CLIENT_INBOUND_REQUEST,
                                    ListEntry);
        InterlockedExchange(&Request->Pending, FALSE);
        RemoveEntryList(&Request->ListEntry);
        Object->InboundRequestCount--;
        Object->InboundRequestPayloadBytes -= Request->PayloadLength;
        ZpClientInbound_ReleaseRequest(Request);
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
}
