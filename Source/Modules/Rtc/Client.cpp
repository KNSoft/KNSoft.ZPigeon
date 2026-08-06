#include "Client.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <WebView2.h>
#include <wrl.h>

using Microsoft::WRL::Callback;

#define ZP_RTC_NEGOTIATION_TIMEOUT_MILLISECONDS 30000
#define ZP_RTC_SEND_HEADER_SIZE sizeof(ULONG)

typedef struct _ZP_RTC_SESSION
{
    volatile LONG ReferenceCount;
    volatile LONG Connected;
    volatile LONG Initializing;
    volatile LONG Stopping;
    RTL_SRWLOCK SendLock;
    PZP_CLIENT_OBJECT Owner;
    BYTE Id[ZP_RTC_SESSION_ID_SIZE];
    HANDLE Thread;
    HANDLE StopEvent;
    HANDLE WorkEvent;
    HANDLE AnswerEvent;
    HANDLE SendEvent;
    PWSTR OfferMessage;
    PWSTR ConfigurationMessage;
    PWSTR Answer;
    ULONG AnswerLength;
    ZP_STATUS OpenStatus;
    const BYTE* SendData;
    ULONG SendLength;
    ULONG SendStreamId;
    NTSTATUS SendStatus;
    BOOLEAN SendPending;
    BOOLEAN CanSend;
    HWND Window;
    HMODULE Loader;
    ICoreWebView2Environment* Environment;
    ICoreWebView2Environment12* Environment12;
    ICoreWebView2Controller* Controller;
    ICoreWebView2* WebView;
    ICoreWebView2_17* WebView17;
} ZP_RTC_SESSION, *PZP_RTC_SESSION;

static const WCHAR ZpRtcPage[] =
    L"<!doctype html><meta charset=utf-8><script>"
    L"let pc,channel,servers=[];const post=value=>chrome.webview.postMessage(value);"
    L"const complete=pc=>pc.iceGatheringState==='complete'?Promise.resolve():new Promise((resolve,reject)=>{"
    L"const timeout=setTimeout(()=>reject(new Error('ICE gathering timeout')),10000);"
    L"pc.addEventListener('icegatheringstatechange',()=>{"
    L"if(pc.iceGatheringState==='complete'){clearTimeout(timeout);resolve()}},{once:false})});"
    L"chrome.webview.addEventListener('message',async event=>{try{const value=event.data;"
    L"if(value.startsWith('CONFIG\\n')){servers=value.slice(7).split('\\n').filter(Boolean);return}"
    L"if(!value.startsWith('OFFER\\n'))return;"
    L"pc=new RTCPeerConnection({iceServers:servers.map(url=>({urls:url}))});"
    L"pc.ondatachannel=event=>{channel=event.channel;channel.binaryType='arraybuffer';"
    L"channel.bufferedAmountLowThreshold=1048576;"
    L"channel.onopen=()=>post('OPEN');channel.onclose=()=>post('CLOSED');"
    L"channel.onmessage=event=>{if(event.data==='PING')channel.send('PONG')}};"
    L"pc.onconnectionstatechange=()=>{"
    L"if(pc.connectionState==='failed'||pc.connectionState==='closed')post('CLOSED')};"
    L"await pc.setRemoteDescription({type:'offer',sdp:value.slice(6)});"
    L"await pc.setLocalDescription(await pc.createAnswer());"
    L"await complete(pc);post('ANSWER\\n'+pc.localDescription.sdp)}catch(error){post('ERROR\\n'+error)}});"
    L"chrome.webview.addEventListener('sharedbufferreceived',event=>{const buffer=event.getBuffer();try{"
    L"if(!channel||channel.readyState!=='open')throw new Error('Data channel is closed');channel.send(buffer)}"
    L"catch(error){post('ERROR\\n'+error)}finally{chrome.webview.releaseBuffer(buffer)}"
    L"if(channel&&channel.bufferedAmount<=channel.bufferedAmountLowThreshold)post('DRAIN');else if(channel){"
    L"channel.onbufferedamountlow=()=>{channel.onbufferedamountlow=null;post('DRAIN')}}});post('READY')</script>";

static
VOID
ZpRtc_AddReference(
    _Inout_ PZP_RTC_SESSION Session)
{
    InterlockedIncrement(&Session->ReferenceCount);
}

static
VOID
ZpRtc_Release(
    _Inout_ PZP_RTC_SESSION Session)
{
    if (InterlockedDecrement(&Session->ReferenceCount) != 0) return;
    if (Session->Thread != NULL) NtClose(Session->Thread);
    if (Session->StopEvent != NULL) NtClose(Session->StopEvent);
    if (Session->WorkEvent != NULL) NtClose(Session->WorkEvent);
    if (Session->AnswerEvent != NULL) NtClose(Session->AnswerEvent);
    if (Session->SendEvent != NULL) NtClose(Session->SendEvent);
    Mem_Free(Session->Answer);
    Mem_Free(Session->ConfigurationMessage);
    Mem_Free(Session->OfferMessage);
    Mem_Free(Session);
}

static
VOID
ZpRtc_SetOpenStatus(
    _Inout_ PZP_RTC_SESSION Session,
    _In_ ZP_STATUS Status)
{
    if (WaitForSingleObject(Session->AnswerEvent, 0) != WAIT_TIMEOUT) return;
    Session->OpenStatus = Status;
    NtSetEvent(Session->AnswerEvent, NULL);
}

static
VOID
ZpRtc_InitializationComplete(
    _Inout_ PZP_RTC_SESSION Session)
{
    InterlockedExchange(&Session->Initializing, FALSE);
    if (Session->Stopping) NtSetEvent(Session->StopEvent, NULL);
}

static
HRESULT
ZpRtc_SendMessages(
    _Inout_ PZP_RTC_SESSION Session)
{
    HRESULT Result;

    Result = Session->WebView->PostWebMessageAsString(Session->ConfigurationMessage);
    return SUCCEEDED(Result) ? Session->WebView->PostWebMessageAsString(Session->OfferMessage) : Result;
}

static
HRESULT
ZpRtc_WebMessage(
    _Inout_ PZP_RTC_SESSION Session,
    _In_ ICoreWebView2WebMessageReceivedEventArgs* Arguments)
{
    PWSTR Message;
    SIZE_T Length;
    HRESULT Result;

    Result = Arguments->TryGetWebMessageAsString(&Message);
    if (FAILED(Result)) return Result;
    if (wcscmp(Message, L"READY") == 0)
    {
        Result = ZpRtc_SendMessages(Session);
    }
    else if (wcsncmp(Message, L"ANSWER\n", 7) == 0)
    {
        Length = wcslen(Message + 7);
        Session->Answer = (PWSTR)Mem_Alloc((Length + 1) * sizeof(WCHAR));
        if (Session->Answer == NULL)
        {
            ZpRtc_SetOpenStatus(Session, ZpStatus_FromNtStatus(STATUS_NO_MEMORY));
        }
        else
        {
            RtlCopyMemory(Session->Answer, Message + 7, (Length + 1) * sizeof(WCHAR));
            Session->AnswerLength = (ULONG)Length;
            ZpRtc_SetOpenStatus(Session, ZpStatus_Make(ZpStatusNone, 0));
        }
    }
    else if (wcscmp(Message, L"OPEN") == 0)
    {
        InterlockedExchange(&Session->Connected, TRUE);
        Session->CanSend = TRUE;
        NtSetEvent(Session->WorkEvent, NULL);
    }
    else if (wcscmp(Message, L"DRAIN") == 0)
    {
        Session->CanSend = TRUE;
        NtSetEvent(Session->WorkEvent, NULL);
    }
    else if (wcscmp(Message, L"CLOSED") == 0 || wcsncmp(Message, L"ERROR\n", 6) == 0)
    {
        InterlockedExchange(&Session->Connected, FALSE);
        ZpRtc_SetOpenStatus(Session, ZpStatus_FromNtStatus(STATUS_CONNECTION_DISCONNECTED));
        NtSetEvent(Session->StopEvent, NULL);
    }
    CoTaskMemFree(Message);
    return Result;
}

static
HRESULT
ZpRtc_ControllerCreated(
    _Inout_ PZP_RTC_SESSION Session,
    _In_ HRESULT Error,
    _In_opt_ ICoreWebView2Controller* Controller)
{
    EventRegistrationToken Token;
    RECT Bounds = { 0, 0, 1, 1 };
    HRESULT Result;

    if (FAILED(Error)) return Error;
    Session->Controller = Controller;
    Controller->AddRef();
    Result = Controller->get_CoreWebView2(&Session->WebView);
    if (SUCCEEDED(Result)) Result = Controller->put_Bounds(Bounds);
    if (SUCCEEDED(Result)) Result = Controller->put_IsVisible(FALSE);
    if (SUCCEEDED(Result))
    {
        Result = Session->WebView->QueryInterface(IID_ICoreWebView2_17,
                                                   (VOID**)&Session->WebView17);
    }
    if (SUCCEEDED(Result))
    {
        auto Handler = Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [Session](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* Arguments) -> HRESULT
            {
                HRESULT CallbackResult = ZpRtc_WebMessage(Session, Arguments);
                if (FAILED(CallbackResult))
                {
                    ZpRtc_SetOpenStatus(Session, ZpStatus_FromCode(ZpStatusHResult, (ULONG)CallbackResult));
                    NtSetEvent(Session->StopEvent, NULL);
                }
                return S_OK;
            });
        Result = Session->WebView->add_WebMessageReceived(Handler.Get(), &Token);
    }
    return SUCCEEDED(Result) ? Session->WebView->NavigateToString(ZpRtcPage) : Result;
}

static
HRESULT
ZpRtc_EnvironmentCreated(
    _Inout_ PZP_RTC_SESSION Session,
    _In_ HRESULT Error,
    _In_opt_ ICoreWebView2Environment* Environment)
{
    HRESULT Result;

    if (FAILED(Error)) return Error;
    Session->Environment = Environment;
    Environment->AddRef();
    Result = Environment->QueryInterface(IID_ICoreWebView2Environment12,
                                          (VOID**)&Session->Environment12);
    if (FAILED(Result)) return Result;
    auto Handler = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [Session](HRESULT ControllerError, ICoreWebView2Controller* Controller) -> HRESULT
        {
            HRESULT CallbackResult = ZpRtc_ControllerCreated(Session, ControllerError, Controller);
            if (FAILED(CallbackResult))
            {
                ZpRtc_SetOpenStatus(Session, ZpStatus_FromCode(ZpStatusHResult, (ULONG)CallbackResult));
                NtSetEvent(Session->StopEvent, NULL);
            }
            ZpRtc_InitializationComplete(Session);
            return S_OK;
        });
    return Environment->CreateCoreWebView2Controller(Session->Window, Handler.Get());
}

static
VOID
ZpRtc_ProcessSend(
    _Inout_ PZP_RTC_SESSION Session)
{
    ICoreWebView2SharedBuffer* Buffer = NULL;
    BYTE* Destination = NULL;
    HRESULT Result;

    if (!Session->SendPending || !Session->CanSend) return;
    Result = Session->Environment12->CreateSharedBuffer(ZP_RTC_SEND_HEADER_SIZE + Session->SendLength,
                                                        &Buffer);
    if (SUCCEEDED(Result)) Result = Buffer->get_Buffer(&Destination);
    if (SUCCEEDED(Result))
    {
        RtlCopyMemory(Destination, &Session->SendStreamId, sizeof(Session->SendStreamId));
        RtlCopyMemory(Destination + ZP_RTC_SEND_HEADER_SIZE, Session->SendData, Session->SendLength);
        Result = Session->WebView17->PostSharedBufferToScript(Buffer,
                                                              COREWEBVIEW2_SHARED_BUFFER_ACCESS_READ_ONLY,
                                                              NULL);
    }
    if (Buffer != NULL) Buffer->Release();
    Session->CanSend = FALSE;
    Session->SendStatus = SUCCEEDED(Result) ? STATUS_SUCCESS : STATUS_CONNECTION_DISCONNECTED;
    Session->SendPending = FALSE;
    NtSetEvent(Session->SendEvent, NULL);
    if (FAILED(Result)) NtSetEvent(Session->StopEvent, NULL);
}

static
LRESULT
CALLBACK
ZpRtc_WindowProcedure(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    return DefWindowProcW(Window, Message, WParam, LParam);
}

static
_Function_class_(USER_THREAD_START_ROUTINE)
NTSTATUS
NTAPI
ZpRtc_Worker(
    _In_ PVOID Context)
{
    PZP_RTC_SESSION Session = (PZP_RTC_SESSION)Context;
    WNDCLASSW WindowClass = { 0 };
    WCHAR UserDataFolder[MAX_PATH];
    WCHAR LoaderPath[MAX_PATH];
    WCHAR ModulePath[MAX_PATH];
    HANDLE Events[] = { Session->StopEvent, Session->WorkEvent };
    PWCHAR Separator;
    MSG Message;
    DWORD WaitResult;
    HRESULT Result;
    LOGICAL ComInitialized = FALSE;

    Result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(Result)) goto Cleanup;
    ComInitialized = TRUE;
    WindowClass.lpfnWndProc = ZpRtc_WindowProcedure;
    WindowClass.hInstance = GetModuleHandleW(NULL);
    WindowClass.lpszClassName = L"KNSoft.ZPigeon.Rtc";
    RegisterClassW(&WindowClass);
    Session->Window = CreateWindowExW(0,
                                      WindowClass.lpszClassName,
                                      L"",
                                      WS_OVERLAPPED,
                                      0,
                                      0,
                                      1,
                                      1,
                                      NULL,
                                      NULL,
                                      WindowClass.hInstance,
                                      NULL);
    if (Session->Window == NULL)
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    if (GetModuleFileNameW(NULL, ModulePath, ARRAYSIZE(ModulePath)) == 0)
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    Separator = wcsrchr(ModulePath, L'\\');
    if (Separator == NULL || _snwprintf_s(UserDataFolder,
                                          ARRAYSIZE(UserDataFolder),
                                          _TRUNCATE,
                                          L"%.*s\\WebView2",
                                          (int)(Separator - ModulePath),
                                          ModulePath) < 0)
    {
        Result = HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        goto Cleanup;
    }
    if (_snwprintf_s(LoaderPath,
                     ARRAYSIZE(LoaderPath),
                     _TRUNCATE,
                     L"%.*s\\WebView2Loader.dll",
                     (int)(Separator - ModulePath),
                     ModulePath) < 0)
    {
        Result = HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        goto Cleanup;
    }
    Session->Loader = LoadLibraryExW(LoaderPath, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (Session->Loader == NULL)
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    {
        auto CreateEnvironment = (decltype(&CreateCoreWebView2EnvironmentWithOptions))GetProcAddress(
            Session->Loader,
            "CreateCoreWebView2EnvironmentWithOptions");
        auto Handler = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [Session](HRESULT EnvironmentError, ICoreWebView2Environment* Environment) -> HRESULT
            {
                HRESULT CallbackResult = ZpRtc_EnvironmentCreated(Session, EnvironmentError, Environment);
                if (FAILED(CallbackResult))
                {
                    ZpRtc_SetOpenStatus(Session, ZpStatus_FromCode(ZpStatusHResult, (ULONG)CallbackResult));
                    NtSetEvent(Session->StopEvent, NULL);
                    ZpRtc_InitializationComplete(Session);
                }
                return S_OK;
            });
        Session->Initializing = TRUE;
        Result = CreateEnvironment != NULL ?
                     CreateEnvironment(NULL, UserDataFolder, NULL, Handler.Get()) :
                     HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    if (FAILED(Result))
    {
        Session->Initializing = FALSE;
        goto Cleanup;
    }
    for (;;)
    {
        WaitResult = MsgWaitForMultipleObjects(ARRAYSIZE(Events), Events, FALSE, INFINITE, QS_ALLINPUT);
        if (WaitResult == WAIT_OBJECT_0)
        {
            Session->Stopping = TRUE;
            if (!Session->Initializing) break;
            ResetEvent(Session->StopEvent);
            if (!Session->Initializing) NtSetEvent(Session->StopEvent, NULL);
            continue;
        }
        if (WaitResult == WAIT_OBJECT_0 + 1)
        {
            ZpRtc_ProcessSend(Session);
            continue;
        }
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
    }
    Result = S_OK;

Cleanup:
    InterlockedExchange(&Session->Connected, FALSE);
    ZpRtc_SetOpenStatus(Session,
                        FAILED(Result) ? ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result) :
                                         ZpStatus_FromNtStatus(STATUS_CONNECTION_DISCONNECTED));
    if (Session->SendPending)
    {
        Session->SendStatus = STATUS_CONNECTION_DISCONNECTED;
        Session->SendPending = FALSE;
        NtSetEvent(Session->SendEvent, NULL);
    }
    if (Session->Controller != NULL) Session->Controller->Close();
    if (Session->WebView17 != NULL) Session->WebView17->Release();
    if (Session->WebView != NULL) Session->WebView->Release();
    if (Session->Controller != NULL) Session->Controller->Release();
    if (Session->Environment12 != NULL) Session->Environment12->Release();
    if (Session->Environment != NULL) Session->Environment->Release();
    if (Session->Loader != NULL) FreeLibrary(Session->Loader);
    if (Session->Window != NULL) DestroyWindow(Session->Window);
    if (ComInitialized) CoUninitialize();
    RtlAcquireSRWLockExclusive(&Session->Owner->Lock);
    if (Session->Owner->RtcSession == Session)
    {
        Session->Owner->RtcSession = NULL;
        RtlReleaseSRWLockExclusive(&Session->Owner->Lock);
        ZpRtc_Release(Session);
    }
    else
    {
        RtlReleaseSRWLockExclusive(&Session->Owner->Lock);
    }
    ZpRtc_Release(Session);
    return SUCCEEDED(Result) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static
NTSTATUS
ZpRtc_CreateMessages(
    _Inout_ PZP_RTC_SESSION Session,
    _In_ PCZP_RTC_OPEN_REQUEST_VIEW Request)
{
    ZP_STRING_VIEW IceServer;
    SIZE_T ConfigurationLength = 7;
    ULONG Index, Offset = 0;

    Session->OfferMessage = (PWSTR)Mem_Alloc(((SIZE_T)Request->Offer.Length + 7) * sizeof(WCHAR));
    if (Session->OfferMessage == NULL) return STATUS_NO_MEMORY;
    RtlCopyMemory(Session->OfferMessage, L"OFFER\n", 6 * sizeof(WCHAR));
    RtlCopyMemory(Session->OfferMessage + 6,
                  Request->Offer.Buffer,
                  (SIZE_T)Request->Offer.Length * sizeof(WCHAR));
    Session->OfferMessage[Request->Offer.Length + 6] = UNICODE_NULL;
    for (Index = 0; Index < Request->IceServerCount; Index++)
    {
        if (!NT_SUCCESS(ZpRtc_GetNextIceServer(Request, &Offset, &IceServer))) return STATUS_DATA_ERROR;
        ConfigurationLength += IceServer.Length + 1;
    }
    Session->ConfigurationMessage = (PWSTR)Mem_Alloc((ConfigurationLength + 1) * sizeof(WCHAR));
    if (Session->ConfigurationMessage == NULL) return STATUS_NO_MEMORY;
    RtlCopyMemory(Session->ConfigurationMessage, L"CONFIG\n", 7 * sizeof(WCHAR));
    ConfigurationLength = 7;
    Offset = 0;
    for (Index = 0; Index < Request->IceServerCount; Index++)
    {
        ZpRtc_GetNextIceServer(Request, &Offset, &IceServer);
        RtlCopyMemory(Session->ConfigurationMessage + ConfigurationLength,
                      IceServer.Buffer,
                      (SIZE_T)IceServer.Length * sizeof(WCHAR));
        ConfigurationLength += IceServer.Length;
        Session->ConfigurationMessage[ConfigurationLength++] = L'\n';
    }
    Session->ConfigurationMessage[ConfigurationLength] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpRtc_Open(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PCZP_RTC_OPEN_REQUEST_VIEW Request,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_RTC_SESSION Session;
    LARGE_INTEGER Timeout;
    ZP_STATUS OpenStatus;
    NTSTATUS Status;

    Session = (PZP_RTC_SESSION)Mem_Alloc(sizeof(*Session));
    if (Session == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    RtlZeroMemory(Session, sizeof(*Session));
    Session->ReferenceCount = 1;
    Session->OpenStatus = ZpStatus_FromNtStatus(STATUS_PENDING);
    Session->Owner = Client;
    Session->SendLock = RTL_SRWLOCK_INIT;
    RtlCopyMemory(Session->Id, Request->SessionId, sizeof(Session->Id));
    Session->StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Session->WorkEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    Session->AnswerEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Session->SendEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (Session->StopEvent == NULL || Session->WorkEvent == NULL ||
        Session->AnswerEvent == NULL || Session->SendEvent == NULL)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        ZpRtc_Release(Session);
        return ZpStatus_FromNtStatus(Status);
    }
    Status = ZpRtc_CreateMessages(Session, Request);
    if (!NT_SUCCESS(Status))
    {
        ZpRtc_Release(Session);
        return ZpStatus_FromNtStatus(Status);
    }
    RtlAcquireSRWLockExclusive(&Client->Lock);
    if (Client->RtcSession != NULL)
    {
        RtlReleaseSRWLockExclusive(&Client->Lock);
        ZpRtc_Release(Session);
        return ZpStatus_FromNtStatus(STATUS_DEVICE_BUSY);
    }
    Client->RtcSession = Session;
    ZpRtc_AddReference(Session);
    RtlReleaseSRWLockExclusive(&Client->Lock);
    ZpRtc_AddReference(Session);
    Status = PS_CreateThread(NtCurrentProcess(), TRUE, ZpRtc_Worker, Session, &Session->Thread, NULL);
    if (NT_SUCCESS(Status)) Status = NtResumeThread(Session->Thread, NULL);
    if (!NT_SUCCESS(Status))
    {
        if (Session->Thread != NULL) NtTerminateThread(Session->Thread, Status);
        ZpRtc_Release(Session);
        RtlAcquireSRWLockExclusive(&Client->Lock);
        Client->RtcSession = NULL;
        RtlReleaseSRWLockExclusive(&Client->Lock);
        ZpRtc_Release(Session);
        ZpRtc_Release(Session);
        return ZpStatus_FromNtStatus(Status);
    }
    Timeout.QuadPart = -(LONGLONG)ZP_RTC_NEGOTIATION_TIMEOUT_MILLISECONDS * 10000;
    Status = NtWaitForSingleObject(Session->AnswerEvent, FALSE, &Timeout);
    if (Status == STATUS_TIMEOUT)
    {
        Session->OpenStatus = ZpStatus_FromNtStatus(STATUS_IO_TIMEOUT);
        NtSetEvent(Session->AnswerEvent, NULL);
        NtSetEvent(Session->StopEvent, NULL);
    }
    else if (!NT_SUCCESS(Status))
    {
        Session->OpenStatus = ZpStatus_FromNtStatus(Status);
        NtSetEvent(Session->AnswerEvent, NULL);
        NtSetEvent(Session->StopEvent, NULL);
    }
    if (ZpStatus_IsSuccess(Session->OpenStatus))
    {
        Status = ZpRtc_EncodeAnswer(Session->Answer,
                                    Session->AnswerLength,
                                    NULL,
                                    0,
                                    ResponseLength);
        if (NT_SUCCESS(Status))
        {
            *Response = (PBYTE)Mem_Alloc(*ResponseLength);
            if (*Response == NULL)
            {
                Status = STATUS_NO_MEMORY;
            }
            else
            {
                Status = ZpRtc_EncodeAnswer(Session->Answer,
                                            Session->AnswerLength,
                                            *Response,
                                            *ResponseLength,
                                            ResponseLength);
            }
        }
        if (!NT_SUCCESS(Status)) Session->OpenStatus = ZpStatus_FromNtStatus(Status);
    }
    OpenStatus = Session->OpenStatus;
    ZpRtc_Release(Session);
    return OpenStatus;
}

static
ZP_STATUS
ZpRtc_CloseSession(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_reads_(ZP_RTC_SESSION_ID_SIZE) const BYTE* SessionId)
{
    PZP_RTC_SESSION Session;

    RtlAcquireSRWLockExclusive(&Client->Lock);
    Session = (PZP_RTC_SESSION)Client->RtcSession;
    if (Session == NULL || memcmp(Session->Id, SessionId, sizeof(Session->Id)) != 0)
    {
        RtlReleaseSRWLockExclusive(&Client->Lock);
        return ZpStatus_FromNtStatus(STATUS_NOT_FOUND);
    }
    ZpRtc_AddReference(Session);
    Client->RtcSession = NULL;
    RtlReleaseSRWLockExclusive(&Client->Lock);
    ZpRtc_Release(Session);
    NtSetEvent(Session->StopEvent, NULL);
    NtWaitForSingleObject(Session->Thread, FALSE, NULL);
    ZpRtc_Release(Session);
    return ZpStatus_Make(ZpStatusNone, 0);
}

ZP_STATUS
ZpRtc_Execute(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_RTC_OPEN_REQUEST_VIEW Request;
    const BYTE* SessionId;
    NTSTATUS Status;

    if (OperationId == ZP_RTC_OPERATION_OPEN)
    {
        Status = ZpRtc_DecodeOpenRequest(Payload, PayloadLength, &Request);
        return NT_SUCCESS(Status) ? ZpRtc_Open(Client, &Request, Response, ResponseLength) :
                                      ZpStatus_FromNtStatus(Status);
    }
    if (OperationId == ZP_RTC_OPERATION_CLOSE)
    {
        Status = ZpRtc_DecodeSessionId(Payload, PayloadLength, &SessionId);
        return NT_SUCCESS(Status) ? ZpRtc_CloseSession(Client, SessionId) : ZpStatus_FromNtStatus(Status);
    }
    return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
}

NTSTATUS
ZpRtc_Send(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ ULONG StreamId,
    _In_reads_bytes_(Length) const VOID* Data,
    _In_ ULONG Length)
{
    PZP_RTC_SESSION Session;
    HANDLE Events[2];
    ULONG Offset = 0, ChunkLength;
    DWORD WaitResult;
    NTSTATUS Status = STATUS_SUCCESS;

    if (StreamId == 0 || Data == NULL || Length == 0) return STATUS_INVALID_PARAMETER;
    RtlAcquireSRWLockShared(&Client->Lock);
    Session = (PZP_RTC_SESSION)Client->RtcSession;
    if (Session != NULL) ZpRtc_AddReference(Session);
    RtlReleaseSRWLockShared(&Client->Lock);
    if (Session == NULL || !Session->Connected)
    {
        if (Session != NULL) ZpRtc_Release(Session);
        return STATUS_CONNECTION_DISCONNECTED;
    }
    Events[0] = Session->StopEvent;
    Events[1] = Session->SendEvent;
    RtlAcquireSRWLockExclusive(&Session->SendLock);
    while (Offset < Length)
    {
        ChunkLength = min(Length - Offset, ZP_RTC_DATA_CHUNK_SIZE);
        Session->SendData = (const BYTE*)Add2Ptr(Data, Offset);
        Session->SendLength = ChunkLength;
        Session->SendStreamId = StreamId;
        Session->SendPending = TRUE;
        NtSetEvent(Session->WorkEvent, NULL);
        WaitResult = WaitForMultipleObjects(ARRAYSIZE(Events), Events, FALSE, INFINITE);
        if (WaitResult != WAIT_OBJECT_0 + 1)
        {
            Status = STATUS_CONNECTION_DISCONNECTED;
            break;
        }
        Status = Session->SendStatus;
        if (!NT_SUCCESS(Status)) break;
        Offset += ChunkLength;
    }
    RtlReleaseSRWLockExclusive(&Session->SendLock);
    ZpRtc_Release(Session);
    return Status;
}

VOID
ZpRtc_Close(
    _Inout_ PZP_CLIENT_OBJECT Client)
{
    PZP_RTC_SESSION Session;

    RtlAcquireSRWLockExclusive(&Client->Lock);
    Session = (PZP_RTC_SESSION)Client->RtcSession;
    if (Session != NULL)
    {
        ZpRtc_AddReference(Session);
        Client->RtcSession = NULL;
    }
    RtlReleaseSRWLockExclusive(&Client->Lock);
    if (Session == NULL) return;
    ZpRtc_Release(Session);
    NtSetEvent(Session->StopEvent, NULL);
    NtWaitForSingleObject(Session->Thread, FALSE, NULL);
    ZpRtc_Release(Session);
}
