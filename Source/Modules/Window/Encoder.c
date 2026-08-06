#define COBJMACROS

#include "Encoder.h"

#include <icodecapi.h>
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <objbase.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

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
    ULONGLONG FirstTimestamp;
    LONGLONG FrameDuration;
    BOOLEAN TimestampStarted;
    BOOLEAN Async;
    BOOLEAN NeedInput;
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
VOID
ZpWindowVideoEncoder_SetUInt32(
    _In_opt_ ICodecAPI* CodecApi,
    _In_ const GUID* Key,
    _In_ ULONG Value)
{
    VARIANT Variant;

    if (CodecApi == NULL) return;
    VariantInit(&Variant);
    V_VT(&Variant) = VT_UI4;
    V_UI4(&Variant) = Value;
    ICodecAPI_SetValue(CodecApi, Key, &Variant);
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
    _Out_ MediaEventType* Type)
{
    IMFMediaEvent* Event;
    HRESULT Result, EventStatus;

    Result = IMFMediaEventGenerator_GetEvent(Encoder->Events, 0, &Event);
    if (FAILED(Result)) return Result;
    Result = IMFMediaEvent_GetStatus(Event, &EventStatus);
    if (SUCCEEDED(Result)) Result = EventStatus;
    if (SUCCEEDED(Result)) Result = IMFMediaEvent_GetType(Event, Type);
    IMFMediaEvent_Release(Event);
    return Result;
}

static
HRESULT
ZpWindowVideoEncoder_EncodeAsync(
    _Inout_ PZP_WINDOW_VIDEO_ENCODER Encoder,
    _In_ IMFSample* Sample,
    _Out_ PZP_WINDOW_VIDEO_FRAME Frame)
{
    MediaEventType Type;
    LOGICAL OutputReady = FALSE;
    HRESULT Result;

    while (!Encoder->NeedInput)
    {
        Result = ZpWindowVideoEncoder_GetEvent(Encoder, &Type);
        if (FAILED(Result)) return Result;
        if (Type == METransformNeedInput) Encoder->NeedInput = TRUE;
        else if (Type == METransformHaveOutput)
        {
            Result = ZpWindowVideoEncoder_ProcessOutput(Encoder, Frame);
            if (FAILED(Result)) return Result;
            if (Result == S_OK) OutputReady = TRUE;
        }
    }
    Result = IMFTransform_ProcessInput(Encoder->Transform, 0, Sample, 0);
    if (FAILED(Result))
    {
        if (OutputReady) ZpWindowVideoEncoder_FreeFrame(Frame);
        return Result;
    }
    Encoder->NeedInput = FALSE;
    if (OutputReady) return S_OK;
    for (;;)
    {
        Result = ZpWindowVideoEncoder_GetEvent(Encoder, &Type);
        if (FAILED(Result)) return Result;
        if (Type == METransformNeedInput)
        {
            Encoder->NeedInput = TRUE;
            return S_FALSE;
        }
        else if (Type == METransformHaveOutput)
        {
            return ZpWindowVideoEncoder_ProcessOutput(Encoder, Frame);
        }
    }
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
        IMFTransform_QueryInterface(Object->Transform, &IID_ICodecAPI, (PVOID*)&Object->CodecApi);
        ZpWindowVideoEncoder_SetUInt32(Object->CodecApi, &CODECAPI_AVLowLatencyMode, TRUE);
        ZpWindowVideoEncoder_SetUInt32(Object->CodecApi, &CODECAPI_AVEncMPVDefaultBPictureCount, 0);
        ZpWindowVideoEncoder_SetUInt32(Object->CodecApi,
                                      &CODECAPI_AVEncVideoMaxKeyframeDistance,
                                      FrameRate * 2);
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
ZpWindowVideoEncoder_Encode(
    _Inout_ PZP_WINDOW_VIDEO_ENCODER Encoder,
    _In_ IMFSample* Sample,
    _In_ ULONGLONG Timestamp,
    _In_ LOGICAL ForceKeyFrame,
    _Out_ PZP_WINDOW_VIDEO_FRAME Frame)
{
    HRESULT Result;

    if (!Encoder->TimestampStarted)
    {
        Encoder->FirstTimestamp = Timestamp;
        Encoder->TimestampStarted = TRUE;
    }
    Result = IMFSample_SetSampleTime(Sample, (LONGLONG)(Timestamp - Encoder->FirstTimestamp));
    if (SUCCEEDED(Result)) Result = IMFSample_SetSampleDuration(Sample, Encoder->FrameDuration);
    if (SUCCEEDED(Result) && ForceKeyFrame)
    {
        ZpWindowVideoEncoder_SetUInt32(Encoder->CodecApi, &CODECAPI_AVEncVideoForceKeyFrame, TRUE);
    }
    if (FAILED(Result)) return Result;
    if (Encoder->Async)
    {
        Result = ZpWindowVideoEncoder_EncodeAsync(Encoder, Sample, Frame);
    }
    else
    {
        Result = IMFTransform_ProcessInput(Encoder->Transform, 0, Sample, 0);
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
    if (Encoder == NULL) return;
    if (Encoder->Transform != NULL)
    {
        IMFTransform_ProcessMessage(Encoder->Transform, MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        IMFTransform_ProcessMessage(Encoder->Transform, MFT_MESSAGE_COMMAND_FLUSH, 0);
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
