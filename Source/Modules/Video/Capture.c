#define COBJMACROS

#include "Capture.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wincodec.h>
#include <float.h>
#include <math.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mf.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "Mfuuid.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Windowscodecs.lib")

struct _ZP_VIDEO_CAPTURE
{
    IMFMediaSource* Source;
    IMFSourceReader* Reader;
    IWICImagingFactory* ImagingFactory;
    ULONG Width;
    ULONG Height;
    LOGICAL Started;
};

static
HRESULT
ZpVideoCapture_GetDevices(
    _Outptr_result_buffer_(*Count) IMFActivate*** Devices,
    _Out_ PUINT32 Count)
{
    IMFAttributes* Attributes;
    HRESULT Result;

    Result = MFCreateAttributes(&Attributes, 1);
    if (FAILED(Result)) return Result;
    Result = IMFAttributes_SetGUID(Attributes,
                                   &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                   &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (SUCCEEDED(Result)) Result = MFEnumDeviceSources(Attributes, Devices, Count);
    IMFAttributes_Release(Attributes);
    return Result;
}

static
HRESULT
ZpVideoCapture_GetFormats(
    _In_ IMFActivate* Activation,
    _Outptr_result_buffer_(*Count) PZP_VIDEO_FORMAT* Formats,
    _Out_ PULONG Count)
{
    IMFMediaSource* Source = NULL;
    IMFPresentationDescriptor* Presentation = NULL;
    IMFStreamDescriptor* Stream = NULL;
    IMFMediaTypeHandler* Handler = NULL;
    IMFMediaType* Type = NULL;
    PZP_VIDEO_FORMAT Records;
    GUID MajorType;
    UINT32 StreamCount, TypeCount, Numerator, Denominator, Width, Height;
    ULONGLONG Attribute;
    ULONG RecordCount = 0, StreamIndex, TypeIndex, Index;
    BOOL Selected;
    HRESULT Result;

    Records = Mem_Alloc(ZP_VIDEO_MAX_FORMATS * sizeof(*Records));
    if (Records == NULL) return E_OUTOFMEMORY;
    Result = IMFActivate_ActivateObject(Activation, &IID_IMFMediaSource, (PVOID*)&Source);
    if (SUCCEEDED(Result)) Result = IMFMediaSource_CreatePresentationDescriptor(Source, &Presentation);
    if (SUCCEEDED(Result)) Result = IMFPresentationDescriptor_GetStreamDescriptorCount(Presentation, &StreamCount);
    for (StreamIndex = 0; SUCCEEDED(Result) && StreamIndex < StreamCount; StreamIndex++)
    {
        Result = IMFPresentationDescriptor_GetStreamDescriptorByIndex(Presentation,
                                                                       StreamIndex,
                                                                       &Selected,
                                                                       &Stream);
        UNREFERENCED_PARAMETER(Selected);
        if (SUCCEEDED(Result)) Result = IMFStreamDescriptor_GetMediaTypeHandler(Stream, &Handler);
        if (SUCCEEDED(Result)) Result = IMFMediaTypeHandler_GetMajorType(Handler, &MajorType);
        if (SUCCEEDED(Result) && IsEqualGUID(&MajorType, &MFMediaType_Video))
        {
            Result = IMFMediaTypeHandler_GetMediaTypeCount(Handler, &TypeCount);
            for (TypeIndex = 0; SUCCEEDED(Result) && TypeIndex < TypeCount; TypeIndex++)
            {
                Result = IMFMediaTypeHandler_GetMediaTypeByIndex(Handler, TypeIndex, &Type);
                if (SUCCEEDED(Result)) Result = IMFMediaType_GetUINT64(Type, &MF_MT_FRAME_SIZE, &Attribute);
                if (SUCCEEDED(Result))
                {
                    Width = (UINT32)(Attribute >> 32);
                    Height = (UINT32)Attribute;
                    Result = IMFMediaType_GetUINT64(Type, &MF_MT_FRAME_RATE, &Attribute);
                }
                if (SUCCEEDED(Result))
                {
                    Numerator = (UINT32)(Attribute >> 32);
                    Denominator = (UINT32)Attribute;
                }
                if (SUCCEEDED(Result) && Width != 0 && Width <= ZP_VIDEO_MAX_DIMENSION && Height != 0 &&
                    Height <= ZP_VIDEO_MAX_DIMENSION && Numerator != 0 && Denominator != 0 &&
                    Numerator <= (ULONGLONG)Denominator * ZP_VIDEO_MAX_FRAME_RATE)
                {
                    for (Index = 0; Index < RecordCount; Index++)
                    {
                        if (Records[Index].Width == Width && Records[Index].Height == Height &&
                            Records[Index].FrameRateNumerator == Numerator &&
                            Records[Index].FrameRateDenominator == Denominator) break;
                    }
                    if (Index == RecordCount)
                    {
                        if (RecordCount == ZP_VIDEO_MAX_FORMATS)
                        {
                            Result = HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
                        }
                        else
                        {
                            Records[RecordCount++] = (ZP_VIDEO_FORMAT){ Width, Height, Numerator, Denominator };
                        }
                    }
                }
                if (Type != NULL)
                {
                    IMFMediaType_Release(Type);
                    Type = NULL;
                }
            }
        }
        if (Handler != NULL)
        {
            IMFMediaTypeHandler_Release(Handler);
            Handler = NULL;
        }
        if (Stream != NULL)
        {
            IMFStreamDescriptor_Release(Stream);
            Stream = NULL;
        }
    }
    if (SUCCEEDED(Result) && RecordCount == 0) Result = MF_E_INVALIDMEDIATYPE;
    if (Type != NULL) IMFMediaType_Release(Type);
    if (Handler != NULL) IMFMediaTypeHandler_Release(Handler);
    if (Stream != NULL) IMFStreamDescriptor_Release(Stream);
    if (Presentation != NULL) IMFPresentationDescriptor_Release(Presentation);
    if (Source != NULL)
    {
        IMFMediaSource_Shutdown(Source);
        IMFMediaSource_Release(Source);
    }
    if (FAILED(Result))
    {
        Mem_Free(Records);
        return Result;
    }
    *Formats = Records;
    *Count = RecordCount;
    return S_OK;
}

HRESULT
ZpVideoCapture_SelectFormat(
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ULONG MaximumDimension,
    _In_ USHORT FrameRate,
    _Out_ PZP_VIDEO_FORMAT Format)
{
    IMFActivate** Activations = NULL;
    PZP_VIDEO_FORMAT Formats = NULL;
    LPWSTR Id = NULL;
    UINT32 Count, IdLength, Index, FormatCount, FormatIndex, BestIndex = 0;
    ULONG Dimension, DimensionScore, BestDimensionScore = MAXULONG;
    double RateScore, BestRateScore = DBL_MAX;
    HRESULT Result;

    Result = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(Result)) return Result;
    Result = ZpVideoCapture_GetDevices(&Activations, &Count);
    for (Index = 0; SUCCEEDED(Result) && Index < Count; Index++)
    {
        Result = IMFActivate_GetAllocatedString(Activations[Index],
                                                 &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                                                 &Id,
                                                 &IdLength);
        if (SUCCEEDED(Result) && IdLength == DeviceIdLength &&
            _wcsnicmp(Id, DeviceId, DeviceIdLength) == 0)
        {
            Result = ZpVideoCapture_GetFormats(Activations[Index], &Formats, &FormatCount);
            CoTaskMemFree(Id);
            Id = NULL;
            break;
        }
        CoTaskMemFree(Id);
        Id = NULL;
    }
    if (SUCCEEDED(Result) && Index == Count) Result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    for (FormatIndex = 0; SUCCEEDED(Result) && FormatIndex < FormatCount; FormatIndex++)
    {
        Dimension = max(Formats[FormatIndex].Width, Formats[FormatIndex].Height);
        DimensionScore = Dimension <= MaximumDimension ? MaximumDimension - Dimension :
                             ZP_VIDEO_MAX_DIMENSION + Dimension - MaximumDimension;
        RateScore = fabs((double)Formats[FormatIndex].FrameRateNumerator /
                         Formats[FormatIndex].FrameRateDenominator - FrameRate);
        if (DimensionScore < BestDimensionScore ||
            (DimensionScore == BestDimensionScore && RateScore < BestRateScore))
        {
            BestIndex = FormatIndex;
            BestDimensionScore = DimensionScore;
            BestRateScore = RateScore;
        }
    }
    if (SUCCEEDED(Result)) *Format = Formats[BestIndex];
    Mem_Free(Formats);
    CoTaskMemFree(Id);
    for (Index = 0; Activations != NULL && Index < Count; Index++) IMFActivate_Release(Activations[Index]);
    CoTaskMemFree(Activations);
    MFShutdown();
    return Result;
}

HRESULT
ZpVideoCapture_Enumerate(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    IMFActivate** Activations = NULL;
    PZP_VIDEO_DEVICE Devices = NULL;
    PBYTE Encoded = NULL;
    LPWSTR Id, Name;
    UINT32 Count = 0, IdLength, NameLength, Index, EncodedLength;
    HRESULT Result;
    NTSTATUS Status;

    Result = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(Result)) return Result;
    Result = ZpVideoCapture_GetDevices(&Activations, &Count);
    if (SUCCEEDED(Result) && Count > ZP_VIDEO_MAX_DEVICES) Result = HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    if (SUCCEEDED(Result) && Count != 0)
    {
        Devices = Mem_Alloc((SIZE_T)Count * sizeof(*Devices));
        if (Devices == NULL) Result = E_OUTOFMEMORY;
        else RtlZeroMemory(Devices, (SIZE_T)Count * sizeof(*Devices));
    }
    for (Index = 0; SUCCEEDED(Result) && Index < Count; Index++)
    {
        Id = Name = NULL;
        Result = IMFActivate_GetAllocatedString(Activations[Index],
                                                 &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                                                 &Id,
                                                 &IdLength);
        if (SUCCEEDED(Result))
        {
            Result = IMFActivate_GetAllocatedString(Activations[Index],
                                                     &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                                     &Name,
                                                     &NameLength);
        }
        if (SUCCEEDED(Result))
        {
            Result = ZpVideoCapture_GetFormats(Activations[Index],
                                               (PZP_VIDEO_FORMAT*)&Devices[Index].Formats,
                                               &Devices[Index].FormatCount);
        }
        if (SUCCEEDED(Result))
        {
            Devices[Index].Id = Id;
            Devices[Index].IdLength = IdLength;
            Devices[Index].Name = Name;
            Devices[Index].NameLength = NameLength;
        }
        else
        {
            CoTaskMemFree(Name);
            CoTaskMemFree(Id);
        }
    }
    Status = SUCCEEDED(Result) ? ZpVideo_EncodeDeviceList(Devices, Count, NULL, 0, &EncodedLength) : STATUS_SUCCESS;
    Encoded = SUCCEEDED(Result) && NT_SUCCESS(Status) ? Mem_Alloc(EncodedLength) : NULL;
    if (SUCCEEDED(Result) && (!NT_SUCCESS(Status) || Encoded == NULL))
    {
        Result = NT_SUCCESS(Status) ? E_OUTOFMEMORY : HRESULT_FROM_NT(Status);
    }
    if (SUCCEEDED(Result))
    {
        Status = ZpVideo_EncodeDeviceList(Devices, Count, Encoded, EncodedLength, ResponseLength);
        if (!NT_SUCCESS(Status)) Result = HRESULT_FROM_NT(Status);
    }
    if (FAILED(Result)) Mem_Free(Encoded);
    for (Index = 0; Index < Count; Index++)
    {
        if (Devices != NULL)
        {
            CoTaskMemFree((PVOID)Devices[Index].Name);
            CoTaskMemFree((PVOID)Devices[Index].Id);
            Mem_Free((PVOID)Devices[Index].Formats);
        }
        if (Activations != NULL) IMFActivate_Release(Activations[Index]);
    }
    Mem_Free(Devices);
    CoTaskMemFree(Activations);
    MFShutdown();
    if (SUCCEEDED(Result)) *Response = Encoded;
    return Result;
}

static
HRESULT
ZpVideoCapture_Activate(
    _In_ PZP_VIDEO_STREAM_REQUEST_VIEW Request,
    _Outptr_ IMFMediaSource** Source)
{
    IMFActivate** Activations = NULL;
    LPWSTR Id = NULL;
    UINT32 Count = 0, IdLength, Index;
    HRESULT Result;

    Result = ZpVideoCapture_GetDevices(&Activations, &Count);
    for (Index = 0; SUCCEEDED(Result) && Index < Count; Index++)
    {
        Result = IMFActivate_GetAllocatedString(Activations[Index],
                                                 &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                                                 &Id,
                                                 &IdLength);
        if (SUCCEEDED(Result) && IdLength == Request->DeviceId.Length &&
            _wcsnicmp(Id, (PCWCH)Request->DeviceId.Buffer, IdLength) == 0)
        {
            Result = IMFActivate_ActivateObject(Activations[Index], &IID_IMFMediaSource, (PVOID*)Source);
            CoTaskMemFree(Id);
            break;
        }
        CoTaskMemFree(Id);
        Id = NULL;
    }
    if (SUCCEEDED(Result) && Index == Count) Result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    for (Index = 0; Index < Count; Index++) IMFActivate_Release(Activations[Index]);
    CoTaskMemFree(Activations);
    return Result;
}

static
HRESULT
ZpVideoCapture_SelectReaderFormat(
    _In_ IMFSourceReader* Reader,
    _In_ PZP_VIDEO_STREAM_REQUEST_VIEW Request)
{
    IMFMediaType* Type = NULL;
    ULONGLONG Size, Rate;
    DWORD Index;
    HRESULT Result;

    for (Index = 0;; Index++)
    {
        Result = IMFSourceReader_GetNativeMediaType(Reader,
                                                    MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                    Index,
                                                    &Type);
        if (Result == MF_E_NO_MORE_TYPES) return MF_E_INVALIDMEDIATYPE;
        if (FAILED(Result)) return Result;
        Result = IMFMediaType_GetUINT64(Type, &MF_MT_FRAME_SIZE, &Size);
        if (SUCCEEDED(Result)) Result = IMFMediaType_GetUINT64(Type, &MF_MT_FRAME_RATE, &Rate);
        if (SUCCEEDED(Result) && (ULONG)(Size >> 32) == Request->Width &&
            (ULONG)Size == Request->Height && (ULONG)(Rate >> 32) == Request->FrameRateNumerator &&
            (ULONG)Rate == Request->FrameRateDenominator)
        {
            Result = IMFSourceReader_SetCurrentMediaType(Reader,
                                                         MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                         NULL,
                                                         Type);
            IMFMediaType_Release(Type);
            return Result;
        }
        IMFMediaType_Release(Type);
        Type = NULL;
    }
}

HRESULT
ZpVideoCapture_Create(
    _In_ PZP_VIDEO_STREAM_REQUEST_VIEW Request,
    _Out_ PZP_VIDEO_CAPTURE* Capture)
{
    PZP_VIDEO_CAPTURE Object;
    IMFAttributes* Attributes = NULL;
    IMFMediaType* Type = NULL;
    HRESULT Result;

    Object = Mem_Alloc(sizeof(*Object));
    if (Object == NULL) return E_OUTOFMEMORY;
    RtlZeroMemory(Object, sizeof(*Object));
    Result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    Object->Started = SUCCEEDED(Result);
    if (SUCCEEDED(Result)) Result = ZpVideoCapture_Activate(Request, &Object->Source);
    if (SUCCEEDED(Result)) Result = MFCreateAttributes(&Attributes, 1);
    if (SUCCEEDED(Result))
    {
        Result = IMFAttributes_SetUINT32(Attributes, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }
    if (SUCCEEDED(Result)) Result = MFCreateSourceReaderFromMediaSource(Object->Source, Attributes, &Object->Reader);
    if (SUCCEEDED(Result)) Result = ZpVideoCapture_SelectReaderFormat(Object->Reader, Request);
    if (SUCCEEDED(Result)) Result = MFCreateMediaType(&Type);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetGUID(Type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetGUID(Type, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT64(Type,
                                                           &MF_MT_FRAME_SIZE,
                                                           ((ULONGLONG)Request->Width << 32) |
                                                               Request->Height);
    if (SUCCEEDED(Result)) Result = IMFMediaType_SetUINT64(Type,
                                                           &MF_MT_FRAME_RATE,
                                                           ((ULONGLONG)Request->FrameRateNumerator << 32) |
                                                               Request->FrameRateDenominator);
    if (SUCCEEDED(Result)) Result = IMFSourceReader_SetCurrentMediaType(Object->Reader,
                                                                        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                                        NULL,
                                                                        Type);
    if (SUCCEEDED(Result))
    {
        IMFMediaType_Release(Type);
        Type = NULL;
        Result = IMFSourceReader_GetCurrentMediaType(Object->Reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &Type);
    }
    if (SUCCEEDED(Result))
    {
        ULONGLONG Size;

        Result = IMFMediaType_GetUINT64(Type, &MF_MT_FRAME_SIZE, &Size);
        if (SUCCEEDED(Result))
        {
            Object->Width = (ULONG)(Size >> 32);
            Object->Height = (ULONG)Size;
            if (Object->Width != Request->Width || Object->Height != Request->Height)
            {
                Result = MF_E_INVALIDMEDIATYPE;
            }
        }
    }
    if (SUCCEEDED(Result))
    {
        Result = CoCreateInstance(&CLSID_WICImagingFactory,
                                  NULL,
                                  CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory,
                                  (PVOID*)&Object->ImagingFactory);
    }
    if (Type != NULL) IMFMediaType_Release(Type);
    if (Attributes != NULL) IMFAttributes_Release(Attributes);
    if (FAILED(Result))
    {
        ZpVideoCapture_Close(Object);
        return Result;
    }
    *Capture = Object;
    return S_OK;
}

HRESULT
ZpVideoCapture_NextSample(
    _Inout_ PZP_VIDEO_CAPTURE Capture,
    _Outptr_ IMFSample** Sample,
    _Out_ PLONGLONG Timestamp)
{
    DWORD Flags;
    HRESULT Result;

    do
    {
        Result = IMFSourceReader_ReadSample(Capture->Reader,
                                            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                            0,
                                            NULL,
                                            &Flags,
                                            Timestamp,
                                            Sample);
        if (SUCCEEDED(Result) && Flags & MF_SOURCE_READERF_ENDOFSTREAM) Result = MF_E_END_OF_STREAM;
    } while (SUCCEEDED(Result) && *Sample == NULL);
    return Result;
}

static
HRESULT
ZpVideoCapture_Encode(
    _In_ PZP_VIDEO_CAPTURE Capture,
    _In_reads_bytes_(Length) PBYTE Bytes,
    _In_ ULONG Length,
    _In_ USHORT Quality,
    _Out_ PZP_VIDEO_IMAGE Image)
{
    IWICBitmap* Bitmap = NULL;
    IWICFormatConverter* Converter = NULL;
    IWICBitmapEncoder* Encoder = NULL;
    IWICBitmapFrameEncode* Frame = NULL;
    IPropertyBag2* Properties = NULL;
    IStream* Stream = NULL;
    IWICBitmapSource* Source;
    GUID PixelFormat = GUID_WICPixelFormat24bppBGR;
    PROPBAG2 Property = { 0 };
    VARIANT Value;
    STATSTG StreamInfo;
    HGLOBAL Global = NULL;
    PVOID Data;
    PBYTE Output;
    ULONG Stride;
    HRESULT Result;

    if ((ULONGLONG)Capture->Width * 4 > MAXULONG) return E_OUTOFMEMORY;
    Stride = Capture->Width * 4;
    if ((ULONGLONG)Stride * Capture->Height > Length) return MF_E_BUFFERTOOSMALL;
    Result = IWICImagingFactory_CreateBitmapFromMemory(Capture->ImagingFactory,
                                                        Capture->Width,
                                                        Capture->Height,
                                                        &GUID_WICPixelFormat32bppBGR,
                                                        Stride,
                                                        Length,
                                                        Bytes,
                                                        &Bitmap);
    Source = (IWICBitmapSource*)Bitmap;
    if (SUCCEEDED(Result)) Result = IWICImagingFactory_CreateFormatConverter(Capture->ImagingFactory, &Converter);
    if (SUCCEEDED(Result)) Result = IWICFormatConverter_Initialize(Converter, Source, &PixelFormat,
                                                                   WICBitmapDitherTypeNone, NULL, 0,
                                                                   WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(Result)) Result = CreateStreamOnHGlobal(NULL, TRUE, &Stream);
    if (SUCCEEDED(Result)) Result = IWICImagingFactory_CreateEncoder(Capture->ImagingFactory,
                                                                     &GUID_ContainerFormatJpeg,
                                                                     NULL,
                                                                     &Encoder);
    if (SUCCEEDED(Result)) Result = IWICBitmapEncoder_Initialize(Encoder, Stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(Result)) Result = IWICBitmapEncoder_CreateNewFrame(Encoder, &Frame, &Properties);
    if (SUCCEEDED(Result))
    {
        Property.pstrName = L"ImageQuality";
        VariantInit(&Value);
        Value.vt = VT_R4;
        Value.fltVal = Quality / 100.0f;
        Result = IPropertyBag2_Write(Properties, 1, &Property, &Value);
    }
    if (SUCCEEDED(Result)) Result = IWICBitmapFrameEncode_Initialize(Frame, Properties);
    if (SUCCEEDED(Result)) Result = IWICBitmapFrameEncode_SetSize(Frame, Capture->Width, Capture->Height);
    if (SUCCEEDED(Result)) Result = IWICBitmapFrameEncode_SetPixelFormat(Frame, &PixelFormat);
    if (SUCCEEDED(Result)) Result = IWICBitmapFrameEncode_WriteSource(Frame, (IWICBitmapSource*)Converter, NULL);
    if (SUCCEEDED(Result)) Result = IWICBitmapFrameEncode_Commit(Frame);
    if (SUCCEEDED(Result)) Result = IWICBitmapEncoder_Commit(Encoder);
    if (SUCCEEDED(Result)) Result = IStream_Stat(Stream, &StreamInfo, STATFLAG_NONAME);
    if (SUCCEEDED(Result) && (StreamInfo.cbSize.HighPart != 0 || StreamInfo.cbSize.LowPart == 0 ||
        StreamInfo.cbSize.LowPart > ZP_VIDEO_MAX_FRAME_SIZE)) Result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    if (SUCCEEDED(Result)) Result = GetHGlobalFromStream(Stream, &Global);
    Data = SUCCEEDED(Result) ? GlobalLock(Global) : NULL;
    if (SUCCEEDED(Result) && Data == NULL) Result = HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
    Output = SUCCEEDED(Result) ? Mem_Alloc(StreamInfo.cbSize.LowPart) : NULL;
    if (SUCCEEDED(Result) && Output == NULL) Result = E_OUTOFMEMORY;
    if (SUCCEEDED(Result))
    {
        RtlCopyMemory(Output, Data, StreamInfo.cbSize.LowPart);
        Image->Frame.Width = Capture->Width;
        Image->Frame.Height = Capture->Height;
        Image->Frame.DataLength = StreamInfo.cbSize.LowPart;
        Image->Data = Output;
    }
    if (Data != NULL) GlobalUnlock(Global);
    if (Properties != NULL) IPropertyBag2_Release(Properties);
    if (Frame != NULL) IWICBitmapFrameEncode_Release(Frame);
    if (Encoder != NULL) IWICBitmapEncoder_Release(Encoder);
    if (Stream != NULL) IStream_Release(Stream);
    if (Converter != NULL) IWICFormatConverter_Release(Converter);
    if (Bitmap != NULL) IWICBitmap_Release(Bitmap);
    return Result;
}

HRESULT
ZpVideoCapture_EncodeSample(
    _In_ PZP_VIDEO_CAPTURE Capture,
    _In_ IMFSample* Sample,
    _In_ USHORT Quality,
    _Out_ PZP_VIDEO_IMAGE Image)
{
    IMFMediaBuffer* Buffer = NULL;
    PBYTE Bytes = NULL;
    DWORD Length;
    HRESULT Result;

    Result = IMFSample_ConvertToContiguousBuffer(Sample, &Buffer);
    if (SUCCEEDED(Result)) Result = IMFMediaBuffer_Lock(Buffer, &Bytes, NULL, &Length);
    if (SUCCEEDED(Result)) Result = ZpVideoCapture_Encode(Capture,
                                                          Bytes,
                                                          Length,
                                                          Quality,
                                                          Image);
    if (Bytes != NULL) IMFMediaBuffer_Unlock(Buffer);
    if (Buffer != NULL) IMFMediaBuffer_Release(Buffer);
    return Result;
}

VOID
ZpVideoCapture_FreeImage(
    _Inout_ PZP_VIDEO_IMAGE Image)
{
    Mem_Free(Image->Data);
}

VOID
ZpVideoCapture_Close(
    _In_opt_ PZP_VIDEO_CAPTURE Capture)
{
    if (Capture == NULL) return;
    if (Capture->Reader != NULL) IMFSourceReader_Release(Capture->Reader);
    if (Capture->Source != NULL)
    {
        IMFMediaSource_Shutdown(Capture->Source);
        IMFMediaSource_Release(Capture->Source);
    }
    if (Capture->ImagingFactory != NULL) IWICImagingFactory_Release(Capture->ImagingFactory);
    if (Capture->Started) MFShutdown();
    Mem_Free(Capture);
}
