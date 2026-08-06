#include <netfw.h>
#include <sddl.h>
#include <userenv.h>
#include <KNSoft/NDK/NT/Rtl/Security/Sid.h>
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Account.h"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/AppContainer.h"

#pragma comment(lib, "Userenv.lib")

static
LOGICAL
ZpAppContainer_IsLoopbackEnabled(
    _In_ PSID Sid,
    _In_reads_(Count) const SID_AND_ATTRIBUTES* Entries,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (RtlEqualSid(Sid, Entries[Index].Sid)) return TRUE;
    }
    return FALSE;
}

static
NTSTATUS
ZpAppContainer_SidString(
    _In_opt_ PSID Sid,
    _Out_ PUNICODE_STRING String)
{
    if (Sid == NULL)
    {
        RtlInitEmptyUnicodeString(String, NULL, 0);
        return STATUS_SUCCESS;
    }
    return RtlConvertSidToUnicodeString(String, Sid, TRUE);
}

static
NTSTATUS
ZpAppContainer_AddProfile(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ const INET_FIREWALL_APP_CONTAINER* Profile,
    _In_reads_(LoopbackCount) const SID_AND_ATTRIBUTES* Loopback,
    _In_ ULONG LoopbackCount)
{
    UNICODE_STRING Sid = { 0 }, UserSid = { 0 }, CapabilitySid = { 0 };
    PUNICODE_STRING CapabilityName;
    PCWSTR Identity;
    PWSTR Detail = NULL;
    SIZE_T DetailCount;
    ULONG Flags, Index;
    NTSTATUS Status, NameStatus;

    Status = ZpAppContainer_SidString(Profile->appContainerSid, &Sid);
    if (NT_SUCCESS(Status)) Status = ZpAppContainer_SidString(Profile->userSid, &UserSid);
    if (!NT_SUCCESS(Status)) goto Exit;
    if (Sid.Buffer == NULL)
    {
        Status = STATUS_DATA_ERROR;
        goto Exit;
    }
    Identity = Profile->appContainerName != NULL && *Profile->appContainerName != UNICODE_NULL ?
                   Profile->appContainerName : Sid.Buffer;
    DetailCount = (SIZE_T)Sid.Length / sizeof(WCHAR) + UserSid.Length / sizeof(WCHAR) +
                  (Profile->workingDirectory == NULL ? 0 : wcslen(Profile->workingDirectory)) +
                  (Profile->packageFullName == NULL ? 0 : wcslen(Profile->packageFullName)) + 4;
    Detail = Mem_Alloc(DetailCount * sizeof(WCHAR));
    if (Detail == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }
    _snwprintf_s(Detail,
                 DetailCount,
                 _TRUNCATE,
                 L"%s\n%s\n%s\n%s",
                 Sid.Buffer == NULL ? L"" : Sid.Buffer,
                 UserSid.Buffer == NULL ? L"" : UserSid.Buffer,
                 Profile->workingDirectory == NULL ? L"" : Profile->workingDirectory,
                 Profile->packageFullName == NULL ? L"" : Profile->packageFullName);
    Flags = ZpAppContainer_IsLoopbackEnabled(Profile->appContainerSid, Loopback, LoopbackCount) ?
                ZP_ADMINISTRATION_APP_CONTAINER_FLAG_LOOPBACK : 0;
    if (Profile->packageFullName != NULL && *Profile->packageFullName != UNICODE_NULL)
    {
        Flags |= ZP_ADMINISTRATION_APP_CONTAINER_FLAG_PACKAGED;
    }
    Status = ZpAdministration_AddRecord(Builder,
                                         ZpAdministrationKindAppContainerProfile,
                                         Profile->capabilities.count,
                                         Flags,
                                         Profile->binaries.count,
                                         Identity,
                                         Profile->displayName,
                                         Profile->description,
                                         Detail);
    for (Index = 0; NT_SUCCESS(Status) && Index < Profile->capabilities.count; Index++)
    {
        Status = ZpAppContainer_SidString(Profile->capabilities.capabilities[Index].Sid, &CapabilitySid);
        if (NT_SUCCESS(Status))
        {
            NameStatus = ZpAccount_QuerySidName(Profile->capabilities.capabilities[Index].Sid, &CapabilityName);
            Status = ZpAdministration_AddRecord(
                Builder,
                ZpAdministrationKindAppContainerCapability,
                0,
                Profile->capabilities.capabilities[Index].Attributes,
                0,
                Identity,
                NT_SUCCESS(NameStatus) ? CapabilityName->Buffer : NULL,
                NULL,
                CapabilitySid.Buffer);
            if (NT_SUCCESS(NameStatus)) NT_FreeStringW(CapabilityName);
            RtlFreeUnicodeString(&CapabilitySid);
            RtlZeroMemory(&CapabilitySid, sizeof(CapabilitySid));
        }
    }
    for (Index = 0; NT_SUCCESS(Status) && Index < Profile->binaries.count; Index++)
    {
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindAppContainerBinary,
                                             0,
                                             0,
                                             0,
                                             Identity,
                                             Profile->binaries.binaries[Index],
                                             NULL,
                                             NULL);
    }
Exit:
    RtlFreeUnicodeString(&CapabilitySid);
    RtlFreeUnicodeString(&UserSid);
    RtlFreeUnicodeString(&Sid);
    Mem_Free(Detail);
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateAppContainers(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_APP_CONTAINER_NETWORK_API Api;
    PINET_FIREWALL_APP_CONTAINER Profiles = NULL;
    PSID_AND_ATTRIBUTES Loopback = NULL;
    ULONG ProfileCount, LoopbackCount, Index, Previous;
    DWORD Error;
    NTSTATUS Status = STATUS_SUCCESS;

    Status = ZpAppContainer_LoadNetworkApi(&Api);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Error = Api.Get(&LoopbackCount, &Loopback);
    if (Error != ERROR_SUCCESS)
    {
        FreeLibrary(Api.Module);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    Error = Api.Enumerate(0, &ProfileCount, &Profiles);
    if (Error != ERROR_SUCCESS)
    {
        ZpAppContainer_FreeNetworkConfig(Loopback, LoopbackCount);
        FreeLibrary(Api.Module);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    for (Index = 0; NT_SUCCESS(Status) && Index < ProfileCount; Index++)
    {
        if (Profiles[Index].appContainerSid == NULL || !RtlValidSid(Profiles[Index].appContainerSid))
        {
            Status = STATUS_DATA_ERROR;
            break;
        }
        for (Previous = 0; Previous < Index; Previous++)
        {
            if (RtlEqualSid(Profiles[Index].appContainerSid, Profiles[Previous].appContainerSid)) break;
        }
        if (Previous != Index) continue;
        Status = ZpAppContainer_AddProfile(&Builder, &Profiles[Index], Loopback, LoopbackCount);
    }
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    if (Profiles != NULL) Api.Free(Profiles);
    ZpAppContainer_FreeNetworkConfig(Loopback, LoopbackCount);
    FreeLibrary(Api.Module);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAppContainer_Create(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PSID_AND_ATTRIBUTES Capabilities = NULL;
    PBYTE SidBuffer;
    PSID AppContainerSid = NULL;
    PWSTR Name, Metadata, Description, CapabilityNames = NULL, Cursor, Next;
    UNICODE_STRING CapabilityName;
    ULONG CapabilityCount = 0, Index;
    SIZE_T AllocationSize;
    ZP_STATUS Status;
    HRESULT Result;
    NTSTATUS DeriveStatus;

    Name = ZpAdministration_CopyView(&Control->Identity);
    Metadata = ZpAdministration_CopyView(&Control->Argument);
    if (Control->Secret.Length != 0) CapabilityNames = ZpAdministration_CopyView(&Control->Secret);
    if (Name == NULL || Metadata == NULL || (Control->Secret.Length != 0 && CapabilityNames == NULL))
    {
        Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto Exit;
    }
    Description = wcschr(Metadata, L'\n');
    if (Description == NULL || Metadata == Description)
    {
        Status = ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
        goto Exit;
    }
    *Description++ = UNICODE_NULL;
    if (Control->Secret.Length != 0)
    {
        CapabilityCount = 1;
        for (Index = 0; Index < Control->Secret.Length; Index++)
        {
            if (CapabilityNames[Index] != L'\n') continue;
            if (Index == 0 || Index + 1 == Control->Secret.Length || CapabilityNames[Index - 1] == L'\n')
            {
                Status = ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
                goto Exit;
            }
            CapabilityCount++;
        }
    }
    if (CapabilityCount > 64)
    {
        Status = ZpStatus_FromNtStatus(STATUS_QUOTA_EXCEEDED);
        goto Exit;
    }
    AllocationSize = (SIZE_T)CapabilityCount * (sizeof(*Capabilities) + SECURITY_MAX_SID_SIZE * 2);
    Capabilities = CapabilityCount == 0 ? NULL : Mem_Alloc(AllocationSize);
    if (CapabilityCount != 0 && Capabilities == NULL)
    {
        Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto Exit;
    }
    SidBuffer = Add2Ptr(Capabilities, (SIZE_T)CapabilityCount * sizeof(*Capabilities));
    Cursor = CapabilityNames;
    for (Index = 0; Index < CapabilityCount; Index++)
    {
        Next = wcschr(Cursor, L'\n');
        if (Next != NULL) *Next++ = UNICODE_NULL;
        RtlInitUnicodeString(&CapabilityName, Cursor);
        Capabilities[Index].Sid = Add2Ptr(SidBuffer, (SIZE_T)Index * SECURITY_MAX_SID_SIZE * 2);
        Capabilities[Index].Attributes = SE_GROUP_ENABLED;
        DeriveStatus = RtlDeriveCapabilitySidsFromName(
            &CapabilityName,
            Add2Ptr(Capabilities[Index].Sid, SECURITY_MAX_SID_SIZE),
            Capabilities[Index].Sid);
        if (!NT_SUCCESS(DeriveStatus))
        {
            Status = ZpStatus_FromNtStatus(DeriveStatus);
            goto Exit;
        }
        Cursor = Next;
    }
    Result = CreateAppContainerProfile(Name,
                                       Metadata,
                                       Description,
                                       Capabilities,
                                       CapabilityCount,
                                       &AppContainerSid);
    Status = SUCCEEDED(Result) ? ZpStatus_Make(ZpStatusNone, 0) :
                                 ZpStatus_FromCode(ZpStatusHResult, Result);
Exit:
    if (AppContainerSid != NULL) FreeSid(AppContainerSid);
    Mem_Free(Capabilities);
    Mem_Free(CapabilityNames);
    Mem_Free(Metadata);
    Mem_Free(Name);
    return Status;
}

static
ZP_STATUS
ZpAppContainer_ConfigureLoopback(
    _In_ PCWSTR SidString,
    _In_ BOOLEAN Enable)
{
    PSID Sid = NULL;
    ZP_APP_CONTAINER_NETWORK_API Api;
    PSID_AND_ATTRIBUTES Current = NULL, Updated = NULL;
    ULONG Count, Index, UpdatedCount = 0;
    DWORD Error;
    LOGICAL Exists;
    NTSTATUS Status;

    if (!ConvertStringSidToSidW(SidString, &Sid)) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    Status = ZpAppContainer_LoadNetworkApi(&Api);
    if (!NT_SUCCESS(Status))
    {
        LocalFree(Sid);
        return ZpStatus_FromNtStatus(Status);
    }
    Error = Api.Get(&Count, &Current);
    if (Error != ERROR_SUCCESS)
    {
        FreeLibrary(Api.Module);
        LocalFree(Sid);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    Exists = ZpAppContainer_IsLoopbackEnabled(Sid, Current, Count);
    if (Exists == Enable) goto Exit;
    Updated = Mem_Alloc(((SIZE_T)Count + 1) * sizeof(*Updated));
    if (Updated == NULL)
    {
        Error = ERROR_NOT_ENOUGH_MEMORY;
        goto Exit;
    }
    for (Index = 0; Index < Count; Index++)
    {
        if (!RtlEqualSid(Sid, Current[Index].Sid)) Updated[UpdatedCount++] = Current[Index];
    }
    if (Enable)
    {
        Updated[UpdatedCount].Sid = Sid;
        Updated[UpdatedCount++].Attributes = 0;
    }
    Error = Api.Set(UpdatedCount, Updated);
Exit:
    Mem_Free(Updated);
    ZpAppContainer_FreeNetworkConfig(Current, Count);
    FreeLibrary(Api.Module);
    LocalFree(Sid);
    return Error == ERROR_SUCCESS ? ZpStatus_Make(ZpStatusNone, 0) :
                                    ZpStatus_FromCode(ZpStatusWin32, Error);
}

static
ZP_STATUS
ZpAdministration_ControlAppContainer(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PWSTR Identity, Argument = NULL;
    HRESULT Result;
    ZP_STATUS Status;

    if (Control->Action == ZpAdministrationActionCreate) return ZpAppContainer_Create(Control);
    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Control->Argument.Length != 0) Argument = ZpAdministration_CopyView(&Control->Argument);
    if (Identity == NULL || (Control->Argument.Length != 0 && Argument == NULL))
    {
        Mem_Free(Argument);
        Mem_Free(Identity);
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    if (Control->Action == ZpAdministrationActionDelete)
    {
        Result = DeleteAppContainerProfile(Identity);
        Status = SUCCEEDED(Result) ? ZpStatus_Make(ZpStatusNone, 0) :
                                    ZpStatus_FromCode(ZpStatusHResult, Result);
    }
    else if (Control->Action == ZpAdministrationActionConfigure &&
             Argument != NULL && (wcscmp(Argument, L"0") == 0 || wcscmp(Argument, L"1") == 0))
    {
        Status = ZpAppContainer_ConfigureLoopback(Identity, Argument[0] == L'1');
    }
    else
    {
        Status = ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Mem_Free(Argument);
    Mem_Free(Identity);
    return Status;
}
