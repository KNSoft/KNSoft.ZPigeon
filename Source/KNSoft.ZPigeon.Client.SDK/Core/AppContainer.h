#pragma once

#include <KNSoft/ZPigeon/Protocol.h>
#include <netfw.h>

typedef DWORD (WINAPI *ZP_NETWORK_ISOLATION_ENUM)(
    _In_ DWORD Flags,
    _Out_ PDWORD Count,
    _Outptr_result_buffer_(*Count) PINET_FIREWALL_APP_CONTAINER* Profiles);
typedef DWORD (WINAPI *ZP_NETWORK_ISOLATION_FREE)(_In_ PINET_FIREWALL_APP_CONTAINER Profiles);
typedef DWORD (WINAPI *ZP_NETWORK_ISOLATION_GET)(
    _Out_ PDWORD Count,
    _Outptr_result_buffer_(*Count) PSID_AND_ATTRIBUTES* Entries);
typedef DWORD (WINAPI *ZP_NETWORK_ISOLATION_SET)(
    _In_ DWORD Count,
    _In_reads_(Count) PSID_AND_ATTRIBUTES Entries);

typedef struct _ZP_APP_CONTAINER_NETWORK_API
{
    HMODULE Module;
    ZP_NETWORK_ISOLATION_ENUM Enumerate;
    ZP_NETWORK_ISOLATION_FREE Free;
    ZP_NETWORK_ISOLATION_GET Get;
    ZP_NETWORK_ISOLATION_SET Set;
} ZP_APP_CONTAINER_NETWORK_API, *PZP_APP_CONTAINER_NETWORK_API;

typedef struct _ZP_APP_CONTAINER_SECURITY_CONTEXT
{
    ZP_APP_CONTAINER_NETWORK_API Api;
    PINET_FIREWALL_APP_CONTAINER Profiles;
    SECURITY_CAPABILITIES Capabilities;
} ZP_APP_CONTAINER_SECURITY_CONTEXT, *PZP_APP_CONTAINER_SECURITY_CONTEXT;

NTSTATUS
ZpAppContainer_LoadNetworkApi(
    _Out_ PZP_APP_CONTAINER_NETWORK_API Api);

VOID
ZpAppContainer_FreeNetworkConfig(
    _In_reads_opt_(Count) PSID_AND_ATTRIBUTES Entries,
    _In_ ULONG Count);

ZP_STATUS
ZpAppContainer_QuerySecurityCapabilities(
    _In_ PCWSTR SidString,
    _Out_ PZP_APP_CONTAINER_SECURITY_CONTEXT Context);

VOID
ZpAppContainer_FreeSecurityCapabilities(
    _In_ PZP_APP_CONTAINER_SECURITY_CONTEXT Context);
