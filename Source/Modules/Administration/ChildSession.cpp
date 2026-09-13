#include "ChildSession.h"

#include <KNSoft/MakeLifeEasier/System/Registry.h>
#include <UserEnv.h>
#include <WtsApi32.h>
#include <ObjBase.h>
#include <OcIdl.h>
#include <OleAuto.h>
#include <stdio.h>

#import "libid:8C11EFA1-92C3-11D1-BC1E-00C04FA31489" version("1.0") \
    raw_interfaces_only named_guids rename_namespace("MSTSCLib") \
    exclude("wireHWND", "_RemotableHandle", "__MIDL_IWinTypes_0009")

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "Wtsapi32.lib")

#define ZP_RDP_CHILD_SESSION_WORKER_DELAY 1000
#define ZP_RDP_CHILD_SESSION_TOKEN_RETRY_DELAY 250
#define ZP_RDP_CHILD_SESSION_TOKEN_RETRY_COUNT 20
#define ZP_RDP_CHILD_SESSION_COMMAND_CAPACITY \
    (MAX_PATH + ARRAYSIZE(ZP_RDP_CHILD_SESSION_WORKER_ARGUMENT) + 2)
#define ZP_RDP_CHILD_SESSION_STOP_TIMEOUT 5000
#define ZP_RDP_CHILD_SESSION_HOST_CLASS L"AtlAxWin"

static const UNICODE_STRING ZpRdpChildSessionCredentialKey =
    RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\Policies\\Microsoft\\Windows\\CredentialsDelegation");
static const UNICODE_STRING ZpRdpChildSessionCredentialValue =
    RTL_CONSTANT_STRING(L"2147483647");
static const UNICODE_STRING ZpRdpChildSessionCredentialTarget =
    RTL_CONSTANT_STRING(L"TERMSRV/localhost");
static const UNICODE_STRING ZpRdpChildSessionDefaultCredentialPolicy =
    RTL_CONSTANT_STRING(L"AllowDefaultCredentials");
static const UNICODE_STRING ZpRdpChildSessionNtlmCredentialPolicy =
    RTL_CONSTANT_STRING(L"AllowDefCredentialsWhenNTLMOnly");
static const UNICODE_STRING ZpRdpChildSessionPasswordlessKey =
    RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\PasswordLess\\Device");
static const UNICODE_STRING ZpRdpChildSessionPasswordlessValue =
    RTL_CONSTANT_STRING(L"DevicePasswordLessBuildVersion");

typedef BOOL (WINAPI *ZP_ATL_AX_WIN_INIT)(VOID);
typedef HRESULT (WINAPI *ZP_ATL_AX_GET_CONTROL)(
    _In_ HWND Window,
    _Out_ IUnknown** Control);

typedef struct _ZP_RDP_CHILD_SESSION_THREAD_CONTEXT
{
    HANDLE Event;
    ZP_STATUS Status;
} ZP_RDP_CHILD_SESSION_THREAD_CONTEXT, *PZP_RDP_CHILD_SESSION_THREAD_CONTEXT;

static RTL_SRWLOCK ZpRdpChildSessionLock = RTL_SRWLOCK_INIT;
static HANDLE ZpRdpChildSessionThread;
static DWORD ZpRdpChildSessionThreadId;
static volatile LONG ZpRdpChildSessionState;
static volatile LONG ZpRdpChildSessionError;
static volatile LONG ZpRdpChildSessionStopping;

static
DWORD
ZpRdpChildSession_QueryId(
    _Out_ PULONG SessionId,
    _Out_opt_ PWINSTATIONINFORMATION Information)
{
    WINSTATIONINFORMATION LocalInformation;
    DWORD Error;
    ULONG ReturnLength, Value;

    if (!WTSGetChildSessionId(&Value))
    {
        Error = GetLastError();
        return Error == ERROR_FILE_NOT_FOUND ? ERROR_NOT_FOUND : Error;
    }
    if (Value == MAXULONG) return ERROR_NOT_FOUND;
    if (WinStationQueryInformationW(WINSTATION_CURRENT_SERVER,
                                    Value,
                                    WinStationInformation,
                                    Information != NULL ? Information : &LocalInformation,
                                    sizeof(LocalInformation),
                                    &ReturnLength))
    {
        *SessionId = Value;
        return ERROR_SUCCESS;
    }
    Error = GetLastError();
    return Error == ERROR_FILE_NOT_FOUND ? ERROR_NOT_FOUND : Error;
}

static
HRESULT
ZpRdpChildSession_GetProperty(
    _In_ IDispatch* Object,
    _In_ PCWSTR Name,
    _Out_ VARIANT* Value)
{
    LPOLESTR PropertyName = const_cast<LPOLESTR>(Name);
    DISPID Id;
    DISPPARAMS Parameters = { 0 };
    HRESULT Result;

    Result = Object->GetIDsOfNames(IID_NULL,
                                   &PropertyName,
                                   1,
                                   LOCALE_INVARIANT,
                                   &Id);
    if (FAILED(Result)) return Result;
    VariantInit(Value);
    return Object->Invoke(Id,
                          IID_NULL,
                          LOCALE_INVARIANT,
                          DISPATCH_PROPERTYGET,
                          &Parameters,
                          Value,
                          NULL,
                          NULL);
}

static
HRESULT
ZpRdpChildSession_SetProperty(
    _In_ IDispatch* Object,
    _In_ PCWSTR Name,
    _In_ VARIANT* Value)
{
    LPOLESTR PropertyName = const_cast<LPOLESTR>(Name);
    DISPID Id, PutId = DISPID_PROPERTYPUT;
    DISPPARAMS Parameters = { Value, &PutId, 1, 1 };
    HRESULT Result;

    Result = Object->GetIDsOfNames(IID_NULL,
                                   &PropertyName,
                                   1,
                                   LOCALE_INVARIANT,
                                   &Id);
    return FAILED(Result) ?
               Result :
               Object->Invoke(Id,
                              IID_NULL,
                              LOCALE_INVARIANT,
                              DISPATCH_PROPERTYPUT,
                              &Parameters,
                              NULL,
                              NULL,
                              NULL);
}

static
HRESULT
ZpRdpChildSession_Invoke(
    _In_ IDispatch* Object,
    _In_ PCWSTR Name)
{
    LPOLESTR MethodName = const_cast<LPOLESTR>(Name);
    DISPID Id;
    DISPPARAMS Parameters = { 0 };
    HRESULT Result;

    Result = Object->GetIDsOfNames(IID_NULL,
                                   &MethodName,
                                   1,
                                   LOCALE_INVARIANT,
                                   &Id);
    return FAILED(Result) ?
               Result :
               Object->Invoke(Id,
                              IID_NULL,
                              LOCALE_INVARIANT,
                              DISPATCH_METHOD,
                              &Parameters,
                              NULL,
                              NULL,
                              NULL);
}

static
HRESULT
ZpRdpChildSession_GetObject(
    _In_ IDispatch* Object,
    _In_ PCWSTR Name,
    _Out_ IDispatch** Value)
{
    VARIANT Property;
    HRESULT Result;

    Result = ZpRdpChildSession_GetProperty(Object, Name, &Property);
    if (FAILED(Result)) return Result;
    if (Property.vt == VT_DISPATCH && Property.pdispVal != NULL)
    {
        *Value = Property.pdispVal;
        (*Value)->AddRef();
        Result = S_OK;
    }
    else
    {
        Result = Property.vt == VT_UNKNOWN && Property.punkVal != NULL ?
                     Property.punkVal->QueryInterface(IID_PPV_ARGS(Value)) :
                     E_NOINTERFACE;
    }
    VariantClear(&Property);
    return Result;
}

static
HRESULT
ZpRdpChildSession_SetBoolean(
    _In_ IDispatch* Object,
    _In_ PCWSTR Name,
    _In_ VARIANT_BOOL Value)
{
    VARIANT Property;

    VariantInit(&Property);
    Property.vt = VT_BOOL;
    Property.boolVal = Value;
    return ZpRdpChildSession_SetProperty(Object, Name, &Property);
}

static
HRESULT
ZpRdpChildSession_SetLong(
    _In_ IDispatch* Object,
    _In_ PCWSTR Name,
    _In_ LONG Value)
{
    VARIANT Property;

    VariantInit(&Property);
    Property.vt = VT_I4;
    Property.lVal = Value;
    return ZpRdpChildSession_SetProperty(Object, Name, &Property);
}

static
HRESULT
ZpRdpChildSession_SetString(
    _In_ IDispatch* Object,
    _In_ PCWSTR Name,
    _In_ PCWSTR Value)
{
    VARIANT Property;
    HRESULT Result;

    VariantInit(&Property);
    Property.vt = VT_BSTR;
    Property.bstrVal = SysAllocString(Value);
    if (Property.bstrVal == NULL) return E_OUTOFMEMORY;
    Result = ZpRdpChildSession_SetProperty(Object, Name, &Property);
    VariantClear(&Property);
    return Result;
}

static
HRESULT
ZpRdpChildSession_SetExtendedBoolean(
    _In_ MSTSCLib::IMsRdpExtendedSettings* Settings,
    _In_ PCWSTR Name,
    _In_ VARIANT_BOOL Value)
{
    VARIANT Property;
    BSTR PropertyName;
    HRESULT Result;

    PropertyName = SysAllocString(Name);
    if (PropertyName == NULL) return E_OUTOFMEMORY;
    VariantInit(&Property);
    Property.vt = VT_BOOL;
    Property.boolVal = Value;
    Result = Settings->put_Property(PropertyName, &Property);
    SysFreeString(PropertyName);
    return Result;
}

static
BOOLEAN
ZpRdpChildSession_IsRegistryValueMissing(
    _In_ NTSTATUS Status)
{
    return Status == STATUS_OBJECT_NAME_NOT_FOUND ||
           Status == STATUS_OBJECT_PATH_NOT_FOUND;
}

static
NTSTATUS
ZpRdpChildSession_QueryCredentialPolicy(
    _In_ HANDLE Key,
    _In_ PCUNICODE_STRING Policy,
    _Out_ PBOOLEAN Enabled)
{
    PKEY_VALUE_PARTIAL_INFORMATION Data = NULL;
    HANDLE ListKey;
    DWORD PolicyEnabled;
    NTSTATUS Status;

    *Enabled = FALSE;
    Status = Sys_RegQueryDword(Key, Policy, &PolicyEnabled);
    if (ZpRdpChildSession_IsRegistryValueMissing(Status)) return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status) || PolicyEnabled != 1) return Status;
    Status = Sys_RegOpenKeyEx(&ListKey, Key, KEY_QUERY_VALUE, Policy);
    if (ZpRdpChildSession_IsRegistryValueMissing(Status)) return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status)) return Status;
    Status = Sys_RegQueryData(ListKey,
                              &ZpRdpChildSessionCredentialValue,
                              &Data);
    NtClose(ListKey);
    if (ZpRdpChildSession_IsRegistryValueMissing(Status)) return STATUS_SUCCESS;
    if (NT_SUCCESS(Status))
    {
        *Enabled = Data->Type == REG_SZ &&
                   Data->DataLength == ZpRdpChildSessionCredentialTarget.Length + sizeof(WCHAR) &&
                   RtlEqualMemory(Data->Data,
                                  ZpRdpChildSessionCredentialTarget.Buffer,
                                  ZpRdpChildSessionCredentialTarget.Length) &&
                   *(PCWCHAR)(Data->Data + ZpRdpChildSessionCredentialTarget.Length) == UNICODE_NULL;
    }
    Mem_Free(Data);
    return Status;
}

static
NTSTATUS
ZpRdpChildSession_QueryCredentialDelegation(
    _Out_ PBOOLEAN Enabled)
{
    HANDLE Key;
    BOOLEAN DefaultEnabled, NtlmEnabled;
    NTSTATUS Status;

    *Enabled = FALSE;
    Status = Sys_RegOpenKey(&Key,
                            KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS,
                            &ZpRdpChildSessionCredentialKey);
    if (ZpRdpChildSession_IsRegistryValueMissing(Status)) return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status)) return Status;
    Status = ZpRdpChildSession_QueryCredentialPolicy(Key,
                                                     &ZpRdpChildSessionDefaultCredentialPolicy,
                                                     &DefaultEnabled);
    if (NT_SUCCESS(Status))
    {
        Status = ZpRdpChildSession_QueryCredentialPolicy(Key,
                                                         &ZpRdpChildSessionNtlmCredentialPolicy,
                                                         &NtlmEnabled);
    }
    NtClose(Key);
    if (NT_SUCCESS(Status)) *Enabled = DefaultEnabled && NtlmEnabled;
    return Status;
}

static
NTSTATUS
ZpRdpChildSession_SetCredentialPolicy(
    _In_ HANDLE Key,
    _In_ PCUNICODE_STRING Policy,
    _In_ BOOLEAN Enabled)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE ListKey;
    DWORD PolicyEnabled = 1;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)Policy,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               Key,
                               NULL);
    if (Enabled)
    {
        Status = NtSetValueKey(Key,
                               (PUNICODE_STRING)Policy,
                               0,
                               REG_DWORD,
                               &PolicyEnabled,
                               sizeof(PolicyEnabled));
        if (!NT_SUCCESS(Status)) return Status;
        Status = NtCreateKey(&ListKey,
                             KEY_SET_VALUE,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_NON_VOLATILE,
                             NULL);
        if (!NT_SUCCESS(Status)) return Status;
        Status = NtSetValueKey(ListKey,
                               (PUNICODE_STRING)&ZpRdpChildSessionCredentialValue,
                               0,
                               REG_SZ,
                               ZpRdpChildSessionCredentialTarget.Buffer,
                               ZpRdpChildSessionCredentialTarget.Length + sizeof(WCHAR));
    }
    else
    {
        Status = NtOpenKey(&ListKey, KEY_SET_VALUE, &ObjectAttributes);
        if (ZpRdpChildSession_IsRegistryValueMissing(Status)) return STATUS_SUCCESS;
        if (!NT_SUCCESS(Status)) return Status;
        Status = NtDeleteValueKey(ListKey,
                                  (PUNICODE_STRING)&ZpRdpChildSessionCredentialValue);
        if (ZpRdpChildSession_IsRegistryValueMissing(Status)) Status = STATUS_SUCCESS;
    }
    NtClose(ListKey);
    return Status;
}

static
NTSTATUS
ZpRdpChildSession_QueryHelloOnly(
    _Out_ PBOOLEAN Enabled)
{
    HANDLE Key;
    DWORD Value;
    NTSTATUS Status;

    *Enabled = FALSE;
    Status = Sys_RegOpenKey(&Key,
                            KEY_QUERY_VALUE,
                            &ZpRdpChildSessionPasswordlessKey);
    if (ZpRdpChildSession_IsRegistryValueMissing(Status)) return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status)) return Status;
    Status = Sys_RegQueryDword(Key,
                               &ZpRdpChildSessionPasswordlessValue,
                               &Value);
    NtClose(Key);
    if (ZpRdpChildSession_IsRegistryValueMissing(Status)) return STATUS_SUCCESS;
    if (NT_SUCCESS(Status)) *Enabled = Value == 2;
    return Status;
}

static
ZP_STATUS
ZpRdpChildSession_ValidateParent(VOID)
{
    TOKEN_ELEVATION Elevation;
    WINSTATIONINFORMATION Information;
    DWORD Bytes;
    ULONG ReturnLength;

    if (!GetTokenInformation(NtCurrentProcessToken(),
                             TokenElevation,
                             &Elevation,
                             sizeof(Elevation),
                             &Bytes))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (!Elevation.TokenIsElevated)
    {
        return ZpStatus_FromCode(ZpStatusWin32, ERROR_ELEVATION_REQUIRED);
    }
    if (!WinStationQueryInformationW(WINSTATION_CURRENT_SERVER,
                                     WINSTATION_CURRENT_SESSION,
                                     WinStationInformation,
                                     &Information,
                                     sizeof(Information),
                                     &ReturnLength))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (Information.ConnectState != State_Active)
    {
        return ZpStatus_FromCode(ZpStatusWin32, ERROR_CTX_WINSTATION_NOT_FOUND);
    }
    return Information.UserName[0] != L'\0' ?
               ZpStatus_FromNtStatus(STATUS_SUCCESS) :
               ZpStatus_FromCode(ZpStatusWin32, ERROR_NOT_LOGGED_ON);
}

static
ZP_STATUS
ZpRdpChildSession_Logoff(VOID)
{
    DWORD Error, SessionId;

    Error = ZpRdpChildSession_QueryId(&SessionId, NULL);
    if (Error == ERROR_SUCCESS)
    {
        return WinStationReset(WINSTATION_CURRENT_SERVER, SessionId, TRUE) ?
                   ZpStatus_FromNtStatus(STATUS_SUCCESS) :
                   ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    return Error == ERROR_NOT_FOUND ?
               ZpStatus_FromNtStatus(STATUS_SUCCESS) :
               ZpStatus_FromCode(ZpStatusWin32, Error);
}

class ZpRdpChildSessionEventSink final : public IDispatch
{
public:
    STDMETHODIMP QueryInterface(
        _In_ REFIID InterfaceId,
        _COM_Outptr_ VOID** Object) override
    {
        if (Object == NULL) return E_POINTER;
        if (IsEqualIID(InterfaceId, IID_IUnknown) ||
            IsEqualIID(InterfaceId, IID_IDispatch) ||
            IsEqualIID(InterfaceId, MSTSCLib::DIID_IMsTscAxEvents))
        {
            *Object = static_cast<IDispatch*>(this);
            AddRef();
            return S_OK;
        }
        *Object = NULL;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef(VOID) override
    {
        return (ULONG)InterlockedIncrement(&ReferenceCount);
    }

    STDMETHODIMP_(ULONG) Release(VOID) override
    {
        return (ULONG)InterlockedDecrement(&ReferenceCount);
    }

    STDMETHODIMP GetTypeInfoCount(
        _Out_ UINT* Count) override
    {
        if (Count == NULL) return E_POINTER;
        *Count = 0;
        return S_OK;
    }

    STDMETHODIMP GetTypeInfo(
        _In_ UINT,
        _In_ LCID,
        _COM_Outptr_ ITypeInfo**) override
    {
        return E_NOTIMPL;
    }

    STDMETHODIMP GetIDsOfNames(
        _In_ REFIID,
        _In_reads_(NameCount) LPOLESTR*,
        _In_ UINT NameCount,
        _In_ LCID,
        _Out_writes_(NameCount) DISPID*) override
    {
        return E_NOTIMPL;
    }

    STDMETHODIMP Invoke(
        _In_ DISPID Id,
        _In_ REFIID,
        _In_ LCID,
        _In_ WORD,
        _In_ DISPPARAMS* Parameters,
        _Out_opt_ VARIANT*,
        _Out_opt_ EXCEPINFO*,
        _Out_opt_ UINT*) override
    {
        if (Id == 4 || Id == 10)
        {
            if (!InterlockedCompareExchange(&ZpRdpChildSessionStopping, 0, 0) &&
                Parameters != NULL && Parameters->cArgs != 0 &&
                Parameters->rgvarg[0].vt == VT_I4)
            {
                InterlockedExchange(&ZpRdpChildSessionError,
                                    Parameters->rgvarg[0].lVal);
            }
            PostQuitMessage(0);
        }
        return S_OK;
    }

private:
    volatile LONG ReferenceCount = 1;
};

static
HRESULT
ZpRdpChildSession_Advise(
    _In_ IUnknown* Control,
    _In_ IUnknown* Sink,
    _Out_ IConnectionPoint** ConnectionPoint,
    _Out_ PDWORD Cookie)
{
    IConnectionPointContainer* Container;
    IConnectionPoint* Point;
    HRESULT Result;

    Result = Control->QueryInterface(IID_PPV_ARGS(&Container));
    if (FAILED(Result)) return Result;
    Result = Container->FindConnectionPoint(MSTSCLib::DIID_IMsTscAxEvents, &Point);
    Container->Release();
    if (FAILED(Result)) return Result;
    Result = Point->Advise(Sink, Cookie);
    if (FAILED(Result))
    {
        Point->Release();
        return Result;
    }
    *ConnectionPoint = Point;
    return S_OK;
}

static
HRESULT
ZpRdpChildSession_BuildWorkerCommandLine(
    _Out_writes_(MAX_PATH) PWSTR ModulePath,
    _Out_writes_(ZP_RDP_CHILD_SESSION_COMMAND_CAPACITY) PWSTR CommandLine)
{
    DWORD ModuleLength;

    ModuleLength = GetModuleFileNameW(NULL, ModulePath, MAX_PATH);
    if (ModuleLength == 0 || ModuleLength == MAX_PATH)
    {
        return HRESULT_FROM_WIN32(ModuleLength == 0 ?
                                      GetLastError() :
                                      ERROR_FILENAME_EXCED_RANGE);
    }
    return _snwprintf_s(CommandLine,
                        ZP_RDP_CHILD_SESSION_COMMAND_CAPACITY,
                        _TRUNCATE,
                        L"\"%s\" %s",
                        ModulePath,
                        ZP_RDP_CHILD_SESSION_WORKER_ARGUMENT) < 0 ?
               HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE) :
               S_OK;
}

static
HRESULT
ZpRdpChildSession_CreateWorker(
    _In_ ULONG SessionId)
{
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION ProcessInformation;
    TOKEN_ELEVATION Elevation;
    TOKEN_LINKED_TOKEN LinkedToken;
    HANDLE LsaToken = NULL, SessionToken = NULL, WorkerToken = NULL;
    PVOID Environment = NULL;
    WCHAR ModulePath[MAX_PATH];
    WCHAR CommandLine[ZP_RDP_CHILD_SESSION_COMMAND_CAPACITY];
    WCHAR Desktop[] = L"winsta0\\default";
    DWORD Length, Error;
    ULONG LsaProcessId, Retry;
    LARGE_INTEGER Delay;
    NTSTATUS Status;
    HRESULT Result;
    BOOLEAN Impersonating = FALSE, PreviousDebugPrivilege;
    BOOLEAN RestoreDebugPrivilege = FALSE;

    Delay.QuadPart = -(LONGLONG)ZP_RDP_CHILD_SESSION_TOKEN_RETRY_DELAY * 10000;
    Result = ZpRdpChildSession_BuildWorkerCommandLine(ModulePath, CommandLine);
    if (FAILED(Result)) return Result;
    Status = RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE,
                                TRUE,
                                FALSE,
                                &PreviousDebugPrivilege);
    if (!NT_SUCCESS(Status)) return HRESULT_FROM_NT(Status);
    RestoreDebugPrivilege = !PreviousDebugPrivilege;
    Status = Sys_GetLsaProcessId(&LsaProcessId);
    if (NT_SUCCESS(Status))
    {
        Status = PS_DuplicateSystemToken(LsaProcessId,
                                         TokenImpersonation,
                                         &LsaToken);
    }
    if (NT_SUCCESS(Status)) Status = PS_Impersonate(LsaToken);
    if (NT_SUCCESS(Status))
    {
        Impersonating = TRUE;
        Status = NT_AdjustTokenPrivilege(LsaToken,
                                         SE_ASSIGNPRIMARYTOKEN_PRIVILEGE,
                                         SE_PRIVILEGE_ENABLED);
    }
    if (Status == STATUS_SUCCESS)
    {
        Status = NT_AdjustTokenPrivilege(LsaToken,
                                         SE_INCREASE_QUOTA_PRIVILEGE,
                                         SE_PRIVILEGE_ENABLED);
    }
    if (Status != STATUS_SUCCESS)
    {
        Result = HRESULT_FROM_NT(Status);
        goto Cleanup;
    }
    for (Retry = 0;; Retry++)
    {
        Error = NT_GetSessionToken(&SessionToken, SessionId);
        if (Error != ERROR_NO_TOKEN ||
            Retry == ZP_RDP_CHILD_SESSION_TOKEN_RETRY_COUNT)
        {
            break;
        }
        NtDelayExecution(FALSE, &Delay);
    }
    if (Error != ERROR_SUCCESS)
    {
        Result = HRESULT_FROM_WIN32(Error);
        goto Cleanup;
    }
    if (!GetTokenInformation(SessionToken,
                             TokenElevation,
                             &Elevation,
                             sizeof(Elevation),
                             &Length))
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    if (Elevation.TokenIsElevated)
    {
        WorkerToken = SessionToken;
        SessionToken = NULL;
    }
    else
    {
        if (!GetTokenInformation(SessionToken,
                                 TokenLinkedToken,
                                 &LinkedToken,
                                 sizeof(LinkedToken),
                                 &Length))
        {
            Result = HRESULT_FROM_WIN32(GetLastError());
            goto Cleanup;
        }
        WorkerToken = LinkedToken.LinkedToken;
        NtClose(SessionToken);
        SessionToken = NULL;
        if (!GetTokenInformation(WorkerToken,
                                 TokenElevation,
                                 &Elevation,
                                 sizeof(Elevation),
                                 &Length))
        {
            Result = HRESULT_FROM_WIN32(GetLastError());
            goto Cleanup;
        }
        if (!Elevation.TokenIsElevated)
        {
            Result = HRESULT_FROM_WIN32(ERROR_ELEVATION_REQUIRED);
            goto Cleanup;
        }
    }
    if (!CreateEnvironmentBlock(&Environment, WorkerToken, FALSE))
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    Startup.lpDesktop = Desktop;
    if (!CreateProcessAsUserW(WorkerToken,
                              ModulePath,
                              CommandLine,
                              NULL,
                              NULL,
                              FALSE,
                              CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                              Environment,
                              NULL,
                              &Startup,
                              &ProcessInformation))
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    NtClose(ProcessInformation.hThread);
    NtClose(ProcessInformation.hProcess);
    Result = S_OK;

Cleanup:
    if (Environment != NULL) DestroyEnvironmentBlock(Environment);
    if (WorkerToken != NULL) NtClose(WorkerToken);
    if (SessionToken != NULL) NtClose(SessionToken);
    if (Impersonating)
    {
        Status = PS_Impersonate(NULL);
        if (SUCCEEDED(Result) && !NT_SUCCESS(Status))
            Result = HRESULT_FROM_NT(Status);
    }
    if (LsaToken != NULL) NtClose(LsaToken);
    if (RestoreDebugPrivilege)
    {
        Status = RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE,
                                    FALSE,
                                    FALSE,
                                    &PreviousDebugPrivilege);
        if (SUCCEEDED(Result) && !NT_SUCCESS(Status))
            Result = HRESULT_FROM_NT(Status);
    }
    return Result;
}

static
DWORD
WINAPI
ZpRdpChildSession_Thread(
    _In_ PVOID Parameter)
{
    PZP_RDP_CHILD_SESSION_THREAD_CONTEXT Context =
        (PZP_RDP_CHILD_SESSION_THREAD_CONTEXT)Parameter;
    IUnknown* Control = NULL;
    IDispatch* Client = NULL;
    IDispatch* Settings = NULL;
    MSTSCLib::IMsRdpClientNonScriptable5* NonScriptable = NULL;
    MSTSCLib::IMsRdpExtendedSettings* ExtendedSettings = NULL;
    IConnectionPoint* ConnectionPoint = NULL;
    ZpRdpChildSessionEventSink Sink;
    ZP_ATL_AX_WIN_INIT AtlAxWinInit = NULL;
    ZP_ATL_AX_GET_CONTROL AtlAxGetControl = NULL;
    HMODULE Atl = NULL;
    HWND Window = NULL;
    DWORD AdviseCookie = 0;
    MSG Message;
    ULONG WorkerSessionId;
    UINT_PTR WorkerTimer = 0;
    HRESULT Result;
    BOOLEAN Initialized = FALSE, ConnectionStarted = FALSE;
    BOOLEAN NotificationsRegistered = FALSE;

    Result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(Result))
    {
        Initialized = TRUE;
        Atl = LoadLibraryExW(L"atl.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (Atl == NULL)
        {
            Result = HRESULT_FROM_WIN32(GetLastError());
        }
        else
        {
            AtlAxWinInit = (ZP_ATL_AX_WIN_INIT)GetProcAddress(Atl, "AtlAxWinInit");
            AtlAxGetControl = (ZP_ATL_AX_GET_CONTROL)GetProcAddress(Atl, "AtlAxGetControl");
            if (AtlAxWinInit == NULL || AtlAxGetControl == NULL)
            {
                Result = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
            }
            else if (!AtlAxWinInit())
            {
                Result = E_FAIL;
            }
        }
    }
    if (SUCCEEDED(Result))
    {
        Window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                 ZP_RDP_CHILD_SESSION_HOST_CLASS,
                                 L"{A0C63C30-F08D-4AB4-907C-34905D770C7D}",
                                 WS_POPUP | WS_VISIBLE,
                                 -32000,
                                 -32000,
                                 64,
                                 64,
                                 NULL,
                                 NULL,
                                 GetModuleHandleW(NULL),
                                 NULL);
        if (Window == NULL)
        {
            DWORD Error = GetLastError();

            Result = Error == ERROR_SUCCESS ? E_FAIL : HRESULT_FROM_WIN32(Error);
        }
    }
    if (SUCCEEDED(Result))
    {
        if (WTSRegisterSessionNotificationEx(WTS_CURRENT_SERVER_HANDLE,
                                             Window,
                                             NOTIFY_FOR_ALL_SESSIONS))
        {
            NotificationsRegistered = TRUE;
        }
        else
        {
            Result = HRESULT_FROM_WIN32(GetLastError());
        }
    }
    if (SUCCEEDED(Result)) Result = AtlAxGetControl(Window, &Control);
    if (SUCCEEDED(Result)) Result = Control->QueryInterface(IID_PPV_ARGS(&Client));
    if (SUCCEEDED(Result)) Result = Control->QueryInterface(IID_PPV_ARGS(&NonScriptable));
    if (SUCCEEDED(Result)) Result = NonScriptable->put_PromptForCredentials(VARIANT_FALSE);
    if (SUCCEEDED(Result)) Result = NonScriptable->put_AllowPromptingForCredentials(VARIANT_FALSE);
    if (SUCCEEDED(Result)) Result = Control->QueryInterface(IID_PPV_ARGS(&ExtendedSettings));
    if (SUCCEEDED(Result))
    {
        Result = ZpRdpChildSession_SetExtendedBoolean(ExtendedSettings,
                                                      L"ConnectToChildSession",
                                                      VARIANT_TRUE);
    }
    if (SUCCEEDED(Result))
    {
        Result = ZpRdpChildSession_SetExtendedBoolean(ExtendedSettings,
                                                      L"EnableFrameBufferRedirection",
                                                      VARIANT_TRUE);
    }
    if (SUCCEEDED(Result)) Result = ZpRdpChildSession_SetString(Client, L"Server", L"localhost");
    if (SUCCEEDED(Result)) Result = ZpRdpChildSession_SetLong(Client, L"DesktopWidth", 800);
    if (SUCCEEDED(Result)) Result = ZpRdpChildSession_SetLong(Client, L"DesktopHeight", 600);
    if (SUCCEEDED(Result)) Result = ZpRdpChildSession_GetObject(Client, L"AdvancedSettings9", &Settings);
    if (SUCCEEDED(Result))
    {
        Result = ZpRdpChildSession_SetBoolean(Settings,
                                              L"EnableCredSspSupport",
                                              VARIANT_TRUE);
        if (SUCCEEDED(Result))
        {
            Result = ZpRdpChildSession_SetLong(Settings,
                                               L"AuthenticationLevel",
                                               0);
        }
        Settings->Release();
        Settings = NULL;
    }
    if (SUCCEEDED(Result))
    {
        Result = ZpRdpChildSession_Advise(Control,
                                          &Sink,
                                          &ConnectionPoint,
                                          &AdviseCookie);
    }
    if (SUCCEEDED(Result))
    {
        Result = ZpRdpChildSession_Invoke(Client, L"Connect");
        ConnectionStarted = SUCCEEDED(Result);
    }
    if (SUCCEEDED(Result))
    {
        InterlockedExchange(&ZpRdpChildSessionState,
                            ZP_RDP_CHILD_SESSION_STATE_CONNECTING);
    }
    Context->Status = ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result);
    SetEvent(Context->Event);
    if (SUCCEEDED(Result))
    {
        while (GetMessageW(&Message, NULL, 0, 0) > 0)
        {
            if (Message.message == WM_WTSSESSION_CHANGE &&
                Message.wParam == WTS_SESSION_DESKTOP_READY &&
                WorkerTimer == 0 &&
                InterlockedCompareExchange(&ZpRdpChildSessionState, 0, 0) ==
                    ZP_RDP_CHILD_SESSION_STATE_CONNECTING)
            {
                DWORD SessionId;

                if (ZpRdpChildSession_QueryId(&SessionId, NULL) == ERROR_SUCCESS &&
                    SessionId == (DWORD)Message.lParam)
                {
                    WorkerSessionId = SessionId;
                    WorkerTimer = SetTimer(NULL,
                                           0,
                                           ZP_RDP_CHILD_SESSION_WORKER_DELAY,
                                           NULL);
                    if (WorkerTimer == 0)
                    {
                        InterlockedExchange(&ZpRdpChildSessionError,
                                            (LONG)HRESULT_FROM_WIN32(GetLastError()));
                        break;
                    }
                }
            }
            else if (WorkerTimer != 0 &&
                     Message.message == WM_TIMER &&
                     Message.hwnd == NULL &&
                     Message.wParam == WorkerTimer)
            {
                KillTimer(NULL, WorkerTimer);
                WorkerTimer = 0;
                Result = ZpRdpChildSession_CreateWorker(WorkerSessionId);
                if (SUCCEEDED(Result))
                {
                    InterlockedExchange(&ZpRdpChildSessionState,
                                        ZP_RDP_CHILD_SESSION_STATE_ACTIVE);
                }
                else
                {
                    InterlockedExchange(&ZpRdpChildSessionError,
                                        (LONG)Result);
                    break;
                }
            }
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
    }
    if (Client != NULL) ZpRdpChildSession_Invoke(Client, L"Disconnect");
    if (WorkerTimer != 0) KillTimer(NULL, WorkerTimer);
    if (NotificationsRegistered)
    {
        WTSUnRegisterSessionNotificationEx(WTS_CURRENT_SERVER_HANDLE, Window);
    }
    if (ConnectionPoint != NULL)
    {
        if (AdviseCookie != 0) ConnectionPoint->Unadvise(AdviseCookie);
        ConnectionPoint->Release();
    }
    if (Window != NULL) DestroyWindow(Window);
    if (ExtendedSettings != NULL) ExtendedSettings->Release();
    if (NonScriptable != NULL) NonScriptable->Release();
    if (Client != NULL) Client->Release();
    if (Control != NULL) Control->Release();
    if (Atl != NULL) FreeLibrary(Atl);
    if (Initialized) CoUninitialize();
    if (ConnectionStarted) ZpRdpChildSession_Logoff();
    InterlockedExchange(&ZpRdpChildSessionState,
                        ZP_RDP_CHILD_SESSION_STATE_STOPPED);
    return 0;
}

static
VOID
ZpRdpChildSession_ReapThread(VOID)
{
    if (ZpRdpChildSessionThread != NULL &&
        WaitForSingleObject(ZpRdpChildSessionThread, 0) == WAIT_OBJECT_0)
    {
        CloseHandle(ZpRdpChildSessionThread);
        ZpRdpChildSessionThread = NULL;
        ZpRdpChildSessionThreadId = 0;
    }
}

ZP_STATUS
ZpRdpChildSession_Query(
    _Out_ PBOOLEAN Enabled,
    _Out_ PBOOLEAN CredentialDelegation,
    _Out_ PBOOLEAN HelloOnly,
    _Out_ PULONG State,
    _Out_ PULONG SessionId,
    _Out_ PULONG Error)
{
    WINSTATIONINFORMATION Information;
    BOOL ChildSessionsEnabled;
    DWORD Win32Error;
    NTSTATUS Status;

    if (!WTSIsChildSessionsEnabled(&ChildSessionsEnabled))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    *Enabled = !!ChildSessionsEnabled;
    Status = ZpRdpChildSession_QueryCredentialDelegation(CredentialDelegation);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Status = ZpRdpChildSession_QueryHelloOnly(HelloOnly);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    *SessionId = MAXULONG;
    *Error = (ULONG)InterlockedCompareExchange(&ZpRdpChildSessionError, 0, 0);
    Win32Error = ZpRdpChildSession_QueryId(SessionId, &Information);
    if (Win32Error == ERROR_SUCCESS)
    {
        *State = Information.ConnectState == State_Active ?
                     ZP_RDP_CHILD_SESSION_STATE_ACTIVE :
                     Information.ConnectState == State_Disconnected ?
                         ZP_RDP_CHILD_SESSION_STATE_DISCONNECTED :
                         ZP_RDP_CHILD_SESSION_STATE_CONNECTING;
        return ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }
    if (Win32Error != ERROR_NOT_FOUND)
    {
        return ZpStatus_FromCode(ZpStatusWin32, Win32Error);
    }
    *State = (ULONG)InterlockedCompareExchange(&ZpRdpChildSessionState, 0, 0);
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

ZP_STATUS
ZpRdpChildSession_SetEnabled(
    _In_ BOOLEAN Enabled)
{
    DWORD SessionId, Win32Error;
    ZP_STATUS Status;

    RtlAcquireSRWLockExclusive(&ZpRdpChildSessionLock);
    ZpRdpChildSession_ReapThread();
    if (!Enabled)
    {
        Win32Error = ZpRdpChildSession_QueryId(&SessionId, NULL);
        if (ZpRdpChildSessionThread != NULL ||
            InterlockedCompareExchange(&ZpRdpChildSessionState, 0, 0) !=
                ZP_RDP_CHILD_SESSION_STATE_STOPPED ||
            Win32Error == ERROR_SUCCESS)
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, ERROR_BUSY);
            goto Cleanup;
        }
        if (Win32Error != ERROR_NOT_FOUND)
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, Win32Error);
            goto Cleanup;
        }
    }
    if (!WTSEnableChildSessions(Enabled))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    InterlockedExchange(&ZpRdpChildSessionError, 0);
    Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);

Cleanup:
    RtlReleaseSRWLockExclusive(&ZpRdpChildSessionLock);
    return Status;
}

ZP_STATUS
ZpRdpChildSession_SetCredentialDelegation(
    _In_ BOOLEAN Enabled)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Key;
    NTSTATUS Status;

    if (Enabled)
    {
        InitializeObjectAttributes(&ObjectAttributes,
                                   (PUNICODE_STRING)&ZpRdpChildSessionCredentialKey,
                                   OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                   NULL,
                                   NULL);
        Status = NtCreateKey(&Key,
                             KEY_SET_VALUE | KEY_CREATE_SUB_KEY,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_NON_VOLATILE,
                             NULL);
    }
    else
    {
        Status = Sys_RegOpenKey(&Key,
                                KEY_ENUMERATE_SUB_KEYS,
                                &ZpRdpChildSessionCredentialKey);
        if (ZpRdpChildSession_IsRegistryValueMissing(Status))
        {
            return ZpStatus_FromNtStatus(STATUS_SUCCESS);
        }
    }
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Status = ZpRdpChildSession_SetCredentialPolicy(Key,
                                                   &ZpRdpChildSessionDefaultCredentialPolicy,
                                                   Enabled);
    if (NT_SUCCESS(Status))
    {
        Status = ZpRdpChildSession_SetCredentialPolicy(Key,
                                                       &ZpRdpChildSessionNtlmCredentialPolicy,
                                                       Enabled);
    }
    NtClose(Key);
    return ZpStatus_FromNtStatus(Status);
}

ZP_STATUS
ZpRdpChildSession_SetHelloOnly(
    _In_ BOOLEAN Enabled)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Key;
    DWORD Value = Enabled ? 2 : 0;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)&ZpRdpChildSessionPasswordlessKey,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtCreateKey(&Key,
                         KEY_SET_VALUE,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         NULL);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Status = NtSetValueKey(Key,
                           (PUNICODE_STRING)&ZpRdpChildSessionPasswordlessValue,
                           0,
                           REG_DWORD,
                           &Value,
                           sizeof(Value));
    NtClose(Key);
    return ZpStatus_FromNtStatus(Status);
}

ZP_STATUS
ZpRdpChildSession_Start(VOID)
{
    ZP_RDP_CHILD_SESSION_THREAD_CONTEXT Context;
    BOOL Enabled;
    BOOLEAN CredentialDelegation, HelloOnly;
    DWORD SessionId, Win32Error;
    NTSTATUS NtStatus;
    ZP_STATUS Status;

    Status = ZpRdpChildSession_ValidateParent();
    if (!ZpStatus_IsSuccess(Status)) return Status;
    NtStatus = ZpRdpChildSession_QueryCredentialDelegation(&CredentialDelegation);
    if (!NT_SUCCESS(NtStatus)) return ZpStatus_FromNtStatus(NtStatus);
    NtStatus = ZpRdpChildSession_QueryHelloOnly(&HelloOnly);
    if (!NT_SUCCESS(NtStatus)) return ZpStatus_FromNtStatus(NtStatus);
    if (!CredentialDelegation || HelloOnly)
    {
        return ZpStatus_FromCode(ZpStatusWin32, ERROR_LOGON_FAILURE);
    }
    RtlAcquireSRWLockExclusive(&ZpRdpChildSessionLock);
    ZpRdpChildSession_ReapThread();
    if (ZpRdpChildSessionThread != NULL)
    {
        RtlReleaseSRWLockExclusive(&ZpRdpChildSessionLock);
        return ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }
    if (!WTSIsChildSessionsEnabled(&Enabled))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        RtlReleaseSRWLockExclusive(&ZpRdpChildSessionLock);
        return Status;
    }
    if (!Enabled)
    {
        RtlReleaseSRWLockExclusive(&ZpRdpChildSessionLock);
        return ZpStatus_FromCode(ZpStatusWin32, ERROR_SERVICE_DISABLED);
    }
    Win32Error = ZpRdpChildSession_QueryId(&SessionId, NULL);
    if (Win32Error == ERROR_SUCCESS)
    {
        RtlReleaseSRWLockExclusive(&ZpRdpChildSessionLock);
        return ZpStatus_FromCode(ZpStatusWin32, ERROR_ALREADY_EXISTS);
    }
    if (Win32Error != ERROR_NOT_FOUND)
    {
        RtlReleaseSRWLockExclusive(&ZpRdpChildSessionLock);
        return ZpStatus_FromCode(ZpStatusWin32, Win32Error);
    }
    Context.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Context.Event == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        RtlReleaseSRWLockExclusive(&ZpRdpChildSessionLock);
        return Status;
    }
    InterlockedExchange(&ZpRdpChildSessionError, 0);
    InterlockedExchange(&ZpRdpChildSessionStopping, FALSE);
    InterlockedExchange(&ZpRdpChildSessionState,
                        ZP_RDP_CHILD_SESSION_STATE_STARTING);
    ZpRdpChildSessionThread = CreateThread(NULL,
                                          0,
                                          ZpRdpChildSession_Thread,
                                          &Context,
                                          0,
                                          &ZpRdpChildSessionThreadId);
    if (ZpRdpChildSessionThread == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        InterlockedExchange(&ZpRdpChildSessionState,
                            ZP_RDP_CHILD_SESSION_STATE_STOPPED);
    }
    else
    {
        WaitForSingleObject(Context.Event, INFINITE);
        Status = Context.Status;
        if (!ZpStatus_IsSuccess(Status))
        {
            WaitForSingleObject(ZpRdpChildSessionThread, INFINITE);
            ZpRdpChildSession_ReapThread();
        }
    }
    CloseHandle(Context.Event);
    RtlReleaseSRWLockExclusive(&ZpRdpChildSessionLock);
    return Status;
}

ZP_STATUS
ZpRdpChildSession_Stop(VOID)
{
    ZP_STATUS Status;
    DWORD WaitResult;
    BOOLEAN Hosted = FALSE;

    InterlockedExchange(&ZpRdpChildSessionStopping, TRUE);
    RtlAcquireSRWLockExclusive(&ZpRdpChildSessionLock);
    ZpRdpChildSession_ReapThread();
    if (ZpRdpChildSessionThread != NULL)
    {
        Hosted = TRUE;
        if (!PostThreadMessageW(ZpRdpChildSessionThreadId, WM_QUIT, 0, 0))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            RtlReleaseSRWLockExclusive(&ZpRdpChildSessionLock);
            return Status;
        }
        WaitResult = WaitForSingleObject(ZpRdpChildSessionThread,
                                         ZP_RDP_CHILD_SESSION_STOP_TIMEOUT);
        if (WaitResult != WAIT_OBJECT_0)
        {
            Status = WaitResult == WAIT_FAILED ?
                         ZpStatus_FromCode(ZpStatusWin32, GetLastError()) :
                         ZpStatus_FromCode(ZpStatusWin32, WAIT_TIMEOUT);
            RtlReleaseSRWLockExclusive(&ZpRdpChildSessionLock);
            return Status;
        }
        ZpRdpChildSession_ReapThread();
    }
    RtlReleaseSRWLockExclusive(&ZpRdpChildSessionLock);
    Status = Hosted ?
                 ZpStatus_FromNtStatus(STATUS_SUCCESS) :
                 ZpRdpChildSession_Logoff();
    if (ZpStatus_IsSuccess(Status))
    {
        InterlockedExchange(&ZpRdpChildSessionState,
                            ZP_RDP_CHILD_SESSION_STATE_STOPPED);
        InterlockedExchange(&ZpRdpChildSessionError, 0);
    }
    return Status;
}

VOID
ZpRdpChildSession_Close(VOID)
{
    BOOLEAN Hosted;

    RtlAcquireSRWLockExclusive(&ZpRdpChildSessionLock);
    ZpRdpChildSession_ReapThread();
    Hosted = ZpRdpChildSessionThread != NULL;
    RtlReleaseSRWLockExclusive(&ZpRdpChildSessionLock);
    if (Hosted) ZpRdpChildSession_Stop();
}
