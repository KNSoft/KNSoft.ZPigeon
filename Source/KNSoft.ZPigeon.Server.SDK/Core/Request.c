#include "Connection.h"
#include "../../SDK/Request.h"
#include "Channel.h"

typedef struct _ZP_SERVER_REQUEST_OBJECT
{
    ZP_REQUEST_HEADER Header;
    LIST_ENTRY ListEntry;
    LIST_ENTRY BucketEntry;
    LIST_ENTRY TimerEntry;
    PZP_CONNECTION_OBJECT Owner;
    volatile LONG Pending;
    ULONG RequestId;
    ULONGLONG StartTickCount;
    ULONGLONG DeadlineTickCount;
    ZP_REQUEST_COMPLETE_CALLBACK Callback;
    PVOID Context;
} ZP_SERVER_REQUEST_OBJECT, *PZP_SERVER_REQUEST_OBJECT;

static
PZP_SERVER_REQUEST_OBJECT
ZpServerConnection_FindRequestLocked(
    _In_ PZP_CONNECTION_OBJECT Connection,
    _In_ ULONG RequestId)
{
    PLIST_ENTRY Bucket = &Connection->RequestBuckets[RequestId & (ZP_CONNECTION_LOOKUP_BUCKET_COUNT - 1)];
    PLIST_ENTRY Entry;
    PZP_SERVER_REQUEST_OBJECT Request;

    for (Entry = Bucket->Flink; Entry != Bucket; Entry = Entry->Flink)
    {
        Request = CONTAINING_RECORD(Entry, ZP_SERVER_REQUEST_OBJECT, BucketEntry);
        if (Request->RequestId == RequestId) return Request;
    }
    return NULL;
}

static
VOID
ZpServerConnection_RemoveRequestLocked(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _Inout_ PZP_SERVER_REQUEST_OBJECT Request)
{
    RemoveEntryList(&Request->ListEntry);
    RemoveEntryList(&Request->BucketEntry);
    if (Request->DeadlineTickCount != 0) RemoveEntryList(&Request->TimerEntry);
    Connection->RequestCount--;
}

static
VOID
ZpServerConnection_InsertTimedRequestLocked(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _Inout_ PZP_SERVER_REQUEST_OBJECT Request)
{
    PLIST_ENTRY Entry;

    for (Entry = Connection->TimedRequests.Flink;
         Entry != &Connection->TimedRequests;
         Entry = Entry->Flink)
    {
        PZP_SERVER_REQUEST_OBJECT Current = CONTAINING_RECORD(
            Entry,
            ZP_SERVER_REQUEST_OBJECT,
            TimerEntry);

        if (Current->DeadlineTickCount > Request->DeadlineTickCount) break;
    }
    InsertTailList(Entry, &Request->TimerEntry);
}

static
VOID
ZpServerConnection_RecordRequest(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ PZP_SERVER_REQUEST_OBJECT Request,
    _In_ LOGICAL Completed)
{
    ULONGLONG Milliseconds;

    if (Completed)
    {
        Milliseconds = GetTickCount64() - Request->StartTickCount;
        Connection->CompletedRequests++;
        Connection->SmoothedRequestMilliseconds = Connection->SmoothedRequestMilliseconds == 0 ?
                                                       Milliseconds :
                                                       (Connection->SmoothedRequestMilliseconds * 7 +
                                                        Milliseconds * 3 + 5) / 10;
        Connection->ConsecutiveFailures = 0;
    }
    else
    {
        Connection->FailedRequests++;
        if (Connection->ConsecutiveFailures != MAXULONG) Connection->ConsecutiveFailures++;
    }
}

static
VOID
ZpServerConnection_ArmRequestTimer(
    _Inout_ PZP_CONNECTION_OBJECT Connection)
{
    PZP_SERVER_REQUEST_OBJECT Request;
    LARGE_INTEGER DueTime;
    ULONGLONG Now;
    ULONG Delay;

    if (Connection->RequestTimer == NULL)
    {
        return;
    }
    if (IsListEmpty(&Connection->TimedRequests))
    {
        SetThreadpoolTimer(Connection->RequestTimer, NULL, 0, 0);
        return;
    }
    Request = CONTAINING_RECORD(Connection->TimedRequests.Flink,
                                ZP_SERVER_REQUEST_OBJECT,
                                TimerEntry);
    Now = GetTickCount64();
    Delay = Request->DeadlineTickCount > Now ?
                (ULONG)min(Request->DeadlineTickCount - Now, MAXULONG) : 1;
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
    _In_ ULONG RequestId)
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
    ULONGLONG Now;

    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Timer);
    for (;;)
    {
        Request = NULL;
        Now = GetTickCount64();
        RtlAcquireSRWLockExclusive(&Connection->Lock);
        if (!IsListEmpty(&Connection->TimedRequests))
        {
            Request = CONTAINING_RECORD(Connection->TimedRequests.Flink,
                                        ZP_SERVER_REQUEST_OBJECT,
                                        TimerEntry);
            if (Request->DeadlineTickCount > Now) Request = NULL;
        }
        if (Request == NULL)
        {
            ZpServerConnection_ArmRequestTimer(Connection);
            RtlReleaseSRWLockExclusive(&Connection->Lock);
            return;
        }
        InterlockedExchange(&Request->Pending, FALSE);
        ZpServerConnection_RemoveRequestLocked(Connection, Request);
        ZpServerConnection_RecordRequest(Connection, Request, FALSE);
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
    ZpServerConnection_RemoveRequestLocked(Connection, RequestObject);
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
    _In_ BYTE ModuleId)
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
    _In_ BYTE ModuleId,
    _In_ BYTE OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    BYTE StackBody[256];
    PZP_CONNECTION_OBJECT ConnectionObject = Connection;
    PZP_SERVER_REQUEST_OBJECT RequestObject;
    ZP_REQUEST Message = { 1, ModuleId, OperationId, TimeoutMilliseconds, Payload, PayloadLength };
    PBYTE Body = StackBody;
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
    if (!NT_SUCCESS(Status)) return Status;
    if (BodyLength > sizeof(StackBody)) Body = Mem_Alloc(BodyLength);
    if (Body == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RequestObject = Mem_Alloc(sizeof(*RequestObject));
    if (RequestObject == NULL)
    {
        if (Body != StackBody) Mem_Free(Body);
        return STATUS_NO_MEMORY;
    }
    RequestObject->Header.Cancel = ZpServerConnection_CancelRequest;
    RequestObject->Header.ReferenceCount = 3;
    RequestObject->Owner = ConnectionObject;
    RequestObject->Pending = TRUE;
    RequestObject->Callback = Callback;
    RequestObject->Context = Context;

    RtlEnterCriticalSection(&ConnectionObject->RequestSendLock);
    RtlAcquireSRWLockExclusive(&ConnectionObject->Lock);
    if (ConnectionObject->Phase != ZpConnectionPhaseReady ||
        !ZpServerConnection_HasModule(ConnectionObject, ModuleId))
    {
        Status = ConnectionObject->Phase == ZpConnectionPhaseReady ?
                     STATUS_NOT_SUPPORTED :
                     STATUS_INVALID_DEVICE_STATE;
        RtlReleaseSRWLockExclusive(&ConnectionObject->Lock);
        RtlLeaveCriticalSection(&ConnectionObject->RequestSendLock);
        Mem_Free(RequestObject);
        if (Body != StackBody) Mem_Free(Body);
        return Status;
    }
    if (ConnectionObject->RequestCount == ConnectionObject->MaxRequests)
    {
        RtlReleaseSRWLockExclusive(&ConnectionObject->Lock);
        RtlLeaveCriticalSection(&ConnectionObject->RequestSendLock);
        Mem_Free(RequestObject);
        if (Body != StackBody) Mem_Free(Body);
        return STATUS_QUOTA_EXCEEDED;
    }
    if (ConnectionObject->NextRequestId == 0)
    {
        RtlReleaseSRWLockExclusive(&ConnectionObject->Lock);
        RtlLeaveCriticalSection(&ConnectionObject->RequestSendLock);
        Mem_Free(RequestObject);
        if (Body != StackBody) Mem_Free(Body);
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
            RtlLeaveCriticalSection(&ConnectionObject->RequestSendLock);
            Mem_Free(RequestObject);
            if (Body != StackBody) Mem_Free(Body);
            return STATUS_NO_MEMORY;
        }
    }
    RequestObject->RequestId = ConnectionObject->NextRequestId;
    ConnectionObject->NextRequestId++;
    RequestObject->StartTickCount = GetTickCount64();
    RequestObject->DeadlineTickCount = TimeoutMilliseconds != 0 ?
                                           RequestObject->StartTickCount + TimeoutMilliseconds :
                                           0;
    InsertTailList(&ConnectionObject->Requests, &RequestObject->ListEntry);
    InsertTailList(&ConnectionObject->RequestBuckets[
                       RequestObject->RequestId & (ZP_CONNECTION_LOOKUP_BUCKET_COUNT - 1)],
                   &RequestObject->BucketEntry);
    if (RequestObject->DeadlineTickCount != 0)
    {
        ZpServerConnection_InsertTimedRequestLocked(ConnectionObject, RequestObject);
    }
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
    RtlLeaveCriticalSection(&ConnectionObject->RequestSendLock);
    if (ModuleId == ZP_SERVICE_MODULE_ID &&
        OperationId == ZP_SERVICE_OPERATION_CONFIGURE_ACCOUNT)
    {
        RtlSecureZeroMemory(Body, BodyLength);
    }
    if (Body != StackBody) Mem_Free(Body);
    if (!NT_SUCCESS(Status))
    {
        RtlAcquireSRWLockExclusive(&ConnectionObject->Lock);
        Pending = InterlockedExchange(&RequestObject->Pending, FALSE);
        if (Pending)
        {
            ZpServerConnection_RemoveRequestLocked(ConnectionObject, RequestObject);
            ZpServerConnection_RecordRequest(ConnectionObject, RequestObject, FALSE);
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
    PZP_SERVER_REQUEST_OBJECT Request;

    RtlAcquireSRWLockExclusive(&Connection->Lock);
    Request = ZpServerConnection_FindRequestLocked(Connection, Response->RequestId);
    if (Request == NULL)
    {
        NTSTATUS Status = Response->RequestId != 0 &&
                          Response->RequestId < Connection->NextRequestId ?
                              STATUS_SUCCESS : STATUS_PROTOCOL_UNREACHABLE;

        RtlReleaseSRWLockExclusive(&Connection->Lock);
        return Status;
    }
    InterlockedExchange(&Request->Pending, FALSE);
    ZpServerConnection_RemoveRequestLocked(Connection, Request);
    ZpServerConnection_RecordRequest(Connection, Request, TRUE);
    ZpServerConnection_ArmRequestTimer(Connection);
    RtlReleaseSRWLockExclusive(&Connection->Lock);
    ZpServerConnection_InvokeRequest(Request,
                                     Response->Status,
                                     &Response->Payload);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ZpServer_QueryConnectionStatistics(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _Out_ PZP_SERVER_CONNECTION_STATISTICS Statistics)
{
    PZP_CONNECTION_OBJECT ConnectionObject = Connection;

    if (ConnectionObject == NULL || Statistics == NULL) return STATUS_INVALID_PARAMETER;
    RtlAcquireSRWLockShared(&ConnectionObject->Lock);
    Statistics->CompletedRequests = ConnectionObject->CompletedRequests;
    Statistics->FailedRequests = ConnectionObject->FailedRequests;
    Statistics->SmoothedRequestMilliseconds = ConnectionObject->SmoothedRequestMilliseconds;
    Statistics->PendingRequests = ConnectionObject->RequestCount;
    Statistics->ConsecutiveFailures = ConnectionObject->ConsecutiveFailures;
    RtlReleaseSRWLockShared(&ConnectionObject->Lock);
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
        ZpServerConnection_RemoveRequestLocked(Connection, Request);
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        ZpServerConnection_InvokeRequest(Request, CompletionStatus, NULL);
    }
}
