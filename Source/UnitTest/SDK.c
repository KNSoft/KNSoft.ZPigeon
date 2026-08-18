#include "UnitTest.h"

#include <KNSoft/ZPigeon/Client.h>
#include <KNSoft/ZPigeon/Server.h>
#include <KNSoft/ZPigeon/Terminal.h>

#include "../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../KNSoft.ZPigeon.Client.SDK/Transport/Retry.inl"
#include "../KNSoft.ZPigeon.Server.SDK/Server.inl"
#include "../KNSoft.ZPigeon.Server.SDK/Core/Connection.h"
#include "../Modules/EventLog/Client.h"
#include "../Modules/File/Client.h"
#include "../Modules/Process/Client.h"
#include "../Modules/Registry/Client.h"
#include "../Modules/Service/Client.h"
#include "../Modules/System/Client.h"
#include "../Network/Authentication.inl"
#include "../Network/Quic.inl"

#define SDK_STATUS_IS(Status, Value) \
    ((Status).Type == ((Value) == STATUS_SUCCESS ? ZpStatusNone : ZpStatusNtStatus) && \
     (Status).Code == (ULONG)(Value))

typedef struct _SDK_TEST_CONTEXT
{
    ZP_STATUS StartStatus;
    ULONG StartCount;
    ULONG StartEndpointIndices[8];
    ULONG StopCount;
    ULONG SendCount;
    ZP_MESSAGE_TYPE SendMessageType;
    ULONGLONG SendToken;
    ULONGLONG SendRequestId;
    USHORT SendModuleId;
    USHORT SendOperationId;
    ULONG SendPayloadLength;
    ULONGLONG SendChannelId;
    ULONG SendChannelCredit;
    ULONG SendChannelDataLength;
    ZP_STATUS SendChannelStatus;
    ZP_STATUS RequestStatus;
    ULONG FileOpenReadCount;
    ZP_STATUS FileOpenReadStatus;
    ULONG FileHashCount;
    ZP_STATUS FileHashStatus;
    ZP_FILE_HASH_ALGORITHM FileHashAlgorithm;
    ULONGLONG FileHashSize;
    BYTE FileDigest[ZP_FILE_SHA256_SIZE];
    ULONG FilePageCount;
    ZP_STATUS FilePageStatus;
    ULONG FilePageFileCount;
    ULONGLONG FilePageEnumerationId;
    ULONG RegistryPageCount;
    ZP_STATUS RegistryPageStatus;
    ULONG RegistryRecordCount;
    ULONG RegistryValueCount;
    ZP_STATUS RegistryValueStatus;
    ULONG RegistryValueType;
    ULONG RegistryValueDataLength;
    ULONG EventPageCount;
    ZP_STATUS EventPageStatus;
    ULONG EventPageRecordCount;
    BOOLEAN EventPageHasMore;
    ZP_CHANNEL_HANDLE FileChannel;
    ULONGLONG FileSize;
    ULONGLONG FileOffset;
    ULONG FileOpenWriteCount;
    ZP_STATUS FileOpenWriteStatus;
    ZP_CHANNEL_HANDLE FileWriteChannel;
    ULONGLONG FileWriteSize;
    ULONG TerminalCreateCount;
    ZP_STATUS TerminalCreateStatus;
    ZP_CHANNEL_HANDLE TerminalChannel;
    ULONG TerminalProcessId;
    ULONG ChannelDataCount;
    ULONG ChannelDataLength;
    ULONG ChannelWritableCount;
    ULONG ChannelWritableCredit;
    ULONG ChannelCloseCount;
    ZP_STATUS ChannelCloseStatus;
    ULONG RequestStatusCount;
    ULONG ClientStateCount;
    ZP_CLIENT_STATE ClientStates[8];
    ZP_STATUS ClientStatuses[8];
    LOGICAL CloseClientOnStopped;
    NTSTATUS ClientCloseStatus;
    ULONG ServerStateCount;
    ZP_SERVER_STATE ServerStates[8];
    ZP_STATUS ServerStatuses[8];
    LOGICAL CloseServerOnStopped;
    NTSTATUS ServerCloseStatus;
} SDK_TEST_CONTEXT, *PSDK_TEST_CONTEXT;

typedef struct _SDK_SYSTEM_LOOPBACK
{
    ZP_CONNECTION_OBJECT Connection;
    ZP_CLIENT_OBJECT Client;
    ULONG SendCount;
    ULONG CallbackCount;
    ULONG DestroyCount;
    ZP_STATUS Status;
    ZP_SYSTEM_ARCHITECTURE Architecture;
    ULONG ProcessorCount;
    ULONG ProcessCount;
    LOGICAL FoundCurrentProcess;
    ULONG ServiceCount;
    ULONG RegistryCallbackCount;
    ZP_STATUS RegistryStatus;
    ULONG RegistryPageCount;
    ULONG FileCallbackCount;
    ZP_STATUS FileStatus;
    ULONGLONG FileSize;
    ULONG FilePageCount;
    ULONG FileHashCallbackCount;
    ZP_STATUS FileHashStatus;
    ZP_FILE_HASH_ALGORITHM FileHashAlgorithm;
    ULONG FileHashDigestLength;
    ULONGLONG FileHashSize;
} SDK_SYSTEM_LOOPBACK, *PSDK_SYSTEM_LOOPBACK;

typedef struct _SDK_REQUEST_CONNECTION
{
    ZP_CONNECTION_OBJECT Connection;
    PSDK_TEST_CONTEXT Context;
    volatile LONG ActiveSend;
    volatile LONG OrderedSendCount;
    volatile LONG Failed;
    ULONG SendDelay;
    ULONG DestroyCount;
} SDK_REQUEST_CONNECTION, *PSDK_REQUEST_CONNECTION;

#define SDK_ORDERED_REQUEST_COUNT 16

typedef struct _SDK_ORDERED_REQUEST_THREAD
{
    PSDK_REQUEST_CONNECTION Connection;
    HANDLE StartEvent;
    NTSTATUS Status;
} SDK_ORDERED_REQUEST_THREAD, *PSDK_ORDERED_REQUEST_THREAD;

static
VOID
NTAPI
SDKTest_RequestConnectionDestroy(
    _Inout_ PZP_CONNECTION_OBJECT Connection)
{
    PSDK_REQUEST_CONNECTION RequestConnection = CONTAINING_RECORD(
        Connection,
        SDK_REQUEST_CONNECTION,
        Connection);

    RequestConnection->DestroyCount++;
}

static
NTSTATUS
NTAPI
SDKTest_RequestConnectionSend(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PSDK_REQUEST_CONNECTION RequestConnection = CONTAINING_RECORD(
        Connection,
        SDK_REQUEST_CONNECTION,
        Connection);
    PSDK_TEST_CONTEXT TestContext = RequestConnection->Context;
    ZP_REQUEST_VIEW Request;
    ZP_CHANNEL_DATA_VIEW ChannelData;
    ZP_CHANNEL_CLOSE ChannelClose;
    NTSTATUS Status;

    if (RequestConnection->SendDelay != 0 &&
        InterlockedIncrement(&RequestConnection->ActiveSend) != 1)
    {
        InterlockedExchange(&RequestConnection->Failed, TRUE);
    }
    if (RequestConnection->SendDelay != 0)
    {
        Sleep(RequestConnection->SendDelay);
    }
    if (TestContext != NULL)
    {
        TestContext->SendMessageType = MessageType;
    }
    if (MessageType == ZpMessageChannelData)
    {
        Status = ZpMessage_DecodeChannelData(Body,
                                             BodyLength,
                                             &ChannelData);
        if (NT_SUCCESS(Status))
        {
            TestContext->SendChannelId = ChannelData.ChannelId;
            TestContext->SendChannelDataLength = ChannelData.Data.Length;
        }
        return Status;
    }
    if (MessageType == ZpMessageChannelClose)
    {
        Status = ZpMessage_DecodeChannelClose(Body,
                                              BodyLength,
                                              &ChannelClose);
        if (NT_SUCCESS(Status))
        {
            TestContext->SendChannelId = ChannelClose.ChannelId;
            TestContext->SendChannelStatus = ChannelClose.Status;
        }
        return Status;
    }
    if (MessageType == ZpMessageChannelWindow)
    {
        return ZpMessage_DecodeChannelWindow(Body,
                                             BodyLength,
                                             &TestContext->SendChannelId,
                                             &TestContext->SendChannelCredit);
    }
    if (MessageType == ZpMessageCancel)
    {
        return ZpMessage_DecodeCancel(Body,
                                      BodyLength,
                                      &TestContext->SendRequestId);
    }
    if (MessageType != ZpMessageRequest)
    {
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Status = ZpMessage_DecodeRequest(Body, BodyLength, &Request);
    if (NT_SUCCESS(Status))
    {
        if (RequestConnection->SendDelay != 0)
        {
            if (Request.RequestId != (ULONGLONG)InterlockedIncrement(
                                         &RequestConnection->OrderedSendCount))
            {
                InterlockedExchange(&RequestConnection->Failed, TRUE);
            }
        }
        else
        {
            TestContext->SendCount++;
            TestContext->SendRequestId = Request.RequestId;
            TestContext->SendModuleId = Request.ModuleId;
            TestContext->SendOperationId = Request.OperationId;
            TestContext->SendPayloadLength = Request.Payload.Length;
        }
    }
    if (RequestConnection->SendDelay != 0)
    {
        InterlockedDecrement(&RequestConnection->ActiveSend);
    }
    return Status;
}

static
VOID
NTAPI
SDKTest_OrderedRequestCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Payload);
    UNREFERENCED_PARAMETER(Context);
}

static
DWORD
WINAPI
SDKTest_OrderedRequestThread(
    _In_ PVOID Context)
{
    PSDK_ORDERED_REQUEST_THREAD Thread = Context;
    ZP_REQUEST_HANDLE Request;

    WaitForSingleObject(Thread->StartEvent, INFINITE);
    Thread->Status = ZpServer_SendRequest(
                         (ZP_CONNECTION_HANDLE)&Thread->Connection->Connection,
                         ZP_SYSTEM_MODULE_ID,
                         ZP_SYSTEM_OPERATION_INFO,
                         0,
                         NULL,
                         0,
                         SDKTest_OrderedRequestCallback,
                         NULL,
                         &Request);
    if (NT_SUCCESS(Thread->Status))
    {
        ZpRequest_Close(Request);
    }
    return 0;
}

static
LOGICAL
SDKTest_OrderedConcurrentRequests(VOID)
{
    static const ZP_MODULE_RECORD Module = {
        ZP_SYSTEM_MODULE_ID,
        ZP_SYSTEM_MODULE_VERSION
    };
    SDK_REQUEST_CONNECTION Connection = { 0 };
    SDK_ORDERED_REQUEST_THREAD Threads[SDK_ORDERED_REQUEST_COUNT] = { 0 };
    HANDLE ThreadHandles[SDK_ORDERED_REQUEST_COUNT] = { 0 };
    HANDLE StartEvent = NULL;
    ULONG Index;
    LOGICAL Result = FALSE;

    if (!NT_SUCCESS(ZpServerConnection_Initialize(
                        &Connection.Connection,
                        SDK_ORDERED_REQUEST_COUNT,
                        1,
                        SDKTest_RequestConnectionSend,
                        SDKTest_RequestConnectionDestroy)))
    {
        return FALSE;
    }
    ZpServerConnection_SetModules(&Connection.Connection, &Module, 1);
    ZpServerConnection_SetPhase(&Connection.Connection,
                                ZpConnectionPhaseReady);
    Connection.SendDelay = 5;
    StartEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (StartEvent == NULL)
    {
        goto Cleanup;
    }
    for (Index = 0; Index < ARRAYSIZE(Threads); Index++)
    {
        Threads[Index].Connection = &Connection;
        Threads[Index].StartEvent = StartEvent;
        ThreadHandles[Index] = CreateThread(NULL,
                                            0,
                                            SDKTest_OrderedRequestThread,
                                            &Threads[Index],
                                            0,
                                            NULL);
        if (ThreadHandles[Index] == NULL)
        {
            goto Cleanup;
        }
    }
    SetEvent(StartEvent);
    if (WaitForMultipleObjects(ARRAYSIZE(ThreadHandles),
                               ThreadHandles,
                               TRUE,
                               INFINITE) != WAIT_OBJECT_0)
    {
        goto Cleanup;
    }
    Result = Connection.Failed == FALSE &&
             Connection.OrderedSendCount == SDK_ORDERED_REQUEST_COUNT;
    for (Index = 0; Result && Index < ARRAYSIZE(Threads); Index++)
    {
        Result = NT_SUCCESS(Threads[Index].Status);
    }

Cleanup:
    if (StartEvent != NULL)
    {
        SetEvent(StartEvent);
    }
    for (Index = 0; Index < ARRAYSIZE(ThreadHandles); Index++)
    {
        if (ThreadHandles[Index] != NULL)
        {
            WaitForSingleObject(ThreadHandles[Index], INFINITE);
            CloseHandle(ThreadHandles[Index]);
        }
    }
    if (StartEvent != NULL)
    {
        CloseHandle(StartEvent);
    }
    ZpServerConnection_Close(&Connection.Connection,
                              ZpStatus_FromNtStatus(
                                  STATUS_CONNECTION_DISCONNECTED));
    ZpConnection_Release((ZP_CONNECTION_HANDLE)&Connection.Connection);
    return Result && Connection.DestroyCount == 1;
}

static
VOID
NTAPI
SDKTest_SystemConnectionDestroy(
    _Inout_ PZP_CONNECTION_OBJECT Connection)
{
    PSDK_SYSTEM_LOOPBACK Loopback = CONTAINING_RECORD(Connection,
                                                      SDK_SYSTEM_LOOPBACK,
                                                      Connection);

    Loopback->DestroyCount++;
}

static
NTSTATUS
NTAPI
SDKTest_SystemConnectionSend(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PSDK_SYSTEM_LOOPBACK Loopback = CONTAINING_RECORD(Connection,
                                                      SDK_SYSTEM_LOOPBACK,
                                                      Connection);
    BYTE Payload[30 + (MAX_COMPUTERNAME_LENGTH + 1) * sizeof(WCHAR)];
    PBYTE AllocatedPayload = NULL;
    PZP_CLIENT_FILE_CHANNEL FileChannel;
    ZP_REQUEST_VIEW Request;
    ZP_RESPONSE_VIEW Response;
    ULONG PayloadLength;
    LONG Pending = TRUE;
    NTSTATUS Status;
    ZP_STATUS ResultStatus;

    if (MessageType != ZpMessageRequest)
    {
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Status = ZpMessage_DecodeRequest(Body, BodyLength, &Request);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Loopback->SendCount++;
    if (Request.ModuleId == ZP_SYSTEM_MODULE_ID &&
        Request.OperationId == ZP_SYSTEM_OPERATION_INFO &&
        Request.Payload.Length == 0)
    {
        ResultStatus = ZpSystem_ExecuteInfo(Payload,
                                            sizeof(Payload),
                                            &PayloadLength);
    }
    else if (Request.ModuleId == ZP_FILE_MODULE_ID)
    {
        Status = ZpFile_Execute(&Loopback->Client,
                                Request.OperationId,
                                Request.Payload.Buffer,
                                Request.Payload.Length,
                                &Pending,
                                &AllocatedPayload,
                                &PayloadLength,
                                 &FileChannel);
        ResultStatus = ZpStatus_FromNtStatus(Status);
    }
    else if (Request.ModuleId == ZP_PROCESS_MODULE_ID)
    {
        ResultStatus = ZpProcess_Execute(Request.OperationId,
                                         Request.Payload.Buffer,
                                         Request.Payload.Length,
                                         &AllocatedPayload,
                                         &PayloadLength);
    }
    else if (Request.ModuleId == ZP_SERVICE_MODULE_ID)
    {
        ResultStatus = ZpService_Execute(Request.OperationId,
                                         Request.Payload.Buffer,
                                         Request.Payload.Length,
                                         &AllocatedPayload,
                                         &PayloadLength);
    }
    else if (Request.ModuleId == ZP_REGISTRY_MODULE_ID)
    {
        Status = ZpRegistry_Execute(Request.OperationId,
                                    Request.Payload.Buffer,
                                    Request.Payload.Length,
                                    &AllocatedPayload,
                                    &PayloadLength);
        ResultStatus = ZpStatus_FromNtStatus(Status);
    }
    else if (Request.ModuleId == ZP_EVENT_LOG_MODULE_ID)
    {
        ResultStatus = ZpEventLog_Execute(Request.OperationId,
                                          Request.Payload.Buffer,
                                          Request.Payload.Length,
                                          &Pending,
                                          &AllocatedPayload,
                                          &PayloadLength);
    }
    else
    {
        ResultStatus = ZpStatus_FromNtStatus(STATUS_PROTOCOL_UNREACHABLE);
    }
    Response.RequestId = Request.RequestId;
    Response.Status = ResultStatus;
    Response.Payload.Buffer = ZpStatus_IsSuccess(ResultStatus) ?
                                  (AllocatedPayload != NULL ?
                                       AllocatedPayload :
                                       Payload) :
                                  NULL;
    Response.Payload.Length = ZpStatus_IsSuccess(ResultStatus) ? PayloadLength : 0;
    Status = ZpServerConnection_ReceiveResponse(Connection, &Response);
    if (AllocatedPayload != NULL)
    {
        Mem_Free(AllocatedPayload);
    }
    return Status;
}

static
VOID
NTAPI
SDKTest_SystemInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_SYSTEM_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PSDK_SYSTEM_LOOPBACK Loopback = Context;

    Loopback->CallbackCount++;
    Loopback->Status = Status;
    if (Info != NULL)
    {
        Loopback->Architecture = Info->Architecture;
        Loopback->ProcessorCount = Info->ProcessorCount;
    }
    ZpRequest_Close(Request);
}

static
VOID
NTAPI
SDKTest_ProcessListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_PROCESS_LIST_VIEW Processes,
    _In_opt_ PVOID Context)
{
    PSDK_SYSTEM_LOOPBACK Loopback = Context;
    ZP_PROCESS_RECORD_VIEW Process;
    ULONG Index;

    Loopback->CallbackCount++;
    Loopback->Status = Status;
    if (Processes != NULL)
    {
        Loopback->ProcessCount = Processes->Count;
        for (Index = 0; Index < Processes->Count; Index++)
        {
            if (NT_SUCCESS(ZpProcess_GetRecord(Processes, Index, &Process)) &&
                Process.ProcessId == GetCurrentProcessId())
            {
                Loopback->FoundCurrentProcess = TRUE;
                break;
            }
        }
    }
    ZpRequest_Close(Request);
}

static
VOID
NTAPI
SDKTest_ServiceListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_SERVICE_LIST_VIEW Services,
    _In_opt_ PVOID Context)
{
    PSDK_SYSTEM_LOOPBACK Loopback = Context;

    Loopback->CallbackCount++;
    Loopback->Status = Status;
    if (Services != NULL)
    {
        Loopback->ServiceCount = Services->Count;
    }
    ZpRequest_Close(Request);
}

static
VOID
NTAPI
SDKTest_RegistryPageLoopbackCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PSDK_SYSTEM_LOOPBACK Loopback = Context;

    Loopback->RegistryCallbackCount++;
    Loopback->RegistryStatus = Status;
    if (Page != NULL)
    {
        Loopback->RegistryPageCount = Page->Records.Count;
    }
    ZpRequest_Close(Request);
}

static
VOID
NTAPI
SDKTest_FileQueryLoopbackCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_INFO Info,
    _In_opt_ PVOID Context)
{
    PSDK_SYSTEM_LOOPBACK Loopback = Context;

    Loopback->FileCallbackCount++;
    Loopback->FileStatus = Status;
    if (Info != NULL)
    {
        Loopback->FileSize = Info->Size;
    }
    ZpRequest_Close(Request);
}

static
VOID
NTAPI
SDKTest_FilePageLoopbackCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PSDK_SYSTEM_LOOPBACK Loopback = Context;

    Loopback->FileStatus = Status;
    if (Page != NULL)
    {
        Loopback->FilePageCount = Page->Files.Count;
    }
    ZpRequest_Close(Request);
}

static
VOID
NTAPI
SDKTest_FileStatusLoopbackCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_SYSTEM_LOOPBACK Loopback = Context;

    Loopback->FileStatus = Status;
    ZpRequest_Close(Request);
}

static
VOID
NTAPI
SDKTest_FileHashLoopbackCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_HASH_VIEW Hash,
    _In_opt_ PVOID Context)
{
    PSDK_SYSTEM_LOOPBACK Loopback = Context;

    Loopback->FileHashCallbackCount++;
    Loopback->FileHashStatus = Status;
    if (Hash != NULL)
    {
        Loopback->FileHashAlgorithm = Hash->Algorithm;
        Loopback->FileHashDigestLength = Hash->Digest.Length;
        Loopback->FileHashSize = Hash->FileSize;
    }
    ZpRequest_Close(Request);
}

static
ZP_STATUS
NTAPI
SDKTest_TransportStart(
    _In_opt_ PVOID Context,
    _In_ ULONG EndpointIndex)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    TestContext->StartEndpointIndices[TestContext->StartCount++] = EndpointIndex;
    return TestContext->StartStatus;
}

static
VOID
NTAPI
SDKTest_TransportStop(
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    TestContext->StopCount++;
}

static
NTSTATUS
NTAPI
SDKTest_TransportSend(
    _In_opt_ PVOID Context,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PSDK_TEST_CONTEXT TestContext = Context;
    ZP_CHANNEL_DATA_VIEW ChannelData;
    ZP_CHANNEL_CLOSE ChannelClose;
    ZP_RESPONSE_VIEW Response;

    TestContext->SendCount++;
    TestContext->SendMessageType = MessageType;
    if (MessageType == ZpMessagePing)
    {
        ZpMessage_DecodePing(MessageType, Body, BodyLength, &TestContext->SendToken);
    }
    else if (MessageType == ZpMessageChannelWindow)
    {
        ZpMessage_DecodeChannelWindow(Body,
                                      BodyLength,
                                      &TestContext->SendChannelId,
                                      &TestContext->SendChannelCredit);
    }
    else if (MessageType == ZpMessageChannelData &&
             NT_SUCCESS(ZpMessage_DecodeChannelData(Body,
                                                     BodyLength,
                                                     &ChannelData)))
    {
        TestContext->SendChannelId = ChannelData.ChannelId;
        TestContext->SendChannelDataLength = ChannelData.Data.Length;
    }
    else if (MessageType == ZpMessageChannelClose &&
             NT_SUCCESS(ZpMessage_DecodeChannelClose(Body,
                                                      BodyLength,
                                                      &ChannelClose)))
    {
        TestContext->SendChannelId = ChannelClose.ChannelId;
        TestContext->SendChannelStatus = ChannelClose.Status;
    }
    else if (MessageType == ZpMessageResponse &&
             NT_SUCCESS(ZpMessage_DecodeResponse(Body,
                                                  BodyLength,
                                                  &Response)))
    {
        TestContext->SendRequestId = Response.RequestId;
        TestContext->RequestStatus = Response.Status;
    }
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
SDKTest_FileOpenReadCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_ ULONGLONG Offset,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->FileOpenReadCount++;
    TestContext->FileOpenReadStatus = Status;
    TestContext->FileChannel = Channel;
    TestContext->FileSize = FileSize;
    TestContext->FileOffset = Offset;
}

static
VOID
NTAPI
SDKTest_FileHashCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_HASH_VIEW Hash,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->FileHashCount++;
    TestContext->FileHashStatus = Status;
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->FileHashAlgorithm = Hash->Algorithm;
        TestContext->FileHashSize = Hash->FileSize;
        RtlCopyMemory(TestContext->FileDigest,
                      Hash->Digest.Buffer,
                      Hash->Digest.Length);
    }
}

static
VOID
NTAPI
SDKTest_FilePageCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->FilePageCount++;
    TestContext->FilePageStatus = Status;
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->FilePageFileCount = Page->Files.Count;
        TestContext->FilePageEnumerationId = Page->EnumerationId;
    }
}

static
VOID
NTAPI
SDKTest_RegistryPageCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->RegistryPageCount++;
    TestContext->RegistryPageStatus = Status;
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->RegistryRecordCount = Page->Records.Count;
    }
}

static
VOID
NTAPI
SDKTest_RegistryValueCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_VALUE_VIEW Value,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->RegistryValueCount++;
    TestContext->RegistryValueStatus = Status;
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->RegistryValueType = Value->Type;
        TestContext->RegistryValueDataLength = Value->Data.Length;
    }
}

static
VOID
NTAPI
SDKTest_EventLogPageCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_EVENT_LOG_PAGE_VIEW* Page,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->EventPageCount++;
    TestContext->EventPageStatus = Status;
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->EventPageRecordCount = Page->Records.Count;
        TestContext->EventPageHasMore = Page->HasMore;
    }
}

static
VOID
NTAPI
SDKTest_FileOpenWriteCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->FileOpenWriteCount++;
    TestContext->FileOpenWriteStatus = Status;
    TestContext->FileWriteChannel = Channel;
    TestContext->FileWriteSize = FileSize;
}

static
VOID
NTAPI
SDKTest_TerminalCreateCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG ProcessId,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->TerminalCreateCount++;
    TestContext->TerminalCreateStatus = Status;
    TestContext->TerminalChannel = Channel;
    TestContext->TerminalProcessId = ProcessId;
}

static
VOID
NTAPI
SDKTest_ChannelDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    TestContext->ChannelDataCount++;
    TestContext->ChannelDataLength = Data->Length;
}

static
VOID
NTAPI
SDKTest_ChannelWritableCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    TestContext->ChannelWritableCount++;
    TestContext->ChannelWritableCredit = CreditBytes;
}

static
VOID
NTAPI
SDKTest_ChannelCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    TestContext->ChannelCloseCount++;
    TestContext->ChannelCloseStatus = Status;
}

static
VOID
NTAPI
SDKTest_RequestStatusCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->RequestStatusCount++;
    TestContext->RequestStatus = Status;
}

static const ZP_TRANSPORT_OPERATIONS SDKTest_TransportOperations = {
    SDKTest_TransportStart,
    SDKTest_TransportStop,
    SDKTest_TransportSend
};

static
LOGICAL
SDKTest_AuthenticationRoundTrip(VOID)
{
    BCRYPT_ALG_HANDLE Algorithm = NULL;
    BCRYPT_KEY_HANDLE Key = NULL;
    BCRYPT_ECCKEY_BLOB* Blob;
    BYTE BlobBuffer[sizeof(BCRYPT_ECCKEY_BLOB) + 64];
    BYTE PublicKey[ZP_CLIENT_PUBLIC_KEY_SIZE];
    BYTE Challenge[ZP_SERVER_CHALLENGE_SIZE] = { 1 };
    BYTE Hash[32];
    BYTE Signature[ZP_CLIENT_SIGNATURE_SIZE];
    ULONG BlobSize, SignatureSize;
    NTSTATUS Status;
    LOGICAL Result = FALSE;

    Status = BCryptOpenAlgorithmProvider(&Algorithm,
                                         BCRYPT_ECDSA_P256_ALGORITHM,
                                         NULL,
                                         0);
    if (!NT_SUCCESS(Status) ||
        !NT_SUCCESS(Status = BCryptGenerateKeyPair(Algorithm, &Key, 256, 0)) ||
        !NT_SUCCESS(Status = BCryptFinalizeKeyPair(Key, 0)) ||
        !NT_SUCCESS(Status = BCryptExportKey(Key,
                                             NULL,
                                             BCRYPT_ECCPUBLIC_BLOB,
                                             BlobBuffer,
                                             sizeof(BlobBuffer),
                                             &BlobSize,
                                             0)))
    {
        goto Cleanup;
    }
    Blob = (BCRYPT_ECCKEY_BLOB*)BlobBuffer;
    if (BlobSize != sizeof(BlobBuffer) ||
        Blob->dwMagic != BCRYPT_ECDSA_PUBLIC_P256_MAGIC ||
        Blob->cbKey != 32)
    {
        goto Cleanup;
    }
    PublicKey[0] = 0x04;
    RtlCopyMemory(PublicKey + 1, BlobBuffer + sizeof(*Blob), 64);
    Status = ZpAuthentication_Hash(Challenge, PublicKey, Hash);
    if (!NT_SUCCESS(Status) ||
        !NT_SUCCESS(Status = BCryptSignHash(Key,
                                            NULL,
                                            Hash,
                                            sizeof(Hash),
                                            Signature,
                                            sizeof(Signature),
                                            &SignatureSize,
                                            0)) ||
        SignatureSize != sizeof(Signature) ||
        !NT_SUCCESS(ZpAuthentication_Verify(PublicKey, Challenge, Signature)))
    {
        goto Cleanup;
    }
    Signature[0] ^= 1;
    Result = !NT_SUCCESS(ZpAuthentication_Verify(PublicKey, Challenge, Signature));

Cleanup:
    if (Key != NULL)
    {
        BCryptDestroyKey(Key);
    }
    if (Algorithm != NULL)
    {
        BCryptCloseAlgorithmProvider(Algorithm, 0);
    }
    RtlSecureZeroMemory(Hash, sizeof(Hash));
    RtlSecureZeroMemory(Signature, sizeof(Signature));
    return Result;
}

static
VOID
NTAPI
SDKTest_ClientStateCallback(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;
    ULONG Index;

    UNREFERENCED_PARAMETER(Client);
    if (TestContext != NULL)
    {
        Index = TestContext->ClientStateCount++;
        TestContext->ClientStates[Index] = State;
        TestContext->ClientStatuses[Index] = Status;
        if (TestContext->CloseClientOnStopped && State == ZpClientStateStopped)
        {
            TestContext->ClientCloseStatus = ZpClient_Close(Client);
        }
    }
}

static
VOID
NTAPI
SDKTest_ServerStateCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_SERVER_STATE State,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;
    ULONG Index;

    UNREFERENCED_PARAMETER(Server);
    if (TestContext != NULL)
    {
        Index = TestContext->ServerStateCount++;
        TestContext->ServerStates[Index] = State;
        TestContext->ServerStatuses[Index] = Status;
        if (TestContext->CloseServerOnStopped && State == ZpServerStateStopped)
        {
            TestContext->ServerCloseStatus = ZpServer_Close(Server);
        }
    }
}

static
VOID
NTAPI
SDKTest_ServerConnectionCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_CONNECTION_PHASE Phase,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(Connection);
    UNREFERENCED_PARAMETER(Phase);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Context);
}

TEST_FUNC(SDKContract)
{
    WCHAR Host[] = L"127.0.0.1", ServerName[] = L"server.example", ClientKeyName[] = L"ClientKey";
    WCHAR ListenerHost[] = L"::";
    WCHAR EventChannel[] = L"System";
    WCHAR EventBookmark[] = L"<Bookmark>1</Bookmark>";
    WCHAR EventXml[] = L"<Event/>";
    WCHAR FileLoopbackPath[MAX_PATH];
    WCHAR FileAttributePath[MAX_PATH], TempPath[MAX_PATH];
    BYTE RootCertificate[] = { 0x30, 0x01, 0x00 };
    ZP_MODULE_RECORD Modules[] = { { 1, 1 }, { 2, 1 } };
    ZP_ENDPOINT Endpoint = { ZpTransportQuic, Host, 443, ServerName, NULL };
    ZP_ENDPOINT MixedEndpoints[] = {
        { ZpTransportTlsTcp, Host, 443, ServerName, NULL },
        { ZpTransportQuic, Host, 443, ServerName, NULL }
    };
    ZP_LISTENER_ENDPOINT Listener = { ZpTransportQuic, ListenerHost, 443, NULL };
    ZP_SERVER_DEPLOYMENT InvalidDeployment = { L"server.example", NULL };
    ZP_CLIENT_CONFIG ClientConfig = {
        sizeof(ZP_CLIENT_CONFIG),
        &Endpoint,
        1,
        RootCertificate,
        sizeof(RootCertificate),
        ClientKeyName,
        Modules,
        ARRAYSIZE(Modules),
        0,
        SDKTest_ClientStateCallback,
        NULL,
        NULL
    };
    ZP_SERVER_CONFIG ServerConfig = {
        sizeof(ZP_SERVER_CONFIG),
        &Listener,
        1,
        NULL,
        0,
        Modules,
        ARRAYSIZE(Modules),
        0,
        0,
        SDKTest_ServerStateCallback,
        SDKTest_ServerConnectionCallback,
        NULL
    };
    ZP_CLIENT_HANDLE Client;
    ZP_SERVER_HANDLE Server;
    PZP_CLIENT_OBJECT ClientObject;
    PZP_SERVER_OBJECT ServerObject;
    ZP_REQUEST_HANDLE Request;
    ZP_REQUEST_VIEW InboundRequest = { 7, 1, 1, 0, { NULL, 0 } };
    ZP_RESPONSE_VIEW Response;
    ZP_CHANNEL_DATA_VIEW ChannelData;
    ZP_CHANNEL_CLOSE ChannelClose;
    BYTE FileOpenReadResponse[3 * sizeof(ULONGLONG)];
    ULONG FileOpenReadResponseLength;
    BYTE FileHashResponse[sizeof(USHORT) + sizeof(ULONGLONG) +
                          ZP_FILE_SHA256_SIZE];
    ULONG FileHashResponseLength;
    BYTE FileDigest[ZP_FILE_SHA256_SIZE];
    BYTE FileOpenWriteResponse[2 * sizeof(ULONGLONG)];
    ULONG FileOpenWriteResponseLength;
    BYTE FileWriteData[16] = { 0 };
    BYTE FileWriteTooLongData[17] = { 0 };
    ZP_FILE_RECORD FilePageRecords[] = {
        {
            { FILE_ATTRIBUTE_ARCHIVE, 16, 1, 2, 3, FALSE },
            L"Upload.bin",
            10
        }
    };
    BYTE FilePageResponse[256];
    ULONG FilePageResponseLength;
    ZP_REGISTRY_KEY_RECORD RegistryKeyRecords[] = {
        { L"Child", 5, 123, TRUE }
    };
    ZP_REGISTRY_VALUE_RECORD RegistryValueRecords[] = {
        { L"", 0, 4, sizeof(ULONG), NULL, 0 }
    };
    BYTE RegistryResponse[256];
    ULONG RegistryResponseLength;
    ULONG RegistryValueData = 42;
    ZP_EVENT_LOG_RECORD EventRecords[] = {
        {
            EventBookmark,
            ARRAYSIZE(EventBookmark) - 1,
            EventXml,
            ARRAYSIZE(EventXml) - 1
        }
    };
    BYTE EventPageResponse[256];
    ULONG EventPageResponseLength;
    DWORD FileLoopbackPathLength, TempPathLength;
    ULONG FileLoopbackDirectoryLength;
    BYTE TerminalCreateResponse[sizeof(ULONGLONG) + sizeof(ULONG)];
    ULONG TerminalCreateResponseLength;
    BYTE TerminalInput[] = { 'e', 'x', 'i', 't' };
    BYTE TerminalTooLongInput[] = { 'e', 'x', 'i', 't', '\r' };
    ZP_BUFFER_VIEW EmptyPayload = { NULL, 0 };
    SDK_TEST_CONTEXT TestContext = { { ZpStatusNone, 0 } };
    SDK_TEST_CONTEXT TlsContext = {
        { ZpStatusNtStatus, STATUS_ACCESS_DENIED }
    };
    SDK_TEST_CONTEXT QuicContext = { { ZpStatusNone, 0 } };
    SDK_SYSTEM_LOOPBACK SystemLoopback = { 0 };
    SDK_REQUEST_CONNECTION RegistryConnection = { 0 };
    LOGICAL TempFileCreated;
    ULONGLONG CanceledRequestId;
    ZP_MODULE_RECORD LoopbackModules[] = {
        { ZP_SYSTEM_MODULE_ID, ZP_SYSTEM_MODULE_VERSION },
        { ZP_PROCESS_MODULE_ID, ZP_PROCESS_MODULE_VERSION },
        { ZP_SERVICE_MODULE_ID, ZP_SERVICE_MODULE_VERSION },
        { ZP_FILE_MODULE_ID, ZP_FILE_MODULE_VERSION },
        { ZP_REGISTRY_MODULE_ID, ZP_REGISTRY_MODULE_VERSION }
    };
    ZP_MODULE_RECORD ServerModules[] = {
        { ZP_SERVICE_MODULE_ID, ZP_SERVICE_MODULE_VERSION },
        { ZP_FILE_MODULE_ID, ZP_FILE_MODULE_VERSION },
        { ZP_TERMINAL_MODULE_ID, ZP_TERMINAL_MODULE_VERSION },
        { ZP_EVENT_LOG_MODULE_ID, ZP_EVENT_LOG_MODULE_VERSION },
        { ZP_REGISTRY_MODULE_ID, ZP_REGISTRY_MODULE_VERSION }
    };
    QUIC_ADDR QuicAddress;
    QUIC_STATUS QuicStatus;

    RtlFillMemory(FileDigest, sizeof(FileDigest), 0x5A);

    TEST_OK(ZpTransportQuic == 1 && ZpTransportTlsTcp == 2 && ZpTransportWss == 3);
    TEST_OK(Endpoint.Transport == ZpTransportQuic &&
            Endpoint.Port == 443 &&
            wcscmp(Endpoint.ServerName, L"server.example") == 0);
    TEST_OK(Listener.Transport == ZpTransportQuic &&
            wcscmp(Listener.Host, L"::") == 0 &&
            Listener.Port == 443);
    TEST_OK(ClientConfig.Size == sizeof(ZP_CLIENT_CONFIG));
    TEST_OK(ServerConfig.Size == sizeof(ZP_SERVER_CONFIG));
    TEST_OK(sizeof(ZP_CLIENT_HANDLE) == sizeof(PVOID));
    TEST_OK(sizeof(ZP_SERVER_HANDLE) == sizeof(PVOID));
    TEST_OK(sizeof(ZP_CONNECTION_HANDLE) == sizeof(PVOID));
    TEST_OK(ZP_CLIENT_DEFAULT_CONNECT_TIMEOUT_MILLISECONDS == 10000);
    TEST_OK(ZP_CLIENT_DEFAULT_RETRY_MAX_MILLISECONDS == 60000);
    TEST_OK(ZP_CLIENT_DEFAULT_STABLE_RESET_MILLISECONDS == 60000);
    TEST_OK(ZP_CLIENT_DEFAULT_RETRY_JITTER_PERCENT == 20);
    TEST_OK(ZpClientRetry_GetBaseDelay(0) == 1000 &&
            ZpClientRetry_GetBaseDelay(1) == 2000 &&
            ZpClientRetry_GetBaseDelay(5) == 32000 &&
            ZpClientRetry_GetBaseDelay(6) == 60000 &&
            ZpClientRetry_GetBaseDelay(MAXULONG) == 60000);
    TEST_OK(ZpClientRetry_GetDelay(0, 0) == 800 &&
            ZpClientRetry_GetDelay(0, 400) == 1200);
    TEST_OK(ZpClientRetry_GetDelay(6, 0) == 48000 &&
            ZpClientRetry_GetDelay(6, 24000) == 72000);
    TEST_OK(ZpQuicAlpn.Length == sizeof(ZP_QUIC_ALPN) - sizeof(ANSI_NULL));
    TEST_OK(ZpStatus_FromCode(ZpStatusQuic,
                               (ULONG)QUIC_STATUS_CONNECTION_TIMEOUT).Code ==
                (ULONG)QUIC_STATUS_CONNECTION_TIMEOUT &&
            sizeof(ZP_STATUS_TYPE) == sizeof(USHORT) &&
            sizeof(ZP_STATUS) == 2 * sizeof(ULONG) &&
            ZpStatus_IsValid(ZpStatus_FromProcessExit(0)) &&
            !ZpStatus_IsValid(ZpStatus_Make(ZpStatusQuic, 0)));
    TEST_OK(SDKTest_AuthenticationRoundTrip());
    TEST_OK(SDKTest_OrderedConcurrentRequests());
    RtlInitializeSRWLock(&SystemLoopback.Client.FileEnumerationLock);
    InitializeListHead(&SystemLoopback.Client.FileEnumerations);
    SystemLoopback.Client.NextFileEnumerationId = 1;
    SystemLoopback.Client.State = ZpClientStateReady;
    TEST_OK(NT_SUCCESS(ZpServerConnection_Initialize(
                           &SystemLoopback.Connection,
                           1,
                           1,
                           SDKTest_SystemConnectionSend,
                           SDKTest_SystemConnectionDestroy)));
    ZpServerConnection_SetModules(&SystemLoopback.Connection,
                                  LoopbackModules,
                                  ARRAYSIZE(LoopbackModules));
    ZpServerConnection_SetPhase(&SystemLoopback.Connection,
                                ZpConnectionPhaseReady);
    TEST_OK(NT_SUCCESS(ZpServer_GetSystemInfo(
                (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                0,
                SDKTest_SystemInfoCallback,
                &SystemLoopback,
                &Request)) &&
            SystemLoopback.SendCount == 1 &&
            SystemLoopback.CallbackCount == 1 &&
            ZpStatus_IsSuccess(SystemLoopback.Status) &&
            SystemLoopback.Architecture >= ZpSystemArchitectureX86 &&
            SystemLoopback.Architecture <= ZpSystemArchitectureArm64 &&
            SystemLoopback.ProcessorCount != 0);
    TEST_OK(NT_SUCCESS(ZpServer_EnumerateProcesses(
                (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                0,
                SDKTest_ProcessListCallback,
                &SystemLoopback,
                &Request)) &&
            SystemLoopback.SendCount == 2 &&
            SystemLoopback.CallbackCount == 2 &&
            ZpStatus_IsSuccess(SystemLoopback.Status) &&
            SystemLoopback.ProcessCount != 0 &&
            SystemLoopback.FoundCurrentProcess);
    TEST_OK(NT_SUCCESS(ZpServer_EnumerateServices(
                (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                0,
                SDKTest_ServiceListCallback,
                &SystemLoopback,
                &Request)) &&
            SystemLoopback.SendCount == 3 &&
            SystemLoopback.CallbackCount == 3 &&
            ZpStatus_IsSuccess(SystemLoopback.Status) &&
            SystemLoopback.ServiceCount != 0);
    TEST_OK(NT_SUCCESS(ZpServer_EnumerateRegistryKeysPage(
                (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                ZpRegistryLocalMachine,
                L"Software",
                ARRAYSIZE(L"Software") - 1,
                NULL,
                0,
                16,
                0,
                SDKTest_RegistryPageLoopbackCallback,
                &SystemLoopback,
                &Request)) &&
            SystemLoopback.RegistryCallbackCount == 1 &&
            ZpStatus_IsSuccess(SystemLoopback.RegistryStatus) &&
            SystemLoopback.RegistryPageCount != 0);
    FileLoopbackPathLength = GetModuleFileNameW(NULL,
                                                FileLoopbackPath,
                                                ARRAYSIZE(FileLoopbackPath));
    TEST_OK(FileLoopbackPathLength != 0 &&
            FileLoopbackPathLength < ARRAYSIZE(FileLoopbackPath));
    TEST_OK(NT_SUCCESS(ZpServer_QueryFile(
                (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                FileLoopbackPath,
                FileLoopbackPathLength,
                0,
                SDKTest_FileQueryLoopbackCallback,
                &SystemLoopback,
                &Request)) &&
            SystemLoopback.FileCallbackCount == 1 &&
            ZpStatus_IsSuccess(SystemLoopback.FileStatus) &&
            SystemLoopback.FileSize != 0);
    TEST_OK(NT_SUCCESS(ZpServer_HashFile(
                (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                FileLoopbackPath,
                FileLoopbackPathLength,
                ZpFileHashSha256,
                0,
                SDKTest_FileHashLoopbackCallback,
                &SystemLoopback,
                &Request)) &&
            SystemLoopback.FileHashCallbackCount == 1 &&
            ZpStatus_IsSuccess(SystemLoopback.FileHashStatus) &&
            SystemLoopback.FileHashAlgorithm == ZpFileHashSha256 &&
            SystemLoopback.FileHashDigestLength == ZP_FILE_SHA256_SIZE &&
            SystemLoopback.FileHashSize == SystemLoopback.FileSize);
    TEST_OK(NT_SUCCESS(ZpServer_HashFile(
                (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                FileLoopbackPath,
                FileLoopbackPathLength,
                ZpFileHashCrc32,
                0,
                SDKTest_FileHashLoopbackCallback,
                &SystemLoopback,
                &Request)) &&
            SystemLoopback.FileHashCallbackCount == 2 &&
            ZpStatus_IsSuccess(SystemLoopback.FileHashStatus) &&
            SystemLoopback.FileHashAlgorithm == ZpFileHashCrc32 &&
            SystemLoopback.FileHashDigestLength == ZP_FILE_CRC32_SIZE);
    TEST_OK(NT_SUCCESS(ZpServer_HashFile(
                (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                FileLoopbackPath,
                FileLoopbackPathLength,
                ZpFileHashMd5,
                0,
                SDKTest_FileHashLoopbackCallback,
                &SystemLoopback,
                &Request)) &&
            SystemLoopback.FileHashCallbackCount == 3 &&
            ZpStatus_IsSuccess(SystemLoopback.FileHashStatus) &&
            SystemLoopback.FileHashAlgorithm == ZpFileHashMd5 &&
            SystemLoopback.FileHashDigestLength == ZP_FILE_MD5_SIZE);
    TEST_OK(NT_SUCCESS(ZpServer_HashFile(
                (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                FileLoopbackPath,
                FileLoopbackPathLength,
                ZpFileHashSha1,
                0,
                SDKTest_FileHashLoopbackCallback,
                &SystemLoopback,
                &Request)) &&
            SystemLoopback.FileHashCallbackCount == 4 &&
            ZpStatus_IsSuccess(SystemLoopback.FileHashStatus) &&
            SystemLoopback.FileHashAlgorithm == ZpFileHashSha1 &&
            SystemLoopback.FileHashDigestLength == ZP_FILE_SHA1_SIZE);
    TempPathLength = GetTempPathW(ARRAYSIZE(TempPath), TempPath);
    TempFileCreated = TempPathLength != 0 &&
                      TempPathLength < ARRAYSIZE(TempPath) &&
                      GetTempFileNameW(TempPath,
                                       L"ZPF",
                                       0,
                                       FileAttributePath) != 0;
    TEST_OK(TempFileCreated);
    if (TempFileCreated)
    {
        TEST_OK(NT_SUCCESS(ZpServer_SetFileAttributes(
                    (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                    FileAttributePath,
                    (ULONG)wcslen(FileAttributePath),
                    FILE_ATTRIBUTE_HIDDEN,
                    0,
                    SDKTest_FileStatusLoopbackCallback,
                    &SystemLoopback,
                    &Request)) &&
                ZpStatus_IsSuccess(SystemLoopback.FileStatus) &&
                FlagOn(GetFileAttributesW(FileAttributePath),
                       FILE_ATTRIBUTE_HIDDEN));
        TEST_OK(NT_SUCCESS(ZpServer_SetFileAttributes(
                    (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                    FileAttributePath,
                    (ULONG)wcslen(FileAttributePath),
                    0,
                    0,
                    SDKTest_FileStatusLoopbackCallback,
                    &SystemLoopback,
                    &Request)) &&
                ZpStatus_IsSuccess(SystemLoopback.FileStatus) &&
                !FlagOn(GetFileAttributesW(FileAttributePath),
                        FILE_ATTRIBUTE_HIDDEN));
        DeleteFileW(FileAttributePath);
    }
    FileLoopbackDirectoryLength = (ULONG)(wcsrchr(FileLoopbackPath, L'\\') -
                                           FileLoopbackPath);
    TEST_OK(NT_SUCCESS(ZpServer_EnumerateFilesPage(
                (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                 FileLoopbackPath,
                 FileLoopbackDirectoryLength,
                 0,
                 0,
                SDKTest_FilePageLoopbackCallback,
                &SystemLoopback,
                &Request)) &&
            ZpStatus_IsSuccess(SystemLoopback.FileStatus) &&
            SystemLoopback.FilePageCount != 0);
    FileLoopbackPathLength = GetSystemDirectoryW(FileLoopbackPath,
                                                 ARRAYSIZE(FileLoopbackPath));
    TEST_OK(FileLoopbackPathLength != 0 &&
            FileLoopbackPathLength < ARRAYSIZE(FileLoopbackPath) &&
            NT_SUCCESS(ZpServer_EnumerateFilesPage(
                (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                FileLoopbackPath,
                FileLoopbackPathLength,
                0,
                0,
                SDKTest_FilePageLoopbackCallback,
                &SystemLoopback,
                &Request)) &&
             ZpStatus_IsSuccess(SystemLoopback.FileStatus) &&
             SystemLoopback.Client.FileEnumerationCount == 1);
    TEST_OK(NT_SUCCESS(ZpServer_EnumerateFilesPage(
                (ZP_CONNECTION_HANDLE)&SystemLoopback.Connection,
                L"C:\\ZPigeon.Does.Not.Exist",
                ARRAYSIZE(L"C:\\ZPigeon.Does.Not.Exist") - 1,
                0,
                0,
                SDKTest_FilePageLoopbackCallback,
                &SystemLoopback,
                &Request)) &&
             SDK_STATUS_IS(SystemLoopback.FileStatus,
                           STATUS_OBJECT_NAME_NOT_FOUND) &&
             SystemLoopback.Client.FileEnumerationCount == 1);
    ZpFile_ResetEnumeration(&SystemLoopback.Client);
    TEST_OK(SystemLoopback.Client.FileEnumerationCount == 0);
    ZpServerConnection_Close(&SystemLoopback.Connection,
                              ZpStatus_FromNtStatus(
                                  STATUS_CONNECTION_DISCONNECTED));
    ZpConnection_Release((ZP_CONNECTION_HANDLE)&SystemLoopback.Connection);
    TEST_OK(SystemLoopback.DestroyCount == 1);
    QuicStatus = KNSoftQuicInitialize();
    TEST_OK(QUIC_SUCCEEDED(QuicStatus));
    if (QUIC_SUCCEEDED(QuicStatus))
    {
        TEST_OK(ZpStatus_IsSuccess(
                    ZpQuic_ResolveAddress(L"127.0.0.1", 443, &QuicAddress)) &&
                QuicAddress.si_family == QUIC_ADDRESS_FAMILY_INET &&
                QuicAddrGetPort(&QuicAddress) == 443);
        KNSoftQuicUninitialize();
    }

    TEST_OK(NT_SUCCESS(ZpClient_Create(&ClientConfig, &Client)));
    ClientObject = (PZP_CLIENT_OBJECT)Client;
    Host[0] = L'X';
    ServerName[0] = L'X';
    ClientKeyName[0] = L'X';
    RootCertificate[0] = 0;
    Modules[0].ModuleVersion = 2;
    TEST_OK(ClientObject->State == ZpClientStateStopped);
    TEST_OK(ClientObject->Config.ConnectTimeoutMilliseconds ==
            ZP_CLIENT_DEFAULT_CONNECT_TIMEOUT_MILLISECONDS);
    TEST_OK(wcscmp(ClientObject->Config.Endpoints[0].Host, L"127.0.0.1") == 0 &&
            wcscmp(ClientObject->Config.Endpoints[0].ServerName, L"server.example") == 0);
    TEST_OK(wcscmp(ClientObject->Config.ClientKeyName, L"ClientKey") == 0);
    TEST_OK(ClientObject->Config.DeploymentRootCertificate[0] == 0x30);
    TEST_OK(ClientObject->Config.Modules[0].ModuleVersion == 1);
    TEST_OK(ClientObject->TransportOperations[ZpTransportQuic] != NULL &&
            ClientObject->TransportContexts[ZpTransportQuic] == &ClientObject->QuicTransport);
    ClientObject->State = ZpClientStateConnecting;
    TEST_OK(ZpClient_Close(Client) == STATUS_DEVICE_BUSY);
    ClientObject->State = ZpClientStateStopped;
    TEST_OK(NT_SUCCESS(ZpClient_Close(Client)));

    Host[0] = L'1';
    ServerName[0] = L's';
    ClientKeyName[0] = L'C';
    RootCertificate[0] = 0x30;
    Modules[0].ModuleVersion = 1;
    ClientConfig.Endpoints = MixedEndpoints;
    ClientConfig.EndpointCount = ARRAYSIZE(MixedEndpoints);
    TEST_OK(NT_SUCCESS(ZpClient_Create(&ClientConfig, &Client)));
    ClientObject = (PZP_CLIENT_OBJECT)Client;
    TEST_OK(ClientObject->TransportOperations[ZpTransportQuic] != NULL &&
            ClientObject->TransportContexts[ZpTransportQuic] == &ClientObject->QuicTransport);
    TEST_OK(NT_SUCCESS(ZpClient_Close(Client)));

    ClientConfig.CallbackContext = &TlsContext;
    TEST_OK(NT_SUCCESS(ZpClient_Create(&ClientConfig, &Client)));
    ClientObject = (PZP_CLIENT_OBJECT)Client;
    TEST_OK(NT_SUCCESS(ZpClient_SetTransport(Client,
                                             ZpTransportTlsTcp,
                                             &SDKTest_TransportOperations,
                                             &TlsContext)) &&
            NT_SUCCESS(ZpClient_SetTransport(Client,
                                             ZpTransportQuic,
                                             &SDKTest_TransportOperations,
                                             &QuicContext)));
    TEST_OK(NT_SUCCESS(ZpClient_Start(Client)) &&
            TlsContext.StartCount == 1 &&
            TlsContext.StartEndpointIndices[0] == 0 &&
            QuicContext.StartCount == 1 &&
            QuicContext.StartEndpointIndices[0] == 1 &&
            ClientObject->ActiveTransport == ZpTransportQuic &&
            ClientObject->EndpointIndex == 1);
    TEST_OK(NT_SUCCESS(ZpClient_Stop(Client)) && QuicContext.StopCount == 1);
    TEST_OK(NT_SUCCESS(ZpClient_NotifyState(Client,
                                           ZpClientStateStopped,
                                           ZpStatus_FromNtStatus(
                                               STATUS_SUCCESS))) &&
            NT_SUCCESS(ZpClient_Close(Client)));

    ClientConfig.Endpoints = &Endpoint;
    ClientConfig.EndpointCount = 1;
    ClientConfig.Size = 0;
    TEST_OK(ZpClient_Create(&ClientConfig, &Client) == STATUS_INVALID_PARAMETER);
    ClientConfig.Size = sizeof(ClientConfig);
    Endpoint.WssPath = L"/invalid";
    TEST_OK(ZpClient_Create(&ClientConfig, &Client) == STATUS_INVALID_PARAMETER);
    Endpoint.WssPath = NULL;
    Modules[1].ModuleId = Modules[0].ModuleId;
    TEST_OK(ZpClient_Create(&ClientConfig, &Client) == STATUS_INVALID_PARAMETER);
    Modules[1].ModuleId = 2;
    ServerConfig.MaxRequestsPerConnection =
        ZP_SERVER_MAX_REQUESTS_PER_CONNECTION + 1;
    TEST_OK(ZpServer_Create(&ServerConfig, &Server) == STATUS_INVALID_PARAMETER);
    ServerConfig.MaxRequestsPerConnection = 0;
    ServerConfig.MaxChannelsPerConnection =
        ZP_SERVER_MAX_CHANNELS_PER_CONNECTION + 1;
    TEST_OK(ZpServer_Create(&ServerConfig, &Server) == STATUS_INVALID_PARAMETER);
    ServerConfig.MaxChannelsPerConnection = 0;
    TEST_OK(NT_SUCCESS(ZpServer_Create(&ServerConfig, &Server)));
    ServerObject = (PZP_SERVER_OBJECT)Server;
    ListenerHost[0] = L'X';
    Modules[0].ModuleVersion = 2;
    TEST_OK(ServerObject->State == ZpServerStateStopped);
    TEST_OK(ServerObject->Config.MaxRequestsPerConnection ==
                ZP_SERVER_DEFAULT_MAX_REQUESTS_PER_CONNECTION &&
            ServerObject->Config.MaxChannelsPerConnection ==
                ZP_SERVER_DEFAULT_MAX_CHANNELS_PER_CONNECTION);
    TEST_OK(wcscmp(ServerObject->Config.Listeners[0].Host, L"::") == 0);
    TEST_OK(ServerObject->Config.Modules[0].ModuleVersion == 1);
    ServerObject->State = ZpServerStateRunning;
    TEST_OK(ZpServer_Close(Server) == STATUS_DEVICE_BUSY);
    ServerObject->State = ZpServerStateStopped;
    TEST_OK(NT_SUCCESS(ZpServer_Close(Server)));

    ListenerHost[0] = L':';
    Modules[0].ModuleVersion = 1;
    Listener.WssPath = L"/invalid";
    TEST_OK(ZpServer_Create(&ServerConfig, &Server) == STATUS_INVALID_PARAMETER);
    Listener.WssPath = NULL;
    ServerConfig.Deployments = &InvalidDeployment;
    ServerConfig.DeploymentCount = 1;
    TEST_OK(ZpServer_Create(&ServerConfig, &Server) == STATUS_INVALID_PARAMETER);

    ServerConfig.Deployments = NULL;
    ServerConfig.DeploymentCount = 0;
    Endpoint.Transport = ZpTransportTlsTcp;
    ClientConfig.CallbackContext = &TestContext;
    TEST_OK(NT_SUCCESS(ZpClient_Create(&ClientConfig, &Client)));
    ClientObject = (PZP_CLIENT_OBJECT)Client;
    TEST_OK(ZpClient_Start(Client) == STATUS_NOT_SUPPORTED &&
            ClientObject->State == ZpClientStateStopped);
    TEST_OK(NT_SUCCESS(ZpClient_SetTransport(Client,
                                             ZpTransportTlsTcp,
                                             &SDKTest_TransportOperations,
                                             &TestContext)));
    TEST_OK(NT_SUCCESS(ZpClient_Start(Client)) &&
            ClientObject->State == ZpClientStateConnecting &&
            TestContext.StartCount == 1 &&
            TestContext.ClientStateCount == 1 &&
            TestContext.ClientStates[0] == ZpClientStateConnecting);
    TEST_OK(ZpClient_Start(Client) == STATUS_INVALID_DEVICE_STATE);
    ClientObject->HighestInboundRequestId = 6;
    TEST_OK(NT_SUCCESS(ZpClient_NotifyState(Client,
                                            ZpClientStateAuthenticating,
                                            ZpStatus_FromNtStatus(STATUS_SUCCESS))) &&
            NT_SUCCESS(ZpClient_NotifyState(
                Client,
                ZpClientStateReady,
                ZpStatus_FromNtStatus(STATUS_SUCCESS))) &&
            ClientObject->HighestInboundRequestId == 0 &&
            TestContext.ClientStateCount == 3 &&
            TestContext.ClientStates[1] == ZpClientStateAuthenticating &&
            TestContext.ClientStates[2] == ZpClientStateReady);
    TEST_OK(ZpClient_NotifyState(Client,
                                 ZpClientStateAuthenticating,
                                 ZpStatus_FromNtStatus(STATUS_SUCCESS)) ==
                STATUS_INVALID_DEVICE_STATE);
    TEST_OK(NT_SUCCESS(ZpClient_Ping(Client, 0x0102030405060708)) &&
            TestContext.SendCount == 1 &&
            TestContext.SendMessageType == ZpMessagePing &&
            TestContext.SendToken == 0x0102030405060708);
    ClientObject->QuicTransport.Modules[0] = ClientObject->Config.Modules[0];
    ClientObject->QuicTransport.ModuleCount = 1;
    ClientObject->InboundRequestCount = ClientObject->Config.MaxRequestsPerConnection;
    TEST_OK(NT_SUCCESS(ZpClient_QueueRequest(Client, &InboundRequest)) &&
            ClientObject->HighestInboundRequestId == InboundRequest.RequestId &&
            TestContext.SendMessageType == ZpMessageResponse &&
            TestContext.SendRequestId == InboundRequest.RequestId &&
            SDK_STATUS_IS(TestContext.RequestStatus, STATUS_QUOTA_EXCEEDED));
    ClientObject->InboundRequestCount = 0;
    TEST_OK(ZpClient_QueueRequest(Client, &InboundRequest) ==
                STATUS_PROTOCOL_UNREACHABLE &&
            NT_SUCCESS(ZpClient_CancelInboundRequest(Client,
                                                     InboundRequest.RequestId)) &&
            ZpClient_CancelInboundRequest(Client,
                                          InboundRequest.RequestId + 1) ==
                STATUS_PROTOCOL_UNREACHABLE);
    RegistryConnection.Context = &TestContext;
    TEST_OK(NT_SUCCESS(ZpServerConnection_Initialize(
                           &RegistryConnection.Connection,
                           1,
                           1,
                           SDKTest_RequestConnectionSend,
                           SDKTest_RequestConnectionDestroy)));
    ZpServerConnection_SetModules(&RegistryConnection.Connection,
                                  ServerModules,
                                  ARRAYSIZE(ServerModules));
    ZpServerConnection_SetPhase(&RegistryConnection.Connection,
                                ZpConnectionPhaseReady);
    TEST_OK(NT_SUCCESS(ZpServer_SetRegistryValue(
                           (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                           ZpRegistryCurrentUser,
                           L"Software\\KNSoft",
                           15,
                           L"Value",
                           5,
                           4,
                           &RegistryValueData,
                           sizeof(RegistryValueData),
                           1000,
                           SDKTest_RequestStatusCallback,
                           &TestContext,
                           &Request)) &&
            TestContext.SendOperationId == ZP_REGISTRY_OPERATION_SET_VALUE);
    CanceledRequestId = TestContext.SendRequestId;
    TEST_OK(NT_SUCCESS(ZpRequest_Cancel(Request)) &&
            TestContext.SendMessageType == ZpMessageCancel &&
            TestContext.SendRequestId == CanceledRequestId &&
            TestContext.RequestStatusCount == 1 &&
            SDK_STATUS_IS(TestContext.RequestStatus, STATUS_CANCELLED) &&
            RegistryConnection.Connection.RequestCount == 0);
    Response.RequestId = CanceledRequestId;
    Response.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Response.Payload = EmptyPayload;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.RequestStatusCount == 1 &&
            ZpRequest_Cancel(Request) == STATUS_INVALID_DEVICE_STATE);
    Response.RequestId = RegistryConnection.Connection.NextRequestId;
    TEST_OK(ZpServerConnection_ReceiveResponse(&RegistryConnection.Connection,
                                               &Response) ==
            STATUS_PROTOCOL_UNREACHABLE);
    ZpRequest_Close(Request);
    TestContext.RequestStatusCount = 0;
    TEST_OK(NT_SUCCESS(ZpServer_ControlService(
                           (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                           ZP_SERVICE_CONTROL_STOP,
                           L"Test",
                           4,
                           NULL,
                           0,
                           1000,
                           SDKTest_RequestStatusCallback,
                           &TestContext,
                           &Request)) &&
            TestContext.SendModuleId == ZP_SERVICE_MODULE_ID &&
            TestContext.SendOperationId == ZP_SERVICE_OPERATION_CONTROL);
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.RequestStatusCount == 1 &&
            SDK_STATUS_IS(TestContext.RequestStatus, STATUS_SUCCESS));
    ZpRequest_Close(Request);
    TestContext.RequestStatusCount = 0;
    TEST_OK(NT_SUCCESS(ZpServer_EnumerateFilesPage(
                                                   (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                                                    L"C:\\Test",
                                                    7,
                                                    0,
                                                    1000,
                                                   SDKTest_FilePageCallback,
                                                   &TestContext,
                                                   &Request)) &&
            TestContext.SendModuleId == ZP_FILE_MODULE_ID &&
            TestContext.SendOperationId ==
                ZP_FILE_OPERATION_ENUMERATE_PAGE);
    TEST_OK(NT_SUCCESS(ZpFile_EncodePage(FilePageRecords,
                                         ARRAYSIZE(FilePageRecords),
                                         0,
                                         FilePageResponse,
                                         sizeof(FilePageResponse),
                                         &FilePageResponseLength)));
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Response.Payload.Buffer = FilePageResponse;
    Response.Payload.Length = FilePageResponseLength;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.FilePageCount == 1 &&
            SDK_STATUS_IS(TestContext.FilePageStatus, STATUS_SUCCESS) &&
            TestContext.FilePageFileCount == 1 &&
            TestContext.FilePageEnumerationId == 0);
    ZpRequest_Close(Request);
    TEST_OK(NT_SUCCESS(ZpServer_HashFile(
                                         (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                                         L"C:\\Test.bin",
                                         11,
                                         ZpFileHashSha256,
                                         1000,
                                         SDKTest_FileHashCallback,
                                         &TestContext,
                                         &Request)) &&
            TestContext.SendMessageType == ZpMessageRequest &&
            TestContext.SendModuleId == ZP_FILE_MODULE_ID &&
            TestContext.SendOperationId == ZP_FILE_OPERATION_HASH);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeHashResponse(ZpFileHashSha256,
                                                 sizeof(RootCertificate),
                                                 FileDigest,
                                                 sizeof(FileDigest),
                                                 FileHashResponse,
                                                 sizeof(FileHashResponse),
                                                 &FileHashResponseLength)));
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Response.Payload.Buffer = FileHashResponse;
    Response.Payload.Length = FileHashResponseLength;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.FileHashCount == 1 &&
            SDK_STATUS_IS(TestContext.FileHashStatus, STATUS_SUCCESS) &&
            TestContext.FileHashAlgorithm == ZpFileHashSha256 &&
            TestContext.FileHashSize == sizeof(RootCertificate) &&
            RtlCompareMemory(TestContext.FileDigest,
                             FileDigest,
                             sizeof(FileDigest)) == sizeof(FileDigest));
    ZpRequest_Close(Request);
    TEST_OK(NT_SUCCESS(ZpServer_EnumerateRegistryKeysPage(
                           (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                           ZpRegistryCurrentUser,
                           L"Software\\KNSoft",
                           15,
                           NULL,
                           0,
                           4,
                           1000,
                           SDKTest_RegistryPageCallback,
                           &TestContext,
                           &Request)) &&
            TestContext.SendModuleId == ZP_REGISTRY_MODULE_ID &&
            TestContext.SendOperationId ==
                ZP_REGISTRY_OPERATION_ENUMERATE_KEYS_PAGE);
    TEST_OK(NT_SUCCESS(ZpRegistry_EncodeKeyPage(
                           FALSE,
                           RegistryKeyRecords,
                           ARRAYSIZE(RegistryKeyRecords),
                           NULL,
                           0,
                           RegistryResponse,
                           sizeof(RegistryResponse),
                           &RegistryResponseLength)));
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Response.Payload.Buffer = RegistryResponse;
    Response.Payload.Length = RegistryResponseLength;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.RegistryPageCount == 1 &&
            SDK_STATUS_IS(TestContext.RegistryPageStatus, STATUS_SUCCESS) &&
            TestContext.RegistryRecordCount == 1);
    ZpRequest_Close(Request);
    TEST_OK(NT_SUCCESS(ZpServer_EnumerateRegistryValuesPage(
                           (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                           ZpRegistryCurrentUser,
                           L"Software\\KNSoft",
                           15,
                           L"",
                           0,
                           4,
                           1000,
                           SDKTest_RegistryPageCallback,
                           &TestContext,
                           &Request)) &&
            TestContext.SendOperationId ==
                ZP_REGISTRY_OPERATION_ENUMERATE_VALUES_PAGE);
    TEST_OK(NT_SUCCESS(ZpRegistry_EncodeValuePage(
                           FALSE,
                           RegistryValueRecords,
                           ARRAYSIZE(RegistryValueRecords),
                           NULL,
                           0,
                           RegistryResponse,
                           sizeof(RegistryResponse),
                           &RegistryResponseLength)));
    Response.RequestId = TestContext.SendRequestId;
    Response.Payload.Length = RegistryResponseLength;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.RegistryPageCount == 2 &&
            TestContext.RegistryRecordCount == 1);
    ZpRequest_Close(Request);
    TEST_OK(NT_SUCCESS(ZpServer_QueryRegistryValue(
                           (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                           ZpRegistryCurrentUser,
                           L"Software\\KNSoft",
                           15,
                           L"Value",
                           5,
                           1000,
                           SDKTest_RegistryValueCallback,
                           &TestContext,
                           &Request)) &&
            TestContext.SendOperationId == ZP_REGISTRY_OPERATION_QUERY_VALUE);
    TEST_OK(NT_SUCCESS(ZpRegistry_EncodeValue(4,
                                              &RegistryValueData,
                                              sizeof(RegistryValueData),
                                              RegistryResponse,
                                              sizeof(RegistryResponse),
                                              &RegistryResponseLength)));
    Response.RequestId = TestContext.SendRequestId;
    Response.Payload.Length = RegistryResponseLength;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.RegistryValueCount == 1 &&
            SDK_STATUS_IS(TestContext.RegistryValueStatus, STATUS_SUCCESS) &&
            TestContext.RegistryValueType == 4 &&
            TestContext.RegistryValueDataLength == sizeof(ULONG));
    ZpRequest_Close(Request);
    TEST_OK(NT_SUCCESS(ZpServer_SetRegistryValue(
                           (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                           ZpRegistryCurrentUser,
                           L"Software\\KNSoft",
                           15,
                           L"Value",
                           5,
                           4,
                           &RegistryValueData,
                           sizeof(RegistryValueData),
                           1000,
                           SDKTest_RequestStatusCallback,
                           &TestContext,
                           &Request)) &&
            TestContext.SendOperationId == ZP_REGISTRY_OPERATION_SET_VALUE);
    Response.RequestId = TestContext.SendRequestId;
    Response.Payload.Buffer = NULL;
    Response.Payload.Length = 0;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.RequestStatusCount == 1);
    ZpRequest_Close(Request);
    TEST_OK(NT_SUCCESS(ZpServer_DeleteRegistryValue(
                           (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                           ZpRegistryCurrentUser,
                           L"Software\\KNSoft",
                           15,
                           L"Value",
                           5,
                           1000,
                           SDKTest_RequestStatusCallback,
                           &TestContext,
                           &Request)) &&
            TestContext.SendOperationId == ZP_REGISTRY_OPERATION_DELETE_VALUE);
    Response.RequestId = TestContext.SendRequestId;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.RequestStatusCount == 2);
    ZpRequest_Close(Request);
    TEST_OK(NT_SUCCESS(ZpServer_CreateRegistryKey(
                           (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                           ZpRegistryCurrentUser,
                           L"Software\\KNSoft\\Child",
                           21,
                           1000,
                           SDKTest_RequestStatusCallback,
                           &TestContext,
                           &Request)) &&
            TestContext.SendOperationId == ZP_REGISTRY_OPERATION_CREATE_KEY);
    Response.RequestId = TestContext.SendRequestId;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.RequestStatusCount == 3);
    ZpRequest_Close(Request);
    TEST_OK(NT_SUCCESS(ZpServer_DeleteRegistryKey(
                           (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                           ZpRegistryCurrentUser,
                           L"Software\\KNSoft\\Child",
                           21,
                           1000,
                           SDKTest_RequestStatusCallback,
                           &TestContext,
                           &Request)) &&
            TestContext.SendOperationId == ZP_REGISTRY_OPERATION_DELETE_KEY);
    Response.RequestId = TestContext.SendRequestId;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.RequestStatusCount == 4);
    ZpRequest_Close(Request);
    TestContext.RequestStatusCount = 0;
    TEST_OK(NT_SUCCESS(ZpServer_QueryEventLogPage(
                           (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                           ZpEventLogStartOldest,
                           16,
                           EventChannel,
                           ARRAYSIZE(EventChannel) - 1,
                           NULL,
                           0,
                           NULL,
                           0,
                           1000,
                           SDKTest_EventLogPageCallback,
                           &TestContext,
                           &Request)) &&
            TestContext.SendModuleId == ZP_EVENT_LOG_MODULE_ID &&
            TestContext.SendOperationId ==
                ZP_EVENT_LOG_OPERATION_QUERY_PAGE);
    TEST_OK(NT_SUCCESS(ZpEventLog_EncodePage(
                           FALSE,
                           EventRecords,
                           ARRAYSIZE(EventRecords),
                           EventBookmark,
                           ARRAYSIZE(EventBookmark) - 1,
                           EventPageResponse,
                           sizeof(EventPageResponse),
                           &EventPageResponseLength)));
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Response.Payload.Buffer = EventPageResponse;
    Response.Payload.Length = EventPageResponseLength;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.EventPageCount == 1 &&
            SDK_STATUS_IS(TestContext.EventPageStatus, STATUS_SUCCESS) &&
            TestContext.EventPageRecordCount == 1 &&
            !TestContext.EventPageHasMore);
    ZpRequest_Close(Request);
    TestContext.RequestStatusCount = 0;
    TEST_OK(NT_SUCCESS(ZpServer_SetEventLogChannelEnabled(
                           (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                           EventChannel,
                           ARRAYSIZE(EventChannel) - 1,
                           TRUE,
                           1000,
                           SDKTest_RequestStatusCallback,
                           &TestContext,
                           &Request)) &&
            TestContext.SendModuleId == ZP_EVENT_LOG_MODULE_ID &&
            TestContext.SendOperationId ==
                ZP_EVENT_LOG_OPERATION_SET_CHANNEL_ENABLED);
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Response.Payload.Buffer = NULL;
    Response.Payload.Length = 0;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
        &RegistryConnection.Connection,
        &Response)));
    TEST_OK(TestContext.RequestStatusCount == 1 &&
            SDK_STATUS_IS(TestContext.RequestStatus, STATUS_SUCCESS));
    ZpRequest_Close(Request);
    TEST_OK(NT_SUCCESS(ZpServer_ClearEventLog(
                           (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                           EventChannel,
                           ARRAYSIZE(EventChannel) - 1,
                           1000,
                           SDKTest_RequestStatusCallback,
                           &TestContext,
                           &Request)) &&
            TestContext.SendOperationId == ZP_EVENT_LOG_OPERATION_CLEAR);
    Response.RequestId = TestContext.SendRequestId;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.RequestStatusCount == 2);
    ZpRequest_Close(Request);
    TestContext.RequestStatusCount = 0;
    TEST_OK(NT_SUCCESS(ZpServer_OpenFileRead(
                                             (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                                             L"C:\\Test.bin",
                                             11,
                                             16,
                                             1000,
                                             SDKTest_FileOpenReadCallback,
                                             SDKTest_ChannelDataCallback,
                                             SDKTest_ChannelCloseCallback,
                                             &TestContext,
                                             &Request)) &&
            TestContext.SendMessageType == ZpMessageRequest);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeOpenReadResponse(1,
                                                     16 + sizeof(RootCertificate),
                                                     16,
                                                     FileOpenReadResponse,
                                                     sizeof(FileOpenReadResponse),
                                                     &FileOpenReadResponseLength)));
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Response.Payload.Buffer = FileOpenReadResponse;
    Response.Payload.Length = FileOpenReadResponseLength;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.FileOpenReadCount == 1 &&
            SDK_STATUS_IS(TestContext.FileOpenReadStatus, STATUS_SUCCESS) &&
            TestContext.FileChannel != NULL &&
            TestContext.FileSize == 16 + sizeof(RootCertificate) &&
            TestContext.FileOffset == 16 &&
            TestContext.SendMessageType == ZpMessageChannelWindow &&
            TestContext.SendChannelId == 1 &&
            TestContext.SendChannelCredit == sizeof(RootCertificate));
    ZpRequest_Close(Request);
    ChannelData.ChannelId = 1;
    ChannelData.Data.Buffer = RootCertificate;
    ChannelData.Data.Length = sizeof(RootCertificate);
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveChannelData(
                           &RegistryConnection.Connection,
                           &ChannelData)) &&
            TestContext.ChannelDataCount == 1 &&
            TestContext.ChannelDataLength == sizeof(RootCertificate));
    ChannelClose.ChannelId = 1;
    ChannelClose.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveChannelClose(
                           &RegistryConnection.Connection,
                           &ChannelClose)) &&
            TestContext.ChannelCloseCount == 1 &&
            SDK_STATUS_IS(TestContext.ChannelCloseStatus, STATUS_SUCCESS));
    ZpChannel_Close(TestContext.FileChannel);
    TestContext.FileChannel = NULL;
    TEST_OK(NT_SUCCESS(ZpServer_OpenFileRead(
                                             (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                                             L"C:\\Test.bin",
                                             11,
                                             0,
                                             1000,
                                             SDKTest_FileOpenReadCallback,
                                             SDKTest_ChannelDataCallback,
                                             SDKTest_ChannelCloseCallback,
                                             &TestContext,
                                             &Request)));
    TEST_OK(NT_SUCCESS(ZpFile_EncodeOpenReadResponse(2,
                                                     64,
                                                     0,
                                                     FileOpenReadResponse,
                                                     sizeof(FileOpenReadResponse),
                                                     &FileOpenReadResponseLength)));
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Response.Payload.Buffer = FileOpenReadResponse;
    Response.Payload.Length = FileOpenReadResponseLength;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.FileOpenReadCount == 2 &&
            TestContext.FileChannel != NULL &&
            TestContext.SendMessageType == ZpMessageChannelWindow &&
            TestContext.SendChannelId == 2);
    ZpRequest_Close(Request);
    TEST_OK(NT_SUCCESS(ZpChannel_Cancel(TestContext.FileChannel)) &&
            TestContext.SendMessageType == ZpMessageChannelClose &&
            TestContext.SendChannelId == 2 &&
            SDK_STATUS_IS(TestContext.SendChannelStatus, STATUS_CANCELLED) &&
            TestContext.ChannelCloseCount == 2 &&
            SDK_STATUS_IS(TestContext.ChannelCloseStatus, STATUS_CANCELLED));
    TEST_OK(ZpChannel_Cancel(TestContext.FileChannel) == STATUS_INVALID_DEVICE_STATE);
    ChannelClose.ChannelId = 2;
    ChannelClose.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveChannelClose(
                           &RegistryConnection.Connection,
                           &ChannelClose)) &&
            TestContext.ChannelCloseCount == 2);
    ZpChannel_Close(TestContext.FileChannel);
    TestContext.FileChannel = NULL;
    TEST_OK(NT_SUCCESS(ZpServer_CreateTerminal(
                                                (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                                                120,
                                                30,
                                                L"cmd.exe",
                                                7,
                                                NULL,
                                                0,
                                                1000,
                                                SDKTest_TerminalCreateCallback,
                                                SDKTest_ChannelDataCallback,
                                                SDKTest_ChannelWritableCallback,
                                                SDKTest_ChannelCloseCallback,
                                                &TestContext,
                                                &Request)) &&
            TestContext.SendMessageType == ZpMessageRequest &&
            TestContext.SendModuleId == ZP_TERMINAL_MODULE_ID &&
            TestContext.SendOperationId == ZP_TERMINAL_OPERATION_CREATE &&
            TestContext.SendPayloadLength != 0);
    TEST_OK(NT_SUCCESS(ZpTerminal_EncodeCreateResponse(
                           3,
                           1234,
                           TerminalCreateResponse,
                           sizeof(TerminalCreateResponse),
                           &TerminalCreateResponseLength)));
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Response.Payload.Buffer = TerminalCreateResponse;
    Response.Payload.Length = TerminalCreateResponseLength;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.TerminalCreateCount == 1 &&
            SDK_STATUS_IS(TestContext.TerminalCreateStatus, STATUS_SUCCESS) &&
            TestContext.TerminalChannel != NULL &&
            TestContext.TerminalProcessId == 1234 &&
            TestContext.SendMessageType == ZpMessageChannelWindow &&
            TestContext.SendChannelId == 3 &&
            TestContext.SendChannelCredit ==
                ZP_CLIENT_DEFAULT_CHANNEL_WINDOW_SIZE);
    ZpRequest_Close(Request);
    TEST_OK(ZpChannel_Send(TestContext.TerminalChannel,
                           TerminalInput,
                           sizeof(TerminalInput)) == STATUS_RETRY);
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveChannelWindow(
                           &RegistryConnection.Connection,
                           3,
                           sizeof(TerminalInput))) &&
            TestContext.ChannelWritableCount == 1 &&
            TestContext.ChannelWritableCredit == sizeof(TerminalInput));
    TEST_OK(ZpChannel_Send(TestContext.TerminalChannel,
                           TerminalTooLongInput,
                           sizeof(TerminalTooLongInput)) == STATUS_RETRY);
    TEST_OK(NT_SUCCESS(ZpChannel_Send(TestContext.TerminalChannel,
                                     TerminalInput,
                                     sizeof(TerminalInput))) &&
            TestContext.SendMessageType == ZpMessageChannelData &&
            TestContext.SendChannelId == 3 &&
            TestContext.SendChannelDataLength == sizeof(TerminalInput));
    ChannelData.ChannelId = 3;
    ChannelData.Data.Buffer = TerminalInput;
    ChannelData.Data.Length = sizeof(TerminalInput);
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveChannelData(
                           &RegistryConnection.Connection,
                           &ChannelData)) &&
            TestContext.ChannelDataCount == 2 &&
            TestContext.ChannelDataLength == sizeof(TerminalInput) &&
            TestContext.SendMessageType == ZpMessageChannelWindow &&
            TestContext.SendChannelId == 3 &&
            TestContext.SendChannelCredit == sizeof(TerminalInput));
    TEST_OK(NT_SUCCESS(ZpServer_ResizeTerminal(
                                               (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                                               TestContext.TerminalChannel,
                                               132,
                                               40,
                                               1000,
                                               SDKTest_RequestStatusCallback,
                                               &TestContext,
                                               &Request)) &&
            TestContext.SendMessageType == ZpMessageRequest &&
            TestContext.SendModuleId == ZP_TERMINAL_MODULE_ID &&
            TestContext.SendOperationId == ZP_TERMINAL_OPERATION_RESIZE);
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Response.Payload = EmptyPayload;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.RequestStatusCount == 1 &&
            SDK_STATUS_IS(TestContext.RequestStatus, STATUS_SUCCESS));
    ZpRequest_Close(Request);
    ChannelClose.ChannelId = 3;
    ChannelClose.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveChannelClose(
                           &RegistryConnection.Connection,
                           &ChannelClose)) &&
            TestContext.ChannelCloseCount == 3 &&
            SDK_STATUS_IS(TestContext.ChannelCloseStatus, STATUS_SUCCESS));
    ZpChannel_Close(TestContext.TerminalChannel);
    TestContext.TerminalChannel = NULL;
    TEST_OK(NT_SUCCESS(ZpServer_OpenFileWrite(
                                               (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection,
                                               L"C:\\Upload.bin",
                                               13,
                                               sizeof(FileWriteData),
                                               ZpFileCreateAlways,
                                               1000,
                                               SDKTest_FileOpenWriteCallback,
                                               SDKTest_ChannelWritableCallback,
                                               SDKTest_ChannelCloseCallback,
                                               &TestContext,
                                               &Request)) &&
            TestContext.SendMessageType == ZpMessageRequest &&
            TestContext.SendModuleId == ZP_FILE_MODULE_ID &&
            TestContext.SendOperationId == ZP_FILE_OPERATION_OPEN_WRITE);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeOpenWriteResponse(
                           4,
                           sizeof(FileWriteData),
                           FileOpenWriteResponse,
                           sizeof(FileOpenWriteResponse),
                           &FileOpenWriteResponseLength)));
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Response.Payload.Buffer = FileOpenWriteResponse;
    Response.Payload.Length = FileOpenWriteResponseLength;
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveResponse(
                           &RegistryConnection.Connection,
                           &Response)) &&
            TestContext.FileOpenWriteCount == 1 &&
            SDK_STATUS_IS(TestContext.FileOpenWriteStatus, STATUS_SUCCESS) &&
            TestContext.FileWriteChannel != NULL &&
            TestContext.FileWriteSize == sizeof(FileWriteData));
    ZpRequest_Close(Request);
    ChannelClose.ChannelId = 4;
    ChannelClose.Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    TEST_OK(ZpServerConnection_ReceiveChannelClose(
                &RegistryConnection.Connection,
                &ChannelClose) ==
            STATUS_PROTOCOL_UNREACHABLE);
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveChannelWindow(
                           &RegistryConnection.Connection,
                           4,
                           sizeof(FileWriteData))) &&
            ZpChannel_Send(TestContext.FileWriteChannel,
                           FileWriteTooLongData,
                           sizeof(FileWriteTooLongData)) == STATUS_RETRY &&
            NT_SUCCESS(ZpChannel_Send(TestContext.FileWriteChannel,
                                      FileWriteData,
                                      sizeof(FileWriteData))) &&
            TestContext.SendChannelId == 4 &&
            TestContext.SendChannelDataLength == sizeof(FileWriteData));
    TEST_OK(NT_SUCCESS(ZpServerConnection_ReceiveChannelClose(
                           &RegistryConnection.Connection,
                           &ChannelClose)) &&
            TestContext.ChannelCloseCount == 4 &&
            SDK_STATUS_IS(TestContext.ChannelCloseStatus, STATUS_SUCCESS));
    ZpChannel_Close(TestContext.FileWriteChannel);
    TestContext.FileWriteChannel = NULL;
    ZpServerConnection_Close(&RegistryConnection.Connection,
                              ZpStatus_FromNtStatus(
                                  STATUS_CONNECTION_DISCONNECTED));
    ZpConnection_Release(
        (ZP_CONNECTION_HANDLE)&RegistryConnection.Connection);
    TEST_OK(RegistryConnection.DestroyCount == 1);
    TEST_OK(NT_SUCCESS(ZpClient_Stop(Client)) &&
            ClientObject->State == ZpClientStateStopping &&
            TestContext.StopCount == 1 &&
            TestContext.ClientStates[3] == ZpClientStateStopping);
    TEST_OK(NT_SUCCESS(ZpClient_Stop(Client)) && TestContext.StopCount == 1);
    TEST_OK(ZpClient_Close(Client) == STATUS_DEVICE_BUSY);
    TestContext.CloseClientOnStopped = TRUE;
    TEST_OK(NT_SUCCESS(ZpClient_NotifyState(
                Client,
                ZpClientStateStopped,
                ZpStatus_FromNtStatus(STATUS_SUCCESS))) &&
            TestContext.ClientStates[4] == ZpClientStateStopped &&
            TestContext.ClientCloseStatus == STATUS_DEVICE_BUSY);
    TEST_OK(NT_SUCCESS(ZpClient_Close(Client)));

    RtlZeroMemory(&TestContext, sizeof(TestContext));
    TestContext.StartStatus = ZpStatus_FromNtStatus(STATUS_ACCESS_DENIED);
    ClientConfig.CallbackContext = &TestContext;
    TEST_OK(NT_SUCCESS(ZpClient_Create(&ClientConfig, &Client)));
    TEST_OK(NT_SUCCESS(ZpClient_SetTransport(Client,
                                             ZpTransportTlsTcp,
                                             &SDKTest_TransportOperations,
                                             &TestContext)));
    ClientObject = (PZP_CLIENT_OBJECT)Client;
    TEST_OK(NT_SUCCESS(ZpClient_Start(Client)) &&
            ClientObject->State == ZpClientStateRetryWait &&
            ClientObject->FailureRound == 1 &&
            ClientObject->RetryPending &&
            TestContext.ClientStateCount == 2 &&
            TestContext.ClientStates[1] == ZpClientStateRetryWait &&
            SDK_STATUS_IS(TestContext.ClientStatuses[1], STATUS_ACCESS_DENIED));
#ifdef _DEBUG
    TEST_OK(ClientObject->RetryDelay == 5000);
#endif
    TEST_OK(NT_SUCCESS(ZpClient_Stop(Client)) && TestContext.StopCount == 1);
    TEST_OK(NT_SUCCESS(ZpClient_NotifyState(Client,
                                            ZpClientStateStopped,
                                            ZpStatus_FromNtStatus(
                                                STATUS_SUCCESS))));
    TEST_OK(NT_SUCCESS(ZpClient_Close(Client)));

    RtlZeroMemory(&TestContext, sizeof(TestContext));
    ServerConfig.CallbackContext = &TestContext;
    TEST_OK(NT_SUCCESS(ZpServer_Create(&ServerConfig, &Server)));
    ServerObject = (PZP_SERVER_OBJECT)Server;
    ServerObject->Config.DeploymentCount = 1;
    TEST_OK(SDK_STATUS_IS(ZpServer_Start(Server), STATUS_NOT_SUPPORTED) &&
            ServerObject->State == ZpServerStateStopped);
    TEST_OK(NT_SUCCESS(ZpServer_SetTransport(Server, &SDKTest_TransportOperations, &TestContext)));
    TEST_OK(ZpStatus_IsSuccess(ZpServer_Start(Server)) &&
            ServerObject->State == ZpServerStateStarting &&
            TestContext.StartCount == 1 &&
            TestContext.ServerStateCount == 1 &&
            TestContext.ServerStates[0] == ZpServerStateStarting);
    TEST_OK(NT_SUCCESS(ZpServer_NotifyState(
                Server,
                ZpServerStateRunning,
                ZpStatus_FromNtStatus(STATUS_SUCCESS))) &&
            TestContext.ServerStates[1] == ZpServerStateRunning);
    TEST_OK(NT_SUCCESS(ZpServer_Stop(Server)) &&
            ServerObject->State == ZpServerStateStopping &&
            TestContext.StopCount == 1 &&
            TestContext.ServerStates[2] == ZpServerStateStopping);
    TEST_OK(NT_SUCCESS(ZpServer_Stop(Server)) && TestContext.StopCount == 1);
    TestContext.CloseServerOnStopped = TRUE;
    TEST_OK(NT_SUCCESS(ZpServer_NotifyState(
                Server,
                ZpServerStateStopped,
                ZpStatus_FromNtStatus(STATUS_SUCCESS))) &&
            TestContext.ServerStates[3] == ZpServerStateStopped &&
            TestContext.ServerCloseStatus == STATUS_DEVICE_BUSY);
    ServerObject->Config.DeploymentCount = 0;
    TEST_OK(NT_SUCCESS(ZpServer_Close(Server)));
}
