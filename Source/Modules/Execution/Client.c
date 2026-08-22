#include "Client.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <ShellApi.h>
#include <TlHelp32.h>
#include <WtsApi32.h>

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/AppContainer.h"

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Wtsapi32.lib")

#define ZP_EXECUTION_MAX_JOBS 256
#define ZP_EXECUTION_MAX_STAGING_NAME 128

typedef struct _ZP_EXECUTION_JOB
{
    LIST_ENTRY ListEntry;
    HANDLE Process;
    ZP_EXECUTION_JOB_RECORD Record;
    WCHAR FileName[ANYSIZE_ARRAY];
} ZP_EXECUTION_JOB, *PZP_EXECUTION_JOB;

static
PWSTR
ZpExecution_CopyString(
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
ZP_STATUS
ZpExecution_EncodeSessionsResponse(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PWTS_SESSION_INFOW Sessions = NULL;
    PZP_EXECUTION_SESSION_RECORD Records = NULL;
    PWSTR* Names = NULL;
    DWORD Count = 0, Index;
    DWORD ClientSessionId;
    NTSTATUS Status;
    ULONG Length;
    PBYTE Buffer;

    if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &Sessions, &Count))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &ClientSessionId))
    {
        WTSFreeMemory(Sessions);
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Count * (sizeof(*Records) + sizeof(*Names)));
        if (Records == NULL)
        {
            WTSFreeMemory(Sessions);
            return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
        RtlZeroMemory(Records, (SIZE_T)Count * (sizeof(*Records) + sizeof(*Names)));
        Names = (PWSTR*)(Records + Count);
    }
    for (Index = 0; Index < Count; Index++)
    {
        PWSTR User = NULL, Domain = NULL;
        DWORD UserBytes = 0, DomainBytes = 0;
        ULONG UserLength, DomainLength;

        Records[Index].SessionId = Sessions[Index].SessionId;
        Records[Index].State = Sessions[Index].State;
        Records[Index].Flags = (Sessions[Index].SessionId == ClientSessionId ? ZP_EXECUTION_SESSION_FLAG_CLIENT : 0) |
                               (Sessions[Index].State == WTSActive ? ZP_EXECUTION_SESSION_FLAG_ACTIVE : 0);
        Records[Index].StationName = Sessions[Index].pWinStationName;
        Records[Index].StationNameLength = Sessions[Index].pWinStationName != NULL ?
                                               (ULONG)wcslen(Sessions[Index].pWinStationName) : 0;
        WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,
                                    Sessions[Index].SessionId,
                                    WTSUserName,
                                    &User,
                                    &UserBytes);
        WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,
                                    Sessions[Index].SessionId,
                                    WTSDomainName,
                                    &Domain,
                                    &DomainBytes);
        UserLength = UserBytes >= sizeof(WCHAR) ? UserBytes / sizeof(WCHAR) - 1 : 0;
        DomainLength = DomainBytes >= sizeof(WCHAR) ? DomainBytes / sizeof(WCHAR) - 1 : 0;
        if (UserLength != 0)
        {
            Names[Index] = Mem_Alloc(((SIZE_T)DomainLength + UserLength + (DomainLength != 0 ? 2 : 1)) *
                                     sizeof(WCHAR));
            if (Names[Index] == NULL)
            {
                if (User != NULL) WTSFreeMemory(User);
                if (Domain != NULL) WTSFreeMemory(Domain);
                Status = STATUS_NO_MEMORY;
                goto Cleanup;
            }
            if (DomainLength != 0)
            {
                RtlCopyMemory(Names[Index], Domain, (SIZE_T)DomainLength * sizeof(WCHAR));
                Names[Index][DomainLength] = L'\\';
                RtlCopyMemory(Names[Index] + DomainLength + 1,
                              User,
                              (SIZE_T)UserLength * sizeof(WCHAR));
                Records[Index].UserNameLength = DomainLength + UserLength + 1;
            }
            else
            {
                RtlCopyMemory(Names[Index], User, (SIZE_T)UserLength * sizeof(WCHAR));
                Records[Index].UserNameLength = UserLength;
            }
            Names[Index][Records[Index].UserNameLength] = UNICODE_NULL;
            Records[Index].UserName = Names[Index];
        }
        if (User != NULL) WTSFreeMemory(User);
        if (Domain != NULL) WTSFreeMemory(Domain);
    }
    Status = ZpExecution_EncodeSessions(Records, Count, NULL, 0, &Length);
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status)) Status = ZpExecution_EncodeSessions(Records, Count, Buffer, Length, &Length);
    if (NT_SUCCESS(Status))
    {
        *Response = Buffer;
        *ResponseLength = Length;
    }
    else
    {
        Mem_Free(Buffer);
    }
Cleanup:
    for (Index = 0; Index < Count; Index++) Mem_Free(Names[Index]);
    Mem_Free(Records);
    WTSFreeMemory(Sessions);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpExecution_DuplicateProcessToken(
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
ZpExecution_QuerySystemToken(
    _In_ ULONG SessionId,
    _Out_ PHANDLE Token)
{
    PROCESSENTRY32W Entry = { sizeof(Entry) };
    HANDLE Snapshot;
    ULONG ProcessSessionId;
    ZP_STATUS Status = ZpStatus_FromNtStatus(STATUS_NOT_FOUND);

    PS_AdjustPrivilege(NtCurrentProcessToken(), SE_DEBUG_PRIVILEGE, TRUE);
    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Snapshot == INVALID_HANDLE_VALUE) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    if (Process32FirstW(Snapshot, &Entry))
    {
        do
        {
            if (_wcsicmp(Entry.szExeFile, L"winlogon.exe") == 0 &&
                ProcessIdToSessionId(Entry.th32ProcessID, &ProcessSessionId) && ProcessSessionId == SessionId)
            {
                Status = ZpExecution_DuplicateProcessToken(Entry.th32ProcessID, SessionId, Token);
                break;
            }
        } while (Process32NextW(Snapshot, &Entry));
    }
    NtClose(Snapshot);
    return Status;
}

static
ZP_STATUS
ZpExecution_QueryTrustedInstallerToken(
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
    if (!QueryServiceStatusEx(Service,
                              SC_STATUS_PROCESS_INFO,
                              (PBYTE)&Status,
                              sizeof(Status),
                              &BytesNeeded))
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
        if (!QueryServiceStatusEx(Service,
                                  SC_STATUS_PROCESS_INFO,
                                  (PBYTE)&Status,
                                  sizeof(Status),
                                  &BytesNeeded))
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
    Result = ZpExecution_DuplicateProcessToken(Status.dwProcessId, SessionId, Token);
    return Result;
}

static
ZP_STATUS
ZpExecution_QueryToken(
    _In_ ZP_EXECUTION_IDENTITY Identity,
    _In_ ULONG SessionId,
    _In_opt_ PCWSTR UserName,
    _In_opt_ PCWSTR Password,
    _Out_ PHANDLE Token)
{
    HANDLE UserToken, LinkedToken;
    TOKEN_ELEVATION Elevation;
    DWORD Length, Error, CurrentSessionId;
    ZP_STATUS Status;

    if (Identity == ZpExecutionIdentityCurrent) return ZpStatus_Make(ZpStatusNone, 0);
    if (Identity == ZpExecutionIdentitySystem) return ZpExecution_QuerySystemToken(SessionId, Token);
    if (Identity == ZpExecutionIdentityTrustedInstaller)
    {
        return ZpExecution_QueryTrustedInstallerToken(SessionId, Token);
    }
    if (Identity == ZpExecutionIdentityAdministrator &&
        ProcessIdToSessionId(GetCurrentProcessId(), &CurrentSessionId) && CurrentSessionId == SessionId)
    {
        if (!GetTokenInformation(NtCurrentProcessToken(),
                                 TokenElevation,
                                 &Elevation,
                                 sizeof(Elevation),
                                 &Length))
        {
            return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        }
        return Elevation.TokenIsElevated ? ZpStatus_Make(ZpStatusNone, 0) :
                                           ZpStatus_FromCode(ZpStatusWin32, ERROR_ELEVATION_REQUIRED);
    }
    if (Identity == ZpExecutionIdentityOtherUser)
    {
        if (UserName == NULL || Password == NULL)
        {
            return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
        }
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
    else if (!WTSQueryUserToken(SessionId, &UserToken))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (Identity == ZpExecutionIdentityAdministrator)
    {
        if (!GetTokenInformation(UserToken, TokenLinkedToken, &LinkedToken, sizeof(LinkedToken), &Length))
        {
            Error = GetLastError();
            NtClose(UserToken);
            return ZpStatus_FromCode(ZpStatusWin32, Error);
        }
        NtClose(UserToken);
        UserToken = LinkedToken;
        if (!GetTokenInformation(UserToken, TokenElevation, &Elevation, sizeof(Elevation), &Length) ||
            !Elevation.TokenIsElevated)
        {
            Error = GetLastError();
            NtClose(UserToken);
            return ZpStatus_FromCode(ZpStatusWin32, Error != ERROR_SUCCESS ? Error : ERROR_ELEVATION_REQUIRED);
        }
    }
    if (!SetTokenInformation(UserToken, TokenSessionId, &SessionId, sizeof(SessionId)))
    {
        Error = GetLastError();
        NtClose(UserToken);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    Status = ZpStatus_Make(ZpStatusNone, 0);
    *Token = UserToken;
    return Status;
}

static
VOID
ZpExecution_RefreshJob(
    _Inout_ PZP_EXECUTION_JOB Job)
{
    PROCESS_BASIC_INFORMATION Basic;
    KERNEL_USER_TIMES Times;

    if (Job->Record.State != ZpExecutionJobRunning || Job->Process == NULL ||
        PS_WaitForObject(Job->Process, 0) != STATUS_SUCCESS)
    {
        return;
    }
    if (NT_SUCCESS(NtQueryInformationProcess(Job->Process,
                                              ProcessBasicInformation,
                                              &Basic,
                                              sizeof(Basic),
                                              NULL)))
    {
        Job->Record.ExitCode = (ULONG)Basic.ExitStatus;
    }
    if (NT_SUCCESS(NtQueryInformationProcess(Job->Process,
                                              ProcessTimes,
                                              &Times,
                                              sizeof(Times),
                                              NULL)))
    {
        Job->Record.ExitTime = Times.ExitTime.QuadPart;
    }
    Job->Record.State = ZpExecutionJobExited;
    NtClose(Job->Process);
    Job->Process = NULL;
    if (FlagOn(Job->Record.Flags, ZP_EXECUTION_FLAG_DELETE_FILE))
    {
        IO_DeleteWin32File(Job->FileName, NULL);
        ClearFlag(Job->Record.Flags, ZP_EXECUTION_FLAG_DELETE_FILE);
    }
}

static
VOID
ZpExecution_FreeJob(
    _In_ PZP_EXECUTION_JOB Job)
{
    if (Job->Process != NULL) NtClose(Job->Process);
    Mem_Free(Job);
}

static
ZP_STATUS
ZpExecution_AddJob(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ HANDLE Process,
    _In_ ULONG ProcessId,
    _In_ ULONG SessionId,
    _In_ PCZP_EXECUTION_START_VIEW Start,
    _In_ PCWSTR FileName,
    _Outptr_ PZP_EXECUTION_JOB* AddedJob)
{
    PZP_EXECUTION_JOB Job, Oldest = NULL;
    PLIST_ENTRY Entry;
    KERNEL_USER_TIMES Times;
    SIZE_T AllocationSize;

    AllocationSize = FIELD_OFFSET(ZP_EXECUTION_JOB, FileName) + ((SIZE_T)Start->FileName.Length + 1) * sizeof(WCHAR);
    Job = Mem_Alloc(AllocationSize);
    if (Job == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    RtlZeroMemory(Job, FIELD_OFFSET(ZP_EXECUTION_JOB, FileName));
    RtlCopyMemory(Job->FileName, FileName, (SIZE_T)Start->FileName.Length * sizeof(WCHAR));
    Job->FileName[Start->FileName.Length] = UNICODE_NULL;
    Job->Process = Process;
    Job->Record.ProcessId = ProcessId;
    Job->Record.SessionId = SessionId;
    Job->Record.Flags = Start->Flags;
    Job->Record.Engine = Start->Engine;
    Job->Record.Identity = Start->Identity;
    Job->Record.State = Process != NULL ? ZpExecutionJobRunning : ZpExecutionJobExited;
    Job->Record.FileName = Job->FileName;
    Job->Record.FileNameLength = Start->FileName.Length;
    NtQuerySystemTime((PLARGE_INTEGER)&Job->Record.CreateTime);
    if (Process != NULL && NT_SUCCESS(NtQueryInformationProcess(Process,
                                                                 ProcessTimes,
                                                                 &Times,
                                                                 sizeof(Times),
                                                                 NULL)))
    {
        Job->Record.CreateTime = Times.CreateTime.QuadPart;
    }
    else if (Process == NULL)
    {
        Job->Record.ExitTime = Job->Record.CreateTime;
    }
    RtlAcquireSRWLockExclusive(&Client->ExecutionLock);
    if (Client->ExecutionJobCount == ZP_EXECUTION_MAX_JOBS)
    {
        for (Entry = Client->ExecutionJobs.Flink; Entry != &Client->ExecutionJobs; Entry = Entry->Flink)
        {
            PZP_EXECUTION_JOB Candidate = CONTAINING_RECORD(Entry, ZP_EXECUTION_JOB, ListEntry);
            ZpExecution_RefreshJob(Candidate);
            if (Candidate->Record.State == ZpExecutionJobExited)
            {
                Oldest = Candidate;
                RemoveEntryList(&Candidate->ListEntry);
                Client->ExecutionJobCount--;
                break;
            }
        }
    }
    if (Client->ExecutionJobCount == ZP_EXECUTION_MAX_JOBS)
    {
        RtlReleaseSRWLockExclusive(&Client->ExecutionLock);
        Job->Process = NULL;
        Mem_Free(Job);
        return ZpStatus_FromNtStatus(STATUS_QUOTA_EXCEEDED);
    }
    Job->Record.JobId = Client->NextExecutionJobId++;
    InsertTailList(&Client->ExecutionJobs, &Job->ListEntry);
    Client->ExecutionJobCount++;
    RtlReleaseSRWLockExclusive(&Client->ExecutionLock);
    if (Oldest != NULL) ZpExecution_FreeJob(Oldest);
    *AddedJob = Job;
    return ZpStatus_Make(ZpStatusNone, 0);
}

static
ZP_STATUS
ZpExecution_CreateAppContainerProcess(
    _In_ PCWSTR Sid,
    _In_ PCWSTR FileName,
    _Inout_ PWSTR CommandLine,
    _In_opt_ PCWSTR WorkingDirectory,
    _In_ BOOLEAN Hidden,
    _Out_ PPROCESS_INFORMATION ProcessInformation)
{
    ZP_APP_CONTAINER_SECURITY_CONTEXT SecurityContext;
    STARTUPINFOEXW Startup = { 0 };
    PPROC_THREAD_ATTRIBUTE_LIST Attributes;
    SIZE_T AttributesSize = 0;
    BOOLEAN AttributesInitialized = FALSE;
    ZP_STATUS Status;

    Status = ZpAppContainer_QuerySecurityCapabilities(Sid, &SecurityContext);
    if (!ZpStatus_IsSuccess(Status)) return Status;
    InitializeProcThreadAttributeList(NULL, 1, 0, &AttributesSize);
    Attributes = Mem_Alloc(AttributesSize);
    if (Attributes == NULL)
    {
        Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto Cleanup;
    }
    if (!InitializeProcThreadAttributeList(Attributes, 1, 0, &AttributesSize))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    AttributesInitialized = TRUE;
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
    Startup.StartupInfo.cb = sizeof(Startup);
    if (Hidden)
    {
        Startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
        Startup.StartupInfo.wShowWindow = SW_HIDE;
    }
    Startup.lpAttributeList = Attributes;
    if (!CreateProcessW(FileName,
                        CommandLine,
                        NULL,
                        NULL,
                        FALSE,
                        EXTENDED_STARTUPINFO_PRESENT |
                            NORMAL_PRIORITY_CLASS |
                            CREATE_NEW_CONSOLE |
                            CREATE_DEFAULT_ERROR_MODE,
                        NULL,
                        WorkingDirectory,
                        &Startup.StartupInfo,
                        ProcessInformation))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    else
    {
        Status = ZpStatus_Make(ZpStatusNone, 0);
    }
Cleanup:
    if (AttributesInitialized) DeleteProcThreadAttributeList(Attributes);
    Mem_Free(Attributes);
    ZpAppContainer_FreeSecurityCapabilities(&SecurityContext);
    return Status;
}

static
ZP_STATUS
ZpExecution_Start(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PCZP_EXECUTION_START_VIEW Start,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION ProcessInfo = { 0 };
    SHELLEXECUTEINFOW Shell = { sizeof(Shell) };
    HANDLE Token = NULL;
    PZP_EXECUTION_JOB Job;
    ZP_EXECUTION_JOB_RECORD Record;
    PWSTR FileName, Arguments, WorkingDirectory, Verb, UserName, Password, AppContainerSid, CommandLine = NULL;
    ULONG CurrentSessionId, SessionId, Length;
    INT CommandLineLength;
    PBYTE Buffer;
    W32ERROR Result;
    NTSTATUS Status;
    ZP_STATUS ExecutionStatus;

    FileName = ZpExecution_CopyString(&Start->FileName);
    Arguments = ZpExecution_CopyString(&Start->Arguments);
    WorkingDirectory = ZpExecution_CopyString(&Start->WorkingDirectory);
    Verb = ZpExecution_CopyString(&Start->Verb);
    UserName = ZpExecution_CopyString(&Start->UserName);
    Password = ZpExecution_CopyString(&Start->Password);
    AppContainerSid = ZpExecution_CopyString(&Start->AppContainerSid);
    if (FileName == NULL || Arguments == NULL || WorkingDirectory == NULL || Verb == NULL ||
        UserName == NULL || Password == NULL || AppContainerSid == NULL)
    {
        ExecutionStatus = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto Cleanup;
    }
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &CurrentSessionId))
    {
        ExecutionStatus = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    SessionId = Start->SessionId == ZP_EXECUTION_SESSION_CURRENT ? CurrentSessionId : Start->SessionId;
    if (Start->Engine == ZpExecutionEngineShellExecute)
    {
        if (CurrentSessionId == 0 || SessionId != CurrentSessionId || Start->Identity != ZpExecutionIdentityCurrent)
        {
            ExecutionStatus = ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
            goto Cleanup;
        }
        Shell.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        Shell.lpVerb = Start->Verb.Length != 0 ? Verb : NULL;
        Shell.lpFile = FileName;
        Shell.lpParameters = Start->Arguments.Length != 0 ? Arguments : NULL;
        Shell.lpDirectory = Start->WorkingDirectory.Length != 0 ? WorkingDirectory : NULL;
        Shell.nShow = FlagOn(Start->Flags, ZP_EXECUTION_FLAG_HIDDEN) ? SW_HIDE : SW_SHOWNORMAL;
        if (!ShellExecuteExW(&Shell))
        {
            ExecutionStatus = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
        ProcessInfo.hProcess = Shell.hProcess;
        ProcessInfo.dwProcessId = Shell.hProcess != NULL ? GetProcessId(Shell.hProcess) : 0;
    }
    else
    {
        if (Start->Identity == ZpExecutionIdentityCurrent && SessionId != CurrentSessionId)
        {
            ExecutionStatus = ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
            goto Cleanup;
        }
        CommandLineLength = _scwprintf(L"\"%s\"%s%s",
                                       FileName,
                                       Start->Arguments.Length != 0 ? L" " : L"",
                                       Arguments);
        if (CommandLineLength < 0)
        {
            ExecutionStatus = ZpStatus_FromNtStatus(STATUS_NAME_TOO_LONG);
            goto Cleanup;
        }
        CommandLine = Mem_Alloc(((SIZE_T)CommandLineLength + 1) * sizeof(WCHAR));
        if (CommandLine == NULL)
        {
            ExecutionStatus = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            goto Cleanup;
        }
        _snwprintf_s(CommandLine,
                     (SIZE_T)CommandLineLength + 1,
                     _TRUNCATE,
                     L"\"%s\"%s%s",
                     FileName,
                     Start->Arguments.Length != 0 ? L" " : L"",
                     Arguments);
        if (Start->Identity == ZpExecutionIdentityAppContainer)
        {
            ExecutionStatus = ZpExecution_CreateAppContainerProcess(
                AppContainerSid,
                FileName,
                CommandLine,
                Start->WorkingDirectory.Length != 0 ? WorkingDirectory : NULL,
                FlagOn(Start->Flags, ZP_EXECUTION_FLAG_HIDDEN),
                &ProcessInfo);
        }
        else
        {
            ExecutionStatus = ZpExecution_QueryToken(Start->Identity,
                                                      SessionId,
                                                      Start->UserName.Length != 0 ? UserName : NULL,
                                                      Start->Password.Length != 0 ? Password : NULL,
                                                      &Token);
            if (!ZpStatus_IsSuccess(ExecutionStatus)) goto Cleanup;
            Startup.lpDesktop = Token != NULL ? L"winsta0\\default" : NULL;
            if (FlagOn(Start->Flags, ZP_EXECUTION_FLAG_HIDDEN))
            {
                Startup.dwFlags = STARTF_USESHOWWINDOW;
                Startup.wShowWindow = SW_HIDE;
            }
            Result = PS_CreateProcess(Token,
                                      FileName,
                                      CommandLine,
                                      FALSE,
                                      Start->WorkingDirectory.Length != 0 ? WorkingDirectory : NULL,
                                      &Startup,
                                      &ProcessInfo);
            ExecutionStatus = Result == ERROR_SUCCESS ? ZpStatus_Make(ZpStatusNone, 0) :
                                                        ZpStatus_FromCode(ZpStatusWin32, Result);
        }
        if (!ZpStatus_IsSuccess(ExecutionStatus)) goto Cleanup;
        NtClose(ProcessInfo.hThread);
        ProcessInfo.hThread = NULL;
    }
    ExecutionStatus = ZpExecution_AddJob(Client,
                                          ProcessInfo.hProcess,
                                          ProcessInfo.dwProcessId,
                                          SessionId,
                                          Start,
                                          FileName,
                                          &Job);
    if (!ZpStatus_IsSuccess(ExecutionStatus))
    {
        if (ProcessInfo.hProcess != NULL) NtTerminateProcess(ProcessInfo.hProcess, STATUS_CANCELLED);
        goto Cleanup;
    }
    ProcessInfo.hProcess = NULL;
    Record = Job->Record;
    Status = ZpExecution_EncodeJobs(&Record, 1, NULL, 0, &Length);
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status)) Status = ZpExecution_EncodeJobs(&Record, 1, Buffer, Length, &Length);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        ExecutionStatus = ZpStatus_FromNtStatus(Status);
        goto Cleanup;
    }
    *Response = Buffer;
    *ResponseLength = Length;
Cleanup:
    if (ProcessInfo.hThread != NULL) NtClose(ProcessInfo.hThread);
    if (ProcessInfo.hProcess != NULL) NtClose(ProcessInfo.hProcess);
    if (Token != NULL) NtClose(Token);
    if (Password != NULL)
    {
        RtlSecureZeroMemory(Password, ((SIZE_T)Start->Password.Length + 1) * sizeof(WCHAR));
    }
    Mem_Free(CommandLine);
    Mem_Free(Password);
    Mem_Free(AppContainerSid);
    Mem_Free(UserName);
    Mem_Free(Verb);
    Mem_Free(WorkingDirectory);
    Mem_Free(Arguments);
    Mem_Free(FileName);
    return ExecutionStatus;
}

static
ZP_STATUS
ZpExecution_EnumerateJobs(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_EXECUTION_JOB_RECORD Records = NULL;
    PLIST_ENTRY Entry;
    ULONG Index = 0, Length;
    PBYTE Buffer;
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&Client->ExecutionLock);
    if (Client->ExecutionJobCount != 0)
    {
        Records = Mem_Alloc((SIZE_T)Client->ExecutionJobCount * sizeof(*Records));
        if (Records == NULL)
        {
            RtlReleaseSRWLockExclusive(&Client->ExecutionLock);
            return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
    }
    for (Entry = Client->ExecutionJobs.Flink; Entry != &Client->ExecutionJobs; Entry = Entry->Flink)
    {
        PZP_EXECUTION_JOB Job = CONTAINING_RECORD(Entry, ZP_EXECUTION_JOB, ListEntry);
        ZpExecution_RefreshJob(Job);
        Records[Index++] = Job->Record;
    }
    Status = ZpExecution_EncodeJobs(Records, Index, NULL, 0, &Length);
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status)) Status = ZpExecution_EncodeJobs(Records, Index, Buffer, Length, &Length);
    RtlReleaseSRWLockExclusive(&Client->ExecutionLock);
    Mem_Free(Records);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return ZpStatus_FromNtStatus(Status);
    }
    *Response = Buffer;
    *ResponseLength = Length;
    return ZpStatus_Make(ZpStatusNone, 0);
}

static
ZP_STATUS
ZpExecution_Terminate(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ ULONG JobId)
{
    PLIST_ENTRY Entry;
    ZP_STATUS Status = ZpStatus_FromNtStatus(STATUS_NOT_FOUND);

    RtlAcquireSRWLockExclusive(&Client->ExecutionLock);
    for (Entry = Client->ExecutionJobs.Flink; Entry != &Client->ExecutionJobs; Entry = Entry->Flink)
    {
        PZP_EXECUTION_JOB Job = CONTAINING_RECORD(Entry, ZP_EXECUTION_JOB, ListEntry);
        if (Job->Record.JobId != JobId) continue;
        ZpExecution_RefreshJob(Job);
        Status = Job->Record.State == ZpExecutionJobRunning ?
                     ZpStatus_FromNtStatus(NtTerminateProcess(Job->Process, STATUS_CANCELLED)) :
                     ZpStatus_FromNtStatus(STATUS_PROCESS_IS_TERMINATING);
        break;
    }
    RtlReleaseSRWLockExclusive(&Client->ExecutionLock);
    return Status;
}

static
ZP_STATUS
ZpExecution_CreateStaging(
    _In_ PCZP_STRING_VIEW Name,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    WCHAR RandomString[17];
    PWSTR Path, FileName;
    ULONGLONG RandomValue;
    ULONG Index, TempLength, PathCapacity, PathLength, Length;
    PBYTE Buffer;
    HRESULT Result;
    NTSTATUS Status;

    if (Name->Length == 0 || Name->Length > ZP_EXECUTION_MAX_STAGING_NAME)
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    FileName = ZpExecution_CopyString(Name);
    if (FileName == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    if (wcspbrk(FileName, L"\\/:*?\"<>|") != NULL ||
        wcscmp(FileName, L".") == 0 || wcscmp(FileName, L"..") == 0)
    {
        Mem_Free(FileName);
        return ZpStatus_FromNtStatus(STATUS_OBJECT_NAME_INVALID);
    }
    TempLength = GetTempPath2W(0, NULL);
    PathCapacity = TempLength + 19 + Name->Length;
    Path = TempLength != 0 ? Mem_Alloc((SIZE_T)PathCapacity * sizeof(WCHAR)) : NULL;
    if (TempLength == 0)
    {
        Mem_Free(FileName);
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (Path == NULL)
    {
        Mem_Free(FileName);
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    PathLength = GetTempPath2W(PathCapacity, Path);
    Status = PathLength != 0 && PathLength < PathCapacity ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
    if (NT_SUCCESS(Status)) Status = BCryptGenRandom(NULL,
                                                     (PBYTE)&RandomValue,
                                                     sizeof(RandomValue),
                                                     BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (NT_SUCCESS(Status))
    {
        Result = Str_PrintfExW(RandomString, ARRAYSIZE(RandomString), L"%016llX", RandomValue);
        if (FAILED(Result)) Status = STATUS_UNSUCCESSFUL;
    }
    if (NT_SUCCESS(Status))
    {
        Result = Str_PrintfExW(Path + PathLength,
                               PathCapacity - PathLength,
                               L"ZPigeon-%s-%s",
                               RandomString,
                               FileName);
        if (FAILED(Result)) Status = STATUS_NAME_TOO_LONG;
    }
    Mem_Free(FileName);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Path);
        return ZpStatus_FromNtStatus(Status);
    }
    Length = (ULONG)wcslen(Path);
    Status = ZpExecution_EncodeStaging(Path, Length, NULL, 0, &Index);
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(Index) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status)) Status = ZpExecution_EncodeStaging(Path, Length, Buffer, Index, &Index);
    Mem_Free(Path);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return ZpStatus_FromNtStatus(Status);
    }
    *Response = Buffer;
    *ResponseLength = Index;
    return ZpStatus_Make(ZpStatusNone, 0);
}

ZP_STATUS
ZpExecution_Execute(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_EXECUTION_START_VIEW Start;
    ZP_STRING_VIEW Name;
    ULONG JobId;
    NTSTATUS Status;

    switch (OperationId)
    {
        case ZP_EXECUTION_OPERATION_ENUMERATE_SESSIONS:
            return RequestLength == 0 ?
                       ZpExecution_EncodeSessionsResponse(Response, ResponseLength) :
                       ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);

        case ZP_EXECUTION_OPERATION_START:
            Status = ZpExecution_DecodeStart(Request, RequestLength, &Start);
            return NT_SUCCESS(Status) ? ZpExecution_Start(Client, &Start, Response, ResponseLength) :
                                        ZpStatus_FromNtStatus(Status);

        case ZP_EXECUTION_OPERATION_ENUMERATE_JOBS:
            return RequestLength == 0 ? ZpExecution_EnumerateJobs(Client, Response, ResponseLength) :
                                        ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);

        case ZP_EXECUTION_OPERATION_TERMINATE:
            Status = ZpExecution_DecodeJobId(Request, RequestLength, &JobId);
            if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
            *Response = NULL;
            *ResponseLength = 0;
            return ZpExecution_Terminate(Client, JobId);

        case ZP_EXECUTION_OPERATION_CREATE_STAGING:
            Status = ZpExecution_DecodeStaging(Request, RequestLength, &Name);
            return NT_SUCCESS(Status) ? ZpExecution_CreateStaging(&Name, Response, ResponseLength) :
                                        ZpStatus_FromNtStatus(Status);

        default:
            return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
}

VOID
ZpExecution_Cleanup(
    _Inout_ PZP_CLIENT_OBJECT Client)
{
    PLIST_ENTRY Entry;

    while (!IsListEmpty(&Client->ExecutionJobs))
    {
        Entry = RemoveHeadList(&Client->ExecutionJobs);
        ZpExecution_FreeJob(CONTAINING_RECORD(Entry, ZP_EXECUTION_JOB, ListEntry));
    }
}
