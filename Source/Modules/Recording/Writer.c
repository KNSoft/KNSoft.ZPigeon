#define COBJMACROS

#include "Writer.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfreadwrite.h>

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "Mfuuid.lib")

#define ZP_MEDIA_WRITER_NO_STREAM MAXDWORD
#define ZP_WAVE_HEADER_SIZE 44

struct _ZP_MEDIA_WRITER
{
    RTL_SRWLOCK Lock;
    IMFSinkWriter* Sink;
    HANDLE WaveFile;
    DWORD VideoStream;
    DWORD AudioStream;
    ULONGLONG FirstTimestamp;
    ULONGLONG WaveBytes;
    USHORT AudioChannels;
    ULONG AudioSampleRate;
    BOOLEAN TimestampStarted;
    BOOLEAN AudioWritten;
    BOOLEAN MfStarted;
    BOOLEAN Finalized;
};

static
HRESULT
ZpMediaWriter_HResultFromNtStatus(
    _In_ NTSTATUS Status)
{
    return NT_SUCCESS(Status) ? S_OK : HRESULT_FROM_NT(Status);
}

static
HRESULT
ZpMediaWriter_HasEncoder(
    _In_ REFGUID Category,
    _In_ REFGUID MajorType,
    _In_ REFGUID Subtype)
{
    MFT_REGISTER_TYPE_INFO Output = { *MajorType, *Subtype };
    IMFActivate** Activations = NULL;
    UINT32 Count = 0, Index;
    HRESULT Result;

    Result = MFTEnumEx(*Category,
                       MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_HARDWARE |
                           MFT_ENUM_FLAG_SORTANDFILTER,
                       NULL,
                       &Output,
                       &Activations,
                       &Count);
    for (Index = 0; Index < Count; Index++) IMFActivate_Release(Activations[Index]);
    CoTaskMemFree(Activations);
    return SUCCEEDED(Result) && Count != 0 ? S_OK : SUCCEEDED(Result) ? MF_E_TOPO_CODEC_NOT_FOUND : Result;
}

HRESULT
ZpMediaWriter_QueryCodecs(
    _Out_ PULONG Codecs)
{
    ULONG Value = ZP_RECORDING_CODEC_CAPABILITY(ZpRecordingCodecPcm);
    HRESULT Result;

    Result = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(Result)) return Result;
    if (SUCCEEDED(ZpMediaWriter_HasEncoder(&MFT_CATEGORY_AUDIO_ENCODER,
                                           &MFMediaType_Audio,
                                           &MFAudioFormat_AAC)))
    {
        Value |= ZP_RECORDING_CODEC_CAPABILITY(ZpRecordingCodecAac);
    }
    if (SUCCEEDED(ZpMediaWriter_HasEncoder(&MFT_CATEGORY_VIDEO_ENCODER,
                                           &MFMediaType_Video,
                                           &MFVideoFormat_H264)))
    {
        Value |= ZP_RECORDING_CODEC_CAPABILITY(ZpRecordingCodecH264);
    }
    if (SUCCEEDED(ZpMediaWriter_HasEncoder(&MFT_CATEGORY_VIDEO_ENCODER,
                                           &MFMediaType_Video,
                                           &MFVideoFormat_HEVC)))
    {
        Value |= ZP_RECORDING_CODEC_CAPABILITY(ZpRecordingCodecH265);
    }
    if (SUCCEEDED(ZpMediaWriter_HasEncoder(&MFT_CATEGORY_VIDEO_ENCODER,
                                           &MFMediaType_Video,
                                           &MFVideoFormat_MSS2)))
    {
        Value |= ZP_RECORDING_CODEC_CAPABILITY(ZpRecordingCodecWmvScreen);
    }
    MFShutdown();
    *Codecs = Value;
    return S_OK;
}

static
HRESULT
ZpMediaWriter_CreateSink(
    _In_ PCWSTR Path,
    _In_opt_ IUnknown* DeviceManager,
    _Outptr_ IMFSinkWriter** Sink)
{
    IMFAttributes* Attributes;
    HRESULT Result;

    Result = MFCreateAttributes(&Attributes, 3);
    if (FAILED(Result)) return Result;
    Result = IMFAttributes_SetUINT32(Attributes, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (SUCCEEDED(Result)) Result = IMFAttributes_SetUINT32(Attributes, &MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (SUCCEEDED(Result) && DeviceManager != NULL)
    {
        Result = IMFAttributes_SetUnknown(Attributes, &MF_SINK_WRITER_D3D_MANAGER, DeviceManager);
    }
    if (SUCCEEDED(Result)) Result = MFCreateSinkWriterFromURL(Path, NULL, Attributes, Sink);
    IMFAttributes_Release(Attributes);
    return Result;
}

static
HRESULT
ZpMediaWriter_AddAudio(
    _Inout_ PZP_MEDIA_WRITER Writer,
    _In_ ZP_RECORDING_CODEC VideoCodec,
    _In_ USHORT Channels,
    _In_ ULONG SampleRate,
    _In_ ULONG BitRate)
{
    IMFMediaType* Output = NULL;
    IMFMediaType* Input = NULL;
    HRESULT Result;

    Result = MFCreateMediaType(&Output);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetGUID(Output, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    if (SUCCEEDED(Result))
    {
        Result = IMFMediaType_SetGUID(Output,
                                      &MF_MT_SUBTYPE,
                                      VideoCodec == ZpRecordingCodecWmvScreen ?
                                          &MFAudioFormat_WMAudioV9 : &MFAudioFormat_AAC);
    }
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT32(Output, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT32(Output, &MF_MT_AUDIO_SAMPLES_PER_SECOND, SampleRate);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT32(Output, &MF_MT_AUDIO_NUM_CHANNELS, Channels);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT32(Output, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, BitRate / 8);
    if (SUCCEEDED(Result) && VideoCodec != ZpRecordingCodecWmvScreen)
    {
        Result = IMFMediaType_SetUINT32(Output, &MF_MT_AAC_PAYLOAD_TYPE, 0);
    }
    if (SUCCEEDED(Result) && VideoCodec != ZpRecordingCodecWmvScreen)
    {
        Result = IMFMediaType_SetUINT32(Output, &MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
    }
    if (SUCCEEDED(Result)) Result = IMFSinkWriter_AddStream(Writer->Sink, Output, &Writer->AudioStream);
    if (SUCCEEDED(Result)) Result = MFCreateMediaType(&Input);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetGUID(Input, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetGUID(Input, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT32(Input, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT32(Input, &MF_MT_AUDIO_SAMPLES_PER_SECOND, SampleRate);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT32(Input, &MF_MT_AUDIO_NUM_CHANNELS, Channels);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT32(Input, &MF_MT_AUDIO_BLOCK_ALIGNMENT, Channels * 2);
    if (SUCCEEDED(Result))
    {
        Result = IMFMediaType_SetUINT32(Input, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, SampleRate * Channels * 2);
    }
    if (SUCCEEDED(Result)) Result = IMFSinkWriter_SetInputMediaType(Writer->Sink, Writer->AudioStream, Input, NULL);
    if (Input != NULL) IMFMediaType_Release(Input);
    if (Output != NULL) IMFMediaType_Release(Output);
    if (SUCCEEDED(Result))
    {
        Writer->AudioChannels = Channels;
        Writer->AudioSampleRate = SampleRate;
    }
    return Result;
}

static
VOID
ZpMediaWriter_WriteUInt16(
    _Out_writes_(2) PBYTE Buffer,
    _In_ USHORT Value)
{
    Buffer[0] = (BYTE)Value;
    Buffer[1] = (BYTE)(Value >> 8);
}

static
VOID
ZpMediaWriter_WriteUInt32(
    _Out_writes_(4) PBYTE Buffer,
    _In_ ULONG Value)
{
    Buffer[0] = (BYTE)Value;
    Buffer[1] = (BYTE)(Value >> 8);
    Buffer[2] = (BYTE)(Value >> 16);
    Buffer[3] = (BYTE)(Value >> 24);
}

static
HRESULT
ZpMediaWriter_WriteWaveHeader(
    _Inout_ PZP_MEDIA_WRITER Writer)
{
    BYTE Header[ZP_WAVE_HEADER_SIZE] = {
        'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
        16, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0
    };
    LARGE_INTEGER Offset = { 0 };
    ULONG BlockAlign = Writer->AudioChannels * sizeof(SHORT);
    NTSTATUS Status;

    if (Writer->WaveBytes > MAXULONG - 36) return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    ZpMediaWriter_WriteUInt32(&Header[4], (ULONG)Writer->WaveBytes + 36);
    ZpMediaWriter_WriteUInt16(&Header[22], Writer->AudioChannels);
    ZpMediaWriter_WriteUInt32(&Header[24], Writer->AudioSampleRate);
    ZpMediaWriter_WriteUInt32(&Header[28], Writer->AudioSampleRate * BlockAlign);
    ZpMediaWriter_WriteUInt16(&Header[32], (USHORT)BlockAlign);
    ZpMediaWriter_WriteUInt32(&Header[40], (ULONG)Writer->WaveBytes);
    Status = IO_WriteFile(Writer->WaveFile, &Offset, Header, sizeof(Header), NULL);
    return ZpMediaWriter_HResultFromNtStatus(Status);
}

HRESULT
ZpMediaWriter_CreateAudio(
    _In_ PCWSTR Path,
    _In_ ZP_RECORDING_CODEC Codec,
    _In_ USHORT Channels,
    _In_ ULONG SampleRate,
    _In_ ULONG BitRate,
    _Out_ PZP_MEDIA_WRITER* Writer)
{
    PZP_MEDIA_WRITER Object;
    HRESULT Result;

    if ((Codec != ZpRecordingCodecPcm && Codec != ZpRecordingCodecAac) || Channels == 0 || SampleRate == 0)
    {
        return E_INVALIDARG;
    }
    Object = Mem_Alloc(sizeof(*Object));
    if (Object == NULL) return E_OUTOFMEMORY;
    RtlZeroMemory(Object, sizeof(*Object));
    Object->VideoStream = Object->AudioStream = ZP_MEDIA_WRITER_NO_STREAM;
    Object->AudioChannels = Channels;
    Object->AudioSampleRate = SampleRate;
    if (Codec == ZpRecordingCodecPcm)
    {
        NTSTATUS Status = IO_CreateWin32File(&Object->WaveFile,
                                             Path,
                                             NULL,
                                             FILE_WRITE_DATA | SYNCHRONIZE,
                                             FILE_SHARE_READ,
                                             FILE_OVERWRITE_IF,
                                             FILE_SYNCHRONOUS_IO_NONALERT);

        Result = ZpMediaWriter_HResultFromNtStatus(Status);
        if (SUCCEEDED(Result)) Result = ZpMediaWriter_WriteWaveHeader(Object);
    }
    else
    {
        Result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        Object->MfStarted = SUCCEEDED(Result);
        if (SUCCEEDED(Result)) Result = ZpMediaWriter_CreateSink(Path, NULL, &Object->Sink);
        if (SUCCEEDED(Result))
        {
            Result = ZpMediaWriter_AddAudio(Object, ZpRecordingCodecAac, Channels, SampleRate, BitRate);
        }
        if (SUCCEEDED(Result)) Result = IMFSinkWriter_BeginWriting(Object->Sink);
    }
    if (FAILED(Result))
    {
        ZpMediaWriter_Close(Object);
        return Result;
    }
    *Writer = Object;
    return S_OK;
}

static
HRESULT
ZpMediaWriter_SetVideoType(
    _Inout_ PZP_MEDIA_WRITER Writer,
    _In_ ZP_RECORDING_CODEC Codec,
    _In_ REFGUID InputSubtype,
    _In_ ULONG InputWidth,
    _In_ ULONG InputHeight,
    _In_ ULONG OutputWidth,
    _In_ ULONG OutputHeight,
    _In_ USHORT FrameRate,
    _In_ ULONG BitRate)
{
    IMFMediaType* Output = NULL;
    IMFMediaType* Input = NULL;
    const GUID* OutputSubtype = Codec == ZpRecordingCodecH264 ? &MFVideoFormat_H264 :
                                Codec == ZpRecordingCodecH265 ? &MFVideoFormat_HEVC :
                                                               &MFVideoFormat_MSS2;
    HRESULT Result;

    Result = MFCreateMediaType(&Output);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetGUID(Output, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetGUID(Output, &MF_MT_SUBTYPE, OutputSubtype);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT32(Output, &MF_MT_AVG_BITRATE, BitRate);
    if (SUCCEEDED(Result))
    {
        Result = IMFMediaType_SetUINT64(Output,
                                        &MF_MT_FRAME_SIZE,
                                        ((ULONGLONG)OutputWidth << 32) | OutputHeight);
    }
    if (SUCCEEDED(Result))
    {
        Result = IMFMediaType_SetUINT64(Output, &MF_MT_FRAME_RATE, ((ULONGLONG)FrameRate << 32) | 1);
    }
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT64(Output, &MF_MT_PIXEL_ASPECT_RATIO, 1ULL << 32 | 1);
    if (SUCCEEDED(Result))
    {
        Result = IMFMediaType_SetUINT32(Output, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    }
    if (SUCCEEDED(Result)) Result = IMFSinkWriter_AddStream(Writer->Sink, Output, &Writer->VideoStream);
    if (SUCCEEDED(Result)) Result = MFCreateMediaType(&Input);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetGUID(Input, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetGUID(Input, &MF_MT_SUBTYPE, InputSubtype);
    if (SUCCEEDED(Result))
    {
        Result = IMFMediaType_SetUINT64(Input,
                                        &MF_MT_FRAME_SIZE,
                                        ((ULONGLONG)InputWidth << 32) | InputHeight);
    }
    if (SUCCEEDED(Result))
    {
        Result = IMFMediaType_SetUINT64(Input, &MF_MT_FRAME_RATE, ((ULONGLONG)FrameRate << 32) | 1);
    }
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT64(Input, &MF_MT_PIXEL_ASPECT_RATIO, 1ULL << 32 | 1);
    if (SUCCEEDED(Result))
    {
        Result = IMFMediaType_SetUINT32(Input, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    }
    if (SUCCEEDED(Result)) Result = IMFSinkWriter_SetInputMediaType(Writer->Sink, Writer->VideoStream, Input, NULL);
    if (Input != NULL) IMFMediaType_Release(Input);
    if (Output != NULL) IMFMediaType_Release(Output);
    return Result;
}

HRESULT
ZpMediaWriter_CreateVideo(
    _In_ PCWSTR Path,
    _In_ ZP_RECORDING_CODEC Codec,
    _In_ REFGUID InputSubtype,
    _In_ ULONG InputWidth,
    _In_ ULONG InputHeight,
    _In_ ULONG OutputWidth,
    _In_ ULONG OutputHeight,
    _In_ USHORT FrameRate,
    _In_ ULONG VideoBitRate,
    _In_ USHORT AudioChannels,
    _In_ ULONG AudioSampleRate,
    _In_ ULONG AudioBitRate,
    _In_opt_ IUnknown* DeviceManager,
    _Out_ PZP_MEDIA_WRITER* Writer)
{
    PZP_MEDIA_WRITER Object;
    HRESULT Result;

    if ((Codec != ZpRecordingCodecH264 && Codec != ZpRecordingCodecH265 &&
         Codec != ZpRecordingCodecWmvScreen) || InputWidth == 0 || InputHeight == 0 ||
        OutputWidth == 0 || OutputHeight == 0 || FrameRate == 0 || VideoBitRate == 0)
    {
        return E_INVALIDARG;
    }
    Object = Mem_Alloc(sizeof(*Object));
    if (Object == NULL) return E_OUTOFMEMORY;
    RtlZeroMemory(Object, sizeof(*Object));
    Object->VideoStream = Object->AudioStream = ZP_MEDIA_WRITER_NO_STREAM;
    Result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    Object->MfStarted = SUCCEEDED(Result);
    if (SUCCEEDED(Result)) Result = ZpMediaWriter_CreateSink(Path, DeviceManager, &Object->Sink);
    if (SUCCEEDED(Result))
    {
        Result = ZpMediaWriter_SetVideoType(Object,
                                            Codec,
                                            InputSubtype,
                                            InputWidth,
                                            InputHeight,
                                            OutputWidth,
                                            OutputHeight,
                                            FrameRate,
                                            VideoBitRate);
    }
    if (SUCCEEDED(Result) && AudioChannels != 0)
    {
        Result = ZpMediaWriter_AddAudio(Object, Codec, AudioChannels, AudioSampleRate, AudioBitRate);
    }
    if (SUCCEEDED(Result)) Result = IMFSinkWriter_BeginWriting(Object->Sink);
    if (FAILED(Result))
    {
        ZpMediaWriter_Close(Object);
        return Result;
    }
    *Writer = Object;
    return S_OK;
}

static
HRESULT
ZpMediaWriter_WriteSample(
    _Inout_ PZP_MEDIA_WRITER Writer,
    _In_ DWORD Stream,
    _In_ IMFSample* Sample,
    _In_ ULONGLONG Timestamp,
    _In_ ULONGLONG Duration)
{
    HRESULT Result;

    if (!Writer->TimestampStarted)
    {
        Writer->FirstTimestamp = Timestamp;
        Writer->TimestampStarted = TRUE;
    }
    Result = IMFSample_SetSampleTime(Sample,
                                     Timestamp >= Writer->FirstTimestamp ?
                                         Timestamp - Writer->FirstTimestamp : 0);
    if (SUCCEEDED(Result)) Result = IMFSample_SetSampleDuration(Sample, Duration);
    if (SUCCEEDED(Result)) Result = IMFSinkWriter_WriteSample(Writer->Sink, Stream, Sample);
    return Result;
}

HRESULT
ZpMediaWriter_WriteAudio(
    _Inout_ PZP_MEDIA_WRITER Writer,
    _In_reads_(FrameCount * Channels) const SHORT* Samples,
    _In_ USHORT Channels,
    _In_ ULONG SampleRate,
    _In_ ULONG FrameCount,
    _In_ ULONGLONG Timestamp)
{
    IMFMediaBuffer* Buffer = NULL;
    IMFSample* Sample = NULL;
    BYTE* Bytes = NULL;
    ULONG Length = FrameCount * Channels * sizeof(SHORT);
    HRESULT Result;

    if (Channels != Writer->AudioChannels || SampleRate != Writer->AudioSampleRate) return E_INVALIDARG;
    RtlAcquireSRWLockExclusive(&Writer->Lock);
    if (Writer->Finalized)
    {
        Result = MF_E_SHUTDOWN;
        goto Cleanup;
    }
    if (Writer->WaveFile != NULL)
    {
        LARGE_INTEGER Offset;
        NTSTATUS Status;

        if (MAXULONGLONG - ZP_WAVE_HEADER_SIZE - Writer->WaveBytes < Length)
        {
            Result = E_OUTOFMEMORY;
            goto Cleanup;
        }
        Offset.QuadPart = ZP_WAVE_HEADER_SIZE + Writer->WaveBytes;
        Status = IO_WriteFile(Writer->WaveFile, &Offset, (PVOID)Samples, Length, NULL);
        Result = ZpMediaWriter_HResultFromNtStatus(Status);
        if (SUCCEEDED(Result)) Writer->WaveBytes += Length;
        goto Cleanup;
    }
    Result = MFCreateMemoryBuffer(Length, &Buffer);
    if (SUCCEEDED(Result)) Result = IMFMediaBuffer_Lock(Buffer, &Bytes, NULL, NULL);
    if (SUCCEEDED(Result)) RtlCopyMemory(Bytes, Samples, Length);
    if (Bytes != NULL) IMFMediaBuffer_Unlock(Buffer);
    if (SUCCEEDED(Result)) Result = IMFMediaBuffer_SetCurrentLength(Buffer, Length);
    if (SUCCEEDED(Result)) Result = MFCreateSample(&Sample);
    if (SUCCEEDED(Result)) Result = IMFSample_AddBuffer(Sample, Buffer);
    if (SUCCEEDED(Result))
    {
        Result = ZpMediaWriter_WriteSample(Writer,
                                           Writer->AudioStream,
                                           Sample,
                                           Timestamp,
                                           (ULONGLONG)FrameCount * 10000000 / SampleRate);
    }

Cleanup:
    if (Sample != NULL) IMFSample_Release(Sample);
    if (Buffer != NULL) IMFMediaBuffer_Release(Buffer);
    if (SUCCEEDED(Result)) Writer->AudioWritten = TRUE;
    RtlReleaseSRWLockExclusive(&Writer->Lock);
    return Result;
}

HRESULT
ZpMediaWriter_FillAudioSilence(
    _Inout_ PZP_MEDIA_WRITER Writer,
    _In_ ULONGLONG Duration)
{
    SHORT* Samples;
    ULONGLONG Frames, Position = 0, Timestamp;
    ULONG Count;
    HRESULT Result = S_OK;

    RtlAcquireSRWLockShared(&Writer->Lock);
    if (Writer->AudioChannels == 0 || Writer->AudioWritten)
    {
        RtlReleaseSRWLockShared(&Writer->Lock);
        return S_OK;
    }
    Timestamp = Writer->TimestampStarted ? Writer->FirstTimestamp : 0;
    RtlReleaseSRWLockShared(&Writer->Lock);
    Frames = Duration / 10000000 * Writer->AudioSampleRate +
             Duration % 10000000 * Writer->AudioSampleRate / 10000000;
    Frames = max(1ULL, Frames);
    Count = Writer->AudioSampleRate;
    Samples = Mem_Alloc((SIZE_T)Count * Writer->AudioChannels * sizeof(*Samples));
    if (Samples == NULL) return E_OUTOFMEMORY;
    RtlZeroMemory(Samples, (SIZE_T)Count * Writer->AudioChannels * sizeof(*Samples));
    while (Position < Frames && SUCCEEDED(Result))
    {
        Count = (ULONG)min((ULONGLONG)Writer->AudioSampleRate, Frames - Position);
        Result = ZpMediaWriter_WriteAudio(Writer,
                                          Samples,
                                          Writer->AudioChannels,
                                          Writer->AudioSampleRate,
                                          Count,
                                          Timestamp + Position * 10000000 / Writer->AudioSampleRate);
        Position += Count;
    }
    Mem_Free(Samples);
    return Result;
}

HRESULT
ZpMediaWriter_WriteVideo(
    _Inout_ PZP_MEDIA_WRITER Writer,
    _In_ IMFSample* Sample,
    _In_ ULONGLONG Timestamp,
    _In_ ULONGLONG Duration)
{
    HRESULT Result;

    RtlAcquireSRWLockExclusive(&Writer->Lock);
    Result = Writer->Finalized ? MF_E_SHUTDOWN :
                                 ZpMediaWriter_WriteSample(Writer,
                                                           Writer->VideoStream,
                                                           Sample,
                                                           Timestamp,
                                                           Duration);
    RtlReleaseSRWLockExclusive(&Writer->Lock);
    return Result;
}

HRESULT
ZpMediaWriter_WriteVideoBytes(
    _Inout_ PZP_MEDIA_WRITER Writer,
    _In_reads_bytes_(Length) const VOID* Data,
    _In_ ULONG Length,
    _In_ ULONGLONG Timestamp,
    _In_ ULONGLONG Duration)
{
    IMFMediaBuffer* Buffer = NULL;
    IMFSample* Sample = NULL;
    BYTE* Bytes = NULL;
    HRESULT Result;

    Result = MFCreateMemoryBuffer(Length, &Buffer);
    if (SUCCEEDED(Result)) Result = IMFMediaBuffer_Lock(Buffer, &Bytes, NULL, NULL);
    if (SUCCEEDED(Result)) RtlCopyMemory(Bytes, Data, Length);
    if (Bytes != NULL) IMFMediaBuffer_Unlock(Buffer);
    if (SUCCEEDED(Result)) Result = IMFMediaBuffer_SetCurrentLength(Buffer, Length);
    if (SUCCEEDED(Result)) Result = MFCreateSample(&Sample);
    if (SUCCEEDED(Result)) Result = IMFSample_AddBuffer(Sample, Buffer);
    if (SUCCEEDED(Result)) Result = ZpMediaWriter_WriteVideo(Writer, Sample, Timestamp, Duration);
    if (Sample != NULL) IMFSample_Release(Sample);
    if (Buffer != NULL) IMFMediaBuffer_Release(Buffer);
    return Result;
}

HRESULT
ZpMediaWriter_Finalize(
    _Inout_ PZP_MEDIA_WRITER Writer)
{
    HRESULT Result;

    RtlAcquireSRWLockExclusive(&Writer->Lock);
    if (Writer->Finalized)
    {
        Result = S_FALSE;
    }
    else
    {
        Writer->Finalized = TRUE;
        Result = Writer->WaveFile != NULL ? ZpMediaWriter_WriteWaveHeader(Writer) :
                                           IMFSinkWriter_Finalize(Writer->Sink);
    }
    RtlReleaseSRWLockExclusive(&Writer->Lock);
    return Result;
}

VOID
ZpMediaWriter_Close(
    _In_opt_ PZP_MEDIA_WRITER Writer)
{
    if (Writer == NULL) return;
    if (Writer->Sink != NULL) IMFSinkWriter_Release(Writer->Sink);
    if (Writer->WaveFile != NULL) NtClose(Writer->WaveFile);
    if (Writer->MfStarted) MFShutdown();
    Mem_Free(Writer);
}
