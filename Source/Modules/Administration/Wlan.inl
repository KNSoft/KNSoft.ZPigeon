#include <wlanapi.h>

#define ZP_WLAN_NETWORK_CONNECTED 0x00000001
#define ZP_WLAN_NETWORK_HAS_PROFILE 0x00000002
#define ZP_WLAN_NETWORK_CONNECTABLE 0x00000004
#define ZP_WLAN_NETWORK_SECURE 0x00000008
#define ZP_WLAN_NETWORK_PLAINTEXT_SUPPORTED 0x00000010

typedef DWORD (WINAPI* ZP_WLAN_OPEN_HANDLE)(DWORD, PVOID, PDWORD, PHANDLE);
typedef DWORD (WINAPI* ZP_WLAN_CLOSE_HANDLE)(HANDLE, PVOID);
typedef DWORD (WINAPI* ZP_WLAN_ENUM_INTERFACES)(HANDLE, PVOID, PWLAN_INTERFACE_INFO_LIST*);
typedef DWORD (WINAPI* ZP_WLAN_GET_NETWORKS)(HANDLE, const GUID*, DWORD, PVOID, PWLAN_AVAILABLE_NETWORK_LIST*);
typedef DWORD (WINAPI* ZP_WLAN_GET_PROFILES)(HANDLE, const GUID*, PVOID, PWLAN_PROFILE_INFO_LIST*);
typedef DWORD (WINAPI* ZP_WLAN_GET_PROFILE)(HANDLE, const GUID*, LPCWSTR, PVOID, LPWSTR*, PDWORD, PDWORD);
typedef DWORD (WINAPI* ZP_WLAN_CONNECT)(HANDLE, const GUID*, const WLAN_CONNECTION_PARAMETERS*, PVOID);
typedef DWORD (WINAPI* ZP_WLAN_DISCONNECT)(HANDLE, const GUID*, PVOID);
typedef DWORD (WINAPI* ZP_WLAN_DELETE_PROFILE)(HANDLE, const GUID*, LPCWSTR, PVOID);
typedef VOID (WINAPI* ZP_WLAN_FREE_MEMORY)(PVOID);

typedef struct _ZP_WLAN_API
{
    HMODULE Module;
    ZP_WLAN_OPEN_HANDLE OpenHandle;
    ZP_WLAN_CLOSE_HANDLE CloseHandle;
    ZP_WLAN_ENUM_INTERFACES EnumInterfaces;
    ZP_WLAN_GET_NETWORKS GetNetworks;
    ZP_WLAN_GET_PROFILES GetProfiles;
    ZP_WLAN_GET_PROFILE GetProfile;
    ZP_WLAN_CONNECT Connect;
    ZP_WLAN_DISCONNECT Disconnect;
    ZP_WLAN_DELETE_PROFILE DeleteProfile;
    ZP_WLAN_FREE_MEMORY FreeMemory;
} ZP_WLAN_API, *PZP_WLAN_API;

static
DWORD
ZpAdministration_LoadWlan(
    _Out_ PZP_WLAN_API Api)
{
    Api->Module = LoadLibraryExW(L"wlanapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (Api->Module == NULL) return GetLastError();
    Api->OpenHandle = (ZP_WLAN_OPEN_HANDLE)GetProcAddress(Api->Module, "WlanOpenHandle");
    Api->CloseHandle = (ZP_WLAN_CLOSE_HANDLE)GetProcAddress(Api->Module, "WlanCloseHandle");
    Api->EnumInterfaces = (ZP_WLAN_ENUM_INTERFACES)GetProcAddress(Api->Module, "WlanEnumInterfaces");
    Api->GetNetworks = (ZP_WLAN_GET_NETWORKS)GetProcAddress(Api->Module, "WlanGetAvailableNetworkList");
    Api->GetProfiles = (ZP_WLAN_GET_PROFILES)GetProcAddress(Api->Module, "WlanGetProfileList");
    Api->GetProfile = (ZP_WLAN_GET_PROFILE)GetProcAddress(Api->Module, "WlanGetProfile");
    Api->Connect = (ZP_WLAN_CONNECT)GetProcAddress(Api->Module, "WlanConnect");
    Api->Disconnect = (ZP_WLAN_DISCONNECT)GetProcAddress(Api->Module, "WlanDisconnect");
    Api->DeleteProfile = (ZP_WLAN_DELETE_PROFILE)GetProcAddress(Api->Module, "WlanDeleteProfile");
    Api->FreeMemory = (ZP_WLAN_FREE_MEMORY)GetProcAddress(Api->Module, "WlanFreeMemory");
    if (Api->OpenHandle != NULL && Api->CloseHandle != NULL && Api->EnumInterfaces != NULL &&
        Api->GetNetworks != NULL && Api->GetProfiles != NULL && Api->GetProfile != NULL &&
        Api->Connect != NULL && Api->Disconnect != NULL && Api->DeleteProfile != NULL && Api->FreeMemory != NULL)
    {
        return ERROR_SUCCESS;
    }
    FreeLibrary(Api->Module);
    return ERROR_PROC_NOT_FOUND;
}

static
VOID
ZpAdministration_WlanSsid(
    _In_ const DOT11_SSID* Ssid,
    _Out_writes_(CharacterCount) PWSTR Buffer,
    _In_ ULONG CharacterCount)
{
    static const WCHAR Hex[] = L"0123456789ABCDEF";
    INT Length;
    ULONG Index;

    if (Ssid->uSSIDLength == 0)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }
    Length = MultiByteToWideChar(CP_UTF8,
                                 MB_ERR_INVALID_CHARS,
                                 (PCCH)Ssid->ucSSID,
                                 Ssid->uSSIDLength,
                                 Buffer,
                                 CharacterCount - 1);
    if (Length != 0)
    {
        Buffer[Length] = UNICODE_NULL;
        for (Index = 0; Index < (ULONG)Length; Index++)
        {
            if (Buffer[Index] < L' ' || Buffer[Index] == 0x7f) break;
        }
        if (Index == (ULONG)Length) return;
    }
    Buffer[0] = L'0';
    Buffer[1] = L'x';
    for (Index = 0; Index < Ssid->uSSIDLength; Index++)
    {
        Buffer[Index * 2 + 2] = Hex[Ssid->ucSSID[Index] >> 4];
        Buffer[Index * 2 + 3] = Hex[Ssid->ucSSID[Index] & 0x0f];
    }
    Buffer[Ssid->uSSIDLength * 2 + 2] = UNICODE_NULL;
}

static
NTSTATUS
ZpAdministration_AddWlanNetworks(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PZP_WLAN_API Api,
    _In_ HANDLE Handle,
    _In_ const WLAN_INTERFACE_INFO* Interface,
    _In_ PCUNICODE_STRING InterfaceIdentity,
    _Out_ PDWORD WlanError)
{
    PWLAN_AVAILABLE_NETWORK_LIST Networks;
    WCHAR Identity[WLAN_MAX_NAME_LENGTH + 48];
    WCHAR Name[DOT11_SSID_MAX_LENGTH * 2 + 3];
    ULONG Index, Match, Flags;
    NTSTATUS Status = STATUS_SUCCESS;

    *WlanError = Api->GetNetworks(Handle, &Interface->InterfaceGuid, 0, NULL, &Networks);
    if (*WlanError != ERROR_SUCCESS) return STATUS_UNSUCCESSFUL;
    for (Index = 0; Index < Networks->dwNumberOfItems && NT_SUCCESS(Status); Index++)
    {
        const WLAN_AVAILABLE_NETWORK* Network = &Networks->Network[Index];

        if (!(Network->dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE))
        {
            for (Match = 0; Match < Networks->dwNumberOfItems; Match++)
            {
                const WLAN_AVAILABLE_NETWORK* Candidate = &Networks->Network[Match];

                if (Candidate->dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE &&
                    Candidate->dot11Ssid.uSSIDLength == Network->dot11Ssid.uSSIDLength &&
                    Candidate->dot11DefaultAuthAlgorithm == Network->dot11DefaultAuthAlgorithm &&
                    Candidate->dot11DefaultCipherAlgorithm == Network->dot11DefaultCipherAlgorithm &&
                    RtlEqualMemory(Candidate->dot11Ssid.ucSSID,
                                   Network->dot11Ssid.ucSSID,
                                   Network->dot11Ssid.uSSIDLength))
                {
                    break;
                }
            }
            if (Match != Networks->dwNumberOfItems) continue;
        }
        ZpAdministration_WlanSsid(&Network->dot11Ssid, Name, ARRAYSIZE(Name));
        _snwprintf_s(Identity,
                     ARRAYSIZE(Identity),
                     _TRUNCATE,
                     L"%s|%s",
                     InterfaceIdentity->Buffer,
                     Network->strProfileName[0] != UNICODE_NULL ? Network->strProfileName : Name);
        Flags = (Network->dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED ? ZP_WLAN_NETWORK_CONNECTED : 0) |
                (Network->dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE ? ZP_WLAN_NETWORK_HAS_PROFILE : 0) |
                (Network->bNetworkConnectable ? ZP_WLAN_NETWORK_CONNECTABLE : 0) |
                (Network->bSecurityEnabled ? ZP_WLAN_NETWORK_SECURE : 0) |
                (Api->GetProfile != NULL ? ZP_WLAN_NETWORK_PLAINTEXT_SUPPORTED : 0);
        Status = ZpAdministration_AddRecord(
            Builder,
            ZpAdministrationKindWlanNetwork,
            Network->wlanSignalQuality,
            Flags,
            ((ULONGLONG)Network->dot11DefaultAuthAlgorithm << 32) | Network->dot11DefaultCipherAlgorithm,
            Identity,
            Name,
            Interface->strInterfaceDescription,
            Network->strProfileName);
    }
    Api->FreeMemory(Networks);
    return Status;
}

static
NTSTATUS
ZpAdministration_AddWlanProfiles(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PZP_WLAN_API Api,
    _In_ HANDLE Handle,
    _In_ const WLAN_INTERFACE_INFO* Interface,
    _In_ PCUNICODE_STRING InterfaceIdentity,
    _Out_ PDWORD WlanError)
{
    PWLAN_PROFILE_INFO_LIST Profiles;
    WCHAR Identity[WLAN_MAX_NAME_LENGTH + 48];
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;

    *WlanError = Api->GetProfiles(Handle, &Interface->InterfaceGuid, NULL, &Profiles);
    if (*WlanError != ERROR_SUCCESS) return STATUS_UNSUCCESSFUL;
    for (Index = 0; Index < Profiles->dwNumberOfItems && NT_SUCCESS(Status); Index++)
    {
        const WLAN_PROFILE_INFO* Profile = &Profiles->ProfileInfo[Index];

        _snwprintf_s(Identity,
                     ARRAYSIZE(Identity),
                     _TRUNCATE,
                     L"%s|%s",
                     InterfaceIdentity->Buffer,
                     Profile->strProfileName);
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindWlanProfile,
                                             0,
                                             Profile->dwFlags,
                                             0,
                                             Identity,
                                             Profile->strProfileName,
                                             Interface->strInterfaceDescription,
                                             NULL);
    }
    Api->FreeMemory(Profiles);
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateWlan(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_WLAN_API Api;
    PWLAN_INTERFACE_INFO_LIST Interfaces;
    UNICODE_STRING Identity;
    HANDLE Handle;
    DWORD Version, Error, CleanupError;
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;

    Error = ZpAdministration_LoadWlan(&Api);
    if (Error != ERROR_SUCCESS) return ZpStatus_FromCode(ZpStatusWin32, Error);
    Error = Api.OpenHandle(2, NULL, &Version, &Handle);
    if (Error == ERROR_SERVICE_NOT_ACTIVE)
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
        ZpAdministration_FreeBuilder(&Builder);
        FreeLibrary(Api.Module);
        return ZpStatus_FromNtStatus(Status);
    }
    if (Error != ERROR_SUCCESS) goto CleanupModule;
    Error = Api.EnumInterfaces(Handle, NULL, &Interfaces);
    if (Error != ERROR_SUCCESS) goto CleanupHandle;
    for (Index = 0; Index < Interfaces->dwNumberOfItems && NT_SUCCESS(Status); Index++)
    {
        const WLAN_INTERFACE_INFO* Interface = &Interfaces->InterfaceInfo[Index];

        Status = RtlStringFromGUID(&Interface->InterfaceGuid, &Identity);
        if (!NT_SUCCESS(Status)) break;
        Status = ZpAdministration_AddRecord(&Builder,
                                            ZpAdministrationKindWlanInterface,
                                            Interface->isState,
                                            0,
                                            0,
                                            Identity.Buffer,
                                            Interface->strInterfaceDescription,
                                            NULL,
                                            NULL);
        if (NT_SUCCESS(Status)) Status = ZpAdministration_AddWlanNetworks(&Builder, &Api, Handle, Interface,
                                                                          &Identity, &Error);
        if (NT_SUCCESS(Status)) Status = ZpAdministration_AddWlanProfiles(&Builder, &Api, Handle, Interface,
                                                                          &Identity, &Error);
        RtlFreeUnicodeString(&Identity);
        if (Error != ERROR_SUCCESS) break;
    }
    Api.FreeMemory(Interfaces);
    if (NT_SUCCESS(Status) && Error == ERROR_SUCCESS)
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }
    ZpAdministration_FreeBuilder(&Builder);
CleanupHandle:
    CleanupError = Api.CloseHandle(Handle, NULL);
    if (Error == ERROR_SUCCESS && CleanupError != ERROR_SUCCESS) Error = CleanupError;
CleanupModule:
    FreeLibrary(Api.Module);
    if (Error != ERROR_SUCCESS) return ZpStatus_FromCode(ZpStatusWin32, Error);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_QueryWlanProfile(
    _In_ PCZP_STRING_VIEW IdentityView,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_WLAN_API Api;
    UNICODE_STRING GuidString;
    PWSTR Identity, Profile, Xml;
    HANDLE Handle;
    GUID InterfaceGuid;
    DWORD Version, Flags = WLAN_PROFILE_GET_PLAINTEXT_KEY, Access, Error, CleanupError;
    NTSTATUS Status;

    Identity = ZpAdministration_CopyView(IdentityView);
    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Profile = wcschr(Identity, L'|');
    if (Profile != NULL) *Profile++ = UNICODE_NULL;
    RtlInitUnicodeString(&GuidString, Identity);
    Status = RtlGUIDFromString(&GuidString, &InterfaceGuid);
    if (!NT_SUCCESS(Status) || Profile == NULL || *Profile == UNICODE_NULL)
    {
        Mem_Free(Identity);
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Error = ZpAdministration_LoadWlan(&Api);
    if (Error != ERROR_SUCCESS) goto CleanupIdentity;
    if (Api.GetProfile == NULL)
    {
        Error = ERROR_NOT_SUPPORTED;
        goto CleanupModule;
    }
    Error = Api.OpenHandle(2, NULL, &Version, &Handle);
    if (Error != ERROR_SUCCESS) goto CleanupModule;
    Error = Api.GetProfile(Handle, &InterfaceGuid, Profile, NULL, &Xml, &Flags, &Access);
    if (Error == ERROR_SUCCESS)
    {
        Profile[-1] = L'|';
        Status = ZpAdministration_AddRecord(&Builder,
                                             ZpAdministrationKindWlanProfile,
                                             0,
                                             0,
                                             0,
                                             Identity,
                                             Profile,
                                             NULL,
                                             Xml);
        if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
        if (!NT_SUCCESS(Status)) Error = RtlNtStatusToDosError(Status);
        Api.FreeMemory(Xml);
    }
    ZpAdministration_FreeBuilder(&Builder);
    CleanupError = Api.CloseHandle(Handle, NULL);
    if (Error == ERROR_SUCCESS && CleanupError != ERROR_SUCCESS) Error = CleanupError;
CleanupModule:
    FreeLibrary(Api.Module);
CleanupIdentity:
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusWin32, Error);
}

static
ZP_STATUS
ZpAdministration_ControlWlan(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    ZP_WLAN_API Api;
    WLAN_CONNECTION_PARAMETERS Parameters;
    UNICODE_STRING GuidString;
    PWSTR Identity, Profile;
    HANDLE Handle;
    GUID InterfaceGuid;
    DWORD Version, Error, CleanupError;
    NTSTATUS Status;

    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    if (wcslen(Identity) != Control->Identity.Length)
    {
        Mem_Free(Identity);
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Profile = wcschr(Identity, L'|');
    if (Profile != NULL) *Profile++ = UNICODE_NULL;
    RtlInitUnicodeString(&GuidString, Identity);
    Status = RtlGUIDFromString(&GuidString, &InterfaceGuid);
    if (!NT_SUCCESS(Status) ||
        ((Control->Action == ZpAdministrationActionConnect || Control->Action == ZpAdministrationActionDelete) &&
         (Profile == NULL || *Profile == UNICODE_NULL)))
    {
        Mem_Free(Identity);
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Error = ZpAdministration_LoadWlan(&Api);
    if (Error != ERROR_SUCCESS) goto CleanupIdentity;
    Error = Api.OpenHandle(2, NULL, &Version, &Handle);
    if (Error != ERROR_SUCCESS) goto CleanupModule;
    switch (Control->Action)
    {
        case ZpAdministrationActionConnect:
            RtlZeroMemory(&Parameters, sizeof(Parameters));
            Parameters.wlanConnectionMode = wlan_connection_mode_profile;
            Parameters.strProfile = Profile;
            Parameters.dot11BssType = dot11_BSS_type_any;
            Error = Api.Connect(Handle, &InterfaceGuid, &Parameters, NULL);
            break;

        case ZpAdministrationActionDisconnect:
            Error = Api.Disconnect(Handle, &InterfaceGuid, NULL);
            break;

        case ZpAdministrationActionDelete:
            Error = Api.DeleteProfile(Handle, &InterfaceGuid, Profile, NULL);
            break;

        default:
            Error = ERROR_NOT_SUPPORTED;
            break;
    }
    CleanupError = Api.CloseHandle(Handle, NULL);
    if (Error == ERROR_SUCCESS && CleanupError != ERROR_SUCCESS) Error = CleanupError;
CleanupModule:
    FreeLibrary(Api.Module);
CleanupIdentity:
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusWin32, Error);
}
