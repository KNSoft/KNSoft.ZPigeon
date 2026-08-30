#include "Capture.h"

#include <d3d11.h>
#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <objbase.h>
#include <roapi.h>
#include <wincodec.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#pragma comment(lib, "D3d11.lib")
#pragma comment(lib, "Dxguid.lib")
#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "RuntimeObject.lib")
#pragma comment(lib, "WindowsCodecs.lib")

#define ZP_WINDOW_CAPTURE_MIN_BUILD 26100
#define ZP_WINDOW_CAPTURE_BUFFER_COUNT 2
#define ZP_WINDOW_CAPTURE_DIRTY_THRESHOLD 40

typedef __x_ABI_CWindows_CGraphics_CCapture_CIDirect3D11CaptureFrame WGC_FRAME;
typedef __x_ABI_CWindows_CGraphics_CCapture_CIDirect3D11CaptureFrame2 WGC_FRAME2;
typedef __x_ABI_CWindows_CGraphics_CCapture_CIDirect3D11CaptureFramePool WGC_FRAME_POOL;
typedef __x_ABI_CWindows_CGraphics_CCapture_CIDirect3D11CaptureFramePoolStatics2 WGC_FRAME_POOL_STATICS;
typedef __x_ABI_CWindows_CGraphics_CCapture_CIGraphicsCaptureItem WGC_ITEM;
typedef __x_ABI_CWindows_CGraphics_CCapture_CIGraphicsCaptureSession WGC_SESSION;
typedef __x_ABI_CWindows_CGraphics_CCapture_CIGraphicsCaptureSession2 WGC_SESSION2;
typedef __x_ABI_CWindows_CGraphics_CCapture_CIGraphicsCaptureSession4 WGC_SESSION4;
typedef __x_ABI_CWindows_CGraphics_CCapture_CIGraphicsCaptureSession5 WGC_SESSION5;
typedef __x_ABI_CWindows_CGraphics_CCapture_CIGraphicsCaptureSession6 WGC_SESSION6;
typedef __x_ABI_CWindows_CGraphics_CDirectX_CDirect3D11_CIDirect3DDevice WGC_DEVICE;
typedef __x_ABI_CWindows_CGraphics_CDirectX_CDirect3D11_CIDirect3DSurface WGC_SURFACE;
typedef __FIVectorView_1_Windows__CGraphics__CRectInt32 WGC_RECT_VECTOR;

typedef struct _WGC_ITEM_INTEROP WGC_ITEM_INTEROP;
typedef struct _WGC_DXGI_ACCESS WGC_DXGI_ACCESS;
typedef struct _WGC_FRAME_HANDLER WGC_FRAME_HANDLER;

typedef struct _WGC_ITEM_INTEROP_VTBL
{
    BEGIN_INTERFACE

    HRESULT (STDMETHODCALLTYPE* QueryInterface)(WGC_ITEM_INTEROP* This, REFIID Riid, PVOID* Object);
    ULONG (STDMETHODCALLTYPE* AddRef)(WGC_ITEM_INTEROP* This);
    ULONG (STDMETHODCALLTYPE* Release)(WGC_ITEM_INTEROP* This);
    HRESULT (STDMETHODCALLTYPE* CreateForWindow)(WGC_ITEM_INTEROP* This, HWND Window, REFIID Riid, PVOID* Object);
    HRESULT (STDMETHODCALLTYPE* CreateForMonitor)(
        WGC_ITEM_INTEROP* This,
        HMONITOR Monitor,
        REFIID Riid,
        PVOID* Object);

    END_INTERFACE
} WGC_ITEM_INTEROP_VTBL;

struct _WGC_ITEM_INTEROP
{
    CONST_VTBL WGC_ITEM_INTEROP_VTBL* lpVtbl;
};

typedef struct _WGC_DXGI_ACCESS_VTBL
{
    BEGIN_INTERFACE

    HRESULT (STDMETHODCALLTYPE* QueryInterface)(WGC_DXGI_ACCESS* This, REFIID Riid, PVOID* Object);
    ULONG (STDMETHODCALLTYPE* AddRef)(WGC_DXGI_ACCESS* This);
    ULONG (STDMETHODCALLTYPE* Release)(WGC_DXGI_ACCESS* This);
    HRESULT (STDMETHODCALLTYPE* GetInterface)(WGC_DXGI_ACCESS* This, REFIID Riid, PVOID* Object);

    END_INTERFACE
} WGC_DXGI_ACCESS_VTBL;

struct _WGC_DXGI_ACCESS
{
    CONST_VTBL WGC_DXGI_ACCESS_VTBL* lpVtbl;
};

struct _WGC_FRAME_HANDLER
{
    __FITypedEventHandler_2_Windows__CGraphics__CCapture__CDirect3D11CaptureFramePool_IInspectable Interface;
    LONG ReferenceCount;
    HANDLE Event;
};

struct _ZP_WINDOW_CAPTURE
{
    ZP_WINDOW_CAPTURE_OPTIONS Options;
    HWND Window;
    ID3D11Device* Device;
    ID3D11DeviceContext* Context;
    ID3D11VideoDevice* VideoDevice;
    ID3D11VideoContext* VideoContext;
    ID3D11VideoProcessorEnumerator* VideoEnumerator;
    ID3D11VideoProcessor* VideoProcessor;
    IMFDXGIDeviceManager* DeviceManager;
    IMFVideoSampleAllocatorEx* SampleAllocator;
    UINT DeviceManagerToken;
    ID3D11Texture2D* Staging;
    IWICImagingFactory* ImagingFactory;
    WGC_ITEM* Item;
    WGC_FRAME_POOL* FramePool;
    WGC_SESSION* Session;
    WGC_SESSION5* RateSession;
    WGC_DEVICE* WgcDevice;
    WGC_FRAME_HANDLER* FrameHandler;
    EventRegistrationToken FrameToken;
    HDC GdiMemory;
    HBITMAP GdiBitmap;
    HGDIOBJ GdiPreviousBitmap;
    PVOID GdiBits;
    ULONGLONG GdiFrameTime;
    struct __x_ABI_CWindows_CGraphics_CSizeInt32 Size;
    ULONG VideoWidth;
    ULONG VideoHeight;
    ULONG Sequence;
    BOOLEAN KeyFrame;
    BOOLEAN Gdi;
    BOOLEAN WgcFrameReceived;
    BOOLEAN FrameHandlerRegistered;
    BOOLEAN RoInitialized;
};

struct _ZP_WINDOW_CAPTURE_FRAME
{
    LONG ReferenceCount;
    ID3D11Texture2D* Texture;
    struct __x_ABI_CWindows_CGraphics_CSizeInt32 Size;
    struct __x_ABI_CWindows_CFoundation_CTimeSpan Time;
    WICRect DirtyRect;
    HRESULT DirtyResult;
};

static CONST IID WgcCaptureItemIid = { 0x79c3f95b, 0x31f7, 0x4ec2,
                                       { 0xa4, 0x64, 0x63, 0x2e, 0xf5, 0xd3, 0x07, 0x60 } };
static CONST IID WgcCaptureItemInteropIid = { 0x3628e81b, 0x3cac, 0x4c60,
                                              { 0xb7, 0xf4, 0x23, 0xce, 0x0e, 0x0c, 0x33, 0x56 } };
static CONST IID WgcFramePoolStaticsIid = { 0x589b103f, 0x6bbc, 0x5df5,
                                            { 0xa9, 0x91, 0x02, 0xe2, 0x8b, 0x3b, 0x66, 0xd5 } };
static CONST IID WgcDeviceIid = { 0xa37624ab, 0x8d5f, 0x4650,
                                  { 0x9d, 0x3e, 0x9e, 0xae, 0x3d, 0x9b, 0xc6, 0x70 } };
static CONST IID WgcDxgiAccessIid = { 0xa9b3d012, 0x3df2, 0x4ee3,
                                      { 0xb8, 0xd1, 0x86, 0x95, 0xf4, 0x57, 0xd3, 0xc1 } };
static CONST IID WgcDxgiDeviceIid = { 0x54ec77fa, 0x1377, 0x44e6,
                                      { 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c } };
static CONST IID WgcTextureIid = { 0x6f15aaf2, 0xd208, 0x4e89,
                                   { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };
static CONST IID WgcFrame2Iid = { 0x37869cfa, 0x2b48, 0x5ebf,
                                  { 0x9a, 0xfb, 0xdf, 0xfd, 0x80, 0x5d, 0xef, 0xdb } };
static CONST IID WgcSession2Iid = { 0x2c39ae40, 0x7d2e, 0x5044,
                                    { 0x80, 0x4e, 0x8b, 0x67, 0x99, 0xd4, 0xcf, 0x9e } };
static CONST IID WgcSession4Iid = { 0xae99813c, 0xc257, 0x5759,
                                    { 0x8e, 0xd0, 0x66, 0x8c, 0x9b, 0x55, 0x7e, 0xd4 } };
static CONST IID WgcSession5Iid = { 0x67c0ea62, 0x1f85, 0x5061,
                                    { 0x92, 0x5a, 0x23, 0x9b, 0xe0, 0xac, 0x09, 0xcb } };
static CONST IID WgcSession6Iid = { 0xd7419236, 0xbe20, 0x5e9f,
                                    { 0xbc, 0xd6, 0xc4, 0xe9, 0x8f, 0xd6, 0xaf, 0xdc } };
static CONST IID WgcFrameHandlerIid = { 0x51a947f7, 0x79cf, 0x5a3e,
                                        { 0xa3, 0xa5, 0x12, 0x89, 0xcf, 0xa6, 0xdf, 0xe8 } };
static CONST IID WgcAgileObjectIid = { 0x94ea2b94, 0xe9cc, 0x49e0,
                                       { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };

static
VOID
WgcRelease(
    _In_opt_ IUnknown* Object)
{
    if (Object != NULL) Object->lpVtbl->Release(Object);
}

static
HRESULT
STDMETHODCALLTYPE
ZpWindowCapture_FrameHandlerQueryInterface(
    _In_ __FITypedEventHandler_2_Windows__CGraphics__CCapture__CDirect3D11CaptureFramePool_IInspectable* This,
    _In_ REFIID Riid,
    _Outptr_ PVOID* Object)
{
    if (!IsEqualIID(Riid, &IID_IUnknown) && !IsEqualIID(Riid, &WgcFrameHandlerIid) &&
        !IsEqualIID(Riid, &WgcAgileObjectIid))
    {
        *Object = NULL;
        return E_NOINTERFACE;
    }
    *Object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static
ULONG
STDMETHODCALLTYPE
ZpWindowCapture_FrameHandlerAddRef(
    _In_ __FITypedEventHandler_2_Windows__CGraphics__CCapture__CDirect3D11CaptureFramePool_IInspectable* This)
{
    return InterlockedIncrement(&CONTAINING_RECORD(This, WGC_FRAME_HANDLER, Interface)->ReferenceCount);
}

static
ULONG
STDMETHODCALLTYPE
ZpWindowCapture_FrameHandlerRelease(
    _In_ __FITypedEventHandler_2_Windows__CGraphics__CCapture__CDirect3D11CaptureFramePool_IInspectable* This)
{
    WGC_FRAME_HANDLER* Handler = CONTAINING_RECORD(This, WGC_FRAME_HANDLER, Interface);
    ULONG ReferenceCount = InterlockedDecrement(&Handler->ReferenceCount);

    if (ReferenceCount == 0)
    {
        NtClose(Handler->Event);
        Mem_Free(Handler);
    }
    return ReferenceCount;
}

static
HRESULT
STDMETHODCALLTYPE
ZpWindowCapture_FrameArrived(
    _In_ __FITypedEventHandler_2_Windows__CGraphics__CCapture__CDirect3D11CaptureFramePool_IInspectable* This,
    _In_ WGC_FRAME_POOL* Sender,
    _In_opt_ IInspectable* Arguments)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Sender);
    UNREFERENCED_PARAMETER(Arguments);
    Status = NtSetEvent(CONTAINING_RECORD(This, WGC_FRAME_HANDLER, Interface)->Event, NULL);
    return NT_SUCCESS(Status) ? S_OK : HRESULT_FROM_NT(Status);
}

static
__FITypedEventHandler_2_Windows__CGraphics__CCapture__CDirect3D11CaptureFramePool_IInspectableVtbl
WgcFrameHandlerVtbl =
{
    ZpWindowCapture_FrameHandlerQueryInterface,
    ZpWindowCapture_FrameHandlerAddRef,
    ZpWindowCapture_FrameHandlerRelease,
    ZpWindowCapture_FrameArrived
};

static
HRESULT
ZpWindowCapture_CreateFrameHandler(
    _Out_ WGC_FRAME_HANDLER** FrameHandler)
{
    WGC_FRAME_HANDLER* Handler;
    NTSTATUS Status;

    Handler = Mem_Alloc(sizeof(*Handler));
    if (Handler == NULL) return E_OUTOFMEMORY;
    Handler->Interface.lpVtbl = &WgcFrameHandlerVtbl;
    Handler->ReferenceCount = 1;
    Status = NtCreateEvent(&Handler->Event,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           NULL,
                           SynchronizationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Handler);
        return HRESULT_FROM_NT(Status);
    }
    *FrameHandler = Handler;
    return S_OK;
}

static
HRESULT
ZpWindowCapture_CreateFactory(
    _In_ PCWSTR RuntimeClass,
    _In_ REFIID Iid,
    _Out_ PVOID* Factory)
{
    HSTRING ClassName;
    HRESULT Result;

    Result = WindowsCreateString(RuntimeClass, (UINT32)wcslen(RuntimeClass), &ClassName);
    if (FAILED(Result)) return Result;
    Result = RoGetActivationFactory(ClassName, Iid, Factory);
    WindowsDeleteString(ClassName);
    return Result;
}

static
VOID
ZpWindowCapture_GetOutputSize(
    _In_ PZP_WINDOW_CAPTURE Capture,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    ULONG SourceWidth = Capture->Size.Width;
    ULONG SourceHeight = Capture->Size.Height;
    ULONG Maximum = max(SourceWidth, SourceHeight);

    if (Maximum <= Capture->Options.MaxDimension)
    {
        *Width = SourceWidth;
        *Height = SourceHeight;
    }
    else
    {
        *Width = max(1UL, (ULONG)((ULONGLONG)SourceWidth * Capture->Options.MaxDimension / Maximum));
        *Height = max(1UL, (ULONG)((ULONGLONG)SourceHeight * Capture->Options.MaxDimension / Maximum));
    }
}

static
VOID
ZpWindowCapture_ResetVideoProcessor(
    _Inout_ PZP_WINDOW_CAPTURE Capture)
{
    WgcRelease((IUnknown*)Capture->VideoProcessor);
    WgcRelease((IUnknown*)Capture->VideoEnumerator);
    Capture->VideoProcessor = NULL;
    Capture->VideoEnumerator = NULL;
}

static
VOID
ZpWindowCapture_InitializeVideo(
    _Inout_ PZP_WINDOW_CAPTURE Capture)
{
    HRESULT Result;

    Result = Capture->Device->lpVtbl->QueryInterface(Capture->Device,
                                                      &IID_ID3D11VideoDevice,
                                                      (PVOID*)&Capture->VideoDevice);
    if (SUCCEEDED(Result))
    {
        Result = Capture->Context->lpVtbl->QueryInterface(Capture->Context,
                                                           &IID_ID3D11VideoContext,
                                                           (PVOID*)&Capture->VideoContext);
    }
    if (SUCCEEDED(Result))
    {
        Result = MFCreateDXGIDeviceManager(&Capture->DeviceManagerToken, &Capture->DeviceManager);
    }
    if (SUCCEEDED(Result))
    {
        Result = Capture->DeviceManager->lpVtbl->ResetDevice(Capture->DeviceManager,
                                                              (IUnknown*)Capture->Device,
                                                              Capture->DeviceManagerToken);
    }
    if (FAILED(Result))
    {
        WgcRelease((IUnknown*)Capture->DeviceManager);
        WgcRelease((IUnknown*)Capture->VideoContext);
        WgcRelease((IUnknown*)Capture->VideoDevice);
        Capture->DeviceManager = NULL;
        Capture->VideoContext = NULL;
        Capture->VideoDevice = NULL;
    }
}

static
HRESULT
ZpWindowCapture_CreateVideoProcessor(
    _Inout_ PZP_WINDOW_CAPTURE Capture)
{
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC Description = { 0 };
    UINT Flags;
    HRESULT Result;

    Description.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    Description.InputFrameRate.Numerator = Capture->Options.FrameRate;
    Description.InputFrameRate.Denominator = 1;
    Description.InputWidth = Capture->Size.Width;
    Description.InputHeight = Capture->Size.Height;
    Description.OutputFrameRate = Description.InputFrameRate;
    Description.OutputWidth = Capture->VideoWidth;
    Description.OutputHeight = Capture->VideoHeight;
    Description.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    Result = Capture->VideoDevice->lpVtbl->CreateVideoProcessorEnumerator(
        Capture->VideoDevice,
        &Description,
        &Capture->VideoEnumerator);
    if (SUCCEEDED(Result))
    {
        Result = Capture->VideoEnumerator->lpVtbl->CheckVideoProcessorFormat(
            Capture->VideoEnumerator,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            &Flags);
    }
    if (SUCCEEDED(Result) && !FlagOn(Flags, D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT))
    {
        Result = MF_E_INVALIDMEDIATYPE;
    }
    if (SUCCEEDED(Result))
    {
        Result = Capture->VideoEnumerator->lpVtbl->CheckVideoProcessorFormat(
            Capture->VideoEnumerator,
            DXGI_FORMAT_NV12,
            &Flags);
    }
    if (SUCCEEDED(Result) && !FlagOn(Flags, D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT))
    {
        Result = MF_E_INVALIDMEDIATYPE;
    }
    if (SUCCEEDED(Result))
    {
        Result = Capture->VideoDevice->lpVtbl->CreateVideoProcessor(
            Capture->VideoDevice,
            Capture->VideoEnumerator,
            0,
            &Capture->VideoProcessor);
    }
    if (FAILED(Result)) ZpWindowCapture_ResetVideoProcessor(Capture);
    return Result;
}

static
HRESULT
ZpWindowCapture_CreateSampleAllocator(
    _Inout_ PZP_WINDOW_CAPTURE Capture)
{
    IMFAttributes* Attributes = NULL;
    IMFMediaType* Type = NULL;
    HRESULT Result;

    Result = MFCreateAttributes(&Attributes, 2);
    if (SUCCEEDED(Result))
    {
        Result = Attributes->lpVtbl->SetUINT32(
            Attributes,
            &MF_SA_D3D11_BINDFLAGS,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    }
    if (SUCCEEDED(Result))
    {
        Result = Attributes->lpVtbl->SetUINT32(Attributes, &MF_SA_D3D11_USAGE, D3D11_USAGE_DEFAULT);
    }
    if (SUCCEEDED(Result)) Result = MFCreateMediaType(&Type);
    if (SUCCEEDED(Result)) Result = Type->lpVtbl->SetGUID(Type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (SUCCEEDED(Result)) Result = Type->lpVtbl->SetGUID(Type, &MF_MT_SUBTYPE, &MFVideoFormat_NV12);
    if (SUCCEEDED(Result))
    {
        Result = Type->lpVtbl->SetUINT64(
            Type,
            &MF_MT_FRAME_SIZE,
            ((ULONGLONG)Capture->VideoWidth << 32) | Capture->VideoHeight);
    }
    if (SUCCEEDED(Result))
    {
        Result = MFCreateVideoSampleAllocatorEx(
            &IID_IMFVideoSampleAllocatorEx,
            (PVOID*)&Capture->SampleAllocator);
    }
    if (SUCCEEDED(Result))
    {
        Result = Capture->SampleAllocator->lpVtbl->SetDirectXManager(
            Capture->SampleAllocator,
            (IUnknown*)Capture->DeviceManager);
    }
    if (SUCCEEDED(Result))
    {
        Result = Capture->SampleAllocator->lpVtbl->InitializeSampleAllocatorEx(
            Capture->SampleAllocator,
            3,
            8,
            Attributes,
            Type);
    }
    WgcRelease((IUnknown*)Type);
    WgcRelease((IUnknown*)Attributes);
    if (FAILED(Result))
    {
        WgcRelease((IUnknown*)Capture->SampleAllocator);
        Capture->SampleAllocator = NULL;
    }
    return Result;
}

static
HRESULT
ZpWindowCapture_CreateStaging(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ ID3D11Texture2D* Texture)
{
    D3D11_TEXTURE2D_DESC Description;

    WgcRelease((IUnknown*)Capture->Staging);
    Capture->Staging = NULL;
    Texture->lpVtbl->GetDesc(Texture, &Description);
    Description.Usage = D3D11_USAGE_STAGING;
    Description.BindFlags = 0;
    Description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Description.MiscFlags = 0;
    return Capture->Device->lpVtbl->CreateTexture2D(Capture->Device,
                                                     &Description,
                                                     NULL,
                                                     &Capture->Staging);
}

static
HRESULT
ZpWindowCapture_Encode(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ ID3D11Texture2D* Texture,
    _In_ WICRect* Rect,
    _In_ ULONG OutputWidth,
    _In_ ULONG OutputHeight,
    _In_ LOGICAL Png,
    _Outptr_result_bytebuffer_(*DataLength) PBYTE* Data,
    _Out_ PULONG DataLength)
{
    D3D11_TEXTURE2D_DESC Description;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    IWICBitmap* Bitmap = NULL;
    IWICBitmapScaler* Scaler = NULL;
    IWICBitmapClipper* Clipper = NULL;
    IWICFormatConverter* Converter = NULL;
    IWICBitmapEncoder* Encoder = NULL;
    IWICBitmapFrameEncode* Frame = NULL;
    IPropertyBag2* Properties = NULL;
    IStream* Stream = NULL;
    IWICBitmapSource* Source;
    GUID PixelFormat;
    STATSTG StreamInfo;
    HGLOBAL Global;
    PVOID Bytes;
    PROPBAG2 Property = { 0 };
    VARIANT Value;
    ULONG Width, Height, BufferSize;
    HRESULT Result;

    Texture->lpVtbl->GetDesc(Texture, &Description);
    if (Capture->Staging == NULL)
    {
        Result = ZpWindowCapture_CreateStaging(Capture, Texture);
        if (FAILED(Result)) return Result;
    }
    Capture->Context->lpVtbl->CopyResource(Capture->Context,
                                           (ID3D11Resource*)Capture->Staging,
                                           (ID3D11Resource*)Texture);
    Result = Capture->Context->lpVtbl->Map(Capture->Context,
                                           (ID3D11Resource*)Capture->Staging,
                                           0,
                                           D3D11_MAP_READ,
                                           0,
                                           &Mapped);
    if (FAILED(Result)) return Result;
    if ((ULONGLONG)Mapped.RowPitch * Capture->Size.Height > MAXULONG)
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    BufferSize = Mapped.RowPitch * Capture->Size.Height;
    Result = Capture->ImagingFactory->lpVtbl->CreateBitmapFromMemory(
        Capture->ImagingFactory,
        Capture->Size.Width,
        Capture->Size.Height,
        &GUID_WICPixelFormat32bppBGRA,
        Mapped.RowPitch,
        BufferSize,
        Mapped.pData,
        &Bitmap);
    if (FAILED(Result)) goto Cleanup;
    Source = (IWICBitmapSource*)Bitmap;
    if (OutputWidth != Capture->Size.Width || OutputHeight != Capture->Size.Height)
    {
        Result = Capture->ImagingFactory->lpVtbl->CreateBitmapScaler(Capture->ImagingFactory, &Scaler);
        if (FAILED(Result)) goto Cleanup;
        Result = Scaler->lpVtbl->Initialize(Scaler,
                                            Source,
                                            OutputWidth,
                                            OutputHeight,
                                            WICBitmapInterpolationModeFant);
        if (FAILED(Result)) goto Cleanup;
        Source = (IWICBitmapSource*)Scaler;
    }
    if (Rect->X != 0 || Rect->Y != 0 ||
        (ULONG)Rect->Width != OutputWidth || (ULONG)Rect->Height != OutputHeight)
    {
        Result = Capture->ImagingFactory->lpVtbl->CreateBitmapClipper(Capture->ImagingFactory, &Clipper);
        if (FAILED(Result)) goto Cleanup;
        Result = Clipper->lpVtbl->Initialize(Clipper, Source, Rect);
        if (FAILED(Result)) goto Cleanup;
        Source = (IWICBitmapSource*)Clipper;
    }
    Result = Capture->ImagingFactory->lpVtbl->CreateFormatConverter(Capture->ImagingFactory, &Converter);
    if (FAILED(Result)) goto Cleanup;
    PixelFormat = Png ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
    Result = Converter->lpVtbl->Initialize(Converter,
                                           Source,
                                           &PixelFormat,
                                           WICBitmapDitherTypeNone,
                                           NULL,
                                           0,
                                           WICBitmapPaletteTypeCustom);
    if (FAILED(Result)) goto Cleanup;
    Result = CreateStreamOnHGlobal(NULL, TRUE, &Stream);
    if (FAILED(Result)) goto Cleanup;
    Result = Capture->ImagingFactory->lpVtbl->CreateEncoder(
        Capture->ImagingFactory,
        Png ? &GUID_ContainerFormatPng : &GUID_ContainerFormatJpeg,
        NULL,
        &Encoder);
    if (FAILED(Result)) goto Cleanup;
    Result = Encoder->lpVtbl->Initialize(Encoder, Stream, WICBitmapEncoderNoCache);
    if (FAILED(Result)) goto Cleanup;
    Result = Encoder->lpVtbl->CreateNewFrame(Encoder, &Frame, &Properties);
    if (FAILED(Result)) goto Cleanup;
    if (!Png)
    {
        Property.pstrName = L"ImageQuality";
        VariantInit(&Value);
        Value.vt = VT_R4;
        Value.fltVal = Capture->Options.Quality / 100.0f;
        Result = Properties->lpVtbl->Write(Properties, 1, &Property, &Value);
        if (FAILED(Result)) goto Cleanup;
    }
    Result = Frame->lpVtbl->Initialize(Frame, Properties);
    if (FAILED(Result)) goto Cleanup;
    Width = Rect->Width;
    Height = Rect->Height;
    Result = Frame->lpVtbl->SetSize(Frame, Width, Height);
    if (FAILED(Result)) goto Cleanup;
    Result = Frame->lpVtbl->SetPixelFormat(Frame, &PixelFormat);
    if (FAILED(Result)) goto Cleanup;
    Result = Frame->lpVtbl->WriteSource(Frame, (IWICBitmapSource*)Converter, NULL);
    if (FAILED(Result)) goto Cleanup;
    Result = Frame->lpVtbl->Commit(Frame);
    if (FAILED(Result)) goto Cleanup;
    Result = Encoder->lpVtbl->Commit(Encoder);
    if (FAILED(Result)) goto Cleanup;
    Result = Stream->lpVtbl->Stat(Stream, &StreamInfo, STATFLAG_NONAME);
    if (FAILED(Result)) goto Cleanup;
    if (StreamInfo.cbSize.HighPart != 0 || StreamInfo.cbSize.LowPart == 0 ||
        StreamInfo.cbSize.LowPart > ZP_RESPONSE_MAX_PAYLOAD_SIZE)
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
    *Data = Mem_Alloc(StreamInfo.cbSize.LowPart);
    if (*Data != NULL) RtlCopyMemory(*Data, Bytes, StreamInfo.cbSize.LowPart);
    GlobalUnlock(Global);
    if (*Data == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    *DataLength = StreamInfo.cbSize.LowPart;

Cleanup:
    WgcRelease((IUnknown*)Properties);
    WgcRelease((IUnknown*)Frame);
    WgcRelease((IUnknown*)Encoder);
    WgcRelease((IUnknown*)Stream);
    WgcRelease((IUnknown*)Converter);
    WgcRelease((IUnknown*)Clipper);
    WgcRelease((IUnknown*)Scaler);
    WgcRelease((IUnknown*)Bitmap);
    Capture->Context->lpVtbl->Unmap(Capture->Context, (ID3D11Resource*)Capture->Staging, 0);
    return Result;
}

static
HRESULT
ZpWindowCapture_GetDirtyRect(
    _In_ WGC_FRAME* Frame,
    _In_ ULONG SourceWidth,
    _In_ ULONG SourceHeight,
    _Out_ WICRect* Rect)
{
    WGC_FRAME2* Frame2 = NULL;
    WGC_RECT_VECTOR* Regions = NULL;
    struct __x_ABI_CWindows_CGraphics_CRectInt32 Region;
    UINT32 Count, Index;
    LONG Right, Bottom;
    HRESULT Result;

    Result = Frame->lpVtbl->QueryInterface(Frame,
                                            &WgcFrame2Iid,
                                            (PVOID*)&Frame2);
    if (FAILED(Result)) return Result;
    Result = Frame2->lpVtbl->get_DirtyRegions(Frame2, &Regions);
    if (FAILED(Result)) goto Cleanup;
    Result = Regions->lpVtbl->get_Size(Regions, &Count);
    if (FAILED(Result)) goto Cleanup;
    if (Count == 0)
    {
        Result = S_FALSE;
        goto Cleanup;
    }
    Rect->X = MAXLONG;
    Rect->Y = MAXLONG;
    Right = 0;
    Bottom = 0;
    for (Index = 0; Index < Count; Index++)
    {
        Result = Regions->lpVtbl->GetAt(Regions, Index, &Region);
        if (FAILED(Result)) goto Cleanup;
        if (Region.Width <= 0 || Region.Height <= 0 || Region.X < 0 || Region.Y < 0 ||
            (ULONG)Region.X >= SourceWidth || (ULONG)Region.Y >= SourceHeight)
        {
            Result = E_UNEXPECTED;
            goto Cleanup;
        }
        Rect->X = min(Rect->X, Region.X);
        Rect->Y = min(Rect->Y, Region.Y);
        Right = max(Right,
                    (LONG)min((ULONGLONG)SourceWidth,
                              (ULONGLONG)(ULONG)Region.X + (ULONG)Region.Width));
        Bottom = max(Bottom,
                     (LONG)min((ULONGLONG)SourceHeight,
                               (ULONGLONG)(ULONG)Region.Y + (ULONG)Region.Height));
    }
    Rect->Width = Right - Rect->X;
    Rect->Height = Bottom - Rect->Y;

Cleanup:
    WgcRelease((IUnknown*)Regions);
    WgcRelease((IUnknown*)Frame2);
    return Result;
}

typedef struct _ZP_WINDOW_MONITOR_SEARCH
{
    ULONG Target;
    ULONG Current;
    HMONITOR Monitor;
    RECT Rect;
} ZP_WINDOW_MONITOR_SEARCH, *PZP_WINDOW_MONITOR_SEARCH;

static
BOOL
CALLBACK
ZpWindowCapture_FindMonitor(
    _In_ HMONITOR Value,
    _In_ HDC DeviceContext,
    _In_ PRECT Rect,
    _In_ LPARAM Parameter)
{
    PZP_WINDOW_MONITOR_SEARCH Search = (PVOID)Parameter;
    MONITORINFO Info = { sizeof(Info) };

    UNREFERENCED_PARAMETER(DeviceContext);
    if ((Search->Target == ZP_WINDOW_CAPTURE_PRIMARY_MONITOR &&
         GetMonitorInfoW(Value, &Info) && FlagOn(Info.dwFlags, MONITORINFOF_PRIMARY)) ||
        Search->Target == Search->Current)
    {
        Search->Monitor = Value;
        Search->Rect = *Rect;
        return FALSE;
    }
    Search->Current++;
    return TRUE;
}

HRESULT
ZpWindowCapture_ResolveMonitor(
    _In_ ULONG MonitorIndex,
    _Out_ HMONITOR* Monitor,
    _Out_opt_ PRECT MonitorRect)
{
    ZP_WINDOW_MONITOR_SEARCH Search = { MonitorIndex };

    if (Monitor == NULL) return E_INVALIDARG;
    EnumDisplayMonitors(NULL, NULL, ZpWindowCapture_FindMonitor, (LPARAM)&Search);
    if (Search.Monitor == NULL) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    *Monitor = Search.Monitor;
    if (MonitorRect != NULL) *MonitorRect = Search.Rect;
    return S_OK;
}

static
VOID
ZpWindowCapture_CloseWgc(
    _Inout_ PZP_WINDOW_CAPTURE Capture)
{
    if (Capture->FrameHandlerRegistered)
    {
        Capture->FramePool->lpVtbl->remove_FrameArrived(Capture->FramePool,
                                                         Capture->FrameToken);
        Capture->FrameHandlerRegistered = FALSE;
    }
    WgcRelease((IUnknown*)Capture->RateSession);
    WgcRelease((IUnknown*)Capture->Session);
    WgcRelease((IUnknown*)Capture->FramePool);
    WgcRelease((IUnknown*)Capture->Item);
    WgcRelease((IUnknown*)Capture->WgcDevice);
    Capture->RateSession = NULL;
    Capture->Session = NULL;
    Capture->FramePool = NULL;
    Capture->Item = NULL;
    Capture->WgcDevice = NULL;
    if (Capture->FrameHandler != NULL)
    {
        Capture->FrameHandler->Interface.lpVtbl->Release(&Capture->FrameHandler->Interface);
        Capture->FrameHandler = NULL;
    }
}

static
HRESULT
ZpWindowCapture_GetGdiError(VOID)
{
    DWORD Error = GetLastError();

    return HRESULT_FROM_WIN32(Error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : Error);
}

static
HRESULT
ZpWindowCapture_GetGdiRect(
    _In_ PZP_WINDOW_CAPTURE Capture,
    _Out_ PRECT Rect)
{
    HMONITOR Monitor;

    if (FlagOn(Capture->Options.Flags, ZP_WINDOW_CAPTURE_DESKTOP))
    {
        return ZpWindowCapture_ResolveMonitor(Capture->Options.MonitorIndex, &Monitor, Rect);
    }
    if (GetWindowRect(Capture->Window, Rect)) return S_OK;
    return ZpWindowCapture_GetGdiError();
}

static
VOID
ZpWindowCapture_CloseGdiSurface(
    _Inout_ PZP_WINDOW_CAPTURE Capture)
{
    if (Capture->GdiMemory != NULL && Capture->GdiPreviousBitmap != NULL)
    {
        SelectObject(Capture->GdiMemory, Capture->GdiPreviousBitmap);
    }
    if (Capture->GdiBitmap != NULL) DeleteObject(Capture->GdiBitmap);
    if (Capture->GdiMemory != NULL) DeleteDC(Capture->GdiMemory);
    Capture->GdiMemory = NULL;
    Capture->GdiBitmap = NULL;
    Capture->GdiPreviousBitmap = NULL;
    Capture->GdiBits = NULL;
}

static
HRESULT
ZpWindowCapture_CreateGdiSurface(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    BITMAPINFO Information;

    ZpWindowCapture_CloseGdiSurface(Capture);
    Capture->GdiMemory = CreateCompatibleDC(NULL);
    if (Capture->GdiMemory == NULL) return ZpWindowCapture_GetGdiError();
    RtlZeroMemory(&Information, sizeof(Information));
    Information.bmiHeader.biSize = sizeof(Information.bmiHeader);
    Information.bmiHeader.biWidth = Width;
    Information.bmiHeader.biHeight = -(LONG)Height;
    Information.bmiHeader.biPlanes = 1;
    Information.bmiHeader.biBitCount = 32;
    Capture->GdiBitmap = CreateDIBSection(NULL,
                                           &Information,
                                           DIB_RGB_COLORS,
                                           &Capture->GdiBits,
                                           NULL,
                                           0);
    if (Capture->GdiBitmap == NULL) return ZpWindowCapture_GetGdiError();
    Capture->GdiPreviousBitmap = SelectObject(Capture->GdiMemory, Capture->GdiBitmap);
    if (Capture->GdiPreviousBitmap == NULL || Capture->GdiPreviousBitmap == HGDI_ERROR)
    {
        Capture->GdiPreviousBitmap = NULL;
        return ZpWindowCapture_GetGdiError();
    }
    return S_OK;
}

static
HRESULT
ZpWindowCapture_CreateGdi(
    _Inout_ PZP_WINDOW_CAPTURE Capture)
{
    RECT Rect;
    LONGLONG Width, Height;
    HRESULT Result;

    Result = ZpWindowCapture_GetGdiRect(Capture, &Rect);
    if (FAILED(Result)) return Result;
    Width = (LONGLONG)Rect.right - Rect.left;
    Height = (LONGLONG)Rect.bottom - Rect.top;
    if (Width <= 0 || Width > MAXLONG || Height <= 0 || Height > MAXLONG) return E_INVALIDARG;
    Capture->Size.Width = (INT32)Width;
    Capture->Size.Height = (INT32)Height;
    Capture->Gdi = TRUE;
    return S_OK;
}

static
HRESULT
ZpWindowCapture_CreateWgc(
    _Inout_ PZP_WINDOW_CAPTURE Object)
{
    WGC_ITEM_INTEROP* ItemInterop = NULL;
    WGC_FRAME_POOL_STATICS* FramePoolStatics = NULL;
    WGC_SESSION2* Session2 = NULL;
    WGC_SESSION4* Session4 = NULL;
    WGC_SESSION6* Session6 = NULL;
    IDXGIDevice* DxgiDevice = NULL;
    IInspectable* InspectableDevice = NULL;
    struct __x_ABI_CWindows_CFoundation_CTimeSpan Interval;
    RTL_OSVERSIONINFOW Version = { sizeof(Version) };
    HMONITOR CaptureMonitor = NULL;
    HRESULT Result;

    RtlGetVersion(&Version);
    if (Version.dwBuildNumber < ZP_WINDOW_CAPTURE_MIN_BUILD)
    {
        return HRESULT_FROM_WIN32(ERROR_OLD_WIN_VERSION);
    }
    Result = Object->Device->lpVtbl->QueryInterface(Object->Device,
                                                     &WgcDxgiDeviceIid,
                                                     (PVOID*)&DxgiDevice);
    if (FAILED(Result)) goto Cleanup;
    Result = CreateDirect3D11DeviceFromDXGIDevice(DxgiDevice, &InspectableDevice);
    if (FAILED(Result)) goto Cleanup;
    Result = InspectableDevice->lpVtbl->QueryInterface(InspectableDevice,
                                                       &WgcDeviceIid,
                                                       (PVOID*)&Object->WgcDevice);
    if (FAILED(Result)) goto Cleanup;
    Result = ZpWindowCapture_CreateFactory(
        RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem,
        &WgcCaptureItemInteropIid,
        (PVOID*)&ItemInterop);
    if (FAILED(Result)) goto Cleanup;
    if (FlagOn(Object->Options.Flags, ZP_WINDOW_CAPTURE_DESKTOP))
    {
        Result = ZpWindowCapture_ResolveMonitor(Object->Options.MonitorIndex, &CaptureMonitor, NULL);
        if (FAILED(Result)) goto Cleanup;
    }
    Result = FlagOn(Object->Options.Flags, ZP_WINDOW_CAPTURE_DESKTOP) ?
                 ItemInterop->lpVtbl->CreateForMonitor(
                     ItemInterop,
                     CaptureMonitor,
                     &WgcCaptureItemIid,
                     (PVOID*)&Object->Item) :
                 ItemInterop->lpVtbl->CreateForWindow(ItemInterop,
                                                      Object->Window,
                                                      &WgcCaptureItemIid,
                                                      (PVOID*)&Object->Item);
    if (FAILED(Result)) goto Cleanup;
    Result = Object->Item->lpVtbl->get_Size(Object->Item, &Object->Size);
    if (FAILED(Result)) goto Cleanup;
    if (Object->Size.Width <= 0 || Object->Size.Height <= 0)
    {
        Result = E_INVALIDARG;
        goto Cleanup;
    }
    Result = ZpWindowCapture_CreateFactory(
        RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool,
        &WgcFramePoolStaticsIid,
        (PVOID*)&FramePoolStatics);
    if (FAILED(Result)) goto Cleanup;
    Result = FramePoolStatics->lpVtbl->CreateFreeThreaded(
        FramePoolStatics,
        Object->WgcDevice,
        DirectXPixelFormat_B8G8R8A8UIntNormalized,
        ZP_WINDOW_CAPTURE_BUFFER_COUNT,
        Object->Size,
        &Object->FramePool);
    if (FAILED(Result)) goto Cleanup;
    Result = ZpWindowCapture_CreateFrameHandler(&Object->FrameHandler);
    if (FAILED(Result)) goto Cleanup;
    Result = Object->FramePool->lpVtbl->add_FrameArrived(
        Object->FramePool,
        &Object->FrameHandler->Interface,
        &Object->FrameToken);
    if (FAILED(Result)) goto Cleanup;
    Object->FrameHandlerRegistered = TRUE;
    Result = Object->FramePool->lpVtbl->CreateCaptureSession(Object->FramePool,
                                                             Object->Item,
                                                             &Object->Session);
    if (FAILED(Result)) goto Cleanup;
    Result = Object->Session->lpVtbl->QueryInterface(
        Object->Session,
        &WgcSession2Iid,
        (PVOID*)&Session2);
    if (FAILED(Result)) goto Cleanup;
    Result = Object->Session->lpVtbl->QueryInterface(
        Object->Session,
        &WgcSession4Iid,
        (PVOID*)&Session4);
    if (FAILED(Result)) goto Cleanup;
    Result = Object->Session->lpVtbl->QueryInterface(
        Object->Session,
        &WgcSession5Iid,
        (PVOID*)&Object->RateSession);
    if (FAILED(Result)) goto Cleanup;
    Result = Object->Session->lpVtbl->QueryInterface(
        Object->Session,
        &WgcSession6Iid,
        (PVOID*)&Session6);
    if (FAILED(Result)) goto Cleanup;
    Result = Session2->lpVtbl->put_IsCursorCaptureEnabled(
        Session2,
        FlagOn(Object->Options.Flags, ZP_WINDOW_CAPTURE_CURSOR));
    if (FAILED(Result)) goto Cleanup;
    Result = Session4->lpVtbl->put_DirtyRegionMode(Session4,
                                                   GraphicsCaptureDirtyRegionMode_ReportOnly);
    if (FAILED(Result)) goto Cleanup;
    Interval.Duration = 10000000LL / Object->Options.FrameRate;
    Result = Object->RateSession->lpVtbl->put_MinUpdateInterval(Object->RateSession, Interval);
    if (FAILED(Result)) goto Cleanup;
    Result = Session6->lpVtbl->put_IncludeSecondaryWindows(Session6, TRUE);
    if (FAILED(Result)) goto Cleanup;
    Result = Object->Session->lpVtbl->StartCapture(Object->Session);

Cleanup:
    WgcRelease((IUnknown*)Session6);
    WgcRelease((IUnknown*)Session4);
    WgcRelease((IUnknown*)Session2);
    WgcRelease((IUnknown*)InspectableDevice);
    WgcRelease((IUnknown*)DxgiDevice);
    WgcRelease((IUnknown*)FramePoolStatics);
    WgcRelease((IUnknown*)ItemInterop);
    if (FAILED(Result)) ZpWindowCapture_CloseWgc(Object);
    return Result;
}

HRESULT
ZpWindowCapture_Create(
    _In_ HWND Window,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _Out_ PZP_WINDOW_CAPTURE* Capture)
{
    PZP_WINDOW_CAPTURE Object;
    HRESULT Result;

    Object = Mem_Alloc(sizeof(*Object));
    if (Object == NULL) return E_OUTOFMEMORY;
    RtlZeroMemory(Object, sizeof(*Object));
    Object->Options = *Options;
    Object->Window = Window;
    Object->KeyFrame = TRUE;
    Result = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(Result)) goto Cleanup;
    Object->RoInitialized = TRUE;
    Result = CoCreateInstance(&CLSID_WICImagingFactory2,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_IWICImagingFactory,
                              (PVOID*)&Object->ImagingFactory);
    if (FAILED(Result)) goto Cleanup;
    Result = D3D11CreateDevice(NULL,
                               D3D_DRIVER_TYPE_HARDWARE,
                               NULL,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                               NULL,
                               0,
                               D3D11_SDK_VERSION,
                               &Object->Device,
                               NULL,
                               &Object->Context);
    if (FAILED(Result))
    {
        Result = D3D11CreateDevice(NULL,
                                   D3D_DRIVER_TYPE_WARP,
                                   NULL,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                   NULL,
                                   0,
                                   D3D11_SDK_VERSION,
                                   &Object->Device,
                                   NULL,
                                   &Object->Context);
    }
    if (FAILED(Result)) goto Cleanup;
    Result = ZpWindowCapture_CreateWgc(Object);
    if (FAILED(Result)) Result = ZpWindowCapture_CreateGdi(Object);
    if (FAILED(Result)) goto Cleanup;
    ZpWindowCapture_InitializeVideo(Object);
    *Capture = Object;

Cleanup:
    if (FAILED(Result)) ZpWindowCapture_Close(Object);
    return Result;
}

HRESULT
ZpWindowCapture_UpdateOptions(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options)
{
    struct __x_ABI_CWindows_CFoundation_CTimeSpan Interval;
    LOGICAL Resize = Capture->Options.MaxDimension != Options->MaxDimension;
    HRESULT Result;

    Result = S_OK;
    if (Capture->RateSession != NULL)
    {
        Interval.Duration = 10000000LL / Options->FrameRate;
        Result = Capture->RateSession->lpVtbl->put_MinUpdateInterval(Capture->RateSession, Interval);
    }
    if (SUCCEEDED(Result))
    {
        Capture->Options = *Options;
        if (Resize)
        {
            Capture->VideoWidth = Capture->VideoHeight = 0;
            Capture->KeyFrame = TRUE;
            ZpWindowCapture_ResetVideoProcessor(Capture);
        }
    }
    return Result;
}

VOID
ZpWindowCapture_RequestKeyFrame(
    _Inout_ PZP_WINDOW_CAPTURE Capture)
{
    Capture->KeyFrame = TRUE;
}

VOID
ZpWindowCapture_AddRefFrame(
    _In_ PZP_WINDOW_CAPTURE_FRAME Frame)
{
    InterlockedIncrement(&Frame->ReferenceCount);
}

VOID
ZpWindowCapture_ReleaseFrame(
    _In_ PZP_WINDOW_CAPTURE_FRAME Frame)
{
    if (InterlockedDecrement(&Frame->ReferenceCount) != 0) return;
    WgcRelease((IUnknown*)Frame->Texture);
    Mem_Free(Frame);
}

static
VOID
ZpWindowCapture_DrawGdiCursor(
    _In_ PZP_WINDOW_CAPTURE Capture,
    _In_ const RECT* Rect)
{
    CURSORINFO Cursor = { sizeof(Cursor) };
    ICONINFO Information;

    if (!FlagOn(Capture->Options.Flags, ZP_WINDOW_CAPTURE_CURSOR) ||
        !GetCursorInfo(&Cursor) || !FlagOn(Cursor.flags, CURSOR_SHOWING) ||
        Cursor.ptScreenPos.x < Rect->left || Cursor.ptScreenPos.x >= Rect->right ||
        Cursor.ptScreenPos.y < Rect->top || Cursor.ptScreenPos.y >= Rect->bottom ||
        !GetIconInfo(Cursor.hCursor, &Information))
    {
        return;
    }
    DrawIconEx(Capture->GdiMemory,
               Cursor.ptScreenPos.x - Rect->left - Information.xHotspot,
               Cursor.ptScreenPos.y - Rect->top - Information.yHotspot,
               Cursor.hCursor,
               0,
               0,
               0,
               NULL,
               DI_NORMAL);
    if (Information.hbmColor != NULL) DeleteObject(Information.hbmColor);
    if (Information.hbmMask != NULL) DeleteObject(Information.hbmMask);
}

static
HRESULT
ZpWindowCapture_NextGdiFrame(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_CAPTURE_FRAME* Frame)
{
    PZP_WINDOW_CAPTURE_FRAME Object;
    D3D11_TEXTURE2D_DESC Description;
    D3D11_SUBRESOURCE_DATA Data;
    RECT Rect;
    HDC Source;
    LONGLONG Width, Height;
    ULONGLONG Now, Delay;
    ULONG Interval;
    HRESULT Result;

    Interval = max(1UL, 1000 / Capture->Options.FrameRate);
    Now = GetTickCount64();
    if (Capture->GdiFrameTime != 0 && Now - Capture->GdiFrameTime < Interval)
    {
        Delay = Interval - (Now - Capture->GdiFrameTime);
        if (TimeoutMilliseconds != INFINITE && Delay > TimeoutMilliseconds)
        {
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        Sleep((DWORD)Delay);
    }
    Result = ZpWindowCapture_GetGdiRect(Capture, &Rect);
    if (FAILED(Result)) return Result;
    Width = (LONGLONG)Rect.right - Rect.left;
    Height = (LONGLONG)Rect.bottom - Rect.top;
    if (Width <= 0 || Width > MAXLONG || Height <= 0 || Height > MAXLONG) return E_INVALIDARG;
    if (Width != Capture->Size.Width || Height != Capture->Size.Height || Capture->GdiMemory == NULL)
    {
        Result = ZpWindowCapture_CreateGdiSurface(Capture, (ULONG)Width, (ULONG)Height);
        if (FAILED(Result)) return Result;
        if (Width != Capture->Size.Width || Height != Capture->Size.Height)
        {
            Capture->Size.Width = (INT32)Width;
            Capture->Size.Height = (INT32)Height;
            Capture->VideoWidth = Capture->VideoHeight = 0;
            Capture->KeyFrame = TRUE;
            WgcRelease((IUnknown*)Capture->SampleAllocator);
            Capture->SampleAllocator = NULL;
            WgcRelease((IUnknown*)Capture->Staging);
            Capture->Staging = NULL;
            ZpWindowCapture_ResetVideoProcessor(Capture);
        }
    }
    Source = FlagOn(Capture->Options.Flags, ZP_WINDOW_CAPTURE_DESKTOP) ?
                 GetDC(NULL) :
                 GetDCEx(Capture->Window, NULL, DCX_WINDOW | DCX_CACHE);
    if (Source == NULL) return ZpWindowCapture_GetGdiError();
    SetLastError(ERROR_SUCCESS);
    if (!BitBlt(Capture->GdiMemory,
                0,
                0,
                (INT)Width,
                (INT)Height,
                Source,
                FlagOn(Capture->Options.Flags, ZP_WINDOW_CAPTURE_DESKTOP) ? Rect.left : 0,
                FlagOn(Capture->Options.Flags, ZP_WINDOW_CAPTURE_DESKTOP) ? Rect.top : 0,
                SRCCOPY | CAPTUREBLT))
    {
        Result = ZpWindowCapture_GetGdiError();
        ReleaseDC(FlagOn(Capture->Options.Flags, ZP_WINDOW_CAPTURE_DESKTOP) ? NULL : Capture->Window,
                  Source);
        return Result;
    }
    ReleaseDC(FlagOn(Capture->Options.Flags, ZP_WINDOW_CAPTURE_DESKTOP) ? NULL : Capture->Window,
              Source);
    ZpWindowCapture_DrawGdiCursor(Capture, &Rect);
    Object = Mem_Alloc(sizeof(*Object));
    if (Object == NULL) return E_OUTOFMEMORY;
    RtlZeroMemory(&Description, sizeof(Description));
    Description.Width = (UINT)Width;
    Description.Height = (UINT)Height;
    Description.MipLevels = 1;
    Description.ArraySize = 1;
    Description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Description.SampleDesc.Count = 1;
    Description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Data.pSysMem = Capture->GdiBits;
    Data.SysMemPitch = (UINT)Width * sizeof(ULONG);
    Result = Capture->Device->lpVtbl->CreateTexture2D(Capture->Device,
                                                       &Description,
                                                       &Data,
                                                       &Object->Texture);
    if (FAILED(Result))
    {
        Mem_Free(Object);
        return Result;
    }
    Object->ReferenceCount = 1;
    Object->Size = Capture->Size;
    Object->Time.Duration = MFGetSystemTime();
    Object->DirtyRect.X = 0;
    Object->DirtyRect.Y = 0;
    Object->DirtyRect.Width = (INT)Width;
    Object->DirtyRect.Height = (INT)Height;
    Object->DirtyResult = S_OK;
    Capture->GdiFrameTime = GetTickCount64();
    *Frame = Object;
    return S_OK;
}

_Success_(return == S_OK)
static
HRESULT
ZpWindowCapture_NextWgcFrame(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_CAPTURE_FRAME* Frame)
{
    PZP_WINDOW_CAPTURE_FRAME Object = NULL;
    WGC_FRAME* WgcFrame = NULL;
    WGC_SURFACE* Surface = NULL;
    WGC_DXGI_ACCESS* DxgiAccess = NULL;
    struct __x_ABI_CWindows_CGraphics_CSizeInt32 Size;
    ULONGLONG Start, Elapsed;
    LARGE_INTEGER Timeout;
    HRESULT Result;
    NTSTATUS Status;

    Start = GetTickCount64();
    for (;;)
    {
        Result = Capture->FramePool->lpVtbl->TryGetNextFrame(Capture->FramePool, &WgcFrame);
        if (FAILED(Result) || WgcFrame != NULL) break;
        Elapsed = GetTickCount64() - Start;
        if (Elapsed >= TimeoutMilliseconds) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        Timeout.QuadPart = -(LONGLONG)(TimeoutMilliseconds - Elapsed) * 10000;
        Status = NtWaitForSingleObject(Capture->FrameHandler->Event, FALSE, &Timeout);
        if (Status == STATUS_TIMEOUT) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        if (!NT_SUCCESS(Status)) return HRESULT_FROM_NT(Status);
    }
    if (FAILED(Result)) return Result;
    Result = WgcFrame->lpVtbl->get_ContentSize(WgcFrame, &Size);
    if (FAILED(Result)) goto Cleanup;
    if (Size.Width <= 0 || Size.Height <= 0)
    {
        Result = E_INVALIDARG;
        goto Cleanup;
    }
    if (Size.Width != Capture->Size.Width || Size.Height != Capture->Size.Height)
    {
        Capture->Size = Size;
        WgcRelease((IUnknown*)Capture->Staging);
        Capture->Staging = NULL;
        ZpWindowCapture_ResetVideoProcessor(Capture);
        Result = Capture->FramePool->lpVtbl->Recreate(Capture->FramePool,
                                                       Capture->WgcDevice,
                                                       DirectXPixelFormat_B8G8R8A8UIntNormalized,
                                                       ZP_WINDOW_CAPTURE_BUFFER_COUNT,
                                                       Size);
        if (SUCCEEDED(Result)) Result = S_FALSE;
        goto Cleanup;
    }
    Object = Mem_Alloc(sizeof(*Object));
    if (Object == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    Object->ReferenceCount = 1;
    Object->Texture = NULL;
    Object->Size = Size;
    Result = WgcFrame->lpVtbl->get_SystemRelativeTime(WgcFrame, &Object->Time);
    if (FAILED(Result)) goto Cleanup;
    Object->DirtyResult = ZpWindowCapture_GetDirtyRect(WgcFrame,
                                                       Size.Width,
                                                       Size.Height,
                                                       &Object->DirtyRect);
    Result = WgcFrame->lpVtbl->get_Surface(WgcFrame, &Surface);
    if (FAILED(Result)) goto Cleanup;
    Result = Surface->lpVtbl->QueryInterface(Surface, &WgcDxgiAccessIid, (PVOID*)&DxgiAccess);
    if (FAILED(Result)) goto Cleanup;
    Result = DxgiAccess->lpVtbl->GetInterface(DxgiAccess, &WgcTextureIid, (PVOID*)&Object->Texture);
    if (SUCCEEDED(Result))
    {
        *Frame = Object;
        Object = NULL;
    }

Cleanup:
    if (Object != NULL)
    {
        WgcRelease((IUnknown*)Object->Texture);
        Mem_Free(Object);
    }
    WgcRelease((IUnknown*)DxgiAccess);
    WgcRelease((IUnknown*)Surface);
    WgcRelease((IUnknown*)WgcFrame);
    return Result;
}

_Success_(return == S_OK)
HRESULT
ZpWindowCapture_NextFrame(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_CAPTURE_FRAME* Frame)
{
    HRESULT Result;

    if (Capture->Gdi)
    {
        return ZpWindowCapture_NextGdiFrame(Capture, TimeoutMilliseconds, Frame);
    }
    Result = ZpWindowCapture_NextWgcFrame(Capture, TimeoutMilliseconds, Frame);
    if (Result == S_OK)
    {
        Capture->WgcFrameReceived = TRUE;
        return S_OK;
    }
    if (SUCCEEDED(Result) ||
        (Result == HRESULT_FROM_WIN32(ERROR_TIMEOUT) && Capture->WgcFrameReceived))
    {
        return Result;
    }
    ZpWindowCapture_CloseWgc(Capture);
    Result = ZpWindowCapture_CreateGdi(Capture);
    if (FAILED(Result)) return Result;
    return ZpWindowCapture_NextGdiFrame(Capture, TimeoutMilliseconds, Frame);
}

HRESULT
ZpWindowCapture_EncodeFrame(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ PZP_WINDOW_CAPTURE_FRAME Frame,
    _Out_ PZP_WINDOW_CAPTURE_IMAGE Image)
{
    struct __x_ABI_CWindows_CGraphics_CSizeInt32 Size;
    WICRect SourceRect, OutputRect;
    ULONG OutputWidth, OutputHeight;
    ULONGLONG DirtyArea, CanvasArea;
    LOGICAL KeyFrame;
    HRESULT Result;
    Size = Frame->Size;
    KeyFrame = Capture->KeyFrame;
    SourceRect.X = 0;
    SourceRect.Y = 0;
    SourceRect.Width = Size.Width;
    SourceRect.Height = Size.Height;
    if (!KeyFrame)
    {
        Result = Frame->DirtyResult;
        if (Result == S_OK)
        {
            SourceRect = Frame->DirtyRect;
            DirtyArea = (ULONGLONG)SourceRect.Width * SourceRect.Height;
            CanvasArea = (ULONGLONG)Size.Width * Size.Height;
            KeyFrame = DirtyArea * 100 >= CanvasArea * ZP_WINDOW_CAPTURE_DIRTY_THRESHOLD;
        }
        else if (SUCCEEDED(Result))
        {
            goto Cleanup;
        }
        else
        {
            goto Cleanup;
        }
    }
    ZpWindowCapture_GetOutputSize(Capture, &OutputWidth, &OutputHeight);
    if (KeyFrame)
    {
        OutputRect.X = 0;
        OutputRect.Y = 0;
        OutputRect.Width = OutputWidth;
        OutputRect.Height = OutputHeight;
    }
    else
    {
        OutputRect.X = (LONG)((ULONGLONG)SourceRect.X * OutputWidth / Size.Width);
        OutputRect.Y = (LONG)((ULONGLONG)SourceRect.Y * OutputHeight / Size.Height);
        OutputRect.Width = (LONG)(((ULONGLONG)(SourceRect.X + SourceRect.Width) * OutputWidth + Size.Width - 1) /
                                  Size.Width) - OutputRect.X;
        OutputRect.Height = (LONG)(((ULONGLONG)(SourceRect.Y + SourceRect.Height) * OutputHeight + Size.Height - 1) /
                                   Size.Height) - OutputRect.Y;
    }
    Result = ZpWindowCapture_Encode(Capture,
                                    Frame->Texture,
                                    &OutputRect,
                                    OutputWidth,
                                    OutputHeight,
                                    !KeyFrame,
                                    &Image->Data,
                                    &Image->Record.DataLength);
    if (FAILED(Result)) goto Cleanup;
    Capture->Sequence++;
    if (Capture->Sequence == 0) Capture->Sequence++;
    Image->Record.Type = KeyFrame ? ZpWindowCaptureRecordKeyFrame : ZpWindowCaptureRecordPatch;
    Image->Record.Sequence = Capture->Sequence;
    Image->Record.CanvasWidth = OutputWidth;
    Image->Record.CanvasHeight = OutputHeight;
    Image->Record.Left = OutputRect.X;
    Image->Record.Top = OutputRect.Y;
    Image->Record.Width = OutputRect.Width;
    Image->Record.Height = OutputRect.Height;
    Capture->KeyFrame = FALSE;

Cleanup:
    return Result;
}

_Success_(return == S_OK)
HRESULT
ZpWindowCapture_Next(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Out_ PZP_WINDOW_CAPTURE_IMAGE Image)
{
    PZP_WINDOW_CAPTURE_FRAME Frame;
    HRESULT Result;

    Result = ZpWindowCapture_NextFrame(Capture, TimeoutMilliseconds, &Frame);
    if (Result == S_OK)
    {
        Result = ZpWindowCapture_EncodeFrame(Capture, Frame, Image);
        ZpWindowCapture_ReleaseFrame(Frame);
    }
    return Result;
}

VOID
ZpWindowCapture_GetFormat(
    _In_ PZP_WINDOW_CAPTURE Capture,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    if (Capture->VideoWidth == 0)
    {
        ZpWindowCapture_GetOutputSize(Capture, &Capture->VideoWidth, &Capture->VideoHeight);
        Capture->VideoWidth &= ~1UL;
        Capture->VideoHeight &= ~1UL;
        Capture->VideoWidth = max(Capture->VideoWidth, ZP_WINDOW_CAPTURE_MIN_VIDEO_DIMENSION);
        Capture->VideoHeight = max(Capture->VideoHeight, ZP_WINDOW_CAPTURE_MIN_VIDEO_DIMENSION);
    }
    *Width = Capture->VideoWidth;
    *Height = Capture->VideoHeight;
}

IMFDXGIDeviceManager*
ZpWindowCapture_GetDeviceManager(
    _In_ PZP_WINDOW_CAPTURE Capture)
{
    return Capture->DeviceManager;
}

HRESULT
ZpWindowCapture_CreateSample(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ PZP_WINDOW_CAPTURE_FRAME Frame,
    _Outptr_ IMFSample** Sample,
    _Out_ PULONGLONG Timestamp)
{
    ID3D11Texture2D* OutputTexture = NULL;
    ID3D11VideoProcessorInputView* InputView = NULL;
    ID3D11VideoProcessorOutputView* OutputView = NULL;
    IMFMediaBuffer* Buffer = NULL;
    IMFDXGIBuffer* DxgiBuffer = NULL;
    IMFSample* Output = NULL;
    struct __x_ABI_CWindows_CGraphics_CSizeInt32 Size = Frame->Size;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC InputDescription = { 0 };
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC OutputDescription = { 0 };
    D3D11_VIDEO_PROCESSOR_STREAM Stream = { 0 };
    D3D11_VIDEO_COLOR Background = { 0 };
    RECT SourceRect, DestinationRect, TargetRect;
    ULONG DestinationWidth, DestinationHeight;
    HRESULT Result;

    Result = S_OK;
    if (Capture->VideoWidth == 0)
    {
        ZpWindowCapture_GetFormat(Capture, &DestinationWidth, &DestinationHeight);
    }
    if (Capture->VideoProcessor == NULL)
    {
        Result = ZpWindowCapture_CreateVideoProcessor(Capture);
        if (FAILED(Result)) goto Cleanup;
    }
    if (Capture->SampleAllocator == NULL)
    {
        Result = ZpWindowCapture_CreateSampleAllocator(Capture);
        if (FAILED(Result)) goto Cleanup;
    }
    Result = Capture->SampleAllocator->lpVtbl->AllocateSample(Capture->SampleAllocator, &Output);
    if (SUCCEEDED(Result)) Result = Output->lpVtbl->GetBufferByIndex(Output, 0, &Buffer);
    if (SUCCEEDED(Result))
    {
        Result = Buffer->lpVtbl->QueryInterface(Buffer, &IID_IMFDXGIBuffer, (PVOID*)&DxgiBuffer);
    }
    if (SUCCEEDED(Result))
    {
        Result = DxgiBuffer->lpVtbl->GetResource(
            DxgiBuffer,
            &IID_ID3D11Texture2D,
            (PVOID*)&OutputTexture);
    }
    if (SUCCEEDED(Result))
    {
        InputDescription.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        Result = Capture->VideoDevice->lpVtbl->CreateVideoProcessorInputView(
            Capture->VideoDevice,
            (ID3D11Resource*)Frame->Texture,
            Capture->VideoEnumerator,
            &InputDescription,
            &InputView);
    }
    if (SUCCEEDED(Result))
    {
        OutputDescription.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        Result = Capture->VideoDevice->lpVtbl->CreateVideoProcessorOutputView(
            Capture->VideoDevice,
            (ID3D11Resource*)OutputTexture,
            Capture->VideoEnumerator,
            &OutputDescription,
            &OutputView);
    }
    if (SUCCEEDED(Result))
    {
        SourceRect.left = SourceRect.top = 0;
        SourceRect.right = Size.Width;
        SourceRect.bottom = Size.Height;
        DestinationWidth = min(Capture->VideoWidth,
                               (ULONG)((ULONGLONG)Size.Width * Capture->VideoHeight / Size.Height));
        DestinationHeight = min(Capture->VideoHeight,
                                (ULONG)((ULONGLONG)Size.Height * Capture->VideoWidth / Size.Width));
        DestinationRect.left = (Capture->VideoWidth - DestinationWidth) / 2;
        DestinationRect.top = (Capture->VideoHeight - DestinationHeight) / 2;
        DestinationRect.right = DestinationRect.left + DestinationWidth;
        DestinationRect.bottom = DestinationRect.top + DestinationHeight;
        TargetRect.left = TargetRect.top = 0;
        TargetRect.right = Capture->VideoWidth;
        TargetRect.bottom = Capture->VideoHeight;
        Background.RGBA.A = 1.0f;
        Capture->VideoContext->lpVtbl->VideoProcessorSetOutputBackgroundColor(
            Capture->VideoContext,
            Capture->VideoProcessor,
            FALSE,
            &Background);
        Capture->VideoContext->lpVtbl->VideoProcessorSetOutputTargetRect(
            Capture->VideoContext,
            Capture->VideoProcessor,
            TRUE,
            &TargetRect);
        Capture->VideoContext->lpVtbl->VideoProcessorSetStreamFrameFormat(
            Capture->VideoContext,
            Capture->VideoProcessor,
            0,
            D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        Capture->VideoContext->lpVtbl->VideoProcessorSetStreamSourceRect(
            Capture->VideoContext,
            Capture->VideoProcessor,
            0,
            TRUE,
            &SourceRect);
        Capture->VideoContext->lpVtbl->VideoProcessorSetStreamDestRect(
            Capture->VideoContext,
            Capture->VideoProcessor,
            0,
            TRUE,
            &DestinationRect);
        Stream.Enable = TRUE;
        Stream.pInputSurface = InputView;
        Result = Capture->VideoContext->lpVtbl->VideoProcessorBlt(
            Capture->VideoContext,
            Capture->VideoProcessor,
            OutputView,
            0,
            1,
            &Stream);
    }
    if (SUCCEEDED(Result))
    {
        Result = Buffer->lpVtbl->SetCurrentLength(
            Buffer,
            Capture->VideoWidth * Capture->VideoHeight * 3 / 2);
    }
    if (SUCCEEDED(Result))
    {
        *Timestamp = Frame->Time.Duration;
        *Sample = Output;
        Output = NULL;
    }

Cleanup:
    WgcRelease((IUnknown*)Output);
    WgcRelease((IUnknown*)DxgiBuffer);
    WgcRelease((IUnknown*)Buffer);
    WgcRelease((IUnknown*)OutputView);
    WgcRelease((IUnknown*)InputView);
    WgcRelease((IUnknown*)OutputTexture);
    return Result;
}

HRESULT
ZpWindowCapture_GetFrameChangeRate(
    _In_ PZP_WINDOW_CAPTURE_FRAME Frame,
    _Out_ PBYTE ChangeRate)
{
    ULONGLONG DirtyArea, CanvasArea;

    if (Frame->DirtyResult == S_FALSE)
    {
        *ChangeRate = 0;
        return S_OK;
    }
    if (FAILED(Frame->DirtyResult)) return Frame->DirtyResult;
    DirtyArea = (ULONGLONG)Frame->DirtyRect.Width * Frame->DirtyRect.Height;
    CanvasArea = (ULONGLONG)Frame->Size.Width * Frame->Size.Height;
    *ChangeRate = (BYTE)min(100, (DirtyArea * 100 + CanvasArea - 1) / CanvasArea);
    return S_OK;
}

_Success_(return == S_OK)
HRESULT
ZpWindowCapture_NextSample(
    _Inout_ PZP_WINDOW_CAPTURE Capture,
    _In_ ULONG TimeoutMilliseconds,
    _Outptr_ IMFSample** Sample,
    _Out_ PULONGLONG Timestamp)
{
    PZP_WINDOW_CAPTURE_FRAME Frame;
    HRESULT Result;

    Result = ZpWindowCapture_NextFrame(Capture, TimeoutMilliseconds, &Frame);
    if (Result == S_OK)
    {
        Result = ZpWindowCapture_CreateSample(Capture, Frame, Sample, Timestamp);
        ZpWindowCapture_ReleaseFrame(Frame);
    }
    return Result;
}

VOID
ZpWindowCapture_FreeImage(
    _Inout_ PZP_WINDOW_CAPTURE_IMAGE Image)
{
    Mem_Free(Image->Data);
}

VOID
ZpWindowCapture_Close(
    _In_opt_ PZP_WINDOW_CAPTURE Capture)
{
    if (Capture == NULL) return;
    ZpWindowCapture_CloseWgc(Capture);
    ZpWindowCapture_CloseGdiSurface(Capture);
    ZpWindowCapture_ResetVideoProcessor(Capture);
    WgcRelease((IUnknown*)Capture->VideoContext);
    WgcRelease((IUnknown*)Capture->VideoDevice);
    WgcRelease((IUnknown*)Capture->SampleAllocator);
    WgcRelease((IUnknown*)Capture->DeviceManager);
    WgcRelease((IUnknown*)Capture->Staging);
    WgcRelease((IUnknown*)Capture->ImagingFactory);
    WgcRelease((IUnknown*)Capture->Context);
    WgcRelease((IUnknown*)Capture->Device);
    if (Capture->RoInitialized) RoUninitialize();
    Mem_Free(Capture);
}
