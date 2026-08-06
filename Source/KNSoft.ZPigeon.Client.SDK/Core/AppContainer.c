#include "AppContainer.h"

#include <sddl.h>

#pragma comment(lib, "Advapi32.lib")

typedef union _ZP_APP_CONTAINER_NETWORK_PROC
{
    FARPROC Raw;
    ZP_NETWORK_ISOLATION_ENUM Enumerate;
    ZP_NETWORK_ISOLATION_FREE Free;
    ZP_NETWORK_ISOLATION_GET Get;
    ZP_NETWORK_ISOLATION_SET Set;
} ZP_APP_CONTAINER_NETWORK_PROC;

NTSTATUS
ZpAppContainer_LoadNetworkApi(
    _Out_ PZP_APP_CONTAINER_NETWORK_API Api)
{
    ZP_APP_CONTAINER_NETWORK_PROC Proc;

    Api->Module = LoadLibraryExW(L"FirewallAPI.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (Api->Module == NULL) return NTSTATUS_FROM_WIN32(GetLastError());
#define ZP_APP_CONTAINER_GET_PROC(Field, Name) \
    Proc.Raw = GetProcAddress(Api->Module, "NetworkIsolation" #Name); \
    Api->Field = Proc.Field; \
    if (Api->Field == NULL) \
    { \
        FreeLibrary(Api->Module); \
        return STATUS_ENTRYPOINT_NOT_FOUND; \
    }
    ZP_APP_CONTAINER_GET_PROC(Enumerate, EnumAppContainers);
    ZP_APP_CONTAINER_GET_PROC(Free, FreeAppContainers);
    ZP_APP_CONTAINER_GET_PROC(Get, GetAppContainerConfig);
    ZP_APP_CONTAINER_GET_PROC(Set, SetAppContainerConfig);
#undef ZP_APP_CONTAINER_GET_PROC
    return STATUS_SUCCESS;
}

VOID
ZpAppContainer_FreeNetworkConfig(
    _In_reads_opt_(Count) PSID_AND_ATTRIBUTES Entries,
    _In_ ULONG Count)
{
    ULONG Index;

    if (Entries == NULL) return;
    for (Index = 0; Index < Count; Index++) HeapFree(GetProcessHeap(), 0, Entries[Index].Sid);
    HeapFree(GetProcessHeap(), 0, Entries);
}

ZP_STATUS
ZpAppContainer_QuerySecurityCapabilities(
    _In_ PCWSTR SidString,
    _Out_ PZP_APP_CONTAINER_SECURITY_CONTEXT Context)
{
    ZP_APP_CONTAINER_NETWORK_API Api;
    PINET_FIREWALL_APP_CONTAINER Profiles;
    PSID Sid;
    ULONG Count, Index;
    DWORD Error;
    NTSTATUS Status;

    if (!ConvertStringSidToSidW(SidString, &Sid)) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    Status = ZpAppContainer_LoadNetworkApi(&Api);
    if (!NT_SUCCESS(Status))
    {
        LocalFree(Sid);
        return ZpStatus_FromNtStatus(Status);
    }
    Error = Api.Enumerate(0, &Count, &Profiles);
    if (Error != ERROR_SUCCESS)
    {
        FreeLibrary(Api.Module);
        LocalFree(Sid);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    for (Index = 0; Index < Count; Index++)
    {
        if (Profiles[Index].appContainerSid == NULL || !RtlValidSid(Profiles[Index].appContainerSid) ||
            !RtlEqualSid(Sid, Profiles[Index].appContainerSid))
        {
            continue;
        }
        Context->Api = Api;
        Context->Profiles = Profiles;
        Context->Capabilities.AppContainerSid = Profiles[Index].appContainerSid;
        Context->Capabilities.Capabilities = Profiles[Index].capabilities.capabilities;
        Context->Capabilities.CapabilityCount = Profiles[Index].capabilities.count;
        Context->Capabilities.Reserved = 0;
        LocalFree(Sid);
        return ZpStatus_Make(ZpStatusNone, 0);
    }
    Api.Free(Profiles);
    FreeLibrary(Api.Module);
    LocalFree(Sid);
    return ZpStatus_FromNtStatus(STATUS_NOT_FOUND);
}

VOID
ZpAppContainer_FreeSecurityCapabilities(
    _In_ PZP_APP_CONTAINER_SECURITY_CONTEXT Context)
{
    Context->Api.Free(Context->Profiles);
    FreeLibrary(Context->Api.Module);
}
