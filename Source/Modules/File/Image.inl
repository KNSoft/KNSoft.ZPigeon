#include <Wincodec.h>

#pragma comment(lib, "WindowsCodecs.lib")

#define ZP_FILE_IMAGE_MAX_SOURCE_PIXELS 0x04000000ULL

static
NTSTATUS
ZpFile_PreviewImage(
    _In_ PCZP_STRING_VIEW Path,
    _In_ ZP_FILE_IMAGE_PREVIEW_QUALITY Quality,
    _In_ volatile LONG* Pending,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    IWICImagingFactory* Factory = NULL;
    IWICBitmapDecoder* Decoder = NULL;
    IWICBitmapFrameDecode* Source = NULL;
    IWICBitmapScaler* Scaler = NULL;
    IWICFormatConverter* Converter = NULL;
    IWICBitmapEncoder* Encoder = NULL;
    IWICBitmapFrameEncode* Frame = NULL;
    IPropertyBag2* Properties = NULL;
    IStream* Stream = NULL;
    IWICBitmapSource* Output;
    PUNICODE_STRING PathString = NULL;
    STATSTG StreamInfo;
    HGLOBAL Global;
    PVOID Bytes;
    PROPBAG2 Property = { 0 };
    VARIANT Value;
    GUID PixelFormat = GUID_WICPixelFormat24bppBGR;
    UINT Width, Height, OutputWidth, OutputHeight, MaximumDimension;
    FLOAT CompressionQuality;
    HRESULT Result;
    BOOLEAN Uninitialize = FALSE;

    switch (Quality)
    {
    case ZpFileImagePreviewLow:
        MaximumDimension = 800;
        CompressionQuality = 0.70f;
        break;
    case ZpFileImagePreviewMedium:
        MaximumDimension = 1600;
        CompressionQuality = 0.82f;
        break;
    case ZpFileImagePreviewHigh:
        MaximumDimension = 2560;
        CompressionQuality = 0.90f;
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }
    if (!InterlockedCompareExchange(Pending, TRUE, TRUE)) return STATUS_CANCELLED;
    PathString = ZpFile_CopyPath(Path);
    if (PathString == NULL) return STATUS_NO_MEMORY;
    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (Result == RPC_E_CHANGED_MODE)
    {
        Result = S_OK;
    }
    else if (SUCCEEDED(Result))
    {
        Uninitialize = TRUE;
    }
    if (FAILED(Result)) goto Cleanup;
    Result = CoCreateInstance(&CLSID_WICImagingFactory2,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_IWICImagingFactory,
                              (PVOID*)&Factory);
    if (FAILED(Result)) goto Cleanup;
    Result = Factory->lpVtbl->CreateDecoderFromFilename(Factory,
                                                        PathString->Buffer,
                                                        NULL,
                                                        GENERIC_READ,
                                                        WICDecodeMetadataCacheOnDemand,
                                                        &Decoder);
    if (FAILED(Result)) goto Cleanup;
    Result = Decoder->lpVtbl->GetFrame(Decoder, 0, &Source);
    if (FAILED(Result)) goto Cleanup;
    Result = Source->lpVtbl->GetSize(Source, &Width, &Height);
    if (FAILED(Result)) goto Cleanup;
    if (Width == 0 || Height == 0 || (ULONGLONG)Width * Height > ZP_FILE_IMAGE_MAX_SOURCE_PIXELS)
    {
        Result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        goto Cleanup;
    }
    Output = (IWICBitmapSource*)Source;
    OutputWidth = Width;
    OutputHeight = Height;
    if (max(Width, Height) > MaximumDimension)
    {
        if (Width >= Height)
        {
            OutputWidth = MaximumDimension;
            OutputHeight = (UINT)max(1ULL, (ULONGLONG)Height * MaximumDimension / Width);
        }
        else
        {
            OutputHeight = MaximumDimension;
            OutputWidth = (UINT)max(1ULL, (ULONGLONG)Width * MaximumDimension / Height);
        }
        Result = Factory->lpVtbl->CreateBitmapScaler(Factory, &Scaler);
        if (FAILED(Result)) goto Cleanup;
        Result = Scaler->lpVtbl->Initialize(Scaler,
                                            Output,
                                            OutputWidth,
                                            OutputHeight,
                                            WICBitmapInterpolationModeFant);
        if (FAILED(Result)) goto Cleanup;
        Output = (IWICBitmapSource*)Scaler;
    }
    Result = Factory->lpVtbl->CreateFormatConverter(Factory, &Converter);
    if (FAILED(Result)) goto Cleanup;
    Result = Converter->lpVtbl->Initialize(Converter,
                                           Output,
                                           &PixelFormat,
                                           WICBitmapDitherTypeNone,
                                           NULL,
                                           0,
                                           WICBitmapPaletteTypeCustom);
    if (FAILED(Result)) goto Cleanup;
    Result = CreateStreamOnHGlobal(NULL, TRUE, &Stream);
    if (FAILED(Result)) goto Cleanup;
    Result = Factory->lpVtbl->CreateEncoder(Factory, &GUID_ContainerFormatJpeg, NULL, &Encoder);
    if (FAILED(Result)) goto Cleanup;
    Result = Encoder->lpVtbl->Initialize(Encoder, Stream, WICBitmapEncoderNoCache);
    if (FAILED(Result)) goto Cleanup;
    Result = Encoder->lpVtbl->CreateNewFrame(Encoder, &Frame, &Properties);
    if (FAILED(Result)) goto Cleanup;
    Property.pstrName = L"ImageQuality";
    VariantInit(&Value);
    Value.vt = VT_R4;
    Value.fltVal = CompressionQuality;
    Result = Properties->lpVtbl->Write(Properties, 1, &Property, &Value);
    if (FAILED(Result)) goto Cleanup;
    Result = Frame->lpVtbl->Initialize(Frame, Properties);
    if (FAILED(Result)) goto Cleanup;
    Result = Frame->lpVtbl->SetSize(Frame, OutputWidth, OutputHeight);
    if (FAILED(Result)) goto Cleanup;
    Result = Frame->lpVtbl->SetPixelFormat(Frame, &PixelFormat);
    if (FAILED(Result)) goto Cleanup;
    if (!InterlockedCompareExchange(Pending, TRUE, TRUE))
    {
        Result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        goto Cleanup;
    }
    Result = Frame->lpVtbl->WriteSource(Frame, (IWICBitmapSource*)Converter, NULL);
    if (FAILED(Result)) goto Cleanup;
    Result = Frame->lpVtbl->Commit(Frame);
    if (FAILED(Result)) goto Cleanup;
    Result = Encoder->lpVtbl->Commit(Encoder);
    if (FAILED(Result)) goto Cleanup;
    Result = Stream->lpVtbl->Stat(Stream, &StreamInfo, STATFLAG_NONAME);
    if (FAILED(Result)) goto Cleanup;
    if (StreamInfo.cbSize.HighPart != 0 || StreamInfo.cbSize.LowPart == 0 ||
        StreamInfo.cbSize.LowPart > ZP_FILE_IMAGE_PREVIEW_MAX_SIZE)
    {
        Result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        goto Cleanup;
    }
    Result = GetHGlobalFromStream(Stream, &Global);
    if (FAILED(Result)) goto Cleanup;
    Bytes = GlobalLock(Global);
    if (Bytes == NULL)
    {
        ULONG Error = GetLastError();

        Result = HRESULT_FROM_WIN32(Error != ERROR_SUCCESS ? Error : ERROR_NOT_ENOUGH_MEMORY);
        goto Cleanup;
    }
    *Response = Mem_Alloc(StreamInfo.cbSize.LowPart);
    if (*Response != NULL) RtlCopyMemory(*Response, Bytes, StreamInfo.cbSize.LowPart);
    GlobalUnlock(Global);
    if (*Response == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    *ResponseLength = StreamInfo.cbSize.LowPart;
Cleanup:
    if (Properties != NULL) Properties->lpVtbl->Release(Properties);
    if (Frame != NULL) Frame->lpVtbl->Release(Frame);
    if (Encoder != NULL) Encoder->lpVtbl->Release(Encoder);
    if (Stream != NULL) Stream->lpVtbl->Release(Stream);
    if (Converter != NULL) Converter->lpVtbl->Release(Converter);
    if (Scaler != NULL) Scaler->lpVtbl->Release(Scaler);
    if (Source != NULL) Source->lpVtbl->Release(Source);
    if (Decoder != NULL) Decoder->lpVtbl->Release(Decoder);
    if (Factory != NULL) Factory->lpVtbl->Release(Factory);
    if (Uninitialize) CoUninitialize();
    NT_FreeStringW(PathString);
    if (FAILED(Result))
    {
        ULONG Error = HRESULT_CODE(Result);
        NTSTATUS Status;

        if (Error == ERROR_SUCCESS) return STATUS_UNSUCCESSFUL;
        Status = NTSTATUS_FROM_WIN32(Error);
        _Analysis_assume_(!NT_SUCCESS(Status));
        return Status;
    }
    return STATUS_SUCCESS;
}
