#define COBJMACROS

#include "Client.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"

#define Network ZpAudioNetwork
#define PDEVCAPS ZP_AUDIO_PDEVCAPS
#include <audiopolicy.h>
#include <endpointvolume.h>
#undef PDEVCAPS
#undef Network
#include <mmdeviceapi.h>
#include <propvarutil.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#pragma comment(lib, "Ole32.lib")

typedef struct _ZP_AUDIO_DEVICE_BUILDER
{
    PZP_AUDIO_DEVICE Records;
    ULONG Count;
} ZP_AUDIO_DEVICE_BUILDER, *PZP_AUDIO_DEVICE_BUILDER;

typedef struct _ZP_AUDIO_SESSION_BUILDER
{
    PZP_AUDIO_SESSION Records;
    ULONG Count;
    ULONG Capacity;
} ZP_AUDIO_SESSION_BUILDER, *PZP_AUDIO_SESSION_BUILDER;

typedef struct _ZP_POLICY_CONFIG ZP_POLICY_CONFIG;

typedef struct _ZP_POLICY_CONFIG_VTBL
{
    BEGIN_INTERFACE

    HRESULT (STDMETHODCALLTYPE* QueryInterface)(ZP_POLICY_CONFIG* This, REFIID Riid, PVOID* Object);
    ULONG (STDMETHODCALLTYPE* AddRef)(ZP_POLICY_CONFIG* This);
    ULONG (STDMETHODCALLTYPE* Release)(ZP_POLICY_CONFIG* This);
    PVOID GetMixFormat;
    PVOID GetDeviceFormat;
    PVOID ResetDeviceFormat;
    PVOID SetDeviceFormat;
    PVOID GetProcessingPeriod;
    PVOID SetProcessingPeriod;
    PVOID GetShareMode;
    PVOID SetShareMode;
    PVOID GetPropertyValue;
    PVOID SetPropertyValue;
    HRESULT (STDMETHODCALLTYPE* SetDefaultEndpoint)(ZP_POLICY_CONFIG* This, PCWSTR DeviceId, ERole Role);
    HRESULT (STDMETHODCALLTYPE* SetEndpointVisibility)(ZP_POLICY_CONFIG* This, PCWSTR DeviceId, BOOL Visible);

    END_INTERFACE
} ZP_POLICY_CONFIG_VTBL;

struct _ZP_POLICY_CONFIG
{
    CONST_VTBL ZP_POLICY_CONFIG_VTBL* lpVtbl;
};

static CONST CLSID ZpPolicyConfigClass = { 0x870af99c, 0x171d, 0x4f9e,
                                            { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 } };
static CONST IID ZpPolicyConfigIid = { 0xf8679f50, 0x850a, 0x41cf,
                                       { 0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8 } };
static CONST CLSID ZpAudioDeviceEnumeratorClass = { 0xbcde0395, 0xe52f, 0x467c,
                                                     { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
static CONST IID ZpAudioDeviceEnumeratorIid = { 0xa95664d2, 0x9614, 0x4f35,
                                                { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };
static CONST IID ZpAudioEndpointIid = { 0x1be09788, 0x6894, 0x4089,
                                        { 0x85, 0x86, 0x9a, 0x2a, 0x6c, 0x26, 0x5a, 0xc5 } };
static CONST IID ZpAudioEndpointVolumeIid = { 0x5cdf2c82, 0x841e, 0x4546,
                                              { 0x97, 0x22, 0x0c, 0xf7, 0x40, 0x78, 0x22, 0x9a } };
static CONST IID ZpAudioSessionControl2Iid = { 0xbfb7ff88, 0x7239, 0x4fc9,
                                               { 0x8f, 0xa2, 0x07, 0xc9, 0x50, 0xbe, 0x9c, 0x6d } };
static CONST IID ZpAudioSessionManager2Iid = { 0x77aa99a0, 0x1bd6, 0x484f,
                                               { 0x8b, 0xc7, 0x2c, 0x65, 0x4c, 0x9a, 0x9b, 0x6f } };
static CONST IID ZpSimpleAudioVolumeIid = { 0x87ce5498, 0x68d6, 0x44e5,
                                            { 0x92, 0x15, 0x6d, 0xa4, 0x7e, 0xf8, 0x83, 0xd8 } };
static CONST PROPERTYKEY ZpAudioFriendlyNameKey = { { 0xa45c254e, 0xdf1c, 0x4efd,
                                                       { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } },
                                                     14 };

static
PWSTR
ZpAudio_CopyString(
    _In_reads_opt_(Length) PCWCH Value,
    _In_ ULONG Length)
{
    PWSTR Copy;

    Copy = Mem_Alloc(((SIZE_T)Length + 1) * sizeof(WCHAR));
    if (Copy == NULL) return NULL;
    if (Length != 0) RtlCopyMemory(Copy, Value, (SIZE_T)Length * sizeof(WCHAR));
    Copy[Length] = UNICODE_NULL;
    return Copy;
}

static
PWSTR
ZpAudio_CopyView(
    _In_ PCZP_STRING_VIEW View)
{
    PCWCH Value = (PCWCH)View->Buffer;
    ULONG Index;

    for (Index = 0; Index < View->Length; Index++)
    {
        if (Value[Index] == UNICODE_NULL) return NULL;
    }
    return ZpAudio_CopyString(Value, View->Length);
}

static
VOID
ZpAudio_FreeDevices(
    _Inout_ PZP_AUDIO_DEVICE_BUILDER Builder)
{
    ULONG Index;

    for (Index = 0; Index < Builder->Count; Index++)
    {
        Mem_Free((PVOID)Builder->Records[Index].Name);
        CoTaskMemFree((PVOID)Builder->Records[Index].Id);
    }
    Mem_Free(Builder->Records);
}

static
VOID
ZpAudio_FreeSessions(
    _Inout_ PZP_AUDIO_SESSION_BUILDER Builder)
{
    ULONG Index;

    for (Index = 0; Index < Builder->Count; Index++)
    {
        Mem_Free((PVOID)Builder->Records[Index].DeviceId);
        Mem_Free((PVOID)Builder->Records[Index].Id);
        Mem_Free((PVOID)Builder->Records[Index].Name);
    }
    Mem_Free(Builder->Records);
}

static
HRESULT
ZpAudio_CreateEnumerator(
    _Outptr_ IMMDeviceEnumerator** Enumerator)
{
    return CoCreateInstance(&ZpAudioDeviceEnumeratorClass,
                            NULL,
                            CLSCTX_INPROC_SERVER,
                            &ZpAudioDeviceEnumeratorIid,
                            (PVOID*)Enumerator);
}

static
HRESULT
ZpAudio_GetFriendlyName(
    _In_ IMMDevice* Device,
    _Outptr_ PWSTR* Name,
    _Out_ PULONG NameLength)
{
    IPropertyStore* Properties;
    PROPVARIANT Value;
    HRESULT Result;

    Result = IMMDevice_OpenPropertyStore(Device, STGM_READ, &Properties);
    if (FAILED(Result)) return Result;
    PropVariantInit(&Value);
    Result = IPropertyStore_GetValue(Properties, &ZpAudioFriendlyNameKey, &Value);
    if (SUCCEEDED(Result) && Value.vt != VT_LPWSTR) Result = E_UNEXPECTED;
    if (SUCCEEDED(Result))
    {
        *NameLength = Value.pwszVal == NULL ? 0 : (ULONG)wcslen(Value.pwszVal);
        *Name = ZpAudio_CopyString(Value.pwszVal, *NameLength);
        if (*Name == NULL) Result = E_OUTOFMEMORY;
    }
    PropVariantClear(&Value);
    IPropertyStore_Release(Properties);
    return Result;
}

static
VOID
ZpAudio_GetEndpointVolume(
    _In_ IMMDevice* Device,
    _Inout_ PZP_AUDIO_DEVICE Record)
{
    IAudioEndpointVolume* Volume;
    float Scalar;
    BOOL Muted;

    if (FAILED(IMMDevice_Activate(Device,
                                  &ZpAudioEndpointVolumeIid,
                                  CLSCTX_INPROC_SERVER,
                                  NULL,
                                  (PVOID*)&Volume)))
    {
        return;
    }
    if (SUCCEEDED(IAudioEndpointVolume_GetMasterVolumeLevelScalar(Volume, &Scalar)) &&
        SUCCEEDED(IAudioEndpointVolume_GetMute(Volume, &Muted)))
    {
        Record->Volume = (USHORT)(min(max(Scalar, 0.0f), 1.0f) * ZP_AUDIO_VOLUME_MAX + 0.5f);
        Record->Flags |= ZP_AUDIO_ENDPOINT_VOLUME_AVAILABLE;
        if (Muted) Record->Flags |= ZP_AUDIO_ENDPOINT_MUTED;
    }
    IAudioEndpointVolume_Release(Volume);
}

static
VOID
ZpAudio_GetDefaultIds(
    _In_ IMMDeviceEnumerator* Enumerator,
    _Out_writes_(6) PWSTR* Ids)
{
    EDataFlow Flows[] = { eRender, eCapture };
    ERole Roles[] = { eConsole, eMultimedia, eCommunications };
    ULONG FlowIndex, RoleIndex;

    RtlZeroMemory(Ids, 6 * sizeof(*Ids));
    for (FlowIndex = 0; FlowIndex < ARRAYSIZE(Flows); FlowIndex++)
    {
        for (RoleIndex = 0; RoleIndex < ARRAYSIZE(Roles); RoleIndex++)
        {
            IMMDevice* Device;

            if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator,
                                                                       Flows[FlowIndex],
                                                                       Roles[RoleIndex],
                                                                       &Device)))
            {
                IMMDevice_GetId(Device, &Ids[FlowIndex * ARRAYSIZE(Roles) + RoleIndex]);
                IMMDevice_Release(Device);
            }
        }
    }
}

static
VOID
ZpAudio_FreeDefaultIds(
    _In_reads_(6) PWSTR* Ids)
{
    ULONG Index;

    for (Index = 0; Index < 6; Index++) CoTaskMemFree(Ids[Index]);
}

static
VOID
ZpAudio_SetDefaultFlags(
    _In_reads_(6) PWSTR* Ids,
    _Inout_ PZP_AUDIO_DEVICE Record)
{
    ULONG Base = Record->Flow == ZpAudioFlowRender ? 0 : 3;

    if (Ids[Base] != NULL && !_wcsicmp(Ids[Base], Record->Id))
    {
        Record->Flags |= ZP_AUDIO_ENDPOINT_DEFAULT_CONSOLE;
    }
    if (Ids[Base + 1] != NULL && !_wcsicmp(Ids[Base + 1], Record->Id))
    {
        Record->Flags |= ZP_AUDIO_ENDPOINT_DEFAULT_MULTIMEDIA;
    }
    if (Ids[Base + 2] != NULL && !_wcsicmp(Ids[Base + 2], Record->Id))
    {
        Record->Flags |= ZP_AUDIO_ENDPOINT_DEFAULT_COMMUNICATIONS;
    }
}

static
HRESULT
ZpAudio_BuildDevices(
    _Out_ PZP_AUDIO_DEVICE_BUILDER Builder)
{
    IMMDeviceEnumerator* Enumerator;
    IMMDeviceCollection* Collection;
    PWSTR DefaultIds[6] = { 0 };
    UINT Count, Index;
    HRESULT Result;

    Result = ZpAudio_CreateEnumerator(&Enumerator);
    if (FAILED(Result)) return Result;
    Result = IMMDeviceEnumerator_EnumAudioEndpoints(Enumerator,
                                                    eAll,
                                                    DEVICE_STATEMASK_ALL,
                                                    &Collection);
    if (FAILED(Result))
    {
        IMMDeviceEnumerator_Release(Enumerator);
        return Result;
    }
    Result = IMMDeviceCollection_GetCount(Collection, &Count);
    if (SUCCEEDED(Result) && Count > ZP_AUDIO_MAX_DEVICES) Result = HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    Builder->Records = SUCCEEDED(Result) && Count != 0 ? Mem_Alloc(Count * sizeof(*Builder->Records)) : NULL;
    if (SUCCEEDED(Result) && Count != 0 && Builder->Records == NULL) Result = E_OUTOFMEMORY;
    if (SUCCEEDED(Result)) ZpAudio_GetDefaultIds(Enumerator, DefaultIds);
    for (Index = 0; SUCCEEDED(Result) && Index < Count; Index++)
    {
        IMMDevice* Device;
        IMMEndpoint* Endpoint = NULL;
        EDataFlow Flow;
        DWORD State;
        PZP_AUDIO_DEVICE Record = &Builder->Records[Index];

        RtlZeroMemory(Record, sizeof(*Record));
        Result = IMMDeviceCollection_Item(Collection, Index, &Device);
        if (FAILED(Result)) break;
        Result = IMMDevice_GetId(Device, (LPWSTR*)&Record->Id);
        if (SUCCEEDED(Result)) Record->IdLength = (ULONG)wcslen(Record->Id);
        if (SUCCEEDED(Result)) Result = IMMDevice_GetState(Device, &State);
        if (SUCCEEDED(Result)) Record->State = (BYTE)State;
        if (SUCCEEDED(Result)) Result = IMMDevice_QueryInterface(Device, &ZpAudioEndpointIid, (PVOID*)&Endpoint);
        if (SUCCEEDED(Result))
        {
            Result = IMMEndpoint_GetDataFlow(Endpoint, &Flow);
            IMMEndpoint_Release(Endpoint);
        }
        if (SUCCEEDED(Result))
        {
            Record->Flow = Flow == eRender ? ZpAudioFlowRender :
                           Flow == eCapture ? ZpAudioFlowCapture : 0;
            if (Record->Flow == 0) Result = E_UNEXPECTED;
        }
        if (SUCCEEDED(Result)) Result = ZpAudio_GetFriendlyName(Device, (PWSTR*)&Record->Name, &Record->NameLength);
        if (SUCCEEDED(Result))
        {
            ZpAudio_GetEndpointVolume(Device, Record);
            ZpAudio_SetDefaultFlags(DefaultIds, Record);
            Builder->Count++;
        }
        if (FAILED(Result))
        {
            Mem_Free((PVOID)Record->Name);
            CoTaskMemFree((PVOID)Record->Id);
        }
        IMMDevice_Release(Device);
    }
    ZpAudio_FreeDefaultIds(DefaultIds);
    IMMDeviceCollection_Release(Collection);
    IMMDeviceEnumerator_Release(Enumerator);
    return Result;
}

static
HRESULT
ZpAudio_AddSession(
    _Inout_ PZP_AUDIO_SESSION_BUILDER Builder,
    _In_ PCWSTR DeviceId,
    _In_ IAudioSessionControl* Control)
{
    IAudioSessionControl2* Control2;
    ISimpleAudioVolume* Volume = NULL;
    LPWSTR Id = NULL, Name = NULL;
    AudioSessionState State;
    float Scalar;
    BOOL Muted;
    ULONG ProcessId;
    PZP_AUDIO_SESSION Records, Record = NULL;
    HRESULT Result;

    Result = IAudioSessionControl_QueryInterface(Control, &ZpAudioSessionControl2Iid, (PVOID*)&Control2);
    if (FAILED(Result)) return Result;
    Result = IAudioSessionControl_QueryInterface(Control, &ZpSimpleAudioVolumeIid, (PVOID*)&Volume);
    if (SUCCEEDED(Result)) Result = IAudioSessionControl2_GetSessionInstanceIdentifier(Control2, &Id);
    if (SUCCEEDED(Result)) Result = IAudioSessionControl_GetDisplayName(Control, &Name);
    if (SUCCEEDED(Result)) Result = IAudioSessionControl2_GetProcessId(Control2, &ProcessId);
    if (SUCCEEDED(Result)) Result = IAudioSessionControl_GetState(Control, &State);
    if (SUCCEEDED(Result)) Result = ISimpleAudioVolume_GetMasterVolume(Volume, &Scalar);
    if (SUCCEEDED(Result)) Result = ISimpleAudioVolume_GetMute(Volume, &Muted);
    if (SUCCEEDED(Result) && Builder->Count == ZP_AUDIO_MAX_SESSIONS)
    {
        Result = HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    }
    if (SUCCEEDED(Result) && Builder->Count == Builder->Capacity)
    {
        ULONG Capacity = Builder->Capacity == 0 ? 32 : min(Builder->Capacity * 2, ZP_AUDIO_MAX_SESSIONS);

        Records = Mem_ReAlloc(Builder->Records, (SIZE_T)Capacity * sizeof(*Records));
        if (Records == NULL) Result = E_OUTOFMEMORY;
        else
        {
            Builder->Records = Records;
            Builder->Capacity = Capacity;
        }
    }
    if (SUCCEEDED(Result))
    {
        Record = &Builder->Records[Builder->Count];
        RtlZeroMemory(Record, sizeof(*Record));
        Record->DeviceIdLength = (ULONG)wcslen(DeviceId);
        Record->IdLength = Id == NULL ? 0 : (ULONG)wcslen(Id);
        Record->NameLength = Name == NULL ? 0 : (ULONG)wcslen(Name);
        if (Record->DeviceIdLength > ZP_AUDIO_MAX_ID_LENGTH || Record->IdLength == 0 ||
            Record->IdLength > ZP_AUDIO_MAX_ID_LENGTH || Record->NameLength > ZP_AUDIO_MAX_NAME_LENGTH)
        {
            Result = E_INVALIDARG;
        }
    }
    if (SUCCEEDED(Result))
    {
        Record->DeviceId = ZpAudio_CopyString(DeviceId, Record->DeviceIdLength);
        Record->Id = ZpAudio_CopyString(Id, Record->IdLength);
        Record->Name = ZpAudio_CopyString(Name, Record->NameLength);
        if (Record->DeviceId == NULL || Record->Id == NULL || Record->Name == NULL)
        {
            Mem_Free((PVOID)Record->DeviceId);
            Mem_Free((PVOID)Record->Id);
            Mem_Free((PVOID)Record->Name);
            Result = E_OUTOFMEMORY;
        }
    }
    if (SUCCEEDED(Result))
    {
        Record->ProcessId = ProcessId;
        Record->State = (BYTE)State;
        Record->Volume = (USHORT)(min(max(Scalar, 0.0f), 1.0f) * ZP_AUDIO_VOLUME_MAX + 0.5f);
        if (Muted) Record->Flags |= ZP_AUDIO_SESSION_MUTED;
        if (IAudioSessionControl2_IsSystemSoundsSession(Control2) == S_OK)
        {
            Record->Flags |= ZP_AUDIO_SESSION_SYSTEM_SOUNDS;
        }
        Builder->Count++;
    }
    CoTaskMemFree(Name);
    CoTaskMemFree(Id);
    if (Volume != NULL) ISimpleAudioVolume_Release(Volume);
    IAudioSessionControl2_Release(Control2);
    return Result;
}

static
HRESULT
ZpAudio_AddDeviceSessions(
    _Inout_ PZP_AUDIO_SESSION_BUILDER Builder,
    _In_ IMMDevice* Device)
{
    IAudioSessionManager2* Manager = NULL;
    IAudioSessionEnumerator* Enumerator = NULL;
    LPWSTR DeviceId = NULL;
    int Count = 0, Index;
    HRESULT Result;

    Result = IMMDevice_GetId(Device, &DeviceId);
    if (SUCCEEDED(Result)) Result = IMMDevice_Activate(Device,
                                                       &ZpAudioSessionManager2Iid,
                                                       CLSCTX_INPROC_SERVER,
                                                       NULL,
                                                       (PVOID*)&Manager);
    if (SUCCEEDED(Result)) Result = IAudioSessionManager2_GetSessionEnumerator(Manager, &Enumerator);
    if (SUCCEEDED(Result)) Result = IAudioSessionEnumerator_GetCount(Enumerator, &Count);
    for (Index = 0; SUCCEEDED(Result) && Index < Count; Index++)
    {
        IAudioSessionControl* Control;

        Result = IAudioSessionEnumerator_GetSession(Enumerator, Index, &Control);
        if (SUCCEEDED(Result))
        {
            HRESULT SessionResult = ZpAudio_AddSession(Builder, DeviceId, Control);

            IAudioSessionControl_Release(Control);
            if (FAILED(SessionResult) && SessionResult != AUDCLNT_E_DEVICE_INVALIDATED) Result = SessionResult;
        }
    }
    if (Enumerator != NULL) IAudioSessionEnumerator_Release(Enumerator);
    if (Manager != NULL) IAudioSessionManager2_Release(Manager);
    CoTaskMemFree(DeviceId);
    return Result;
}

static
HRESULT
ZpAudio_BuildSessions(
    _Out_ PZP_AUDIO_SESSION_BUILDER Builder)
{
    IMMDeviceEnumerator* Enumerator = NULL;
    IMMDeviceCollection* Collection = NULL;
    UINT Count = 0, Index;
    HRESULT Result;

    Result = ZpAudio_CreateEnumerator(&Enumerator);
    if (SUCCEEDED(Result)) Result = IMMDeviceEnumerator_EnumAudioEndpoints(Enumerator,
                                                                           eRender,
                                                                           DEVICE_STATE_ACTIVE,
                                                                           &Collection);
    if (SUCCEEDED(Result)) Result = IMMDeviceCollection_GetCount(Collection, &Count);
    for (Index = 0; SUCCEEDED(Result) && Index < Count; Index++)
    {
        IMMDevice* Device;

        Result = IMMDeviceCollection_Item(Collection, Index, &Device);
        if (SUCCEEDED(Result))
        {
            Result = ZpAudio_AddDeviceSessions(Builder, Device);
            IMMDevice_Release(Device);
        }
    }
    if (Collection != NULL) IMMDeviceCollection_Release(Collection);
    if (Enumerator != NULL) IMMDeviceEnumerator_Release(Enumerator);
    return Result;
}

static
NTSTATUS
ZpAudio_EncodeDevices(
    _In_ PZP_AUDIO_DEVICE_BUILDER Builder,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    NTSTATUS Status;

    Status = ZpAudio_EncodeDeviceList(Builder->Records, Builder->Count, NULL, 0, ResponseLength);
    if (!NT_SUCCESS(Status)) return Status;
    *Response = Mem_Alloc(*ResponseLength);
    if (*Response == NULL) return STATUS_NO_MEMORY;
    Status = ZpAudio_EncodeDeviceList(Builder->Records,
                                      Builder->Count,
                                      *Response,
                                      *ResponseLength,
                                      ResponseLength);
    return Status;
}

static
NTSTATUS
ZpAudio_EncodeSessions(
    _In_ PZP_AUDIO_SESSION_BUILDER Builder,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    NTSTATUS Status;

    Status = ZpAudio_EncodeSessionList(Builder->Records, Builder->Count, NULL, 0, ResponseLength);
    if (!NT_SUCCESS(Status)) return Status;
    *Response = Mem_Alloc(*ResponseLength);
    if (*Response == NULL) return STATUS_NO_MEMORY;
    Status = ZpAudio_EncodeSessionList(Builder->Records,
                                       Builder->Count,
                                       *Response,
                                       *ResponseLength,
                                       ResponseLength);
    return Status;
}

static
ZP_STATUS
ZpAudio_EnumerateDevices(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_AUDIO_DEVICE_BUILDER Builder = { 0 };
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL Uninitialize;

    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Uninitialize = SUCCEEDED(Result);
    if (Result == RPC_E_CHANGED_MODE) Result = S_OK;
    if (SUCCEEDED(Result)) Result = ZpAudio_BuildDevices(&Builder);
    if (SUCCEEDED(Result)) Status = ZpAudio_EncodeDevices(&Builder, Response, ResponseLength);
    ZpAudio_FreeDevices(&Builder);
    if (Uninitialize) CoUninitialize();
    return !NT_SUCCESS(Status) ? ZpStatus_FromNtStatus(Status) :
           FAILED(Result) ? ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result) : ZpStatus_Make(ZpStatusNone, 0);
}

static
ZP_STATUS
ZpAudio_EnumerateSessions(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_AUDIO_SESSION_BUILDER Builder = { 0 };
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL Uninitialize;

    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Uninitialize = SUCCEEDED(Result);
    if (Result == RPC_E_CHANGED_MODE) Result = S_OK;
    if (SUCCEEDED(Result)) Result = ZpAudio_BuildSessions(&Builder);
    if (SUCCEEDED(Result)) Status = ZpAudio_EncodeSessions(&Builder, Response, ResponseLength);
    ZpAudio_FreeSessions(&Builder);
    if (Uninitialize) CoUninitialize();
    return !NT_SUCCESS(Status) ? ZpStatus_FromNtStatus(Status) :
           FAILED(Result) ? ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result) : ZpStatus_Make(ZpStatusNone, 0);
}

static
HRESULT
ZpAudio_ValidateDeviceFlow(
    _In_ IMMDevice* Device,
    _In_ ZP_AUDIO_FLOW ExpectedFlow)
{
    IMMEndpoint* Endpoint;
    EDataFlow Flow;
    HRESULT Result;

    Result = IMMDevice_QueryInterface(Device, &ZpAudioEndpointIid, (PVOID*)&Endpoint);
    if (SUCCEEDED(Result))
    {
        Result = IMMEndpoint_GetDataFlow(Endpoint, &Flow);
        IMMEndpoint_Release(Endpoint);
    }
    if (SUCCEEDED(Result) && ((Flow == eRender) != (ExpectedFlow == ZpAudioFlowRender))) Result = E_INVALIDARG;
    return Result;
}

static
HRESULT
ZpAudio_ControlEndpoint(
    _In_ PZP_AUDIO_ENDPOINT_CONTROL_VIEW Request)
{
    IMMDeviceEnumerator* Enumerator = NULL;
    IMMDevice* Device = NULL;
    IAudioEndpointVolume* Volume = NULL;
    ZP_POLICY_CONFIG* Policy = NULL;
    PWSTR DeviceId;
    HRESULT Result;

    DeviceId = ZpAudio_CopyView(&Request->DeviceId);
    if (DeviceId == NULL) return E_INVALIDARG;
    Result = ZpAudio_CreateEnumerator(&Enumerator);
    if (SUCCEEDED(Result)) Result = IMMDeviceEnumerator_GetDevice(Enumerator, DeviceId, &Device);
    if (SUCCEEDED(Result)) Result = ZpAudio_ValidateDeviceFlow(Device, Request->Flow);
    if (SUCCEEDED(Result) && (Request->Control == ZpAudioEndpointSetVolume ||
                              Request->Control == ZpAudioEndpointSetMute))
    {
        Result = IMMDevice_Activate(Device,
                                    &ZpAudioEndpointVolumeIid,
                                    CLSCTX_INPROC_SERVER,
                                    NULL,
                                    (PVOID*)&Volume);
        if (SUCCEEDED(Result))
        {
            Result = Request->Control == ZpAudioEndpointSetVolume ?
                         IAudioEndpointVolume_SetMasterVolumeLevelScalar(
                             Volume,
                             Request->Value / (float)ZP_AUDIO_VOLUME_MAX,
                             NULL) :
                         IAudioEndpointVolume_SetMute(Volume, Request->Value != 0, NULL);
            IAudioEndpointVolume_Release(Volume);
        }
    }
    else if (SUCCEEDED(Result))
    {
        Result = CoCreateInstance(&ZpPolicyConfigClass,
                                  NULL,
                                  CLSCTX_INPROC_SERVER,
                                  &ZpPolicyConfigIid,
                                  (PVOID*)&Policy);
        if (SUCCEEDED(Result) && Request->Control == ZpAudioEndpointSetDefault)
        {
            Result = Policy->lpVtbl->SetDefaultEndpoint(Policy, DeviceId, eConsole);
            if (SUCCEEDED(Result)) Result = Policy->lpVtbl->SetDefaultEndpoint(Policy, DeviceId, eMultimedia);
            if (SUCCEEDED(Result)) Result = Policy->lpVtbl->SetDefaultEndpoint(Policy, DeviceId, eCommunications);
        }
        else if (SUCCEEDED(Result))
        {
            Result = Policy->lpVtbl->SetEndpointVisibility(Policy, DeviceId, Request->Value != 0);
        }
        if (Policy != NULL) Policy->lpVtbl->Release(Policy);
    }
    if (Device != NULL) IMMDevice_Release(Device);
    if (Enumerator != NULL) IMMDeviceEnumerator_Release(Enumerator);
    Mem_Free(DeviceId);
    return Result;
}

static
HRESULT
ZpAudio_ControlSession(
    _In_ PZP_AUDIO_SESSION_CONTROL_VIEW Request)
{
    IMMDeviceEnumerator* DeviceEnumerator = NULL;
    IMMDevice* Device = NULL;
    IAudioSessionManager2* Manager = NULL;
    IAudioSessionEnumerator* SessionEnumerator = NULL;
    PWSTR DeviceId, SessionId;
    int Count = 0, Index;
    HRESULT Result;

    DeviceId = ZpAudio_CopyView(&Request->DeviceId);
    SessionId = ZpAudio_CopyView(&Request->SessionId);
    if (DeviceId == NULL || SessionId == NULL)
    {
        Mem_Free(SessionId);
        Mem_Free(DeviceId);
        return E_INVALIDARG;
    }
    Result = ZpAudio_CreateEnumerator(&DeviceEnumerator);
    if (SUCCEEDED(Result)) Result = IMMDeviceEnumerator_GetDevice(DeviceEnumerator, DeviceId, &Device);
    if (SUCCEEDED(Result)) Result = IMMDevice_Activate(Device,
                                                       &ZpAudioSessionManager2Iid,
                                                       CLSCTX_INPROC_SERVER,
                                                       NULL,
                                                       (PVOID*)&Manager);
    if (SUCCEEDED(Result)) Result = IAudioSessionManager2_GetSessionEnumerator(Manager, &SessionEnumerator);
    if (SUCCEEDED(Result)) Result = IAudioSessionEnumerator_GetCount(SessionEnumerator, &Count);
    if (SUCCEEDED(Result)) Result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    for (Index = 0; Result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && Index < Count; Index++)
    {
        IAudioSessionControl* Control;
        IAudioSessionControl2* Control2;
        LPWSTR Id = NULL;

        if (FAILED(IAudioSessionEnumerator_GetSession(SessionEnumerator, Index, &Control))) continue;
        if (FAILED(IAudioSessionControl_QueryInterface(Control, &ZpAudioSessionControl2Iid, (PVOID*)&Control2)))
        {
            IAudioSessionControl_Release(Control);
            continue;
        }
        if (SUCCEEDED(IAudioSessionControl2_GetSessionInstanceIdentifier(Control2, &Id)) &&
            !_wcsicmp(Id, SessionId))
        {
            ISimpleAudioVolume* Volume;

            Result = IAudioSessionControl_QueryInterface(Control, &ZpSimpleAudioVolumeIid, (PVOID*)&Volume);
            if (SUCCEEDED(Result))
            {
                Result = Request->Control == ZpAudioSessionSetVolume ?
                             ISimpleAudioVolume_SetMasterVolume(
                                 Volume,
                                 Request->Value / (float)ZP_AUDIO_VOLUME_MAX,
                                 NULL) :
                             ISimpleAudioVolume_SetMute(Volume, Request->Value != 0, NULL);
                ISimpleAudioVolume_Release(Volume);
            }
        }
        CoTaskMemFree(Id);
        IAudioSessionControl2_Release(Control2);
        IAudioSessionControl_Release(Control);
        if (Result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) break;
    }
    if (SessionEnumerator != NULL) IAudioSessionEnumerator_Release(SessionEnumerator);
    if (Manager != NULL) IAudioSessionManager2_Release(Manager);
    if (Device != NULL) IMMDevice_Release(Device);
    if (DeviceEnumerator != NULL) IMMDeviceEnumerator_Release(DeviceEnumerator);
    Mem_Free(SessionId);
    Mem_Free(DeviceId);
    return Result;
}

static
ZP_STATUS
ZpAudio_RunControl(
    _In_ LOGICAL Session,
    _In_ PVOID Request)
{
    HRESULT Result;
    LOGICAL Uninitialize;

    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Uninitialize = SUCCEEDED(Result);
    if (Result == RPC_E_CHANGED_MODE) Result = S_OK;
    if (SUCCEEDED(Result))
    {
        Result = Session ? ZpAudio_ControlSession(Request) : ZpAudio_ControlEndpoint(Request);
    }
    if (Uninitialize) CoUninitialize();
    return SUCCEEDED(Result) ? ZpStatus_Make(ZpStatusNone, 0) :
                               ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result);
}

ZP_STATUS
ZpAudio_Execute(
    _Inout_ struct _ZP_CLIENT_OBJECT* Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_LOCAL_CHANNEL* Channel)
{
    ZP_AUDIO_ENDPOINT_CONTROL_VIEW EndpointControl;
    ZP_AUDIO_SESSION_CONTROL_VIEW SessionControl;
    ZP_AUDIO_STREAM_REQUEST_VIEW StreamRequest;
    PZP_CLIENT_AUDIO_CHANNEL AudioChannel;
    NTSTATUS Status;

    if (OperationId == ZP_AUDIO_OPERATION_ENUMERATE_DEVICES)
    {
        return RequestLength == 0 ? ZpAudio_EnumerateDevices(Response, ResponseLength) :
                                    ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    else if (OperationId == ZP_AUDIO_OPERATION_ENUMERATE_SESSIONS)
    {
        return RequestLength == 0 ? ZpAudio_EnumerateSessions(Response, ResponseLength) :
                                    ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    else if (OperationId == ZP_AUDIO_OPERATION_CONTROL_ENDPOINT)
    {
        Status = ZpAudio_DecodeEndpointControl(Request, RequestLength, &EndpointControl);
        return NT_SUCCESS(Status) ? ZpAudio_RunControl(FALSE, &EndpointControl) : ZpStatus_FromNtStatus(Status);
    }
    else if (OperationId == ZP_AUDIO_OPERATION_CONTROL_SESSION)
    {
        Status = ZpAudio_DecodeSessionControl(Request, RequestLength, &SessionControl);
        return NT_SUCCESS(Status) ? ZpAudio_RunControl(TRUE, &SessionControl) : ZpStatus_FromNtStatus(Status);
    }
    else if (OperationId != ZP_AUDIO_OPERATION_OPEN_STREAM)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Status = ZpAudio_DecodeStreamRequest(Request, RequestLength, &StreamRequest);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Status = ZpAudio_CreateStreamChannel(Client, &StreamRequest, &AudioChannel);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    *ResponseLength = sizeof(ULONGLONG);
    *Response = Mem_Alloc(*ResponseLength);
    Status = *Response == NULL ? STATUS_NO_MEMORY :
                 ZpAudio_EncodeChannel(((PZP_CLIENT_LOCAL_CHANNEL)AudioChannel)->ChannelId,
                                       *Response,
                                       *ResponseLength,
                                       ResponseLength);
    if (!NT_SUCCESS(Status))
    {
        ZpAudio_CommitChannel((PZP_CLIENT_LOCAL_CHANNEL)AudioChannel, FALSE);
        return ZpStatus_FromNtStatus(Status);
    }
    *Channel = (PZP_CLIENT_LOCAL_CHANNEL)AudioChannel;
    return ZpStatus_Make(ZpStatusNone, 0);
}
