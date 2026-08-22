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
    HRESULT Result;
    ULONGLONG Sequence;
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
    ULONG FrameRate = 0;

    Options->Flags = Source->Flags;
    Options->MonitorIndex = Source->MonitorIndex;
    Options->MaxDimension = 0;
    Options->Quality = 0;
    for (Entry = Source->Captures.Flink; Entry != &Source->Captures; Entry = Entry->Flink)
    {
        Capture = CONTAINING_RECORD(Entry, ZP_WINDOW_SHARED_CAPTURE, ListEntry);
        Options->MaxDimension = max(Options->MaxDimension, Capture->Options.MaxDimension);
        Options->Quality = max(Options->Quality, Capture->Options.Quality);
        FrameRate += Capture->Options.FrameRate;
    }
    Options->FrameRate = (USHORT)min(FrameRate, 120UL);
}

static
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
        RtlReleaseSRWLockExclusive(&Source->Lock);
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
HRESULT
ZpWindowShared_NextFrame(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_CAPTURE_FRAME* Frame)
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
            Capture->Sequence = Source->Sequence;
            *Frame = Source->Frame;
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

HRESULT
ZpWindowShared_Next(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_CAPTURE_IMAGE Image)
{
    PZP_WINDOW_CAPTURE_FRAME Frame;
    HRESULT Result;

    Result = ZpWindowShared_NextFrame(Capture, TimeoutMilliseconds, &Frame);
    if (Result == S_OK)
    {
        RtlAcquireSRWLockExclusive(&Capture->Source->ConvertLock);
        Result = ZpWindowCapture_EncodeFrame(Capture->Source->Device, Frame, Image);
        RtlReleaseSRWLockExclusive(&Capture->Source->ConvertLock);
        ZpWindowCapture_ReleaseFrame(Frame);
    }
    return Result;
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

HRESULT
ZpWindowShared_NextSample(
    _Inout_ PZP_WINDOW_SHARED_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Outptr_ IMFSample** Sample,
    _Out_ PULONGLONG Timestamp)
{
    PZP_WINDOW_CAPTURE_FRAME Frame;
    HRESULT Result;

    Result = ZpWindowShared_NextFrame(Capture, TimeoutMilliseconds, &Frame);
    if (Result == S_OK)
    {
        RtlAcquireSRWLockExclusive(&Capture->Source->ConvertLock);
        Result = ZpWindowCapture_CreateSample(Capture->Source->Device, Frame, Sample, Timestamp);
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
