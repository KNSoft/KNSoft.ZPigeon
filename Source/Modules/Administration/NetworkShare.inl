#include <lm.h>
#include <aclapi.h>
#include <sddl.h>
#include <winnetwk.h>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Netapi32.lib")
#pragma comment(lib, "Mpr.lib")

static
NTSTATUS
ZpNetworkShare_AddPublished(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ const SHARE_INFO_2* Share)
{
    return ZpAdministration_AddRecord(Builder,
                                      ZpAdministrationKindPublishedShare,
                                      Share->shi2_current_uses,
                                      Share->shi2_type,
                                      Share->shi2_max_uses,
                                      Share->shi2_netname,
                                      Share->shi2_path,
                                      Share->shi2_remark,
                                      NULL);
}

static
NTSTATUS
ZpNetworkShare_AddConnection(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ const USE_INFO_2* Use)
{
    PCWSTR Identity = Use->ui2_local != NULL && *Use->ui2_local != UNICODE_NULL ?
                          Use->ui2_local : Use->ui2_remote;
    PWSTR Account = NULL;
    NTSTATUS Status;

    if (Use->ui2_domainname != NULL && *Use->ui2_domainname != UNICODE_NULL &&
        Use->ui2_username != NULL && *Use->ui2_username != UNICODE_NULL && wcschr(Use->ui2_username, L'\\') == NULL)
    {
        SIZE_T Count = wcslen(Use->ui2_domainname) + 1 + wcslen(Use->ui2_username) + 1;

        Account = Mem_Alloc(Count * sizeof(WCHAR));
        if (Account == NULL) return STATUS_NO_MEMORY;
        _snwprintf_s(Account, Count, _TRUNCATE, L"%s\\%s", Use->ui2_domainname, Use->ui2_username);
    }
    Status = ZpAdministration_AddRecord(Builder,
                                         ZpAdministrationKindNetworkConnection,
                                         Use->ui2_status,
                                         Use->ui2_asg_type,
                                         Use->ui2_usecount,
                                         Identity,
                                         Use->ui2_remote,
                                         Account != NULL ? Account : Use->ui2_username,
                                         Use->ui2_local);
    Mem_Free(Account);
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumeratePublishedShares(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PSHARE_INFO_2 Shares;
    DWORD EntriesRead, TotalEntries, Resume = 0, Result;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    do
    {
        Result = NetShareEnum(NULL,
                              2,
                              (PBYTE*)&Shares,
                              MAX_PREFERRED_LENGTH,
                              &EntriesRead,
                              &TotalEntries,
                              &Resume);
        if (Result != NERR_Success && Result != ERROR_MORE_DATA) break;
        for (Index = 0; NT_SUCCESS(Status) && Index < EntriesRead; Index++)
        {
            Status = ZpNetworkShare_AddPublished(&Builder, &Shares[Index]);
        }
        NetApiBufferFree(Shares);
        if (!NT_SUCCESS(Status)) break;
    } while (Result == ERROR_MORE_DATA);
    if (Result == NERR_Success && NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }
    ZpAdministration_FreeBuilder(&Builder);
    return !NT_SUCCESS(Status) ?
               ZpStatus_FromNtStatus(Status) :
               Result == NERR_Success ?
               ZpStatus_FromNtStatus(Status) :
               ZpStatus_FromCode(ZpStatusWin32, Result);
}

static
ZP_STATUS
ZpAdministration_QueryPublishedShare(
    _In_ PCZP_STRING_VIEW Identity,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    SECURITY_DESCRIPTOR NullSecurityDescriptor;
    PSECURITY_DESCRIPTOR SecurityDescriptor = NULL;
    PSECURITY_DESCRIPTOR Descriptor;
    PWSTR Name = ZpAdministration_CopyView(Identity), Sddl = NULL;
    SECURITY_DESCRIPTOR_CONTROL Control;
    DWORD Revision;
    DWORD Result;
    NTSTATUS Status;

    if (Name == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Result = GetNamedSecurityInfoW(Name,
                                   SE_LMSHARE,
                                   OWNER_SECURITY_INFORMATION |
                                       GROUP_SECURITY_INFORMATION |
                                       DACL_SECURITY_INFORMATION,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &SecurityDescriptor);
    Descriptor = SecurityDescriptor;
    if (Result == NERR_Success && Descriptor == NULL)
    {
        if (!InitializeSecurityDescriptor(&NullSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION) ||
            !SetSecurityDescriptorDacl(&NullSecurityDescriptor, TRUE, NULL, FALSE))
        {
            Result = GetLastError();
        }
        else
        {
            Descriptor = &NullSecurityDescriptor;
        }
    }
    if (Result == NERR_Success &&
        (!GetSecurityDescriptorControl(Descriptor, &Control, &Revision) ||
         !ConvertSecurityDescriptorToStringSecurityDescriptorW(Descriptor,
                                                                SDDL_REVISION_1,
                                                                OWNER_SECURITY_INFORMATION |
                                                                    GROUP_SECURITY_INFORMATION |
                                                                    DACL_SECURITY_INFORMATION,
                                                                &Sddl,
                                                                NULL)))
    {
        Result = GetLastError();
    }
    if (Result == NERR_Success)
    {
        Status = ZpAdministration_AddRecord(
            &Builder,
            ZpAdministrationKindSecurityDescriptor,
            0,
            BooleanFlagOn(Control, SE_DACL_PROTECTED) ?
                ZP_ADMINISTRATION_SECURITY_DESCRIPTOR_FLAG_DACL_PROTECTED : 0,
            0,
            Name,
            NULL,
            NULL,
            Sddl);
        if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }
    else
    {
        Status = STATUS_SUCCESS;
    }
    LocalFree(Sddl);
    LocalFree(SecurityDescriptor);
    Mem_Free(Name);
    ZpAdministration_FreeBuilder(&Builder);
    return Result == NERR_Success ?
               ZpStatus_FromNtStatus(Status) :
               ZpStatus_FromCode(ZpStatusWin32, Result);
}

static
DWORD
ZpNetworkShare_SetPublishedPermissions(
    _In_ PWSTR Name,
    _In_ PCWSTR Sddl,
    _In_ BOOLEAN DaclProtected)
{
    SHARE_INFO_1501 Information = { 0 };
    DWORD Result, ParameterError;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl,
                                                               SDDL_REVISION_1,
                                                               &Information.shi1501_security_descriptor,
                                                               NULL))
    {
        return GetLastError();
    }
    if (!SetSecurityDescriptorControl(Information.shi1501_security_descriptor,
                                      SE_DACL_PROTECTED,
                                      DaclProtected ? SE_DACL_PROTECTED : 0))
    {
        Result = GetLastError();
        LocalFree(Information.shi1501_security_descriptor);
        return Result;
    }
    Result = NetShareSetInfo(NULL, Name, 1501, (PBYTE)&Information, &ParameterError);
    LocalFree(Information.shi1501_security_descriptor);
    return Result;
}

static
ZP_STATUS
ZpAdministration_ControlPublishedShare(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PSHARE_INFO_502 Current;
    SHARE_INFO_502 Share = { 0 };
    PWSTR Name = ZpAdministration_CopyView(&Control->Identity);
    PWSTR Argument = Control->Argument.Length != 0 ? ZpAdministration_CopyView(&Control->Argument) : NULL;
    PWSTR Remark = Control->Secret.Length != 0 ? ZpAdministration_CopyView(&Control->Secret) : NULL;
    PWSTR End;
    DWORD Result, ParameterError;

    if (Name == NULL || (Control->Argument.Length != 0 && Argument == NULL) ||
        (Control->Secret.Length != 0 && Remark == NULL))
    {
        Result = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
    }
    switch (Control->Action)
    {
        case ZpAdministrationActionCreate:
            if (Argument == NULL || *Argument == UNICODE_NULL)
            {
                Result = ERROR_INVALID_PARAMETER;
                break;
            }
            Share.shi502_netname = Name;
            Share.shi502_type = STYPE_DISKTREE;
            Share.shi502_remark = Remark != NULL ? Remark : L"";
            Share.shi502_max_uses = SHI_USES_UNLIMITED;
            Share.shi502_path = Argument;
            Result = NetShareAdd(NULL, 502, (PBYTE)&Share, &ParameterError);
            break;

        case ZpAdministrationActionConfigure:
            if (Argument == NULL)
            {
                Result = ERROR_INVALID_PARAMETER;
                break;
            }
            Share.shi502_max_uses = wcstoul(Argument, &End, 10);
            if (End == Argument || *End != UNICODE_NULL)
            {
                Result = ERROR_INVALID_PARAMETER;
                break;
            }
            Result = NetShareGetInfo(NULL, Name, 502, (PBYTE*)&Current);
            if (Result == NERR_Success)
            {
                Current->shi502_remark = Remark != NULL ? Remark : L"";
                Current->shi502_max_uses = Share.shi502_max_uses;
                Result = NetShareSetInfo(NULL, Name, 502, (PBYTE)Current, &ParameterError);
                NetApiBufferFree(Current);
            }
            break;

        case ZpAdministrationActionDelete:
            Result = NetShareDel(NULL, Name, 0);
            break;

        default:
            Result = ERROR_NOT_SUPPORTED;
            break;
    }
Cleanup:
    if (Remark != NULL)
    {
        RtlSecureZeroMemory(Remark, ((SIZE_T)Control->Secret.Length + 1) * sizeof(WCHAR));
        Mem_Free(Remark);
    }
    Mem_Free(Argument);
    Mem_Free(Name);
    return ZpStatus_FromCode(ZpStatusWin32, Result);
}

static
ZP_STATUS
ZpAdministration_ControlPublishedShareSecurity(
    _In_ PCZP_ADMINISTRATION_DATA_CONTROL_VIEW Control)
{
    ZP_STRING_VIEW Identity;
    PWSTR Name, Sddl;
    DWORD Result;
    NTSTATUS Status;

    if (Control->Action != ZpAdministrationActionSetPermissions ||
        Control->Flags > ZP_ADMINISTRATION_SECURITY_DESCRIPTOR_FLAG_DACL_PROTECTED ||
        Control->Data.Length == 0 ||
        Control->Data.Length % sizeof(WCHAR) != 0 ||
        Control->Data.Length > ZP_CODEC_MAX_ELEMENT_COUNT * sizeof(WCHAR))
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Status = ZpAdministration_GetDataControlIdentityString(Control, &Identity);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Name = ZpAdministration_CopyView(&Identity);
    Sddl = Mem_Alloc((SIZE_T)Control->Data.Length + sizeof(WCHAR));
    if (Name == NULL || Sddl == NULL)
    {
        Mem_Free(Name);
        Mem_Free(Sddl);
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    RtlCopyMemory(Sddl, Control->Data.Buffer, Control->Data.Length);
    Sddl[Control->Data.Length / sizeof(WCHAR)] = UNICODE_NULL;
    Result = wcsnlen(Sddl, Control->Data.Length / sizeof(WCHAR)) == Control->Data.Length / sizeof(WCHAR) ?
                 ZpNetworkShare_SetPublishedPermissions(
                     Name,
                     Sddl,
                     BooleanFlagOn(Control->Flags,
                                   ZP_ADMINISTRATION_SECURITY_DESCRIPTOR_FLAG_DACL_PROTECTED)) :
                 ERROR_INVALID_PARAMETER;
    Mem_Free(Sddl);
    Mem_Free(Name);
    return ZpStatus_FromCode(ZpStatusWin32, Result);
}

static
ZP_STATUS
ZpAdministration_EnumerateNetworkConnections(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PUSE_INFO_2 Uses;
    DWORD EntriesRead, TotalEntries, Resume = 0, Result;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    do
    {
        Result = NetUseEnum(NULL,
                            2,
                            (PBYTE*)&Uses,
                            MAX_PREFERRED_LENGTH,
                            &EntriesRead,
                            &TotalEntries,
                            &Resume);
        if (Result != NERR_Success && Result != ERROR_MORE_DATA) break;
        for (Index = 0; NT_SUCCESS(Status) && Index < EntriesRead; Index++)
        {
            Status = ZpNetworkShare_AddConnection(&Builder, &Uses[Index]);
        }
        NetApiBufferFree(Uses);
        if (!NT_SUCCESS(Status)) break;
    } while (Result == ERROR_MORE_DATA);
    if (Result == NERR_Success && NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }
    ZpAdministration_FreeBuilder(&Builder);
    return !NT_SUCCESS(Status) ?
               ZpStatus_FromNtStatus(Status) :
               Result == NERR_Success ?
               ZpStatus_FromNtStatus(Status) :
               ZpStatus_FromCode(ZpStatusWin32, Result);
}

static
ZP_STATUS
ZpAdministration_ControlNetworkConnection(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    NETRESOURCEW Resource = { 0 };
    PWSTR Identity = ZpAdministration_CopyView(&Control->Identity);
    PWSTR Local = Control->Argument.Length != 0 ? ZpAdministration_CopyView(&Control->Argument) : NULL;
    PWSTR Credentials = NULL, Separator, User = NULL, Password = NULL;
    DWORD Result;
    ULONG Index;

    if (Identity == NULL || (Control->Argument.Length != 0 && Local == NULL))
    {
        Result = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
    }
    if (Control->Secret.Length != 0)
    {
        Credentials = ZpAdministration_CopyView(&Control->Secret);
        if (Credentials == NULL)
        {
            Result = ERROR_NOT_ENOUGH_MEMORY;
            goto Cleanup;
        }
        for (Index = 0; Index < Control->Secret.Length && Credentials[Index] != UNICODE_NULL; Index++);
        if (Index == Control->Secret.Length)
        {
            Result = ERROR_INVALID_PARAMETER;
            goto Cleanup;
        }
        Separator = &Credentials[Index++];
        for (; Index < Control->Secret.Length && Credentials[Index] != UNICODE_NULL; Index++);
        if (Index != Control->Secret.Length)
        {
            Result = ERROR_INVALID_PARAMETER;
            goto Cleanup;
        }
        if (Separator != Credentials) User = Credentials;
        Password = Separator + 1;
        if (User == NULL && *Password == UNICODE_NULL) Password = NULL;
    }
    switch (Control->Action)
    {
        case ZpAdministrationActionConnect:
            Resource.dwType = RESOURCETYPE_DISK;
            Resource.lpLocalName = Local != NULL && *Local != UNICODE_NULL ? Local : NULL;
            Resource.lpRemoteName = Identity;
            Result = WNetAddConnection2W(&Resource, Password, User, 0);
            break;

        case ZpAdministrationActionDisconnect:
            Result = WNetCancelConnection2W(Identity, 0, FALSE);
            break;

        default:
            Result = ERROR_NOT_SUPPORTED;
            break;
    }
Cleanup:
    if (Credentials != NULL)
    {
        RtlSecureZeroMemory(Credentials, ((SIZE_T)Control->Secret.Length + 1) * sizeof(WCHAR));
        Mem_Free(Credentials);
    }
    Mem_Free(Local);
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusWin32, Result);
}
