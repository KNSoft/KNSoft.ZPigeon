#define COBJMACROS

#include "Client.h"

#include "ChildSession.h"
#include "ShadowCopy.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
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

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_USER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_HARDWARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UPDATE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_TASK_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIREWALL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_POWER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_ADMINISTRATION_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WLAN_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CERTIFICATE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIPBOARD_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CREDENTIAL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIRMWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_STATUS_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PAGE_FILE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BLUETOOTH_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_KEYBOARD_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_LOCATION_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FONT_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_APP_CONTAINER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINOBJ_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WSL_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UI_AUTOMATION_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PROXY_VPN_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIENT_STATUS_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BITLOCKER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SHADOW_COPY_)

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

#endif

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

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_USER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_HARDWARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UPDATE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_TASK_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIREWALL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_POWER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_ADMINISTRATION_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WLAN_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CERTIFICATE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIPBOARD_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CREDENTIAL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIRMWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_STATUS_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PAGE_FILE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BLUETOOTH_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_KEYBOARD_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_LOCATION_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FONT_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_APP_CONTAINER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINOBJ_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WSL_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UI_AUTOMATION_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PROXY_VPN_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIENT_STATUS_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BITLOCKER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SHADOW_COPY_)

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

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UPDATE_)

static ZP_STATUS
ZpAdministration_EnumerateAvailableUpdates(
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    return ZpAdministration_EnumerateUpdates(FALSE, Response, ResponseLength);
}

#endif

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_USER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_HARDWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UPDATE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_TASK_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIREWALL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_POWER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_ADMINISTRATION_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WLAN_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CERTIFICATE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIPBOARD_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CREDENTIAL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIRMWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_STATUS_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PAGE_FILE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BLUETOOTH_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FONT_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_APP_CONTAINER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WSL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PROXY_VPN_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIENT_STATUS_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SHADOW_COPY_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BITLOCKER_)

static const ZP_ENUMERATE_OPERATION ZpEnumerateOperations[] = {
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_USER_)
    { ZP_USER_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_USERS, ZpAdministration_EnumerateUsers },
    { ZP_USER_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_SESSIONS, ZpAdministration_EnumerateSessions },
    { ZP_USER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_LOGON_SESSIONS,
      ZpAdministration_EnumerateLogonSessions },
    { ZP_USER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_USER_PROFILES,
      ZpAdministration_EnumerateUserProfiles },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_)
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
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_HARDWARE_)
    { ZP_HARDWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_HARDWARE, ZpAdministration_EnumerateHardware },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UPDATE_)
    { ZP_UPDATE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_UPDATES, ZpAdministration_EnumerateAvailableUpdates },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_TASK_)
    { ZP_TASK_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_TASKS, ZpAdministration_EnumerateTasks },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIREWALL_)
    { ZP_FIREWALL_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_FIREWALL, ZpAdministration_EnumerateFirewall },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_POWER_)
    { ZP_POWER_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_POWER, ZpAdministration_EnumeratePower },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_ADMINISTRATION_)
    { ZP_SYSTEM_ADMINISTRATION_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_SYSTEM,
      ZpAdministration_EnumerateSystem },
    { ZP_SYSTEM_ADMINISTRATION_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_REMOTE_DESKTOP,
      ZpAdministration_EnumerateRemoteDesktop },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WLAN_)
    { ZP_WLAN_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_WLAN, ZpAdministration_EnumerateWlan },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CERTIFICATE_)
    { ZP_CERTIFICATE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_CERTIFICATES,
      ZpAdministration_EnumerateCertificates },
    { ZP_CERTIFICATE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_CERTIFICATE_STORES,
      ZpAdministration_EnumerateCertificateStores },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIPBOARD_)
    { ZP_CLIPBOARD_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_CLIPBOARD_IMAGE,
      ZpAdministration_QueryClipboardImage },
    { ZP_CLIPBOARD_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_CLIPBOARD,
      ZpAdministration_EnumerateClipboard },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CREDENTIAL_)
    { ZP_CREDENTIAL_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_CREDENTIALS,
      ZpAdministration_EnumerateCredentials },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIRMWARE_)
    { ZP_FIRMWARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_FIRMWARE_VARIABLES,
      ZpAdministration_EnumerateFirmwareVariables },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_)
    { ZP_NETWORK_SHARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_PUBLISHED_SHARES,
      ZpAdministration_EnumeratePublishedShares },
    { ZP_NETWORK_SHARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_NETWORK_CONNECTIONS,
      ZpAdministration_EnumerateNetworkConnections },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_STATUS_)
    { ZP_NETWORK_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_NETWORK_ADAPTERS,
      ZpAdministration_EnumerateNetworkAdapters },
    { ZP_NETWORK_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_NETWORK_ROUTES,
      ZpAdministration_EnumerateNetworkRoutes },
    { ZP_NETWORK_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_NETWORK_ENDPOINTS,
      ZpAdministration_EnumerateNetworkEndpoints },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PAGE_FILE_)
    { ZP_PAGE_FILE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_PAGE_FILES,
      ZpAdministration_EnumeratePageFiles },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BLUETOOTH_)
    { ZP_BLUETOOTH_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_BLUETOOTH,
      ZpAdministration_EnumerateBluetooth },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FONT_)
    { ZP_FONT_MODULE_ID, ZP_ADMINISTRATION_OPERATION_ENUMERATE_FONTS, ZpAdministration_EnumerateFonts },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_APP_CONTAINER_)
    { ZP_APP_CONTAINER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_APP_CONTAINERS,
      ZpAdministration_EnumerateAppContainers },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WSL_)
    { ZP_WSL_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_WSL_DISTRIBUTIONS,
      ZpAdministration_EnumerateWslDistributions },
    { ZP_WSL_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_WSL_PROCESSES,
      ZpAdministration_EnumerateWslProcesses },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PROXY_VPN_)
    { ZP_PROXY_VPN_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_PROXY_VPN,
      ZpAdministration_EnumerateProxyVpn },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIENT_STATUS_)
    { ZP_CLIENT_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_CLIENT_STATUS,
      ZpAdministration_EnumerateClientStatus },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SHADOW_COPY_)
    { ZP_SHADOW_COPY_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_SYSTEM_PROTECTION,
      ZpAdministration_EnumerateSystemProtection },
    { ZP_SHADOW_COPY_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_RESTORE_POINTS,
      ZpAdministration_EnumerateRestorePoints },
    { ZP_SHADOW_COPY_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_SHADOW_COPIES,
      ZpAdministration_EnumerateShadowCopies },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BITLOCKER_)
    { ZP_BITLOCKER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_BITLOCKER_VOLUMES,
      ZpAdministration_EnumerateBitLockerVolumes },
    { ZP_BITLOCKER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_ENUMERATE_BITLOCKER_PROTECTORS,
      ZpAdministration_EnumerateBitLockerProtectors },
#endif
};

#endif

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WLAN_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CERTIFICATE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIPBOARD_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CREDENTIAL_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIRMWARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_KEYBOARD_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_LOCATION_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINOBJ_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UI_AUTOMATION_)

static const ZP_QUERY_OPERATION ZpQueryOperations[] = {
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_)
    { ZP_SOFTWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_QUERY_PACKAGES, ZpAdministration_QueryPackages },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WLAN_)
    { ZP_WLAN_MODULE_ID, ZP_ADMINISTRATION_OPERATION_QUERY_WLAN_PROFILE, ZpAdministration_QueryWlanProfile },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CERTIFICATE_)
    { ZP_CERTIFICATE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_CERTIFICATE,
      ZpAdministration_QueryCertificate },
    { ZP_CERTIFICATE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_CERTIFICATE_DATA,
      ZpAdministration_QueryCertificateData },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIPBOARD_)
    { ZP_CLIPBOARD_MODULE_ID, ZP_ADMINISTRATION_OPERATION_WAIT_CLIPBOARD, ZpAdministration_WaitClipboard },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CREDENTIAL_)
    { ZP_CREDENTIAL_MODULE_ID, ZP_ADMINISTRATION_OPERATION_QUERY_CREDENTIAL, ZpAdministration_QueryCredential },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIRMWARE_)
    { ZP_FIRMWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_QUERY_FIRMWARE, ZpAdministration_QueryFirmware },
    { ZP_FIRMWARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_FIRMWARE_DATA,
      ZpAdministration_QueryFirmwareData },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_)
    { ZP_NETWORK_SHARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_PUBLISHED_SHARE,
      ZpAdministration_QueryPublishedShare },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_KEYBOARD_)
    { ZP_KEYBOARD_MODULE_ID, ZP_ADMINISTRATION_OPERATION_WAIT_KEYBOARD, ZpAdministration_WaitKeyboard },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_LOCATION_)
    { ZP_LOCATION_MODULE_ID, ZP_ADMINISTRATION_OPERATION_QUERY_LOCATION, ZpAdministration_QueryLocation },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINOBJ_)
    { ZP_WINOBJ_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_OBJECT_DIRECTORY,
      ZpAdministration_QueryObjectDirectory },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UI_AUTOMATION_)
    { ZP_UI_AUTOMATION_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_UI_AUTOMATION_CHILDREN,
      ZpAdministration_QueryUiAutomationChildren },
    { ZP_UI_AUTOMATION_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_QUERY_UI_AUTOMATION_PROPERTIES,
      ZpAdministration_QueryUiAutomationProperties },
#endif
};

#endif

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_USER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_HARDWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UPDATE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_TASK_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIREWALL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_POWER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_ADMINISTRATION_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WLAN_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIPBOARD_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CREDENTIAL_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_STATUS_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PAGE_FILE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BLUETOOTH_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FONT_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_APP_CONTAINER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WSL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PROXY_VPN_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SHADOW_COPY_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BITLOCKER_)

static const ZP_CONTROL_OPERATION ZpControlOperations[] = {
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_USER_)
    { ZP_USER_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_USER, ZpAdministration_ControlUser },
    { ZP_USER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_USER_PROFILE,
      ZpAdministration_ControlUserProfile },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_)
    { ZP_SOFTWARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_INPUT_METHOD,
      ZpAdministration_ControlInputMethod },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_HARDWARE_)
    { ZP_HARDWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_HARDWARE, ZpAdministration_ControlHardware },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UPDATE_)
    { ZP_UPDATE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_UPDATE, ZpAdministration_ControlUpdate },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_TASK_)
    { ZP_TASK_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_TASK, ZpAdministration_ControlTask },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIREWALL_)
    { ZP_FIREWALL_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_FIREWALL, ZpAdministration_ControlFirewall },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_POWER_)
    { ZP_POWER_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_POWER, ZpAdministration_ControlPower },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_ADMINISTRATION_)
    { ZP_SYSTEM_ADMINISTRATION_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_SYSTEM,
      ZpAdministration_ControlSystem },
    { ZP_SYSTEM_ADMINISTRATION_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_REMOTE_DESKTOP_CHILD_SESSION,
      ZpAdministration_ControlRemoteDesktopChildSession },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WLAN_)
    { ZP_WLAN_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_WLAN, ZpAdministration_ControlWlan },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIPBOARD_)
    { ZP_CLIPBOARD_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_CLIPBOARD,
      ZpAdministration_ControlClipboard },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CREDENTIAL_)
    { ZP_CREDENTIAL_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_CREDENTIAL,
      ZpAdministration_ControlCredential },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_)
    { ZP_NETWORK_SHARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_PUBLISHED_SHARE,
      ZpAdministration_ControlPublishedShare },
    { ZP_NETWORK_SHARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_NETWORK_CONNECTION,
      ZpAdministration_ControlNetworkConnection },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_STATUS_)
    { ZP_NETWORK_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_NETWORK_ADAPTER,
      ZpAdministration_ControlNetworkAdapter },
    { ZP_NETWORK_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_NETWORK_ROUTE,
      ZpAdministration_ControlNetworkRoute },
    { ZP_NETWORK_STATUS_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_NETWORK_ENDPOINT,
      ZpAdministration_ControlNetworkEndpoint },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PAGE_FILE_)
    { ZP_PAGE_FILE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_PAGE_FILE, ZpAdministration_ControlPageFile },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BLUETOOTH_)
    { ZP_BLUETOOTH_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_BLUETOOTH,
      ZpAdministration_ControlBluetooth },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FONT_)
    { ZP_FONT_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_FONT, ZpAdministration_ControlFont },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_APP_CONTAINER_)
    { ZP_APP_CONTAINER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_APP_CONTAINER,
      ZpAdministration_ControlAppContainer },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WSL_)
    { ZP_WSL_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_WSL_DISTRIBUTION,
      ZpAdministration_ControlWslDistribution },
    { ZP_WSL_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_WSL_PROCESS,
      ZpAdministration_ControlWslProcess },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PROXY_VPN_)
    { ZP_PROXY_VPN_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_PROXY_VPN,
      ZpAdministration_ControlProxyVpn },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SHADOW_COPY_)
    { ZP_SHADOW_COPY_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_SYSTEM_PROTECTION,
      ZpAdministration_ControlSystemProtection },
    { ZP_SHADOW_COPY_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_RESTORE_POINT,
      ZpAdministration_ControlRestorePoint },
    { ZP_SHADOW_COPY_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_SHADOW_COPY,
      ZpAdministration_ControlShadowCopy },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BITLOCKER_)
    { ZP_BITLOCKER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_BITLOCKER_VOLUME,
      ZpAdministration_ControlBitLockerVolume },
    { ZP_BITLOCKER_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_BITLOCKER_PROTECTOR,
      ZpAdministration_ControlBitLockerProtector },
#endif
};

#endif

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_)

static const ZP_CONTROL_RESULT_OPERATION ZpControlResultOperations[] = {
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_)
    { ZP_SOFTWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_FEATURE, ZpAdministration_ControlFeature },
#endif
};

#endif

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_ADMINISTRATION_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CERTIFICATE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIRMWARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_)

static const ZP_DATA_CONTROL_OPERATION ZpDataControlOperations[] = {
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_)
    { ZP_SOFTWARE_MODULE_ID, ZP_ADMINISTRATION_OPERATION_CONTROL_SOFTWARE, ZpAdministration_ControlSoftware },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_ADMINISTRATION_)
    { ZP_SYSTEM_ADMINISTRATION_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONFIGURE_REMOTE_DESKTOP,
      ZpAdministration_ConfigureRemoteDesktop },
    { ZP_SYSTEM_ADMINISTRATION_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_REMOTE_DESKTOP_PATCH,
      ZpAdministration_ControlRemoteDesktopPatch },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CERTIFICATE_)
    { ZP_CERTIFICATE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_CERTIFICATE_DATA,
      ZpAdministration_ControlCertificateData },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIRMWARE_)
    { ZP_FIRMWARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_FIRMWARE_DATA,
      ZpAdministration_ControlFirmwareData },
#endif
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_)
    { ZP_NETWORK_SHARE_MODULE_ID,
      ZP_ADMINISTRATION_OPERATION_CONTROL_PUBLISHED_SHARE_SECURITY,
      ZpAdministration_ControlPublishedShareSecurity },
#endif
};

#endif

ZP_STATUS
ZpAdministration_Execute(
    _In_ BYTE ModuleId,
    _In_ BYTE OperationId,
    _In_reads_bytes_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    SIZE_T Index;

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_USER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_HARDWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UPDATE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_TASK_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIREWALL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_POWER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_ADMINISTRATION_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WLAN_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CERTIFICATE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIPBOARD_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CREDENTIAL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIRMWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_STATUS_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PAGE_FILE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BLUETOOTH_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FONT_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_APP_CONTAINER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WSL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PROXY_VPN_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIENT_STATUS_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SHADOW_COPY_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BITLOCKER_)
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
#endif

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WLAN_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CERTIFICATE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIPBOARD_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CREDENTIAL_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIRMWARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_KEYBOARD_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_LOCATION_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WINOBJ_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UI_AUTOMATION_)
    for (Index = 0; Index < ARRAYSIZE(ZpQueryOperations); Index++)
    {
        const ZP_QUERY_OPERATION* Operation = &ZpQueryOperations[Index];

        if (Operation->ModuleId == ModuleId && Operation->OperationId == OperationId)
        {
            ZP_STRING_VIEW Identity;
            NTSTATUS Status;

            Status = ZpAdministration_DecodeQuery(Request, RequestLength, &Identity);
            return NT_SUCCESS(Status) ?
                       Operation->Routine(&Identity, Response, ResponseLength) :
                       ZpStatus_FromNtStatus(Status);
        }
    }
#endif

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_ADMINISTRATION_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CERTIFICATE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIRMWARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_)
    for (Index = 0; Index < ARRAYSIZE(ZpDataControlOperations); Index++)
    {
        const ZP_DATA_CONTROL_OPERATION* Operation = &ZpDataControlOperations[Index];

        if (Operation->ModuleId == ModuleId && Operation->OperationId == OperationId)
        {
            ZP_ADMINISTRATION_DATA_CONTROL_VIEW DataControl;
            NTSTATUS Status;

            Status = ZpAdministration_DecodeDataControl(Request, RequestLength, &DataControl);
            return NT_SUCCESS(Status) ?
                       Operation->Routine(&DataControl) :
                       ZpStatus_FromNtStatus(Status);
        }
    }
#endif

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_)
    for (Index = 0; Index < ARRAYSIZE(ZpControlResultOperations); Index++)
    {
        const ZP_CONTROL_RESULT_OPERATION* Operation = &ZpControlResultOperations[Index];

        if (Operation->ModuleId == ModuleId && Operation->OperationId == OperationId)
        {
            ZP_ADMINISTRATION_CONTROL_VIEW Control;
            NTSTATUS Status;

            Status = ZpAdministration_DecodeControl(Request, RequestLength, &Control);
            return NT_SUCCESS(Status) ?
                       Operation->Routine(&Control, Response, ResponseLength) :
                       ZpStatus_FromNtStatus(Status);
        }
    }
#endif

#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_USER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SOFTWARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_HARDWARE_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_UPDATE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_TASK_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FIREWALL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_POWER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SYSTEM_ADMINISTRATION_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WLAN_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CLIPBOARD_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_CREDENTIAL_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_SHARE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_NETWORK_STATUS_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PAGE_FILE_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BLUETOOTH_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_FONT_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_APP_CONTAINER_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_WSL_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_PROXY_VPN_) || \
    defined(_KNSOFT_ZPIGEON_BUILD_MODULE_SHADOW_COPY_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BITLOCKER_)
    for (Index = 0; Index < ARRAYSIZE(ZpControlOperations); Index++)
    {
        const ZP_CONTROL_OPERATION* Operation = &ZpControlOperations[Index];

        if (Operation->ModuleId == ModuleId && Operation->OperationId == OperationId)
        {
            ZP_ADMINISTRATION_CONTROL_VIEW Control;
            NTSTATUS Status;

            Status = ZpAdministration_DecodeControl(Request, RequestLength, &Control);
            return NT_SUCCESS(Status) ?
                       Operation->Routine(&Control) :
                       ZpStatus_FromNtStatus(Status);
        }
    }
#endif

    return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
}

#endif
