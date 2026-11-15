#define COBJMACROS

#include "UnitTest.h"
#include "../Modules/Window/Encoder.h"
#include "../Modules/Window/Rate.h"

#include <d3d11.h>
#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <objbase.h>
#include <strsafe.h>
#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#pragma comment(lib, "D3d11.lib")
#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfuuid.lib")
#pragma comment(lib, "Ole32.lib")

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINDOW_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RECORDING_)

#define WINDOW_MEDIA_DIMENSION 128
#define WINDOW_MEDIA_FRAME_COUNT 40
#define WINDOW_MEDIA_TIMEOUT_MILLISECONDS 20000

typedef struct _WINDOW_MEDIA_CONTEXT
{
    PUNITTEST_RESULT Result;
    ZP_WINDOW_VIDEO_CODEC Codec;
    ULONG Frames;
    ULONG KeyFrames;
    ULONG InstanceOutputs;
    ULONG AwaitKeyFrameOutputs;
    BOOL RecoveryPending;
    WCHAR ArtifactDirectory[MAX_PATH];
} WINDOW_MEDIA_CONTEXT, *PWINDOW_MEDIA_CONTEXT;

static
BOOL
WindowMedia_ReadUe(
    _In_reads_bytes_(Length) const BYTE* Data,
    _In_ ULONG Length,
    _Inout_ PULONG BitOffset,
    _Out_ PULONG Value)
{
    ULONG Zeros = 0, Code = 1;

    for (;;)
    {
        if (*BitOffset >= Length * 8) return FALSE;
        ULONG Bit = (Data[*BitOffset / 8] >> (7 - (*BitOffset % 8))) & 1;
        (*BitOffset)++;
        if (Bit != 0) break;
        if (++Zeros > 31) return FALSE;
    }
    for (ULONG Index = 0; Index < Zeros; Index++)
    {
        if (*BitOffset >= Length * 8) return FALSE;
        Code = (Code << 1) | ((Data[*BitOffset / 8] >> (7 - (*BitOffset % 8))) & 1);
        (*BitOffset)++;
    }
    *Value = Code - 1;
    return TRUE;
}

static
ULONG
WindowMedia_StartCode(
    _In_reads_bytes_(Length) const BYTE* Data,
    _In_ ULONG Length,
    _In_ ULONG Offset)
{
    if (Offset + 3 > Length || Data[Offset] != 0 || Data[Offset + 1] != 0) return 0;
    if (Data[Offset + 2] == 1) return 3;
    return Offset + 4 <= Length && Data[Offset + 2] == 0 && Data[Offset + 3] == 1 ? 4 : 0;
}

static
BOOL
WindowMedia_CheckFrame(
    _In_ const ZP_WINDOW_VIDEO_FRAME* Frame,
    _In_ ZP_WINDOW_VIDEO_CODEC Codec)
{
    ULONG Offset = 0, Slices = 0;
    BOOL Idr = FALSE;

    while (Offset < Frame->Length)
    {
        ULONG Prefix = WindowMedia_StartCode(Frame->Data, Frame->Length, Offset);
        if (Prefix == 0)
        {
            Offset++;
            continue;
        }
        ULONG Start = Offset + Prefix, End = Start;
        while (End < Frame->Length && WindowMedia_StartCode(Frame->Data, Frame->Length, End) == 0) End++;
        if (Start == End) return FALSE;
        BYTE Type = Codec == ZpWindowVideoCodecH264 ? Frame->Data[Start] & 31 : (Frame->Data[Start] >> 1) & 63;
        if (Codec == ZpWindowVideoCodecH265 && Type < 32)
        {
            if (End - Start < 2) return FALSE;
            Slices++;
            Idr |= Type >= 16 && Type <= 23;
        }
        else if (Codec == ZpWindowVideoCodecH264 && (Type == 1 || Type == 5))
        {
            BYTE Rbsp[32];
            ULONG RbspLength = 0, Zeros = 0, BitOffset = 0, FirstMacroblock, SliceType;

            for (ULONG Index = Start + 1; Index < End && RbspLength < sizeof(Rbsp); Index++)
            {
                BYTE Value = Frame->Data[Index];
                if (Zeros >= 2 && Value == 3)
                {
                    Zeros = 0;
                    continue;
                }
                Rbsp[RbspLength++] = Value;
                Zeros = Value == 0 ? Zeros + 1 : 0;
            }
            if (!WindowMedia_ReadUe(Rbsp, RbspLength, &BitOffset, &FirstMacroblock) ||
                !WindowMedia_ReadUe(Rbsp, RbspLength, &BitOffset, &SliceType) ||
                SliceType > 9 || SliceType % 5 == 1)
            {
                return FALSE;
            }
            Slices++;
            Idr |= Type == 5;
        }
        Offset = End;
    }
    return Slices != 0 && (!Frame->KeyFrame || Idr);
}

static
VOID
WindowMedia_ExportFrame(
    _In_ const WINDOW_MEDIA_CONTEXT* Context,
    _In_ const ZP_WINDOW_VIDEO_FRAME* Frame)
{
    PUNITTEST_RESULT TEST_PARAMETER_RESULT = Context->Result;
    ZP_WINDOW_CAPTURE_RECORD Record = {
        .Type = Context->Codec == ZpWindowVideoCodecH264 ?
                    (Frame->KeyFrame ? ZpWindowCaptureRecordH264KeyFrame : ZpWindowCaptureRecordH264Delta) :
                    (Frame->KeyFrame ? ZpWindowCaptureRecordH265KeyFrame : ZpWindowCaptureRecordH265Delta),
        .Sequence = Context->Frames + 1,
        .CanvasWidth = WINDOW_MEDIA_DIMENSION, .CanvasHeight = WINDOW_MEDIA_DIMENSION,
        .Width = WINDOW_MEDIA_DIMENSION, .Height = WINDOW_MEDIA_DIMENSION, .DataLength = Frame->Length
    };
    BYTE Header[ZP_WINDOW_CAPTURE_RECORD_WIRE_SIZE];
    WCHAR Path[MAX_PATH + 32];
    ULONG HeaderLength, Written;
    HANDLE File;
    BOOL Success;

    if (Context->ArtifactDirectory[0] == UNICODE_NULL) return;
    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\h%u-%03lu.bin", Context->ArtifactDirectory,
                               Context->Codec == ZpWindowVideoCodecH264 ? 264U : 265U, Record.Sequence)) ||
        !NT_SUCCESS(ZpWindow_EncodeCaptureRecord(&Record, Header, sizeof(Header), &HeaderLength)))
    {
        TEST_FAIL("Cannot encode frame artifact %lu", Record.Sequence);
        return;
    }
    File = CreateFileW(Path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        TEST_FAIL("Cannot create frame artifact %ls (Win32 %lu)", Path, GetLastError());
        return;
    }
    Success = WriteFile(File, Header, HeaderLength, &Written, NULL) && Written == HeaderLength &&
              WriteFile(File, Frame->Data, Frame->Length, &Written, NULL) && Written == Frame->Length;
    NtClose(File);
    if (!Success) TEST_FAIL("Cannot write frame artifact %ls", Path);
}

static
VOID
WindowMedia_AcceptFrame(
    _Inout_ PWINDOW_MEDIA_CONTEXT Context,
    _Inout_ PZP_WINDOW_VIDEO_FRAME Frame)
{
    PUNITTEST_RESULT TEST_PARAMETER_RESULT = Context->Result;
    BOOL Valid = Frame->Data != NULL && Frame->Length != 0 &&
                 Frame->Length <= WINDOW_MEDIA_DIMENSION * WINDOW_MEDIA_DIMENSION * 6;

    TEST_OK(Valid);
    if (Valid) TEST_OK(WindowMedia_CheckFrame(Frame, Context->Codec));
    if (Valid) WindowMedia_ExportFrame(Context, Frame);
    if (Context->RecoveryPending)
    {
        TEST_OK(Frame->KeyFrame);
        Context->RecoveryPending = FALSE;
    }
    if (Context->AwaitKeyFrameOutputs != 0)
    {
        if (Frame->KeyFrame) Context->AwaitKeyFrameOutputs = 0;
        else TEST_OK(--Context->AwaitKeyFrameOutputs != 0);
    }
    Context->Frames++;
    Context->InstanceOutputs++;
    if (Frame->KeyFrame) Context->KeyFrames++;
    ZpWindowVideoEncoder_FreeFrame(Frame);
}

static
HRESULT
WindowMedia_Drain(
    _Inout_ PWINDOW_MEDIA_CONTEXT Context,
    _Inout_ PZP_WINDOW_VIDEO_ENCODER Encoder,
    _In_ ULONG Inputs)
{
    ULONGLONG Started = GetTickCount64();
    LARGE_INTEGER Delay = { .QuadPart = -10000LL };
    ZP_WINDOW_VIDEO_FRAME Frame;
    HRESULT Result;

    while (Context->InstanceOutputs < Inputs)
    {
        Result = ZpWindowVideoEncoder_Receive(Encoder, &Frame);
        if (FAILED(Result)) return Result;
        if (Result == S_OK) WindowMedia_AcceptFrame(Context, &Frame);
        else
        {
            if (GetTickCount64() - Started >= 2000) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            NtDelayExecution(FALSE, &Delay);
        }
    }
    Result = ZpWindowVideoEncoder_Receive(Encoder, &Frame);
    if (Result == S_FALSE) return S_OK;
    if (Result == S_OK)
    {
        WindowMedia_AcceptFrame(Context, &Frame);
        return E_UNEXPECTED;
    }
    return Result;
}

static
_Function_class_(USER_THREAD_START_ROUTINE)
NTSTATUS
NTAPI
WindowMedia_Worker(
    _In_ PVOID Parameter)
{
    PWINDOW_MEDIA_CONTEXT Context = Parameter;
    PUNITTEST_RESULT TEST_PARAMETER_RESULT = Context->Result;
    ID3D11Device* Device = NULL;
    IMFDXGIDeviceManager* Manager = NULL;
    HANDLE CancelEvent = NULL;
    PZP_WINDOW_VIDEO_ENCODER Encoder = NULL;
    MFT_REGISTER_TYPE_INFO Input = { MFMediaType_Video, MFVideoFormat_NV12 };
    MFT_REGISTER_TYPE_INFO Output = {
        MFMediaType_Video,
        Context->Codec == ZpWindowVideoCodecH264 ? MFVideoFormat_H264 : MFVideoFormat_HEVC
    };
    IMFActivate** Activations;
    UINT32 Count;
    UINT Token;
    ULONG InstanceInputs = 0, Rebuilds = 0, Updates = 0;
    BOOL ComInitialized = FALSE, MfStarted = FALSE;
    BOOL ForceNext = TRUE;
    PCSTR Stage = "COM startup";
    NTSTATUS Status;
    HRESULT Result;

    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(Result)) goto Failure;
    ComInitialized = TRUE;
    Stage = "Media Foundation startup";
    Result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(Result)) goto Failure;
    MfStarted = TRUE;
    Stage = "hardware encoder enumeration";
    Result = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                       MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                       &Input,
                       &Output,
                       &Activations,
                       &Count);
    if (FAILED(Result)) goto Failure;
    for (UINT32 Index = 0; Index < Count; Index++) IMFActivate_Release(Activations[Index]);
    CoTaskMemFree(Activations);
    if (Count == 0)
    {
        TEST_SKIP("No hardware H.%u encoder installed", Context->Codec == ZpWindowVideoCodecH264 ? 264U : 265U);
        goto Cleanup;
    }
    Stage = "D3D11 video device creation";
    Result = D3D11CreateDevice(NULL,
                               D3D_DRIVER_TYPE_HARDWARE,
                               NULL,
                               D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                               NULL,
                               0,
                               D3D11_SDK_VERSION,
                               &Device,
                               NULL,
                               NULL);
    if (Result == DXGI_ERROR_UNSUPPORTED)
    {
        TEST_SKIP("No D3D11 hardware video adapter available");
        goto Cleanup;
    }
    if (FAILED(Result)) goto Failure;
    Stage = "DXGI device manager setup";
    Result = MFCreateDXGIDeviceManager(&Token, &Manager);
    if (SUCCEEDED(Result)) Result = IMFDXGIDeviceManager_ResetDevice(Manager, (IUnknown*)Device, Token);
    if (SUCCEEDED(Result))
    {
        Stage = "encoder configuration";
        Result = ZpWindowVideoEncoder_Create(Context->Codec,
                                             WINDOW_MEDIA_DIMENSION,
                                             WINDOW_MEDIA_DIMENSION,
                                             30,
                                             1000000,
                                             Manager,
                                             &Encoder);
    }
    if (FAILED(Result)) goto Failure;
    Context->RecoveryPending = TRUE;
    TEST_OK(ZpWindowVideoEncoder_UpdateBitRate(Encoder, 0) == E_INVALIDARG);
    Stage = "encoder cancellation event creation";
    Status = NtCreateEvent(&CancelEvent,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        Result = HRESULT_FROM_NT(Status);
        goto Failure;
    }
    for (ULONG Index = 0; Index < WINDOW_MEDIA_FRAME_COUNT; Index++)
    {
        ID3D11Texture2D* Texture = NULL;
        IMFMediaBuffer* Buffer = NULL;
        IMFSample* Sample = NULL;
        D3D11_TEXTURE2D_DESC Description = { 0 };
        D3D11_SUBRESOURCE_DATA Data;
        BYTE Pixels[WINDOW_MEDIA_DIMENSION * WINDOW_MEDIA_DIMENSION * 3 / 2];
        ZP_WINDOW_VIDEO_FRAME Frame;

        if (Index == 6 || Index == 20)
        {
            ULONG BitRate = Index == 6 ? 500000 : 750000;

            Stage = "dynamic bitrate update";
            Result = ZpWindowVideoEncoder_UpdateBitRate(Encoder, BitRate);
            if (Result == E_NOTIMPL || Result == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED))
            {
                UnitTest_PrintF("H.%u bitrate property unavailable/read-only (0x%08lX); rebuild at %lu bit/s\n",
                                Context->Codec == ZpWindowVideoCodecH264 ? 264U : 265U,
                                (ULONG)Result,
                                BitRate);
                Stage = "receive pending output before recreation";
                Result = WindowMedia_Drain(Context, Encoder, InstanceInputs);
                if (FAILED(Result)) goto Failure;
                TEST_OK(Context->InstanceOutputs == InstanceInputs);
                ZpWindowVideoEncoder_Close(Encoder);
                Encoder = NULL;
                Stage = "encoder recreation for bitrate capability";
                Result = ZpWindowVideoEncoder_Create(Context->Codec,
                                                     WINDOW_MEDIA_DIMENSION,
                                                     WINDOW_MEDIA_DIMENSION,
                                                     30,
                                                     BitRate,
                                                     Manager,
                                                     &Encoder);
                InstanceInputs = Context->InstanceOutputs = 0;
                ForceNext = Context->RecoveryPending = TRUE;
                Rebuilds++;
            }
            else if (Result == S_OK) Updates++;
            if (FAILED(Result)) goto Failure;
            TEST_OK(Result == S_OK);
        }
        for (ULONG Pixel = 0; Pixel < WINDOW_MEDIA_DIMENSION * WINDOW_MEDIA_DIMENSION; Pixel++)
        {
            Pixels[Pixel] = (BYTE)(16 + ((Pixel / WINDOW_MEDIA_DIMENSION + Pixel + Index * 5) % 220));
        }
        RtlFillMemory(Pixels + WINDOW_MEDIA_DIMENSION * WINDOW_MEDIA_DIMENSION,
                       WINDOW_MEDIA_DIMENSION * WINDOW_MEDIA_DIMENSION / 2,
                       128);
        Description.Width = Description.Height = WINDOW_MEDIA_DIMENSION;
        Description.MipLevels = Description.ArraySize = 1;
        Description.Format = DXGI_FORMAT_NV12;
        Description.SampleDesc.Count = 1;
        Description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        Data.pSysMem = Pixels;
        Data.SysMemPitch = WINDOW_MEDIA_DIMENSION;
        Data.SysMemSlicePitch = sizeof(Pixels);
        Stage = "synthetic NV12 sample creation";
        Result = ID3D11Device_CreateTexture2D(Device, &Description, &Data, &Texture);
        if (SUCCEEDED(Result)) Result = MFCreateDXGISurfaceBuffer(&IID_ID3D11Texture2D,
                                                                 (IUnknown*)Texture,
                                                                 0,
                                                                 FALSE,
                                                                 &Buffer);
        if (SUCCEEDED(Result)) Result = IMFMediaBuffer_SetCurrentLength(Buffer, sizeof(Pixels));
        if (SUCCEEDED(Result)) Result = MFCreateSample(&Sample);
        if (SUCCEEDED(Result)) Result = IMFSample_AddBuffer(Sample, Buffer);
        if (SUCCEEDED(Result))
        {
            BOOL Force = ForceNext || Index == 12 || Index == 24;

            Stage = "synthetic frame encoding";
            if (Force) Context->AwaitKeyFrameOutputs = 2;
            if (Index == 0)
            {
                ULONGLONG Started = GetTickCount64();

                TEST_OK(NT_SUCCESS(NtSetEvent(CancelEvent, NULL)));
                Result = ZpWindowVideoEncoder_Encode(Encoder, Sample, 0, TRUE, CancelEvent, 2000, &Frame);
                TEST_OK(Result == HRESULT_FROM_WIN32(ERROR_CANCELLED));
                TEST_OK(GetTickCount64() - Started < 500);
                if (Result == S_OK) ZpWindowVideoEncoder_FreeFrame(&Frame);
                TEST_OK(NT_SUCCESS(NtClearEvent(CancelEvent)));
                Started = GetTickCount64();
                Result = ZpWindowVideoEncoder_Encode(Encoder, Sample, 0, TRUE, CancelEvent, 0, &Frame);
                TEST_OK(Result == HRESULT_FROM_WIN32(ERROR_TIMEOUT));
                TEST_OK(GetTickCount64() - Started < 500);
                if (Result == S_OK) ZpWindowVideoEncoder_FreeFrame(&Frame);
                TEST_OK(ZpWindowVideoEncoder_Encode(Encoder, Sample, 0, TRUE, CancelEvent, INFINITE, &Frame) ==
                        E_INVALIDARG);
            }
            Result = ZpWindowVideoEncoder_Encode(Encoder,
                                                 Sample,
                                                 (ULONGLONG)Index * 10000000 / 30,
                                                 Force,
                                                 CancelEvent,
                                                 2000,
                                                 &Frame);
        }
        if (Sample != NULL) IMFSample_Release(Sample);
        if (Buffer != NULL) IMFMediaBuffer_Release(Buffer);
        if (Texture != NULL) ID3D11Texture2D_Release(Texture);
        if (FAILED(Result)) goto Failure;
        InstanceInputs++;
        ForceNext = FALSE;
        if (Result == S_FALSE) continue;
        WindowMedia_AcceptFrame(Context, &Frame);
    }
    Stage = "receive final static output";
    Result = WindowMedia_Drain(Context, Encoder, InstanceInputs);
    if (FAILED(Result)) goto Failure;
    TEST_OK(Context->InstanceOutputs == InstanceInputs);
    TEST_OK(Context->Frames == WINDOW_MEDIA_FRAME_COUNT);
    TEST_OK(!Context->RecoveryPending && Context->AwaitKeyFrameOutputs == 0);
    TEST_OK(Context->KeyFrames >= 3);
    UnitTest_PrintF("H.%u synthetic NV12: %lu frames, %lu keyframes; %lu in-place updates, %lu rebuilds, "
                    "all submitted inputs received\n",
                    Context->Codec == ZpWindowVideoCodecH264 ? 264U : 265U,
                    Context->Frames,
                    Context->KeyFrames,
                    Updates,
                    Rebuilds);
    goto Cleanup;

Failure:
    TEST_FAIL("H.%u %s HRESULT 0x%08lX", Context->Codec == ZpWindowVideoCodecH264 ? 264U : 265U, Stage, (ULONG)Result);

Cleanup:
    if (CancelEvent != NULL) NtClose(CancelEvent);
    ZpWindowVideoEncoder_Close(Encoder);
    if (Manager != NULL) IMFDXGIDeviceManager_Release(Manager);
    if (Device != NULL) ID3D11Device_Release(Device);
    if (MfStarted) MFShutdown();
    if (ComInitialized) CoUninitialize();
    return STATUS_SUCCESS;
}

static
VOID
WindowMedia_CheckRate(
    _Inout_ PUNITTEST_RESULT TEST_PARAMETER_RESULT)
{
    ZP_WINDOW_CAPTURE_OPTIONS Options = {
        .Flags = ZP_WINDOW_CAPTURE_DESKTOP,
        .MaxDimension = 3840,
        .FrameRate = 30,
        .Quality = 85,
        .Encoding = { ZpWindowCaptureModeAuto, ZpWindowVideoCodecH264, 0 }
    };

    TEST_OK(ZpWindowCapture_GetVideoBitRate(&Options, 5000000, 75, 3840, 2160, ZpWindowVideoCodecH264) == 3750000);
    TEST_OK(ZpWindowCapture_GetVideoBitRate(&Options, 100000000, 75, 1280, 720, ZpWindowVideoCodecH264) == 2592000);
    TEST_OK(ZpWindowCapture_GetVideoBitRate(&Options, 100000000, 75, 128, 128, ZpWindowVideoCodecH264) == 256000 &&
            Options.Flags == ZP_WINDOW_CAPTURE_DESKTOP && Options.MaxDimension == 3840 &&
            Options.FrameRate == 30 && Options.Quality == 85 && Options.Encoding.Mode == ZpWindowCaptureModeAuto &&
            Options.Encoding.Codec == ZpWindowVideoCodecH264);
}

static
VOID
WindowMedia_Run(
    _Inout_ PUNITTEST_RESULT TEST_PARAMETER_RESULT,
    _In_ ZP_WINDOW_VIDEO_CODEC Codec)
{
    WINDOW_MEDIA_CONTEXT Context = { TEST_PARAMETER_RESULT, Codec };
    LARGE_INTEGER Timeout;
    HANDLE Thread;
    NTSTATUS Status;
    ULONG Length, Attributes;

    WindowMedia_CheckRate(TEST_PARAMETER_RESULT);
    Length = GetEnvironmentVariableW(L"ZP_WINDOW_MEDIA_ARTIFACT_DIR", Context.ArtifactDirectory,
                                     ARRAYSIZE(Context.ArtifactDirectory));
    if (Length != 0)
    {
        Attributes = Length < ARRAYSIZE(Context.ArtifactDirectory) ?
                         GetFileAttributesW(Context.ArtifactDirectory) : INVALID_FILE_ATTRIBUTES;
        if (Length >= ARRAYSIZE(Context.ArtifactDirectory) || Attributes == INVALID_FILE_ATTRIBUTES ||
            !FlagOn(Attributes, FILE_ATTRIBUTE_DIRECTORY))
        {
            TEST_FAIL("ZP_WINDOW_MEDIA_ARTIFACT_DIR must name an existing directory within MAX_PATH");
            return;
        }
        UnitTest_PrintF("H.%u frame artifacts: %ls\\h%u-001.bin through -040.bin\n",
                        Codec == ZpWindowVideoCodecH264 ? 264U : 265U,
                        Context.ArtifactDirectory,
                        Codec == ZpWindowVideoCodecH264 ? 264U : 265U);
    }
    Status = PS_CreateThread(NtCurrentProcess(), FALSE, WindowMedia_Worker, &Context, &Thread, NULL);
    TEST_OK(NT_SUCCESS(Status));
    if (!NT_SUCCESS(Status)) return;
    Timeout.QuadPart = -WINDOW_MEDIA_TIMEOUT_MILLISECONDS * 10000LL;
    Status = NtWaitForSingleObject(Thread, FALSE, &Timeout);
    if (Status != STATUS_SUCCESS)
    {
        TEST_FAIL("Synthetic encoder worker did not finish in %u ms (0x%08lX)",
                   (ULONG)WINDOW_MEDIA_TIMEOUT_MILLISECONDS,
                   (ULONG)Status);
        RtlExitUserProcess((ULONG)STATUS_IO_TIMEOUT);
    }
    NtClose(Thread);
}

#endif

TEST_FUNC(WindowMediaH264)
{
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINDOW_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RECORDING_)
    WindowMedia_Run(TEST_PARAMETER_RESULT, ZpWindowVideoCodecH264);
#else
    TEST_SKIP("Window and Recording encoders are excluded from this Client build");
#endif
}

TEST_FUNC(WindowMediaH265)
{
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINDOW_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_RECORDING_)
    WindowMedia_Run(TEST_PARAMETER_RESULT, ZpWindowVideoCodecH265);
#else
    TEST_SKIP("Window and Recording encoders are excluded from this Client build");
#endif
}
