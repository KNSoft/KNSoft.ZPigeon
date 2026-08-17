#include <lm.h>

#pragma comment(lib, "Netapi32.lib")

static
ZP_STATUS
ZpAdministration_EnumerateUsers(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PUSER_INFO_2 Users;
    DWORD EntriesRead, TotalEntries, Resume = 0, Result;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    do
    {
        Result = NetUserEnum(NULL,
                             2,
                             FILTER_NORMAL_ACCOUNT,
                             (PBYTE*)&Users,
                             MAX_PREFERRED_LENGTH,
                             &EntriesRead,
                             &TotalEntries,
                             &Resume);
        if (Result != NERR_Success && Result != ERROR_MORE_DATA) break;
        for (Index = 0; NT_SUCCESS(Status) && Index < EntriesRead; Index++)
        {
            Status = ZpAdministration_AddRecord(&Builder,
                                                 ZpAdministrationKindUser,
                                                 Users[Index].usri2_priv,
                                                 Users[Index].usri2_flags,
                                                 Users[Index].usri2_last_logon,
                                                 Users[Index].usri2_name,
                                                 Users[Index].usri2_full_name,
                                                 Users[Index].usri2_comment,
                                                 Users[Index].usri2_home_dir);
        }
        NetApiBufferFree(Users);
        if (!NT_SUCCESS(Status)) break;
    } while (Result == ERROR_MORE_DATA);
    if (Result == NERR_Success && NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }
    ZpAdministration_FreeBuilder(&Builder);
    return Result == NERR_Success ?
               ZpStatus_FromNtStatus(Status) :
               ZpStatus_FromCode(ZpStatusWin32, Result);
}

static
ZP_STATUS
ZpAdministration_ControlUser(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PUSER_INFO_4 User;
    USER_INFO_1 NewUser = { 0 };
    USER_INFO_0 Rename;
    USER_INFO_1003 Password;
    USER_INFO_1008 Flags;
    PWSTR Identity, Argument = NULL, Secret = NULL;
    DWORD Result, ParameterError;

    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Control->Argument.Length != 0) Argument = ZpAdministration_CopyView(&Control->Argument);
    if (Control->Secret.Length != 0) Secret = ZpAdministration_CopyView(&Control->Secret);
    if (Identity == NULL || (Control->Argument.Length != 0 && Argument == NULL) ||
        (Control->Secret.Length != 0 && Secret == NULL))
    {
        Result = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
    }
    switch (Control->Action)
    {
        case ZpAdministrationActionCreate:
            if (Secret == NULL)
            {
                Result = ERROR_INVALID_PASSWORD;
                break;
            }
            NewUser.usri1_name = Identity;
            NewUser.usri1_password = Secret;
            NewUser.usri1_priv = USER_PRIV_USER;
            NewUser.usri1_home_dir = NULL;
            NewUser.usri1_comment = Argument;
            NewUser.usri1_flags = UF_SCRIPT;
            Result = NetUserAdd(NULL, 1, (PBYTE)&NewUser, &ParameterError);
            break;

        case ZpAdministrationActionDelete:
            Result = NetUserDel(NULL, Identity);
            break;

        case ZpAdministrationActionSetPassword:
            if (Secret == NULL)
            {
                Result = ERROR_INVALID_PASSWORD;
                break;
            }
            Password.usri1003_password = Secret;
            Result = NetUserSetInfo(NULL, Identity, 1003, (PBYTE)&Password, &ParameterError);
            break;

        case ZpAdministrationActionRename:
            if (Argument == NULL || *Argument == UNICODE_NULL)
            {
                Result = ERROR_INVALID_NAME;
                break;
            }
            Rename.usri0_name = Argument;
            Result = NetUserSetInfo(NULL, Identity, 0, (PBYTE)&Rename, &ParameterError);
            break;

        case ZpAdministrationActionEnable:
        case ZpAdministrationActionDisable:
            Result = NetUserGetInfo(NULL, Identity, 4, (PBYTE*)&User);
            if (Result == NERR_Success)
            {
                Flags.usri1008_flags = Control->Action == ZpAdministrationActionEnable ?
                                           User->usri4_flags & ~UF_ACCOUNTDISABLE :
                                           User->usri4_flags | UF_ACCOUNTDISABLE;
                NetApiBufferFree(User);
                Result = NetUserSetInfo(NULL, Identity, 1008, (PBYTE)&Flags, &ParameterError);
            }
            break;

        default:
            Result = ERROR_NOT_SUPPORTED;
            break;
    }
Cleanup:
    if (Secret != NULL)
    {
        RtlSecureZeroMemory(Secret, ((SIZE_T)Control->Secret.Length + 1) * sizeof(WCHAR));
        Mem_Free(Secret);
    }
    Mem_Free(Argument);
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusWin32, Result);
}
