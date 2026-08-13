#include "Client.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

static
NTSTATUS
ZpProcess_Enumerate(
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    PSYSTEM_PROCESS_INFORMATION Entry;
    PZP_PROCESS_RECORD Processes;
    PVOID SystemInfo;
    PBYTE Buffer;
    NTSTATUS Status;
    ULONG Count = 0, Index, Length;

    Status = Sys_QueryDynamicInfo(SystemProcessInformation, &SystemInfo);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Entry = SystemInfo;
    do
    {
        Count++;
        Entry = Entry->NextEntryOffset != 0 ?
                    Add2Ptr(Entry, Entry->NextEntryOffset) :
                    NULL;
    } while (Entry != NULL);
    Processes = Mem_Alloc((SIZE_T)Count * sizeof(*Processes));
    if (Processes == NULL)
    {
        Sys_FreeInfo(SystemInfo);
        return STATUS_NO_MEMORY;
    }
    Entry = SystemInfo;
    for (Index = 0; Index < Count; Index++)
    {
        Processes[Index].ProcessId = (ULONG)(ULONG_PTR)Entry->UniqueProcessId;
        Processes[Index].ParentProcessId = (ULONG)(ULONG_PTR)Entry->InheritedFromUniqueProcessId;
        Processes[Index].SessionId = Entry->SessionId;
        Processes[Index].ThreadCount = Entry->NumberOfThreads;
        Processes[Index].HandleCount = Entry->HandleCount;
        Processes[Index].CreateTime = Entry->CreateTime.QuadPart;
        Processes[Index].UserTime = Entry->UserTime.QuadPart;
        Processes[Index].KernelTime = Entry->KernelTime.QuadPart;
        Processes[Index].WorkingSetBytes = Entry->WorkingSetSize;
        Processes[Index].PrivateBytes = Entry->PrivatePageCount;
        Processes[Index].ImageName = Entry->ImageName.Buffer;
        Processes[Index].ImageNameLength = Entry->ImageName.Length / sizeof(WCHAR);
        Entry = Entry->NextEntryOffset != 0 ?
                    Add2Ptr(Entry, Entry->NextEntryOffset) :
                    NULL;
    }
    Status = ZpProcess_EncodeList(Processes, Count, NULL, 0, &Length);
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpProcess_EncodeList(Processes,
                                      Count,
                                      Buffer,
                                      Length,
                                      &Length);
    }
    Mem_Free(Processes);
    Sys_FreeInfo(SystemInfo);
    if (!NT_SUCCESS(Status))
    {
        if (Buffer != NULL)
        {
            Mem_Free(Buffer);
        }
        return Status;
    }
    *Payload = Buffer;
    *PayloadLength = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpProcess_QueryString(
    _In_ HANDLE Process,
    _In_ PROCESSINFOCLASS InformationClass,
    _Outptr_ PUNICODE_STRING* String)
{
    PUNICODE_STRING Buffer;
    ULONG Length = sizeof(UNICODE_STRING) + MAX_PATH * sizeof(WCHAR);
    NTSTATUS Status;

    Buffer = Mem_Alloc(Length);
    if (Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = NtQueryInformationProcess(Process,
                                       InformationClass,
                                       Buffer,
                                       Length,
                                       &Length);
    if (Status == STATUS_INFO_LENGTH_MISMATCH)
    {
        Mem_Free(Buffer);
        Buffer = Mem_Alloc(Length);
        if (Buffer == NULL)
        {
            return STATUS_NO_MEMORY;
        }
        Status = NtQueryInformationProcess(Process,
                                           InformationClass,
                                           Buffer,
                                           Length,
                                           NULL);
    }
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    *String = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpProcess_Query(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    PSYSTEM_PROCESS_INFORMATION Entry;
    ZP_PROCESS_INFO Info;
    PUNICODE_STRING ImagePath = NULL, CommandLine = NULL;
    PVOID SystemInfo;
    PBYTE Buffer;
    HANDLE Process;
    NTSTATUS Status;
    ULONG Length;

    Status = Sys_QueryDynamicInfo(SystemProcessInformation, &SystemInfo);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Entry = SystemInfo;
    while ((ULONG)(ULONG_PTR)Entry->UniqueProcessId != ProcessId)
    {
        if (Entry->NextEntryOffset == 0)
        {
            Sys_FreeInfo(SystemInfo);
            return STATUS_NOT_FOUND;
        }
        Entry = Add2Ptr(Entry, Entry->NextEntryOffset);
    }
    if ((ULONGLONG)Entry->CreateTime.QuadPart != CreateTime)
    {
        Sys_FreeInfo(SystemInfo);
        return STATUS_NOT_FOUND;
    }
    Info.ProcessId = ProcessId;
    Info.ParentProcessId = (ULONG)(ULONG_PTR)Entry->InheritedFromUniqueProcessId;
    Info.SessionId = Entry->SessionId;
    Info.ThreadCount = Entry->NumberOfThreads;
    Info.HandleCount = Entry->HandleCount;
    Info.CreateTime = Entry->CreateTime.QuadPart;
    Info.UserTime = Entry->UserTime.QuadPart;
    Info.KernelTime = Entry->KernelTime.QuadPart;
    Info.WorkingSetBytes = Entry->WorkingSetSize;
    Info.PrivateBytes = Entry->PrivatePageCount;
    Info.ImageName = Entry->ImageName.Buffer;
    Info.ImageNameLength = Entry->ImageName.Length / sizeof(WCHAR);
    Info.ImagePathStatus = PS_OpenProcess(&Process,
                                          PROCESS_QUERY_LIMITED_INFORMATION,
                                          ProcessId);
    Info.CommandLineStatus = Info.ImagePathStatus;
    if (NT_SUCCESS(Info.ImagePathStatus))
    {
        Info.ImagePathStatus = ZpProcess_QueryString(Process,
                                                     ProcessImageFileNameWin32,
                                                     &ImagePath);
        Info.CommandLineStatus = ZpProcess_QueryString(Process,
                                                       ProcessCommandLineInformation,
                                                       &CommandLine);
        NtClose(Process);
    }
    Info.ImagePath = NT_SUCCESS(Info.ImagePathStatus) ? ImagePath->Buffer : NULL;
    Info.ImagePathLength = NT_SUCCESS(Info.ImagePathStatus) ? ImagePath->Length / sizeof(WCHAR) : 0;
    Info.CommandLine = NT_SUCCESS(Info.CommandLineStatus) ? CommandLine->Buffer : NULL;
    Info.CommandLineLength = NT_SUCCESS(Info.CommandLineStatus) ? CommandLine->Length / sizeof(WCHAR) : 0;
    Status = ZpProcess_EncodeInfo(&Info, NULL, 0, &Length);
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpProcess_EncodeInfo(&Info, Buffer, Length, &Length);
    }
    Mem_Free(ImagePath);
    Mem_Free(CommandLine);
    Sys_FreeInfo(SystemInfo);
    if (!NT_SUCCESS(Status))
    {
        if (Buffer != NULL)
        {
            Mem_Free(Buffer);
        }
        return Status;
    }
    *Payload = Buffer;
    *PayloadLength = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpProcess_Terminate(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ NTSTATUS ExitStatus)
{
    KERNEL_USER_TIMES Times;
    HANDLE Process;
    NTSTATUS Status;

    Status = PS_OpenProcess(&Process,
                            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                            ProcessId);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = NtQueryInformationProcess(Process,
                                       ProcessTimes,
                                       &Times,
                                       sizeof(Times),
                                       NULL);
    if (NT_SUCCESS(Status) && (ULONGLONG)Times.CreateTime.QuadPart != CreateTime)
    {
        Status = STATUS_NOT_FOUND;
    }
    if (NT_SUCCESS(Status))
    {
        Status = NtTerminateProcess(Process, ExitStatus);
    }
    NtClose(Process);
    return Status;
}

NTSTATUS
ZpProcess_Execute(
    _In_ USHORT OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ULONGLONG CreateTime;
    ULONG ProcessId, ExitCode;
    NTSTATUS Status;

    switch (OperationId)
    {
        case ZP_PROCESS_OPERATION_ENUMERATE:
            return RequestLength == 0 ?
                       ZpProcess_Enumerate(Response, ResponseLength) :
                       STATUS_INVALID_PARAMETER;

        case ZP_PROCESS_OPERATION_QUERY:
            Status = ZpProcess_DecodeQuery(Request, RequestLength, &ProcessId, &CreateTime);
            return NT_SUCCESS(Status) ?
                       ZpProcess_Query(ProcessId, CreateTime, Response, ResponseLength) :
                       Status;

        case ZP_PROCESS_OPERATION_TERMINATE:
            Status = ZpProcess_DecodeTerminate(Request,
                                               RequestLength,
                                               &ProcessId,
                                               &CreateTime,
                                               &ExitCode);
            if (NT_SUCCESS(Status))
            {
                Status = ZpProcess_Terminate(ProcessId,
                                             CreateTime,
                                             (NTSTATUS)ExitCode);
            }
            if (NT_SUCCESS(Status))
            {
                *Response = NULL;
                *ResponseLength = 0;
            }
            return Status;

        default:
            return STATUS_NOT_SUPPORTED;
    }
}
