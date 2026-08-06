#include "Shared.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

typedef struct _ZP_WINDOW_SHARED_SOURCE ZP_WINDOW_SHARED_SOURCE, *PZP_WINDOW_SHARED_SOURCE;

struct _ZP_WINDOW_SHARED_CAPTURE
{
    LIST_ENTRY ListEntry;
    PZP_WINDOW_SHARED_SOURCE Source;
    HANDLE Event;
    ULONGLONG Sequence;
    ZP_WINDOW_CAPTURE_OPTIONS Options;
};

struct _ZP_WINDOW_SHARED_SOURCE
{
    LIST_ENTRY ListEntry;
    RTL_SRWLOCK Lock;
    RTL_SRWLOCK ConvertLock;
    LIST_ENTRY Captures;
    HANDLE Thread;
    HANDLE StopEvent;
    HANDLE ReadyEvent;
    PZP_WINDOW_CAPTURE Device;
    PZP_WINDOW_CAPTURE_FRAME Frame;
    PZP_WINDOW_SHARED_IMAGE Image;
    HRESULT Result;
    ULONGLONG Sequence;
    ULONGLONG ImageSequence;
    HWND Window;
    ULONG CaptureCount;
    ULONG Flags;
    ULONG MonitorIndex;
};

static RTL_SRWLOCK ZpWindowSharedLock = RTL_SRWLOCK_INIT;
static LIST_ENTRY ZpWindowSharedSources = { &ZpWindowSharedSources, &ZpWindowSharedSources };

static
VOID
ZpWindowShared_SignalCaptures(
    _In_ PZP_WINDOW_SHARED_SOURCE Source)
{
    PLIST_ENTRY Entry;

    for (Entry = Source->Captures.Flink; Entry != &Source->Captures; Entry = Entry->Flink)
    {
        NtSetEvent(CONTAINING_RECORD(Entry, ZP_WINDOW_SHARED_CAPTURE, ListEntry)->Event, NULL);
    }
}

static
VOID
ZpWindowShared_GetOptions(
    _In_ PZP_WINDOW_SHARED_SOURCE Source,
    _Out_ PZP_WINDOW_CAPTURE_OPTIONS Options)
{
    PZP_WINDOW_SHARED_CAPTURE Capture;
    PLIST_ENTRY Entry;

    Options->Flags = Source->Flags;
    Options->MonitorIndex = Source->MonitorIndex;
    Options->MaxDimension = 0;
    Options->FrameRate = 0;
    Options->Quality = 0;
    for (Entry = Source->Captures.Flink; Entry != &Source->Captures; Entry = Entry->Flink)
    {
        Capture = CONTAINING_RECORD(Entry, ZP_WINDOW_SHARED_CAPTURE, ListEntry);
        Options->MaxDimension = max(Options->MaxDimension, Capture->Options.MaxDimension);
        Options->Quality = max(Options->Quality, Capture->Options.Quality);
        Options->FrameRate = max(Options->FrameRate, Capture->Options.FrameRate);
    }
}

VOID
ZpWindowShared_ReleaseImage(
    _In_ PZP_WINDOW_SHARED_IMAGE Image)
{
    if (InterlockedDecrement(&Image->ReferenceCount) != 0) return;
    ZpWindowCapture_FreeImage(&Image->Value);
    Mem_Free(Image);
}

static
_Function_class_(USER_THREAD_START_ROUTINE)
NTSTATUS
NTAPI
ZpWindowShared_Worker(
    _In_ PVOID Context)
{
    PZP_WINDOW_SHARED_SOURCE Source = Context;
    ZP_WINDOW_CAPTURE_OPTIONS Options;
    PZP_WINDOW_CAPTURE_FRAME Frame, Previous;
    LARGE_INTEGER Zero = { 0 };
    HRESULT Result;

    RtlAcquireSRWLockShared(&Source->Lock);
    ZpWindowShared_GetOptions(Source, &Options);
    RtlReleaseSRWLockShared(&Source->Lock);
    Result = ZpWindowCapture_Create(Source->Window, &Options, &Source->Device);
    RtlAcquireSRWLockExclusive(&Source->Lock);
    Source->Result = Result;
    NtSetEvent(Source->ReadyEvent, NULL);
    RtlReleaseSRWLockExclusive(&Source->Lock);
    while (SUCCEEDED(Result) && NtWaitForSingleObject(Source->StopEvent, FALSE, &Zero) == STATUS_TIMEOUT)
    {
        Result = ZpWindowCapture_NextFrame(Source->Device, 1000, &Frame);
        if (Result == HRESULT_FROM_WIN32(ERROR_TIMEOUT) || Result == S_FALSE)
        {
            Result = S_OK;
            continue;
        }
        if (FAILED(Result)) break;
        RtlAcquireSRWLockExclusive(&Source->Lock);
        Previous = Source->Frame;
        Source->Frame = Frame;
        if (++Source->Sequence == 0) Source->Sequence++;
        ZpWindowShared_SignalCaptures(Source);
        RtlReleaseSRWLockExclusive(&Source->Lock);
        if (Previous != NULL) ZpWindowCapture_ReleaseFrame(Previous);
    }
    RtlAcquireSRWLockExclusive(&Source->Lock);
    Source->Result = FAILED(Result) ? Result : S_FALSE;
    ZpWindowShared_SignalCaptures(Source);
    RtlReleaseSRWLockExclusive(&Source->Lock);
    NtWaitForSingleObject(Source->StopEvent, FALSE, NULL);
    ZpWindowCapture_Close(Source->Device);
    return FAILED(Result) ? (NTSTATUS)Result : STATUS_SUCCESS;
}

static
HRESULT
ZpWindowShared_UpdateOptions(
    _In_ PZP_WINDOW_SHARED_SOURCE Source)
{
    ZP_WINDOW_CAPTURE_OPTIONS Options;
    HRESULT Result;

    ZpWindowShared_GetOptions(Source, &Options);
    if (Source->Device == NULL) return S_OK;
    RtlAcquireSRWLockExclusive(&Source->ConvertLock);
    Result = ZpWindowCapture_UpdateOptions(Source->Device, &Options);
    RtlReleaseSRWLockExclusive(&Source->ConvertLock);
    return Result;
}

static
VOID
ZpWindowShared_FreeSource(
    _In_ PZP_WINDOW_SHARED_SOURCE Source)
{
    NtSetEvent(Source->StopEvent, NULL);
    if (Source->Thread != NULL)
    {
        NtWaitForSingleObject(Source->Thread, FALSE, NULL);
        NtClose(Source->Thread);
    }
    NtClose(Source->ReadyEvent);
    NtClose(Source->StopEvent);
    if (Source->Image != NULL) ZpWindowShared_ReleaseImage(Source->Image);
    if (Source->Frame != NULL) ZpWindowCapture_ReleaseFrame(Source->Frame);
    Mem_Free(Source);
}

HRESULT
ZpWindowShared_Open(
    _In_ HWND Window,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _Out_ PZP_WINDOW_SHARED_CAPTURE* Capture)
{
    PZP_WINDOW_SHARED_CAPTURE Object;
    PZP_WINDOW_SHARED_SOURCE Source = NULL;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    HRESULT Result;

    Object = Mem_Alloc(sizeof(*Object));
    if (Object == NULL) return E_OUTOFMEMORY;
    Object->Options = *Options;
    Status = NtCreateEvent(&Object->Event, EVENT_MODIFY_STATE | SYNCHRONIZE, NULL, NotificationEvent, FALSE);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Object);
        return HRESULT_FROM_NT(Status);
    }
    RtlAcquireSRWLockExclusive(&ZpWindowSharedLock);
    for (Entry = ZpWindowSharedSources.Flink; Entry != &ZpWindowSharedSources; Entry = Entry->Flink)
    {
        Source = CONTAINING_RECORD(Entry, ZP_WINDOW_SHARED_SOURCE, ListEntry);
        if (Source->Window == Window && Source->Flags == Options->Flags &&
            Source->MonitorIndex == Options->MonitorIndex)
        {
            break;
        }
    }
    if (Entry == &ZpWindowSharedSources)
    {
        Source = Mem_Alloc(sizeof(*Source));
        if (Source == NULL)
        {
            RtlReleaseSRWLockExclusive(&ZpWindowSharedLock);
            NtClose(Object->Event);
            Mem_Free(Object);
            return E_OUTOFMEMORY;
        }
        RtlZeroMemory(Source, sizeof(*Source));
        RtlInitializeSRWLock(&Source->Lock);
        RtlInitializeSRWLock(&Source->ConvertLock);
        InitializeListHead(&Source->Captures);
        Source->Window = Window;
        Source->Flags = Options->Flags;
        Source->MonitorIndex = Options->MonitorIndex;
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
        if (!NT_SUCCESS(Status))
        {
            if (Source->ReadyEvent != NULL) NtClose(Source->ReadyEvent);
            if (Source->StopEvent != NULL) NtClose(Source->StopEvent);
            RtlReleaseSRWLockExclusive(&ZpWindowSharedLock);
            Mem_Free(Source);
            NtClose(Object->Event);
            Mem_Free(Object);
            return HRESULT_FROM_NT(Status);
        }
        InsertTailList(&ZpWindowSharedSources, &Source->ListEntry);
    }
    Object->Source = Source;
    Object->Sequence = 0;
    RtlAcquireSRWLockExclusive(&Source->Lock);
    InsertTailList(&Source->Captures, &Object->ListEntry);
    Source->CaptureCount++;
    if (Source->Thread == NULL)
    {
        Status = PS_CreateThread(NtCurrentProcess(),
                                 TRUE,
                                 ZpWindowShared_Worker,
                                 Source,
                                 &Source->Thread,
                                 NULL);
        if (NT_SUCCESS(Status)) NtResumeThread(Source->Thread, NULL);
    }
    else
    {
        Result = ZpWindowShared_UpdateOptions(Source);
        Status = SUCCEEDED(Result) ? STATUS_SUCCESS : (NTSTATUS)Result;
    }
    RtlReleaseSRWLockExclusive(&Source->Lock);
    RtlReleaseSRWLockExclusive(&ZpWindowSharedLock);
    if (!NT_SUCCESS(Status))
    {
        ZpWindowShared_Close(Object);
        return HRESULT_FROM_NT(Status);
    }
    Status = NtWaitForSingleObject(Source->ReadyEvent, FALSE, NULL);
    Result = NT_SUCCESS(Status) ? Source->Result : HRESULT_FROM_NT(Status);
    if (SUCCEEDED(Result))
    {
        RtlAcquireSRWLockExclusive(&Source->Lock);
        Result = ZpWindowShared_UpdateOptions(Source);
        Object->Sequence = Source->Sequence;
        RtlReleaseSRWLockExclusive(&Source->Lock);
        if (SUCCEEDED(Result)) ZpWindowShared_RequestKeyFrame(Object);
    }
    if (FAILED(Result))
    {
        ZpWindowShared_Close(Object);
        return Result;
    }
    *Capture = Object;
    return S_OK;
}

static
_Success_(return == S_OK)
HRESULT
ZpWindowShared_NextFrame(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_CAPTURE_FRAME* Frame,
    _Out_ PULONGLONG Sequence,
    _Out_opt_ PLOGICAL Skipped)
{
    PZP_WINDOW_SHARED_SOURCE Source = Capture->Source;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    HRESULT Result;

    for (;;)
    {
        RtlAcquireSRWLockExclusive(&Source->Lock);
        if (Capture->Sequence != Source->Sequence)
        {
            if (Skipped != NULL) *Skipped = Capture->Sequence != 0 &&
                                             Capture->Sequence + 1 != Source->Sequence;
            Capture->Sequence = Source->Sequence;
            *Frame = Source->Frame;
            *Sequence = Source->Sequence;
            ZpWindowCapture_AddRefFrame(*Frame);
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
        Timeout.QuadPart = -(LONGLONG)TimeoutMilliseconds * 10000;
        Status = NtWaitForSingleObject(Capture->Event, FALSE, &Timeout);
        if (Status == STATUS_TIMEOUT) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        if (!NT_SUCCESS(Status)) return HRESULT_FROM_NT(Status);
    }
}

_Success_(return == S_OK)
HRESULT
ZpWindowShared_Next(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_SHARED_IMAGE* Image)
{
    PZP_WINDOW_SHARED_SOURCE Source = Capture->Source;
    PZP_WINDOW_CAPTURE_FRAME Frame;
    PZP_WINDOW_SHARED_IMAGE Object, Previous = NULL;
    ULONGLONG Sequence;
    LOGICAL Skipped;
    HRESULT Result;

    Result = ZpWindowShared_NextFrame(Capture, TimeoutMilliseconds, &Frame, &Sequence, &Skipped);
    if (Result == S_OK)
    {
        RtlAcquireSRWLockExclusive(&Source->ConvertLock);
        if (Skipped) ZpWindowCapture_RequestKeyFrame(Source->Device);
        if (!Skipped && Source->ImageSequence == Sequence)
        {
            Object = Source->Image;
            InterlockedIncrement(&Object->ReferenceCount);
        }
        else
        {
            Object = Mem_Alloc(sizeof(*Object));
            Result = Object != NULL ?
                         ZpWindowCapture_EncodeFrame(Source->Device, Frame, &Object->Value) :
                         E_OUTOFMEMORY;
            if (SUCCEEDED(Result))
            {
                Object->ReferenceCount = 1;
                if (Source->ImageSequence <= Sequence)
                {
                    Previous = Source->Image;
                    Source->Image = Object;
                    Source->ImageSequence = Sequence;
                    InterlockedIncrement(&Object->ReferenceCount);
                }
            }
            else
            {
                Mem_Free(Object);
                Object = NULL;
            }
        }
        RtlReleaseSRWLockExclusive(&Source->ConvertLock);
        ZpWindowCapture_ReleaseFrame(Frame);
        if (Previous != NULL) ZpWindowShared_ReleaseImage(Previous);
        if (SUCCEEDED(Result)) *Image = Object;
    }
    return Result;
}

HRESULT
ZpWindowShared_Update(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _In_ ULONG MaxDimension,
    _In_ BYTE FrameRate,
    _In_ BYTE Quality)
{
    PZP_WINDOW_SHARED_SOURCE Source = Capture->Source;
    ZP_WINDOW_CAPTURE_OPTIONS Previous = Capture->Options;
    HRESULT Result;

    RtlAcquireSRWLockExclusive(&Source->Lock);
    Capture->Options.MaxDimension = MaxDimension;
    Capture->Options.FrameRate = FrameRate;
    Capture->Options.Quality = Quality;
    Result = ZpWindowShared_UpdateOptions(Source);
    if (FAILED(Result)) Capture->Options = Previous;
    RtlReleaseSRWLockExclusive(&Source->Lock);
    return Result;
}

VOID
ZpWindowShared_RequestKeyFrame(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture)
{
    PZP_WINDOW_SHARED_SOURCE Source = Capture->Source;

    RtlAcquireSRWLockExclusive(&Source->Lock);
    RtlAcquireSRWLockExclusive(&Source->ConvertLock);
    Capture->Sequence = 0;
    Source->ImageSequence = 0;
    ZpWindowCapture_RequestKeyFrame(Source->Device);
    NtSetEvent(Capture->Event, NULL);
    RtlReleaseSRWLockExclusive(&Source->ConvertLock);
    RtlReleaseSRWLockExclusive(&Source->Lock);
}

VOID
ZpWindowShared_GetFormat(
    _In_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    RtlAcquireSRWLockExclusive(&Capture->Source->ConvertLock);
    ZpWindowCapture_GetFormat(Capture->Source->Device, Width, Height);
    RtlReleaseSRWLockExclusive(&Capture->Source->ConvertLock);
}

IMFDXGIDeviceManager*
ZpWindowShared_GetDeviceManager(
    _In_ PZP_WINDOW_SHARED_CAPTURE Capture)
{
    return ZpWindowCapture_GetDeviceManager(Capture->Source->Device);
}

_Success_(return == S_OK)
HRESULT
ZpWindowShared_NextSample(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Outptr_ IMFSample** Sample,
    _Out_ PULONGLONG Timestamp,
    _Out_opt_ PBYTE ChangeRate)
{
    PZP_WINDOW_CAPTURE_FRAME Frame;
    HRESULT Result;

    ULONGLONG Sequence;

    Result = ZpWindowShared_NextFrame(Capture, TimeoutMilliseconds, &Frame, &Sequence, NULL);
    if (Result == S_OK)
    {
        RtlAcquireSRWLockExclusive(&Capture->Source->ConvertLock);
        if (ChangeRate != NULL) Result = ZpWindowCapture_GetFrameChangeRate(Frame, ChangeRate);
        if (SUCCEEDED(Result))
        {
            Result = ZpWindowCapture_CreateSample(Capture->Source->Device, Frame, Sample, Timestamp);
        }
        RtlReleaseSRWLockExclusive(&Capture->Source->ConvertLock);
        ZpWindowCapture_ReleaseFrame(Frame);
    }
    return Result;
}

VOID
ZpWindowShared_Close(
    _In_opt_ PZP_WINDOW_SHARED_CAPTURE Capture)
{
    PZP_WINDOW_SHARED_SOURCE Source;
    LOGICAL Last;

    if (Capture == NULL) return;
    Source = Capture->Source;
    RtlAcquireSRWLockExclusive(&ZpWindowSharedLock);
    RtlAcquireSRWLockExclusive(&Source->Lock);
    RemoveEntryList(&Capture->ListEntry);
    Last = --Source->CaptureCount == 0;
    if (Last) RemoveEntryList(&Source->ListEntry);
    else ZpWindowShared_UpdateOptions(Source);
    RtlReleaseSRWLockExclusive(&Source->Lock);
    RtlReleaseSRWLockExclusive(&ZpWindowSharedLock);
    NtClose(Capture->Event);
    Mem_Free(Capture);
    if (Last) ZpWindowShared_FreeSource(Source);
}
