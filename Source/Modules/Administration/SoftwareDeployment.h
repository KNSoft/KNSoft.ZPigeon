#pragma once

#include <KNSoft/ZPigeon/Administration.h>

EXTERN_C_START

typedef struct _ZP_SOFTWARE_PACKAGE_INFO
{
    ULONG Provider;
    PCWSTR Identity;
    PCWSTR Name;
    PCWSTR Version;
    PCWSTR Source;
} ZP_SOFTWARE_PACKAGE_INFO, *PZP_SOFTWARE_PACKAGE_INFO;

typedef const ZP_SOFTWARE_PACKAGE_INFO* PCZP_SOFTWARE_PACKAGE_INFO;

typedef struct _ZP_SOFTWARE_PACKAGE_PROVIDER_INFO
{
    ULONG Provider;
    ULONG Capabilities;
    PCWSTR Identity;
    PCWSTR Name;
    PCWSTR RuntimeVersion;
    PCWSTR ManagerVersion;
} ZP_SOFTWARE_PACKAGE_PROVIDER_INFO, *PZP_SOFTWARE_PACKAGE_PROVIDER_INFO;

typedef const ZP_SOFTWARE_PACKAGE_PROVIDER_INFO* PCZP_SOFTWARE_PACKAGE_PROVIDER_INFO;

typedef struct _ZP_SOFTWARE_DEPLOYMENT_INFO
{
    GUID Id;
    PCWSTR Name;
    PCWSTR Identity;
    PCWSTR ErrorText;
    ULONG State;
    ULONG Flags;
    ULONGLONG Result;
} ZP_SOFTWARE_DEPLOYMENT_INFO, *PZP_SOFTWARE_DEPLOYMENT_INFO;

typedef const ZP_SOFTWARE_DEPLOYMENT_INFO* PCZP_SOFTWARE_DEPLOYMENT_INFO;

typedef BOOL (NTAPI *ZP_SOFTWARE_PACKAGE_CALLBACK)(
    _In_ PCZP_SOFTWARE_PACKAGE_INFO Package,
    _In_opt_ PVOID Context);

typedef BOOL (NTAPI *ZP_SOFTWARE_PACKAGE_PROVIDER_CALLBACK)(
    _In_ PCZP_SOFTWARE_PACKAGE_PROVIDER_INFO Provider,
    _In_opt_ PVOID Context);

typedef BOOL (NTAPI *ZP_SOFTWARE_DEPLOYMENT_CALLBACK)(
    _In_ PCZP_SOFTWARE_DEPLOYMENT_INFO Deployment,
    _In_opt_ PVOID Context);

HRESULT
ZpSoftware_EnumeratePackageProviders(
    _In_ ZP_SOFTWARE_PACKAGE_PROVIDER_CALLBACK Callback,
    _In_opt_ PVOID Context);

HRESULT
ZpSoftware_EnumeratePackages(
    _In_ PCWSTR Provider,
    _In_ ZP_SOFTWARE_PACKAGE_CALLBACK Callback,
    _In_opt_ PVOID Context);

HRESULT
ZpSoftware_StartDeployment(
    _In_ ZP_ADMINISTRATION_ACTION Action,
    _In_ ULONG Flags,
    _In_ const GUID* Id,
    _In_reads_(PayloadLength) PCWCH Payload,
    _In_ ULONG PayloadLength);

HRESULT
ZpSoftware_EnumerateDeployments(
    _In_ ZP_SOFTWARE_DEPLOYMENT_CALLBACK Callback,
    _In_opt_ PVOID Context);

EXTERN_C_END
