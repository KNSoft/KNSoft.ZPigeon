#define COBJMACROS

#include "Shared.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

typedef struct _ZP_VIDEO_SHARED_SOURCE ZP_VIDEO_SHARED_SOURCE, *PZP_VIDEO_SHARED_SOURCE;

struct _ZP_VIDEO_SHARED_CAPTURE
{
    LIST_ENTRY ListEntry;
    PZP_VIDEO_SHARED_SOURCE Source;
    HANDLE Event;
    ULONGLONG Sequence;
};

struct _ZP_VIDEO_SHARED_SOURCE
{
    LIST_ENTRY ListEntry;
    RTL_SRWLOCK Lock;
    RTL_SRWLOCK EncodeLock;
    LIST_ENTRY Captures;
    HANDLE Thread;
    HANDLE StopEvent;
    HANDLE ReadyEvent;
    PZP_VIDEO_CAPTURE Device;
    IMFSample* Sample;
    HRESULT Result;
    LONGLONG Timestamp;
    ULONGLONG Sequence;
    ULONG CaptureCount;
    ZP_VIDEO_FORMAT Format;
    ULONG DeviceIdLength;
    WCHAR DeviceId[ANYSIZE_ARRAY];
};

static RTL_SRWLOCK ZpVideoSharedLock = RTL_SRWLOCK_INIT;
static LIST_ENTRY ZpVideoSharedSources = { &ZpVideoSharedSources, &ZpVideoSharedSources };

static
LOGICAL
ZpVideoShared_IsSameSource(
    _In_ PZP_VIDEO_SHARED_SOURCE Source,
    _In_ PZP_VIDEO_STREAM_REQUEST_VIEW Request)
{
    return Source->DeviceIdLength == Request->DeviceId.Length &&
           _wcsnicmp(Source->DeviceId, (PCWCH)Request->DeviceId.Buffer, Request->DeviceId.Length) == 0 &&
           Source->Format.Width == Request->Width && Source->Format.Height == Request->Height &&
           Source->Format.FrameRateNumerator == Request->FrameRateNumerator &&
           Source->Format.FrameRateDenominator == Request->FrameRateDenominator;
}

static
VOID
ZpVideoShared_SignalCaptures(
    _In_ PZP_VIDEO_SHARED_SOURCE Source)
{
    PLIST_ENTRY Entry;

    for (Entry = Source->Captures.Flink; Entry != &Source->Captures; Entry = Entry->Flink)
    {
        NtSetEvent(CONTAINING_RECORD(Entry, ZP_VIDEO_SHARED_CAPTURE, ListEntry)->Event, NULL);
    }
}

static
NTSTATUS
NTAPI
ZpVideoShared_Worker(
    _In_ PVOID Context)
{
    PZP_VIDEO_SHARED_SOURCE Source = Context;
    ZP_VIDEO_STREAM_REQUEST_VIEW Request;
    IMFSample* Sample;
    LONGLONG Timestamp;
    LARGE_INTEGER Zero = { 0 };
    HRESULT Result;
    LOGICAL Uninitialize;

    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Uninitialize = SUCCEEDED(Result);
    if (Result == RPC_E_CHANGED_MODE) Result = S_OK;
    Request.Width = Source->Format.Width;
    Request.Height = Source->Format.Height;
    Request.FrameRateNumerator = Source->Format.FrameRateNumerator;
    Request.FrameRateDenominator = Source->Format.FrameRateDenominator;
    Request.DirectStreamId = 0;
    Request.Quality = 100;
    Request.DeviceId.Buffer = Source->DeviceId;
    Request.DeviceId.Length = Source->DeviceIdLength;
    if (SUCCEEDED(Result)) Result = ZpVideoCapture_Create(&Request, &Source->Device);
    RtlAcquireSRWLockExclusive(&Source->Lock);
    Source->Result = Result;
    NtSetEvent(Source->ReadyEvent, NULL);
    RtlReleaseSRWLockExclusive(&Source->Lock);
    while (SUCCEEDED(Result) && NtWaitForSingleObject(Source->StopEvent, FALSE, &Zero) == STATUS_TIMEOUT)
    {
        Result = ZpVideoCapture_NextSample(Source->Device, &Sample, &Timestamp);
        if (FAILED(Result)) break;
        RtlAcquireSRWLockExclusive(&Source->Lock);
        if (Source->Sample != NULL) IMFSample_Release(Source->Sample);
        Source->Sample = Sample;
        Source->Timestamp = Timestamp;
        if (++Source->Sequence == 0) Source->Sequence++;
        ZpVideoShared_SignalCaptures(Source);
        RtlReleaseSRWLockExclusive(&Source->Lock);
    }
    RtlAcquireSRWLockExclusive(&Source->Lock);
    Source->Result = FAILED(Result) ? Result : S_FALSE;
    ZpVideoShared_SignalCaptures(Source);
    RtlReleaseSRWLockExclusive(&Source->Lock);
    NtWaitForSingleObject(Source->StopEvent, FALSE, NULL);
    ZpVideoCapture_Close(Source->Device);
    Source->Device = NULL;
    if (Uninitialize) CoUninitialize();
    return FAILED(Result) ? (NTSTATUS)Result : STATUS_SUCCESS;
}

static
VOID
ZpVideoShared_FreeSource(
    _In_ PZP_VIDEO_SHARED_SOURCE Source)
{
    NtSetEvent(Source->StopEvent, NULL);
    NtWaitForSingleObject(Source->Thread, FALSE, NULL);
    NtClose(Source->Thread);
    NtClose(Source->ReadyEvent);
    NtClose(Source->StopEvent);
    if (Source->Sample != NULL) IMFSample_Release(Source->Sample);
    Mem_Free(Source);
}

HRESULT
ZpVideoShared_Open(
    _In_ PZP_VIDEO_STREAM_REQUEST_VIEW Request,
    _Out_ PZP_VIDEO_SHARED_CAPTURE* Capture)
{
    PZP_VIDEO_SHARED_CAPTURE Object;
    PZP_VIDEO_SHARED_SOURCE Source = NULL;
    PLIST_ENTRY Entry;
    SIZE_T Size;
    NTSTATUS Status;
    HRESULT Result;

    if (Request->DeviceId.Length == 0) return E_INVALIDARG;
    Object = Mem_Alloc(sizeof(*Object));
    if (Object == NULL) return E_OUTOFMEMORY;
    Status = NtCreateEvent(&Object->Event, EVENT_MODIFY_STATE | SYNCHRONIZE, NULL, NotificationEvent, FALSE);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Object);
        return HRESULT_FROM_NT(Status);
    }
    RtlAcquireSRWLockExclusive(&ZpVideoSharedLock);
    for (Entry = ZpVideoSharedSources.Flink; Entry != &ZpVideoSharedSources; Entry = Entry->Flink)
    {
        Source = CONTAINING_RECORD(Entry, ZP_VIDEO_SHARED_SOURCE, ListEntry);
        if (ZpVideoShared_IsSameSource(Source, Request)) break;
    }
    if (Entry == &ZpVideoSharedSources)
    {
        Size = FIELD_OFFSET(ZP_VIDEO_SHARED_SOURCE, DeviceId) +
               ((SIZE_T)Request->DeviceId.Length + 1) * sizeof(WCHAR);
        Source = Mem_Alloc(Size);
        if (Source == NULL)
        {
            Status = STATUS_NO_MEMORY;
            goto Cleanup;
        }
        RtlZeroMemory(Source, FIELD_OFFSET(ZP_VIDEO_SHARED_SOURCE, DeviceId));
        RtlInitializeSRWLock(&Source->Lock);
        RtlInitializeSRWLock(&Source->EncodeLock);
        InitializeListHead(&Source->Captures);
        Source->Format = *(PCZP_VIDEO_FORMAT)Request;
        Source->DeviceIdLength = Request->DeviceId.Length;
        RtlCopyMemory(Source->DeviceId,
                      Request->DeviceId.Buffer,
                      (SIZE_T)Request->DeviceId.Length * sizeof(WCHAR));
        Source->DeviceId[Request->DeviceId.Length] = UNICODE_NULL;
        Status = NtCreateEvent(&Source->StopEvent,
                               EVENT_MODIFY_STATE | SYNCHRONIZE,
                               NULL,
                               NotificationEvent,
                               FALSE);
        if (NT_SUCCESS(Status)) Status = NtCreateEvent(&Source->ReadyEvent,
                                                        EVENT_MODIFY_STATE | SYNCHRONIZE,
                                                        NULL,
                                                        NotificationEvent,
                                                        FALSE);
        if (NT_SUCCESS(Status)) Status = PS_CreateThread(NtCurrentProcess(),
                                                         TRUE,
                                                         ZpVideoShared_Worker,
                                                         Source,
                                                         &Source->Thread,
                                                         NULL);
        if (!NT_SUCCESS(Status))
        {
            if (Source->ReadyEvent != NULL) NtClose(Source->ReadyEvent);
            if (Source->StopEvent != NULL) NtClose(Source->StopEvent);
            Mem_Free(Source);
            Source = NULL;
            goto Cleanup;
        }
        InsertTailList(&ZpVideoSharedSources, &Source->ListEntry);
        NtResumeThread(Source->Thread, NULL);
    }
    Object->Source = Source;
    Object->Sequence = 0;
    RtlAcquireSRWLockExclusive(&Source->Lock);
    InsertTailList(&Source->Captures, &Object->ListEntry);
    Source->CaptureCount++;
    RtlReleaseSRWLockExclusive(&Source->Lock);
    Status = STATUS_SUCCESS;

Cleanup:
    RtlReleaseSRWLockExclusive(&ZpVideoSharedLock);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Object->Event);
        Mem_Free(Object);
        return HRESULT_FROM_NT(Status);
    }
    Status = NtWaitForSingleObject(Source->ReadyEvent, FALSE, NULL);
    Result = NT_SUCCESS(Status) ? Source->Result : HRESULT_FROM_NT(Status);
    if (FAILED(Result))
    {
        ZpVideoShared_Close(Object);
        return Result;
    }
    *Capture = Object;
    return S_OK;
}

HRESULT
ZpVideoShared_NextSample(
    _Inout_ PZP_VIDEO_SHARED_CAPTURE Capture,
    _In_ HANDLE StopEvent,
    _Outptr_ IMFSample** Sample,
    _Out_ PLONGLONG Timestamp)
{
    PZP_VIDEO_SHARED_SOURCE Source = Capture->Source;
    HANDLE Events[2] = { StopEvent, Capture->Event };
    NTSTATUS Status;
    HRESULT Result;

    for (;;)
    {
        RtlAcquireSRWLockExclusive(&Source->Lock);
        if (Capture->Sequence != Source->Sequence)
        {
            Capture->Sequence = Source->Sequence;
            *Sample = Source->Sample;
            IMFSample_AddRef(*Sample);
            *Timestamp = Source->Timestamp;
            RtlReleaseSRWLockExclusive(&Source->Lock);
            return S_OK;
        }
        Result = Source->Result;
        if (Result != S_OK)
        {
            RtlReleaseSRWLockExclusive(&Source->Lock);
            return Result;
        }
        NtClearEvent(Capture->Event);
        RtlReleaseSRWLockExclusive(&Source->Lock);
        Status = NtWaitForMultipleObjects(ARRAYSIZE(Events), Events, WaitAny, FALSE, NULL);
        if (Status == STATUS_WAIT_0) return HRESULT_FROM_NT(STATUS_CANCELLED);
        if (Status != STATUS_WAIT_1) return HRESULT_FROM_NT(Status);
    }
}

HRESULT
ZpVideoShared_Encode(
    _In_ PZP_VIDEO_SHARED_CAPTURE Capture,
    _In_ IMFSample* Sample,
    _In_ USHORT Quality,
    _Out_ PZP_VIDEO_IMAGE Image)
{
    HRESULT Result;

    RtlAcquireSRWLockExclusive(&Capture->Source->EncodeLock);
    Result = ZpVideoCapture_EncodeSample(Capture->Source->Device,
                                         Sample,
                                         Quality,
                                         Image);
    RtlReleaseSRWLockExclusive(&Capture->Source->EncodeLock);
    return Result;
}

VOID
ZpVideoShared_Close(
    _In_opt_ PZP_VIDEO_SHARED_CAPTURE Capture)
{
    PZP_VIDEO_SHARED_SOURCE Source;
    LOGICAL Last;

    if (Capture == NULL) return;
    Source = Capture->Source;
    RtlAcquireSRWLockExclusive(&ZpVideoSharedLock);
    RtlAcquireSRWLockExclusive(&Source->Lock);
    RemoveEntryList(&Capture->ListEntry);
    Last = --Source->CaptureCount == 0;
    if (Last) RemoveEntryList(&Source->ListEntry);
    RtlReleaseSRWLockExclusive(&Source->Lock);
    RtlReleaseSRWLockExclusive(&ZpVideoSharedLock);
    NtClose(Capture->Event);
    Mem_Free(Capture);
    if (Last) ZpVideoShared_FreeSource(Source);
}
