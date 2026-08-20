#define COBJMACROS

#include "Client.h"
#include "Writer.h"
#include "../Audio/Shared.h"
#include "../Video/Shared.h"
#include "../Window/Shared.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"

#include <mfapi.h>
#include <strsafe.h>

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#define ZP_RECORDING_DEFAULT_AUDIO_BIT_RATE 160000
#define ZP_RECORDING_DEFAULT_VIDEO_BIT_RATE 4000000
#define ZP_RECORDING_DEFAULT_FRAME_RATE 30
#define ZP_RECORDING_DEFAULT_MAX_DIMENSION 1920

typedef struct _ZP_RECORDING_JOB
{
    LIST_ENTRY ListEntry;
    PZP_CLIENT_OBJECT Owner;
    HANDLE Thread;
    HANDLE AudioThread;
    HANDLE StopEvent;
    PZP_MEDIA_WRITER Writer;
    PZP_AUDIO_SHARED_CAPTURE AudioCapture;
    ULONG RecordingId;
    ZP_RECORDING_SOURCE Source;
    ZP_RECORDING_CODEC Codec;
    ZP_RECORDING_STATE State;
    ZP_STATUS Status;
    ULONGLONG StartTime;
    ULONGLONG MediaStartTimestamp;
    ULONGLONG Duration;
    ULONGLONG FileSize;
    USHORT Flags;
    ULONG MaxDimension;
    ULONG VideoBitRate;
    ULONG AudioBitRate;
    ULONGLONG WindowHandle;
    USHORT FrameRate;
    ZP_RECORDING_AUDIO_SOURCE AudioSource;
    USHORT AudioChannels;
    ULONG AudioSampleRate;
    ULONGLONG AudioFirstTimestamp;
    ULONGLONG AudioStartTimestamp;
    PWSTR SourceId;
    ULONG SourceIdLength;
    PWSTR AudioDeviceId;
    ULONG AudioDeviceIdLength;
    PWSTR Path;
    ULONG PathLength;
    HRESULT MediaResult;
    LOGICAL AudioTimestampStarted;
    LOGICAL Interrupted;
} ZP_RECORDING_JOB, *PZP_RECORDING_JOB;

static
ULONGLONG
ZpRecording_QueryTimestamp(VOID)
{
    LARGE_INTEGER Counter, Frequency;

    NtQueryPerformanceCounter(&Counter, &Frequency);
    return (ULONGLONG)(Counter.QuadPart / Frequency.QuadPart) * 10000000 +
           (ULONGLONG)(Counter.QuadPart % Frequency.QuadPart) * 10000000 / Frequency.QuadPart;
}

static
PWSTR
ZpRecording_CopyString(
    _In_reads_opt_(Length) PCWCH Value,
    _In_ ULONG Length)
{
    PWSTR Copy;

    if (Length == 0) return NULL;
    Copy = Mem_Alloc(((SIZE_T)Length + 1) * sizeof(WCHAR));
    if (Copy == NULL) return NULL;
    RtlCopyMemory(Copy, Value, (SIZE_T)Length * sizeof(WCHAR));
    Copy[Length] = UNICODE_NULL;
    return Copy;
}

static
HRESULT
ZpRecording_CreatePath(
    _In_ ULONG RecordingId,
    _In_ ZP_RECORDING_CODEC Codec,
    _Outptr_ PWSTR* Path,
    _Out_ PULONG PathLength)
{
    PCWSTR Extension = Codec == ZpRecordingCodecPcm ? L"wav" :
                        Codec == ZpRecordingCodecAac ? L"m4a" :
                        Codec == ZpRecordingCodecWmvScreen ? L"wmv" : L"mp4";
    PWSTR Buffer;
    ULONG TempLength, Capacity;
    HRESULT Result;

    TempLength = GetTempPath2W(0, NULL);
    if (TempLength == 0) return HRESULT_FROM_WIN32(GetLastError());
    Capacity = TempLength + 64;
    Buffer = Mem_Alloc((SIZE_T)Capacity * sizeof(WCHAR));
    if (Buffer == NULL) return E_OUTOFMEMORY;
    TempLength = GetTempPath2W(Capacity, Buffer);
    if (TempLength == 0 || TempLength >= Capacity)
    {
        Result = HRESULT_FROM_WIN32(TempLength == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);
        Mem_Free(Buffer);
        return Result;
    }
    Result = StringCchPrintfW(Buffer + TempLength,
                              Capacity - TempLength,
                              L"ZPigeon-Recording-%lu-%llu.%s",
                              RecordingId,
                              GetTickCount64(),
                              Extension);
    if (FAILED(Result))
    {
        Mem_Free(Buffer);
        return Result;
    }
    *PathLength = (ULONG)wcslen(Buffer);
    *Path = Buffer;
    return S_OK;
}

static
ZP_RECORDING_CODEC
ZpRecording_ResolveCodec(
    _In_ ZP_RECORDING_SOURCE Source,
    _In_ ZP_RECORDING_CODEC Codec,
    _In_ ULONG Capabilities)
{
    if (Codec != ZpRecordingCodecAuto) return Codec;
    if (Source == ZpRecordingSourceAudioOutput || Source == ZpRecordingSourceAudioInput)
    {
        return FlagOn(Capabilities, ZP_RECORDING_CODEC_CAPABILITY(ZpRecordingCodecAac)) ?
                   ZpRecordingCodecAac : ZpRecordingCodecPcm;
    }
    if (Source == ZpRecordingSourceWindow)
    {
        if (FlagOn(Capabilities, ZP_RECORDING_CODEC_CAPABILITY(ZpRecordingCodecH265)))
        {
            return ZpRecordingCodecH265;
        }
        if (FlagOn(Capabilities, ZP_RECORDING_CODEC_CAPABILITY(ZpRecordingCodecH264)))
        {
            return ZpRecordingCodecH264;
        }
        return FlagOn(Capabilities, ZP_RECORDING_CODEC_CAPABILITY(ZpRecordingCodecWmvScreen)) ?
                   ZpRecordingCodecWmvScreen : 0;
    }
    if (FlagOn(Capabilities, ZP_RECORDING_CODEC_CAPABILITY(ZpRecordingCodecH265)))
    {
        return ZpRecordingCodecH265;
    }
    return FlagOn(Capabilities, ZP_RECORDING_CODEC_CAPABILITY(ZpRecordingCodecH264)) ?
               ZpRecordingCodecH264 : 0;
}

static
LOGICAL
ZpRecording_IsConfigurationValid(
    _In_ PCZP_RECORDING_START_VIEW Start,
    _In_ ZP_RECORDING_CODEC Codec,
    _In_ ULONG Capabilities)
{
    if (Codec == 0 || !FlagOn(Capabilities, ZP_RECORDING_CODEC_CAPABILITY(Codec))) return FALSE;
    if (Start->FrameRate > 120 || Start->MaxDimension > 7680 || Start->VideoBitRate > 100000000 ||
        Start->AudioBitRate > 1000000 ||
        (Start->AudioSource == ZpRecordingAudioNone && Start->AudioDeviceId.Length != 0)) return FALSE;
    if (Start->Source == ZpRecordingSourceAudioOutput || Start->Source == ZpRecordingSourceAudioInput)
    {
        return Start->Flags == 0 && Start->AudioSource == ZpRecordingAudioNone && Start->WindowHandle == 0 &&
               Start->FrameRate == 0 && Start->MaxDimension == 0 && Start->VideoBitRate == 0 &&
               Start->AudioDeviceId.Length == 0 &&
               (Codec == ZpRecordingCodecPcm || Codec == ZpRecordingCodecAac);
    }
    if (Start->Source == ZpRecordingSourceCamera)
    {
        return !FlagOn(Start->Flags, ZP_RECORDING_FLAG_CAPTURE_CURSOR) && Start->WindowHandle == 0 &&
               Start->SourceId.Length != 0 &&
               (Codec == ZpRecordingCodecH264 || Codec == ZpRecordingCodecH265);
    }
    return Start->WindowHandle != 0 && Start->SourceId.Length == 0 &&
           (Codec == ZpRecordingCodecH264 || Codec == ZpRecordingCodecH265 ||
            Codec == ZpRecordingCodecWmvScreen);
}

static
VOID
ZpRecording_SetResult(
    _Inout_ PZP_RECORDING_JOB Job,
    _In_ ZP_RECORDING_STATE State,
    _In_ ZP_STATUS Status)
{
    LARGE_INTEGER Time;

    NtQuerySystemTime(&Time);
    RtlAcquireSRWLockExclusive(&Job->Owner->RecordingLock);
    Job->State = State;
    Job->Status = Status;
    Job->Duration = Job->MediaStartTimestamp == 0 ? (ULONGLONG)Time.QuadPart - Job->StartTime :
                                                    ZpRecording_QueryTimestamp() - Job->MediaStartTimestamp;
    RtlReleaseSRWLockExclusive(&Job->Owner->RecordingLock);
}

static
NTSTATUS
NTAPI
ZpRecording_WriteAudio(
    _In_ USHORT Channels,
    _In_ ULONG SampleRate,
    _In_ ULONG FrameCount,
    _In_reads_(FrameCount * Channels) const SHORT* Samples,
    _In_ ULONGLONG Timestamp,
    _In_opt_ PVOID Context)
{
    PZP_RECORDING_JOB Job = Context;
    ULONGLONG Delta;
    HRESULT Result;

    if (!Job->AudioTimestampStarted)
    {
        Job->AudioFirstTimestamp = Timestamp;
        Job->AudioStartTimestamp = ZpRecording_QueryTimestamp();
        Job->AudioTimestampStarted = TRUE;
    }
    if (Timestamp < Job->AudioFirstTimestamp) return STATUS_DATA_ERROR;
    Delta = Timestamp - Job->AudioFirstTimestamp;
    if (Delta > MAXULONGLONG - Job->AudioStartTimestamp) return STATUS_INTEGER_OVERFLOW;
    Result = ZpMediaWriter_WriteAudio(Job->Writer,
                                      Samples,
                                      Channels,
                                      SampleRate,
                                      FrameCount,
                                      Job->AudioStartTimestamp + Delta);
    if (FAILED(Result))
    {
        RtlAcquireSRWLockExclusive(&Job->Owner->RecordingLock);
        if (SUCCEEDED(Job->MediaResult)) Job->MediaResult = Result;
        RtlReleaseSRWLockExclusive(&Job->Owner->RecordingLock);
        NtSetEvent(Job->StopEvent, NULL);
        return STATUS_UNSUCCESSFUL;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
ZpRecording_AudioThread(
    _In_ PVOID Context)
{
    PZP_RECORDING_JOB Job = Context;
    PZP_AUDIO_SHARED_FRAME Frame;
    HRESULT Result = S_OK;
    NTSTATUS Status = STATUS_SUCCESS;

    while (SUCCEEDED(Result) && NT_SUCCESS(Status))
    {
        Result = ZpAudioShared_Next(Job->AudioCapture, Job->StopEvent, &Frame);
        if (FAILED(Result)) break;
        Status = ZpRecording_WriteAudio(Frame->Channels,
                                        Frame->SampleRate,
                                        Frame->FrameCount,
                                        Frame->Samples,
                                        Frame->Timestamp,
                                        Job);
        ZpAudioShared_ReleaseFrame(Frame);
    }
    ZpAudioShared_Close(Job->AudioCapture);
    Job->AudioCapture = NULL;
    if (Result != HRESULT_FROM_NT(STATUS_CANCELLED) &&
        (FAILED(Result) || !NT_SUCCESS(Status)))
    {
        RtlAcquireSRWLockExclusive(&Job->Owner->RecordingLock);
        if (SUCCEEDED(Job->MediaResult))
        {
            Job->MediaResult = FAILED(Result) ? Result : HRESULT_FROM_NT(Status);
        }
        RtlReleaseSRWLockExclusive(&Job->Owner->RecordingLock);
        NtSetEvent(Job->StopEvent, NULL);
    }
    return Status;
}

static
HRESULT
ZpRecording_RunAudio(
    _Inout_ PZP_RECORDING_JOB Job)
{
    ZP_AUDIO_FLOW Flow = Job->Source == ZpRecordingSourceAudioOutput ? ZpAudioFlowRender : ZpAudioFlowCapture;
    PZP_AUDIO_SHARED_FRAME Frame;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;

    Result = ZpAudioShared_Open(Flow,
                                Job->SourceId,
                                Job->SourceIdLength,
                                &Job->AudioCapture);
    if (SUCCEEDED(Result)) ZpAudioShared_GetFormat(Job->AudioCapture,
                                                   &Job->AudioChannels,
                                                   &Job->AudioSampleRate);
    if (SUCCEEDED(Result))
    {
        Result = ZpMediaWriter_CreateAudio(Job->Path,
                                           Job->Codec,
                                           Job->AudioChannels,
                                           Job->AudioSampleRate,
                                           Job->AudioBitRate,
                                           &Job->Writer);
    }
    if (SUCCEEDED(Result))
    {
        Job->MediaStartTimestamp = ZpRecording_QueryTimestamp();
        while (SUCCEEDED(Result) && NT_SUCCESS(Status))
        {
            Result = ZpAudioShared_Next(Job->AudioCapture, Job->StopEvent, &Frame);
            if (FAILED(Result)) break;
            Status = ZpRecording_WriteAudio(Frame->Channels,
                                            Frame->SampleRate,
                                            Frame->FrameCount,
                                            Frame->Samples,
                                            Frame->Timestamp,
                                            Job);
            ZpAudioShared_ReleaseFrame(Frame);
        }
        if (Result == HRESULT_FROM_NT(STATUS_CANCELLED)) Result = S_OK;
        else if (SUCCEEDED(Result) && !NT_SUCCESS(Status)) Result = HRESULT_FROM_NT(Status);
    }
    ZpAudioShared_Close(Job->AudioCapture);
    Job->AudioCapture = NULL;
    return Result;
}

static
HRESULT
ZpRecording_StartAudioTrack(
    _Inout_ PZP_RECORDING_JOB Job)
{
    HRESULT Result;

    if (Job->AudioSource == ZpRecordingAudioNone) return S_OK;
    Result = ZpAudioShared_Open(Job->AudioSource == ZpRecordingAudioOutput ?
                                   ZpAudioFlowRender : ZpAudioFlowCapture,
                               Job->AudioDeviceId,
                               Job->AudioDeviceIdLength,
                               &Job->AudioCapture);
    if (SUCCEEDED(Result)) ZpAudioShared_GetFormat(Job->AudioCapture,
                                                   &Job->AudioChannels,
                                                   &Job->AudioSampleRate);
    return Result;
}

static
HRESULT
ZpRecording_CreateAudioThread(
    _Inout_ PZP_RECORDING_JOB Job)
{
    NTSTATUS Status;

    if (Job->AudioSource == ZpRecordingAudioNone) return S_OK;
    Status = PS_CreateThread(NtCurrentProcess(), FALSE, ZpRecording_AudioThread, Job, &Job->AudioThread, NULL);
    return NT_SUCCESS(Status) ? S_OK : HRESULT_FROM_NT(Status);
}

static
HRESULT
ZpRecording_CloneSample(
    _In_ IMFSample* Source,
    _Outptr_ IMFSample** Sample)
{
    IMFSample* Copy;
    IMFMediaBuffer* Buffer;
    DWORD BufferCount, Index;
    HRESULT Result;

    Result = MFCreateSample(&Copy);
    if (FAILED(Result)) return Result;
    Result = IMFSample_GetBufferCount(Source, &BufferCount);
    for (Index = 0; SUCCEEDED(Result) && Index < BufferCount; Index++)
    {
        Result = IMFSample_GetBufferByIndex(Source, Index, &Buffer);
        if (SUCCEEDED(Result))
        {
            Result = IMFSample_AddBuffer(Copy, Buffer);
            IMFMediaBuffer_Release(Buffer);
        }
    }
    if (SUCCEEDED(Result)) *Sample = Copy;
    else IMFSample_Release(Copy);
    return Result;
}

static
HRESULT
ZpRecording_RunCamera(
    _Inout_ PZP_RECORDING_JOB Job)
{
    PZP_VIDEO_SHARED_CAPTURE Capture = NULL;
    ZP_VIDEO_STREAM_REQUEST_VIEW Request;
    IMFSample* SourceSample = NULL;
    IMFSample* Sample = NULL;
    ULONG Width, Height, OutputWidth, OutputHeight;
    USHORT SourceFrameRate;
    LONGLONG MediaTimestamp;
    ULONGLONG Timestamp, TimestampBase = 0;
    LARGE_INTEGER ZeroTimeout = { 0 };
    LOGICAL TimestampStarted = FALSE;
    HRESULT Result;

    Result = ZpRecording_StartAudioTrack(Job);
    Request.DeviceId.Buffer = (const BYTE*)Job->SourceId;
    Request.DeviceId.Length = Job->SourceIdLength;
    Request.MaxDimension = Job->MaxDimension;
    Request.FrameRate = Job->FrameRate;
    Request.Quality = 85;
    if (SUCCEEDED(Result))
    {
        Result = ZpVideoShared_Open(&Request, &Capture);
    }
    if (SUCCEEDED(Result))
    {
        ZpVideoShared_GetFormat(Capture, &Width, &Height, &SourceFrameRate);
        UNREFERENCED_PARAMETER(SourceFrameRate);
        OutputWidth = Width;
        OutputHeight = Height;
    }
    if (SUCCEEDED(Result) && max(OutputWidth, OutputHeight) > Job->MaxDimension)
    {
        if (OutputWidth >= OutputHeight)
        {
            OutputHeight = (ULONG)((ULONGLONG)OutputHeight * Job->MaxDimension / OutputWidth);
            OutputWidth = Job->MaxDimension;
        }
        else
        {
            OutputWidth = (ULONG)((ULONGLONG)OutputWidth * Job->MaxDimension / OutputHeight);
            OutputHeight = Job->MaxDimension;
        }
        OutputWidth &= ~1UL;
        OutputHeight &= ~1UL;
    }
    if (SUCCEEDED(Result))
    {
        Result = ZpMediaWriter_CreateVideo(Job->Path,
                                           Job->Codec,
                                           &MFVideoFormat_RGB32,
                                           Width,
                                           Height,
                                           OutputWidth,
                                           OutputHeight,
                                           Job->FrameRate,
                                           Job->VideoBitRate,
                                           Job->AudioChannels,
                                           Job->AudioSampleRate,
                                           Job->AudioBitRate,
                                           NULL,
                                           &Job->Writer);
    }
    if (SUCCEEDED(Result))
    {
        Job->MediaStartTimestamp = ZpRecording_QueryTimestamp();
        Result = ZpRecording_CreateAudioThread(Job);
    }
    while (SUCCEEDED(Result) && NtWaitForSingleObject(Job->StopEvent, FALSE, &ZeroTimeout) == STATUS_TIMEOUT)
    {
        Result = ZpVideoShared_NextSample(Capture,
                                          Job->StopEvent,
                                          &SourceSample,
                                          &MediaTimestamp);
        if (SUCCEEDED(Result)) Result = ZpRecording_CloneSample(SourceSample, &Sample);
        if (SUCCEEDED(Result))
        {
            if (MediaTimestamp < 0)
            {
                Result = E_INVALIDARG;
            }
            else
            {
                if (!TimestampStarted)
                {
                    Timestamp = ZpRecording_QueryTimestamp();
                    if ((ULONGLONG)MediaTimestamp > Timestamp) Result = E_INVALIDARG;
                    else
                    {
                        TimestampBase = Timestamp - (ULONGLONG)MediaTimestamp;
                        TimestampStarted = TRUE;
                    }
                }
                if (SUCCEEDED(Result))
                {
                    Timestamp = TimestampBase + (ULONGLONG)MediaTimestamp;
                    Result = ZpMediaWriter_WriteVideo(Job->Writer,
                                                      Sample,
                                                      Timestamp,
                                                      10000000ULL / Job->FrameRate);
                }
            }
        }
        if (Sample != NULL)
        {
            IMFSample_Release(Sample);
            Sample = NULL;
        }
        if (SourceSample != NULL)
        {
            IMFSample_Release(SourceSample);
            SourceSample = NULL;
        }
    }
    if (SourceSample != NULL) IMFSample_Release(SourceSample);
    if (Result == HRESULT_FROM_NT(STATUS_CANCELLED)) Result = S_OK;
    ZpVideoShared_Close(Capture);
    return Result;
}

static
HRESULT
ZpRecording_RunWindow(
    _Inout_ PZP_RECORDING_JOB Job)
{
    ZP_WINDOW_CAPTURE_OPTIONS Options;
    PZP_WINDOW_SHARED_CAPTURE Capture = NULL;
    IMFSample* Sample = NULL;
    IMFSample* LastSample = NULL;
    ULONG Width, Height;
    ULONGLONG Timestamp, FirstTimestamp, StartTimestamp, LastTimestamp = 0, FrameDuration, Delta;
    LARGE_INTEGER ZeroTimeout = { 0 };
    LOGICAL TimestampStarted = FALSE;
    HRESULT Result;

    Result = ZpRecording_StartAudioTrack(Job);
    Options.Flags = FlagOn(Job->Flags, ZP_RECORDING_FLAG_CAPTURE_CURSOR) ? ZP_WINDOW_CAPTURE_CURSOR : 0;
    Options.MaxDimension = Job->MaxDimension;
    Options.FrameRate = Job->FrameRate;
    Options.Quality = 85;
    if (SUCCEEDED(Result)) Result = ZpWindowShared_Open((HWND)(ULONG_PTR)Job->WindowHandle, &Options, &Capture);
    if (SUCCEEDED(Result)) ZpWindowShared_GetFormat(Capture, &Width, &Height);
    if (SUCCEEDED(Result))
    {
        Result = ZpMediaWriter_CreateVideo(Job->Path,
                                           Job->Codec,
                                           &MFVideoFormat_NV12,
                                           Width,
                                           Height,
                                           Width,
                                           Height,
                                           Job->FrameRate,
                                           Job->VideoBitRate,
                                           Job->AudioChannels,
                                           Job->AudioSampleRate,
                                           Job->AudioBitRate,
                                           (IUnknown*)ZpWindowShared_GetDeviceManager(Capture),
                                           &Job->Writer);
    }
    if (SUCCEEDED(Result))
    {
        Job->MediaStartTimestamp = ZpRecording_QueryTimestamp();
        Result = ZpRecording_CreateAudioThread(Job);
    }
    FrameDuration = 10000000ULL / Job->FrameRate;
    while (SUCCEEDED(Result) && NtWaitForSingleObject(Job->StopEvent, FALSE, &ZeroTimeout) == STATUS_TIMEOUT)
    {
        Result = ZpWindowShared_NextSample(Capture, 1000, &Sample, &Timestamp);
        if (Result == HRESULT_FROM_WIN32(ERROR_TIMEOUT) || Result == S_FALSE)
        {
            Result = S_OK;
            continue;
        }
        if (SUCCEEDED(Result))
        {
            if (!TimestampStarted)
            {
                FirstTimestamp = Timestamp;
                StartTimestamp = ZpRecording_QueryTimestamp();
                TimestampStarted = TRUE;
            }
            if (Timestamp < FirstTimestamp || Timestamp - FirstTimestamp > MAXULONGLONG - StartTimestamp)
            {
                Result = E_INVALIDARG;
            }
            else
            {
                Delta = Timestamp - FirstTimestamp;
                Timestamp = StartTimestamp + Delta;
            }
        }
        if (SUCCEEDED(Result))
        {
            Result = ZpMediaWriter_WriteVideo(Job->Writer,
                                              Sample,
                                              Timestamp,
                                              FrameDuration);
            if (SUCCEEDED(Result))
            {
                if (LastSample != NULL) IMFSample_Release(LastSample);
                LastSample = Sample;
                IMFSample_AddRef(LastSample);
                LastTimestamp = Timestamp;
            }
        }
        if (Sample != NULL)
        {
            IMFSample_Release(Sample);
            Sample = NULL;
        }
    }
    Timestamp = ZpRecording_QueryTimestamp();
    if (SUCCEEDED(Result) && LastSample != NULL && Timestamp > LastTimestamp + FrameDuration)
    {
        Result = ZpRecording_CloneSample(LastSample, &Sample);
        if (SUCCEEDED(Result)) Result = ZpMediaWriter_WriteVideo(Job->Writer, Sample, Timestamp, FrameDuration);
    }
    if (Sample != NULL) IMFSample_Release(Sample);
    if (LastSample != NULL) IMFSample_Release(LastSample);
    ZpWindowShared_Close(Capture);
    return Result;
}

static
VOID
ZpRecording_QueryFileSize(
    _Inout_ PZP_RECORDING_JOB Job)
{
    HANDLE File;
    ULONGLONG Size;

    if (NT_SUCCESS(IO_CreateWin32File(&File,
                                      Job->Path,
                                      NULL,
                                      FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      FILE_OPEN,
                                      FILE_SYNCHRONOUS_IO_NONALERT)))
    {
        if (NT_SUCCESS(IO_GetFileSize(File, &Size)))
        {
            RtlAcquireSRWLockExclusive(&Job->Owner->RecordingLock);
            Job->FileSize = Size;
            RtlReleaseSRWLockExclusive(&Job->Owner->RecordingLock);
        }
        NtClose(File);
    }
}

static
NTSTATUS
NTAPI
ZpRecording_Worker(
    _In_ PVOID Context)
{
    PZP_RECORDING_JOB Job = Context;
    HRESULT Result, FinalizeResult;
    LOGICAL Interrupted;

    Job->MediaResult = S_OK;
    Result = Job->Source == ZpRecordingSourceAudioOutput || Job->Source == ZpRecordingSourceAudioInput ?
                 ZpRecording_RunAudio(Job) :
             Job->Source == ZpRecordingSourceCamera ? ZpRecording_RunCamera(Job) : ZpRecording_RunWindow(Job);
    NtSetEvent(Job->StopEvent, NULL);
    if (Job->AudioThread != NULL) NtWaitForSingleObject(Job->AudioThread, FALSE, NULL);
    else
    {
        ZpAudioShared_Close(Job->AudioCapture);
        Job->AudioCapture = NULL;
    }
    if (SUCCEEDED(Result) && FAILED(Job->MediaResult)) Result = Job->MediaResult;
    ZpRecording_SetResult(Job,
                          ZpRecordingStateFinalizing,
                          FAILED(Result) ? ZpStatus_FromCode(ZpStatusHResult, Result) :
                                           ZpStatus_FromNtStatus(STATUS_SUCCESS));
    if (SUCCEEDED(Result) && Job->Writer != NULL)
    {
        Result = ZpMediaWriter_FillAudioSilence(Job->Writer, Job->Duration);
    }
    FinalizeResult = Job->Writer == NULL ? S_OK : ZpMediaWriter_Finalize(Job->Writer);
    if (SUCCEEDED(Result) && FAILED(FinalizeResult)) Result = FinalizeResult;
    ZpMediaWriter_Close(Job->Writer);
    Job->Writer = NULL;
    ZpRecording_QueryFileSize(Job);
    RtlAcquireSRWLockShared(&Job->Owner->RecordingLock);
    Interrupted = Job->Interrupted;
    RtlReleaseSRWLockShared(&Job->Owner->RecordingLock);
    ZpRecording_SetResult(Job,
                          FAILED(Result) ? ZpRecordingStateFailed :
                          Interrupted ? ZpRecordingStateInterrupted : ZpRecordingStateCompleted,
                          FAILED(Result) ? ZpStatus_FromCode(ZpStatusHResult, Result) :
                                           ZpStatus_FromNtStatus(STATUS_SUCCESS));
    return FAILED(Result) ? (NTSTATUS)Result : STATUS_SUCCESS;
}

static
VOID
ZpRecording_FreeJob(
    _In_ PZP_RECORDING_JOB Job)
{
    if (Job->Thread != NULL)
    {
        NtSetEvent(Job->StopEvent, NULL);
        NtWaitForSingleObject(Job->Thread, FALSE, NULL);
        NtClose(Job->Thread);
    }
    if (Job->AudioThread != NULL) NtClose(Job->AudioThread);
    ZpAudioShared_Close(Job->AudioCapture);
    ZpMediaWriter_Close(Job->Writer);
    if (Job->StopEvent != NULL) NtClose(Job->StopEvent);
    Mem_Free(Job->Path);
    Mem_Free(Job->AudioDeviceId);
    Mem_Free(Job->SourceId);
    Mem_Free(Job);
}

static
NTSTATUS
ZpRecording_EncodeJobs(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_opt_ PZP_RECORDING_JOB Single,
    _Outptr_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_RECORDING_RECORD Records;
    PZP_RECORDING_JOB Job;
    PLIST_ENTRY Entry;
    LARGE_INTEGER Time;
    ULONG Count, Index = 0, Length;
    NTSTATUS Status;

    RtlAcquireSRWLockShared(&Client->RecordingLock);
    Count = Single == NULL ? Client->RecordingJobCount : 1;
    Records = Mem_Alloc((SIZE_T)Count * sizeof(*Records));
    if (Records == NULL && Count != 0)
    {
        RtlReleaseSRWLockShared(&Client->RecordingLock);
        return STATUS_NO_MEMORY;
    }
    NtQuerySystemTime(&Time);
    Entry = Single == NULL ? Client->RecordingJobs.Flink : &Single->ListEntry;
    while (Index < Count)
    {
        Job = Single == NULL ? CONTAINING_RECORD(Entry, ZP_RECORDING_JOB, ListEntry) : Single;
        Records[Index].RecordingId = Job->RecordingId;
        Records[Index].Source = Job->Source;
        Records[Index].Codec = Job->Codec;
        Records[Index].State = Job->State;
        Records[Index].Status = Job->Status;
        Records[Index].StartTime = Job->StartTime;
        Records[Index].Duration = Job->State == ZpRecordingStateRecording ?
                                      (ULONGLONG)Time.QuadPart - Job->StartTime : Job->Duration;
        Records[Index].FileSize = Job->FileSize;
        Records[Index].Path = Job->Path;
        Records[Index].PathLength = Job->PathLength;
        Index++;
        if (Single == NULL) Entry = Entry->Flink;
    }
    Status = ZpRecording_EncodeRecords(Records, Count, NULL, 0, &Length);
    *Response = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && *Response == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status)) Status = ZpRecording_EncodeRecords(Records, Count, *Response, Length, ResponseLength);
    RtlReleaseSRWLockShared(&Client->RecordingLock);
    Mem_Free(Records);
    return Status;
}

static
ZP_STATUS
ZpRecording_Start(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PCZP_RECORDING_START_VIEW Start,
    _Outptr_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_RECORDING_JOB Job;
    ULONG Capabilities, RecordingId;
    ZP_RECORDING_CODEC Codec;
    LARGE_INTEGER Time;
    HRESULT Result;
    NTSTATUS Status;

    Result = ZpMediaWriter_QueryCodecs(&Capabilities);
    if (FAILED(Result)) return ZpStatus_FromCode(ZpStatusHResult, Result);
    Codec = ZpRecording_ResolveCodec(Start->Source, Start->Codec, Capabilities);
    if (!ZpRecording_IsConfigurationValid(Start, Codec, Capabilities))
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Job = Mem_Alloc(sizeof(*Job));
    if (Job == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    RtlZeroMemory(Job, sizeof(*Job));
    Job->Owner = Client;
    Job->Source = Start->Source;
    Job->Codec = Codec;
    Job->State = ZpRecordingStateRecording;
    Job->Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    Job->Flags = Start->Flags;
    Job->MaxDimension = Start->MaxDimension == 0 ? ZP_RECORDING_DEFAULT_MAX_DIMENSION : Start->MaxDimension;
    Job->FrameRate = Start->FrameRate == 0 ? ZP_RECORDING_DEFAULT_FRAME_RATE : Start->FrameRate;
    Job->AudioSource = Start->AudioSource;
    Job->VideoBitRate = Start->VideoBitRate == 0 ? ZP_RECORDING_DEFAULT_VIDEO_BIT_RATE : Start->VideoBitRate;
    Job->AudioBitRate = Start->AudioBitRate == 0 ? ZP_RECORDING_DEFAULT_AUDIO_BIT_RATE : Start->AudioBitRate;
    Job->WindowHandle = Start->WindowHandle;
    Job->SourceIdLength = Start->SourceId.Length;
    Job->AudioDeviceIdLength = Start->AudioDeviceId.Length;
    Job->SourceId = ZpRecording_CopyString((PCWCH)Start->SourceId.Buffer, Start->SourceId.Length);
    Job->AudioDeviceId = ZpRecording_CopyString((PCWCH)Start->AudioDeviceId.Buffer,
                                                 Start->AudioDeviceId.Length);
    if ((Job->SourceIdLength != 0 && Job->SourceId == NULL) ||
        (Job->AudioDeviceIdLength != 0 && Job->AudioDeviceId == NULL))
    {
        ZpRecording_FreeJob(Job);
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    RtlAcquireSRWLockExclusive(&Client->RecordingLock);
    RecordingId = ++Client->NextRecordingId;
    if (RecordingId == 0) RecordingId = ++Client->NextRecordingId;
    Job->RecordingId = RecordingId;
    RtlReleaseSRWLockExclusive(&Client->RecordingLock);
    Result = ZpRecording_CreatePath(RecordingId, Codec, &Job->Path, &Job->PathLength);
    if (FAILED(Result))
    {
        ZpRecording_FreeJob(Job);
        return ZpStatus_FromCode(ZpStatusHResult, Result);
    }
    Status = NtCreateEvent(&Job->StopEvent, EVENT_MODIFY_STATE | SYNCHRONIZE, NULL, NotificationEvent, FALSE);
    if (!NT_SUCCESS(Status))
    {
        ZpRecording_FreeJob(Job);
        return ZpStatus_FromNtStatus(Status);
    }
    NtQuerySystemTime(&Time);
    Job->StartTime = Time.QuadPart;
    Status = ZpRecording_EncodeJobs(Client, Job, Response, ResponseLength);
    if (!NT_SUCCESS(Status))
    {
        ZpRecording_FreeJob(Job);
        return ZpStatus_FromNtStatus(Status);
    }
    RtlAcquireSRWLockExclusive(&Client->RecordingLock);
    if (Client->RecordingJobCount >= ZP_RECORDING_MAX_JOBS)
    {
        Status = STATUS_TOO_MANY_CONTEXT_IDS;
    }
    else
    {
        Status = PS_CreateThread(NtCurrentProcess(), TRUE, ZpRecording_Worker, Job, &Job->Thread, NULL);
        if (NT_SUCCESS(Status))
        {
            InsertTailList(&Client->RecordingJobs, &Job->ListEntry);
            Client->RecordingJobCount++;
        }
    }
    RtlReleaseSRWLockExclusive(&Client->RecordingLock);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(*Response);
        *Response = NULL;
        *ResponseLength = 0;
        ZpRecording_FreeJob(Job);
        return ZpStatus_FromNtStatus(Status);
    }
    NtResumeThread(Job->Thread, NULL);
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ZP_STATUS
ZpRecording_Stop(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ ULONG RecordingId)
{
    PZP_RECORDING_JOB Job;
    PLIST_ENTRY Entry;

    RtlAcquireSRWLockShared(&Client->RecordingLock);
    for (Entry = Client->RecordingJobs.Flink; Entry != &Client->RecordingJobs; Entry = Entry->Flink)
    {
        Job = CONTAINING_RECORD(Entry, ZP_RECORDING_JOB, ListEntry);
        if (Job->RecordingId != RecordingId) continue;
        if (Job->State != ZpRecordingStateRecording)
        {
            RtlReleaseSRWLockShared(&Client->RecordingLock);
            return ZpStatus_FromNtStatus(STATUS_INVALID_DEVICE_STATE);
        }
        NtSetEvent(Job->StopEvent, NULL);
        RtlReleaseSRWLockShared(&Client->RecordingLock);
        return ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }
    RtlReleaseSRWLockShared(&Client->RecordingLock);
    return ZpStatus_FromNtStatus(STATUS_NOT_FOUND);
}

static
ZP_STATUS
ZpRecording_DeleteFile(
    _In_ PCWSTR Path)
{
    HANDLE File;
    NTSTATUS Status;

    Status = IO_CreateWin32File(&File,
                                Path,
                                NULL,
                                DELETE | SYNCHRONIZE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_OPEN,
                                FILE_SYNCHRONOUS_IO_NONALERT);
    if (NT_SUCCESS(Status))
    {
        Status = IO_DisposeFile(File);
        NtClose(File);
    }
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND)
    {
        Status = STATUS_SUCCESS;
    }
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpRecording_Delete(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ ULONG RecordingId)
{
    PZP_RECORDING_JOB Job = NULL;
    PLIST_ENTRY Entry;
    ZP_STATUS Status;

    RtlAcquireSRWLockExclusive(&Client->RecordingLock);
    for (Entry = Client->RecordingJobs.Flink; Entry != &Client->RecordingJobs; Entry = Entry->Flink)
    {
        Job = CONTAINING_RECORD(Entry, ZP_RECORDING_JOB, ListEntry);
        if (Job->RecordingId != RecordingId) continue;
        if (Job->State == ZpRecordingStateRecording || Job->State == ZpRecordingStateFinalizing)
        {
            RtlReleaseSRWLockExclusive(&Client->RecordingLock);
            return ZpStatus_FromNtStatus(STATUS_DEVICE_BUSY);
        }
        Status = ZpRecording_DeleteFile(Job->Path);
        if (ZpStatus_IsSuccess(Status))
        {
            RemoveEntryList(&Job->ListEntry);
            Client->RecordingJobCount--;
        }
        break;
    }
    RtlReleaseSRWLockExclusive(&Client->RecordingLock);
    if (Entry == &Client->RecordingJobs) return ZpStatus_FromNtStatus(STATUS_NOT_FOUND);
    if (ZpStatus_IsSuccess(Status)) ZpRecording_FreeJob(Job);
    return Status;
}

ZP_STATUS
ZpRecording_Execute(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_RECORDING_START_VIEW Start;
    ULONG RecordingId, Codecs;
    HRESULT Result;
    NTSTATUS Status;

    switch (OperationId)
    {
        case ZP_RECORDING_OPERATION_QUERY_CAPABILITIES:
            if (RequestLength != 0) return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
            Result = ZpMediaWriter_QueryCodecs(&Codecs);
            if (FAILED(Result)) return ZpStatus_FromCode(ZpStatusHResult, Result);
            *Response = Mem_Alloc(sizeof(ULONG));
            if (*Response == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            Status = ZpRecording_EncodeCapabilities(Codecs, *Response, sizeof(ULONG), ResponseLength);
            return ZpStatus_FromNtStatus(Status);

        case ZP_RECORDING_OPERATION_START:
            Status = ZpRecording_DecodeStart(Request, RequestLength, &Start);
            return NT_SUCCESS(Status) ? ZpRecording_Start(Client, &Start, Response, ResponseLength) :
                                        ZpStatus_FromNtStatus(Status);

        case ZP_RECORDING_OPERATION_ENUMERATE:
            Status = RequestLength == 0 ? ZpRecording_EncodeJobs(Client, NULL, Response, ResponseLength) :
                                          STATUS_INVALID_PARAMETER;
            return ZpStatus_FromNtStatus(Status);

        case ZP_RECORDING_OPERATION_STOP:
        case ZP_RECORDING_OPERATION_DELETE:
            Status = ZpRecording_DecodeId(Request, RequestLength, &RecordingId);
            if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
            *Response = NULL;
            *ResponseLength = 0;
            return OperationId == ZP_RECORDING_OPERATION_STOP ? ZpRecording_Stop(Client, RecordingId) :
                                                                ZpRecording_Delete(Client, RecordingId);

        default:
            return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
}

VOID
ZpRecording_StopAll(
    _Inout_ PZP_CLIENT_OBJECT Client)
{
    PZP_RECORDING_JOB Job;
    PLIST_ENTRY Entry;

    RtlAcquireSRWLockExclusive(&Client->RecordingLock);
    for (Entry = Client->RecordingJobs.Flink; Entry != &Client->RecordingJobs; Entry = Entry->Flink)
    {
        Job = CONTAINING_RECORD(Entry, ZP_RECORDING_JOB, ListEntry);
        if (Job->State != ZpRecordingStateRecording) continue;
        Job->Interrupted = TRUE;
        NtSetEvent(Job->StopEvent, NULL);
    }
    RtlReleaseSRWLockExclusive(&Client->RecordingLock);
}

VOID
ZpRecording_Cleanup(
    _Inout_ PZP_CLIENT_OBJECT Client)
{
    PZP_RECORDING_JOB Job;

    ZpRecording_StopAll(Client);
    while (!IsListEmpty(&Client->RecordingJobs))
    {
        Job = CONTAINING_RECORD(RemoveHeadList(&Client->RecordingJobs), ZP_RECORDING_JOB, ListEntry);
        ZpRecording_DeleteFile(Job->Path);
        ZpRecording_FreeJob(Job);
    }
}
