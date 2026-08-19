#include <lm.h>

#include <wtsapi32.h>

#pragma comment(lib, "Netapi32.lib")
#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Wtsapi32.lib")

static
NTSTATUS
ZpAdministration_AddSessions(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    PWTS_SESSION_INFOW Sessions;
    PWSTR User, Domain;
    WCHAR Identity[32], Name[512], Detail[64];
    DWORD Count, Index, Length, Error;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &Sessions, &Count))
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    for (Index = 0; Index < Count && NT_SUCCESS(Status); Index++)
    {
        Error = ERROR_SUCCESS;
        User = Domain = NULL;
        if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,
                                         Sessions[Index].SessionId,
                                         WTSUserName,
                                         &User,
                                         &Length))
        {
            Error = GetLastError();
        }
        if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,
                                         Sessions[Index].SessionId,
                                         WTSDomainName,
                                         &Domain,
                                         &Length))
        {
            if (Error == ERROR_SUCCESS) Error = GetLastError();
        }
        _snwprintf_s(Identity,
                     ARRAYSIZE(Identity),
                     _TRUNCATE,
                     L"session:%lu",
                     Sessions[Index].SessionId);
        _snwprintf_s(Name,
                     ARRAYSIZE(Name),
                     _TRUNCATE,
                     Domain != NULL && *Domain != UNICODE_NULL ? L"%s\\%s" : L"%s%s",
                     Domain == NULL ? L"" : Domain,
                     User == NULL ? L"" : User);
        if (Error != ERROR_SUCCESS)
        {
            _snwprintf_s(Detail, ARRAYSIZE(Detail), _TRUNCATE, L"部分信息不可用，Win32: 0x%08lX", Error);
        }
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindSession,
                                             Sessions[Index].State,
                                             Error == ERROR_SUCCESS ? 0 : ZP_ADMINISTRATION_FLAG_PARTIAL,
                                             Sessions[Index].SessionId,
                                             Identity,
                                             Name,
                                             Sessions[Index].pWinStationName,
                                             Error == ERROR_SUCCESS ? NULL : Detail);
        if (Domain != NULL) WTSFreeMemory(Domain);
        if (User != NULL) WTSFreeMemory(User);
    }
    WTSFreeMemory(Sessions);
    return Status;
}

static
NTSTATUS
ZpAdministration_AddLogonSessions(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    PSECURITY_LOGON_SESSION_DATA Data;
    PLUID Sessions;
    WCHAR Identity[32], User[512], Domain[512], Detail[1024];
    ULONG Count, Index;
    NTSTATUS Status;

    Status = LsaEnumerateLogonSessions(&Count, &Sessions);
    if (!NT_SUCCESS(Status)) return Status;
    for (Index = 0; Index < Count; Index++)
    {
        Status = LsaGetLogonSessionData(&Sessions[Index], &Data);
        if (!NT_SUCCESS(Status))
        {
            _snwprintf_s(Identity,
                         ARRAYSIZE(Identity),
                         _TRUNCATE,
                         L"logon:%08lX%08lX",
                         Sessions[Index].HighPart,
                         Sessions[Index].LowPart);
            _snwprintf_s(Detail, ARRAYSIZE(Detail), _TRUNCATE, L"NTSTATUS: 0x%08lX", Status);
            Status = ZpAdministration_AddRecord(Builder,
                                                 ZpAdministrationKindLogonSession,
                                                 0,
                                                 ZP_ADMINISTRATION_FLAG_PARTIAL,
                                                 0,
                                                 Identity,
                                                 NULL,
                                                 NULL,
                                                 Detail);
            if (!NT_SUCCESS(Status)) break;
            continue;
        }
        if (Data == NULL) continue;
        _snwprintf_s(Identity,
                     ARRAYSIZE(Identity),
                     _TRUNCATE,
                     L"logon:%08lX%08lX",
                     Sessions[Index].HighPart,
                     Sessions[Index].LowPart);
        _snwprintf_s(User,
                     ARRAYSIZE(User),
                     _TRUNCATE,
                     L"%.*s",
                     (INT)(Data->UserName.Length / sizeof(WCHAR)),
                     Data->UserName.Buffer == NULL ? L"" : Data->UserName.Buffer);
        _snwprintf_s(Domain,
                     ARRAYSIZE(Domain),
                     _TRUNCATE,
                     L"%.*s",
                     (INT)(Data->LogonDomain.Length / sizeof(WCHAR)),
                     Data->LogonDomain.Buffer == NULL ? L"" : Data->LogonDomain.Buffer);
        _snwprintf_s(Detail,
                     ARRAYSIZE(Detail),
                     _TRUNCATE,
                     L"会话 %lu · %.*s",
                     Data->Session,
                     (INT)(Data->AuthenticationPackage.Length / sizeof(WCHAR)),
                     Data->AuthenticationPackage.Buffer == NULL ? L"" : Data->AuthenticationPackage.Buffer);
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindLogonSession,
                                             Data->LogonType,
                                             0,
                                             Data->LogonTime.QuadPart,
                                             Identity,
                                             User,
                                             Domain,
                                             Detail);
        LsaFreeReturnBuffer(Data);
        if (!NT_SUCCESS(Status)) break;
    }
    LsaFreeReturnBuffer(Sessions);
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateUsers(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PUSER_INFO_2 Users;
    PUSER_INFO_0 Names;
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
    if (Result == ERROR_ACCESS_DENIED && Builder.Count == 0)
    {
        Resume = 0;
        do
        {
            Result = NetUserEnum(NULL,
                                 0,
                                 FILTER_NORMAL_ACCOUNT,
                                 (PBYTE*)&Names,
                                 MAX_PREFERRED_LENGTH,
                                 &EntriesRead,
                                 &TotalEntries,
                                 &Resume);
            if (Result != NERR_Success && Result != ERROR_MORE_DATA) break;
            for (Index = 0; NT_SUCCESS(Status) && Index < EntriesRead; Index++)
            {
                Status = ZpAdministration_AddRecord(&Builder,
                                                     ZpAdministrationKindUser,
                                                     ERROR_ACCESS_DENIED,
                                                     ZP_ADMINISTRATION_FLAG_PARTIAL,
                                                     0,
                                                     Names[Index].usri0_name,
                                                     NULL,
                                                     L"详细信息不可用，Win32: 0x00000005",
                                                     NULL);
            }
            NetApiBufferFree(Names);
            if (!NT_SUCCESS(Status)) break;
        } while (Result == ERROR_MORE_DATA);
    }
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
ZpAdministration_EnumerateSessions(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    NTSTATUS Status = ZpAdministration_AddSessions(&Builder);

    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_EnumerateLogonSessions(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    NTSTATUS Status = ZpAdministration_AddLogonSessions(&Builder);

    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
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

    if ((Control->Action == ZpAdministrationActionDisconnect ||
         Control->Action == ZpAdministrationActionSignOut) &&
        Control->Identity.Length > 8)
    {
        UNICODE_STRING Number;
        ULONG SessionId;
        NTSTATUS Status;

        Identity = ZpAdministration_CopyView(&Control->Identity);
        if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        if (wcsncmp(Identity, L"session:", 8) != 0)
        {
            Mem_Free(Identity);
            return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
        }
        RtlInitUnicodeString(&Number, Identity + 8);
        Status = RtlUnicodeStringToInteger(&Number, 10, &SessionId);
        Mem_Free(Identity);
        if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
        Result = Control->Action == ZpAdministrationActionDisconnect ?
                     (WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE, SessionId, FALSE) ? ERROR_SUCCESS :
                                                                                       GetLastError()) :
                     (WTSLogoffSession(WTS_CURRENT_SERVER_HANDLE, SessionId, FALSE) ? ERROR_SUCCESS :
                                                                                   GetLastError());
        return ZpStatus_FromCode(ZpStatusWin32, Result);
    }

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
