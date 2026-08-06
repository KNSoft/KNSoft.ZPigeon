#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#include "ShadowCopy.h"

#include <srrestoreptapi.h>
#include <vss.h>
#include <vswriter.h>
#include <vsmgmt.h>
#include <vsbackup.h>
#include <wrl/client.h>

#pragma comment(lib, "SrClient.lib")
#pragma comment(lib, "VssApi.lib")

EXTERN_C DWORD WINAPI DisableSRInternal(_In_opt_ PCWSTR drive, _In_ BOOL wait);
EXTERN_C DWORD WINAPI EnableSRInternal(_In_opt_ PCWSTR drive, _In_ BOOL wait);

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr GUID SystemProvider =
        { 0xB5946137, 0x7B9F, 0x4925, { 0xAF, 0x80, 0x51, 0xAB, 0xD6, 0x0B, 0x20, 0xD5 } };
    constexpr GUID RestoreControlClass =
        { 0x883FF1FC, 0x09E1, 0x48E5, { 0x8E, 0x54, 0xE2, 0x46, 0x9A, 0xCB, 0x0C, 0xFD } };
    constexpr GUID SppClass =
        { 0x4B966436, 0x6781, 0x4906, { 0x80, 0x35, 0x9A, 0xF9, 0x4B, 0x32, 0xC3, 0xF7 } };
    constexpr GUID SystemRestoreClient =
        { 0x09F7EDC5, 0x294E, 0x4180, { 0xAF, 0x6A, 0xFB, 0x0E, 0x6A, 0x0E, 0x95, 0x13 } };

    struct RestorePointProperty
    {
        BYTE Identifier[16];
        FILETIME CreationTime;
        ULONG SequenceNumber;
        ULONG Reserved1;
        ULONG Type;
        ULONG Reserved2;
        PWSTR Description;
        BYTE Reserved3[0x70];
    };

    static_assert(sizeof(RestorePointProperty) == 0xA0);
    static_assert(offsetof(RestorePointProperty, CreationTime) == 0x10);
    static_assert(offsetof(RestorePointProperty, SequenceNumber) == 0x18);
    static_assert(offsetof(RestorePointProperty, Type) == 0x20);
    static_assert(offsetof(RestorePointProperty, Description) == 0x28);

    struct SppClientProperty
    {
        GUID ClientId;
        ULONG State;
        ULONG Flags;
        ULONG VolumeCount;
        ULONG Reserved;
        PWSTR* Volumes;
        PWSTR* DiffAreaVolumes;
    };

    static_assert(sizeof(SppClientProperty) == 0x30);
    static_assert(offsetof(SppClientProperty, VolumeCount) == 0x18);
    static_assert(offsetof(SppClientProperty, Volumes) == 0x20);

    MIDL_INTERFACE("ADCF3F49-521F-48A6-BABC-8E20D5D3E861")
    ISppManagement : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE ReservedMethod3() = 0;
        virtual HRESULT STDMETHODCALLTYPE ReservedMethod4() = 0;
        virtual HRESULT STDMETHODCALLTYPE ReservedMethod5() = 0;
        virtual HRESULT STDMETHODCALLTYPE ReservedMethod6() = 0;
        virtual HRESULT STDMETHODCALLTYPE ReservedMethod7() = 0;
        virtual HRESULT STDMETHODCALLTYPE ReservedMethod8() = 0;
        virtual HRESULT STDMETHODCALLTYPE ReservedMethod9() = 0;
        virtual HRESULT STDMETHODCALLTYPE ReservedMethod10() = 0;
        virtual HRESULT STDMETHODCALLTYPE SetClient() = 0;
        virtual HRESULT STDMETHODCALLTYPE GetClients(
            _In_ ULONG flags,
            _Out_ PULONG count,
            _Outptr_result_buffer_(*count) SppClientProperty** properties) = 0;
    };

    MIDL_INTERFACE("B653F1E0-17D7-4AC6-9B18-F84B61DBC1A2")
    IRestoreControl : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE GetRestorePointList(
            _Out_ PULONG count,
            _Outptr_result_buffer_(*count) RestorePointProperty** properties) = 0;
        virtual HRESULT STDMETHODCALLTYPE ReservedMethod4() = 0;
        virtual HRESULT STDMETHODCALLTYPE ReservedMethod5() = 0;
        virtual HRESULT STDMETHODCALLTYPE ReservedMethod6() = 0;
        virtual HRESULT STDMETHODCALLTYPE Restore(
            _In_ RestorePointProperty* property,
            _In_ ULONG volumeCount,
            _In_opt_ PVOID volumeInformation) = 0;
    };

    using FreeRestorePointProperties = void (WINAPI*)(
        _In_ ULONG count,
        _Inout_ RestorePointProperty** properties);
    using FreeSppClientProperties = void (WINAPI*)(
        _In_ ULONG count,
        _Inout_ SppClientProperty** properties);

    struct Builder
    {
        PBYTE Buffer;
        ULONG Length;
        ULONG Capacity;
        ULONG Count;
    };

    struct DiffArea
    {
        bool Exists;
        LONGLONG MaximumSpace;
        LONGLONG AllocatedSpace;
        LONGLONG UsedSpace;
        WCHAR Volume[64];
    };

    class ComScope
    {
    public:
        ComScope() noexcept : Result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), Uninitialize(SUCCEEDED(Result))
        {
            if (Result == RPC_E_CHANGED_MODE) Result = S_OK;
        }

        ~ComScope()
        {
            if (Uninitialize) CoUninitialize();
        }

        HRESULT Result;

    private:
        bool Uninitialize;
    };

    NTSTATUS ReserveBuilder(Builder* builder, ULONG additionalLength)
    {
        PBYTE buffer;
        ULONG capacity, requiredLength;

        if (additionalLength > ZP_RESPONSE_MAX_PAYLOAD_SIZE - builder->Length) return STATUS_BUFFER_OVERFLOW;
        requiredLength = builder->Length + additionalLength;
        if (requiredLength <= builder->Capacity) return STATUS_SUCCESS;
        capacity = builder->Capacity == 0 ? 1024 : builder->Capacity;
        while (capacity < requiredLength) capacity = min(capacity * 2, ZP_RESPONSE_MAX_PAYLOAD_SIZE);
        buffer = static_cast<PBYTE>(Mem_ReAlloc(builder->Buffer, capacity));
        if (buffer == nullptr) return STATUS_NO_MEMORY;
        builder->Buffer = buffer;
        builder->Capacity = capacity;
        return STATUS_SUCCESS;
    }

    NTSTATUS AddRecord(
        Builder* builder,
        ZP_ADMINISTRATION_KIND kind,
        ULONG state,
        ULONG flags,
        ULONGLONG value,
        PCWSTR identity,
        PCWSTR name,
        PCWSTR description,
        PCWSTR detail)
    {
        ZP_ADMINISTRATION_RECORD record;
        ULONG recordLength;
        NTSTATUS status;

        if (builder->Count == ZP_CODEC_MAX_ELEMENT_COUNT) return STATUS_QUOTA_EXCEEDED;
        record.Kind = kind;
        record.State = state;
        record.Flags = flags;
        record.Value = value;
        record.Identity = identity;
        record.IdentityLength = static_cast<ULONG>(wcslen(identity));
        record.Name = name;
        record.NameLength = name == nullptr ? 0 : static_cast<ULONG>(wcslen(name));
        record.Description = description;
        record.DescriptionLength = description == nullptr ? 0 : static_cast<ULONG>(wcslen(description));
        record.Detail = detail;
        record.DetailLength = detail == nullptr ? 0 : static_cast<ULONG>(wcslen(detail));
        record.Data = nullptr;
        record.DataLength = 0;
        status = ZpAdministration_EncodeRecord(&record, nullptr, 0, &recordLength);
        if (!NT_SUCCESS(status)) return status;
        if (builder->Length == 0) builder->Length = sizeof(ULONG);
        status = ReserveBuilder(builder, recordLength);
        if (!NT_SUCCESS(status)) return status;
        status = ZpAdministration_EncodeRecord(&record,
                                                builder->Buffer + builder->Length,
                                                builder->Capacity - builder->Length,
                                                &recordLength);
        if (NT_SUCCESS(status))
        {
            builder->Length += recordLength;
            builder->Count++;
        }
        return status;
    }

    NTSTATUS EncodeBuilder(Builder* builder, PBYTE* response, PULONG responseLength)
    {
        ZP_CODEC_WRITER writer;
        NTSTATUS status;

        if (builder->Length == 0)
        {
            status = ReserveBuilder(builder, sizeof(ULONG));
            if (!NT_SUCCESS(status)) return status;
            builder->Length = sizeof(ULONG);
        }
        ZpCodec_InitializeWriter(&writer, builder->Buffer, builder->Length);
        status = ZpCodec_WriteUInt32(&writer, builder->Count);
        if (!NT_SUCCESS(status)) return status;
        *response = builder->Buffer;
        *responseLength = builder->Length;
        builder->Buffer = nullptr;
        return STATUS_SUCCESS;
    }

    PWSTR CopyString(PCZP_STRING_VIEW view)
    {
        auto string = static_cast<PWSTR>(Mem_Alloc((static_cast<SIZE_T>(view->Length) + 1) * sizeof(WCHAR)));

        if (string != nullptr)
        {
            RtlCopyMemory(string, view->Buffer, static_cast<SIZE_T>(view->Length) * sizeof(WCHAR));
            string[view->Length] = UNICODE_NULL;
        }
        return string;
    }

    HRESULT InitializeComSecurity()
    {
        HRESULT result = CoInitializeSecurity(nullptr,
                                              -1,
                                              nullptr,
                                              nullptr,
                                              RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                                              RPC_C_IMP_LEVEL_IMPERSONATE,
                                              nullptr,
                                              EOAC_NONE,
                                              nullptr);

        return result == RPC_E_TOO_LATE ? S_OK : result;
    }

    HRESULT OpenSnapshotComponents(LONG context, ComPtr<IVssBackupComponents>* components)
    {
        HRESULT result = InitializeComSecurity();

        if (SUCCEEDED(result)) result = CreateVssBackupComponents(components->ReleaseAndGetAddressOf());
        if (SUCCEEDED(result)) result = (*components)->InitializeForBackup();
        if (SUCCEEDED(result)) result = (*components)->SetContext(context);
        return result;
    }

    HRESULT OpenDiffAreaManagement(ComPtr<IVssDifferentialSoftwareSnapshotMgmt>* management)
    {
        ComPtr<IVssSnapshotMgmt> snapshotManagement;
        ComPtr<IUnknown> interfaceValue;
        HRESULT result = InitializeComSecurity();

        if (SUCCEEDED(result))
        {
            result = CoCreateInstance(CLSID_VssSnapshotMgmt,
                                      nullptr,
                                      CLSCTX_ALL,
                                      IID_PPV_ARGS(snapshotManagement.ReleaseAndGetAddressOf()));
        }
        if (SUCCEEDED(result))
        {
            result = snapshotManagement->GetProviderMgmtInterface(SystemProvider,
                                                                   IID_IVssDifferentialSoftwareSnapshotMgmt,
                                                                   interfaceValue.ReleaseAndGetAddressOf());
        }
        if (SUCCEEDED(result)) result = interfaceValue.As(management);
        return result;
    }

    void FreeDiffArea(VSS_DIFF_AREA_PROP* property)
    {
        CoTaskMemFree(property->m_pwszVolumeName);
        CoTaskMemFree(property->m_pwszDiffAreaVolumeName);
    }

    HRESULT QueryDiffArea(
        IVssDifferentialSoftwareSnapshotMgmt* management,
        PCWSTR volume,
        DiffArea* resultValue)
    {
        ComPtr<IVssEnumMgmtObject> enumerator;
        VSS_MGMT_OBJECT_PROP property = {};
        ULONG returned;
        HRESULT result;

        resultValue->Exists = false;
        result = management->QueryDiffAreasForVolume(const_cast<PWSTR>(volume), enumerator.ReleaseAndGetAddressOf());
        if (FAILED(result)) return result;
        result = enumerator->Next(1, &property, &returned);
        if (result == S_FALSE || returned == 0) return S_OK;
        if (FAILED(result)) return result;
        if (property.Type != VSS_MGMT_OBJECT_DIFF_AREA) return VSS_E_BAD_STATE;
        resultValue->Exists = true;
        resultValue->MaximumSpace = property.Obj.DiffArea.m_llMaximumDiffSpace;
        resultValue->AllocatedSpace = property.Obj.DiffArea.m_llAllocatedDiffSpace;
        resultValue->UsedSpace = property.Obj.DiffArea.m_llUsedDiffSpace;
        _snwprintf_s(resultValue->Volume,
                     ARRAYSIZE(resultValue->Volume),
                     _TRUNCATE,
                     L"%s",
                     property.Obj.DiffArea.m_pwszDiffAreaVolumeName);
        FreeDiffArea(&property.Obj.DiffArea);
        return S_OK;
    }

    HRESULT WaitForVss(IVssAsync* operation)
    {
        HRESULT result = operation->Wait();
        HRESULT operationResult;

        if (SUCCEEDED(result)) result = operation->QueryStatus(&operationResult, nullptr);
        return SUCCEEDED(result) ? operationResult : result;
    }

    HRESULT OpenRestoreControl(ComPtr<IRestoreControl>* control)
    {
        return CoCreateInstance(RestoreControlClass,
                                nullptr,
                                CLSCTX_INPROC_SERVER,
                                __uuidof(IRestoreControl),
                                reinterpret_cast<PVOID*>(control->ReleaseAndGetAddressOf()));
    }

    HRESULT OpenSppManagement(ComPtr<ISppManagement>* management)
    {
        return CoCreateInstance(SppClass,
                                nullptr,
                                CLSCTX_ALL,
                                __uuidof(ISppManagement),
                                reinterpret_cast<PVOID*>(management->ReleaseAndGetAddressOf()));
    }

    HRESULT PrepareForRestore()
    {
        SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        SC_HANDLE service;
        SERVICE_STATUS serviceStatus;

        if (manager == nullptr) return HRESULT_FROM_WIN32(GetLastError());
        service = OpenServiceW(manager, L"wbengine", SERVICE_STOP);
        if (service != nullptr)
        {
            ControlService(service, SERVICE_CONTROL_STOP, &serviceStatus);
            CloseServiceHandle(service);
        }
        CloseServiceHandle(manager);
        return S_OK;
    }

    DWORD WINAPI RestartForRestore(_In_opt_ PVOID)
    {
        LARGE_INTEGER delay;
        BOOLEAN previous;

        delay.QuadPart = -2 * 10'000'000LL;
        NtDelayExecution(FALSE, &delay);
        if (NT_SUCCESS(RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, TRUE, FALSE, &previous)))
            NtShutdownSystem(ShutdownReboot);
        return 0;
    }

    HRESULT ScheduleRestoreRestart()
    {
        HANDLE thread = CreateThread(nullptr, 0, RestartForRestore, nullptr, 0, nullptr);

        if (thread == nullptr) return HRESULT_FROM_WIN32(GetLastError());
        NtClose(thread);
        return S_OK;
    }

    HRESULT OpenRestorePointFreeRoutine(HMODULE* module, FreeRestorePointProperties* freeProperties)
    {
        *module = LoadLibraryExW(L"srcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (*module == nullptr) return HRESULT_FROM_WIN32(GetLastError());
        *freeProperties = reinterpret_cast<FreeRestorePointProperties>(
            GetProcAddress(*module, "SrFreeRpPropArray"));
        if (*freeProperties != nullptr) return S_OK;
        HRESULT result = HRESULT_FROM_WIN32(GetLastError());
        FreeLibrary(*module);
        *module = nullptr;
        return result;
    }

    HRESULT OpenSppClientFreeRoutine(HMODULE* module, FreeSppClientProperties* freeProperties)
    {
        *module = LoadLibraryExW(L"spp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (*module == nullptr) return HRESULT_FROM_WIN32(GetLastError());
        *freeProperties = reinterpret_cast<FreeSppClientProperties>(
            GetProcAddress(*module, "SppFreeClientPropArray"));
        if (*freeProperties != nullptr) return S_OK;
        HRESULT result = HRESULT_FROM_WIN32(GetLastError());
        FreeLibrary(*module);
        *module = nullptr;
        return result;
    }

    bool IsSystemProtectionEnabled(
        const SppClientProperty* properties,
        ULONG count,
        PCWSTR volume)
    {
        for (ULONG index = 0; index < count; index++)
        {
            const auto& property = properties[index];

            if (!IsEqualGUID(property.ClientId, SystemRestoreClient)) continue;
            for (ULONG volumeIndex = 0; volumeIndex < property.VolumeCount; volumeIndex++)
            {
                if (_wcsicmp(property.Volumes[volumeIndex], volume) == 0) return true;
            }
        }
        return false;
    }
}

ZP_STATUS
ZpAdministration_EnumerateSystemProtection(
    _Outptr_result_bytebuffer_(*responseLength) PBYTE* response,
    _Out_ PULONG responseLength)
{
    Builder builder = {};
    ComScope com;
    ComPtr<IVssDifferentialSoftwareSnapshotMgmt> management;
    ComPtr<ISppManagement> sppManagement;
    SppClientProperty* sppProperties = nullptr;
    FreeSppClientProperties freeSppProperties = nullptr;
    HMODULE spp = nullptr;
    DiffArea diffArea;
    WCHAR drives[128], *drive, volume[64], label[MAX_PATH], windowsDirectory[MAX_PATH], detail[256];
    HRESULT result = com.Result;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG flags, sppCount = 0;
    bool protectedVolume;

    if (SUCCEEDED(result)) result = OpenDiffAreaManagement(&management);
    if (FAILED(result)) return ZpStatus_FromCode(ZpStatusHResult, result);
    if (GetLogicalDriveStringsW(ARRAYSIZE(drives), drives) == 0)
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (GetWindowsDirectoryW(windowsDirectory, ARRAYSIZE(windowsDirectory)) == 0)
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (SUCCEEDED(result)) result = OpenSppClientFreeRoutine(&spp, &freeSppProperties);
    if (SUCCEEDED(result)) result = OpenSppManagement(&sppManagement);
    if (SUCCEEDED(result)) result = sppManagement->GetClients(0, &sppCount, &sppProperties);
    for (drive = drives; *drive != UNICODE_NULL && NT_SUCCESS(status); drive += wcslen(drive) + 1)
    {
        if (FAILED(result)) break;
        if (GetDriveTypeW(drive) != DRIVE_FIXED) continue;
        if (!GetVolumeNameForVolumeMountPointW(drive, volume, ARRAYSIZE(volume)))
        {
            status = NTSTATUS_FROM_WIN32(GetLastError());
            break;
        }
        result = QueryDiffArea(management.Get(), volume, &diffArea);
        if (result == VSS_E_OBJECT_NOT_FOUND)
        {
            diffArea.Exists = false;
            result = S_OK;
        }
        if (FAILED(result)) break;
        label[0] = UNICODE_NULL;
        GetVolumeInformationW(drive, label, ARRAYSIZE(label), nullptr, nullptr, nullptr, nullptr, 0);
        protectedVolume = IsSystemProtectionEnabled(sppProperties, sppCount, volume);
        flags = towupper(drive[0]) == towupper(windowsDirectory[0]) ?
                    ZP_ADMINISTRATION_SYSTEM_PROTECTION_FLAG_SYSTEM_VOLUME : 0;
        _snwprintf_s(detail,
                     ARRAYSIZE(detail),
                     _TRUNCATE,
                     L"%lld\n%lld\n%s",
                     diffArea.Exists ? diffArea.UsedSpace : 0,
                     diffArea.Exists ? diffArea.AllocatedSpace : 0,
                     diffArea.Exists ? diffArea.Volume : L"");
        status = AddRecord(&builder,
                           ZpAdministrationKindSystemProtectionVolume,
                           protectedVolume ? 1 : 0,
                           flags,
                           diffArea.Exists ? static_cast<ULONGLONG>(diffArea.MaximumSpace) : 0,
                           drive,
                           *label == UNICODE_NULL ? drive : label,
                           volume,
                           detail);
    }
    if (sppProperties != nullptr) freeSppProperties(sppCount, &sppProperties);
    if (spp != nullptr) FreeLibrary(spp);
    if (FAILED(result))
    {
        Mem_Free(builder.Buffer);
        return ZpStatus_FromCode(ZpStatusHResult, result);
    }
    if (NT_SUCCESS(status)) status = EncodeBuilder(&builder, response, responseLength);
    Mem_Free(builder.Buffer);
    return ZpStatus_FromNtStatus(status);
}

ZP_STATUS
ZpAdministration_ControlSystemProtection(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW control)
{
    ComScope com;
    ComPtr<IVssDifferentialSoftwareSnapshotMgmt> management;
    DiffArea diffArea;
    PWSTR identity = CopyString(&control->Identity);
    PWSTR argument = control->Argument.Length == 0 ? nullptr : CopyString(&control->Argument);
    PWSTR end;
    WCHAR volume[64];
    ULONGLONG maximumSpace = 0;
    HRESULT result = com.Result;

    if (identity == nullptr || control->Argument.Length != 0 && argument == nullptr)
    {
        result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    if (wcslen(identity) != 3 || !iswalpha(identity[0]) || identity[1] != L':' || identity[2] != L'\\')
    {
        result = E_INVALIDARG;
        goto Cleanup;
    }
    if (argument != nullptr)
    {
        maximumSpace = _wcstoui64(argument, &end, 10);
        if (*argument == UNICODE_NULL || *end != UNICODE_NULL || maximumSpace == 0 || maximumSpace > MAXLONGLONG)
        {
            result = E_INVALIDARG;
            goto Cleanup;
        }
    }
    if (control->Action == ZpAdministrationActionDisable)
    {
        if (argument != nullptr)
        {
            result = E_INVALIDARG;
            goto Cleanup;
        }
        if (SUCCEEDED(result))
        {
            DWORD error = DisableSRInternal(identity, TRUE);

            result = error == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(error);
        }
        goto Cleanup;
    }
    if (control->Action != ZpAdministrationActionEnable &&
        control->Action != ZpAdministrationActionConfigure)
    {
        result = E_NOTIMPL;
        goto Cleanup;
    }
    if (control->Action == ZpAdministrationActionEnable && SUCCEEDED(result))
    {
        DWORD error = EnableSRInternal(identity, TRUE);

        result = error == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(error);
    }
    if (SUCCEEDED(result) && maximumSpace != 0)
    {
        if (!GetVolumeNameForVolumeMountPointW(identity, volume, ARRAYSIZE(volume)))
        {
            result = HRESULT_FROM_WIN32(GetLastError());
            goto Cleanup;
        }
        result = OpenDiffAreaManagement(&management);
        if (SUCCEEDED(result)) result = QueryDiffArea(management.Get(), volume, &diffArea);
        if (SUCCEEDED(result) && !diffArea.Exists) result = VSS_E_OBJECT_NOT_FOUND;
        if (SUCCEEDED(result))
        {
            result = management->ChangeDiffAreaMaximumSize(volume,
                                                            diffArea.Volume,
                                                            static_cast<LONGLONG>(maximumSpace));
        }
    }
    else if (control->Action == ZpAdministrationActionConfigure && maximumSpace == 0)
    {
        result = E_INVALIDARG;
    }
Cleanup:
    Mem_Free(argument);
    Mem_Free(identity);
    return ZpStatus_FromCode(ZpStatusHResult, result);
}

ZP_STATUS
ZpAdministration_EnumerateRestorePoints(
    _Outptr_result_bytebuffer_(*responseLength) PBYTE* response,
    _Out_ PULONG responseLength)
{
    Builder builder = {};
    ComScope com;
    ComPtr<IRestoreControl> restoreControl;
    RestorePointProperty* properties = nullptr;
    FreeRestorePointProperties freeProperties = nullptr;
    HMODULE core = nullptr;
    WCHAR identity[32];
    HRESULT result = com.Result;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG count = 0;
    ULONGLONG creation;

    if (SUCCEEDED(result)) result = OpenRestorePointFreeRoutine(&core, &freeProperties);
    if (SUCCEEDED(result)) result = OpenRestoreControl(&restoreControl);
    if (SUCCEEDED(result)) result = restoreControl->GetRestorePointList(&count, &properties);
    for (ULONG index = 0; index < count && SUCCEEDED(result); index++)
    {
        const auto& property = properties[index];

        creation = (static_cast<ULONGLONG>(property.CreationTime.dwHighDateTime) << 32) |
                   property.CreationTime.dwLowDateTime;
        _snwprintf_s(identity, ARRAYSIZE(identity), _TRUNCATE, L"%lu", property.SequenceNumber);
        status = AddRecord(&builder,
                           ZpAdministrationKindRestorePoint,
                           property.Type,
                           0,
                           creation,
                           identity,
                           property.Description,
                           nullptr,
                           nullptr);
        if (!NT_SUCCESS(status)) result = HRESULT_FROM_NT(status);
    }
    if (properties != nullptr) freeProperties(count, &properties);
    if (core != nullptr) FreeLibrary(core);
    if (FAILED(result))
    {
        Mem_Free(builder.Buffer);
        return ZpStatus_FromCode(ZpStatusHResult, result);
    }
    if (NT_SUCCESS(status)) status = EncodeBuilder(&builder, response, responseLength);
    Mem_Free(builder.Buffer);
    return ZpStatus_FromNtStatus(status);
}

ZP_STATUS
ZpAdministration_ControlRestorePoint(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW control)
{
    RESTOREPOINTINFOW restorePoint = {};
    STATEMGRSTATUS managerStatus = {};
    ComScope com;
    ComPtr<IRestoreControl> restoreControl;
    RestorePointProperty* properties = nullptr;
    FreeRestorePointProperties freeProperties = nullptr;
    HMODULE core = nullptr;
    PWSTR identity = CopyString(&control->Identity);
    PWSTR end;
    ULONGLONG number;
    ULONG count = 0;
    DWORD error = ERROR_SUCCESS;
    HRESULT result = com.Result;

    if (identity == nullptr)
    {
        result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    if (control->Action == ZpAdministrationActionCreate)
    {
        if (*identity == UNICODE_NULL || wcslen(identity) >= ARRAYSIZE(restorePoint.szDescription) ||
            wcschr(identity, L'\n') != nullptr || wcschr(identity, L'\r') != nullptr)
        {
            result = E_INVALIDARG;
            goto Cleanup;
        }
        restorePoint.dwEventType = BEGIN_SYSTEM_CHANGE;
        restorePoint.dwRestorePtType = MANUAL_CHECKPOINT;
        _snwprintf_s(restorePoint.szDescription,
                     ARRAYSIZE(restorePoint.szDescription),
                     _TRUNCATE,
                     L"%s",
                     identity);
        if (!SRSetRestorePointW(&restorePoint, &managerStatus))
        {
            error = managerStatus.nStatus == ERROR_SUCCESS ? GetLastError() : managerStatus.nStatus;
            result = HRESULT_FROM_WIN32(error);
            goto Cleanup;
        }
        restorePoint.dwEventType = END_SYSTEM_CHANGE;
        restorePoint.llSequenceNumber = managerStatus.llSequenceNumber;
        if (!SRSetRestorePointW(&restorePoint, &managerStatus))
        {
            error = managerStatus.nStatus == ERROR_SUCCESS ? GetLastError() : managerStatus.nStatus;
            result = HRESULT_FROM_WIN32(error);
        }
        goto Cleanup;
    }
    number = _wcstoui64(identity, &end, 10);
    if (*identity == UNICODE_NULL || *end != UNICODE_NULL || number > MAXDWORD)
    {
        result = E_INVALIDARG;
        goto Cleanup;
    }
    if (control->Action == ZpAdministrationActionDelete)
    {
        error = SRRemoveRestorePoint(static_cast<DWORD>(number));
        result = error == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(error);
    }
    else if (control->Action == ZpAdministrationActionActivate)
    {
        result = OpenRestorePointFreeRoutine(&core, &freeProperties);
        if (SUCCEEDED(result)) result = OpenRestoreControl(&restoreControl);
        if (SUCCEEDED(result)) result = restoreControl->GetRestorePointList(&count, &properties);
        if (SUCCEEDED(result))
        {
            result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            for (ULONG index = 0; index < count; index++)
            {
                if (properties[index].SequenceNumber != number) continue;
                result = restoreControl->Restore(&properties[index], 0, nullptr);
                if (SUCCEEDED(result)) result = PrepareForRestore();
                if (SUCCEEDED(result)) result = ScheduleRestoreRestart();
                break;
            }
        }
    }
    else
    {
        result = E_NOTIMPL;
    }
Cleanup:
    if (properties != nullptr) freeProperties(count, &properties);
    if (core != nullptr) FreeLibrary(core);
    Mem_Free(identity);
    return ZpStatus_FromCode(ZpStatusHResult, result);
}

ZP_STATUS
ZpAdministration_EnumerateShadowCopies(
    _Outptr_result_bytebuffer_(*responseLength) PBYTE* response,
    _Out_ PULONG responseLength)
{
    Builder builder = {};
    ComScope com;
    ComPtr<IVssBackupComponents> components;
    ComPtr<IVssEnumObject> enumerator;
    VSS_OBJECT_PROP property = {};
    WCHAR identity[40], setIdentity[40], providerIdentity[40], detail[1024];
    HRESULT result = com.Result;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG returned, flags;

    if (SUCCEEDED(result)) result = OpenSnapshotComponents(VSS_CTX_ALL, &components);
    if (SUCCEEDED(result))
    {
        result = components->Query(GUID_NULL,
                                   VSS_OBJECT_NONE,
                                   VSS_OBJECT_SNAPSHOT,
                                   enumerator.ReleaseAndGetAddressOf());
    }
    while (SUCCEEDED(result))
    {
        result = enumerator->Next(1, &property, &returned);
        if (result == S_FALSE || returned == 0)
        {
            result = S_OK;
            break;
        }
        if (FAILED(result)) break;
        if (property.Type != VSS_OBJECT_SNAPSHOT)
        {
            result = VSS_E_BAD_STATE;
            break;
        }
        const auto& snapshot = property.Obj.Snap;
        if (StringFromGUID2(snapshot.m_SnapshotId, identity, ARRAYSIZE(identity)) == 0 ||
            StringFromGUID2(snapshot.m_SnapshotSetId, setIdentity, ARRAYSIZE(setIdentity)) == 0 ||
            StringFromGUID2(snapshot.m_ProviderId, providerIdentity, ARRAYSIZE(providerIdentity)) == 0)
        {
            VssFreeSnapshotProperties(&property.Obj.Snap);
            ZeroMemory(&property, sizeof(property));
            result = E_UNEXPECTED;
            break;
        }
        flags = FlagOn(snapshot.m_lSnapshotAttributes, VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE) ?
                    ZP_ADMINISTRATION_SHADOW_COPY_FLAG_CLIENT_ACCESSIBLE : 0;
        if (FlagOn(snapshot.m_lSnapshotAttributes, VSS_VOLSNAP_ATTR_PERSISTENT))
            flags |= ZP_ADMINISTRATION_SHADOW_COPY_FLAG_PERSISTENT;
        if (FlagOn(snapshot.m_lSnapshotAttributes, VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE))
            flags |= ZP_ADMINISTRATION_SHADOW_COPY_FLAG_NO_AUTO_RELEASE;
        if (FlagOn(snapshot.m_lSnapshotAttributes, VSS_VOLSNAP_ATTR_NO_WRITERS))
            flags |= ZP_ADMINISTRATION_SHADOW_COPY_FLAG_NO_WRITERS;
        if (FlagOn(snapshot.m_lSnapshotAttributes,
                   VSS_VOLSNAP_ATTR_EXPOSED_LOCALLY | VSS_VOLSNAP_ATTR_EXPOSED_REMOTELY))
            flags |= ZP_ADMINISTRATION_SHADOW_COPY_FLAG_EXPOSED;
        if (FlagOn(snapshot.m_lSnapshotAttributes, VSS_VOLSNAP_ATTR_HARDWARE_ASSISTED))
            flags |= ZP_ADMINISTRATION_SHADOW_COPY_FLAG_HARDWARE_ASSISTED;
        _snwprintf_s(detail,
                     ARRAYSIZE(detail),
                     _TRUNCATE,
                     L"%s\n%s\n%s\n%s",
                     setIdentity,
                     providerIdentity,
                     snapshot.m_pwszOriginatingMachine == nullptr ? L"" : snapshot.m_pwszOriginatingMachine,
                     snapshot.m_pwszExposedName == nullptr ? L"" : snapshot.m_pwszExposedName);
        status = AddRecord(&builder,
                           ZpAdministrationKindShadowCopy,
                           static_cast<ULONG>(snapshot.m_lSnapshotAttributes),
                           flags,
                           snapshot.m_tsCreationTimestamp > 0 ?
                               static_cast<ULONGLONG>(snapshot.m_tsCreationTimestamp) : 0,
                           identity,
                           snapshot.m_pwszOriginalVolumeName,
                           snapshot.m_pwszSnapshotDeviceObject,
                           detail);
        VssFreeSnapshotProperties(&property.Obj.Snap);
        ZeroMemory(&property, sizeof(property));
        if (!NT_SUCCESS(status)) result = HRESULT_FROM_NT(status);
    }
    if (property.Type == VSS_OBJECT_SNAPSHOT) VssFreeSnapshotProperties(&property.Obj.Snap);
    if (FAILED(result))
    {
        Mem_Free(builder.Buffer);
        return ZpStatus_FromCode(ZpStatusHResult, result);
    }
    if (NT_SUCCESS(status)) status = EncodeBuilder(&builder, response, responseLength);
    Mem_Free(builder.Buffer);
    return ZpStatus_FromNtStatus(status);
}

ZP_STATUS
ZpAdministration_ControlShadowCopy(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW control)
{
    ComScope com;
    ComPtr<IVssBackupComponents> components;
    ComPtr<IVssAsync> operation;
    PWSTR identity = CopyString(&control->Identity);
    WCHAR volume[64];
    VSS_ID snapshotSet, snapshot, nonDeleted;
    LONG deleted;
    HRESULT result = com.Result;

    if (identity == nullptr)
    {
        result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    if (control->Action == ZpAdministrationActionCreate)
    {
        if (wcslen(identity) != 3 || !iswalpha(identity[0]) || identity[1] != L':' || identity[2] != L'\\')
        {
            result = E_INVALIDARG;
            goto Cleanup;
        }
        if (!GetVolumeNameForVolumeMountPointW(identity, volume, ARRAYSIZE(volume)))
        {
            result = HRESULT_FROM_WIN32(GetLastError());
            goto Cleanup;
        }
        if (SUCCEEDED(result)) result = OpenSnapshotComponents(VSS_CTX_CLIENT_ACCESSIBLE, &components);
        if (SUCCEEDED(result)) result = components->StartSnapshotSet(&snapshotSet);
        if (SUCCEEDED(result)) result = components->AddToSnapshotSet(volume, SystemProvider, &snapshot);
        if (SUCCEEDED(result)) result = components->DoSnapshotSet(operation.ReleaseAndGetAddressOf());
        if (SUCCEEDED(result)) result = WaitForVss(operation.Get());
    }
    else if (control->Action == ZpAdministrationActionDelete)
    {
        result = CLSIDFromString(identity, &snapshot);
        if (SUCCEEDED(result)) result = OpenSnapshotComponents(VSS_CTX_ALL, &components);
        if (SUCCEEDED(result))
        {
#pragma warning(suppress: 6001)
            result = components->DeleteSnapshots(snapshot,
                                                  VSS_OBJECT_SNAPSHOT,
                                                  TRUE,
                                                  &deleted,
                                                  &nonDeleted);
        }
    }
    else
    {
        result = E_NOTIMPL;
    }
Cleanup:
    Mem_Free(identity);
    return ZpStatus_FromCode(ZpStatusHResult, result);
}
