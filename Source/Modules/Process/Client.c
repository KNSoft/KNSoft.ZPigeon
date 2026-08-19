#include "Client.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Core/Account.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#include <Winsvc.h>

typedef struct _ZP_PROCESS_ALLOCATION
{
    PUNICODE_STRING UserName;
    PUNICODE_STRING ImagePath;
    PWSTR ServiceNames;
    NTSTATUS ImagePathStatus;
} ZP_PROCESS_ALLOCATION, *PZP_PROCESS_ALLOCATION;

static
LOGICAL
ZpProcess_IsSuspended(
    _In_ PSYSTEM_PROCESS_INFORMATION Entry)
{
    ULONG Index;

    if (Entry->NumberOfThreads == 0) return FALSE;
    for (Index = 0; Index < Entry->NumberOfThreads; Index++)
    {
        if (Entry->Threads[Index].ThreadState != Waiting ||
            (Entry->Threads[Index].WaitReason != Suspended && Entry->Threads[Index].WaitReason != WrSuspended))
        {
            return FALSE;
        }
    }
    return TRUE;
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
    if (Buffer == NULL) return STATUS_NO_MEMORY;
    Status = NtQueryInformationProcess(Process, InformationClass, Buffer, Length, &Length);
    if (Status == STATUS_INFO_LENGTH_MISMATCH)
    {
        Mem_Free(Buffer);
        Buffer = Mem_Alloc(Length);
        if (Buffer == NULL) return STATUS_NO_MEMORY;
        Status = NtQueryInformationProcess(Process, InformationClass, Buffer, Length, NULL);
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
VOID
ZpProcess_QueryTokenMetadata(
    _In_ HANDLE Process,
    _Inout_ PZP_PROCESS_RECORD Record,
    _Inout_ PZP_PROCESS_ALLOCATION Allocation)
{
    HANDLE Token;
    ULONG Value;

    if (!NT_SUCCESS(NtOpenProcessToken(Process, TOKEN_QUERY, &Token))) return;
    if (NT_SUCCESS(ZpAccount_QueryTokenName(Token, &Allocation->UserName)))
    {
        Record->UserName = Allocation->UserName->Buffer;
        Record->UserNameLength = Allocation->UserName->Length / sizeof(WCHAR);
    }
    if (NT_SUCCESS(NtQueryInformationToken(Token, TokenVirtualizationAllowed, &Value, sizeof(Value), NULL)) && Value)
    {
        Record->Flags |= ZP_PROCESS_FLAG_VIRTUALIZATION_ALLOWED;
    }
    if (NT_SUCCESS(NtQueryInformationToken(Token, TokenVirtualizationEnabled, &Value, sizeof(Value), NULL)) && Value)
    {
        Record->Flags |= ZP_PROCESS_FLAG_VIRTUALIZATION_ENABLED;
    }
    NtClose(Token);
}

static
VOID
ZpProcess_FillRecord(
    _In_ PSYSTEM_PROCESS_INFORMATION Entry,
    _Out_ PZP_PROCESS_RECORD Record,
    _Out_ PZP_PROCESS_ALLOCATION Allocation)
{
    POWER_THROTTLING_PROCESS_STATE PowerState;
    PROCESS_PRIORITY_CLASS Priority;
    HANDLE Process;

    RtlZeroMemory(Record, sizeof(*Record));
    RtlZeroMemory(Allocation, sizeof(*Allocation));
    Record->ProcessId = (ULONG)(ULONG_PTR)Entry->UniqueProcessId;
    Record->ParentProcessId = (ULONG)(ULONG_PTR)Entry->InheritedFromUniqueProcessId;
    Record->SessionId = Entry->SessionId;
    Record->ThreadCount = Entry->NumberOfThreads;
    Record->HandleCount = Entry->HandleCount;
    Record->CreateTime = Entry->CreateTime.QuadPart;
    Record->UserTime = Entry->UserTime.QuadPart;
    Record->KernelTime = Entry->KernelTime.QuadPart;
    Record->WorkingSetBytes = Entry->WorkingSetSize;
    Record->PrivateBytes = Entry->PrivatePageCount;
    Record->ImageName = Entry->ImageName.Buffer;
    Record->ImageNameLength = Entry->ImageName.Length / sizeof(WCHAR);
    if (ZpProcess_IsSuspended(Entry)) Record->Flags |= ZP_PROCESS_FLAG_SUSPENDED;
    Allocation->ImagePathStatus = PS_OpenProcess(&Process, PROCESS_QUERY_LIMITED_INFORMATION, Record->ProcessId);
    if (!NT_SUCCESS(Allocation->ImagePathStatus)) return;

    Allocation->ImagePathStatus = ZpProcess_QueryString(Process, ProcessImageFileNameWin32, &Allocation->ImagePath);
    if (NT_SUCCESS(Allocation->ImagePathStatus))
    {
        Record->ImagePath = Allocation->ImagePath->Buffer;
        Record->ImagePathLength = Allocation->ImagePath->Length / sizeof(WCHAR);
    }
    PS_RemoteGetMachineType(Process, &Record->MachineType);
    if (NT_SUCCESS(NtQueryInformationProcess(Process, ProcessPriorityClass, &Priority, sizeof(Priority), NULL)))
    {
        Record->PriorityClass = Priority.PriorityClass;
    }
    if (NT_SUCCESS(NtQueryInformationProcess(Process,
                                              ProcessPowerThrottlingState,
                                              &PowerState,
                                              sizeof(PowerState),
                                              NULL)) &&
        (PowerState.ControlMask & POWER_THROTTLING_PROCESS_EXECUTION_SPEED) != 0 &&
        (PowerState.StateMask & POWER_THROTTLING_PROCESS_EXECUTION_SPEED) != 0)
    {
        Record->Flags |= ZP_PROCESS_FLAG_EFFICIENCY_MODE;
    }
    ZpProcess_QueryTokenMetadata(Process, Record, Allocation);
    NtClose(Process);
}

static
VOID
ZpProcess_FreeAllocation(
    _Inout_ PZP_PROCESS_ALLOCATION Allocation)
{
    NT_FreeStringW(Allocation->UserName);
    Mem_Free(Allocation->ImagePath);
    Mem_Free(Allocation->ServiceNames);
}

static
NTSTATUS
ZpProcess_QueryServices(
    _Inout_updates_(ProcessCount) PZP_PROCESS_RECORD Processes,
    _Inout_updates_(ProcessCount) PZP_PROCESS_ALLOCATION Allocations,
    _In_ ULONG ProcessCount)
{
    LPENUM_SERVICE_STATUS_PROCESSW Services;
    SC_HANDLE Manager;
    DWORD Error, Length = 0, ServiceCount = 0, Resume = 0;
    ULONG Index, ServiceIndex;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (Manager == NULL) return NTSTATUS_FROM_WIN32(GetLastError());
    if (EnumServicesStatusExW(Manager,
                              SC_ENUM_PROCESS_INFO,
                              SERVICE_WIN32,
                              SERVICE_STATE_ALL,
                              NULL,
                              0,
                              &Length,
                              &ServiceCount,
                              &Resume,
                              NULL))
    {
        CloseServiceHandle(Manager);
        return STATUS_SUCCESS;
    }
    Error = GetLastError();
    if (Error != ERROR_MORE_DATA)
    {
        CloseServiceHandle(Manager);
        return NTSTATUS_FROM_WIN32(Error);
    }
    Services = Mem_Alloc(Length);
    if (Services == NULL)
    {
        CloseServiceHandle(Manager);
        return STATUS_NO_MEMORY;
    }
    Resume = 0;
    if (!EnumServicesStatusExW(Manager,
                               SC_ENUM_PROCESS_INFO,
                               SERVICE_WIN32,
                               SERVICE_STATE_ALL,
                               (PBYTE)Services,
                               Length,
                               &Length,
                               &ServiceCount,
                               &Resume,
                               NULL))
    {
        Error = GetLastError();
        Mem_Free(Services);
        CloseServiceHandle(Manager);
        return NTSTATUS_FROM_WIN32(Error);
    }
    for (Index = 0; Index < ProcessCount; Index++)
    {
        SIZE_T NameLength = 0;
        PWSTR Cursor;

        if (Processes[Index].ProcessId == 0) continue;
        for (ServiceIndex = 0; ServiceIndex < ServiceCount; ServiceIndex++)
        {
            if (Services[ServiceIndex].ServiceStatusProcess.dwProcessId == Processes[Index].ProcessId)
            {
                NameLength += wcslen(Services[ServiceIndex].lpServiceName) + (NameLength != 0);
            }
        }
        if (NameLength == 0) continue;
        Allocations[Index].ServiceNames = Mem_Alloc((NameLength + 1) * sizeof(WCHAR));
        if (Allocations[Index].ServiceNames == NULL)
        {
            Mem_Free(Services);
            CloseServiceHandle(Manager);
            return STATUS_NO_MEMORY;
        }
        Cursor = Allocations[Index].ServiceNames;
        for (ServiceIndex = 0; ServiceIndex < ServiceCount; ServiceIndex++)
        {
            PCWSTR Name = Services[ServiceIndex].lpServiceName;
            SIZE_T Length;

            if (Services[ServiceIndex].ServiceStatusProcess.dwProcessId != Processes[Index].ProcessId) continue;
            if (Cursor != Allocations[Index].ServiceNames) *Cursor++ = UNICODE_NULL;
            Length = wcslen(Name);
            RtlCopyMemory(Cursor, Name, Length * sizeof(WCHAR));
            Cursor += Length;
        }
        *Cursor = UNICODE_NULL;
        Processes[Index].ServiceNames = Allocations[Index].ServiceNames;
        Processes[Index].ServiceNamesLength = (ULONG)NameLength;
    }
    Mem_Free(Services);
    CloseServiceHandle(Manager);
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpProcess_Enumerate(
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    PSYSTEM_PROCESS_INFORMATION Entry;
    PZP_PROCESS_RECORD Processes;
    PZP_PROCESS_ALLOCATION Allocations;
    PVOID SystemInfo;
    PBYTE Buffer;
    NTSTATUS Status;
    ULONG Count = 0, Index, Length;

    Status = Sys_QueryDynamicInfo(SystemProcessInformation, &SystemInfo);
    if (!NT_SUCCESS(Status)) return Status;
    Entry = SystemInfo;
    do
    {
        Count++;
        Entry = Entry->NextEntryOffset != 0 ? Add2Ptr(Entry, Entry->NextEntryOffset) : NULL;
    } while (Entry != NULL);
    Processes = Mem_Alloc((SIZE_T)Count * sizeof(*Processes));
    Allocations = Mem_Alloc((SIZE_T)Count * sizeof(*Allocations));
    if (Processes == NULL || Allocations == NULL)
    {
        Mem_Free(Processes);
        Mem_Free(Allocations);
        Sys_FreeInfo(SystemInfo);
        return STATUS_NO_MEMORY;
    }
    Entry = SystemInfo;
    for (Index = 0; Index < Count; Index++)
    {
        ZpProcess_FillRecord(Entry, &Processes[Index], &Allocations[Index]);
        Entry = Entry->NextEntryOffset != 0 ? Add2Ptr(Entry, Entry->NextEntryOffset) : NULL;
    }
    Status = ZpProcess_QueryServices(Processes, Allocations, Count);
    if (NT_SUCCESS(Status)) Status = ZpProcess_EncodeList(Processes, Count, NULL, 0, &Length);
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status)) Status = ZpProcess_EncodeList(Processes, Count, Buffer, Length, &Length);
    for (Index = 0; Index < Count; Index++) ZpProcess_FreeAllocation(&Allocations[Index]);
    Mem_Free(Allocations);
    Mem_Free(Processes);
    Sys_FreeInfo(SystemInfo);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    *Payload = Buffer;
    *PayloadLength = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpProcess_Find(
    _In_ PVOID SystemInfo,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _Out_ PSYSTEM_PROCESS_INFORMATION* Process)
{
    PSYSTEM_PROCESS_INFORMATION Entry = SystemInfo;

    for (;;)
    {
        if ((ULONG)(ULONG_PTR)Entry->UniqueProcessId == ProcessId &&
            (ULONGLONG)Entry->CreateTime.QuadPart == CreateTime)
        {
            *Process = Entry;
            return STATUS_SUCCESS;
        }
        if (Entry->NextEntryOffset == 0) return STATUS_NOT_FOUND;
        Entry = Add2Ptr(Entry, Entry->NextEntryOffset);
    }
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
    ZP_PROCESS_ALLOCATION Allocation;
    ZP_PROCESS_RECORD Record;
    ZP_PROCESS_INFO Info;
    PUNICODE_STRING CommandLine = NULL;
    PVOID SystemInfo;
    PBYTE Buffer;
    HANDLE Process;
    PROCESS_BASIC_INFORMATION BasicInformation;
    PVOID ImageBase;
    NTSTATUS Status;
    ULONG Length;

    Status = Sys_QueryDynamicInfo(SystemProcessInformation, &SystemInfo);
    if (!NT_SUCCESS(Status)) return Status;
    Status = ZpProcess_Find(SystemInfo, ProcessId, CreateTime, &Entry);
    if (!NT_SUCCESS(Status))
    {
        Sys_FreeInfo(SystemInfo);
        return Status;
    }
    ZpProcess_FillRecord(Entry, &Record, &Allocation);
    Info.ProcessId = Record.ProcessId;
    Info.ParentProcessId = Record.ParentProcessId;
    Info.SessionId = Record.SessionId;
    Info.ThreadCount = Record.ThreadCount;
    Info.HandleCount = Record.HandleCount;
    Info.Flags = Record.Flags;
    Info.MachineType = Record.MachineType;
    Info.PriorityClass = Record.PriorityClass;
    Info.CreateTime = Record.CreateTime;
    Info.UserTime = Record.UserTime;
    Info.KernelTime = Record.KernelTime;
    Info.WorkingSetBytes = Record.WorkingSetBytes;
    Info.PrivateBytes = Record.PrivateBytes;
    Info.ImageBaseStatus = PS_OpenProcess(&Process,
                                          PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                          ProcessId);
    if (NT_SUCCESS(Info.ImageBaseStatus))
    {
        Info.ImageBaseStatus = NtQueryInformationProcess(Process,
                                                         ProcessBasicInformation,
                                                         &BasicInformation,
                                                         sizeof(BasicInformation),
                                                         NULL);
        if (NT_SUCCESS(Info.ImageBaseStatus))
        {
            Info.ImageBaseStatus = NtReadVirtualMemory(Process,
                                                       Add2Ptr(BasicInformation.PebBaseAddress,
                                                               FIELD_OFFSET(PEB, ImageBaseAddress)),
                                                       &ImageBase,
                                                       sizeof(ImageBase),
                                                       NULL);
            if (NT_SUCCESS(Info.ImageBaseStatus)) Info.ImageBase = (ULONGLONG)ImageBase;
        }
        NtClose(Process);
    }
    if (!NT_SUCCESS(Info.ImageBaseStatus)) Info.ImageBase = 0;
    Info.ImageName = Record.ImageName;
    Info.ImageNameLength = Record.ImageNameLength;
    Info.UserName = Record.UserName;
    Info.UserNameLength = Record.UserNameLength;
    Info.ImagePathStatus = Allocation.ImagePathStatus;
    Info.ImagePath = Record.ImagePath;
    Info.ImagePathLength = Record.ImagePathLength;
    Info.CommandLineStatus = PS_OpenProcess(&Process, PROCESS_QUERY_LIMITED_INFORMATION, ProcessId);
    if (NT_SUCCESS(Info.CommandLineStatus))
    {
        Info.CommandLineStatus = ZpProcess_QueryString(Process, ProcessCommandLineInformation, &CommandLine);
        NtClose(Process);
    }
    Info.CommandLine = NT_SUCCESS(Info.CommandLineStatus) ? CommandLine->Buffer : NULL;
    Info.CommandLineLength = NT_SUCCESS(Info.CommandLineStatus) ? CommandLine->Length / sizeof(WCHAR) : 0;
    Status = ZpProcess_EncodeInfo(&Info, NULL, 0, &Length);
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status)) Status = ZpProcess_EncodeInfo(&Info, Buffer, Length, &Length);
    Mem_Free(CommandLine);
    ZpProcess_FreeAllocation(&Allocation);
    Sys_FreeInfo(SystemInfo);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    *Payload = Buffer;
    *PayloadLength = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpProcess_OpenVerified(
    _Out_ PHANDLE Process,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime)
{
    KERNEL_USER_TIMES Times;
    NTSTATUS Status;

    Status = PS_OpenProcess(Process, DesiredAccess | PROCESS_QUERY_LIMITED_INFORMATION, ProcessId);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryInformationProcess(*Process, ProcessTimes, &Times, sizeof(Times), NULL);
    if (NT_SUCCESS(Status) && (ULONGLONG)Times.CreateTime.QuadPart != CreateTime) Status = STATUS_NOT_FOUND;
    if (!NT_SUCCESS(Status))
    {
        NtClose(*Process);
        return Status;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpProcess_ValidateMemoryRange(
    _In_ HANDLE Process,
    _In_ ULONGLONG Address,
    _In_ ULONG Length)
{
    MEMORY_BASIC_INFORMATION Information;
    SIZE_T ResultLength;
    ULONG_PTR Start = (ULONG_PTR)Address;
    ULONG_PTR Base, End;
    NTSTATUS Status;

    if (Address != Start || MAXULONG_PTR - Start < Length) return STATUS_INVALID_ADDRESS;
    Status = NtQueryVirtualMemory(Process,
                                  (PVOID)Start,
                                  MemoryBasicInformation,
                                  &Information,
                                  sizeof(Information),
                                  &ResultLength);
    if (!NT_SUCCESS(Status)) return Status;
    Base = (ULONG_PTR)Information.BaseAddress;
    End = Base + Information.RegionSize;
    return Information.State == MEM_COMMIT && End >= Base && Start >= Base && Start + Length <= End ?
               STATUS_SUCCESS : STATUS_CONFLICTING_ADDRESSES;
}

static
NTSTATUS
ZpProcess_ReadMemory(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONGLONG Address,
    _In_ ULONG Length,
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    SIZE_T BytesRead;
    PBYTE Data;
    HANDLE Process;
    NTSTATUS Status;
    ULONG EncodedLength;

    Status = ZpProcess_OpenVerified(&Process, PROCESS_VM_READ, ProcessId, CreateTime);
    if (!NT_SUCCESS(Status)) return Status;
    Status = ZpProcess_ValidateMemoryRange(Process, Address, Length);
    Data = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Data == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = NtReadVirtualMemory(Process, (PVOID)(ULONG_PTR)Address, Data, Length, &BytesRead);
        if (NT_SUCCESS(Status) && BytesRead != Length) Status = STATUS_PARTIAL_COPY;
    }
    NtClose(Process);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Data);
        return Status;
    }
    Status = ZpProcess_EncodeMemoryData(Data, Length, NULL, 0, &EncodedLength);
    *Payload = NT_SUCCESS(Status) ? Mem_Alloc(EncodedLength) : NULL;
    if (NT_SUCCESS(Status) && *Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpProcess_EncodeMemoryData(Data, Length, *Payload, EncodedLength, PayloadLength);
    }
    Mem_Free(Data);
    if (!NT_SUCCESS(Status)) Mem_Free(*Payload);
    return Status;
}

static
NTSTATUS
ZpProcess_WriteMemory(
    _In_ PCZP_PROCESS_MEMORY_VIEW Memory)
{
    SIZE_T BytesWritten;
    HANDLE Process;
    NTSTATUS Status;

    Status = ZpProcess_OpenVerified(&Process,
                                    PROCESS_VM_OPERATION | PROCESS_VM_WRITE,
                                    Memory->ProcessId,
                                    Memory->CreateTime);
    if (!NT_SUCCESS(Status)) return Status;
    Status = ZpProcess_ValidateMemoryRange(Process, Memory->Address, Memory->Data.Length);
    if (NT_SUCCESS(Status))
    {
        Status = NtWriteVirtualMemory(Process,
                                      (PVOID)(ULONG_PTR)Memory->Address,
                                      (PVOID)Memory->Data.Buffer,
                                      Memory->Data.Length,
                                      &BytesWritten);
        if (NT_SUCCESS(Status) && BytesWritten != Memory->Data.Length) Status = STATUS_PARTIAL_COPY;
    }
    NtClose(Process);
    return Status;
}

static
NTSTATUS
ZpProcess_TerminateTree(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime)
{
    PSYSTEM_PROCESS_INFORMATION* Entries;
    PSYSTEM_PROCESS_INFORMATION Entry;
    PVOID SystemInfo;
    PBOOLEAN Targets;
    HANDLE Process;
    NTSTATUS Status, Result = STATUS_SUCCESS;
    ULONG Count = 0, Index, ParentIndex;
    LOGICAL Changed, Found = FALSE;

    Status = Sys_QueryDynamicInfo(SystemProcessInformation, &SystemInfo);
    if (!NT_SUCCESS(Status)) return Status;
    Entry = SystemInfo;
    do
    {
        Count++;
        Entry = Entry->NextEntryOffset != 0 ? Add2Ptr(Entry, Entry->NextEntryOffset) : NULL;
    } while (Entry != NULL);
    Entries = Mem_Alloc((SIZE_T)Count * sizeof(*Entries));
    Targets = Mem_Alloc((SIZE_T)Count * sizeof(*Targets));
    if (Entries == NULL || Targets == NULL)
    {
        Mem_Free(Entries);
        Mem_Free(Targets);
        Sys_FreeInfo(SystemInfo);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Targets, (SIZE_T)Count * sizeof(*Targets));
    Entry = SystemInfo;
    for (Index = 0; Index < Count; Index++)
    {
        Entries[Index] = Entry;
        if ((ULONG)(ULONG_PTR)Entry->UniqueProcessId == ProcessId &&
            (ULONGLONG)Entry->CreateTime.QuadPart == CreateTime)
        {
            Targets[Index] = TRUE;
            Found = TRUE;
        }
        Entry = Entry->NextEntryOffset != 0 ? Add2Ptr(Entry, Entry->NextEntryOffset) : NULL;
    }
    do
    {
        Changed = FALSE;
        for (Index = 0; Index < Count; Index++)
        {
            if (Targets[Index]) continue;
            for (ParentIndex = 0; ParentIndex < Count; ParentIndex++)
            {
                if (Targets[ParentIndex] &&
                    Entries[Index]->InheritedFromUniqueProcessId == Entries[ParentIndex]->UniqueProcessId &&
                    Entries[Index]->CreateTime.QuadPart >= Entries[ParentIndex]->CreateTime.QuadPart)
                {
                    Targets[Index] = TRUE;
                    Changed = TRUE;
                    break;
                }
            }
        }
    } while (Changed);
    for (Index = Count; Index-- != 0;)
    {
        if (!Targets[Index]) continue;
        Status = ZpProcess_OpenVerified(&Process,
                                        PROCESS_TERMINATE,
                                        (ULONG)(ULONG_PTR)Entries[Index]->UniqueProcessId,
                                        Entries[Index]->CreateTime.QuadPart);
        if (NT_SUCCESS(Status))
        {
            Status = NtTerminateProcess(Process, STATUS_CANCELLED);
            NtClose(Process);
        }
        if (!NT_SUCCESS(Status) && Status != STATUS_NOT_FOUND && NT_SUCCESS(Result)) Result = Status;
    }
    if (!Found) Result = STATUS_NOT_FOUND;
    Mem_Free(Targets);
    Mem_Free(Entries);
    Sys_FreeInfo(SystemInfo);
    return Result;
}

static
NTSTATUS
ZpProcess_Control(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ZP_PROCESS_CONTROL Control,
    _In_ ULONG Value)
{
    POWER_THROTTLING_PROCESS_STATE PowerState;
    PROCESS_PRIORITY_CLASS Priority;
    HANDLE Process, Token;
    NTSTATUS Status;
    ACCESS_MASK Access;

    if (Control == ZpProcessControlTerminateTree) return ZpProcess_TerminateTree(ProcessId, CreateTime);
    switch (Control)
    {
        case ZpProcessControlTerminate: Access = PROCESS_TERMINATE; break;
        case ZpProcessControlSuspend:
        case ZpProcessControlResume: Access = PROCESS_SUSPEND_RESUME; break;
        case ZpProcessControlEfficiencyMode:
        case ZpProcessControlPriority: Access = PROCESS_SET_INFORMATION; break;
        case ZpProcessControlUacVirtualization: Access = 0; break;
        default: return STATUS_INVALID_PARAMETER;
    }
    Status = ZpProcess_OpenVerified(&Process, Access, ProcessId, CreateTime);
    if (!NT_SUCCESS(Status)) return Status;
    switch (Control)
    {
        case ZpProcessControlTerminate:
            Status = NtTerminateProcess(Process, STATUS_CANCELLED);
            break;
        case ZpProcessControlSuspend:
            Status = NtSuspendProcess(Process);
            break;
        case ZpProcessControlResume:
            Status = NtResumeProcess(Process);
            break;
        case ZpProcessControlEfficiencyMode:
            if (Value > 1)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            PowerState.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            PowerState.ControlMask = POWER_THROTTLING_PROCESS_EXECUTION_SPEED;
            PowerState.StateMask = Value ? POWER_THROTTLING_PROCESS_EXECUTION_SPEED : 0;
            Status = NtSetInformationProcess(Process,
                                             ProcessPowerThrottlingState,
                                             &PowerState,
                                             sizeof(PowerState));
            break;
        case ZpProcessControlPriority:
            if (Value < PROCESS_PRIORITY_CLASS_IDLE || Value > PROCESS_PRIORITY_CLASS_ABOVE_NORMAL)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Priority.Foreground = FALSE;
            Priority.PriorityClass = (UCHAR)Value;
            Status = NtSetInformationProcess(Process, ProcessPriorityClass, &Priority, sizeof(Priority));
            break;
        case ZpProcessControlUacVirtualization:
            if (Value > 1)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Status = NtOpenProcessToken(Process, TOKEN_ADJUST_DEFAULT | TOKEN_QUERY, &Token);
            if (NT_SUCCESS(Status))
            {
                Status = NtSetInformationToken(Token, TokenVirtualizationEnabled, &Value, sizeof(Value));
                NtClose(Token);
            }
            break;
    }
    NtClose(Process);
    return Status;
}

static
ZP_STATUS
ZpProcess_CreateDump(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONG DumpType,
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    LARGE_INTEGER SystemTime;
    PWCHAR Path;
    HANDLE Process, File;
    ULONG TempLength, Length, PathCapacity, PathLength;
    NTSTATUS Status;
    ZP_STATUS Result;
    HRESULT HResult;
    PBYTE Buffer;

    if ((DumpType & ~MiniDumpValidTypeFlagsEx) != 0)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Status = ZpProcess_OpenVerified(&Process,
                                    PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE,
                                    ProcessId,
                                    CreateTime);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    TempLength = GetTempPath2W(0, NULL);
    if (TempLength == 0)
    {
        Result = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto ExitProcess;
    }
    PathCapacity = TempLength + 64;
    Path = Mem_Alloc((SIZE_T)PathCapacity * sizeof(WCHAR));
    if (Path == NULL)
    {
        Result = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto ExitProcess;
    }
    PathLength = GetTempPath2W(PathCapacity, Path);
    if (PathLength == 0)
    {
        Result = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto ExitPath;
    }
    if (PathLength >= PathCapacity)
    {
        Result = ZpStatus_FromNtStatus(STATUS_BUFFER_TOO_SMALL);
        goto ExitPath;
    }
    NtQuerySystemTime(&SystemTime);
    HResult = Str_PrintfExW(Path + PathLength,
                            PathCapacity - PathLength,
                            L"ZPigeon-%lu-%I64u.dmp",
                            ProcessId,
                            SystemTime.QuadPart);
    if (FAILED(HResult))
    {
        Result = ZpStatus_FromCode(ZpStatusHResult, HResult);
        goto ExitPath;
    }
    Status = IO_CreateWin32File(&File,
                                Path,
                                NULL,
                                FILE_GENERIC_READ | FILE_GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                FILE_CREATE,
                                FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    Result = ZpStatus_FromNtStatus(Status);
    if (!NT_SUCCESS(Status)) goto ExitPath;
    if (!MiniDumpWriteDump(Process, ProcessId, File, (MINIDUMP_TYPE)DumpType, NULL, NULL, NULL))
    {
        Result = ZpStatus_FromCode(ZpStatusHResult, GetLastError());
    }
    NtClose(File);
    if (!ZpStatus_IsSuccess(Result))
    {
        IO_DeleteWin32File(Path, NULL);
        goto ExitPath;
    }
    PathLength = (ULONG)wcslen(Path);
    Status = ZpProcess_EncodeDumpPath(Path, PathLength, NULL, 0, &Length);
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status)) Status = ZpProcess_EncodeDumpPath(Path, PathLength, Buffer, Length, &Length);
    Result = ZpStatus_FromNtStatus(Status);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        IO_DeleteWin32File(Path, NULL);
        goto ExitPath;
    }
    *Payload = Buffer;
    *PayloadLength = Length;
ExitPath:
    Mem_Free(Path);
ExitProcess:
    NtClose(Process);
    return Result;
}

ZP_STATUS
ZpProcess_Execute(
    _In_ USHORT OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_PROCESS_CONTROL Control;
    ZP_PROCESS_MEMORY_VIEW Memory;
    ULONGLONG Address;
    ULONGLONG CreateTime;
    ULONG ProcessId, Value;
    NTSTATUS Status;

    switch (OperationId)
    {
        case ZP_PROCESS_OPERATION_ENUMERATE:
            Status = RequestLength == 0 ?
                         ZpProcess_Enumerate(Response, ResponseLength) :
                         STATUS_INVALID_PARAMETER;
            return ZpStatus_FromNtStatus(Status);
        case ZP_PROCESS_OPERATION_QUERY:
            Status = ZpProcess_DecodeQuery(Request, RequestLength, &ProcessId, &CreateTime);
            if (NT_SUCCESS(Status)) Status = ZpProcess_Query(ProcessId, CreateTime, Response, ResponseLength);
            return ZpStatus_FromNtStatus(Status);
        case ZP_PROCESS_OPERATION_CONTROL:
            Status = ZpProcess_DecodeControl(Request, RequestLength, &ProcessId, &CreateTime, &Control, &Value);
            if (NT_SUCCESS(Status)) Status = ZpProcess_Control(ProcessId, CreateTime, Control, Value);
            if (NT_SUCCESS(Status))
            {
                *Response = NULL;
                *ResponseLength = 0;
            }
            return ZpStatus_FromNtStatus(Status);
        case ZP_PROCESS_OPERATION_DUMP:
            Status = ZpProcess_DecodeDump(Request, RequestLength, &ProcessId, &CreateTime, &Value);
            return NT_SUCCESS(Status) ?
                       ZpProcess_CreateDump(ProcessId, CreateTime, Value, Response, ResponseLength) :
                       ZpStatus_FromNtStatus(Status);
        case ZP_PROCESS_OPERATION_READ_MEMORY:
            Status = ZpProcess_DecodeMemoryRead(Request,
                                                RequestLength,
                                                &ProcessId,
                                                &CreateTime,
                                                &Address,
                                                &Value);
            if (NT_SUCCESS(Status))
            {
                Status = ZpProcess_ReadMemory(ProcessId,
                                              CreateTime,
                                              Address,
                                              Value,
                                              Response,
                                              ResponseLength);
            }
            return ZpStatus_FromNtStatus(Status);
        case ZP_PROCESS_OPERATION_WRITE_MEMORY:
            Status = ZpProcess_DecodeMemoryWrite(Request, RequestLength, &Memory);
            if (NT_SUCCESS(Status)) Status = ZpProcess_WriteMemory(&Memory);
            if (NT_SUCCESS(Status))
            {
                *Response = NULL;
                *ResponseLength = 0;
            }
            return ZpStatus_FromNtStatus(Status);
        default:
            return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
}
