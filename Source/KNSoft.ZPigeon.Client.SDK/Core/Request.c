#include "../Client.inl"
#include "Channel.h"
#include "../../Modules/Administration/Client.h"
#include "../../Modules/Audio/Client.h"
#include "../../Modules/Browser/Client.h"
#include "../../Modules/Wmi/Client.h"
#include "../../Modules/EventLog/Client.h"
#include "../../Modules/Execution/Client.h"
#include "../../Modules/File/Client.h"
#include "../../Modules/PortableDevice/Client.h"
#include "../../Modules/Process/Client.h"
#include "../../Modules/Registry/Client.h"
#include "../../Modules/Service/Client.h"
#include "../../Modules/System/Client.h"
#include "../../Modules/Terminal/Client.h"
#include "../../Modules/Tunnel/Client.h"
#include "../../Modules/Window/Client.h"
#include "../../Modules/Video/Client.h"
#include "../../Modules/Rtc/Client.h"
#include "../../Modules/Serial/Client.h"
#include "../../Modules/Recording/Client.h"

typedef struct _ZP_CLIENT_INBOUND_REQUEST
{
    LIST_ENTRY ListEntry;
    LIST_ENTRY BucketEntry;
    PZP_CLIENT_OBJECT Owner;
    volatile LONG ReferenceCount;
    volatile LONG Pending;
    ULONG RequestId;
    BYTE ModuleId;
    BYTE OperationId;
    ULONG TimeoutMilliseconds;
    ULONGLONG ReceivedTickCount;
    ULONG PayloadLength;
    BYTE Payload[ANYSIZE_ARRAY];
} ZP_CLIENT_INBOUND_REQUEST, *PZP_CLIENT_INBOUND_REQUEST;

static
LOGICAL
ZpClientInbound_IsSensitiveRequest(
    _In_ BYTE ModuleId,
    _In_ BYTE OperationId)
{
    return (ModuleId == ZP_USER_MODULE_ID && OperationId == ZP_ADMINISTRATION_OPERATION_CONTROL_USER) ||
           (ModuleId == ZP_WLAN_MODULE_ID && OperationId == ZP_ADMINISTRATION_OPERATION_CONTROL_WLAN) ||
           (ModuleId == ZP_CERTIFICATE_MODULE_ID &&
             OperationId == ZP_ADMINISTRATION_OPERATION_CONTROL_CERTIFICATE_DATA) ||
           (ModuleId == ZP_CREDENTIAL_MODULE_ID &&
             OperationId == ZP_ADMINISTRATION_OPERATION_CONTROL_CREDENTIAL) ||
           (ModuleId == ZP_CLIPBOARD_MODULE_ID &&
             OperationId == ZP_ADMINISTRATION_OPERATION_CONTROL_CLIPBOARD) ||
           (ModuleId == ZP_FIRMWARE_MODULE_ID &&
             OperationId == ZP_ADMINISTRATION_OPERATION_CONTROL_FIRMWARE_DATA) ||
           (ModuleId == ZP_EXECUTION_MODULE_ID && OperationId == ZP_EXECUTION_OPERATION_START) ||
           (ModuleId == ZP_SERVICE_MODULE_ID && OperationId == ZP_SERVICE_OPERATION_CONFIGURE_ACCOUNT);
}

static
LOGICAL
ZpClientInbound_IsSensitiveResponse(
    _In_ BYTE ModuleId,
    _In_ BYTE OperationId)
{
    return (ModuleId == ZP_CREDENTIAL_MODULE_ID &&
            OperationId == ZP_ADMINISTRATION_OPERATION_QUERY_CREDENTIAL) ||
           (ModuleId == ZP_WLAN_MODULE_ID &&
             OperationId == ZP_ADMINISTRATION_OPERATION_QUERY_WLAN_PROFILE) ||
           (ModuleId == ZP_CERTIFICATE_MODULE_ID &&
             OperationId == ZP_ADMINISTRATION_OPERATION_QUERY_CERTIFICATE_DATA) ||
           (ModuleId == ZP_CLIPBOARD_MODULE_ID &&
             (OperationId == ZP_ADMINISTRATION_OPERATION_ENUMERATE_CLIPBOARD ||
              OperationId == ZP_ADMINISTRATION_OPERATION_QUERY_CLIPBOARD_IMAGE)) ||
           (ModuleId == ZP_FIRMWARE_MODULE_ID &&
             OperationId == ZP_ADMINISTRATION_OPERATION_QUERY_FIRMWARE_DATA);
}

static
PZP_CLIENT_INBOUND_REQUEST
ZpClientInbound_FindRequestLocked(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ULONG RequestId)
{
    PLIST_ENTRY Bucket = &Object->InboundRequestBuckets[RequestId & (ZP_CLIENT_LOOKUP_BUCKET_COUNT - 1)];
    PLIST_ENTRY Entry;
    PZP_CLIENT_INBOUND_REQUEST Request;

    for (Entry = Bucket->Flink; Entry != Bucket; Entry = Entry->Flink)
    {
        Request = CONTAINING_RECORD(Entry, ZP_CLIENT_INBOUND_REQUEST, BucketEntry);
        if (Request->RequestId == RequestId) return Request;
    }
    return NULL;
}

static
VOID
ZpClientInbound_RemoveRequestLocked(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _Inout_ PZP_CLIENT_INBOUND_REQUEST Request)
{
    RemoveEntryList(&Request->ListEntry);
    RemoveEntryList(&Request->BucketEntry);
    Object->InboundRequestCount--;
    Object->InboundRequestPayloadBytes -= Request->PayloadLength;
}

static
VOID
ZpClientInbound_ReleaseRequest(
    _Inout_ PZP_CLIENT_INBOUND_REQUEST Request)
{
    if (InterlockedDecrement(&Request->ReferenceCount) == 0)
    {
        if (ZpClientInbound_IsSensitiveRequest(Request->ModuleId, Request->OperationId))
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
    _In_ ZP_SEND_FLAGS SendFlags,
    _In_ ULONG RequestId,
    _In_ ZP_STATUS ResponseStatus,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength)
{
    BYTE Header[ZP_RESPONSE_MAX_HEADER_WIRE_SIZE];
    ZP_RESPONSE Response = {
        RequestId,
        ResponseStatus,
        Payload,
        PayloadLength
    };
    ULONG HeaderLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeResponseHeader(&Response, Header, &HeaderLength);
    if (NT_SUCCESS(Status))
    {
        RtlAcquireSRWLockShared(&Object->Lock);
        Status = ZpClient_SendLocked(Object,
                                     SendFlags,
                                     ZpMessageResponse,
                                     Header,
                                     HeaderLength,
                                     Payload,
                                     PayloadLength);
        RtlReleaseSRWLockShared(&Object->Lock);
    }
    return Status;
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
    PZP_CLIENT_LOCAL_CHANNEL Channel = NULL;
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
             Request->OperationId == ZP_SYSTEM_OPERATION_PROBE)
    {
        Status = Request->PayloadLength == 0 ?
                     ZpStatus_FromNtStatus(STATUS_SUCCESS) :
                     ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
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
                                      &Channel);
        Status = ZpStatus_FromNtStatus(ModuleStatus);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_PORTABLE_DEVICE_MODULE_ID)
    {
        Status = ZpPortable_Execute(Object,
                                    Request->OperationId,
                                    Request->Payload,
                                    Request->PayloadLength,
                                    &Request->Pending,
                                    &AllocatedResponse,
                                    &PayloadLength,
                                    &Channel);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_PROCESS_MODULE_ID)
    {
        Status = ZpProcess_Execute(Object,
                                   Request->OperationId,
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
                                    &Channel);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_WINDOW_MODULE_ID)
    {
        Status = ZpWindow_Execute(Object,
                                  Request->OperationId,
                                  Request->Payload,
                                  Request->PayloadLength,
                                  &AllocatedResponse,
                                  &PayloadLength,
                                  &Channel);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_AUDIO_MODULE_ID)
    {
        Status = ZpAudio_Execute(Object,
                                 Request->OperationId,
                                 Request->Payload,
                                 Request->PayloadLength,
                                 &AllocatedResponse,
                                 &PayloadLength,
                                 &Channel);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_VIDEO_MODULE_ID)
    {
        Status = ZpVideo_Execute(Object,
                                 Request->OperationId,
                                 Request->Payload,
                                 Request->PayloadLength,
                                 &AllocatedResponse,
                                 &PayloadLength,
                                 &Channel);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_RTC_MODULE_ID)
    {
        Status = ZpRtc_Execute(Object,
                               Request->OperationId,
                               Request->Payload,
                               Request->PayloadLength,
                               &AllocatedResponse,
                               &PayloadLength);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_SERIAL_MODULE_ID)
    {
        Status = ZpSerial_Execute(Object,
                                  Request->OperationId,
                                  Request->Payload,
                                  Request->PayloadLength,
                                  &AllocatedResponse,
                                  &PayloadLength,
                                  &Channel);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_TUNNEL_MODULE_ID)
    {
        Status = ZpTunnel_Execute(Object,
                                  Request->OperationId,
                                  Request->Payload,
                                  Request->PayloadLength,
                                  Request->TimeoutMilliseconds,
                                  &AllocatedResponse,
                                  &PayloadLength,
                                  &Channel);
        Response = AllocatedResponse;
    }
    else if (FlagOn(ZP_MANAGEMENT_MODULE_MASK, ZP_MODULE_BIT(Request->ModuleId)))
    {
        Status = ZpAdministration_Execute(Request->ModuleId,
                                          Request->OperationId,
                                          Request->Payload,
                                          Request->PayloadLength,
                                          &AllocatedResponse,
                                          &PayloadLength);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_BROWSER_MODULE_ID)
    {
        Status = ZpBrowser_Execute(Object,
                                   Request->OperationId,
                                   Request->Payload,
                                   Request->PayloadLength,
                                   &AllocatedResponse,
                                   &PayloadLength);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_WMI_MODULE_ID)
    {
        Status = ZpWmi_Execute(Request->OperationId,
                               Request->Payload,
                               Request->PayloadLength,
                               &AllocatedResponse,
                               &PayloadLength);
        Response = AllocatedResponse;
    }
    else if (Request->ModuleId == ZP_RECORDING_MODULE_ID)
    {
        Status = ZpRecording_Execute(Object,
                                     Request->OperationId,
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
        ZpClientInbound_RemoveRequestLocked(Object, Request);
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Respond)
    {
        SendStatus = ZpClientInbound_SendResponse(
            Object,
            ZpClientInbound_IsSensitiveResponse(Request->ModuleId, Request->OperationId) ?
                ZP_SEND_FLAG_SENSITIVE : ZP_SEND_FLAG_COMPRESSIBLE,
            Request->RequestId,
            Status,
            ZpStatus_IsSuccess(Status) ? Response : NULL,
            ZpStatus_IsSuccess(Status) ? PayloadLength : 0);
        ZpClientInbound_ReleaseRequest(Request);
    }
    if (Channel != NULL)
    {
        Channel->Commit(Channel, Respond && NT_SUCCESS(SendStatus));
    }
    if (AllocatedResponse != NULL)
    {
        if (ZpClientInbound_IsSensitiveResponse(Request->ModuleId, Request->OperationId))
        {
            RtlSecureZeroMemory(AllocatedResponse, PayloadLength);
        }
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

    if (Request->RequestId == 0 || Request->ModuleId == 0)
    {
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
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
                                            0,
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
                                            0,
                                            Request->RequestId,
                                            ZpStatus_FromNtStatus(
                                                STATUS_NO_MEMORY),
                                            NULL,
                                            0);
    }
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
    InsertTailList(&Object->InboundRequestBuckets[
                       RequestObject->RequestId & (ZP_CLIENT_LOOKUP_BUCKET_COUNT - 1)],
                   &RequestObject->BucketEntry);
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
        ZpClientInbound_RemoveRequestLocked(Object, RequestObject);
        Object->CallbackCount--;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        if (ZpClientInbound_IsSensitiveRequest(RequestObject->ModuleId, RequestObject->OperationId))
        {
            RtlSecureZeroMemory(RequestObject->Payload, RequestObject->PayloadLength);
        }
        Mem_Free(RequestObject);
        return ZpClientInbound_SendResponse(Object,
                                            0,
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
    _In_ ULONG RequestId)
{
    PZP_CLIENT_OBJECT Object = (PZP_CLIENT_OBJECT)Client;
    PZP_CLIENT_INBOUND_REQUEST Request;

    if (RequestId == 0)
    {
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Request = ZpClientInbound_FindRequestLocked(Object, RequestId);
    if (Request == NULL)
    {
        NTSTATUS Status = RequestId <= Object->HighestInboundRequestId ?
                              STATUS_SUCCESS : STATUS_PROTOCOL_UNREACHABLE;

        RtlReleaseSRWLockExclusive(&Object->Lock);
        return Status;
    }
    InterlockedExchange(&Request->Pending, FALSE);
    ZpClientInbound_RemoveRequestLocked(Object, Request);
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
        ZpClientInbound_RemoveRequestLocked(Object, Request);
        ZpClientInbound_ReleaseRequest(Request);
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
}
