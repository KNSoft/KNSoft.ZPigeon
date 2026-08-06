#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#include "ProcessCapture.h"

#define ZP_PROCESS_COMMAND_LINE_LENGTH 32767

static
NTSTATUS
ZpAdministration_AppendProcessArgument(
    _Inout_updates_(Capacity) PWSTR CommandLine,
    _In_ ULONG Capacity,
    _Inout_ PULONG Length,
    _In_ PCWSTR Argument)
{
    ULONG ArgumentLength, Backslashes = 0, Index, Required;

    if (Argument == NULL) return STATUS_INVALID_PARAMETER;
    if (*Argument != UNICODE_NULL && wcspbrk(Argument, L" \t\"") == NULL)
    {
        ArgumentLength = (ULONG)wcslen(Argument);
        Required = *Length + (*Length != 0) + ArgumentLength;
        if (Required >= Capacity) return STATUS_NAME_TOO_LONG;
        if (*Length != 0) CommandLine[(*Length)++] = L' ';
        RtlCopyMemory(CommandLine + *Length, Argument, (SIZE_T)ArgumentLength * sizeof(WCHAR));
        *Length += ArgumentLength;
        CommandLine[*Length] = UNICODE_NULL;
        return STATUS_SUCCESS;
    }
    Required = *Length + (*Length != 0) + 2;
    for (Index = 0; Argument[Index] != UNICODE_NULL; Index++)
    {
        if (Argument[Index] == L'\\')
        {
            Backslashes++;
        }
        else
        {
            Required += Backslashes + 1;
            if (Argument[Index] == L'"') Required += Backslashes + 1;
            Backslashes = 0;
        }
        if (Required > Capacity) return STATUS_NAME_TOO_LONG;
    }
    Required += Backslashes * 2;
    if (Required >= Capacity) return STATUS_NAME_TOO_LONG;
    if (*Length != 0) CommandLine[(*Length)++] = L' ';
    CommandLine[(*Length)++] = L'"';
    Backslashes = 0;
    for (Index = 0; Argument[Index] != UNICODE_NULL; Index++)
    {
        if (Argument[Index] == L'\\')
        {
            Backslashes++;
            continue;
        }
        while (Backslashes != 0)
        {
            CommandLine[(*Length)++] = L'\\';
            if (Argument[Index] == L'"') CommandLine[(*Length)++] = L'\\';
            Backslashes--;
        }
        if (Argument[Index] == L'"') CommandLine[(*Length)++] = L'\\';
        CommandLine[(*Length)++] = Argument[Index];
    }
    while (Backslashes != 0)
    {
        CommandLine[(*Length)++] = L'\\';
        CommandLine[(*Length)++] = L'\\';
        Backslashes--;
    }
    CommandLine[(*Length)++] = L'"';
    CommandLine[*Length] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpAdministration_AppendProcessOutput(
    _Inout_ PBYTE* Buffer,
    _Inout_ PULONG Length,
    _Inout_ PULONG Capacity,
    _In_ ULONG MaximumLength,
    _In_reads_bytes_(DataLength) const BYTE* Data,
    _In_ ULONG DataLength)
{
    PBYTE Value;
    ULONG Required, NewCapacity;

    if (DataLength > MaximumLength - *Length) return STATUS_QUOTA_EXCEEDED;
    Required = *Length + DataLength;
    if (Required > *Capacity)
    {
        NewCapacity = min(MaximumLength, max(*Capacity == 0 ? 4096 : *Capacity * 2, Required));
        Value = Mem_ReAlloc(*Buffer, NewCapacity);
        if (Value == NULL) return STATUS_NO_MEMORY;
        *Buffer = Value;
        *Capacity = NewCapacity;
    }
    RtlCopyMemory(*Buffer + *Length, Data, DataLength);
    *Length = Required;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpAdministration_RunProcess(
    _In_ PCWSTR Application,
    _In_reads_(ArgumentCount) PCWSTR const* Arguments,
    _In_ ULONG ArgumentCount,
    _In_ ULONG MaximumOutputLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ BOOLEAN CaptureStandardError,
    _Outptr_result_bytebuffer_maybenull_(*OutputLength) PBYTE* Output,
    _Out_ PULONG OutputLength,
    _Out_ PULONG ExitCode)
{
    SECURITY_ATTRIBUTES Security = { sizeof(Security), NULL, TRUE };
    STARTUPINFOEXW Startup = { 0 };
    PROCESS_INFORMATION Process;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION JobLimits = { 0 };
    PPROC_THREAD_ATTRIBUTE_LIST Attributes;
    HANDLE ReadPipe, WritePipe, Input, ErrorOutput, Job, InheritedHandles[3];
    PWSTR CommandLine;
    PBYTE Buffer = NULL;
    BYTE Chunk[4096];
    ULONG ArgumentIndex, Capacity = 0, Length = 0;
    SIZE_T AttributesSize = 0;
    ULONGLONG StartTime;
    DWORD Available, Read, Wait, Error = ERROR_SUCCESS;
    NTSTATUS Status;

    if (Application == NULL || *Application == UNICODE_NULL || MaximumOutputLength == 0 ||
        TimeoutMilliseconds == 0 || (ArgumentCount != 0 && Arguments == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    CommandLine = Mem_Alloc((SIZE_T)ZP_PROCESS_COMMAND_LINE_LENGTH * sizeof(WCHAR));
    if (CommandLine == NULL) return STATUS_NO_MEMORY;
    Status = ZpAdministration_AppendProcessArgument(CommandLine,
                                                     ZP_PROCESS_COMMAND_LINE_LENGTH,
                                                     &Length,
                                                     Application);
    for (ArgumentIndex = 0; NT_SUCCESS(Status) && ArgumentIndex < ArgumentCount; ArgumentIndex++)
    {
        Status = ZpAdministration_AppendProcessArgument(CommandLine,
                                                         ZP_PROCESS_COMMAND_LINE_LENGTH,
                                                         &Length,
                                                         Arguments[ArgumentIndex]);
    }
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(CommandLine);
        return Status;
    }
    if (!CreatePipe(&ReadPipe, &WritePipe, &Security, 0))
    {
        Error = GetLastError();
        Mem_Free(CommandLine);
        return NTSTATUS_FROM_WIN32(Error);
    }
    if (!SetHandleInformation(ReadPipe, HANDLE_FLAG_INHERIT, 0))
    {
        Error = GetLastError();
        goto CleanupPipe;
    }
    Input = CreateFileW(L"NUL",
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        &Security,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (Input == INVALID_HANDLE_VALUE)
    {
        Error = GetLastError();
        goto CleanupPipe;
    }
    ErrorOutput = CaptureStandardError ? WritePipe :
                      CreateFileW(L"NUL",
                                  GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &Security,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  NULL);
    if (ErrorOutput == INVALID_HANDLE_VALUE)
    {
        Error = GetLastError();
        goto CleanupInput;
    }
    InitializeProcThreadAttributeList(NULL, 1, 0, &AttributesSize);
    Attributes = Mem_Alloc(AttributesSize);
    if (Attributes == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto CleanupErrorOutput;
    }
    if (!InitializeProcThreadAttributeList(Attributes, 1, 0, &AttributesSize))
    {
        Error = GetLastError();
        goto CleanupAttributesBuffer;
    }
    InheritedHandles[0] = Input;
    InheritedHandles[1] = WritePipe;
    InheritedHandles[2] = ErrorOutput;
    if (!UpdateProcThreadAttribute(Attributes,
                                   0,
                                   PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   InheritedHandles,
                                   CaptureStandardError ? sizeof(HANDLE) * 2 : sizeof(InheritedHandles),
                                   NULL,
                                   NULL))
    {
        Error = GetLastError();
        goto CleanupAttributes;
    }
    Startup.StartupInfo.cb = sizeof(Startup);
    Startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    Startup.StartupInfo.hStdInput = Input;
    Startup.StartupInfo.hStdOutput = WritePipe;
    Startup.StartupInfo.hStdError = ErrorOutput;
    Startup.lpAttributeList = Attributes;
    if (!CreateProcessW(Application,
                        CommandLine,
                        NULL,
                        NULL,
                        TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                            EXTENDED_STARTUPINFO_PRESENT,
                        NULL,
                        NULL,
                        &Startup.StartupInfo,
                        &Process))
    {
        Error = GetLastError();
        goto CleanupAttributes;
    }
    Job = CreateJobObjectW(NULL, NULL);
    if (Job == NULL)
    {
        Error = GetLastError();
        TerminateProcess(Process.hProcess, Error);
        goto CleanupProcess;
    }
    JobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(Job, JobObjectExtendedLimitInformation, &JobLimits, sizeof(JobLimits)) ||
        !AssignProcessToJobObject(Job, Process.hProcess))
    {
        Error = GetLastError();
        TerminateProcess(Process.hProcess, Error);
        goto CleanupJob;
    }
    if (ResumeThread(Process.hThread) == MAXDWORD)
    {
        Error = GetLastError();
        TerminateProcess(Process.hProcess, Error);
        goto CleanupJob;
    }
    DeleteProcThreadAttributeList(Attributes);
    Mem_Free(Attributes);
    Mem_Free(CommandLine);
    CloseHandle(Input);
    if (!CaptureStandardError) CloseHandle(ErrorOutput);
    CloseHandle(Process.hThread);
    CloseHandle(WritePipe);
    Length = 0;
    StartTime = GetTickCount64();
    for (;;)
    {
        for (;;)
        {
            Available = 0;
            if (!PeekNamedPipe(ReadPipe, NULL, 0, NULL, &Available, NULL))
            {
                Error = GetLastError();
                if (Error != ERROR_BROKEN_PIPE)
                {
                    Status = NTSTATUS_FROM_WIN32(Error);
                    goto CleanupRunningProcess;
                }
                break;
            }
            if (Available == 0) break;
            if (!ReadFile(ReadPipe, Chunk, min((DWORD)sizeof(Chunk), Available), &Read, NULL))
            {
                Error = GetLastError();
                if (Error == ERROR_BROKEN_PIPE) break;
                Status = NTSTATUS_FROM_WIN32(Error);
                goto CleanupRunningProcess;
            }
            Status = ZpAdministration_AppendProcessOutput(&Buffer,
                                                           &Length,
                                                           &Capacity,
                                                           MaximumOutputLength,
                                                           Chunk,
                                                           Read);
            if (!NT_SUCCESS(Status))
            {
                TerminateJobObject(Job, ERROR_BUFFER_OVERFLOW);
                goto CleanupRunningProcess;
            }
        }
        Wait = WaitForSingleObject(Process.hProcess, 50);
        if (Wait == WAIT_OBJECT_0)
        {
            Available = 0;
            if (!PeekNamedPipe(ReadPipe, NULL, 0, NULL, &Available, NULL) && GetLastError() != ERROR_BROKEN_PIPE)
            {
                Status = NTSTATUS_FROM_WIN32(GetLastError());
                goto CleanupRunningProcess;
            }
            if (Available == 0) break;
        }
        else if (Wait == WAIT_FAILED)
        {
            Status = NTSTATUS_FROM_WIN32(GetLastError());
            goto CleanupRunningProcess;
        }
        if (TimeoutMilliseconds != INFINITE && GetTickCount64() - StartTime >= TimeoutMilliseconds)
        {
            TerminateJobObject(Job, ERROR_TIMEOUT);
            Status = STATUS_IO_TIMEOUT;
            goto CleanupRunningProcess;
        }
    }
    if (!GetExitCodeProcess(Process.hProcess, ExitCode))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto CleanupRunningProcess;
    }
    *Output = Buffer;
    *OutputLength = Length;
    Buffer = NULL;
    Status = STATUS_SUCCESS;
CleanupRunningProcess:
    CloseHandle(ReadPipe);
    CloseHandle(Job);
    CloseHandle(Process.hProcess);
    Mem_Free(Buffer);
    return Status;

CleanupJob:
    CloseHandle(Job);
CleanupProcess:
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
CleanupAttributes:
    DeleteProcThreadAttributeList(Attributes);
CleanupAttributesBuffer:
    Mem_Free(Attributes);
CleanupErrorOutput:
    if (!CaptureStandardError) CloseHandle(ErrorOutput);
CleanupInput:
    CloseHandle(Input);
CleanupPipe:
    CloseHandle(WritePipe);
    CloseHandle(ReadPipe);
    Mem_Free(CommandLine);
    return NT_SUCCESS(Status) ? NTSTATUS_FROM_WIN32(Error) : Status;
}
