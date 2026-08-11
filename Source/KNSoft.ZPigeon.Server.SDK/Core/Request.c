#include "Connection.h"
#include "../../SDK/Request.h"
#include "Channel.h"

typedef struct _ZP_SERVER_REQUEST_OBJECT
{
    ZP_REQUEST_HEADER Header;
    LIST_ENTRY ListEntry;
    PZP_CONNECTION_OBJECT Owner;
    volatile LONG Pending;
    ULONGLONG RequestId;
    ULONGLONG DeadlineTickCount;
    ZP_REQUEST_COMPLETE_CALLBACK Callback;
    PVOID Context;
} ZP_SERVER_REQUEST_OBJECT, *PZP_SERVER_REQUEST_OBJECT;

static
VOID
ZpServerConnection_ArmRequestTimer(
    _Inout_ PZP_CONNECTION_OBJECT Connection)
{
    PZP_SERVER_REQUEST_OBJECT Request;
    LARGE_INTEGER DueTime;
    ULONGLONG Deadline = 0;
    ULONGLONG Now;
    PLIST_ENTRY Entry;
    ULONG Delay;

    if (Connection->RequestTimer == NULL)
    {
        return;
    }
    for (Entry = Connection->Requests.Flink;
         Entry != &Connection->Requests;
         Entry = Entry->Flink)
    {
        Request = CONTAINING_RECORD(Entry,
                                    ZP_SERVER_REQUEST_OBJECT,
                                    ListEntry);
        if (Request->DeadlineTickCount != 0 &&
            (Deadline == 0 || Request->DeadlineTickCount < Deadline))
        {
            Deadline = Request->DeadlineTickCount;
        }
    }
    if (Deadline == 0)
    {
        SetThreadpoolTimer(Connection->RequestTimer, NULL, 0, 0);
        return;
    }
    Now = GetTickCount64();
    Delay = Deadline > Now ? (ULONG)min(Deadline - Now, MAXULONG) : 1;
    DueTime.QuadPart = -(LONGLONG)Delay * 10000;
    SetThreadpoolTimer(Connection->RequestTimer,
                       (PFILETIME)&DueTime,
                       0,
                       0);
}

static
VOID
ZpServerConnection_SendCancel(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ULONGLONG RequestId)
{
    BYTE Body[sizeof(RequestId)];
    ULONG BodyLength;

    if (!NT_SUCCESS(ZpMessage_EncodeCancel(RequestId,
                                            Body,
                                            sizeof(Body),
                                            &BodyLength)))
    {
        return;
    }
    RtlAcquireSRWLockShared(&Connection->Lock);
    if (Connection->Phase == ZpConnectionPhaseReady)
    {
        Connection->Send(Connection,
                         ZpMessageCancel,
                         Body,
                         BodyLength);
    }
    RtlReleaseSRWLockShared(&Connection->Lock);
}

static
VOID
ZpServerConnection_InvokeRequest(
    _Inout_ PZP_SERVER_REQUEST_OBJECT Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BUFFER_VIEW Payload)
{
    static const ZP_BUFFER_VIEW EmptyPayload = { NULL, 0 };
    PZP_CONNECTION_OBJECT Connection = Request->Owner;

    Request->Callback((ZP_REQUEST_HANDLE)Request,
                      Status,
                      Payload != NULL ? Payload : &EmptyPayload,
                      Request->Context);
    Request->Owner = NULL;
    ZpRequest_Release(&Request->Header);
    ZpConnection_Release((ZP_CONNECTION_HANDLE)Connection);
}

static
VOID
CALLBACK
ZpServerConnection_RequestTimerCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context,
    _Inout_ PTP_TIMER Timer)
{
    PZP_CONNECTION_OBJECT Connection = Context;
    PZP_SERVER_REQUEST_OBJECT Request;
    PLIST_ENTRY Entry;
    ULONGLONG Now;

    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Timer);
    for (;;)
    {
        Request = NULL;
        Now = GetTickCount64();
        RtlAcquireSRWLockExclusive(&Connection->Lock);
        for (Entry = Connection->Requests.Flink;
             Entry != &Connection->Requests;
             Entry = Entry->Flink)
        {
            Request = CONTAINING_RECORD(Entry,
                                        ZP_SERVER_REQUEST_OBJECT,
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
            ZpServerConnection_ArmRequestTimer(Connection);
            RtlReleaseSRWLockExclusive(&Connection->Lock);
            return;
        }
        InterlockedExchange(&Request->Pending, FALSE);
        RemoveEntryList(&Request->ListEntry);
        Connection->RequestCount--;
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        ZpServerConnection_SendCancel(Connection, Request->RequestId);
        ZpServerConnection_InvokeRequest(
            Request,
            ZpStatus_FromNtStatus(STATUS_IO_TIMEOUT),
            NULL);
    }
}

static
NTSTATUS
NTAPI
ZpServerConnection_CancelRequest(
    _In_ ZP_REQUEST_HANDLE Request)
{
    PZP_SERVER_REQUEST_OBJECT RequestObject = (PZP_SERVER_REQUEST_OBJECT)Request;
    PZP_CONNECTION_OBJECT Connection = RequestObject->Owner;

    if (Connection == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    RtlAcquireSRWLockExclusive(&Connection->Lock);
    if (!InterlockedExchange(&RequestObject->Pending, FALSE))
    {
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    RemoveEntryList(&RequestObject->ListEntry);
    Connection->RequestCount--;
    ZpServerConnection_ArmRequestTimer(Connection);
    RtlReleaseSRWLockExclusive(&Connection->Lock);
    ZpServerConnection_SendCancel(Connection, RequestObject->RequestId);
    ZpServerConnection_InvokeRequest(
        RequestObject,
        ZpStatus_FromNtStatus(STATUS_CANCELLED),
        NULL);
    return STATUS_SUCCESS;
}

static
LOGICAL
ZpServerConnection_HasModule(
    _In_ PZP_CONNECTION_OBJECT Connection,
    _In_ USHORT ModuleId)
{
    USHORT Index;

    for (Index = 0; Index < Connection->ModuleCount; Index++)
    {
        if (Connection->Modules[Index].ModuleId == ModuleId)
        {
            return TRUE;
        }
    }
    return FALSE;
}

NTSTATUS
NTAPI
ZpServer_SendRequest(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ USHORT ModuleId,
    _In_ USHORT OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_CONNECTION_OBJECT ConnectionObject = Connection;
    PZP_SERVER_REQUEST_OBJECT RequestObject;
    ZP_REQUEST Message = { 1, ModuleId, OperationId, TimeoutMilliseconds, Payload, PayloadLength };
    PBYTE Body;
    ULONG BodyLength;
    NTSTATUS Status;
    LOGICAL Pending;

    if (ModuleId == 0 || OperationId == 0 || Callback == NULL || Request == NULL ||
        PayloadLength > ZP_FRAME_MAX_BODY_SIZE - 16 ||
        (PayloadLength != 0 && Payload == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpMessage_EncodeRequest(&Message, NULL, 0, &BodyLength);
    Body = NT_SUCCESS(Status) ? Mem_Alloc(BodyLength) : NULL;
    if (!NT_SUCCESS(Status) || Body == NULL)
    {
        return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    }
    RequestObject = Mem_Alloc(sizeof(*RequestObject));
    if (RequestObject == NULL)
    {
        Mem_Free(Body);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(RequestObject, sizeof(*RequestObject));
    RequestObject->Header.Cancel = ZpServerConnection_CancelRequest;
    RequestObject->Header.ReferenceCount = 3;
    RequestObject->Owner = ConnectionObject;
    RequestObject->Pending = TRUE;
    RequestObject->Callback = Callback;
    RequestObject->Context = Context;

    RtlAcquireSRWLockExclusive(&ConnectionObject->Lock);
    if (ConnectionObject->Phase != ZpConnectionPhaseReady ||
        !ZpServerConnection_HasModule(ConnectionObject, ModuleId))
    {
        Status = ConnectionObject->Phase == ZpConnectionPhaseReady ?
                     STATUS_NOT_SUPPORTED :
                     STATUS_INVALID_DEVICE_STATE;
        RtlReleaseSRWLockExclusive(&ConnectionObject->Lock);
        Mem_Free(RequestObject);
        Mem_Free(Body);
        return Status;
    }
    if (ConnectionObject->RequestCount == ConnectionObject->MaxRequests)
    {
        RtlReleaseSRWLockExclusive(&ConnectionObject->Lock);
        Mem_Free(RequestObject);
        Mem_Free(Body);
        return STATUS_QUOTA_EXCEEDED;
    }
    if (ConnectionObject->NextRequestId == 0)
    {
        RtlReleaseSRWLockExclusive(&ConnectionObject->Lock);
        Mem_Free(RequestObject);
        Mem_Free(Body);
        return STATUS_INTEGER_OVERFLOW;
    }
    if (TimeoutMilliseconds != 0 && ConnectionObject->RequestTimer == NULL)
    {
        ConnectionObject->RequestTimer = CreateThreadpoolTimer(
            ZpServerConnection_RequestTimerCallback,
            ConnectionObject,
            NULL);
        if (ConnectionObject->RequestTimer == NULL)
        {
            RtlReleaseSRWLockExclusive(&ConnectionObject->Lock);
            Mem_Free(RequestObject);
            Mem_Free(Body);
            return STATUS_NO_MEMORY;
        }
    }
    RequestObject->RequestId = ConnectionObject->NextRequestId;
    ConnectionObject->NextRequestId++;
    RequestObject->DeadlineTickCount = TimeoutMilliseconds != 0 ?
                                           GetTickCount64() + TimeoutMilliseconds :
                                           0;
    InsertTailList(&ConnectionObject->Requests, &RequestObject->ListEntry);
    ConnectionObject->RequestCount++;
    ZpConnection_AddRef(Connection);
    ZpServerConnection_ArmRequestTimer(ConnectionObject);
    RtlReleaseSRWLockExclusive(&ConnectionObject->Lock);

    Message.RequestId = RequestObject->RequestId;
    Status = ZpMessage_EncodeRequest(&Message, Body, BodyLength, &BodyLength);
    if (NT_SUCCESS(Status))
    {
        Status = ConnectionObject->Send(ConnectionObject,
                                        ZpMessageRequest,
                                        Body,
                                        BodyLength);
    }
    Mem_Free(Body);
    if (!NT_SUCCESS(Status))
    {
        RtlAcquireSRWLockExclusive(&ConnectionObject->Lock);
        Pending = InterlockedExchange(&RequestObject->Pending, FALSE);
        if (Pending)
        {
            RemoveEntryList(&RequestObject->ListEntry);
            ConnectionObject->RequestCount--;
            ZpServerConnection_ArmRequestTimer(ConnectionObject);
        }
        RtlReleaseSRWLockExclusive(&ConnectionObject->Lock);
        if (Pending)
        {
            ZpConnection_Release(Connection);
            Mem_Free(RequestObject);
            return Status;
        }
    }
    *Request = (ZP_REQUEST_HANDLE)RequestObject;
    ZpRequest_Release(&RequestObject->Header);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpServerConnection_ReceiveResponse(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ PCZP_RESPONSE_VIEW Response)
{
    PZP_SERVER_REQUEST_OBJECT Request = NULL;
    PLIST_ENTRY Entry;

    RtlAcquireSRWLockExclusive(&Connection->Lock);
    for (Entry = Connection->Requests.Flink;
         Entry != &Connection->Requests;
         Entry = Entry->Flink)
    {
        Request = CONTAINING_RECORD(Entry,
                                    ZP_SERVER_REQUEST_OBJECT,
                                    ListEntry);
        if (Request->RequestId == Response->RequestId)
        {
            break;
        }
        Request = NULL;
    }
    if (Request == NULL)
    {
        NTSTATUS Status = Response->RequestId != 0 &&
                          Response->RequestId < Connection->NextRequestId ?
                              STATUS_SUCCESS : STATUS_PROTOCOL_UNREACHABLE;

        RtlReleaseSRWLockExclusive(&Connection->Lock);
        return Status;
    }
    InterlockedExchange(&Request->Pending, FALSE);
    RemoveEntryList(&Request->ListEntry);
    Connection->RequestCount--;
    ZpServerConnection_ArmRequestTimer(Connection);
    RtlReleaseSRWLockExclusive(&Connection->Lock);
    ZpServerConnection_InvokeRequest(Request,
                                     Response->Status,
                                     &Response->Payload);
    return STATUS_SUCCESS;
}

VOID
ZpServerConnection_Close(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_STATUS Status)
{
    PZP_SERVER_REQUEST_OBJECT Request;
    PTP_TIMER Timer;
    ZP_STATUS CompletionStatus = ZpStatus_IsSuccess(Status) ?
                                     ZpStatus_FromNtStatus(
                                         STATUS_CONNECTION_DISCONNECTED) :
                                     Status;

    RtlAcquireSRWLockExclusive(&Connection->Lock);
    Connection->Phase = ZpConnectionPhaseClosed;
    Timer = Connection->RequestTimer;
    Connection->RequestTimer = NULL;
    if (Timer != NULL)
    {
        SetThreadpoolTimer(Timer, NULL, 0, 0);
    }
    RtlReleaseSRWLockExclusive(&Connection->Lock);
    if (Timer != NULL)
    {
        WaitForThreadpoolTimerCallbacks(Timer, TRUE);
        CloseThreadpoolTimer(Timer);
    }
    ZpServerConnection_CloseChannels(Connection, CompletionStatus);

    for (;;)
    {
        RtlAcquireSRWLockExclusive(&Connection->Lock);
        if (IsListEmpty(&Connection->Requests))
        {
            RtlReleaseSRWLockExclusive(&Connection->Lock);
            return;
        }
        Request = CONTAINING_RECORD(Connection->Requests.Flink,
                                    ZP_SERVER_REQUEST_OBJECT,
                                    ListEntry);
        InterlockedExchange(&Request->Pending, FALSE);
        RemoveEntryList(&Request->ListEntry);
        Connection->RequestCount--;
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        ZpServerConnection_InvokeRequest(Request, CompletionStatus, NULL);
    }
}
