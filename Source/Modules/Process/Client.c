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
        Processes[Index].SessionId = Entry->SessionId;
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
ZpProcess_Query(
    _In_ ULONG ProcessId,
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    PSYSTEM_PROCESS_INFORMATION Entry;
    ZP_PROCESS_INFO Info;
    PVOID SystemInfo;
    PBYTE Buffer;
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

NTSTATUS
ZpProcess_Execute(
    _In_ USHORT OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ULONG ProcessId, ExitCode;
    NTSTATUS Status;

    switch (OperationId)
    {
        case ZP_PROCESS_OPERATION_ENUMERATE:
            return RequestLength == 0 ?
                       ZpProcess_Enumerate(Response, ResponseLength) :
                       STATUS_INVALID_PARAMETER;

        case ZP_PROCESS_OPERATION_QUERY:
            Status = ZpProcess_DecodeQuery(Request, RequestLength, &ProcessId);
            return NT_SUCCESS(Status) ?
                       ZpProcess_Query(ProcessId, Response, ResponseLength) :
                       Status;

        case ZP_PROCESS_OPERATION_TERMINATE:
            Status = ZpProcess_DecodeTerminate(Request,
                                               RequestLength,
                                               &ProcessId,
                                               &ExitCode);
            if (NT_SUCCESS(Status))
            {
                Status = PS_TerminateProcessById(ProcessId, (NTSTATUS)ExitCode);
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
