#include "Process.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <UserEnv.h>

#include "../../KNSoft.ZPigeon.Client.SDK/Core/AppContainer.h"

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Userenv.lib")

#define ZP_EXECUTION_CUSTOM_TOKEN_MAX_GROUPS 256
#define ZP_EXECUTION_CUSTOM_TOKEN_MAX_PRIVILEGES 256

typedef struct _ZP_PROCESS_CUSTOM_TOKEN
{
    HANDLE Token;
    HANDLE ImpersonationToken;
} ZP_PROCESS_CUSTOM_TOKEN, *PZP_PROCESS_CUSTOM_TOKEN;

static
PWSTR
ZpProcess_CopyString(
    _In_ PCZP_STRING_VIEW View)
{
    PWSTR Value;

    Value = Mem_Alloc(((SIZE_T)View->Length + 1) * sizeof(WCHAR));
    if (Value != NULL)
    {
        RtlCopyMemory(Value, View->Buffer, (SIZE_T)View->Length * sizeof(WCHAR));
        Value[View->Length] = UNICODE_NULL;
    }
    return Value;
}

static
W32ERROR
ZpProcess_QueryDefaultWorkingDirectory(
    _In_ PCWSTR FileName,
    _Outptr_ PWSTR* WorkingDirectory)
{
    PWSTR Buffer, Separator;
    DWORD Capacity, Length;

    Capacity = SearchPathW(NULL, FileName, L".exe", 0, NULL, NULL);
    if (Capacity == 0) return GetLastError();
    Buffer = Mem_Alloc((SIZE_T)Capacity * sizeof(WCHAR));
    if (Buffer == NULL) return ERROR_NOT_ENOUGH_MEMORY;
    Length = SearchPathW(NULL, FileName, L".exe", Capacity, Buffer, NULL);
    if (Length == 0 || Length >= Capacity)
    {
        W32ERROR Error = Length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;

        Mem_Free(Buffer);
        return Error;
    }
    Separator = wcsrchr(Buffer, L'\\');
    if (Separator == NULL)
    {
        Mem_Free(Buffer);
        return ERROR_BAD_PATHNAME;
    }
    if (Separator == Buffer + 2 && Buffer[1] == L':') Separator++;
    *Separator = UNICODE_NULL;
    *WorkingDirectory = Buffer;
    return ERROR_SUCCESS;
}

static
ZP_STATUS
ZpProcess_DuplicateProcessToken(
    _In_ ULONG ProcessId,
    _In_ ULONG SessionId,
    _Out_ PHANDLE Token)
{
    HANDLE Process, SourceToken, PrimaryToken;
    NTSTATUS Status;
    ULONG Error;

    Status = PS_OpenProcess(&Process, PROCESS_QUERY_LIMITED_INFORMATION, ProcessId);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Status = NtOpenProcessToken(Process, TOKEN_QUERY | TOKEN_DUPLICATE, &SourceToken);
    NtClose(Process);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    if (!DuplicateTokenEx(SourceToken,
                          TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY |
                              TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                          NULL,
                          SecurityImpersonation,
                          TokenPrimary,
                          &PrimaryToken))
    {
        Error = GetLastError();
        NtClose(SourceToken);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    NtClose(SourceToken);
    if (!SetTokenInformation(PrimaryToken, TokenSessionId, &SessionId, sizeof(SessionId)))
    {
        Error = GetLastError();
        NtClose(PrimaryToken);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    *Token = PrimaryToken;
    return ZpStatus_Make(ZpStatusNone, 0);
}

static
ZP_STATUS
ZpProcess_QuerySystemToken(
    _In_ ULONG SessionId,
    _Out_ PHANDLE Token)
{
    static UNICODE_STRING WinlogonName = RTL_CONSTANT_STRING(L"winlogon.exe");
    SYSTEM_PROCESS_INFORMATION* Processes = NULL;
    SYSTEM_PROCESS_INFORMATION* Process;
    ULONG Length = 0x10000;
    NTSTATUS Status;
    BOOLEAN PreviousDebugPrivilege, RestoreDebugPrivilege = FALSE;
    ZP_STATUS Result = ZpStatus_FromNtStatus(STATUS_NOT_FOUND);

    Status = RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, TRUE, FALSE, &PreviousDebugPrivilege);
    if (NT_SUCCESS(Status)) RestoreDebugPrivilege = !PreviousDebugPrivilege;
    for (;;)
    {
        Processes = Mem_Alloc(Length);
        if (Processes == NULL)
        {
            Result = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            goto Cleanup;
        }
        Status = NtQuerySystemInformation(SystemProcessInformation, Processes, Length, &Length);
        if (Status != STATUS_INFO_LENGTH_MISMATCH) break;
        Mem_Free(Processes);
        Processes = NULL;
    }
    if (!NT_SUCCESS(Status))
    {
        Result = ZpStatus_FromNtStatus(Status);
        goto Cleanup;
    }
    Process = Processes;
    for (;;)
    {
        ULONG ProcessSessionId;

        if (Process->ImageName.Buffer != NULL &&
            RtlEqualUnicodeString(&Process->ImageName, &WinlogonName, TRUE) &&
            ProcessIdToSessionId(HandleToUlong(Process->UniqueProcessId), &ProcessSessionId) &&
            ProcessSessionId == SessionId)
        {
            Result = ZpProcess_DuplicateProcessToken(HandleToUlong(Process->UniqueProcessId), SessionId, Token);
            break;
        }
        if (Process->NextEntryOffset == 0) break;
        Process = Add2Ptr(Process, Process->NextEntryOffset);
    }
Cleanup:
    Mem_Free(Processes);
    if (RestoreDebugPrivilege)
    {
        RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, FALSE, FALSE, &PreviousDebugPrivilege);
    }
    return Result;
}

static
ZP_STATUS
ZpProcess_QueryTrustedInstallerToken(
    _In_ ULONG SessionId,
    _Out_ PHANDLE Token)
{
    SERVICE_STATUS_PROCESS Status;
    SC_HANDLE Manager, Service;
    DWORD BytesNeeded, Error;
    ULONGLONG Deadline;
    ZP_STATUS Result;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (Manager == NULL) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    Service = OpenServiceW(Manager, L"TrustedInstaller", SERVICE_QUERY_STATUS | SERVICE_START);
    CloseServiceHandle(Manager);
    if (Service == NULL) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    if (!QueryServiceStatusEx(Service, SC_STATUS_PROCESS_INFO, (PBYTE)&Status, sizeof(Status), &BytesNeeded))
    {
        Error = GetLastError();
        CloseServiceHandle(Service);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    if (Status.dwCurrentState == SERVICE_STOPPED && !StartServiceW(Service, 0, NULL) &&
        GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
    {
        Error = GetLastError();
        CloseServiceHandle(Service);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    Deadline = GetTickCount64() + 10000;
    do
    {
        if (!QueryServiceStatusEx(Service, SC_STATUS_PROCESS_INFO, (PBYTE)&Status, sizeof(Status), &BytesNeeded))
        {
            Error = GetLastError();
            CloseServiceHandle(Service);
            return ZpStatus_FromCode(ZpStatusWin32, Error);
        }
        if (Status.dwCurrentState == SERVICE_RUNNING) break;
        Sleep(50);
    } while (Status.dwCurrentState == SERVICE_START_PENDING && GetTickCount64() < Deadline);
    CloseServiceHandle(Service);
    if (Status.dwCurrentState != SERVICE_RUNNING || Status.dwProcessId == 0)
    {
        return ZpStatus_FromCode(ZpStatusWin32, Status.dwWin32ExitCode != 0 ?
                                                       Status.dwWin32ExitCode : ERROR_SERVICE_NOT_ACTIVE);
    }
    Result = ZpProcess_DuplicateProcessToken(Status.dwProcessId, SessionId, Token);
    return Result;
}

static
ZP_STATUS
ZpProcess_QueryToken(
    _In_ ZP_EXECUTION_IDENTITY Identity,
    _In_ ULONG SessionId,
    _In_opt_ PCWSTR UserName,
    _In_opt_ PCWSTR Password,
    _Out_ PHANDLE Token)
{
    HANDLE UserToken, LinkedToken;
    TOKEN_ELEVATION Elevation;
    DWORD Length, Error;

    if (Identity == ZpExecutionIdentitySystem) return ZpProcess_QuerySystemToken(SessionId, Token);
    if (Identity == ZpExecutionIdentityTrustedInstaller)
    {
        return ZpProcess_QueryTrustedInstallerToken(SessionId, Token);
    }
    if (Identity == ZpExecutionIdentityOtherUser)
    {
        if (UserName == NULL || Password == NULL) return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
        if (!LogonUserW(UserName,
                        NULL,
                        Password,
                        LOGON32_LOGON_INTERACTIVE,
                        LOGON32_PROVIDER_DEFAULT,
                        &UserToken))
        {
            return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        }
    }
    else if ((Error = NT_GetSessionToken(&UserToken, SessionId)) != ERROR_SUCCESS)
    {
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    if (Identity == ZpExecutionIdentityAdministrator)
    {
        if (!GetTokenInformation(UserToken, TokenElevation, &Elevation, sizeof(Elevation), &Length))
        {
            Error = GetLastError();
            NtClose(UserToken);
            return ZpStatus_FromCode(ZpStatusWin32, Error);
        }
        if (!Elevation.TokenIsElevated)
        {
            if (!GetTokenInformation(UserToken, TokenLinkedToken, &LinkedToken, sizeof(LinkedToken), &Length))
            {
                Error = GetLastError();
                NtClose(UserToken);
                return ZpStatus_FromCode(ZpStatusWin32, Error);
            }
            NtClose(UserToken);
            UserToken = LinkedToken;
            if (!GetTokenInformation(UserToken, TokenElevation, &Elevation, sizeof(Elevation), &Length))
            {
                Error = GetLastError();
                NtClose(UserToken);
                return ZpStatus_FromCode(ZpStatusWin32, Error);
            }
            if (!Elevation.TokenIsElevated)
            {
                NtClose(UserToken);
                return ZpStatus_FromCode(ZpStatusWin32, ERROR_ELEVATION_REQUIRED);
            }
        }
    }
    if (!SetTokenInformation(UserToken, TokenSessionId, &SessionId, sizeof(SessionId)))
    {
        Error = GetLastError();
        NtClose(UserToken);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    *Token = UserToken;
    return ZpStatus_Make(ZpStatusNone, 0);
}

static
NTSTATUS
ZpProcess_ReadSid(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PSID* Sid)
{
    ZP_BUFFER_VIEW View;
    PISID Value;
    NTSTATUS Status;

    Status = ZpCodec_ReadByteString(Reader, &View);
    if (!NT_SUCCESS(Status)) return Status;
    if (View.Length < (ULONG)FIELD_OFFSET(SID, SubAuthority)) return STATUS_INVALID_SID;
    Value = (PISID)View.Buffer;
    if (View.Length != GetSidLengthRequired(Value->SubAuthorityCount) || !RtlValidSid(Value))
    {
        return STATUS_INVALID_SID;
    }
    *Sid = Value;
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpProcess_CreateCustomToken(
    _In_ PCZP_BUFFER_VIEW Definition,
    _In_ ULONG SessionId,
    _Out_ PZP_PROCESS_CUSTOM_TOKEN Custom)
{
    ZP_CODEC_READER Reader;
    TOKEN_GROUPS* Groups = NULL;
    TOKEN_PRIVILEGES* Privileges = NULL;
    TOKEN_USER User = { 0 };
    TOKEN_OWNER Owner = { 0 };
    TOKEN_PRIMARY_GROUP PrimaryGroup = { 0 };
    TOKEN_SOURCE Source = { 0 };
    TOKEN_GROUPS* LogonGroups = NULL;
    SID_IDENTIFIER_AUTHORITY MandatoryAuthority = SECURITY_MANDATORY_LABEL_AUTHORITY;
    PSID UserSid, OwnerSid, PrimaryGroupSid, GroupSid;
    PSID IntegritySid = NULL;
    HANDLE SessionToken = NULL;
    LARGE_INTEGER Expiration = { 0 };
    LUID AuthenticationId;
    ULONGLONG AuthenticationValue, PrivilegeValue;
    ULONG Version, Flags, IntegrityRid, GroupCount, PrivilegeCount, Index, Attributes;
    ULONG GroupIndex = 0;
    ULONG UiAccess;
    LOGICAL LogonSidFound;
    BOOLEAN PreviousDebugPrivilege, RestoreDebugPrivilege = FALSE;
    NTSTATUS Status;
    ZP_STATUS Result;

    ZpCodec_InitializeReader(&Reader, Definition->Buffer, Definition->Length);
    Status = ZpCodec_ReadUInt32(&Reader, &Version);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &AuthenticationValue);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &IntegrityRid);
    if (NT_SUCCESS(Status))
    {
        Status = ZpProcess_ReadSid(&Reader, &UserSid);
        if (NT_SUCCESS(Status)) User.User.Sid = UserSid;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpProcess_ReadSid(&Reader, &OwnerSid);
        if (NT_SUCCESS(Status)) Owner.Owner = OwnerSid;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpProcess_ReadSid(&Reader, &PrimaryGroupSid);
        if (NT_SUCCESS(Status)) PrimaryGroup.PrimaryGroup = PrimaryGroupSid;
    }
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadArrayCount(&Reader, &GroupCount);
    if (!NT_SUCCESS(Status) || Version != ZP_EXECUTION_CUSTOM_TOKEN_VERSION ||
        (Flags & ~(ZP_EXECUTION_CUSTOM_TOKEN_FLAG_UI_ACCESS |
                   ZP_EXECUTION_CUSTOM_TOKEN_FLAG_ADD_LOGON_SID)) != 0 ||
        GroupCount > ZP_EXECUTION_CUSTOM_TOKEN_MAX_GROUPS)
    {
        return ZpStatus_FromNtStatus(NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status);
    }
    Groups = Mem_Alloc(FIELD_OFFSET(TOKEN_GROUPS, Groups) +
                       ((SIZE_T)GroupCount + 2) * sizeof(SID_AND_ATTRIBUTES));
    if (Groups == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    for (Index = 0; Index < GroupCount; Index++)
    {
        Status = ZpCodec_ReadUInt32(&Reader, &Attributes);
        if (NT_SUCCESS(Status))
        {
            Status = ZpProcess_ReadSid(&Reader, &GroupSid);
            if (NT_SUCCESS(Status)) Groups->Groups[GroupIndex].Sid = GroupSid;
        }
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Groups->Groups[GroupIndex].Attributes = Attributes;
        GroupIndex++;
    }
    if (!AllocateAndInitializeSid(&MandatoryAuthority,
                                  1,
                                  IntegrityRid,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  &IntegritySid))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    Groups->Groups[GroupIndex].Sid = IntegritySid;
    Groups->Groups[GroupIndex].Attributes = SE_GROUP_INTEGRITY | SE_GROUP_INTEGRITY_ENABLED;
    GroupIndex++;
    if (FlagOn(Flags, ZP_EXECUTION_CUSTOM_TOKEN_FLAG_ADD_LOGON_SID))
    {
        const W32ERROR Error = NT_GetSessionToken(&SessionToken, SessionId);

        if (Error != ERROR_SUCCESS)
        {
            Status = NTSTATUS_FROM_WIN32(Error);
            goto Cleanup;
        }
        Status = PS_GetTokenInfo(SessionToken, TokenLogonSid, &LogonGroups);
        if (!NT_SUCCESS(Status)) goto Cleanup;
        LogonSidFound = FALSE;
        for (Index = 0; Index < LogonGroups->GroupCount; Index++)
        {
            if (FlagOn(LogonGroups->Groups[Index].Attributes, SE_GROUP_LOGON_ID))
            {
                Groups->Groups[GroupIndex] = LogonGroups->Groups[Index];
                GroupIndex++;
                LogonSidFound = TRUE;
                break;
            }
        }
        if (!LogonSidFound)
        {
            Status = STATUS_NOT_FOUND;
            goto Cleanup;
        }
    }
    Groups->GroupCount = GroupIndex;
    Status = ZpCodec_ReadArrayCount(&Reader, &PrivilegeCount);
    if (!NT_SUCCESS(Status) || PrivilegeCount > ZP_EXECUTION_CUSTOM_TOKEN_MAX_PRIVILEGES)
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto Cleanup;
    }
    Privileges = Mem_Alloc(FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges) +
                           (SIZE_T)PrivilegeCount * sizeof(LUID_AND_ATTRIBUTES));
    if (Privileges == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    Privileges->PrivilegeCount = PrivilegeCount;
    for (Index = 0; Index < PrivilegeCount; Index++)
    {
        Status = ZpCodec_ReadUInt64(&Reader, &PrivilegeValue);
        if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Privileges->Privileges[Index].Attributes);
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Privileges->Privileges[Index].Luid.LowPart = (ULONG)PrivilegeValue;
        Privileges->Privileges[Index].Luid.HighPart = (LONG)(PrivilegeValue >> 32);
    }
    if (Reader.Offset != Definition->Length)
    {
        Status = STATUS_DATA_ERROR;
        goto Cleanup;
    }
    Status = RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, TRUE, FALSE, &PreviousDebugPrivilege);
    if (NT_SUCCESS(Status)) RestoreDebugPrivilege = !PreviousDebugPrivilege;
    if (NT_SUCCESS(Status)) Status = Sys_GetLsaProcessId(&Index);
    if (NT_SUCCESS(Status)) Status = PS_DuplicateSystemToken(Index, TokenImpersonation, &Custom->ImpersonationToken);
    if (NT_SUCCESS(Status)) Status = PS_Impersonate(Custom->ImpersonationToken);
    if (NT_SUCCESS(Status))
    {
        Status = NT_AdjustTokenPrivilege(Custom->ImpersonationToken,
                                         SE_ASSIGNPRIMARYTOKEN_PRIVILEGE,
                                         SE_PRIVILEGE_ENABLED);
    }
    if (NT_SUCCESS(Status))
    {
        Status = NT_AdjustTokenPrivilege(Custom->ImpersonationToken,
                                         SE_INCREASE_QUOTA_PRIVILEGE,
                                         SE_PRIVILEGE_ENABLED);
    }
    if (NT_SUCCESS(Status))
    {
        AuthenticationId.LowPart = (ULONG)AuthenticationValue;
        AuthenticationId.HighPart = (LONG)(AuthenticationValue >> 32);
        Status = NtCreateToken(&Custom->Token,
                               TOKEN_ALL_ACCESS,
                               NULL,
                               TokenPrimary,
                               &AuthenticationId,
                               &Expiration,
                               &User,
                               Groups,
                               Privileges,
                               &Owner,
                               &PrimaryGroup,
                               NULL,
                               &Source);
    }
    if (NT_SUCCESS(Status))
    {
        Status = NtSetInformationToken(Custom->Token, TokenSessionId, &SessionId, sizeof(SessionId));
    }
    if (NT_SUCCESS(Status) && FlagOn(Flags, ZP_EXECUTION_CUSTOM_TOKEN_FLAG_UI_ACCESS))
    {
        UiAccess = TRUE;
        Status = NtSetInformationToken(Custom->Token, TokenUIAccess, &UiAccess, sizeof(UiAccess));
    }
Cleanup:
    if (!NT_SUCCESS(Status))
    {
        PS_Impersonate(NULL);
        if (Custom->Token != NULL)
        {
            NtClose(Custom->Token);
            Custom->Token = NULL;
        }
        if (Custom->ImpersonationToken != NULL)
        {
            NtClose(Custom->ImpersonationToken);
            Custom->ImpersonationToken = NULL;
        }
    }
    if (SessionToken != NULL) NtClose(SessionToken);
    if (RestoreDebugPrivilege)
    {
        RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, FALSE, FALSE, &PreviousDebugPrivilege);
    }
    Mem_Free(LogonGroups);
    if (IntegritySid != NULL) FreeSid(IntegritySid);
    Mem_Free(Privileges);
    Mem_Free(Groups);
    Result = ZpStatus_FromNtStatus(Status);
    return Result;
}

static
VOID
ZpProcess_CloseCustomToken(
    _Inout_ PZP_PROCESS_CUSTOM_TOKEN Custom)
{
    if (Custom->ImpersonationToken != NULL) PS_Impersonate(NULL);
    if (Custom->Token != NULL) NtClose(Custom->Token);
    if (Custom->ImpersonationToken != NULL) NtClose(Custom->ImpersonationToken);
}

ZP_STATUS
ZpProcess_Launch(
    _In_ PCZP_EXECUTION_START_VIEW Start,
    _In_opt_ HPCON PseudoConsole,
    _In_opt_ HANDLE Job,
    _Out_ PPROCESS_INFORMATION ProcessInformation,
    _Out_ PULONG SessionId)
{
    STARTUPINFOEXW Startup = { 0 };
    PPROC_THREAD_ATTRIBUTE_LIST Attributes = NULL;
    ZP_APP_CONTAINER_SECURITY_CONTEXT SecurityContext = { 0 };
    ZP_PROCESS_CUSTOM_TOKEN Custom = { 0 };
    HANDLE Token = NULL;
    PVOID Environment = NULL;
    PWSTR FileName = NULL, Arguments = NULL, WorkingDirectory = NULL;
    PWSTR UserName = NULL, Password = NULL, AppContainerSid = NULL, CommandLine = NULL;
    SIZE_T AttributeSize = 0;
    ULONG CurrentSessionId, AttributeCount = 0;
    INT CommandLineLength;
    DWORD CreationFlags, Error;
    LOGICAL AttributesInitialized = FALSE, SecurityContextInitialized = FALSE;
    LOGICAL UseCurrentToken;
    BOOL Created;
    ZP_STATUS Status;

    RtlZeroMemory(ProcessInformation, sizeof(*ProcessInformation));
    FileName = ZpProcess_CopyString(&Start->FileName);
    Arguments = ZpProcess_CopyString(&Start->Arguments);
    WorkingDirectory = ZpProcess_CopyString(&Start->WorkingDirectory);
    UserName = ZpProcess_CopyString(&Start->UserName);
    Password = ZpProcess_CopyString(&Start->Password);
    AppContainerSid = ZpProcess_CopyString(&Start->AppContainerSid);
    if (FileName == NULL || Arguments == NULL || WorkingDirectory == NULL || UserName == NULL || Password == NULL ||
        AppContainerSid == NULL)
    {
        Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto Cleanup;
    }
    if (Start->WorkingDirectory.Length == 0)
    {
        Mem_Free(WorkingDirectory);
        WorkingDirectory = NULL;
        Error = ZpProcess_QueryDefaultWorkingDirectory(FileName, &WorkingDirectory);
        if (Error != ERROR_SUCCESS)
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, Error);
            goto Cleanup;
        }
    }
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &CurrentSessionId))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    *SessionId = Start->SessionId == ZP_EXECUTION_SESSION_CURRENT ? CurrentSessionId : Start->SessionId;
    if (Start->Identity == ZpExecutionIdentityCurrent && *SessionId != CurrentSessionId)
    {
        Status = ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
        goto Cleanup;
    }
    CommandLineLength = _scwprintf(L"\"%s\"%s%s",
                                   FileName,
                                   Start->Arguments.Length != 0 ? L" " : L"",
                                   Arguments);
    if (CommandLineLength < 0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_NAME_TOO_LONG);
        goto Cleanup;
    }
    CommandLine = Mem_Alloc(((SIZE_T)CommandLineLength + 1) * sizeof(WCHAR));
    if (CommandLine == NULL)
    {
        Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto Cleanup;
    }
    _snwprintf_s(CommandLine,
                 (SIZE_T)CommandLineLength + 1,
                 _TRUNCATE,
                 L"\"%s\"%s%s",
                 FileName,
                 Start->Arguments.Length != 0 ? L" " : L"",
                 Arguments);
    if (PseudoConsole != NULL) AttributeCount++;
    if (Start->Identity == ZpExecutionIdentityAppContainer) AttributeCount++;
    if (Job != NULL) AttributeCount++;
    if (AttributeCount != 0)
    {
        InitializeProcThreadAttributeList(NULL, AttributeCount, 0, &AttributeSize);
        Attributes = Mem_Alloc(AttributeSize);
        if (Attributes == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            goto Cleanup;
        }
        if (!InitializeProcThreadAttributeList(Attributes, AttributeCount, 0, &AttributeSize))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
        AttributesInitialized = TRUE;
        if (PseudoConsole != NULL &&
            !UpdateProcThreadAttribute(Attributes,
                                       0,
                                       PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                       PseudoConsole,
                                       sizeof(PseudoConsole),
                                       NULL,
                                       NULL))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
        if (Job != NULL &&
            !UpdateProcThreadAttribute(Attributes,
                                       0,
                                       PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                       &Job,
                                       sizeof(Job),
                                       NULL,
                                       NULL))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
    }
    if (Start->Identity == ZpExecutionIdentityAppContainer)
    {
        Status = ZpAppContainer_QuerySecurityCapabilities(AppContainerSid, &SecurityContext);
        if (!ZpStatus_IsSuccess(Status)) goto Cleanup;
        SecurityContextInitialized = TRUE;
        if (!UpdateProcThreadAttribute(Attributes,
                                       0,
                                       PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                       &SecurityContext.Capabilities,
                                       sizeof(SecurityContext.Capabilities),
                                       NULL,
                                       NULL))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
    }
    Startup.StartupInfo.cb = AttributeCount != 0 ? sizeof(Startup) : sizeof(Startup.StartupInfo);
    if (FlagOn(Start->Flags, ZP_EXECUTION_FLAG_HIDDEN))
    {
        Startup.StartupInfo.dwFlags |= STARTF_USESHOWWINDOW;
        Startup.StartupInfo.wShowWindow = SW_HIDE;
    }
    if (PseudoConsole != NULL) Startup.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    Startup.lpAttributeList = Attributes;
    UseCurrentToken = Start->Identity == ZpExecutionIdentityCurrent ||
                      Start->Identity == ZpExecutionIdentityAppContainer;
    if (Start->Identity == ZpExecutionIdentityAdministrator && *SessionId == CurrentSessionId)
    {
        TOKEN_ELEVATION Elevation;
        DWORD ReturnLength;

        if (!GetTokenInformation(NtCurrentProcessToken(),
                                 TokenElevation,
                                 &Elevation,
                                 sizeof(Elevation),
                                 &ReturnLength))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
        if (!Elevation.TokenIsElevated)
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, ERROR_ELEVATION_REQUIRED);
            goto Cleanup;
        }
        UseCurrentToken = TRUE;
    }
    if (Start->Identity == ZpExecutionIdentityCustomToken)
    {
        Status = ZpProcess_CreateCustomToken(&Start->CustomToken, *SessionId, &Custom);
        if (!ZpStatus_IsSuccess(Status)) goto Cleanup;
        Token = Custom.Token;
    }
    else if (!UseCurrentToken)
    {
        Status = ZpProcess_QueryToken(Start->Identity,
                                      *SessionId,
                                      Start->UserName.Length != 0 ? UserName : NULL,
                                      Start->Password.Length != 0 ? Password : NULL,
                                      &Token);
        if (!ZpStatus_IsSuccess(Status)) goto Cleanup;
    }
    CreationFlags = NORMAL_PRIORITY_CLASS | CREATE_DEFAULT_ERROR_MODE;
    if (PseudoConsole == NULL) CreationFlags |= CREATE_NEW_CONSOLE;
    if (AttributeCount != 0) CreationFlags |= EXTENDED_STARTUPINFO_PRESENT;
    if (Token != NULL)
    {
        if (!CreateEnvironmentBlock(&Environment, Token, FALSE))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
        CreationFlags |= CREATE_UNICODE_ENVIRONMENT;
    }
    if (Token != NULL) Startup.StartupInfo.lpDesktop = L"winsta0\\default";
    Created = CreateProcessInternalW(Token,
                                     NULL,
                                     CommandLine,
                                     NULL,
                                     NULL,
                                     FALSE,
                                     CreationFlags,
                                     Environment,
                                     WorkingDirectory,
                                     &Startup.StartupInfo,
                                     ProcessInformation,
                                     NULL);
    Error = Created ? ERROR_SUCCESS : GetLastError();
    Status = Created ? ZpStatus_Make(ZpStatusNone, 0) : ZpStatus_FromCode(ZpStatusWin32, Error);
Cleanup:
    if (Environment != NULL) DestroyEnvironmentBlock(Environment);
    if (Token != NULL && Token != Custom.Token) NtClose(Token);
    ZpProcess_CloseCustomToken(&Custom);
    if (AttributesInitialized) DeleteProcThreadAttributeList(Attributes);
    Mem_Free(Attributes);
    if (SecurityContextInitialized) ZpAppContainer_FreeSecurityCapabilities(&SecurityContext);
    if (Password != NULL) RtlSecureZeroMemory(Password, ((SIZE_T)Start->Password.Length + 1) * sizeof(WCHAR));
    Mem_Free(CommandLine);
    Mem_Free(AppContainerSid);
    Mem_Free(Password);
    Mem_Free(UserName);
    Mem_Free(WorkingDirectory);
    Mem_Free(Arguments);
    Mem_Free(FileName);
    return Status;
}

static
LOGICAL
ZpProcess_ContainsId(
    _In_reads_(Count) const ULONG* ProcessIds,
    _In_ ULONG Count,
    _In_ ULONG ProcessId)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (ProcessIds[Index] == ProcessId) return TRUE;
    }
    return FALSE;
}

NTSTATUS
ZpProcess_TerminateTree(
    _In_ HANDLE Process,
    _In_ ULONG ProcessId,
    _In_ LONGLONG CreateTime,
    _In_ NTSTATUS ExitStatus)
{
    SYSTEM_PROCESS_INFORMATION* Processes = NULL;
    SYSTEM_PROCESS_INFORMATION* Entry;
    ULONG* ProcessIds = NULL;
    ULONG Length = 0x10000, Count = 1, Capacity = 64, ParentId, ChildId;
    HANDLE Child;
    LOGICAL Added;
    NTSTATUS Status, FirstStatus;

    ProcessIds = Mem_Alloc((SIZE_T)Capacity * sizeof(*ProcessIds));
    if (ProcessIds == NULL) return STATUS_NO_MEMORY;
    ProcessIds[0] = ProcessId;
    FirstStatus = NtTerminateProcess(Process, ExitStatus);
    if (CreateTime == 0)
    {
        Status = FirstStatus;
        goto Cleanup;
    }
    do
    {
        for (;;)
        {
            Mem_Free(Processes);
            Processes = Mem_Alloc(Length);
            if (Processes == NULL)
            {
                Status = STATUS_NO_MEMORY;
                goto Cleanup;
            }
            Status = NtQuerySystemInformation(SystemProcessInformation, Processes, Length, &Length);
            if (Status != STATUS_INFO_LENGTH_MISMATCH) break;
        }
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Added = FALSE;
        Entry = Processes;
        for (;;)
        {
            ParentId = HandleToUlong(Entry->InheritedFromUniqueProcessId);
            ChildId = HandleToUlong(Entry->UniqueProcessId);
            if (ChildId != 0 && Entry->CreateTime.QuadPart >= CreateTime &&
                ZpProcess_ContainsId(ProcessIds, Count, ParentId) &&
                !ZpProcess_ContainsId(ProcessIds, Count, ChildId))
            {
                if (Count == Capacity)
                {
                    ULONG* Values;

                    Capacity *= 2;
                    Values = Mem_ReAlloc(ProcessIds, (SIZE_T)Capacity * sizeof(*ProcessIds));
                    if (Values == NULL)
                    {
                        Status = STATUS_NO_MEMORY;
                        goto Cleanup;
                    }
                    ProcessIds = Values;
                }
                ProcessIds[Count++] = ChildId;
                Added = TRUE;
                Status = PS_OpenProcess(&Child, PROCESS_TERMINATE, ChildId);
                if (NT_SUCCESS(Status))
                {
                    Status = NtTerminateProcess(Child, ExitStatus);
                    NtClose(Child);
                }
                if (NT_SUCCESS(FirstStatus) && !NT_SUCCESS(Status)) FirstStatus = Status;
            }
            if (Entry->NextEntryOffset == 0) break;
            Entry = Add2Ptr(Entry, Entry->NextEntryOffset);
        }
    } while (Added);
    Status = FirstStatus;
Cleanup:
    Mem_Free(Processes);
    Mem_Free(ProcessIds);
    return NT_SUCCESS(Status) ? FirstStatus : Status;
}
