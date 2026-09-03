#include "Client.h"

#include "ShadowCopy.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#define COBJMACROS
#include <ws2ipdef.h>
#include <netfw.h>
#include <netcon.h>
#include <netlistmgr.h>
#include <oleauto.h>
#include <powrprof.h>
#include <roapi.h>
#include <windows.security.credentials.h>
#include <wincred.h>
#include <wlanapi.h>
#include <wslapi.h>
#include <KNSoft/FirmwareSpec/CPUID.Decode.h>
#include <KNSoft/NDK/NT/Ex/Boot.h>
#include <KNSoft/NDK/NT/Win32K/Win32KApi.h>

typedef struct _ZP_ADMINISTRATION_BUILDER
{
    PBYTE Buffer;
    ULONG Length;
    ULONG Capacity;
    ULONG Count;
} ZP_ADMINISTRATION_BUILDER, *PZP_ADMINISTRATION_BUILDER;

static
NTSTATUS
ZpAdministration_ReserveBuilder(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ ULONG AdditionalLength)
{
    PBYTE Buffer;
    ULONG Capacity, RequiredLength;

    if (AdditionalLength > ZP_RESPONSE_MAX_PAYLOAD_SIZE - Builder->Length)
    {
        return STATUS_BUFFER_OVERFLOW;
    }
    RequiredLength = Builder->Length + AdditionalLength;
    if (RequiredLength <= Builder->Capacity)
    {
        return STATUS_SUCCESS;
    }
    Capacity = Builder->Capacity == 0 ? 1024 : Builder->Capacity;
    while (Capacity < RequiredLength)
    {
        Capacity = min(Capacity * 2, ZP_RESPONSE_MAX_PAYLOAD_SIZE);
    }
    Buffer = Mem_ReAlloc(Builder->Buffer, Capacity);
    if (Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Builder->Buffer = Buffer;
    Builder->Capacity = Capacity;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpAdministration_AddRecordData(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ ZP_ADMINISTRATION_KIND Kind,
    _In_ ULONG State,
    _In_ ULONG Flags,
    _In_ ULONGLONG Value,
    _In_ PCWSTR Identity,
    _In_opt_ PCWSTR Name,
    _In_opt_ PCWSTR Description,
    _In_opt_ PCWSTR Detail,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    ZP_ADMINISTRATION_RECORD Record;
    ULONG RecordLength;
    NTSTATUS Status;

    if (Builder->Count == ZP_CODEC_MAX_ELEMENT_COUNT)
    {
        return STATUS_QUOTA_EXCEEDED;
    }
    Record.Kind = Kind;
    Record.State = State;
    Record.Flags = Flags;
    Record.Value = Value;
    Record.Identity = Identity;
    Record.IdentityLength = (ULONG)wcslen(Identity);
    Record.Name = Name;
    Record.NameLength = Name == NULL ? 0 : (ULONG)wcslen(Name);
    Record.Description = Description;
    Record.DescriptionLength = Description == NULL ? 0 : (ULONG)wcslen(Description);
    Record.Detail = Detail;
    Record.DetailLength = Detail == NULL ? 0 : (ULONG)wcslen(Detail);
    Record.Data = Data;
    Record.DataLength = DataLength;
    Status = ZpAdministration_EncodeRecord(&Record, NULL, 0, &RecordLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Builder->Length == 0)
    {
        Builder->Length = sizeof(ULONG);
    }
    Status = ZpAdministration_ReserveBuilder(Builder, RecordLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpAdministration_EncodeRecord(&Record,
                                            Builder->Buffer + Builder->Length,
                                            Builder->Capacity - Builder->Length,
                                            &RecordLength);
    if (NT_SUCCESS(Status))
    {
        Builder->Length += RecordLength;
        Builder->Count++;
    }
    return Status;
}

static
NTSTATUS
ZpAdministration_AddRecord(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ ZP_ADMINISTRATION_KIND Kind,
    _In_ ULONG State,
    _In_ ULONG Flags,
    _In_ ULONGLONG Value,
    _In_ PCWSTR Identity,
    _In_opt_ PCWSTR Name,
    _In_opt_ PCWSTR Description,
    _In_opt_ PCWSTR Detail)
{
    return ZpAdministration_AddRecordData(Builder,
                                          Kind,
                                          State,
                                          Flags,
                                          Value,
                                          Identity,
                                          Name,
                                          Description,
                                          Detail,
                                          NULL,
                                          0);
}

static
VOID
ZpAdministration_FreeBuilder(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    Mem_Free(Builder->Buffer);
}

static
NTSTATUS
ZpAdministration_EncodeBuilder(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (Builder->Length == 0)
    {
        Status = ZpAdministration_ReserveBuilder(Builder, sizeof(ULONG));
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        Builder->Length = sizeof(ULONG);
    }
    ZpCodec_InitializeWriter(&Writer, Builder->Buffer, Builder->Length);
    Status = ZpCodec_WriteUInt32(&Writer, Builder->Count);
    if (!NT_SUCCESS(Status)) return Status;
    *Response = Builder->Buffer;
    *ResponseLength = Builder->Length;
    Builder->Buffer = NULL;
    return STATUS_SUCCESS;
}

static
PWSTR
ZpAdministration_CopyView(
    _In_ PCZP_STRING_VIEW View)
{
    PWSTR String = Mem_Alloc(((SIZE_T)View->Length + 1) * sizeof(WCHAR));

    if (String != NULL)
    {
        RtlCopyMemory(String, View->Buffer, (SIZE_T)View->Length * sizeof(WCHAR));
        String[View->Length] = UNICODE_NULL;
    }
    return String;
}

static
NTSTATUS
ZpAdministration_GetDataControlIdentityString(
    _In_ PCZP_ADMINISTRATION_DATA_CONTROL_VIEW Control,
    _Out_ PZP_STRING_VIEW Identity)
{
    if (Control->Identity.Length % sizeof(WCHAR) != 0) return STATUS_INVALID_PARAMETER;
    Identity->Buffer = (PCWCH)Control->Identity.Buffer;
    Identity->Length = Control->Identity.Length / sizeof(WCHAR);
    return STATUS_SUCCESS;
}

static
ULONGLONG
ZpAdministration_DateToFileTime(
    _In_ DATE Date)
{
    SYSTEMTIME SystemTime;
    FILETIME FileTime;

    return Date != 0 && VariantTimeToSystemTime(Date, &SystemTime) &&
           SystemTimeToFileTime(&SystemTime, &FileTime) ?
               ((ULONGLONG)FileTime.dwHighDateTime << 32) | FileTime.dwLowDateTime :
               0;
}

typedef const VOID* PCVOID;

#include "User.inl"
#include "Hardware.inl"
#include "Software.inl"
#include "InputMethod.inl"
#include "Update.inl"
#include "Task.inl"
#include "Firewall.inl"
#include "Power.inl"
#include "System.inl"
#include "Wlan.inl"
#include "Certificate.inl"
#include "Clipboard.inl"
#include "Credential.inl"
#include "Firmware.inl"
#include "NetworkShare.inl"
#include "NetworkStatus.inl"
#include "PageFile.inl"
#include "Bluetooth.inl"
#include "Keyboard.inl"
#include "Location.inl"
#include "Font.inl"
#include "AppContainer.inl"
#include "WinObj.inl"
#include "Wsl.inl"
#include "Uia.inl"
#include "ProxyVpn.inl"
#include "ClientStatus.inl"
#include "BitLocker.inl"

typedef ZP_STATUS (*ZP_ENUMERATE_ROUTINE)(PBYTE*, PULONG);
typedef ZP_STATUS (*ZP_QUERY_ROUTINE)(PCZP_STRING_VIEW, PBYTE*, PULONG);
typedef ZP_STATUS (*ZP_CONTROL_ROUTINE)(PCZP_ADMINISTRATION_CONTROL_VIEW);
typedef ZP_STATUS (*ZP_CONTROL_RESULT_ROUTINE)(PCZP_ADMINISTRATION_CONTROL_VIEW, PBYTE*, PULONG);
typedef ZP_STATUS (*ZP_DATA_CONTROL_ROUTINE)(PCZP_ADMINISTRATION_DATA_CONTROL_VIEW);

typedef struct _ZP_ENUMERATE_OPERATION
{
    BYTE ModuleId;
    BYTE OperationId;
    ZP_ENUMERATE_ROUTINE Routine;
} ZP_ENUMERATE_OPERATION;

typedef struct _ZP_QUERY_OPERATION
{
    BYTE ModuleId;
    BYTE OperationId;
    ZP_QUERY_ROUTINE Routine;
} ZP_QUERY_OPERATION;

typedef struct _ZP_CONTROL_OPERATION
{
    BYTE ModuleId;
    BYTE OperationId;
    ZP_CONTROL_ROUTINE Routine;
} ZP_CONTROL_OPERATION;

typedef struct _ZP_CONTROL_RESULT_OPERATION
{
    BYTE ModuleId;
    BYTE OperationId;
    ZP_CONTROL_RESULT_ROUTINE Routine;
} ZP_CONTROL_RESULT_OPERATION;

typedef struct _ZP_DATA_CONTROL_OPERATION
{
    BYTE ModuleId;
    BYTE OperationId;
    ZP_DATA_CONTROL_ROUTINE Routine;
} ZP_DATA_CONTROL_OPERATION;

static ZP_STATUS
ZpAdministration_EnumerateAvailableUpdates(
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    return ZpAdministration_EnumerateUpdates(FALSE, Response, ResponseLength);
}

static const ZP_ENUMERATE_OPERATION ZpEnumerateOperations[] = {
    { ZP_USER_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_USERS, ZpAdministration_EnumerateUsers },
    { ZP_USER_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_SESSIONS, ZpAdministration_EnumerateSessions },
    { ZP_USER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_LOGON_SESSIONS,
      ZpAdministration_EnumerateLogonSessions },
    { ZP_USER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_USER_PROFILES,
      ZpAdministration_EnumerateUserProfiles },
    { ZP_SOFTWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_SOFTWARE, ZpAdministration_EnumerateSoftware },
    { ZP_SOFTWARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_PACKAGE_PROVIDERS,
      ZpAdministration_EnumeratePackageProviders },
    { ZP_SOFTWARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_SOFTWARE_DEPLOYMENTS,
      ZpAdministration_EnumerateSoftwareDeployments },
    { ZP_SOFTWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_FEATURES, ZpAdministration_EnumerateFeatures },
    { ZP_SOFTWARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_INPUT_METHODS,
      ZpAdministration_EnumerateInputMethods },
    { ZP_HARDWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_HARDWARE, ZpAdministration_EnumerateHardware },
    { ZP_UPDATE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_UPDATES, ZpAdministration_EnumerateAvailableUpdates },
    { ZP_TASK_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_TASKS, ZpAdministration_EnumerateTasks },
    { ZP_FIREWALL_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_FIREWALL, ZpAdministration_EnumerateFirewall },
    { ZP_POWER_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_POWER, ZpAdministration_EnumeratePower },
    { ZP_SYSTEM_ADMINISTRATION_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_SYSTEM,
      ZpAdministration_EnumerateSystem },
    { ZP_WLAN_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_WLAN, ZpAdministration_EnumerateWlan },
    { ZP_CERTIFICATE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_CERTIFICATES,
      ZpAdministration_EnumerateCertificates },
    { ZP_CERTIFICATE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_CERTIFICATE_STORES,
      ZpAdministration_EnumerateCertificateStores },
    { ZP_CLIPBOARD_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_CLIPBOARD_IMAGE,
      ZpAdministration_QueryClipboardImage },
    { ZP_CLIPBOARD_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_CLIPBOARD,
      ZpAdministration_EnumerateClipboard },
    { ZP_CREDENTIAL_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_CREDENTIALS,
      ZpAdministration_EnumerateCredentials },
    { ZP_FIRMWARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_FIRMWARE_VARIABLES,
      ZpAdministration_EnumerateFirmwareVariables },
    { ZP_NETWORK_SHARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_PUBLISHED_SHARES,
      ZpAdministration_EnumeratePublishedShares },
    { ZP_NETWORK_SHARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_NETWORK_CONNECTIONS,
      ZpAdministration_EnumerateNetworkConnections },
    { ZP_NETWORK_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_NETWORK_ADAPTERS,
      ZpAdministration_EnumerateNetworkAdapters },
    { ZP_NETWORK_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_NETWORK_ROUTES,
      ZpAdministration_EnumerateNetworkRoutes },
    { ZP_NETWORK_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_NETWORK_ENDPOINTS,
      ZpAdministration_EnumerateNetworkEndpoints },
    { ZP_PAGE_FILE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_PAGE_FILES,
      ZpAdministration_EnumeratePageFiles },
    { ZP_BLUETOOTH_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_BLUETOOTH,
      ZpAdministration_EnumerateBluetooth },
    { ZP_FONT_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_FONTS, ZpAdministration_EnumerateFonts },
    { ZP_APP_CONTAINER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_APP_CONTAINERS,
      ZpAdministration_EnumerateAppContainers },
    { ZP_WSL_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_WSL_DISTRIBUTIONS,
      ZpAdministration_EnumerateWslDistributions },
    { ZP_WSL_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_WSL_PROCESSES,
      ZpAdministration_EnumerateWslProcesses },
    { ZP_PROXY_VPN_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_PROXY_VPN,
      ZpAdministration_EnumerateProxyVpn },
    { ZP_CLIENT_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_CLIENT_STATUS,
      ZpAdministration_EnumerateClientStatus },
    { ZP_SHADOW_COPY_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_SYSTEM_PROTECTION,
      ZpAdministration_EnumerateSystemProtection },
    { ZP_SHADOW_COPY_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_RESTORE_POINTS,
      ZpAdministration_EnumerateRestorePoints },
    { ZP_SHADOW_COPY_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_SHADOW_COPIES,
      ZpAdministration_EnumerateShadowCopies },
    { ZP_BITLOCKER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_BITLOCKER_VOLUMES,
      ZpAdministration_EnumerateBitLockerVolumes },
    { ZP_BITLOCKER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_BITLOCKER_PROTECTORS,
      ZpAdministration_EnumerateBitLockerProtectors },
};

static const ZP_QUERY_OPERATION ZpQueryOperations[] = {
    { ZP_SOFTWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_QUERY_PACKAGES, ZpAdministration_QueryPackages },
    { ZP_WLAN_MODULE_ID, ZP_ADMINISTRATION_OPERATION_QUERY_WLAN_PROFILE, ZpAdministration_QueryWlanProfile },
    { ZP_CERTIFICATE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_CERTIFICATE,
      ZpAdministration_QueryCertificate },
    { ZP_CERTIFICATE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_CERTIFICATE_DATA,
      ZpAdministration_QueryCertificateData },
    { ZP_CLIPBOARD_MODULE_ID, ZP_ADMINISTRATION_OPERATION_WAIT_CLIPBOARD, ZpAdministration_WaitClipboard },
    { ZP_CREDENTIAL_MODULE_ID, ZP_ADMINISTRATION_OPERATION_QUERY_CREDENTIAL, ZpAdministration_QueryCredential },
    { ZP_FIRMWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_QUERY_FIRMWARE, ZpAdministration_QueryFirmware },
    { ZP_FIRMWARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_FIRMWARE_DATA,
      ZpAdministration_QueryFirmwareData },
    { ZP_NETWORK_SHARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_PUBLISHED_SHARE,
      ZpAdministration_QueryPublishedShare },
    { ZP_KEYBOARD_MODULE_ID, ZP_ADMINISTRATION_OPERATION_WAIT_KEYBOARD, ZpAdministration_WaitKeyboard },
    { ZP_LOCATION_MODULE_ID, ZP_ADMINISTRATION_OPERATION_QUERY_LOCATION, ZpAdministration_QueryLocation },
    { ZP_WINOBJ_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_OBJECT_DIRECTORY,
      ZpAdministration_QueryObjectDirectory },
    { ZP_UI_AUTOMATION_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_UI_AUTOMATION_CHILDREN,
      ZpAdministration_QueryUiAutomationChildren },
    { ZP_UI_AUTOMATION_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_UI_AUTOMATION_PROPERTIES,
      ZpAdministration_QueryUiAutomationProperties },
};

static const ZP_CONTROL_OPERATION ZpControlOperations[] = {
    { ZP_USER_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_USER, ZpAdministration_ControlUser },
    { ZP_USER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_USER_PROFILE,
      ZpAdministration_ControlUserProfile },
    { ZP_SOFTWARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_INPUT_METHOD,
      ZpAdministration_ControlInputMethod },
    { ZP_HARDWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_HARDWARE, ZpAdministration_ControlHardware },
    { ZP_UPDATE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_UPDATE, ZpAdministration_ControlUpdate },
    { ZP_TASK_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_TASK, ZpAdministration_ControlTask },
    { ZP_FIREWALL_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_FIREWALL, ZpAdministration_ControlFirewall },
    { ZP_POWER_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_POWER, ZpAdministration_ControlPower },
    { ZP_SYSTEM_ADMINISTRATION_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_SYSTEM,
      ZpAdministration_ControlSystem },
    { ZP_WLAN_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_WLAN, ZpAdministration_ControlWlan },
    { ZP_CLIPBOARD_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_CLIPBOARD,
      ZpAdministration_ControlClipboard },
    { ZP_CREDENTIAL_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_CREDENTIAL,
      ZpAdministration_ControlCredential },
    { ZP_NETWORK_SHARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_PUBLISHED_SHARE,
      ZpAdministration_ControlPublishedShare },
    { ZP_NETWORK_SHARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_NETWORK_CONNECTION,
      ZpAdministration_ControlNetworkConnection },
    { ZP_NETWORK_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_NETWORK_ADAPTER,
      ZpAdministration_ControlNetworkAdapter },
    { ZP_NETWORK_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_NETWORK_ROUTE,
      ZpAdministration_ControlNetworkRoute },
    { ZP_NETWORK_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_NETWORK_ENDPOINT,
      ZpAdministration_ControlNetworkEndpoint },
    { ZP_PAGE_FILE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_PAGE_FILE, ZpAdministration_ControlPageFile },
    { ZP_BLUETOOTH_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_BLUETOOTH,
      ZpAdministration_ControlBluetooth },
    { ZP_FONT_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_FONT, ZpAdministration_ControlFont },
    { ZP_APP_CONTAINER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_APP_CONTAINER,
      ZpAdministration_ControlAppContainer },
    { ZP_WSL_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_WSL_DISTRIBUTION,
      ZpAdministration_ControlWslDistribution },
    { ZP_WSL_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_WSL_PROCESS,
      ZpAdministration_ControlWslProcess },
    { ZP_PROXY_VPN_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_PROXY_VPN,
      ZpAdministration_ControlProxyVpn },
    { ZP_SHADOW_COPY_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_SYSTEM_PROTECTION,
      ZpAdministration_ControlSystemProtection },
    { ZP_SHADOW_COPY_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_RESTORE_POINT,
      ZpAdministration_ControlRestorePoint },
    { ZP_SHADOW_COPY_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_SHADOW_COPY,
      ZpAdministration_ControlShadowCopy },
    { ZP_BITLOCKER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_BITLOCKER_VOLUME,
      ZpAdministration_ControlBitLockerVolume },
    { ZP_BITLOCKER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_BITLOCKER_PROTECTOR,
      ZpAdministration_ControlBitLockerProtector },
};

static const ZP_CONTROL_RESULT_OPERATION ZpControlResultOperations[] = {
    { ZP_SOFTWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_FEATURE, ZpAdministration_ControlFeature },
};

static const ZP_DATA_CONTROL_OPERATION ZpDataControlOperations[] = {
    { ZP_SOFTWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_SOFTWARE, ZpAdministration_ControlSoftware },
    { ZP_CERTIFICATE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_CERTIFICATE_DATA,
      ZpAdministration_ControlCertificateData },
    { ZP_FIRMWARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_FIRMWARE_DATA,
      ZpAdministration_ControlFirmwareData },
    { ZP_NETWORK_SHARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_PUBLISHED_SHARE_SECURITY,
      ZpAdministration_ControlPublishedShareSecurity },
};

ZP_STATUS
ZpAdministration_Execute(
    _In_ BYTE ModuleId,
    _In_ BYTE OperationId,
    _In_reads_bytes_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_CONTROL_VIEW Control;
    ZP_ADMINISTRATION_DATA_CONTROL_VIEW DataControl;
    ZP_STRING_VIEW Identity;
    NTSTATUS Status;
    SIZE_T Index;

    for (Index = 0; Index < ARRAYSIZE(ZpEnumerateOperations); Index++)
    {
        const ZP_ENUMERATE_OPERATION* Operation = &ZpEnumerateOperations[Index];

        if (Operation->ModuleId == ModuleId && Operation->OperationId == OperationId)
        {
            return RequestLength == 0 ?
                       Operation->Routine(Response, ResponseLength) :
                       ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
        }
    }

    for (Index = 0; Index < ARRAYSIZE(ZpQueryOperations); Index++)
    {
        const ZP_QUERY_OPERATION* Operation = &ZpQueryOperations[Index];

        if (Operation->ModuleId == ModuleId && Operation->OperationId == OperationId)
        {
            Status = ZpAdministration_DecodeQuery(Request, RequestLength, &Identity);
            return NT_SUCCESS(Status) ?
                       Operation->Routine(&Identity, Response, ResponseLength) :
                       ZpStatus_FromNtStatus(Status);
        }
    }

    for (Index = 0; Index < ARRAYSIZE(ZpDataControlOperations); Index++)
    {
        const ZP_DATA_CONTROL_OPERATION* Operation = &ZpDataControlOperations[Index];

        if (Operation->ModuleId == ModuleId && Operation->OperationId == OperationId)
        {
            Status = ZpAdministration_DecodeDataControl(Request, RequestLength, &DataControl);
            return NT_SUCCESS(Status) ?
                       Operation->Routine(&DataControl) :
                       ZpStatus_FromNtStatus(Status);
        }
    }

    for (Index = 0; Index < ARRAYSIZE(ZpControlResultOperations); Index++)
    {
        const ZP_CONTROL_RESULT_OPERATION* Operation = &ZpControlResultOperations[Index];

        if (Operation->ModuleId == ModuleId && Operation->OperationId == OperationId)
        {
            Status = ZpAdministration_DecodeControl(Request, RequestLength, &Control);
            return NT_SUCCESS(Status) ?
                       Operation->Routine(&Control, Response, ResponseLength) :
                       ZpStatus_FromNtStatus(Status);
        }
    }

    for (Index = 0; Index < ARRAYSIZE(ZpControlOperations); Index++)
    {
        const ZP_CONTROL_OPERATION* Operation = &ZpControlOperations[Index];

        if (Operation->ModuleId == ModuleId && Operation->OperationId == OperationId)
        {
            Status = ZpAdministration_DecodeControl(Request, RequestLength, &Control);
            return NT_SUCCESS(Status) ?
                       Operation->Routine(&Control) :
                       ZpStatus_FromNtStatus(Status);
        }
    }

    return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
}
