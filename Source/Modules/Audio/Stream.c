#define COBJMACROS

#include "Client.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"

#define PDEVCAPS ZP_AUDIO_PDEVCAPS
#include <audioclient.h>
#undef PDEVCAPS
#include <mmdeviceapi.h>

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#pragma comment(lib, "Ole32.lib")

#define ZP_AUDIO_CHANNEL_CHUNK_SIZE 0x00040000UL
#define ZP_AUDIO_BUFFER_DURATION 1000000LL

static CONST GUID ZpAudioPcmFormat = { 0x00000001, 0x0000, 0x0010,
                                       { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static CONST GUID ZpAudioFloatFormat = { 0x00000003, 0x0000, 0x0010,
                                         { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static CONST CLSID ZpAudioDeviceEnumeratorClass = { 0xbcde0395, 0xe52f, 0x467c,
                                                     { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
static CONST IID ZpAudioDeviceEnumeratorIid = { 0xa95664d2, 0x9614, 0x4f35,
                                                { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };
static CONST IID ZpAudioClientIid = { 0x1cb9ad4c, 0xdbfa, 0x4c32,
                                      { 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2 } };
static CONST IID ZpAudioCaptureClientIid = { 0xc8adbd64, 0xe71e, 0x48a0,
                                             { 0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17 } };

struct _ZP_CLIENT_AUDIO_CHANNEL
{
    ZP_CLIENT_LOCAL_CHANNEL Header;
    LOGICAL WorkerActive;
    ULONGLONG Credit;
    HANDLE WorkerThread;
    HANDLE CreditEvent;
    HANDLE StopEvent;
    ZP_AUDIO_FLOW Flow;
    ULONG DeviceIdLength;
    WCHAR DeviceId[ANYSIZE_ARRAY];
};

static
NTSTATUS
ZpAudio_SendLocked(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PCZP_TRANSPORT_OPERATIONS Operations = Object->TransportOperations[Object->ActiveTransport];

    return Object->State == ZpClientStateReady && Operations->Send != NULL ?
               Operations->Send(Object->TransportContexts[Object->ActiveTransport],
                                MessageType,
                                Body,
                                BodyLength) :
               STATUS_CONNECTION_DISCONNECTED;
}

static
NTSTATUS
ZpAudio_SendCloseLocked(
    _Inout_ PZP_CLIENT_AUDIO_CHANNEL Channel,
    _In_ ZP_STATUS CloseStatus)
{
    BYTE Body[sizeof(ULONGLONG) + ZP_STATUS_WIRE_SIZE];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelClose(Channel->Header.ChannelId,
                                          CloseStatus,
                                          Body,
                                          sizeof(Body),
                                          &BodyLength);
    return NT_SUCCESS(Status) ? ZpAudio_SendLocked(Channel->Header.Owner,
                                                   ZpMessageChannelClose,
                                                   Body,
                                                   BodyLength) : Status;
}

static
NTSTATUS
ZpAudio_SendBytes(
    _Inout_ PZP_CLIENT_AUDIO_CHANNEL Channel,
    _In_reads_bytes_(Length) const VOID* Data,
    _In_ ULONG Length)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    PBYTE Body;
    ULONG Offset = 0, ChunkLength, BodyLength;
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL Pending, Removed;

    Body = Mem_Alloc(sizeof(ULONGLONG) + ZP_AUDIO_CHANNEL_CHUNK_SIZE);
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
        ChunkLength = min(Length - Offset, (ULONG)min(Channel->Credit, ZP_AUDIO_CHANNEL_CHUNK_SIZE));
        Channel->Credit -= ChunkLength;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Status = ZpMessage_EncodeChannelData(Channel->Header.ChannelId,
                                             Add2Ptr(Data, Offset),
                                             ChunkLength,
                                             Body,
                                             sizeof(ULONGLONG) + ZP_AUDIO_CHANNEL_CHUNK_SIZE,
                                             &BodyLength);
        if (!NT_SUCCESS(Status)) break;
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Pending = Channel->Header.Pending;
        Status = Pending ? ZpAudio_SendLocked(Object, ZpMessageChannelData, Body, BodyLength) : STATUS_CANCELLED;
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
ZpAudio_FinishWorker(
    _Inout_ PZP_CLIENT_AUDIO_CHANNEL Channel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    if (Removed) ZpAudio_SendCloseLocked(Channel, Status);
    Channel->WorkerActive = FALSE;
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
    ZpClientLocalChannel_Release(&Channel->Header);
}

static
HRESULT
ZpAudio_GetDevice(
    _In_ PZP_CLIENT_AUDIO_CHANNEL Channel,
    _Outptr_ IMMDevice** Device)
{
    IMMDeviceEnumerator* Enumerator;
    HRESULT Result;

    Result = CoCreateInstance(&ZpAudioDeviceEnumeratorClass,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &ZpAudioDeviceEnumeratorIid,
                              (PVOID*)&Enumerator);
    if (FAILED(Result)) return Result;
    Result = Channel->DeviceIdLength == 0 ?
                 IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator,
                                                              Channel->Flow == ZpAudioFlowRender ? eRender : eCapture,
                                                              eConsole,
                                                              Device) :
                 IMMDeviceEnumerator_GetDevice(Enumerator, Channel->DeviceId, Device);
    IMMDeviceEnumerator_Release(Enumerator);
    return Result;
}

static
HRESULT
ZpAudio_GetFormat(
    _In_ const WAVEFORMATEX* Format,
    _Out_ PLOGICAL FloatingPoint,
    _Out_ PUSHORT ValidBits)
{
    if (Format->nChannels == 0 || Format->nChannels > ZP_AUDIO_MAX_CHANNELS ||
        Format->nSamplesPerSec == 0 || Format->nSamplesPerSec > ZP_AUDIO_MAX_SAMPLE_RATE)
    {
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    if (Format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        *FloatingPoint = TRUE;
        *ValidBits = Format->wBitsPerSample;
    }
    else if (Format->wFormatTag == WAVE_FORMAT_PCM)
    {
        *FloatingPoint = FALSE;
        *ValidBits = Format->wBitsPerSample;
    }
    else if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && Format->cbSize >= 22)
    {
        const WAVEFORMATEXTENSIBLE* Extended = (const WAVEFORMATEXTENSIBLE*)Format;

        if (IsEqualGUID(&Extended->SubFormat, &ZpAudioFloatFormat)) *FloatingPoint = TRUE;
        else if (IsEqualGUID(&Extended->SubFormat, &ZpAudioPcmFormat)) *FloatingPoint = FALSE;
        else return AUDCLNT_E_UNSUPPORTED_FORMAT;
        *ValidBits = Extended->Samples.wValidBitsPerSample;
    }
    else
    {
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    if ((*FloatingPoint && Format->wBitsPerSample != 32) ||
        (!*FloatingPoint && Format->wBitsPerSample != 8 && Format->wBitsPerSample != 16 &&
         Format->wBitsPerSample != 24 && Format->wBitsPerSample != 32) ||
        Format->nBlockAlign != Format->nChannels * Format->wBitsPerSample / 8 ||
        *ValidBits == 0 || *ValidBits > Format->wBitsPerSample)
    {
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    return S_OK;
}

static
SHORT
ZpAudio_ConvertSample(
    _In_reads_bytes_(Bits / 8) const BYTE* Source,
    _In_ USHORT Bits,
    _In_ LOGICAL FloatingPoint)
{
    LONG Value;

    if (FloatingPoint)
    {
        float Sample;

        RtlCopyMemory(&Sample, Source, sizeof(Sample));
        if (Sample >= 1.0f) return MAXSHORT;
        if (Sample <= -1.0f) return MINSHORT;
        return (SHORT)(Sample * 32767.0f);
    }
    if (Bits == 8) return (SHORT)(((LONG)Source[0] - 128) << 8);
    if (Bits == 16)
    {
        SHORT Sample;

        RtlCopyMemory(&Sample, Source, sizeof(Sample));
        return Sample;
    }
    if (Bits == 24)
    {
        Value = (LONG)((ULONG)Source[0] | ((ULONG)Source[1] << 8) | ((ULONG)Source[2] << 16));
        if (Value & 0x00800000) Value |= 0xFF000000;
    }
    else
    {
        RtlCopyMemory(&Value, Source, sizeof(Value));
    }
    return (SHORT)(Value >> (Bits > 16 ? Bits - 16 : 0));
}

static
VOID
ZpAudio_ConvertFrames(
    _In_reads_bytes_(FrameCount * Format->nBlockAlign) const BYTE* Source,
    _In_ const WAVEFORMATEX* Format,
    _In_ LOGICAL FloatingPoint,
    _In_ ULONG FrameCount,
    _Out_writes_(FrameCount * Format->nChannels) SHORT* Destination)
{
    ULONG Samples = FrameCount * Format->nChannels;
    ULONG BytesPerSample = Format->wBitsPerSample / 8;
    ULONG Index;

    for (Index = 0; Index < Samples; Index++)
    {
        Destination[Index] = ZpAudio_ConvertSample(Source + Index * BytesPerSample,
                                                   Format->wBitsPerSample,
                                                   FloatingPoint);
    }
}

static
NTSTATUS
ZpAudio_SendPacket(
    _Inout_ PZP_CLIENT_AUDIO_CHANNEL Channel,
    _In_ const WAVEFORMATEX* Format,
    _In_ LOGICAL FloatingPoint,
    _In_reads_bytes_opt_(FrameCount * Format->nBlockAlign) const BYTE* Data,
    _In_ ULONG FrameCount,
    _In_ LOGICAL Silent,
    _Out_writes_(ZP_AUDIO_MAX_PACKET_FRAMES * Format->nChannels) SHORT* Samples)
{
    BYTE Header[sizeof(USHORT) * 2 + sizeof(ULONG) * 3];
    ZP_AUDIO_PACKET Packet;
    ULONG HeaderLength, Frames, DataLength;
    NTSTATUS Status = STATUS_SUCCESS;

    while (FrameCount != 0)
    {
        Frames = min(FrameCount, ZP_AUDIO_MAX_PACKET_FRAMES);
        DataLength = Frames * Format->nChannels * sizeof(SHORT);
        if (Silent) RtlZeroMemory(Samples, DataLength);
        else ZpAudio_ConvertFrames(Data, Format, FloatingPoint, Frames, Samples);
        Packet.Format = ZP_AUDIO_FORMAT_PCM16;
        Packet.Channels = Format->nChannels;
        Packet.SampleRate = Format->nSamplesPerSec;
        Packet.FrameCount = Frames;
        Packet.DataLength = DataLength;
        Status = ZpAudio_EncodePacket(&Packet, Header, sizeof(Header), &HeaderLength);
        if (NT_SUCCESS(Status)) Status = ZpAudio_SendBytes(Channel, Header, HeaderLength);
        if (NT_SUCCESS(Status)) Status = ZpAudio_SendBytes(Channel, Samples, DataLength);
        if (!NT_SUCCESS(Status)) break;
        if (!Silent) Data += (SIZE_T)Frames * Format->nBlockAlign;
        FrameCount -= Frames;
    }
    return Status;
}

static
NTSTATUS
NTAPI
ZpAudio_Worker(
    _In_ PVOID Context)
{
    PZP_CLIENT_AUDIO_CHANNEL Channel = Context;
    IMMDevice* Device = NULL;
    IAudioClient* Client = NULL;
    IAudioCaptureClient* Capture = NULL;
    WAVEFORMATEX* Format = NULL;
    SHORT* Samples = NULL;
    BYTE* Data;
    HANDLE AudioEvent = NULL;
    HANDLE Events[2];
    DWORD Flags;
    UINT32 Frames, NextFrames;
    USHORT ValidBits;
    LOGICAL FloatingPoint, Started = FALSE, Uninitialize;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;

    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Uninitialize = SUCCEEDED(Result);
    if (Result == RPC_E_CHANGED_MODE) Result = S_OK;
    if (SUCCEEDED(Result)) Result = ZpAudio_GetDevice(Channel, &Device);
    if (SUCCEEDED(Result)) Result = IMMDevice_Activate(Device,
                                                       &ZpAudioClientIid,
                                                       CLSCTX_INPROC_SERVER,
                                                       NULL,
                                                       (PVOID*)&Client);
    if (SUCCEEDED(Result)) Result = IAudioClient_GetMixFormat(Client, &Format);
    if (SUCCEEDED(Result)) Result = ZpAudio_GetFormat(Format, &FloatingPoint, &ValidBits);
    if (SUCCEEDED(Result))
    {
        Samples = Mem_Alloc(ZP_AUDIO_MAX_PACKET_FRAMES * Format->nChannels * sizeof(SHORT));
        if (Samples == NULL) Result = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(Result))
    {
        Status = NtCreateEvent(&AudioEvent, EVENT_MODIFY_STATE | SYNCHRONIZE, NULL, SynchronizationEvent, FALSE);
        if (!NT_SUCCESS(Status)) Result = HRESULT_FROM_NT(Status);
    }
    if (SUCCEEDED(Result))
    {
        Flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST;
        if (Channel->Flow == ZpAudioFlowRender) Flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
        Result = IAudioClient_Initialize(Client,
                                         AUDCLNT_SHAREMODE_SHARED,
                                         Flags,
                                         ZP_AUDIO_BUFFER_DURATION,
                                         0,
                                         Format,
                                         NULL);
    }
    if (SUCCEEDED(Result)) Result = IAudioClient_SetEventHandle(Client, AudioEvent);
    if (SUCCEEDED(Result)) Result = IAudioClient_GetService(Client, &ZpAudioCaptureClientIid, (PVOID*)&Capture);
    if (SUCCEEDED(Result)) Result = IAudioClient_Start(Client);
    if (SUCCEEDED(Result)) Started = TRUE;
    Events[0] = Channel->StopEvent;
    Events[1] = AudioEvent;
    while (SUCCEEDED(Result) && NT_SUCCESS(Status))
    {
        Status = NtWaitForMultipleObjects(ARRAYSIZE(Events), Events, WaitAny, FALSE, NULL);
        if (Status == STATUS_WAIT_0)
        {
            Status = STATUS_CANCELLED;
            break;
        }
        if (Status != STATUS_WAIT_1) break;
        Result = IAudioCaptureClient_GetNextPacketSize(Capture, &NextFrames);
        while (SUCCEEDED(Result) && NextFrames != 0)
        {
            Result = IAudioCaptureClient_GetBuffer(Capture, &Data, &Frames, &Flags, NULL, NULL);
            if (SUCCEEDED(Result))
            {
                Status = ZpAudio_SendPacket(Channel,
                                            Format,
                                            FloatingPoint,
                                            Data,
                                            Frames,
                                            !!(Flags & AUDCLNT_BUFFERFLAGS_SILENT),
                                            Samples);
                Result = IAudioCaptureClient_ReleaseBuffer(Capture, Frames);
            }
            if (SUCCEEDED(Result) && NT_SUCCESS(Status))
            {
                Result = IAudioCaptureClient_GetNextPacketSize(Capture, &NextFrames);
            }
            else
            {
                break;
            }
        }
    }
    if (Started) IAudioClient_Stop(Client);
    if (Capture != NULL) IAudioCaptureClient_Release(Capture);
    if (Client != NULL) IAudioClient_Release(Client);
    if (Device != NULL) IMMDevice_Release(Device);
    CoTaskMemFree(Format);
    if (AudioEvent != NULL) NtClose(AudioEvent);
    Mem_Free(Samples);
    if (Uninitialize) CoUninitialize();
    ZpAudio_FinishWorker(Channel,
                         FAILED(Result) ? ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result) :
                                          ZpStatus_FromNtStatus(Status));
    return Status;
}

static
NTSTATUS
ZpAudio_ChannelWindow(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ULONG CreditBytes)
{
    PZP_CLIENT_AUDIO_CHANNEL Channel = (PZP_CLIENT_AUDIO_CHANNEL)LocalChannel;
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
ZpAudio_ChannelClose(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_AUDIO_CHANNEL Channel = (PZP_CLIENT_AUDIO_CHANNEL)LocalChannel;
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
ZpAudio_ChannelAbort(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_AUDIO_CHANNEL Channel = (PZP_CLIENT_AUDIO_CHANNEL)LocalChannel;

    UNREFERENCED_PARAMETER(Status);
    NtSetEvent(Channel->StopEvent, NULL);
    NtSetEvent(Channel->CreditEvent, NULL);
}

static
VOID
ZpAudio_ChannelDestroy(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel)
{
    PZP_CLIENT_AUDIO_CHANNEL Channel = (PZP_CLIENT_AUDIO_CHANNEL)LocalChannel;

    if (Channel->WorkerThread != NULL) NtClose(Channel->WorkerThread);
    if (Channel->CreditEvent != NULL) NtClose(Channel->CreditEvent);
    if (Channel->StopEvent != NULL) NtClose(Channel->StopEvent);
    Mem_Free(Channel);
}

NTSTATUS
ZpAudio_CreateStreamChannel(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PZP_AUDIO_STREAM_REQUEST_VIEW Request,
    _Out_ PZP_CLIENT_AUDIO_CHANNEL* Channel)
{
    PZP_CLIENT_AUDIO_CHANNEL AudioChannel;
    SIZE_T AllocationSize;
    NTSTATUS Status;

    AllocationSize = FIELD_OFFSET(ZP_CLIENT_AUDIO_CHANNEL, DeviceId) +
                     ((SIZE_T)Request->DeviceId.Length + 1) * sizeof(WCHAR);
    AudioChannel = Mem_Alloc(AllocationSize);
    if (AudioChannel == NULL) return STATUS_NO_MEMORY;
    RtlZeroMemory(AudioChannel, FIELD_OFFSET(ZP_CLIENT_AUDIO_CHANNEL, DeviceId));
    AudioChannel->Flow = Request->Flow;
    AudioChannel->DeviceIdLength = Request->DeviceId.Length;
    if (Request->DeviceId.Length != 0)
    {
        RtlCopyMemory(AudioChannel->DeviceId,
                      Request->DeviceId.Buffer,
                      (SIZE_T)Request->DeviceId.Length * sizeof(WCHAR));
    }
    AudioChannel->DeviceId[Request->DeviceId.Length] = UNICODE_NULL;
    Status = NtCreateEvent(&AudioChannel->CreditEvent,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (NT_SUCCESS(Status))
    {
        Status = NtCreateEvent(&AudioChannel->StopEvent,
                               EVENT_MODIFY_STATE | SYNCHRONIZE,
                               NULL,
                               NotificationEvent,
                               FALSE);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpClientLocalChannel_Insert(Client,
                                             &AudioChannel->Header,
                                             ZP_AUDIO_MODULE_ID,
                                             NULL,
                                             ZpAudio_ChannelWindow,
                                             ZpAudio_ChannelClose,
                                             ZpAudio_ChannelAbort,
                                             ZpAudio_ChannelDestroy);
    }
    if (!NT_SUCCESS(Status))
    {
        if (AudioChannel->StopEvent != NULL) NtClose(AudioChannel->StopEvent);
        if (AudioChannel->CreditEvent != NULL) NtClose(AudioChannel->CreditEvent);
        Mem_Free(AudioChannel);
        return Status;
    }
    *Channel = AudioChannel;
    return STATUS_SUCCESS;
}

VOID
ZpAudio_CommitChannel(
    _Inout_ PZP_CLIENT_AUDIO_CHANNEL Channel,
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
    Status = PS_CreateThread(NtCurrentProcess(), TRUE, ZpAudio_Worker, Channel, &Channel->WorkerThread, NULL);
    if (NT_SUCCESS(Status)) Status = NtResumeThread(Channel->WorkerThread, NULL);
    if (!NT_SUCCESS(Status))
    {
        if (Channel->WorkerThread != NULL) NtTerminateThread(Channel->WorkerThread, Status);
        ZpAudio_FinishWorker(Channel, ZpStatus_FromNtStatus(Status));
    }
}
