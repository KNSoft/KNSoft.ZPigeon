#include "Client.h"
#define COBJMACROS

#include "Capture.h"
#include "Shared.h"
#include "../Rtc/Client.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#define ZP_VIDEO_CHANNEL_CHUNK_SIZE 0x00040000UL

struct _ZP_CLIENT_VIDEO_CHANNEL
{
    ZP_CLIENT_LOCAL_CHANNEL Header;
    LOGICAL WorkerActive;
    ULONGLONG Credit;
    HANDLE WorkerThread;
    HANDLE CreditEvent;
    HANDLE StopEvent;
    RTL_SRWLOCK SettingsLock;
    ZP_VIDEO_FORMAT Format;
    ULONG SettingsVersion;
    ULONG DirectStreamId;
    USHORT Quality;
    ULONG DeviceIdLength;
    WCHAR DeviceId[ANYSIZE_ARRAY];
};

static
NTSTATUS
ZpVideo_SendLocked(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PCZP_TRANSPORT_OPERATIONS Operations = Object->TransportOperations[Object->ActiveTransport];

    return Object->State == ZpClientStateReady && Operations->Send != NULL ?
               Operations->Send(Object->TransportContexts[Object->ActiveTransport], MessageType, Body, BodyLength) :
               STATUS_CONNECTION_DISCONNECTED;
}

static
NTSTATUS
ZpVideo_SendCloseLocked(
    _Inout_ PZP_CLIENT_VIDEO_CHANNEL Channel,
    _In_ ZP_STATUS CloseStatus)
{
    BYTE Body[sizeof(ULONG) + ZP_STATUS_WIRE_SIZE];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelClose(Channel->Header.ChannelId,
                                          CloseStatus,
                                          Body,
                                          sizeof(Body),
                                          &BodyLength);
    return NT_SUCCESS(Status) ? ZpVideo_SendLocked(Channel->Header.Owner,
                                                   ZpMessageChannelClose,
                                                   Body,
                                                   BodyLength) : Status;
}

static
NTSTATUS
ZpVideo_SendBytes(
    _Inout_ PZP_CLIENT_VIDEO_CHANNEL Channel,
    _In_reads_bytes_(Length) const VOID* Data,
    _In_ ULONG Length)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    PBYTE Body;
    ULONG Offset = 0, ChunkLength, BodyLength;
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL Pending, Removed;

    if (Channel->DirectStreamId != 0)
    {
        return ZpRtc_Send(Object, Channel->DirectStreamId, Data, Length);
    }
    Body = Mem_Alloc(sizeof(ULONG) + ZP_VIDEO_CHANNEL_CHUNK_SIZE);
    if (Body == NULL) return STATUS_NO_MEMORY;
    while (Offset < Length)
    {
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Pending = Channel->Header.Pending;
        if (Pending && Channel->Credit == 0)
        {
            NtClearEvent(Channel->CreditEvent);
            RtlReleaseSRWLockExclusive(&Object->Lock);
            Status = NtWaitForSingleObject(Channel->CreditEvent, FALSE, NULL);
            if (!NT_SUCCESS(Status)) break;
            continue;
        }
        if (!Pending)
        {
            RtlReleaseSRWLockExclusive(&Object->Lock);
            Status = STATUS_CANCELLED;
            break;
        }
        ChunkLength = min(Length - Offset, (ULONG)min(Channel->Credit, ZP_VIDEO_CHANNEL_CHUNK_SIZE));
        Channel->Credit -= ChunkLength;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Status = ZpMessage_EncodeChannelData(Channel->Header.ChannelId,
                                             Add2Ptr(Data, Offset),
                                             ChunkLength,
                                             Body,
                                             sizeof(ULONG) + ZP_VIDEO_CHANNEL_CHUNK_SIZE,
                                             &BodyLength);
        if (!NT_SUCCESS(Status)) break;
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Pending = Channel->Header.Pending;
        Status = Pending ? ZpVideo_SendLocked(Object, ZpMessageChannelData, Body, BodyLength) : STATUS_CANCELLED;
        Removed = !NT_SUCCESS(Status) && Pending ? ZpClientLocalChannel_RemoveLocked(&Channel->Header) : FALSE;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
        if (!NT_SUCCESS(Status)) break;
        Offset += ChunkLength;
    }
    Mem_Free(Body);
    return Status;
}

static
VOID
ZpVideo_FinishWorker(
    _Inout_ PZP_CLIENT_VIDEO_CHANNEL Channel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    if (Removed) ZpVideo_SendCloseLocked(Channel, Status);
    Channel->WorkerActive = FALSE;
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
    ZpClientLocalChannel_Release(&Channel->Header);
}

static
NTSTATUS
ZpVideo_SendFrame(
    _Inout_ PZP_CLIENT_VIDEO_CHANNEL Channel,
    _In_ PZP_VIDEO_IMAGE Image)
{
    BYTE Header[sizeof(ULONG) * 3];
    ULONG HeaderLength;
    NTSTATUS Status;

    Status = ZpVideo_EncodeFrame(&Image->Frame, Header, sizeof(Header), &HeaderLength);
    if (NT_SUCCESS(Status)) Status = ZpVideo_SendBytes(Channel, Header, HeaderLength);
    if (NT_SUCCESS(Status)) Status = ZpVideo_SendBytes(Channel, Image->Data, Image->Frame.DataLength);
    return Status;
}

static
NTSTATUS
NTAPI
ZpVideo_Worker(
    _In_ PVOID Context)
{
    PZP_CLIENT_VIDEO_CHANNEL Channel = Context;
    ZP_VIDEO_STREAM_REQUEST_VIEW Request;
    PZP_VIDEO_SHARED_CAPTURE Capture = NULL;
    IMFSample* Sample = NULL;
    LONGLONG Timestamp;
    ZP_VIDEO_IMAGE Image;
    LARGE_INTEGER Zero = { 0 };
    ULONG SettingsVersion = 0;
    USHORT Quality;
    LOGICAL Uninitialize;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;

    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Uninitialize = SUCCEEDED(Result);
    if (Result == RPC_E_CHANGED_MODE) Result = S_OK;
    Request.DirectStreamId = Channel->DirectStreamId;
    Request.DeviceId.Buffer = (const BYTE*)Channel->DeviceId;
    Request.DeviceId.Length = Channel->DeviceIdLength;
    while (SUCCEEDED(Result) && NT_SUCCESS(Status))
    {
        if (NtWaitForSingleObject(Channel->StopEvent, FALSE, &Zero) == STATUS_WAIT_0)
        {
            Status = STATUS_CANCELLED;
            break;
        }
        RtlAcquireSRWLockShared(&Channel->SettingsLock);
        if (SettingsVersion != Channel->SettingsVersion)
        {
            Request.Width = Channel->Format.Width;
            Request.Height = Channel->Format.Height;
            Request.FrameRateNumerator = Channel->Format.FrameRateNumerator;
            Request.FrameRateDenominator = Channel->Format.FrameRateDenominator;
            SettingsVersion = Channel->SettingsVersion;
            ZpVideoShared_Close(Capture);
            Capture = NULL;
        }
        Quality = Channel->Quality;
        RtlReleaseSRWLockShared(&Channel->SettingsLock);
        if (Capture == NULL) Result = ZpVideoShared_Open(&Request, &Capture);
        if (FAILED(Result)) break;
        Result = ZpVideoShared_NextSample(Capture, Channel->StopEvent, &Sample, &Timestamp);
        UNREFERENCED_PARAMETER(Timestamp);
        if (SUCCEEDED(Result)) Result = ZpVideoShared_Encode(Capture,
                                                            Sample,
                                                            Quality,
                                                            &Image);
        if (Sample != NULL)
        {
            IMFSample_Release(Sample);
            Sample = NULL;
        }
        if (FAILED(Result)) break;
        Status = ZpVideo_SendFrame(Channel, &Image);
        ZpVideoCapture_FreeImage(&Image);
    }
    ZpVideoShared_Close(Capture);
    if (Result == HRESULT_FROM_NT(STATUS_CANCELLED))
    {
        Result = S_OK;
        Status = STATUS_CANCELLED;
    }
    if (Uninitialize) CoUninitialize();
    ZpVideo_FinishWorker(Channel,
                         FAILED(Result) ? ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result) :
                                          ZpStatus_FromNtStatus(Status));
    return Status;
}

static
NTSTATUS
ZpVideo_ChannelWindow(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ULONG CreditBytes)
{
    PZP_CLIENT_VIDEO_CHANNEL Channel = (PZP_CLIENT_VIDEO_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending || MAXULONGLONG - Channel->Credit < CreditBytes)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->Credit += CreditBytes;
    NtSetEvent(Channel->CreditEvent, NULL);
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpVideo_ChannelClose(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_VIDEO_CHANNEL Channel = (PZP_CLIENT_VIDEO_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed;

    UNREFERENCED_PARAMETER(Status);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (!Removed) return STATUS_PROTOCOL_UNREACHABLE;
    NtSetEvent(Channel->StopEvent, NULL);
    NtSetEvent(Channel->CreditEvent, NULL);
    ZpClientLocalChannel_Release(&Channel->Header);
    return STATUS_SUCCESS;
}

static
VOID
ZpVideo_ChannelAbort(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_VIDEO_CHANNEL Channel = (PZP_CLIENT_VIDEO_CHANNEL)LocalChannel;

    UNREFERENCED_PARAMETER(Status);
    NtSetEvent(Channel->StopEvent, NULL);
    NtSetEvent(Channel->CreditEvent, NULL);
}

static
VOID
ZpVideo_ChannelDestroy(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel)
{
    PZP_CLIENT_VIDEO_CHANNEL Channel = (PZP_CLIENT_VIDEO_CHANNEL)LocalChannel;

    if (Channel->WorkerThread != NULL) NtClose(Channel->WorkerThread);
    if (Channel->CreditEvent != NULL) NtClose(Channel->CreditEvent);
    if (Channel->StopEvent != NULL) NtClose(Channel->StopEvent);
    Mem_Free(Channel);
}

static
NTSTATUS
ZpVideo_CreateStreamChannel(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PZP_VIDEO_STREAM_REQUEST_VIEW Request,
    _Out_ PZP_CLIENT_VIDEO_CHANNEL* Channel)
{
    PZP_CLIENT_VIDEO_CHANNEL VideoChannel;
    SIZE_T AllocationSize;
    NTSTATUS Status;

    AllocationSize = FIELD_OFFSET(ZP_CLIENT_VIDEO_CHANNEL, DeviceId) +
                     ((SIZE_T)Request->DeviceId.Length + 1) * sizeof(WCHAR);
    VideoChannel = Mem_Alloc(AllocationSize);
    if (VideoChannel == NULL) return STATUS_NO_MEMORY;
    RtlZeroMemory(VideoChannel, FIELD_OFFSET(ZP_CLIENT_VIDEO_CHANNEL, DeviceId));
    RtlInitializeSRWLock(&VideoChannel->SettingsLock);
    VideoChannel->Format = *(PCZP_VIDEO_FORMAT)Request;
    VideoChannel->SettingsVersion = 1;
    VideoChannel->DirectStreamId = Request->DirectStreamId;
    VideoChannel->Quality = Request->Quality;
    VideoChannel->DeviceIdLength = Request->DeviceId.Length;
    RtlCopyMemory(VideoChannel->DeviceId,
                  Request->DeviceId.Buffer,
                  (SIZE_T)Request->DeviceId.Length * sizeof(WCHAR));
    VideoChannel->DeviceId[Request->DeviceId.Length] = UNICODE_NULL;
    Status = NtCreateEvent(&VideoChannel->CreditEvent,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (NT_SUCCESS(Status))
    {
        Status = NtCreateEvent(&VideoChannel->StopEvent,
                               EVENT_MODIFY_STATE | SYNCHRONIZE,
                               NULL,
                               NotificationEvent,
                               FALSE);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpClientLocalChannel_Insert(Client,
                                             &VideoChannel->Header,
                                             ZP_VIDEO_MODULE_ID,
                                             NULL,
                                             ZpVideo_ChannelWindow,
                                             ZpVideo_ChannelClose,
                                             ZpVideo_ChannelAbort,
                                             ZpVideo_ChannelDestroy);
    }
    if (!NT_SUCCESS(Status))
    {
        if (VideoChannel->StopEvent != NULL) NtClose(VideoChannel->StopEvent);
        if (VideoChannel->CreditEvent != NULL) NtClose(VideoChannel->CreditEvent);
        Mem_Free(VideoChannel);
        return Status;
    }
    *Channel = VideoChannel;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpVideo_UpdateStream(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PCZP_VIDEO_STREAM_UPDATE Update)
{
    PZP_CLIENT_LOCAL_CHANNEL LocalChannel;
    PZP_CLIENT_VIDEO_CHANNEL Channel;
    NTSTATUS Status;

    Status = ZpClientLocalChannel_ReferenceById(Client,
                                                Update->ChannelId,
                                                ZP_VIDEO_MODULE_ID,
                                                &LocalChannel);
    if (!NT_SUCCESS(Status)) return Status;
    Channel = (PZP_CLIENT_VIDEO_CHANNEL)LocalChannel;
    RtlAcquireSRWLockExclusive(&Channel->SettingsLock);
    Channel->Format = Update->Format;
    Channel->Quality = Update->Quality;
    Channel->SettingsVersion++;
    RtlReleaseSRWLockExclusive(&Channel->SettingsLock);
    ZpClientLocalChannel_Release(LocalChannel);
    return STATUS_SUCCESS;
}

ZP_STATUS
ZpVideo_Execute(
    _Inout_ struct _ZP_CLIENT_OBJECT* Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_VIDEO_CHANNEL* Channel)
{
    ZP_VIDEO_STREAM_REQUEST_VIEW StreamRequest;
    ZP_VIDEO_STREAM_UPDATE StreamUpdate;
    PZP_CLIENT_VIDEO_CHANNEL VideoChannel;
    HRESULT Result;
    NTSTATUS Status;

    *Channel = NULL;
    if (OperationId == ZP_VIDEO_OPERATION_ENUMERATE_DEVICES)
    {
        if (RequestLength != 0) return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
        Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(Result))
        {
            Result = ZpVideoCapture_Enumerate(Response, ResponseLength);
            CoUninitialize();
        }
        return SUCCEEDED(Result) ? ZpStatus_Make(ZpStatusNone, 0) :
                                   ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result);
    }
    if (OperationId == ZP_VIDEO_OPERATION_UPDATE_STREAM)
    {
        Status = ZpVideo_DecodeStreamUpdate(Request, RequestLength, &StreamUpdate);
        if (NT_SUCCESS(Status)) Status = ZpVideo_UpdateStream(Client, &StreamUpdate);
        return ZpStatus_FromNtStatus(Status);
    }
    if (OperationId != ZP_VIDEO_OPERATION_OPEN_STREAM) return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    Status = ZpVideo_DecodeStreamRequest(Request, RequestLength, &StreamRequest);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Status = ZpVideo_CreateStreamChannel(Client, &StreamRequest, &VideoChannel);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    *ResponseLength = sizeof(ULONGLONG);
    *Response = Mem_Alloc(*ResponseLength);
    Status = *Response == NULL ? STATUS_NO_MEMORY :
                 ZpVideo_EncodeChannel(((PZP_CLIENT_LOCAL_CHANNEL)VideoChannel)->ChannelId,
                                       *Response,
                                       *ResponseLength,
                                       ResponseLength);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(*Response);
        *Response = NULL;
        ZpVideo_CommitChannel(VideoChannel, FALSE);
        return ZpStatus_FromNtStatus(Status);
    }
    *Channel = VideoChannel;
    return ZpStatus_Make(ZpStatusNone, 0);
}

VOID
ZpVideo_CommitChannel(
    _Inout_ PZP_CLIENT_VIDEO_CHANNEL Channel,
    _In_ LOGICAL ResponseSent)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed = FALSE;
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ResponseSent)
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    }
    else
    {
        Channel->WorkerActive = TRUE;
        ZpClientLocalChannel_AddRef(&Channel->Header);
        Object->CallbackCount++;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        ZpClientLocalChannel_Release(&Channel->Header);
        return;
    }
    if (!ResponseSent) return;
    Status = PS_CreateThread(NtCurrentProcess(), TRUE, ZpVideo_Worker, Channel, &Channel->WorkerThread, NULL);
    if (NT_SUCCESS(Status)) Status = NtResumeThread(Channel->WorkerThread, NULL);
    if (!NT_SUCCESS(Status))
    {
        if (Channel->WorkerThread != NULL) NtTerminateThread(Channel->WorkerThread, Status);
        ZpVideo_FinishWorker(Channel, ZpStatus_FromNtStatus(Status));
    }
}
