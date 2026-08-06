#include "Client.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include "Process.h"
#include "Runtime.h"
#include <KNSoft/NDK/Win32/API/WinSta.h>
#include <ShellApi.h>

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "KNSoft.NDK.WinAPI.lib")
#pragma comment(lib, "Shell32.lib")

#define ZP_EXECUTION_MAX_JOBS 256
#define ZP_EXECUTION_MAX_STAGING_NAME 128

typedef struct _ZP_EXECUTION_RUNTIME_CONTEXT
{
    ZP_EXECUTION_RUNTIME_RECORD Records[16];
    PWSTR Paths[16];
    ULONG Count;
    NTSTATUS Status;
} ZP_EXECUTION_RUNTIME_CONTEXT, *PZP_EXECUTION_RUNTIME_CONTEXT;

typedef struct _ZP_EXECUTION_JOB
{
    LIST_ENTRY ListEntry;
    HANDLE Process;
    HANDLE JobObject;
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
BOOL
NTAPI
ZpExecution_CollectRuntime(
    _In_ BYTE Kind,
    _In_ PCWSTR Path,
    _In_ PCZP_EXECUTION_IMAGE_INFO Image,
    _In_opt_ PVOID Context)
{
    PZP_EXECUTION_RUNTIME_CONTEXT RuntimeContext = Context;
    SIZE_T Length;
    PWSTR Copy;

    if (RuntimeContext->Count == ARRAYSIZE(RuntimeContext->Records))
    {
        RuntimeContext->Status = STATUS_BUFFER_OVERFLOW;
        return FALSE;
    }
    Length = wcslen(Path);
    if (Length > MAXULONG)
    {
        RuntimeContext->Status = STATUS_NAME_TOO_LONG;
        return FALSE;
    }
    Copy = Mem_Alloc((Length + 1) * sizeof(WCHAR));
    if (Copy == NULL)
    {
        RuntimeContext->Status = STATUS_NO_MEMORY;
        return FALSE;
    }
    RtlCopyMemory(Copy, Path, (Length + 1) * sizeof(WCHAR));
    RuntimeContext->Paths[RuntimeContext->Count] = Copy;
    RuntimeContext->Records[RuntimeContext->Count].Kind = Kind;
    RuntimeContext->Records[RuntimeContext->Count].Image = *Image;
    RuntimeContext->Records[RuntimeContext->Count].Path = Copy;
    RuntimeContext->Records[RuntimeContext->Count].PathLength = (ULONG)Length;
    RuntimeContext->Count++;
    return TRUE;
}

static
ZP_STATUS
ZpExecution_QueryEnvironment(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_EXECUTION_RUNTIME_CONTEXT Context = { 0 };
    SID_2 Administrators = SID_BUILTIN_ADMINISTRATORS;
    TOKEN_ELEVATION Elevation;
    DWORD Length;
    BOOL Member;
    ULONG Flags = 0, Index;
    HRESULT Result;
    NTSTATUS Status;

    if (CheckTokenMembership(NULL, &Administrators.BaseType, &Member) && Member &&
        GetTokenInformation(NtCurrentProcessToken(), TokenElevation, &Elevation, sizeof(Elevation), &Length) &&
        Elevation.TokenIsElevated)
    {
        Flags |= ZP_EXECUTION_ENVIRONMENT_FLAG_ADMINISTRATOR;
    }
    Result = ZpRuntime_Enumerate(ZpExecution_CollectRuntime, &Context);
    Status = SUCCEEDED(Result) && NT_SUCCESS(Context.Status) ?
                 ZpExecution_EncodeEnvironment(Flags,
                                               Context.Records,
                                               Context.Count,
                                               NULL,
                                               0,
                                               ResponseLength) :
                 FAILED(Result) ? STATUS_UNSUCCESSFUL : Context.Status;
    if (NT_SUCCESS(Status))
    {
        *Response = Mem_Alloc(*ResponseLength);
        if (*Response == NULL) Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpExecution_EncodeEnvironment(Flags,
                                               Context.Records,
                                               Context.Count,
                                               *Response,
                                               *ResponseLength,
                                               ResponseLength);
    }
    for (Index = 0; Index < Context.Count; Index++) Mem_Free(Context.Paths[Index]);
    return SUCCEEDED(Result) ? ZpStatus_FromNtStatus(Status) : ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result);
}

static
ZP_STATUS
ZpExecution_QueryImage(
    _In_ PCZP_STRING_VIEW Path,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_EXECUTION_IMAGE_INFO Image;
    PWSTR Value;
    HRESULT Result;
    NTSTATUS Status;

    Value = ZpExecution_CopyString(Path);
    if (Value == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Result = ZpRuntime_QueryImage(Value, &Image);
    Mem_Free(Value);
    if (FAILED(Result)) return ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result);
    Status = ZpExecution_EncodeImageInfo(&Image, NULL, 0, ResponseLength);
    if (NT_SUCCESS(Status))
    {
        *Response = Mem_Alloc(*ResponseLength);
        if (*Response == NULL) Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpExecution_EncodeImageInfo(&Image, *Response, *ResponseLength, ResponseLength);
    }
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpExecution_EncodeSessionsResponse(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PSESSIONIDW Sessions = NULL;
    PZP_EXECUTION_SESSION_RECORD Records = NULL;
    PWSTR* Names = NULL;
    ULONG Count = 0, Index;
    DWORD ClientSessionId;
    NTSTATUS Status;
    ULONG Length;
    PBYTE Buffer;

    if (!WinStationEnumerateW(WINSTATION_CURRENT_SERVER, &Sessions, &Count))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &ClientSessionId))
    {
        if (Sessions != NULL) WinStationFreeMemory(Sessions);
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Count * (sizeof(*Records) + sizeof(*Names)));
        if (Records == NULL)
        {
            WinStationFreeMemory(Sessions);
            return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
        RtlZeroMemory(Records, (SIZE_T)Count * (sizeof(*Records) + sizeof(*Names)));
        Names = (PWSTR*)(Records + Count);
    }
    for (Index = 0; Index < Count; Index++)
    {
        WINSTATIONINFORMATION Information;
        ULONG ReturnLength;
        ULONG UserLength, DomainLength;

        Records[Index].SessionId = Sessions[Index].SessionId;
        Records[Index].State = Sessions[Index].State;
        Records[Index].Flags = (Sessions[Index].SessionId == ClientSessionId ? ZP_EXECUTION_SESSION_FLAG_CLIENT : 0) |
                               (Sessions[Index].State == State_Active ? ZP_EXECUTION_SESSION_FLAG_ACTIVE : 0);
        Records[Index].StationName = Sessions[Index].WinStationName;
        Records[Index].StationNameLength = (ULONG)wcslen(Sessions[Index].WinStationName);
        if (!WinStationQueryInformationW(WINSTATION_CURRENT_SERVER,
                                         Sessions[Index].SessionId,
                                         WinStationInformation,
                                         &Information,
                                         sizeof(Information),
                                         &ReturnLength))
        {
            continue;
        }
        UserLength = (ULONG)wcslen(Information.UserName);
        DomainLength = (ULONG)wcslen(Information.Domain);
        if (UserLength != 0)
        {
            Names[Index] = Mem_Alloc(((SIZE_T)DomainLength + UserLength + (DomainLength != 0 ? 2 : 1)) *
                                     sizeof(WCHAR));
            if (Names[Index] == NULL)
            {
                Status = STATUS_NO_MEMORY;
                goto Cleanup;
            }
            if (DomainLength != 0)
            {
                RtlCopyMemory(Names[Index], Information.Domain, (SIZE_T)DomainLength * sizeof(WCHAR));
                Names[Index][DomainLength] = L'\\';
                RtlCopyMemory(Names[Index] + DomainLength + 1,
                              Information.UserName,
                              (SIZE_T)UserLength * sizeof(WCHAR));
                Records[Index].UserNameLength = DomainLength + UserLength + 1;
            }
            else
            {
                RtlCopyMemory(Names[Index], Information.UserName, (SIZE_T)UserLength * sizeof(WCHAR));
                Records[Index].UserNameLength = UserLength;
            }
            Names[Index][Records[Index].UserNameLength] = UNICODE_NULL;
            Records[Index].UserName = Names[Index];
        }
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
    if (Sessions != NULL) WinStationFreeMemory(Sessions);
    return ZpStatus_FromNtStatus(Status);
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
    if (Job->JobObject != NULL)
    {
        NtClose(Job->JobObject);
        Job->JobObject = NULL;
    }
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
    if (Job->JobObject != NULL) NtClose(Job->JobObject);
    if (Job->Process != NULL) NtClose(Job->Process);
    Mem_Free(Job);
}

static
ZP_STATUS
ZpExecution_AddJob(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ HANDLE Process,
    _In_opt_ HANDLE JobObject,
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
    NTSTATUS Status;

    AllocationSize = FIELD_OFFSET(ZP_EXECUTION_JOB, FileName) + ((SIZE_T)Start->FileName.Length + 1) * sizeof(WCHAR);
    Job = Mem_Alloc(AllocationSize);
    if (Job == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    RtlZeroMemory(Job, FIELD_OFFSET(ZP_EXECUTION_JOB, FileName));
    RtlCopyMemory(Job->FileName, FileName, (SIZE_T)Start->FileName.Length * sizeof(WCHAR));
    Job->FileName[Start->FileName.Length] = UNICODE_NULL;
    Job->Process = Process;
    Job->JobObject = JobObject;
    Job->Record.ProcessId = ProcessId;
    Job->Record.SessionId = SessionId;
    Job->Record.Flags = Start->Flags;
    Job->Record.Engine = Start->Engine;
    Job->Record.Identity = Start->Identity;
    Job->Record.State = Process != NULL ? ZpExecutionJobRunning : ZpExecutionJobExited;
    Job->Record.FileName = Job->FileName;
    Job->Record.FileNameLength = Start->FileName.Length;
    if (Process != NULL)
    {
        Status = NtQueryInformationProcess(Process, ProcessTimes, &Times, sizeof(Times), NULL);
        if (!NT_SUCCESS(Status))
        {
            Job->Process = NULL;
            Job->JobObject = NULL;
            Mem_Free(Job);
            return ZpStatus_FromNtStatus(Status);
        }
        Job->Record.CreateTime = Times.CreateTime.QuadPart;
    }
    else
    {
        NtQuerySystemTime((PLARGE_INTEGER)&Job->Record.CreateTime);
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
        Job->JobObject = NULL;
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
ZpExecution_CreateJobObject(
    _Out_ PHANDLE JobObject)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits = { 0 };
    HANDLE Job;
    DWORD Error;

    Job = CreateJobObjectW(NULL, NULL);
    if (Job == NULL) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(Job, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)))
    {
        Error = GetLastError();
        NtClose(Job);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    *JobObject = Job;
    return ZpStatus_Make(ZpStatusNone, 0);
}

static
ZP_STATUS
ZpExecution_Start(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PCZP_EXECUTION_START_VIEW Start,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PROCESS_INFORMATION ProcessInfo = { 0 };
    SHELLEXECUTEINFOW Shell = { sizeof(Shell) };
    HANDLE JobObject = NULL;
    PZP_EXECUTION_JOB Job;
    ZP_EXECUTION_JOB_RECORD Record;
    PWSTR FileName, Arguments, WorkingDirectory, Verb;
    ULONG CurrentSessionId, SessionId = 0, Length;
    PBYTE Buffer;
    NTSTATUS Status;
    ZP_STATUS ExecutionStatus;

    FileName = ZpExecution_CopyString(&Start->FileName);
    Arguments = ZpExecution_CopyString(&Start->Arguments);
    WorkingDirectory = ZpExecution_CopyString(&Start->WorkingDirectory);
    Verb = ZpExecution_CopyString(&Start->Verb);
    if (FileName == NULL || Arguments == NULL || WorkingDirectory == NULL || Verb == NULL)
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
    if (FlagOn(Start->Flags, ZP_EXECUTION_FLAG_JOB_OBJECT))
    {
        ExecutionStatus = ZpExecution_CreateJobObject(&JobObject);
        if (!ZpStatus_IsSuccess(ExecutionStatus)) goto Cleanup;
    }
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
        ExecutionStatus = ZpProcess_Launch(Start, NULL, JobObject, &ProcessInfo, &SessionId);
        if (!ZpStatus_IsSuccess(ExecutionStatus)) goto Cleanup;
        NtClose(ProcessInfo.hThread);
        ProcessInfo.hThread = NULL;
    }
    ExecutionStatus = ZpExecution_AddJob(Client,
                                          ProcessInfo.hProcess,
                                          JobObject,
                                          ProcessInfo.dwProcessId,
                                          SessionId,
                                          Start,
                                          FileName,
                                          &Job);
    if (!ZpStatus_IsSuccess(ExecutionStatus))
    {
        if (ProcessInfo.hProcess != NULL && JobObject == NULL)
        {
            KERNEL_USER_TIMES Times;
            LONGLONG CreateTime = 0;

            if (NT_SUCCESS(NtQueryInformationProcess(ProcessInfo.hProcess,
                                                      ProcessTimes,
                                                      &Times,
                                                      sizeof(Times),
                                                      NULL)))
            {
                CreateTime = Times.CreateTime.QuadPart;
            }
            ZpProcess_TerminateTree(ProcessInfo.hProcess,
                                    ProcessInfo.dwProcessId,
                                    CreateTime,
                                    STATUS_CANCELLED);
        }
        goto Cleanup;
    }
    ProcessInfo.hProcess = NULL;
    JobObject = NULL;
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
    if (JobObject != NULL) NtClose(JobObject);
    if (ProcessInfo.hThread != NULL) NtClose(ProcessInfo.hThread);
    if (ProcessInfo.hProcess != NULL) NtClose(ProcessInfo.hProcess);
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
        if (Job->Record.State != ZpExecutionJobRunning)
        {
            Status = ZpStatus_FromNtStatus(STATUS_PROCESS_IS_TERMINATING);
        }
        else if (Job->JobObject != NULL)
        {
            Status = TerminateJobObject(Job->JobObject, ERROR_CANCELLED) ?
                         ZpStatus_Make(ZpStatusNone, 0) :
                         ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        }
        else
        {
            Status = ZpStatus_FromNtStatus(ZpProcess_TerminateTree(Job->Process,
                                                                   Job->Record.ProcessId,
                                                                   (LONGLONG)Job->Record.CreateTime,
                                                                   STATUS_CANCELLED));
        }
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
    ULONG Index, TempLength, PathCapacity, PathLength, Length, PrintLength;
    PBYTE Buffer;
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
    PathCapacity = TempLength + ARRAYSIZE(L"ZPigeon-0000000000000000-") + Name->Length;
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
        PrintLength = Str_PrintfExW(RandomString, ARRAYSIZE(RandomString), L"%016llX", RandomValue);
        if (PrintLength == 0) Status = STATUS_UNSUCCESSFUL;
    }
    if (NT_SUCCESS(Status))
    {
        PrintLength = Str_PrintfExW(Path + PathLength,
                                    PathCapacity - PathLength,
                                    L"ZPigeon-%s-%s",
                                    RandomString,
                                    FileName);
        if (PrintLength == 0) Status = STATUS_NAME_TOO_LONG;
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

    if (OperationId == ZP_EXECUTION_OPERATION_ENUMERATE_SESSIONS)
    {
        return RequestLength == 0 ?
                   ZpExecution_EncodeSessionsResponse(Response, ResponseLength) :
                   ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    else if (OperationId == ZP_EXECUTION_OPERATION_QUERY_ENVIRONMENT)
    {
        return RequestLength == 0 ?
                   ZpExecution_QueryEnvironment(Response, ResponseLength) :
                   ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    else if (OperationId == ZP_EXECUTION_OPERATION_QUERY_IMAGE)
    {
        Status = ZpExecution_DecodeStaging(Request, RequestLength, &Name);
        return NT_SUCCESS(Status) ? ZpExecution_QueryImage(&Name, Response, ResponseLength) :
                                    ZpStatus_FromNtStatus(Status);
    }
    else if (OperationId == ZP_EXECUTION_OPERATION_START)
    {
        Status = ZpExecution_DecodeStart(Request, RequestLength, &Start);
        return NT_SUCCESS(Status) ? ZpExecution_Start(Client, &Start, Response, ResponseLength) :
                                    ZpStatus_FromNtStatus(Status);
    }
    else if (OperationId == ZP_EXECUTION_OPERATION_ENUMERATE_JOBS)
    {
        return RequestLength == 0 ? ZpExecution_EnumerateJobs(Client, Response, ResponseLength) :
                                    ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    else if (OperationId == ZP_EXECUTION_OPERATION_TERMINATE)
    {
        Status = ZpExecution_DecodeJobId(Request, RequestLength, &JobId);
        return NT_SUCCESS(Status) ? ZpExecution_Terminate(Client, JobId) : ZpStatus_FromNtStatus(Status);
    }
    else if (OperationId == ZP_EXECUTION_OPERATION_CREATE_STAGING)
    {
        Status = ZpExecution_DecodeStaging(Request, RequestLength, &Name);
        return NT_SUCCESS(Status) ? ZpExecution_CreateStaging(&Name, Response, ResponseLength) :
                                    ZpStatus_FromNtStatus(Status);
    }
    return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
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
