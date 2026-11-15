#define COBJMACROS

#include "Encoder.h"

#include <icodecapi.h>
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <objbase.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINDOW_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RECORDING_)

#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfuuid.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Strmiids.lib")

struct _ZP_WINDOW_VIDEO_ENCODER
{
    IMFTransform* Transform;
    IMFMediaEventGenerator* Events;
    ICodecAPI* CodecApi;
    IMFMediaType* OutputType;
    MFT_OUTPUT_STREAM_INFO OutputInfo;
    PBYTE SequenceHeader;
    ULONG SequenceHeaderLength;
    ULONG PendingOutputs;
    ULONG PendingInputs;
    ULONGLONG FirstTimestamp;
    LONGLONG FrameDuration;
    BOOLEAN TimestampStarted;
    BOOLEAN Async;
    BOOLEAN MfStarted;
    BOOLEAN ComInitialized;
};

static
HRESULT
ZpWindowVideoEncoder_Activate(
    _In_ ZP_WINDOW_VIDEO_CODEC Codec,
    _Outptr_ IMFTransform** Transform)
{
    MFT_REGISTER_TYPE_INFO Input = { MFMediaType_Video, MFVideoFormat_NV12 };
    MFT_REGISTER_TYPE_INFO Output = {
        MFMediaType_Video,
        Codec == ZpWindowVideoCodecH264 ? MFVideoFormat_H264 : MFVideoFormat_HEVC
    };
    IMFActivate** Activations;
    UINT32 Count, Index;
    HRESULT Result;

    Result = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                       MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                       &Input,
                       &Output,
                       &Activations,
                       &Count);
    if (FAILED(Result)) return Result;
    Result = MF_E_TOPO_CODEC_NOT_FOUND;
    for (Index = 0; Index < Count; Index++)
    {
        if (FAILED(Result))
        {
            Result = IMFActivate_ActivateObject(Activations[Index],
                                                &IID_IMFTransform,
                                                (PVOID*)Transform);
        }
        IMFActivate_Release(Activations[Index]);
    }
    CoTaskMemFree(Activations);
    return Result;
}

static
HRESULT
ZpWindowVideoEncoder_CreateType(
    _In_ REFGUID Subtype,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ BYTE FrameRate,
    _Outptr_ IMFMediaType** Type)
{
    IMFMediaType* MediaType;
    HRESULT Result;

    Result = MFCreateMediaType(&MediaType);
    if (FAILED(Result)) return Result;
    Result = IMFMediaType_SetGUID(MediaType, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetGUID(MediaType, &MF_MT_SUBTYPE, Subtype);
    if (SUCCEEDED(Result))
    {
        Result = IMFMediaType_SetUINT32(MediaType,
                                       &MF_MT_INTERLACE_MODE,
                                       MFVideoInterlace_Progressive);
    }
    if (SUCCEEDED(Result))
    {
        Result = IMFMediaType_SetUINT64(MediaType, &MF_MT_FRAME_SIZE, ((ULONGLONG)Width << 32) | Height);
    }
    if (SUCCEEDED(Result))
    {
        Result = IMFMediaType_SetUINT64(MediaType, &MF_MT_FRAME_RATE, ((ULONGLONG)FrameRate << 32) | 1);
    }
    if (SUCCEEDED(Result))
    {
        Result = IMFMediaType_SetUINT64(MediaType, &MF_MT_PIXEL_ASPECT_RATIO, ((ULONGLONG)1 << 32) | 1);
    }
    if (FAILED(Result))
    {
        IMFMediaType_Release(MediaType);
        return Result;
    }
    *Type = MediaType;
    return S_OK;
}

static
HRESULT
ZpWindowVideoEncoder_SetProperty(
    _In_ ICodecAPI* CodecApi,
    _In_ const GUID* Key,
    _In_ VARTYPE Type,
    _In_ ULONG Value)
{
    VARIANT Variant;
    HRESULT Result;

    Result = ICodecAPI_IsSupported(CodecApi, Key);
    if (Result != S_OK) return Result == S_FALSE ? E_NOTIMPL : Result;
    Result = ICodecAPI_IsModifiable(CodecApi, Key);
    if (Result != S_OK) return Result == S_FALSE ? HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) : Result;
    VariantInit(&Variant);
    V_VT(&Variant) = Type;
    if (Type == VT_BOOL) V_BOOL(&Variant) = Value ? VARIANT_TRUE : VARIANT_FALSE;
    else V_UI4(&Variant) = Value;
    Result = ICodecAPI_SetValue(CodecApi, Key, &Variant);
    return Result == S_FALSE ? HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) : Result;
}

static
VOID
ZpWindowVideoEncoder_LoadSequenceHeader(
    _Inout_ PZP_WINDOW_VIDEO_ENCODER Encoder)
{
    UINT32 Length;
    PBYTE Header;

    if (Encoder->SequenceHeader != NULL ||
        FAILED(IMFMediaType_GetAllocatedBlob(Encoder->OutputType,
                                             &MF_MT_MPEG_SEQUENCE_HEADER,
                                             &Header,
                                             &Length)))
    {
        return;
    }
    Encoder->SequenceHeader = Mem_Alloc(Length);
    if (Encoder->SequenceHeader != NULL)
    {
        RtlCopyMemory(Encoder->SequenceHeader, Header, Length);
        Encoder->SequenceHeaderLength = Length;
    }
    CoTaskMemFree(Header);
}

static
HRESULT
ZpWindowVideoEncoder_CreateOutputSample(
    _In_ PZP_WINDOW_VIDEO_ENCODER Encoder,
    _Outptr_result_maybenull_ IMFSample** Sample)
{
    IMFMediaBuffer* Buffer = NULL;
    HRESULT Result;

    if (FlagOn(Encoder->OutputInfo.dwFlags,
               MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES))
    {
        *Sample = NULL;
        return S_OK;
    }
    Result = MFCreateSample(Sample);
    if (FAILED(Result)) return Result;
    Result = MFCreateMemoryBuffer(Encoder->OutputInfo.cbSize, &Buffer);
    if (SUCCEEDED(Result)) Result = IMFSample_AddBuffer(*Sample, Buffer);
    if (Buffer != NULL) IMFMediaBuffer_Release(Buffer);
    if (FAILED(Result))
    {
        IMFSample_Release(*Sample);
        *Sample = NULL;
    }
    return Result;
}

static
HRESULT
ZpWindowVideoEncoder_CopyOutput(
    _Inout_ PZP_WINDOW_VIDEO_ENCODER Encoder,
    _In_ IMFSample* Sample,
    _Out_ PZP_WINDOW_VIDEO_FRAME Frame)
{
    IMFMediaBuffer* Buffer;
    PBYTE Data;
    DWORD Length;
    UINT32 CleanPoint = FALSE;
    SIZE_T TotalLength;
    HRESULT Result;

    Result = IMFSample_ConvertToContiguousBuffer(Sample, &Buffer);
    if (FAILED(Result)) return Result;
    Result = IMFMediaBuffer_Lock(Buffer, &Data, NULL, &Length);
    if (SUCCEEDED(Result))
    {
        IMFAttributes_GetUINT32((IMFAttributes*)Sample, &MFSampleExtension_CleanPoint, &CleanPoint);
        if (CleanPoint) ZpWindowVideoEncoder_LoadSequenceHeader(Encoder);
        TotalLength = (SIZE_T)Length + (CleanPoint ? Encoder->SequenceHeaderLength : 0);
        Frame->Data = TotalLength <= MAXULONG ? Mem_Alloc(TotalLength) : NULL;
        if (Frame->Data == NULL)
        {
            Result = E_OUTOFMEMORY;
        }
        else
        {
            if (CleanPoint && Encoder->SequenceHeaderLength != 0)
            {
                RtlCopyMemory(Frame->Data, Encoder->SequenceHeader, Encoder->SequenceHeaderLength);
            }
            RtlCopyMemory(Frame->Data + (CleanPoint ? Encoder->SequenceHeaderLength : 0), Data, Length);
            Frame->Length = (ULONG)TotalLength;
            Frame->KeyFrame = CleanPoint != FALSE;
        }
        IMFMediaBuffer_Unlock(Buffer);
    }
    IMFMediaBuffer_Release(Buffer);
    return Result;
}

static
HRESULT
ZpWindowVideoEncoder_UpdateOutputType(
    _Inout_ PZP_WINDOW_VIDEO_ENCODER Encoder)
{
    IMFMediaType* Type;
    HRESULT Result;

    Result = IMFTransform_GetOutputAvailableType(Encoder->Transform, 0, 0, &Type);
    if (FAILED(Result)) return Result;
    Result = IMFTransform_SetOutputType(Encoder->Transform, 0, Type, 0);
    if (SUCCEEDED(Result)) Result = IMFTransform_GetOutputStreamInfo(Encoder->Transform, 0, &Encoder->OutputInfo);
    if (FAILED(Result))
    {
        IMFMediaType_Release(Type);
        return Result;
    }
    IMFMediaType_Release(Encoder->OutputType);
    Encoder->OutputType = Type;
    Mem_Free(Encoder->SequenceHeader);
    Encoder->SequenceHeader = NULL;
    Encoder->SequenceHeaderLength = 0;
    ZpWindowVideoEncoder_LoadSequenceHeader(Encoder);
    return S_OK;
}

static
HRESULT
ZpWindowVideoEncoder_ProcessOutput(
    _Inout_ PZP_WINDOW_VIDEO_ENCODER Encoder,
    _Out_ PZP_WINDOW_VIDEO_FRAME Frame)
{
    MFT_OUTPUT_DATA_BUFFER Output;
    DWORD Status;
    HRESULT Result;

    for (;;)
    {
        Output.dwStreamID = 0;
        Output.dwStatus = 0;
        Output.pEvents = NULL;
        Result = ZpWindowVideoEncoder_CreateOutputSample(Encoder, &Output.pSample);
        if (FAILED(Result)) return Result;
        Result = IMFTransform_ProcessOutput(Encoder->Transform, 0, 1, &Output, &Status);
        if (Output.pEvents != NULL) IMFCollection_Release(Output.pEvents);
        if (SUCCEEDED(Result)) Result = ZpWindowVideoEncoder_CopyOutput(Encoder, Output.pSample, Frame);
        if (Output.pSample != NULL) IMFSample_Release(Output.pSample);
        if (Result != MF_E_TRANSFORM_STREAM_CHANGE) return Result;
        Result = ZpWindowVideoEncoder_UpdateOutputType(Encoder);
        if (FAILED(Result)) return Result;
        if (Encoder->Async) return S_FALSE;
    }
}

static
HRESULT
ZpWindowVideoEncoder_GetEvent(
    _In_ PZP_WINDOW_VIDEO_ENCODER Encoder,
    _In_ DWORD Flags,
    _Out_ MediaEventType* Type)
{
    IMFMediaEvent* Event;
    HRESULT Result, EventStatus;

    Result = IMFMediaEventGenerator_GetEvent(Encoder->Events, Flags, &Event);
    if (FAILED(Result)) return Result;
    Result = IMFMediaEvent_GetStatus(Event, &EventStatus);
    if (SUCCEEDED(Result)) Result = EventStatus;
    if (SUCCEEDED(Result)) Result = IMFMediaEvent_GetType(Event, Type);
    IMFMediaEvent_Release(Event);
    return Result;
}

HRESULT
ZpWindowVideoEncoder_Receive(
    _Inout_ PZP_WINDOW_VIDEO_ENCODER Encoder,
    _Out_ PZP_WINDOW_VIDEO_FRAME Frame)
{
    MediaEventType Type;
    HRESULT Result;

    if (Encoder->Async)
    {
        while (Encoder->PendingOutputs == 0)
        {
            Result = ZpWindowVideoEncoder_GetEvent(Encoder, MF_EVENT_FLAG_NO_WAIT, &Type);
            if (Result == MF_E_NO_EVENTS_AVAILABLE) return S_FALSE;
            if (FAILED(Result)) return Result;
            if (Type == METransformNeedInput) Encoder->PendingInputs++;
            else if (Type == METransformHaveOutput) Encoder->PendingOutputs++;
        }
        Encoder->PendingOutputs--;
    }
    Result = ZpWindowVideoEncoder_ProcessOutput(Encoder, Frame);
    return Result == MF_E_TRANSFORM_NEED_MORE_INPUT ? S_FALSE : Result;
}

static
HRESULT
ZpWindowVideoEncoder_CheckWait(
    _In_opt_ HANDLE CancelEvent,
    _In_ ULONGLONG Deadline)
{
    LARGE_INTEGER Zero = { 0 };
    NTSTATUS Status;

    if (CancelEvent != NULL)
    {
        Status = NtWaitForSingleObject(CancelEvent, FALSE, &Zero);
        if (Status == STATUS_SUCCESS) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        if (Status != STATUS_TIMEOUT) return HRESULT_FROM_NT(Status);
    }
    return GetTickCount64() >= Deadline ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : S_OK;
}

static
HRESULT
ZpWindowVideoEncoder_EncodeAsync(
    _Inout_ PZP_WINDOW_VIDEO_ENCODER Encoder,
    _In_ IMFSample* Sample,
    _In_opt_ HANDLE CancelEvent,
    _In_ ULONGLONG Deadline,
    _Out_ PZP_WINDOW_VIDEO_FRAME Frame)
{
    MediaEventType Type;
    LOGICAL OutputReady = FALSE;
    LARGE_INTEGER Delay;
    ULONGLONG Now;
    NTSTATUS Status;
    HRESULT Result;

    if (Encoder->PendingOutputs != 0)
    {
        Result = ZpWindowVideoEncoder_Receive(Encoder, Frame);
        if (FAILED(Result)) return Result;
        OutputReady = Result == S_OK;
    }
    while (Encoder->PendingInputs == 0)
    {
        Result = ZpWindowVideoEncoder_CheckWait(CancelEvent, Deadline);
        if (FAILED(Result)) goto Failure;
        Result = ZpWindowVideoEncoder_GetEvent(Encoder, MF_EVENT_FLAG_NO_WAIT, &Type);
        if (Result == MF_E_NO_EVENTS_AVAILABLE)
        {
            Now = GetTickCount64();
            if (Now >= Deadline)
            {
                Result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                goto Failure;
            }
            Delay.QuadPart = -(LONGLONG)min(Deadline - Now, 5) * 10000;
            Status = CancelEvent != NULL ? NtWaitForSingleObject(CancelEvent, FALSE, &Delay) :
                                          NtDelayExecution(FALSE, &Delay);
            if (CancelEvent != NULL && Status == STATUS_SUCCESS)
            {
                Result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
                goto Failure;
            }
            if (Status == STATUS_TIMEOUT || (CancelEvent == NULL && Status == STATUS_SUCCESS)) continue;
            Result = HRESULT_FROM_NT(Status);
            goto Failure;
        }
        if (FAILED(Result)) goto Failure;
        if (Type == METransformNeedInput) Encoder->PendingInputs++;
        else if (Type == METransformHaveOutput)
        {
            Encoder->PendingOutputs++;
            if (!OutputReady)
            {
                Result = ZpWindowVideoEncoder_Receive(Encoder, Frame);
                if (FAILED(Result)) goto Failure;
                OutputReady = Result == S_OK;
            }
        }
    }
    Result = ZpWindowVideoEncoder_CheckWait(CancelEvent, Deadline);
    if (FAILED(Result)) goto Failure;
    Result = IMFTransform_ProcessInput(Encoder->Transform, 0, Sample, 0);
    if (FAILED(Result)) goto Failure;
    Encoder->PendingInputs--;
    if (OutputReady) return S_OK;
    return ZpWindowVideoEncoder_Receive(Encoder, Frame);

Failure:
    if (OutputReady) ZpWindowVideoEncoder_FreeFrame(Frame);
    return Result;
}

HRESULT
ZpWindowVideoEncoder_Create(
    _In_ ZP_WINDOW_VIDEO_CODEC Codec,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ BYTE FrameRate,
    _In_ ULONG BitRate,
    _In_ IMFDXGIDeviceManager* DeviceManager,
    _Out_ PZP_WINDOW_VIDEO_ENCODER* Encoder)
{
    PZP_WINDOW_VIDEO_ENCODER Object;
    IMFMediaType* InputType = NULL;
    UINT32 Async = FALSE;
    HRESULT Result;

    if (Codec > ZpWindowVideoCodecH265 || Width == 0 || Height == 0 || FrameRate == 0 ||
        BitRate == 0 || DeviceManager == NULL)
    {
        return E_INVALIDARG;
    }
    Object = Mem_Alloc(sizeof(*Object));
    if (Object == NULL) return E_OUTOFMEMORY;
    RtlZeroMemory(Object, sizeof(*Object));
    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Object->ComInitialized = SUCCEEDED(Result);
    if (Result == RPC_E_CHANGED_MODE) Result = S_OK;
    if (SUCCEEDED(Result)) Result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    Object->MfStarted = SUCCEEDED(Result);
    if (SUCCEEDED(Result)) Result = ZpWindowVideoEncoder_Activate(Codec, &Object->Transform);
    if (SUCCEEDED(Result))
    {
        IMFAttributes* Attributes;

        Result = IMFTransform_GetAttributes(Object->Transform, &Attributes);
        if (SUCCEEDED(Result))
        {
            IMFAttributes_GetUINT32(Attributes, &MF_TRANSFORM_ASYNC, &Async);
            if (Async) Result = IMFAttributes_SetUINT32(Attributes, &MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
            IMFAttributes_Release(Attributes);
        }
    }
    Object->Async = Async != FALSE;
    if (SUCCEEDED(Result))
    {
        Result = IMFTransform_ProcessMessage(Object->Transform,
                                             MFT_MESSAGE_SET_D3D_MANAGER,
                                             (ULONG_PTR)DeviceManager);
    }
    if (SUCCEEDED(Result))
    {
        Result = IMFTransform_QueryInterface(Object->Transform, &IID_ICodecAPI, (PVOID*)&Object->CodecApi);
    }
    if (SUCCEEDED(Result))
    {
        Result = ZpWindowVideoEncoder_SetProperty(Object->CodecApi, &CODECAPI_AVLowLatencyMode, VT_BOOL, TRUE);
    }
    if (SUCCEEDED(Result))
    {
        Result = ZpWindowVideoEncoder_SetProperty(Object->CodecApi,
                                                  &CODECAPI_AVEncMPVDefaultBPictureCount,
                                                  VT_UI4,
                                                  0);
    }
    if (SUCCEEDED(Result))
    {
        Result = ZpWindowVideoEncoder_SetProperty(Object->CodecApi,
                                                  &CODECAPI_AVEncCommonRateControlMode,
                                                  VT_UI4,
                                                  eAVEncCommonRateControlMode_CBR);
    }
    if (SUCCEEDED(Result))
    {
        Result = ZpWindowVideoEncoder_SetProperty(Object->CodecApi,
                                                  &CODECAPI_AVEncMPVGOPSize,
                                                  VT_UI4,
                                                  FrameRate * 2);
    }
    if (SUCCEEDED(Result))
    {
        Result = ZpWindowVideoEncoder_CreateType(Codec == ZpWindowVideoCodecH264 ?
                                                     &MFVideoFormat_H264 : &MFVideoFormat_HEVC,
                                                 Width,
                                                 Height,
                                                 FrameRate,
                                                 &Object->OutputType);
    }
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT32(Object->OutputType, &MF_MT_AVG_BITRATE, BitRate);
    if (SUCCEEDED(Result))
    {
        Result = IMFMediaType_SetUINT32(Object->OutputType,
                                        &MF_MT_MPEG2_PROFILE,
                                        Codec == ZpWindowVideoCodecH264 ?
                                            eAVEncH264VProfile_Main : eAVEncH265VProfile_Main_420_8);
    }
    if (SUCCEEDED(Result)) Result = IMFTransform_SetOutputType(Object->Transform, 0, Object->OutputType, 0);
    if (SUCCEEDED(Result))
    {
        Result = ZpWindowVideoEncoder_CreateType(&MFVideoFormat_NV12,
                                                 Width,
                                                 Height,
                                                 FrameRate,
                                                 &InputType);
    }
    if (SUCCEEDED(Result)) Result = IMFTransform_SetInputType(Object->Transform, 0, InputType, 0);
    if (InputType != NULL) IMFMediaType_Release(InputType);
    if (SUCCEEDED(Result))
    {
        Result = IMFTransform_GetOutputStreamInfo(Object->Transform, 0, &Object->OutputInfo);
    }
    if (SUCCEEDED(Result) && Object->Async)
    {
        Result = IMFTransform_QueryInterface(Object->Transform,
                                             &IID_IMFMediaEventGenerator,
                                             (PVOID*)&Object->Events);
    }
    if (SUCCEEDED(Result))
    {
        Result = IMFTransform_ProcessMessage(Object->Transform,
                                             MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,
                                             0);
    }
    if (SUCCEEDED(Result))
    {
        Result = IMFTransform_ProcessMessage(Object->Transform,
                                             MFT_MESSAGE_NOTIFY_START_OF_STREAM,
                                             0);
    }
    if (FAILED(Result))
    {
        ZpWindowVideoEncoder_Close(Object);
        return Result;
    }
    Object->FrameDuration = 10000000LL / FrameRate;
    ZpWindowVideoEncoder_LoadSequenceHeader(Object);
    *Encoder = Object;
    return S_OK;
}

HRESULT
ZpWindowVideoEncoder_UpdateBitRate(
    _Inout_ PZP_WINDOW_VIDEO_ENCODER Encoder,
    _In_ ULONG BitRate)
{
    if (BitRate == 0) return E_INVALIDARG;
    return ZpWindowVideoEncoder_SetProperty(Encoder->CodecApi,
                                            &CODECAPI_AVEncCommonMeanBitRate,
                                            VT_UI4,
                                            BitRate);
}

HRESULT
ZpWindowVideoEncoder_Encode(
    _Inout_ PZP_WINDOW_VIDEO_ENCODER Encoder,
    _In_ IMFSample* Sample,
    _In_ ULONGLONG Timestamp,
    _In_ LOGICAL ForceKeyFrame,
    _In_opt_ HANDLE CancelEvent,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_VIDEO_FRAME Frame)
{
    ULONGLONG Deadline = GetTickCount64() + TimeoutMilliseconds;
    HRESULT Result;

    if (TimeoutMilliseconds == INFINITE) return E_INVALIDARG;
    Result = ZpWindowVideoEncoder_CheckWait(CancelEvent, Deadline);
    if (FAILED(Result)) return Result;
    if (!Encoder->TimestampStarted)
    {
        Encoder->FirstTimestamp = Timestamp;
        Encoder->TimestampStarted = TRUE;
    }
    Result = IMFSample_SetSampleTime(Sample, (LONGLONG)(Timestamp - Encoder->FirstTimestamp));
    if (SUCCEEDED(Result)) Result = IMFSample_SetSampleDuration(Sample, Encoder->FrameDuration);
    if (SUCCEEDED(Result) && ForceKeyFrame)
    {
        Result = ZpWindowVideoEncoder_SetProperty(Encoder->CodecApi,
                                                  &CODECAPI_AVEncVideoForceKeyFrame,
                                                  VT_UI4,
                                                  TRUE);
    }
    if (FAILED(Result)) return Result;
    if (Encoder->Async)
    {
        Result = ZpWindowVideoEncoder_EncodeAsync(Encoder, Sample, CancelEvent, Deadline, Frame);
    }
    else
    {
        Result = ZpWindowVideoEncoder_CheckWait(CancelEvent, Deadline);
        if (SUCCEEDED(Result)) Result = IMFTransform_ProcessInput(Encoder->Transform, 0, Sample, 0);
        if (SUCCEEDED(Result)) Result = ZpWindowVideoEncoder_ProcessOutput(Encoder, Frame);
    }
    return Result == MF_E_TRANSFORM_NEED_MORE_INPUT ? S_FALSE : Result;
}

VOID
ZpWindowVideoEncoder_FreeFrame(
    _Inout_ PZP_WINDOW_VIDEO_FRAME Frame)
{
    Mem_Free(Frame->Data);
}

VOID
ZpWindowVideoEncoder_Close(
    _In_opt_ PZP_WINDOW_VIDEO_ENCODER Encoder)
{
    IMFShutdown* Shutdown;

    if (Encoder == NULL) return;
    if (Encoder->Transform != NULL)
    {
        IMFTransform_ProcessMessage(Encoder->Transform, MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        IMFTransform_ProcessMessage(Encoder->Transform, MFT_MESSAGE_COMMAND_FLUSH, 0);
        if (SUCCEEDED(IMFTransform_QueryInterface(Encoder->Transform, &IID_IMFShutdown, (PVOID*)&Shutdown)))
        {
            IMFShutdown_Shutdown(Shutdown);
            IMFShutdown_Release(Shutdown);
        }
    }
    Mem_Free(Encoder->SequenceHeader);
    if (Encoder->OutputType != NULL) IMFMediaType_Release(Encoder->OutputType);
    if (Encoder->CodecApi != NULL) ICodecAPI_Release(Encoder->CodecApi);
    if (Encoder->Events != NULL) IMFMediaEventGenerator_Release(Encoder->Events);
    if (Encoder->Transform != NULL) IMFTransform_Release(Encoder->Transform);
    if (Encoder->MfStarted) MFShutdown();
    if (Encoder->ComInitialized) CoUninitialize();
    Mem_Free(Encoder);
}

#endif
