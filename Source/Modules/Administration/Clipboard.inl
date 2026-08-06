#include <wincodec.h>

#pragma comment(lib, "Windowscodecs.lib")

#define ZP_CLIPBOARD_IMAGE_MAX_DIMENSION 1280
#define ZP_CLIPBOARD_IMAGE_MAX_SIZE 0x00400000

static
PCWSTR
ZpAdministration_GetClipboardFormatName(
    _In_ UINT Format,
    _Out_writes_(NameCount) PWCHAR Name,
    _In_ ULONG NameCount)
{
    return Format >= 0xC000 && GetClipboardFormatNameW(Format, Name, NameCount) != 0 ? Name : NULL;
}

typedef struct _ZP_CLIPBOARD_LISTENER
{
    INIT_ONCE Once;
    HANDLE ChangedEvent;
    HANDLE ReadyEvent;
    HANDLE Thread;
    ZP_STATUS Status;
} ZP_CLIPBOARD_LISTENER;

static ZP_CLIPBOARD_LISTENER ZpClipboardListener = { INIT_ONCE_STATIC_INIT };

static
LRESULT
CALLBACK
ZpAdministration_ClipboardWindowProcedure(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    if (Message == WM_CLIPBOARDUPDATE)
    {
        NtSetEvent(ZpClipboardListener.ChangedEvent, NULL);
        return 0;
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

static
_Function_class_(USER_THREAD_START_ROUTINE)
NTSTATUS
NTAPI
ZpAdministration_ClipboardListenerThread(
    _In_opt_ PVOID Context)
{
    static const WCHAR ClassName[] = L"KNSoft.ZPigeon.ClipboardListener";
    WNDCLASSW WindowClass = { 0 };
    HWND Window;
    MSG Message;
    DWORD Error = ERROR_SUCCESS;

    UNREFERENCED_PARAMETER(Context);
    WindowClass.lpfnWndProc = ZpAdministration_ClipboardWindowProcedure;
    WindowClass.hInstance = GetModuleHandleW(NULL);
    WindowClass.lpszClassName = ClassName;
    if (RegisterClassW(&WindowClass) == 0) Error = GetLastError();
    Window = Error == ERROR_SUCCESS ?
                 CreateWindowExW(0,
                                 ClassName,
                                 NULL,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 HWND_MESSAGE,
                                 NULL,
                                 WindowClass.hInstance,
                                 NULL) : NULL;
    if (Error == ERROR_SUCCESS && Window == NULL) Error = GetLastError();
    if (Error == ERROR_SUCCESS && !AddClipboardFormatListener(Window)) Error = GetLastError();
    ZpClipboardListener.Status = ZpStatus_FromCode(ZpStatusWin32, Error);
    NtSetEvent(ZpClipboardListener.ReadyEvent, NULL);
    if (Error != ERROR_SUCCESS) return STATUS_UNSUCCESSFUL;
    while (GetMessageW(&Message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
    RemoveClipboardFormatListener(Window);
    DestroyWindow(Window);
    return STATUS_SUCCESS;
}

static
BOOL
CALLBACK
ZpAdministration_InitializeClipboardListener(
    _Inout_ PINIT_ONCE InitOnce,
    _In_opt_ PVOID Parameter,
    _Outptr_opt_result_maybenull_ PVOID* Context)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(InitOnce);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Context);
    Status = NtCreateEvent(&ZpClipboardListener.ChangedEvent,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (NT_SUCCESS(Status))
    {
        Status = NtCreateEvent(&ZpClipboardListener.ReadyEvent,
                               EVENT_MODIFY_STATE | SYNCHRONIZE,
                               NULL,
                               NotificationEvent,
                               FALSE);
    }
    if (NT_SUCCESS(Status))
    {
        Status = PS_CreateThread(NtCurrentProcess(),
                                 FALSE,
                                 ZpAdministration_ClipboardListenerThread,
                                 NULL,
                                 &ZpClipboardListener.Thread,
                                 NULL);
    }
    if (NT_SUCCESS(Status)) Status = NtWaitForSingleObject(ZpClipboardListener.ReadyEvent, FALSE, NULL);
    if (!NT_SUCCESS(Status)) ZpClipboardListener.Status = ZpStatus_FromNtStatus(Status);
    if (ZpClipboardListener.ReadyEvent != NULL)
    {
        NtClose(ZpClipboardListener.ReadyEvent);
        ZpClipboardListener.ReadyEvent = NULL;
    }
    if (ZpClipboardListener.Thread != NULL)
    {
        NtClose(ZpClipboardListener.Thread);
        ZpClipboardListener.Thread = NULL;
    }
    if (!ZpStatus_IsSuccess(ZpClipboardListener.Status) && ZpClipboardListener.ChangedEvent != NULL)
    {
        NtClose(ZpClipboardListener.ChangedEvent);
        ZpClipboardListener.ChangedEvent = NULL;
    }
    return TRUE;
}

static
NTSTATUS
ZpAdministration_AddClipboardState(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    return ZpAdministration_AddRecord(Builder,
                                      ZpAdministrationKindClipboardState,
                                      0,
                                      0,
                                      GetClipboardSequenceNumber(),
                                      L"sequence",
                                      NULL,
                                      NULL,
                                      NULL);
}

static
HRESULT
ZpClipboard_EncodeBitmap(
    _In_ HBITMAP Bitmap,
    _Outptr_result_bytebuffer_(*DataLength) PBYTE* Data,
    _Out_ PULONG DataLength)
{
    IWICImagingFactory* Factory = NULL;
    IWICBitmap* Source = NULL;
    IWICBitmapScaler* Scaler = NULL;
    IWICFormatConverter* Converter = NULL;
    IWICBitmapEncoder* Encoder = NULL;
    IWICBitmapFrameEncode* Frame = NULL;
    IPropertyBag2* Properties = NULL;
    IStream* Stream = NULL;
    IWICBitmapSource* Output;
    STATSTG StreamInfo;
    HGLOBAL Global;
    PVOID Bytes;
    GUID PixelFormat = GUID_WICPixelFormat24bppBGR;
    PROPBAG2 Property = { 0 };
    VARIANT Value;
    UINT Width, Height, OutputWidth, OutputHeight;
    HRESULT Result;
    BOOLEAN Uninitialize = FALSE;

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
    Result = Factory->lpVtbl->CreateBitmapFromHBITMAP(Factory,
                                                       Bitmap,
                                                       NULL,
                                                       WICBitmapUsePremultipliedAlpha,
                                                       &Source);
    if (FAILED(Result)) goto Cleanup;
    Result = Source->lpVtbl->GetSize(Source, &Width, &Height);
    if (FAILED(Result) || Width == 0 || Height == 0)
    {
        if (SUCCEEDED(Result)) Result = E_INVALIDARG;
        goto Cleanup;
    }
    OutputWidth = Width;
    OutputHeight = Height;
    if (max(Width, Height) > ZP_CLIPBOARD_IMAGE_MAX_DIMENSION)
    {
        if (Width >= Height)
        {
            OutputWidth = ZP_CLIPBOARD_IMAGE_MAX_DIMENSION;
            OutputHeight = max(1, MulDiv(Height, ZP_CLIPBOARD_IMAGE_MAX_DIMENSION, Width));
        }
        else
        {
            OutputHeight = ZP_CLIPBOARD_IMAGE_MAX_DIMENSION;
            OutputWidth = max(1, MulDiv(Width, ZP_CLIPBOARD_IMAGE_MAX_DIMENSION, Height));
        }
        Result = Factory->lpVtbl->CreateBitmapScaler(Factory, &Scaler);
        if (FAILED(Result)) goto Cleanup;
        Result = Scaler->lpVtbl->Initialize(Scaler,
                                            (IWICBitmapSource*)Source,
                                            OutputWidth,
                                            OutputHeight,
                                            WICBitmapInterpolationModeFant);
        if (FAILED(Result)) goto Cleanup;
        Output = (IWICBitmapSource*)Scaler;
    }
    else
    {
        Output = (IWICBitmapSource*)Source;
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
    Value.fltVal = 0.85f;
    Result = Properties->lpVtbl->Write(Properties, 1, &Property, &Value);
    if (FAILED(Result)) goto Cleanup;
    Result = Frame->lpVtbl->Initialize(Frame, Properties);
    if (FAILED(Result)) goto Cleanup;
    Result = Frame->lpVtbl->SetSize(Frame, OutputWidth, OutputHeight);
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
        StreamInfo.cbSize.LowPart > ZP_CLIPBOARD_IMAGE_MAX_SIZE)
    {
        Result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        goto Cleanup;
    }
    Result = GetHGlobalFromStream(Stream, &Global);
    if (FAILED(Result)) goto Cleanup;
    Bytes = GlobalLock(Global);
    if (Bytes == NULL)
    {
        DWORD Error = GetLastError();

        Result = HRESULT_FROM_WIN32(Error == ERROR_SUCCESS ? ERROR_NOT_ENOUGH_MEMORY : Error);
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
    if (Properties != NULL) Properties->lpVtbl->Release(Properties);
    if (Frame != NULL) Frame->lpVtbl->Release(Frame);
    if (Encoder != NULL) Encoder->lpVtbl->Release(Encoder);
    if (Stream != NULL) Stream->lpVtbl->Release(Stream);
    if (Converter != NULL) Converter->lpVtbl->Release(Converter);
    if (Scaler != NULL) Scaler->lpVtbl->Release(Scaler);
    if (Source != NULL) Source->lpVtbl->Release(Source);
    if (Factory != NULL) Factory->lpVtbl->Release(Factory);
    if (Uninitialize) CoUninitialize();
    return Result;
}

static
ZP_STATUS
ZpAdministration_QueryClipboardImage(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    HBITMAP Bitmap;
    DWORD Error;
    HRESULT Result;

    if (!OpenClipboard(NULL)) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    if (!IsClipboardFormatAvailable(CF_BITMAP))
    {
        CloseClipboard();
        return ZpStatus_FromNtStatus(STATUS_NOT_FOUND);
    }
    Bitmap = GetClipboardData(CF_BITMAP);
    if (Bitmap != NULL)
    {
        Result = ZpClipboard_EncodeBitmap(Bitmap, Response, ResponseLength);
        Error = CloseClipboard() ? ERROR_SUCCESS : GetLastError();
        if (FAILED(Result)) return ZpStatus_FromCode(ZpStatusHResult, Result);
        if (Error == ERROR_SUCCESS) return ZpStatus_FromNtStatus(STATUS_SUCCESS);
        Mem_Free(*Response);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    Error = GetLastError();
    CloseClipboard();
    return ZpStatus_FromCode(ZpStatusWin32, Error == ERROR_SUCCESS ? ERROR_NOT_FOUND : Error);
}

static
ZP_STATUS
ZpAdministration_EnumerateClipboard(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    WCHAR Identity[16], Name[256];
    PCWSTR Text = NULL;
    HGLOBAL TextHandle = NULL;
    SIZE_T TextLength = 0;
    DWORD SessionId, Error = ERROR_SUCCESS;
    UINT Format = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &SessionId))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (SessionId == 0) return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    if (!OpenClipboard(NULL)) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    Status = ZpAdministration_AddClipboardState(&Builder);
    TextHandle = GetClipboardData(CF_UNICODETEXT);
    if (TextHandle != NULL)
    {
        SIZE_T CharacterCount = GlobalSize(TextHandle) / sizeof(WCHAR);

        Text = GlobalLock(TextHandle);
        if (Text == NULL)
        {
            Error = GetLastError();
        }
        else
        {
            while (TextLength < CharacterCount && Text[TextLength] != UNICODE_NULL) TextLength++;
            Status = TextLength == CharacterCount ? STATUS_DATA_ERROR : STATUS_SUCCESS;
            if (TextLength > ZP_CODEC_MAX_ELEMENT_COUNT) Status = STATUS_QUOTA_EXCEEDED;
        }
    }
    SetLastError(ERROR_SUCCESS);
    while (Error == ERROR_SUCCESS && NT_SUCCESS(Status) && (Format = EnumClipboardFormats(Format)) != 0)
    {
        _ultow_s(Format, Identity, ARRAYSIZE(Identity), 10);
        Status = ZpAdministration_AddRecord(
            &Builder,
            ZpAdministrationKindClipboardFormat,
            Format,
            Format == CF_UNICODETEXT ? 1 : 0,
            Format == CF_UNICODETEXT ? TextLength * sizeof(WCHAR) : 0,
            Identity,
            ZpAdministration_GetClipboardFormatName(Format, Name, ARRAYSIZE(Name)),
            NULL,
            Format == CF_UNICODETEXT ? Text : NULL);
    }
    if (Error == ERROR_SUCCESS && NT_SUCCESS(Status)) Error = GetLastError();
    if (Text != NULL) GlobalUnlock(TextHandle);
    if (!CloseClipboard() && Error == ERROR_SUCCESS) Error = GetLastError();
    if (Error == ERROR_SUCCESS && NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }
    ZpAdministration_FreeBuilder(&Builder);
    return Error != ERROR_SUCCESS ? ZpStatus_FromCode(ZpStatusWin32, Error) : ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_ControlClipboard(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    static const WCHAR ClipboardIdentity[] = L"clipboard";
    static const WCHAR UnicodeIdentity[] = L"unicode";
    HGLOBAL Memory = NULL;
    PWCHAR Text;
    DWORD SessionId, Error = ERROR_SUCCESS;

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &SessionId))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (SessionId == 0) return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    if (Control->Action == ZpAdministrationActionConfigure &&
        Control->Identity.Length == RTL_NUMBER_OF(UnicodeIdentity) - 1 &&
        RtlEqualMemory(Control->Identity.Buffer,
                       UnicodeIdentity,
                       (RTL_NUMBER_OF(UnicodeIdentity) - 1) * sizeof(WCHAR)))
    {
        Memory = GlobalAlloc(GMEM_MOVEABLE, ((SIZE_T)Control->Argument.Length + 1) * sizeof(WCHAR));
        if (Memory == NULL) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        Text = GlobalLock(Memory);
        if (Text == NULL)
        {
            Error = GetLastError();
            GlobalFree(Memory);
            return ZpStatus_FromCode(ZpStatusWin32, Error);
        }
        RtlCopyMemory(Text, Control->Argument.Buffer, (SIZE_T)Control->Argument.Length * sizeof(WCHAR));
        Text[Control->Argument.Length] = UNICODE_NULL;
        GlobalUnlock(Memory);
    }
    else if (Control->Action != ZpAdministrationActionDelete ||
             Control->Identity.Length != RTL_NUMBER_OF(ClipboardIdentity) - 1 ||
             !RtlEqualMemory(Control->Identity.Buffer,
                             ClipboardIdentity,
                             (RTL_NUMBER_OF(ClipboardIdentity) - 1) * sizeof(WCHAR)))
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    if (!OpenClipboard(NULL))
    {
        Error = GetLastError();
    }
    else
    {
        if (!EmptyClipboard() || Memory != NULL && SetClipboardData(CF_UNICODETEXT, Memory) == NULL)
        {
            Error = GetLastError();
        }
        else
        {
            Memory = NULL;
        }
        if (!CloseClipboard() && Error == ERROR_SUCCESS) Error = GetLastError();
    }
    if (Memory != NULL) GlobalFree(Memory);
    return ZpStatus_FromCode(ZpStatusWin32, Error);
}

static
ZP_STATUS
ZpAdministration_WaitClipboard(
    _In_ PCZP_STRING_VIEW SequenceView,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    UNICODE_STRING SequenceString;
    ULONG Sequence, Current;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    if (SequenceView->Length == 0 || SequenceView->Length > 10)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    SequenceString.Length = (USHORT)(SequenceView->Length * sizeof(WCHAR));
    SequenceString.MaximumLength = SequenceString.Length;
    SequenceString.Buffer = (PWCHAR)SequenceView->Buffer;
    Status = RtlUnicodeStringToInteger(&SequenceString, 10, &Sequence);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    InitOnceExecuteOnce(&ZpClipboardListener.Once,
                        ZpAdministration_InitializeClipboardListener,
                        NULL,
                        NULL);
    if (!ZpStatus_IsSuccess(ZpClipboardListener.Status)) return ZpClipboardListener.Status;
    Current = GetClipboardSequenceNumber();
    if (Current == Sequence)
    {
        NtClearEvent(ZpClipboardListener.ChangedEvent);
        Current = GetClipboardSequenceNumber();
        if (Current == Sequence)
        {
            Timeout.QuadPart = -50000000LL;
            Status = NtWaitForSingleObject(ZpClipboardListener.ChangedEvent, FALSE, &Timeout);
            if (Status != STATUS_SUCCESS && Status != STATUS_TIMEOUT) return ZpStatus_FromNtStatus(Status);
        }
    }
    Status = ZpAdministration_AddClipboardState(&Builder);
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}
