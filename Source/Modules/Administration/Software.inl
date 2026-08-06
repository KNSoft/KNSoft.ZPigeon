#include <KNSoft/MakeLifeEasier/System/Registry.h>

#include <KNSoft/CBS/CBS.h>

#include "SoftwareDeployment.h"

static
PWSTR
ZpAdministration_QueryRegistryString(
    _In_ HANDLE Key,
    _In_ PCUNICODE_STRING Name)
{
    PKEY_VALUE_PARTIAL_INFORMATION Data;
    PWSTR Value;
    ULONG Length;

    if (!NT_SUCCESS(Sys_RegQueryData(Key, Name, &Data))) return NULL;
    if ((Data->Type != REG_SZ && Data->Type != REG_EXPAND_SZ) || Data->DataLength % sizeof(WCHAR) != 0)
    {
        Mem_Free(Data);
        return NULL;
    }
    Length = Data->DataLength / sizeof(WCHAR);
    while (Length != 0 && ((PCWCHAR)Data->Data)[Length - 1] == UNICODE_NULL) Length--;
    Value = Mem_Alloc(((SIZE_T)Length + 1) * sizeof(WCHAR));
    if (Value != NULL)
    {
        RtlCopyMemory(Value, Data->Data, (SIZE_T)Length * sizeof(WCHAR));
        Value[Length] = UNICODE_NULL;
    }
    Mem_Free(Data);
    return Value;
}

static
NTSTATUS
ZpAdministration_EnumerateSoftwareKey(
    _In_ HANDLE Root,
    _In_ PCUNICODE_STRING Path,
    _In_ ZP_ADMINISTRATION_KIND Kind,
    _In_ ULONG Flags,
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    static const UNICODE_STRING DisplayName = RTL_CONSTANT_STRING(L"DisplayName");
    static const UNICODE_STRING Publisher = RTL_CONSTANT_STRING(L"Publisher");
    static const UNICODE_STRING DisplayVersion = RTL_CONSTANT_STRING(L"DisplayVersion");
    KEY_CACHED_INFORMATION Cached;
    PKEY_BASIC_INFORMATION Information;
    UNICODE_STRING Name;
    HANDLE Key, Child;
    PWSTR Identity, Title, Description, Detail;
    ULONG Index, Length, InformationLength;
    NTSTATUS Status, ChildStatus;

    Status = Sys_RegOpenKeyEx(&Key,
                              Root,
                              KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS,
                              Path);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND) return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryKey(Key, KeyCachedInformation, &Cached, sizeof(Cached), &Length);
    if (!NT_SUCCESS(Status)) goto CleanupKey;
    InformationLength = FIELD_OFFSET(KEY_BASIC_INFORMATION, Name) + Cached.MaxNameLength;
    Information = Mem_Alloc(InformationLength);
    if (Information == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto CleanupKey;
    }
    for (Index = 0; NT_SUCCESS(Status); Index++)
    {
        Status = NtEnumerateKey(Key, Index, KeyBasicInformation, Information, InformationLength, &Length);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status)) break;
        Name.Buffer = Information->Name;
        Name.Length = (USHORT)Information->NameLength;
        Name.MaximumLength = Name.Length;
        ChildStatus = Sys_RegOpenKeyEx(&Child, Key, KEY_QUERY_VALUE, &Name);
        if (!NT_SUCCESS(ChildStatus)) continue;
        Identity = Mem_Alloc((SIZE_T)Name.Length + sizeof(WCHAR));
        if (Identity == NULL)
        {
            NtClose(Child);
            Status = STATUS_NO_MEMORY;
            break;
        }
        RtlCopyMemory(Identity, Name.Buffer, Name.Length);
        Identity[Name.Length / sizeof(WCHAR)] = UNICODE_NULL;
        Title = Kind == ZpAdministrationKindWindowsApp ? NULL :
                    ZpAdministration_QueryRegistryString(Child, &DisplayName);
        Description = ZpAdministration_QueryRegistryString(Child, &Publisher);
        Detail = ZpAdministration_QueryRegistryString(Child, &DisplayVersion);
        if (Kind == ZpAdministrationKindWindowsApp || Title != NULL)
        {
            Status = ZpAdministration_AddRecord(Builder,
                                                 Kind,
                                                 0,
                                                 Flags,
                                                 0,
                                                 Identity,
                                                 Title != NULL ? Title : Identity,
                                                 Description,
                                                 Detail);
        }
        Mem_Free(Detail);
        Mem_Free(Description);
        Mem_Free(Title);
        Mem_Free(Identity);
        NtClose(Child);
    }
    Mem_Free(Information);
CleanupKey:
    NtClose(Key);
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateSoftware(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    static const UNICODE_STRING MachineUninstall =
        RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    static const UNICODE_STRING UserUninstall =
        RTL_CONSTANT_STRING(L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    static const UNICODE_STRING MachineWow64Uninstall = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    static const UNICODE_STRING WindowsApps = RTL_CONSTANT_STRING(
        L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\"
        L"AppModel\\Repository\\Packages");
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    HANDLE CurrentUser = NULL;
    NTSTATUS Status;

    Status = ZpAdministration_EnumerateSoftwareKey(NULL,
                                                   &MachineUninstall,
                                                   ZpAdministrationKindDesktopProgram,
                                                   1,
                                                   &Builder);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EnumerateSoftwareKey(NULL,
                                                       &MachineWow64Uninstall,
                                                       ZpAdministrationKindDesktopProgram,
                                                       3,
                                                       &Builder);
    }
    if (NT_SUCCESS(Status))
    {
        Status = RtlOpenCurrentUser(KEY_ENUMERATE_SUB_KEYS, &CurrentUser);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EnumerateSoftwareKey(CurrentUser,
                                                       &UserUninstall,
                                                       ZpAdministrationKindDesktopProgram,
                                                       2,
                                                       &Builder);
        if (NT_SUCCESS(Status))
        {
            Status = ZpAdministration_EnumerateSoftwareKey(CurrentUser,
                                                           &WindowsApps,
                                                           ZpAdministrationKindWindowsApp,
                                                           2,
                                                           &Builder);
        }
        NtClose(CurrentUser);
    }
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

typedef struct _ZP_FEATURE_ENUMERATION_CONTEXT
{
    PZP_ADMINISTRATION_BUILDER Builder;
    NTSTATUS Status;
} ZP_FEATURE_ENUMERATION_CONTEXT, *PZP_FEATURE_ENUMERATION_CONTEXT;

static
BOOL
CALLBACK
ZpAdministration_EnumerateFeatureCallback(
    _In_ PCKCBS_FEATURE Feature,
    _In_ PVOID Context)
{
    PZP_FEATURE_ENUMERATION_CONTEXT Enumeration = Context;
    ULONG State = Feature->CurrentState == KcbsInstallStateInstalled ||
                  Feature->CurrentState == KcbsInstallStatePermanent ? 4 :
                  Feature->CurrentState == KcbsInstallStateAbsent ? 2 : 0;

    Enumeration->Status = ZpAdministration_AddRecord(Enumeration->Builder,
                                                      ZpAdministrationKindWindowsFeature,
                                                      State,
                                                      0,
                                                      0,
                                                      Feature->Name,
                                                      Feature->DisplayName[0] != UNICODE_NULL ?
                                                          Feature->DisplayName : Feature->Name,
                                                      Feature->Description,
                                                      NULL);
    return NT_SUCCESS(Enumeration->Status);
}

static
ZP_STATUS
ZpAdministration_EnumerateFeatures(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_FEATURE_ENUMERATION_CONTEXT Enumeration = { &Builder, STATUS_SUCCESS };
    HRESULT Result;
    NTSTATUS Status;

    Result = KcbsEnumerateFeatures(L"KNSoft.ZPigeon", ZpAdministration_EnumerateFeatureCallback, &Enumeration);
    if (FAILED(Result))
    {
        ZpAdministration_FreeBuilder(&Builder);
        return ZpStatus_FromCode(ZpStatusHResult, Result);
    }
    Status = Enumeration.Status;
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_ControlFeature(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    KCBS_REQUIRED_ACTION RequiredAction;
    PWSTR Identity;
    HRESULT Result;

    if (Control->Action != ZpAdministrationActionEnable &&
        Control->Action != ZpAdministrationActionDisable)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Result = KcbsSetFeatureState(L"KNSoft.ZPigeon",
                                 Identity,
                                 Control->Action == ZpAdministrationActionEnable,
                                 TRUE,
                                 &RequiredAction);
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusHResult, Result);
}

typedef struct _ZP_SOFTWARE_ENUMERATION_CONTEXT
{
    PZP_ADMINISTRATION_BUILDER Builder;
    NTSTATUS Status;
} ZP_SOFTWARE_ENUMERATION_CONTEXT, *PZP_SOFTWARE_ENUMERATION_CONTEXT;

static
BOOL
NTAPI
ZpAdministration_EnumeratePackageCallback(
    _In_ PCZP_SOFTWARE_PACKAGE_INFO Package,
    _In_ PVOID Context)
{
    PZP_SOFTWARE_ENUMERATION_CONTEXT Enumeration = Context;

    Enumeration->Status = ZpAdministration_AddRecord(Enumeration->Builder,
                                                      ZpAdministrationKindPackage,
                                                      Package->Provider,
                                                      0,
                                                      0,
                                                      Package->Identity,
                                                      Package->Name,
                                                      Package->Source,
                                                      Package->Version);
    return NT_SUCCESS(Enumeration->Status);
}

static
BOOL
NTAPI
ZpAdministration_EnumeratePackageProviderCallback(
    _In_ PCZP_SOFTWARE_PACKAGE_PROVIDER_INFO Provider,
    _In_ PVOID Context)
{
    PZP_SOFTWARE_ENUMERATION_CONTEXT Enumeration = Context;

    Enumeration->Status = ZpAdministration_AddRecord(
        Enumeration->Builder,
        Provider->Provider == 0 ? ZpAdministrationKindPackageContext : ZpAdministrationKindPackageProvider,
        Provider->Provider,
        Provider->Capabilities,
        0,
        Provider->Identity,
        Provider->Name,
        Provider->RuntimeVersion,
        Provider->ManagerVersion);
    return NT_SUCCESS(Enumeration->Status);
}

#pragma warning(push)
// Output parameters are conditional on a structure-valued ZP_STATUS, which SAL cannot express.
#pragma warning(disable: 6101)

static
ZP_STATUS
ZpAdministration_EnumeratePackageProviders(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_SOFTWARE_ENUMERATION_CONTEXT Enumeration = { &Builder, STATUS_SUCCESS };
    HRESULT Result;
    NTSTATUS Status;

    Result = ZpSoftware_EnumeratePackageProviders(ZpAdministration_EnumeratePackageProviderCallback, &Enumeration);
    if (FAILED(Result))
    {
        ZpAdministration_FreeBuilder(&Builder);
        return NT_SUCCESS(Enumeration.Status) ?
                   ZpStatus_FromCode(ZpStatusHResult, Result) :
                   ZpStatus_FromNtStatus(Enumeration.Status);
    }
    Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}


static
ZP_STATUS
ZpAdministration_QueryPackages(
    _In_ PCZP_STRING_VIEW Identity,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_SOFTWARE_ENUMERATION_CONTEXT Enumeration = { &Builder, STATUS_SUCCESS };
    PWSTR Provider;
    HRESULT Result;
    NTSTATUS Status;

    Provider = ZpAdministration_CopyView(Identity);
    if (Provider == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Result = ZpSoftware_EnumeratePackages(Provider, ZpAdministration_EnumeratePackageCallback, &Enumeration);
    Mem_Free(Provider);
    if (FAILED(Result))
    {
        ZpAdministration_FreeBuilder(&Builder);
        return NT_SUCCESS(Enumeration.Status) ?
                   ZpStatus_FromCode(ZpStatusHResult, Result) :
                   ZpStatus_FromNtStatus(Enumeration.Status);
    }
    Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
BOOL
NTAPI
ZpAdministration_EnumerateSoftwareDeploymentCallback(
    _In_ PCZP_SOFTWARE_DEPLOYMENT_INFO Deployment,
    _In_ PVOID Context)
{
    PZP_SOFTWARE_ENUMERATION_CONTEXT Enumeration = Context;

    Enumeration->Status = ZpAdministration_AddRecord(Enumeration->Builder,
                                                      ZpAdministrationKindSoftwareDeployment,
                                                      Deployment->State,
                                                      Deployment->Flags,
                                                      Deployment->Result,
                                                      Deployment->Id,
                                                      Deployment->Name,
                                                      Deployment->Identity,
                                                      Deployment->ErrorText);
    return NT_SUCCESS(Enumeration->Status);
}

static
ZP_STATUS
ZpAdministration_EnumerateSoftwareDeployments(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_SOFTWARE_ENUMERATION_CONTEXT Enumeration = { &Builder, STATUS_SUCCESS };
    HRESULT Result;
    NTSTATUS Status;

    Result = ZpSoftware_EnumerateDeployments(ZpAdministration_EnumerateSoftwareDeploymentCallback, &Enumeration);
    if (FAILED(Result))
    {
        ZpAdministration_FreeBuilder(&Builder);
        return NT_SUCCESS(Enumeration.Status) ?
                   ZpStatus_FromCode(ZpStatusHResult, Result) :
                   ZpStatus_FromNtStatus(Enumeration.Status);
    }
    Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

#pragma warning(pop)

static
ZP_STATUS
ZpAdministration_ControlSoftware(
    _In_ PCZP_ADMINISTRATION_DATA_CONTROL_VIEW Control)
{
    PWSTR Id;
    HRESULT Result;

    if (Control->Data.Length % sizeof(WCHAR) != 0)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Id = ZpAdministration_CopyView(&Control->Identity);
    if (Id == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Result = ZpSoftware_StartDeployment(Control->Action,
                                        Control->Flags,
                                        Id,
                                        (PCWCH)Control->Data.Buffer,
                                        Control->Data.Length / sizeof(WCHAR));
    Mem_Free(Id);
    return ZpStatus_FromCode(ZpStatusHResult, Result);
}
