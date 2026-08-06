#include "Shared.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

typedef struct _ZP_AUDIO_SHARED_SOURCE ZP_AUDIO_SHARED_SOURCE, *PZP_AUDIO_SHARED_SOURCE;

struct _ZP_AUDIO_SHARED_CAPTURE
{
    LIST_ENTRY ListEntry;
    PZP_AUDIO_SHARED_SOURCE Source;
    HANDLE Event;
    ULONGLONG Sequence;
};

struct _ZP_AUDIO_SHARED_SOURCE
{
    LIST_ENTRY ListEntry;
    RTL_SRWLOCK Lock;
    LIST_ENTRY Captures;
    HANDLE Thread;
    HANDLE StopEvent;
    HANDLE ReadyEvent;
    PZP_AUDIO_SHARED_FRAME Frame;
    HRESULT Result;
    ULONGLONG Sequence;
    ULONG CaptureCount;
    ZP_AUDIO_FLOW Flow;
    USHORT Channels;
    ULONG SampleRate;
    ULONG DeviceIdLength;
    WCHAR DeviceId[ANYSIZE_ARRAY];
};

static RTL_SRWLOCK ZpAudioSharedLock = RTL_SRWLOCK_INIT;
static LIST_ENTRY ZpAudioSharedSources = { &ZpAudioSharedSources, &ZpAudioSharedSources };

static
VOID
ZpAudioShared_AddRefFrame(
    _In_ PZP_AUDIO_SHARED_FRAME Frame)
{
    InterlockedIncrement(&Frame->ReferenceCount);
}

VOID
ZpAudioShared_ReleaseFrame(
    _In_ PZP_AUDIO_SHARED_FRAME Frame)
{
    if (InterlockedDecrement(&Frame->ReferenceCount) == 0) Mem_Free(Frame);
}

static
LOGICAL
ZpAudioShared_IsSameSource(
    _In_ PZP_AUDIO_SHARED_SOURCE Source,
    _In_ ZP_AUDIO_FLOW Flow,
    _In_reads_opt_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength)
{
    return Source->Flow == Flow && Source->DeviceIdLength == DeviceIdLength &&
           (DeviceIdLength == 0 || _wcsnicmp(Source->DeviceId, DeviceId, DeviceIdLength) == 0);
}

static
VOID
ZpAudioShared_SignalCaptures(
    _In_ PZP_AUDIO_SHARED_SOURCE Source)
{
    PLIST_ENTRY Entry;

    for (Entry = Source->Captures.Flink; Entry != &Source->Captures; Entry = Entry->Flink)
    {
        NtSetEvent(CONTAINING_RECORD(Entry, ZP_AUDIO_SHARED_CAPTURE, ListEntry)->Event, NULL);
    }
}

static
NTSTATUS
NTAPI
ZpAudioShared_Publish(
    _In_ USHORT Channels,
    _In_ ULONG SampleRate,
    _In_ ULONG FrameCount,
    _In_reads_(FrameCount * Channels) const SHORT* Samples,
    _In_ ULONGLONG Timestamp,
    _In_opt_ PVOID Context)
{
    PZP_AUDIO_SHARED_SOURCE Source = Context;
    PZP_AUDIO_SHARED_FRAME Frame, Previous;
    SIZE_T SampleCount = (SIZE_T)FrameCount * Channels;

    Frame = Mem_Alloc(FIELD_OFFSET(ZP_AUDIO_SHARED_FRAME, Samples) + SampleCount * sizeof(SHORT));
    if (Frame == NULL) return STATUS_NO_MEMORY;
    Frame->ReferenceCount = 1;
    Frame->Channels = Channels;
    Frame->SampleRate = SampleRate;
    Frame->FrameCount = FrameCount;
    Frame->Timestamp = Timestamp;
    RtlCopyMemory(Frame->Samples, Samples, SampleCount * sizeof(SHORT));
    RtlAcquireSRWLockExclusive(&Source->Lock);
    Previous = Source->Frame;
    Source->Frame = Frame;
    if (++Source->Sequence == 0) Source->Sequence++;
    ZpAudioShared_SignalCaptures(Source);
    RtlReleaseSRWLockExclusive(&Source->Lock);
    if (Previous != NULL) ZpAudioShared_ReleaseFrame(Previous);
    return STATUS_SUCCESS;
}

static
_Function_class_(USER_THREAD_START_ROUTINE)
NTSTATUS
NTAPI
ZpAudioShared_Worker(
    _In_ PVOID Context)
{
    PZP_AUDIO_SHARED_SOURCE Source = Context;
    NTSTATUS Status;
    HRESULT Result;

    Result = ZpAudioCapture_QueryFormat(Source->Flow,
                                        Source->DeviceId,
                                        Source->DeviceIdLength,
                                        &Source->Channels,
                                        &Source->SampleRate);
    RtlAcquireSRWLockExclusive(&Source->Lock);
    Source->Result = Result;
    NtSetEvent(Source->ReadyEvent, NULL);
    RtlReleaseSRWLockExclusive(&Source->Lock);
    if (SUCCEEDED(Result))
    {
        Result = ZpAudioCapture_Run(Source->Flow,
                                    Source->DeviceId,
                                    Source->DeviceIdLength,
                                    Source->StopEvent,
                                    ZpAudioShared_Publish,
                                    Source,
                                    &Status);
        if (SUCCEEDED(Result) && Status != STATUS_CANCELLED && !NT_SUCCESS(Status)) Result = HRESULT_FROM_NT(Status);
    }
    RtlAcquireSRWLockExclusive(&Source->Lock);
    Source->Result = FAILED(Result) ? Result : S_FALSE;
    ZpAudioShared_SignalCaptures(Source);
    RtlReleaseSRWLockExclusive(&Source->Lock);
    return FAILED(Result) ? (NTSTATUS)Result : STATUS_SUCCESS;
}

static
VOID
ZpAudioShared_FreeSource(
    _In_ PZP_AUDIO_SHARED_SOURCE Source)
{
    NtSetEvent(Source->StopEvent, NULL);
    NtWaitForSingleObject(Source->Thread, FALSE, NULL);
    NtClose(Source->Thread);
    NtClose(Source->ReadyEvent);
    NtClose(Source->StopEvent);
    if (Source->Frame != NULL) ZpAudioShared_ReleaseFrame(Source->Frame);
    Mem_Free(Source);
}

HRESULT
ZpAudioShared_Open(
    _In_ ZP_AUDIO_FLOW Flow,
    _In_reads_opt_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _Out_ PZP_AUDIO_SHARED_CAPTURE* Capture)
{
    PZP_AUDIO_SHARED_CAPTURE Object;
    PZP_AUDIO_SHARED_SOURCE Source = NULL;
    PLIST_ENTRY Entry;
    SIZE_T Size;
    NTSTATUS Status;
    HRESULT Result;

    if ((Flow != ZpAudioFlowRender && Flow != ZpAudioFlowCapture) ||
        (DeviceIdLength != 0 && DeviceId == NULL)) return E_INVALIDARG;
    Object = Mem_Alloc(sizeof(*Object));
    if (Object == NULL) return E_OUTOFMEMORY;
    Status = NtCreateEvent(&Object->Event, EVENT_MODIFY_STATE | SYNCHRONIZE, NULL, NotificationEvent, FALSE);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Object);
        return HRESULT_FROM_NT(Status);
    }
    RtlAcquireSRWLockExclusive(&ZpAudioSharedLock);
    for (Entry = ZpAudioSharedSources.Flink; Entry != &ZpAudioSharedSources; Entry = Entry->Flink)
    {
        Source = CONTAINING_RECORD(Entry, ZP_AUDIO_SHARED_SOURCE, ListEntry);
        if (ZpAudioShared_IsSameSource(Source, Flow, DeviceId, DeviceIdLength)) break;
    }
    if (Entry == &ZpAudioSharedSources)
    {
        Size = FIELD_OFFSET(ZP_AUDIO_SHARED_SOURCE, DeviceId) + ((SIZE_T)DeviceIdLength + 1) * sizeof(WCHAR);
        Source = Mem_Alloc(Size);
        if (Source == NULL)
        {
            Status = STATUS_NO_MEMORY;
            goto Cleanup;
        }
        RtlZeroMemory(Source, FIELD_OFFSET(ZP_AUDIO_SHARED_SOURCE, DeviceId));
        RtlInitializeSRWLock(&Source->Lock);
        InitializeListHead(&Source->Captures);
        Source->Flow = Flow;
        Source->DeviceIdLength = DeviceIdLength;
        if (DeviceIdLength != 0) RtlCopyMemory(Source->DeviceId, DeviceId, (SIZE_T)DeviceIdLength * sizeof(WCHAR));
        Source->DeviceId[DeviceIdLength] = UNICODE_NULL;
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
                                                         ZpAudioShared_Worker,
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
        InsertTailList(&ZpAudioSharedSources, &Source->ListEntry);
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
    RtlReleaseSRWLockExclusive(&ZpAudioSharedLock);
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
        ZpAudioShared_Close(Object);
        return Result;
    }
    *Capture = Object;
    return S_OK;
}

VOID
ZpAudioShared_GetFormat(
    _In_ PZP_AUDIO_SHARED_CAPTURE Capture,
    _Out_ PUSHORT Channels,
    _Out_ PULONG SampleRate)
{
    *Channels = Capture->Source->Channels;
    *SampleRate = Capture->Source->SampleRate;
}

HRESULT
ZpAudioShared_Next(
    _Inout_ PZP_AUDIO_SHARED_CAPTURE Capture,
    _In_ HANDLE StopEvent,
    _Out_ PZP_AUDIO_SHARED_FRAME* Frame)
{
    PZP_AUDIO_SHARED_SOURCE Source = Capture->Source;
    HANDLE Events[2] = { StopEvent, Capture->Event };
    NTSTATUS Status;
    HRESULT Result;

    for (;;)
    {
        RtlAcquireSRWLockExclusive(&Source->Lock);
        if (Capture->Sequence != Source->Sequence)
        {
            Capture->Sequence = Source->Sequence;
            *Frame = Source->Frame;
            ZpAudioShared_AddRefFrame(*Frame);
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

VOID
ZpAudioShared_Close(
    _In_opt_ PZP_AUDIO_SHARED_CAPTURE Capture)
{
    PZP_AUDIO_SHARED_SOURCE Source;
    LOGICAL Last;

    if (Capture == NULL) return;
    Source = Capture->Source;
    RtlAcquireSRWLockExclusive(&ZpAudioSharedLock);
    RtlAcquireSRWLockExclusive(&Source->Lock);
    RemoveEntryList(&Capture->ListEntry);
    Last = --Source->CaptureCount == 0;
    if (Last) RemoveEntryList(&Source->ListEntry);
    RtlReleaseSRWLockExclusive(&Source->Lock);
    RtlReleaseSRWLockExclusive(&ZpAudioSharedLock);
    NtClose(Capture->Event);
    Mem_Free(Capture);
    if (Last) ZpAudioShared_FreeSource(Source);
}
