#include "../Server.inl"
#include "../../Network/Authentication.inl"
#include "../../Network/Quic.inl"

#include <KNSoft/ZPigeon/Process.h>
#include <KNSoft/ZPigeon/Service.h>
#include <KNSoft/ZPigeon/System.h>

#include <Winsvc.h>

#pragma comment(lib, "Advapi32.lib")

typedef struct _ZP_SERVER_QUIC_CONNECTION
{
    PZP_SERVER_QUIC_TRANSPORT Transport;
    RTL_SRWLOCK RequestLock;
    LIST_ENTRY Requests;
    volatile LONG ReferenceCount;
    LOGICAL Closing;
    ULONG ActiveRequestCount;
    HQUIC Connection;
    HQUIC Stream;
    NTSTATUS ShutdownStatus;
    BYTE PublicKey[ZP_CLIENT_PUBLIC_KEY_SIZE];
    BYTE ClientId[ZP_CLIENT_ID_SIZE];
    BYTE Challenge[ZP_SERVER_CHALLENGE_SIZE];
    ZP_MODULE_RECORD Modules[ZP_MODULE_MAX_COUNT];
    USHORT ModuleCount;
    ZP_CONNECTION ProtocolConnection;
    LOGICAL ProtocolConnectionInitialized;
} ZP_SERVER_QUIC_CONNECTION, *PZP_SERVER_QUIC_CONNECTION;

typedef struct _ZP_SERVER_QUIC_REQUEST
{
    LIST_ENTRY ListEntry;
    PZP_SERVER_QUIC_CONNECTION Connection;
    volatile LONG Pending;
    ULONGLONG RequestId;
    USHORT ModuleId;
    USHORT OperationId;
    ULONG TimeoutMilliseconds;
    ULONGLONG ReceivedTickCount;
    ULONG PayloadLength;
    BYTE Payload[ANYSIZE_ARRAY];
} ZP_SERVER_QUIC_REQUEST, *PZP_SERVER_QUIC_REQUEST;

static
VOID
ZpServerQuic_TryCompleteStop(
    _Inout_ PZP_SERVER_QUIC_TRANSPORT Transport);

static
VOID
ZpServerQuic_ReleaseConnection(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection);

static const QUIC_REGISTRATION_CONFIG ZpServerQuicRegistrationConfig = {
    "KNSoft.ZPigeon.Server",
    QUIC_EXECUTION_PROFILE_LOW_LATENCY
};

static
NTSTATUS
ZpServerQuic_GetSystemInfo(
    _Out_ PZP_SYSTEM_INFO Info,
    _Out_writes_(ComputerNameCount) PWCHAR ComputerName,
    _In_ ULONG ComputerNameCount)
{
    RTL_OSVERSIONINFOW Version = { sizeof(Version) };
    MEMORYSTATUSEX Memory = { sizeof(Memory) };
    SYSTEM_INFO NativeInfo;
    DWORD ComputerNameLength = ComputerNameCount;
    NTSTATUS Status;

    GetNativeSystemInfo(&NativeInfo);
    switch (NativeInfo.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_INTEL:
            Info->Architecture = ZpSystemArchitectureX86;
            break;

        case PROCESSOR_ARCHITECTURE_AMD64:
            Info->Architecture = ZpSystemArchitectureX64;
            break;

        case PROCESSOR_ARCHITECTURE_ARM64:
            Info->Architecture = ZpSystemArchitectureArm64;
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }
    Status = RtlGetVersion(&Version);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (!GlobalMemoryStatusEx(&Memory) ||
        !GetComputerNameW(ComputerName, &ComputerNameLength))
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    Info->MajorVersion = Version.dwMajorVersion;
    Info->MinorVersion = Version.dwMinorVersion;
    Info->BuildNumber = Version.dwBuildNumber;
    Info->ProcessorCount = NativeInfo.dwNumberOfProcessors;
    Info->PhysicalMemoryBytes = Memory.ullTotalPhys;
    Info->ComputerName = ComputerName;
    Info->ComputerNameLength = ComputerNameLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpServerQuic_QuerySystemInformation(
    _In_ SYSTEM_INFORMATION_CLASS InformationClass,
    _Outptr_ PVOID* Information)
{
    PVOID Buffer;
    ULONG Length = 0;
    NTSTATUS Status;

    Status = NtQuerySystemInformation(InformationClass,
                                      NULL,
                                      0,
                                      &Length);
    if (Status != STATUS_INFO_LENGTH_MISMATCH)
    {
        return Status;
    }
    for (;;)
    {
        Buffer = Mem_Alloc(Length);
        if (Buffer == NULL)
        {
            return STATUS_NO_MEMORY;
        }
        Status = NtQuerySystemInformation(InformationClass,
                                          Buffer,
                                          Length,
                                          &Length);
        if (NT_SUCCESS(Status))
        {
            *Information = Buffer;
            return STATUS_SUCCESS;
        }
        Mem_Free(Buffer);
        if (Status != STATUS_INFO_LENGTH_MISMATCH)
        {
            return Status;
        }
    }
}

static
NTSTATUS
ZpServerQuic_EnumerateProcesses(
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    PSYSTEM_PROCESS_INFORMATION Entry;
    PZP_PROCESS_RECORD Processes;
    PVOID SystemInfo;
    NTSTATUS Status;
    ULONG Count = 0, Index;

    *Payload = NULL;
    *PayloadLength = 0;
    Status = ZpServerQuic_QuerySystemInformation(SystemProcessInformation,
                                                 &SystemInfo);
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
        Mem_Free(SystemInfo);
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
    Status = ZpProcess_EncodeList(Processes,
                                  Count,
                                  NULL,
                                  0,
                                  PayloadLength);
    *Payload = NT_SUCCESS(Status) ? Mem_Alloc(*PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && *Payload == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpProcess_EncodeList(Processes,
                                      Count,
                                      *Payload,
                                      *PayloadLength,
                                      PayloadLength);
    }
    if (!NT_SUCCESS(Status) && *Payload != NULL)
    {
        Mem_Free(*Payload);
        *Payload = NULL;
    }
    Mem_Free(Processes);
    Mem_Free(SystemInfo);
    return Status;
}

static
NTSTATUS
ZpServerQuic_QueryProcess(
    _In_ ULONG ProcessId,
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    PSYSTEM_PROCESS_INFORMATION Entry;
    ZP_PROCESS_INFO Info;
    PVOID SystemInfo;
    NTSTATUS Status;

    *Payload = NULL;
    *PayloadLength = 0;
    Status = ZpServerQuic_QuerySystemInformation(SystemProcessInformation,
                                                 &SystemInfo);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Entry = SystemInfo;
    while ((ULONG)(ULONG_PTR)Entry->UniqueProcessId != ProcessId)
    {
        if (Entry->NextEntryOffset == 0)
        {
            Mem_Free(SystemInfo);
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
    Status = ZpProcess_EncodeInfo(&Info,
                                  NULL,
                                  0,
                                  PayloadLength);
    *Payload = NT_SUCCESS(Status) ? Mem_Alloc(*PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && *Payload == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpProcess_EncodeInfo(&Info,
                                      *Payload,
                                      *PayloadLength,
                                      PayloadLength);
    }
    if (!NT_SUCCESS(Status) && *Payload != NULL)
    {
        Mem_Free(*Payload);
        *Payload = NULL;
    }
    Mem_Free(SystemInfo);
    return Status;
}

static
NTSTATUS
ZpServerQuic_TerminateProcess(
    _In_ ULONG ProcessId,
    _In_ ULONG ExitCode)
{
    HANDLE Process;
    NTSTATUS Status = STATUS_SUCCESS;

    Process = OpenProcess(PROCESS_TERMINATE, FALSE, ProcessId);
    if (Process == NULL)
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    if (!TerminateProcess(Process, ExitCode))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
    }
    CloseHandle(Process);
    return Status;
}

static
NTSTATUS
ZpServerQuic_EnumerateServices(
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    LPENUM_SERVICE_STATUS_PROCESSW Entries = NULL;
    PZP_SERVICE_RECORD Services = NULL;
    SC_HANDLE Manager;
    PBYTE Buffer = NULL;
    DWORD BytesNeeded = 0, Count = 0, ResumeHandle = 0;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    *Payload = NULL;
    *PayloadLength = 0;
    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (Manager == NULL)
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    if (EnumServicesStatusExW(Manager,
                              SC_ENUM_PROCESS_INFO,
                              SERVICE_WIN32,
                              SERVICE_STATE_ALL,
                              NULL,
                              0,
                              &BytesNeeded,
                              &Count,
                              &ResumeHandle,
                              NULL))
    {
        Count = 0;
        goto Encode;
    }
    if (GetLastError() != ERROR_MORE_DATA)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    Buffer = Mem_Alloc(BytesNeeded);
    if (Buffer == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    ResumeHandle = 0;
    if (!EnumServicesStatusExW(Manager,
                               SC_ENUM_PROCESS_INFO,
                               SERVICE_WIN32,
                               SERVICE_STATE_ALL,
                               Buffer,
                               BytesNeeded,
                               &BytesNeeded,
                               &Count,
                               &ResumeHandle,
                               NULL))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    Entries = (LPENUM_SERVICE_STATUS_PROCESSW)Buffer;
    Services = Count != 0 ?
                   Mem_Alloc((SIZE_T)Count * sizeof(*Services)) :
                   NULL;
    if (Count != 0 && Services == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    for (Index = 0; Index < Count; Index++)
    {
        Services[Index].ServiceType = Entries[Index].ServiceStatusProcess.dwServiceType;
        Services[Index].CurrentState = Entries[Index].ServiceStatusProcess.dwCurrentState;
        Services[Index].ProcessId = Entries[Index].ServiceStatusProcess.dwProcessId;
        Services[Index].ServiceName = Entries[Index].lpServiceName;
        Services[Index].ServiceNameLength = (ULONG)wcslen(Entries[Index].lpServiceName);
        Services[Index].DisplayName = Entries[Index].lpDisplayName;
        Services[Index].DisplayNameLength = (ULONG)wcslen(Entries[Index].lpDisplayName);
    }

Encode:
    Status = ZpService_EncodeList(Services,
                                  Count,
                                  NULL,
                                  0,
                                  PayloadLength);
    *Payload = NT_SUCCESS(Status) ? Mem_Alloc(*PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && *Payload == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpService_EncodeList(Services,
                                      Count,
                                      *Payload,
                                      *PayloadLength,
                                      PayloadLength);
    }
    if (!NT_SUCCESS(Status) && *Payload != NULL)
    {
        Mem_Free(*Payload);
        *Payload = NULL;
    }

Cleanup:
    if (Services != NULL)
    {
        Mem_Free(Services);
    }
    if (Buffer != NULL)
    {
        Mem_Free(Buffer);
    }
    CloseServiceHandle(Manager);
    return Status;
}

static
NTSTATUS
ZpServerQuic_QueryService(
    _In_ PCZP_STRING_VIEW ServiceNameView,
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    SERVICE_STATUS_PROCESS ServiceStatus;
    LPQUERY_SERVICE_CONFIGW Config = NULL;
    ZP_SERVICE_INFO Info;
    SC_HANDLE Manager = NULL, Service = NULL;
    PWCHAR ServiceName;
    DWORD BytesNeeded = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    *Payload = NULL;
    *PayloadLength = 0;
    ServiceName = Mem_Alloc(((SIZE_T)ServiceNameView->Length + 1) *
                            sizeof(WCHAR));
    if (ServiceName == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlCopyMemory(ServiceName,
                  ServiceNameView->Buffer,
                  (SIZE_T)ServiceNameView->Length * sizeof(WCHAR));
    ServiceName[ServiceNameView->Length] = UNICODE_NULL;
    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (Manager == NULL)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    Service = OpenServiceW(Manager,
                           ServiceName,
                           SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (Service == NULL)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    if (!QueryServiceStatusEx(Service,
                              SC_STATUS_PROCESS_INFO,
                              (PBYTE)&ServiceStatus,
                              sizeof(ServiceStatus),
                              &BytesNeeded))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    if (QueryServiceConfigW(Service, NULL, 0, &BytesNeeded) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    Config = Mem_Alloc(BytesNeeded);
    if (Config == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    if (!QueryServiceConfigW(Service, Config, BytesNeeded, &BytesNeeded))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }

    Info.ServiceType = ServiceStatus.dwServiceType;
    Info.CurrentState = ServiceStatus.dwCurrentState;
    Info.ProcessId = ServiceStatus.dwProcessId;
    Info.StartType = Config->dwStartType;
    Info.ErrorControl = Config->dwErrorControl;
    Info.ServiceName = ServiceName;
    Info.ServiceNameLength = ServiceNameView->Length;
    Info.DisplayName = Config->lpDisplayName;
    Info.DisplayNameLength = Config->lpDisplayName != NULL ?
                                 (ULONG)wcslen(Config->lpDisplayName) :
                                 0;
    Info.BinaryPathName = Config->lpBinaryPathName;
    Info.BinaryPathNameLength = Config->lpBinaryPathName != NULL ?
                                    (ULONG)wcslen(Config->lpBinaryPathName) :
                                    0;
    Info.StartName = Config->lpServiceStartName;
    Info.StartNameLength = Config->lpServiceStartName != NULL ?
                               (ULONG)wcslen(Config->lpServiceStartName) :
                               0;
    Status = ZpService_EncodeInfo(&Info,
                                  NULL,
                                  0,
                                  PayloadLength);
    *Payload = NT_SUCCESS(Status) ? Mem_Alloc(*PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && *Payload == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpService_EncodeInfo(&Info,
                                      *Payload,
                                      *PayloadLength,
                                      PayloadLength);
    }
    if (!NT_SUCCESS(Status) && *Payload != NULL)
    {
        Mem_Free(*Payload);
        *Payload = NULL;
    }

Cleanup:
    if (Config != NULL)
    {
        Mem_Free(Config);
    }
    if (Service != NULL)
    {
        CloseServiceHandle(Service);
    }
    if (Manager != NULL)
    {
        CloseServiceHandle(Manager);
    }
    Mem_Free(ServiceName);
    return Status;
}

static
NTSTATUS
ZpServerQuic_ControlService(
    _In_ PCZP_STRING_VIEW ServiceNameView,
    _In_ LOGICAL Start)
{
    SERVICE_STATUS ServiceStatus;
    SC_HANDLE Manager = NULL, Service = NULL;
    PWCHAR ServiceName;
    NTSTATUS Status = STATUS_SUCCESS;

    ServiceName = Mem_Alloc(((SIZE_T)ServiceNameView->Length + 1) *
                            sizeof(WCHAR));
    if (ServiceName == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlCopyMemory(ServiceName,
                  ServiceNameView->Buffer,
                  (SIZE_T)ServiceNameView->Length * sizeof(WCHAR));
    ServiceName[ServiceNameView->Length] = UNICODE_NULL;
    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (Manager == NULL)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    Service = OpenServiceW(Manager,
                           ServiceName,
                           Start ? SERVICE_START : SERVICE_STOP);
    if (Service == NULL)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    if (Start)
    {
        if (!StartServiceW(Service, 0, NULL))
        {
            Status = NTSTATUS_FROM_WIN32(GetLastError());
        }
    }
    else if (!ControlService(Service, SERVICE_CONTROL_STOP, &ServiceStatus))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
    }

Cleanup:
    if (Service != NULL)
    {
        CloseServiceHandle(Service);
    }
    if (Manager != NULL)
    {
        CloseServiceHandle(Manager);
    }
    Mem_Free(ServiceName);
    return Status;
}

static
NTSTATUS
ZpServerQuic_SendResponse(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection,
    _Inout_ PZP_CONNECTION Connection,
    _In_ ULONGLONG RequestId,
    _In_ NTSTATUS ResponseStatus,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength)
{
    ZP_RESPONSE Response = {
        RequestId,
        ResponseStatus,
        Payload,
        PayloadLength
    };
    PBYTE Body;
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeResponse(&Response,
                                      NULL,
                                      0,
                                      &BodyLength);
    Body = NT_SUCCESS(Status) ? Mem_Alloc(BodyLength) : NULL;
    if (NT_SUCCESS(Status) && Body == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpMessage_EncodeResponse(&Response,
                                          Body,
                                          BodyLength,
                                          &BodyLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpQuic_SendFrame(QuicConnection->Stream,
                                  Connection,
                                  ZpMessageResponse,
                                  Body,
                                  BodyLength);
    }
    if (Body != NULL)
    {
        Mem_Free(Body);
    }
    return Status;
}

static
LOGICAL
ZpServerQuic_HasModule(
    _In_ PZP_SERVER_QUIC_CONNECTION QuicConnection,
    _In_ USHORT ModuleId)
{
    USHORT Index;

    for (Index = 0; Index < QuicConnection->ModuleCount; Index++)
    {
        if (QuicConnection->Modules[Index].ModuleId == ModuleId)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static
VOID
CALLBACK
ZpServerQuic_RequestCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_QUIC_REQUEST Request = Context;
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Request->Connection;
    ZP_SYSTEM_INFO SystemInfo;
    BYTE Payload[30 + (MAX_COMPUTERNAME_LENGTH + 1) * sizeof(WCHAR)];
    WCHAR ComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    const VOID* ResponsePayload = NULL;
    PBYTE AllocatedPayload = NULL;
    ULONG PayloadLength = 0;
    ULONG ProcessId, ExitCode;
    ZP_STRING_VIEW ServiceName;
    ZP_REQUEST_ACCESS Access;
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL SendResponse;

    UNREFERENCED_PARAMETER(Instance);
    RtlAcquireSRWLockShared(&QuicConnection->RequestLock);
    SendResponse = Request->Pending && !QuicConnection->Closing;
    RtlReleaseSRWLockShared(&QuicConnection->RequestLock);
    if (!SendResponse)
    {
        goto Cleanup;
    }

    if (!ZpServerQuic_HasModule(QuicConnection, Request->ModuleId))
    {
        Status = STATUS_NOT_SUPPORTED;
    }
    else
    {
        ZP_BUFFER_VIEW RequestPayload = {
            Request->Payload,
            Request->PayloadLength
        };

        Access = (Request->ModuleId == ZP_PROCESS_MODULE_ID &&
                  Request->OperationId == ZP_PROCESS_OPERATION_TERMINATE) ||
                 (Request->ModuleId == ZP_SERVICE_MODULE_ID &&
                  (Request->OperationId == ZP_SERVICE_OPERATION_START ||
                   Request->OperationId == ZP_SERVICE_OPERATION_STOP)) ?
                     ZpRequestAccessControl :
                     ZpRequestAccessRead;
        Status = ZpServer_AuthorizeRequest(
            (ZP_SERVER_HANDLE)QuicConnection->Transport->Owner,
            (ZP_CONNECTION_HANDLE)QuicConnection,
            QuicConnection->ClientId,
            Access,
            Request->ModuleId,
            Request->OperationId,
            &RequestPayload);
    }
    if (NT_SUCCESS(Status))
    {
        if (Request->ModuleId == ZP_SYSTEM_MODULE_ID &&
            Request->OperationId == ZP_SYSTEM_OPERATION_INFO)
        {
            Status = Request->PayloadLength == 0 ?
                         ZpServerQuic_GetSystemInfo(&SystemInfo,
                                                    ComputerName,
                                                    ARRAYSIZE(ComputerName)) :
                         STATUS_INVALID_PARAMETER;
            if (NT_SUCCESS(Status))
            {
                Status = ZpSystem_EncodeInfo(&SystemInfo,
                                             Payload,
                                             sizeof(Payload),
                                             &PayloadLength);
                ResponsePayload = Payload;
            }
        }
        else if (Request->ModuleId == ZP_PROCESS_MODULE_ID &&
                 Request->OperationId == ZP_PROCESS_OPERATION_ENUMERATE)
        {
            Status = Request->PayloadLength == 0 ?
                         ZpServerQuic_EnumerateProcesses(&AllocatedPayload,
                                                         &PayloadLength) :
                         STATUS_INVALID_PARAMETER;
            ResponsePayload = AllocatedPayload;
        }
        else if (Request->ModuleId == ZP_PROCESS_MODULE_ID &&
                 Request->OperationId == ZP_PROCESS_OPERATION_QUERY)
        {
            Status = ZpProcess_DecodeQuery(Request->Payload,
                                           Request->PayloadLength,
                                           &ProcessId);
            if (NT_SUCCESS(Status))
            {
                Status = ZpServerQuic_QueryProcess(ProcessId,
                                                   &AllocatedPayload,
                                                   &PayloadLength);
                ResponsePayload = AllocatedPayload;
            }
        }
        else if (Request->ModuleId == ZP_PROCESS_MODULE_ID &&
                 Request->OperationId == ZP_PROCESS_OPERATION_TERMINATE)
        {
            Status = ZpProcess_DecodeTerminate(Request->Payload,
                                               Request->PayloadLength,
                                               &ProcessId,
                                               &ExitCode);
            if (NT_SUCCESS(Status))
            {
                Status = ZpServerQuic_TerminateProcess(ProcessId, ExitCode);
            }
        }
        else if (Request->ModuleId == ZP_SERVICE_MODULE_ID &&
                 Request->OperationId == ZP_SERVICE_OPERATION_ENUMERATE)
        {
            Status = Request->PayloadLength == 0 ?
                         ZpServerQuic_EnumerateServices(&AllocatedPayload,
                                                        &PayloadLength) :
                         STATUS_INVALID_PARAMETER;
            ResponsePayload = AllocatedPayload;
        }
        else if (Request->ModuleId == ZP_SERVICE_MODULE_ID &&
                 Request->OperationId == ZP_SERVICE_OPERATION_QUERY)
        {
            Status = ZpService_DecodeQuery(Request->Payload,
                                           Request->PayloadLength,
                                           &ServiceName);
            if (NT_SUCCESS(Status))
            {
                Status = ZpServerQuic_QueryService(&ServiceName,
                                                   &AllocatedPayload,
                                                   &PayloadLength);
                ResponsePayload = AllocatedPayload;
            }
        }
        else if (Request->ModuleId == ZP_SERVICE_MODULE_ID &&
                 (Request->OperationId == ZP_SERVICE_OPERATION_START ||
                  Request->OperationId == ZP_SERVICE_OPERATION_STOP))
        {
            Status = ZpService_DecodeQuery(Request->Payload,
                                           Request->PayloadLength,
                                           &ServiceName);
            if (NT_SUCCESS(Status))
            {
                Status = ZpServerQuic_ControlService(
                    &ServiceName,
                    Request->OperationId == ZP_SERVICE_OPERATION_START);
            }
        }
        else
        {
            Status = STATUS_NOT_SUPPORTED;
        }
    }
    if (NT_SUCCESS(Status) &&
        Request->TimeoutMilliseconds != 0 &&
        GetTickCount64() - Request->ReceivedTickCount >=
            Request->TimeoutMilliseconds)
    {
        Status = STATUS_IO_TIMEOUT;
    }

    RtlAcquireSRWLockExclusive(&QuicConnection->RequestLock);
    SendResponse = InterlockedExchange(&Request->Pending, FALSE) &&
                   !QuicConnection->Closing;
    if (SendResponse)
    {
        RemoveEntryList(&Request->ListEntry);
        QuicConnection->ActiveRequestCount--;
        ZpServerQuic_SendResponse(QuicConnection,
                                  &QuicConnection->ProtocolConnection,
                                  Request->RequestId,
                                  Status,
                                  NT_SUCCESS(Status) ? ResponsePayload : NULL,
                                  NT_SUCCESS(Status) ? PayloadLength : 0);
    }
    RtlReleaseSRWLockExclusive(&QuicConnection->RequestLock);

Cleanup:
    if (AllocatedPayload != NULL)
    {
        Mem_Free(AllocatedPayload);
    }
    Mem_Free(Request);
    ZpServerQuic_ReleaseConnection(QuicConnection);
}

static
NTSTATUS
ZpServerQuic_QueueRequest(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection,
    _In_ PCZP_REQUEST_VIEW Message)
{
    PZP_SERVER_QUIC_REQUEST Request, ExistingRequest;
    PLIST_ENTRY Entry;
    SIZE_T AllocationSize;

    AllocationSize = FIELD_OFFSET(ZP_SERVER_QUIC_REQUEST, Payload) +
                     Message->Payload.Length;
    Request = Mem_Alloc(AllocationSize);
    if (Request == NULL)
    {
        return ZpServerQuic_SendResponse(QuicConnection,
                                         &QuicConnection->ProtocolConnection,
                                         Message->RequestId,
                                         STATUS_NO_MEMORY,
                                         NULL,
                                         0);
    }
    RtlZeroMemory(Request, FIELD_OFFSET(ZP_SERVER_QUIC_REQUEST, Payload));
    Request->Connection = QuicConnection;
    Request->Pending = TRUE;
    Request->RequestId = Message->RequestId;
    Request->ModuleId = Message->ModuleId;
    Request->OperationId = Message->OperationId;
    Request->TimeoutMilliseconds = Message->TimeoutMilliseconds;
    Request->ReceivedTickCount = GetTickCount64();
    Request->PayloadLength = Message->Payload.Length;
    if (Message->Payload.Length != 0)
    {
        RtlCopyMemory(Request->Payload,
                      Message->Payload.Buffer,
                      Message->Payload.Length);
    }

    RtlAcquireSRWLockExclusive(&QuicConnection->RequestLock);
    if (QuicConnection->Closing)
    {
        RtlReleaseSRWLockExclusive(&QuicConnection->RequestLock);
        Mem_Free(Request);
        return STATUS_CONNECTION_DISCONNECTED;
    }
    if (QuicConnection->ActiveRequestCount >=
        QuicConnection->Transport->Owner->Config.MaxRequestsPerConnection)
    {
        RtlReleaseSRWLockExclusive(&QuicConnection->RequestLock);
        Mem_Free(Request);
        return ZpServerQuic_SendResponse(QuicConnection,
                                         &QuicConnection->ProtocolConnection,
                                         Message->RequestId,
                                         STATUS_QUOTA_EXCEEDED,
                                         NULL,
                                         0);
    }
    for (Entry = QuicConnection->Requests.Flink;
         Entry != &QuicConnection->Requests;
         Entry = Entry->Flink)
    {
        ExistingRequest = CONTAINING_RECORD(Entry,
                                            ZP_SERVER_QUIC_REQUEST,
                                            ListEntry);
        if (ExistingRequest->RequestId == Request->RequestId)
        {
            RtlReleaseSRWLockExclusive(&QuicConnection->RequestLock);
            Mem_Free(Request);
            return STATUS_PROTOCOL_UNREACHABLE;
        }
    }
    InsertTailList(&QuicConnection->Requests, &Request->ListEntry);
    QuicConnection->ActiveRequestCount++;
    InterlockedIncrement(&QuicConnection->ReferenceCount);
    RtlReleaseSRWLockExclusive(&QuicConnection->RequestLock);

    if (!TrySubmitThreadpoolCallback(ZpServerQuic_RequestCallback,
                                     Request,
                                     NULL))
    {
        NTSTATUS Status;

        RtlAcquireSRWLockExclusive(&QuicConnection->RequestLock);
        if (InterlockedExchange(&Request->Pending, FALSE))
        {
            RemoveEntryList(&Request->ListEntry);
            QuicConnection->ActiveRequestCount--;
        }
        RtlReleaseSRWLockExclusive(&QuicConnection->RequestLock);
        Mem_Free(Request);
        Status = ZpServerQuic_SendResponse(QuicConnection,
                                           &QuicConnection->ProtocolConnection,
                                           Message->RequestId,
                                           STATUS_NO_MEMORY,
                                           NULL,
                                           0);
        ZpServerQuic_ReleaseConnection(QuicConnection);
        return Status;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpServerQuic_CancelRequest(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection,
    _In_ ULONGLONG RequestId)
{
    PZP_SERVER_QUIC_REQUEST Request;
    PLIST_ENTRY Entry;

    RtlAcquireSRWLockExclusive(&QuicConnection->RequestLock);
    for (Entry = QuicConnection->Requests.Flink;
         Entry != &QuicConnection->Requests;
         Entry = Entry->Flink)
    {
        Request = CONTAINING_RECORD(Entry,
                                    ZP_SERVER_QUIC_REQUEST,
                                    ListEntry);
        if (Request->RequestId == RequestId)
        {
            InterlockedExchange(&Request->Pending, FALSE);
            RemoveEntryList(&Request->ListEntry);
            QuicConnection->ActiveRequestCount--;
            RtlReleaseSRWLockExclusive(&QuicConnection->RequestLock);
            return STATUS_SUCCESS;
        }
    }
    RtlReleaseSRWLockExclusive(&QuicConnection->RequestLock);
    return STATUS_PROTOCOL_UNREACHABLE;
}

static
NTSTATUS
ZpServerQuic_SelectModules(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection,
    _In_ const ZP_CLIENT_HELLO_VIEW* Hello)
{
    PZP_SERVER_OBJECT Object = QuicConnection->Transport->Owner;
    ZP_MODULE_RECORD ClientModule;
    ULONG ClientIndex = 0;
    ULONG ServerIndex = 0;
    NTSTATUS Status;

    QuicConnection->ModuleCount = 0;
    while (ClientIndex < Hello->Modules.Count &&
           ServerIndex < Object->Config.ModuleCount)
    {
        Status = ZpMessage_GetModuleRecord(&Hello->Modules,
                                           (USHORT)ClientIndex,
                                           &ClientModule);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if (ClientModule.ModuleId < Object->Config.Modules[ServerIndex].ModuleId)
        {
            ClientIndex++;
            continue;
        }
        if (ClientModule.ModuleId > Object->Config.Modules[ServerIndex].ModuleId)
        {
            ServerIndex++;
            continue;
        }
        QuicConnection->Modules[QuicConnection->ModuleCount].ModuleId = ClientModule.ModuleId;
        QuicConnection->Modules[QuicConnection->ModuleCount].ModuleVersion =
            min(ClientModule.ModuleVersion,
                Object->Config.Modules[ServerIndex].ModuleVersion);
        QuicConnection->Modules[QuicConnection->ModuleCount].Capabilities =
            ClientModule.Capabilities & Object->Config.Modules[ServerIndex].Capabilities;
        QuicConnection->ModuleCount++;
        ClientIndex++;
        ServerIndex++;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
ZpServerQuic_MessageCallback(
    _Inout_ PZP_CONNECTION Connection,
    _In_ const ZP_FRAME_VIEW* Frame,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Context;
    PZP_SERVER_OBJECT Object = QuicConnection->Transport->Owner;
    ZP_CLIENT_HELLO_VIEW Hello;
    ZP_BUFFER_VIEW Signature;
    ZP_DISCONNECT_VIEW Disconnect;
    ZP_REQUEST_VIEW Request;
    ZP_READY Ready;
    BYTE Body[sizeof(USHORT) + ZP_MODULE_MAX_COUNT * 8];
    ULONG BodyLength;
    ULONGLONG Token;
    ULONGLONG RequestId;
    NTSTATUS Status;

    switch (Frame->MessageType)
    {
        case ZpMessageClientHello:
            Status = ZpMessage_DecodeClientHello(Frame->Body,
                                                  Frame->BodyLength,
                                                  &Hello);
            if (NT_SUCCESS(Status))
            {
                RtlCopyMemory(QuicConnection->PublicKey,
                              Hello.ClientPublicKey,
                              sizeof(QuicConnection->PublicKey));
                Status = ZpAuthentication_GetClientId(QuicConnection->PublicKey,
                                                      QuicConnection->ClientId);
            }
            if (NT_SUCCESS(Status))
            {
                Status = ZpServerQuic_SelectModules(QuicConnection, &Hello);
            }
            if (NT_SUCCESS(Status))
            {
                Status = BCryptGenRandom(NULL,
                                         QuicConnection->Challenge,
                                         sizeof(QuicConnection->Challenge),
                                         BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            }
            if (NT_SUCCESS(Status))
            {
                Status = ZpMessage_EncodeServerChallenge(QuicConnection->Challenge,
                                                         Body,
                                                         sizeof(Body),
                                                         &BodyLength);
            }
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            return ZpQuic_SendFrame(QuicConnection->Stream,
                                    Connection,
                                    ZpMessageServerChallenge,
                                    Body,
                                    BodyLength);

        case ZpMessageClientAuthenticate:
            Status = ZpMessage_DecodeClientAuthenticate(Frame->Body,
                                                        Frame->BodyLength,
                                                        &Signature);
            if (NT_SUCCESS(Status))
            {
                Status = ZpAuthentication_Verify(QuicConnection->PublicKey,
                                                 QuicConnection->Challenge,
                                                 Signature.Buffer);
            }
            RtlSecureZeroMemory(QuicConnection->Challenge,
                                sizeof(QuicConnection->Challenge));
            if (!NT_SUCCESS(Status))
            {
                return STATUS_ACCESS_DENIED;
            }
            Ready.Modules = QuicConnection->Modules;
            Ready.ModuleCount = QuicConnection->ModuleCount;
            Status = ZpMessage_EncodeReady(&Ready,
                                           Body,
                                           sizeof(Body),
                                           &BodyLength);
            if (NT_SUCCESS(Status))
            {
                Status = ZpQuic_SendFrame(QuicConnection->Stream,
                                          Connection,
                                          ZpMessageReady,
                                          Body,
                                          BodyLength);
            }
            if (NT_SUCCESS(Status))
            {
                ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Object,
                                          (ZP_CONNECTION_HANDLE)QuicConnection,
                                          ZpConnectionPhaseReady,
                                          STATUS_SUCCESS);
            }
            return Status;

        case ZpMessageDisconnect:
            Status = ZpMessage_DecodeDisconnect(Frame->Body,
                                                Frame->BodyLength,
                                                &Disconnect);
            if (NT_SUCCESS(Status))
            {
                InterlockedExchange((volatile LONG*)&QuicConnection->ShutdownStatus,
                                    Disconnect.Status);
                MsQuicConnectionShutdown(QuicConnection->Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
            }
            return Status;

        case ZpMessagePing:
            Status = ZpMessage_DecodePing(ZpMessagePing,
                                          Frame->Body,
                                          Frame->BodyLength,
                                          &Token);
            if (NT_SUCCESS(Status))
            {
                Status = ZpMessage_EncodePing(Token,
                                              Body,
                                              sizeof(Body),
                                              &BodyLength);
            }
            if (NT_SUCCESS(Status))
            {
                Status = ZpQuic_SendFrame(QuicConnection->Stream,
                                          Connection,
                                          ZpMessagePong,
                                          Body,
                                          BodyLength);
            }
            return Status;

        case ZpMessageRequest:
            Status = ZpMessage_DecodeRequest(Frame->Body,
                                             Frame->BodyLength,
                                             &Request);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            return ZpServerQuic_QueueRequest(QuicConnection, &Request);

        case ZpMessageCancel:
            Status = ZpMessage_DecodeCancel(Frame->Body,
                                            Frame->BodyLength,
                                            &RequestId);
            return NT_SUCCESS(Status) ?
                       ZpServerQuic_CancelRequest(QuicConnection, RequestId) :
                       Status;
    }
    return STATUS_PROTOCOL_UNREACHABLE;
}

static
VOID
ZpServerQuic_TryCompleteStop(
    _Inout_ PZP_SERVER_QUIC_TRANSPORT Transport)
{
    PZP_SERVER_OBJECT Object = Transport->Owner;
    LOGICAL Complete;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Complete = Transport->Stopping &&
               Transport->ActiveConnectionCount == 0 &&
               Transport->StoppedListenerCount == Transport->StartedListenerCount;
    if (Complete)
    {
        Transport->Stopping = FALSE;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Complete)
    {
        ZpServer_NotifyState((ZP_SERVER_HANDLE)Object,
                             ZpServerStateStopped,
                             STATUS_SUCCESS);
    }
}

static
QUIC_STATUS
QUIC_API
ZpServerQuic_StreamCallback(
    _In_ HQUIC Stream,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_STREAM_EVENT* Event)
{
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Context;
    NTSTATUS Status;
    ULONG Index;

    switch (Event->Type)
    {
        case QUIC_STREAM_EVENT_SEND_COMPLETE:
            ZpQuic_CompleteSend(Event->SEND_COMPLETE.ClientContext);
            break;

        case QUIC_STREAM_EVENT_RECEIVE:
            Status = STATUS_SUCCESS;
            for (Index = 0;
                 NT_SUCCESS(Status) && Index < Event->RECEIVE.BufferCount;
                 Index++)
            {
                Status = ZpConnection_Receive(&QuicConnection->ProtocolConnection,
                                              Event->RECEIVE.Buffers[Index].Buffer,
                                              Event->RECEIVE.Buffers[Index].Length);
            }
            if (!NT_SUCCESS(Status))
            {
                InterlockedExchange((volatile LONG*)&QuicConnection->ShutdownStatus,
                                    Status);
                MsQuicConnectionShutdown(QuicConnection->Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
            }
            break;

        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
            InterlockedExchange((volatile LONG*)&QuicConnection->ShutdownStatus,
                                STATUS_CONNECTION_DISCONNECTED);
            MsQuicConnectionShutdown(QuicConnection->Connection,
                                     QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                     0);
            break;

        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            if (QuicConnection->Stream == Stream)
            {
                QuicConnection->Stream = NULL;
            }
            if (!Event->SHUTDOWN_COMPLETE.AppCloseInProgress)
            {
                MsQuicStreamClose(Stream);
            }
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

static
VOID
ZpServerQuic_ReleaseConnection(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection)
{
    PZP_SERVER_QUIC_TRANSPORT Transport;
    PZP_SERVER_OBJECT Object;

    if (InterlockedDecrement(&QuicConnection->ReferenceCount) != 0)
    {
        return;
    }
    Transport = QuicConnection->Transport;
    Object = Transport->Owner;
    if (QuicConnection->ProtocolConnectionInitialized)
    {
        ZpConnection_Uninitialize(&QuicConnection->ProtocolConnection);
        QuicConnection->ProtocolConnectionInitialized = FALSE;
    }
    RtlSecureZeroMemory(QuicConnection->PublicKey,
                        sizeof(QuicConnection->PublicKey));
    RtlSecureZeroMemory(QuicConnection->ClientId,
                        sizeof(QuicConnection->ClientId));
    RtlSecureZeroMemory(QuicConnection->Challenge,
                        sizeof(QuicConnection->Challenge));
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Transport->ActiveConnectionCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    Mem_Free(QuicConnection);
    ZpServerQuic_TryCompleteStop(Transport);
}

static
VOID
ZpServerQuic_CancelRequests(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection)
{
    PZP_SERVER_QUIC_REQUEST Request;

    RtlAcquireSRWLockExclusive(&QuicConnection->RequestLock);
    QuicConnection->Closing = TRUE;
    while (!IsListEmpty(&QuicConnection->Requests))
    {
        Request = CONTAINING_RECORD(QuicConnection->Requests.Flink,
                                    ZP_SERVER_QUIC_REQUEST,
                                    ListEntry);
        InterlockedExchange(&Request->Pending, FALSE);
        RemoveEntryList(&Request->ListEntry);
        QuicConnection->ActiveRequestCount--;
    }
    RtlReleaseSRWLockExclusive(&QuicConnection->RequestLock);
}

static
QUIC_STATUS
QUIC_API
ZpServerQuic_ConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event)
{
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Context;
    PZP_SERVER_QUIC_TRANSPORT Transport = QuicConnection->Transport;
    PZP_SERVER_OBJECT Object = Transport->Owner;
    NTSTATUS Status;

    switch (Event->Type)
    {
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
            if ((Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) != 0 ||
                QuicConnection->Stream != NULL)
            {
                InterlockedExchange((volatile LONG*)&QuicConnection->ShutdownStatus,
                                    STATUS_PROTOCOL_UNREACHABLE);
                MsQuicConnectionShutdown(Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
                break;
            }
            Status = ZpConnection_Initialize(&QuicConnection->ProtocolConnection,
                                             ZpConnectionRoleServer,
                                             ZpServerQuic_MessageCallback,
                                             QuicConnection);
            if (!NT_SUCCESS(Status))
            {
                InterlockedExchange((volatile LONG*)&QuicConnection->ShutdownStatus,
                                    Status);
                MsQuicConnectionShutdown(Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
                break;
            }
            QuicConnection->ProtocolConnectionInitialized = TRUE;
            QuicConnection->Stream = Event->PEER_STREAM_STARTED.Stream;
            MsQuicSetCallbackHandler(QuicConnection->Stream,
                                     ZpServerQuic_StreamCallback,
                                     QuicConnection);
            ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Object,
                                      (ZP_CONNECTION_HANDLE)QuicConnection,
                                      ZpConnectionPhaseAuthenticating,
                                      STATUS_SUCCESS);
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            InterlockedExchange(
                (volatile LONG*)&QuicConnection->ShutdownStatus,
                ZpQuic_StatusToNtStatus(Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status));
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
            InterlockedExchange((volatile LONG*)&QuicConnection->ShutdownStatus,
                                STATUS_CONNECTION_DISCONNECTED);
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            Status = QuicConnection->ShutdownStatus;
            ZpServerQuic_CancelRequests(QuicConnection);
            MsQuicConnectionClose(Connection);
            QuicConnection->Connection = NULL;
            ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Object,
                                      (ZP_CONNECTION_HANDLE)QuicConnection,
                                      ZpConnectionPhaseClosed,
                                      Status);
            ZpServerQuic_ReleaseConnection(QuicConnection);
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

static
LONG
ZpServerQuic_FindDeployment(
    _In_ PZP_SERVER_QUIC_TRANSPORT Transport,
    _In_ const QUIC_NEW_CONNECTION_INFO* Info)
{
    ULONG Index;
    SIZE_T Length;

    if (Info->ServerName == NULL || Info->ServerNameLength == 0)
    {
        return -1;
    }
    for (Index = 0; Index < Transport->Owner->Config.DeploymentCount; Index++)
    {
        Length = strlen(Transport->ServerNames[Index]);
        if (Length == Info->ServerNameLength &&
            _strnicmp(Transport->ServerNames[Index],
                      Info->ServerName,
                      Info->ServerNameLength) == 0)
        {
            return (LONG)Index;
        }
    }
    return -1;
}

static
QUIC_STATUS
QUIC_API
ZpServerQuic_ListenerCallback(
    _In_ HQUIC Listener,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_LISTENER_EVENT* Event)
{
    PZP_SERVER_QUIC_LISTENER QuicListener = Context;
    PZP_SERVER_QUIC_TRANSPORT Transport = QuicListener->Transport;
    PZP_SERVER_OBJECT Object = Transport->Owner;
    PZP_SERVER_QUIC_CONNECTION QuicConnection;
    QUIC_STATUS QuicStatus;
    LONG DeploymentIndex;

    switch (Event->Type)
    {
        case QUIC_LISTENER_EVENT_NEW_CONNECTION:
            DeploymentIndex = ZpServerQuic_FindDeployment(Transport,
                                                           Event->NEW_CONNECTION.Info);
            if (DeploymentIndex < 0)
            {
                return QUIC_STATUS_NOT_SUPPORTED;
            }
            QuicConnection = Mem_Alloc(sizeof(*QuicConnection));
            if (QuicConnection == NULL)
            {
                return QUIC_STATUS_OUT_OF_MEMORY;
            }
            RtlZeroMemory(QuicConnection, sizeof(*QuicConnection));
            QuicConnection->Transport = Transport;
            RtlInitializeSRWLock(&QuicConnection->RequestLock);
            InitializeListHead(&QuicConnection->Requests);
            QuicConnection->ReferenceCount = 1;
            QuicConnection->Connection = Event->NEW_CONNECTION.Connection;
            QuicConnection->ShutdownStatus = STATUS_SUCCESS;
            MsQuicSetCallbackHandler(QuicConnection->Connection,
                                     ZpServerQuic_ConnectionCallback,
                                     QuicConnection);
            RtlAcquireSRWLockExclusive(&Object->Lock);
            Transport->ActiveConnectionCount++;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Object,
                                      (ZP_CONNECTION_HANDLE)QuicConnection,
                                      ZpConnectionPhaseConnecting,
                                      STATUS_SUCCESS);
            QuicStatus = MsQuicConnectionSetConfiguration(
                QuicConnection->Connection,
                Transport->Configurations[DeploymentIndex]);
            if (QUIC_FAILED(QuicStatus))
            {
                ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Object,
                                          (ZP_CONNECTION_HANDLE)QuicConnection,
                                          ZpConnectionPhaseClosed,
                                          ZpQuic_StatusToNtStatus(QuicStatus));
                RtlAcquireSRWLockExclusive(&Object->Lock);
                Transport->ActiveConnectionCount--;
                RtlReleaseSRWLockExclusive(&Object->Lock);
                Mem_Free(QuicConnection);
                return QuicStatus;
            }
            return QUIC_STATUS_SUCCESS;

        case QUIC_LISTENER_EVENT_STOP_COMPLETE:
            RtlAcquireSRWLockExclusive(&Object->Lock);
            if (QuicListener->Handle == Listener)
            {
                QuicListener->Handle = NULL;
                Transport->StoppedListenerCount++;
            }
            RtlReleaseSRWLockExclusive(&Object->Lock);
            if (!Event->STOP_COMPLETE.AppCloseInProgress)
            {
                MsQuicListenerClose(Listener);
            }
            ZpServerQuic_TryCompleteStop(Transport);
            return QUIC_STATUS_SUCCESS;
    }
    return QUIC_STATUS_SUCCESS;
}

static
NTSTATUS
ZpServerQuic_CreateConfigurations(
    _Inout_ PZP_SERVER_QUIC_TRANSPORT Transport)
{
    PZP_SERVER_OBJECT Object = Transport->Owner;
    QUIC_SETTINGS Settings = { 0 };
    QUIC_CREDENTIAL_CONFIG Credentials = { 0 };
    QUIC_STATUS QuicStatus;
    ULONG Index, ServerNameSize;

    Settings.PeerBidiStreamCount = 1;
    Settings.IsSet.PeerBidiStreamCount = TRUE;
    for (Index = 0; Index < Object->Config.DeploymentCount; Index++)
    {
        ServerNameSize = Str_UnicodeToUtf8(NULL,
                                           0,
                                           Object->Config.Deployments[Index].ServerName);
        Transport->ServerNames[Index] = Mem_Alloc(ServerNameSize);
        if (Transport->ServerNames[Index] == NULL ||
            Str_UnicodeToUtf8(Transport->ServerNames[Index],
                              ServerNameSize,
                              Object->Config.Deployments[Index].ServerName) == 0)
        {
            return STATUS_NO_MEMORY;
        }
        QuicStatus = MsQuicConfigurationOpen(Transport->Registration,
                                             &ZpQuicAlpn,
                                             1,
                                             &Settings,
                                             sizeof(Settings),
                                             NULL,
                                             &Transport->Configurations[Index]);
        if (QUIC_FAILED(QuicStatus))
        {
            return ZpQuic_StatusToNtStatus(QuicStatus);
        }
        RtlZeroMemory(&Credentials, sizeof(Credentials));
        Credentials.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_CONTEXT;
        Credentials.CertificateContext = (QUIC_CERTIFICATE*)Object->Config.Deployments[
            Index].Certificate;
        QuicStatus = MsQuicConfigurationLoadCredential(
            Transport->Configurations[Index],
            &Credentials);
        if (QUIC_FAILED(QuicStatus))
        {
            return ZpQuic_StatusToNtStatus(QuicStatus);
        }
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
ZpServerQuic_Start(
    _In_opt_ PVOID Context,
    _In_ ULONG EndpointIndex)
{
    PZP_SERVER_QUIC_TRANSPORT Transport = Context;
    PZP_SERVER_OBJECT Object = Transport->Owner;
    QUIC_STATUS QuicStatus;
    QUIC_ADDR Address;
    NTSTATUS Status;
    ULONG Index;

    UNREFERENCED_PARAMETER(EndpointIndex);

    ZpServerQuic_Uninitialize(Transport);
    Transport->Owner = Object;
    QuicStatus = KNSoftQuicInitialize();
    if (QUIC_FAILED(QuicStatus))
    {
        return ZpQuic_StatusToNtStatus(QuicStatus);
    }
    Transport->Initialized = TRUE;
    QuicStatus = MsQuicRegistrationOpen(&ZpServerQuicRegistrationConfig,
                                        &Transport->Registration);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpQuic_StatusToNtStatus(QuicStatus);
        goto Cleanup;
    }
    Status = ZpServerQuic_CreateConfigurations(Transport);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    for (Index = 0; Index < Object->Config.ListenerCount; Index++)
    {
        Transport->Listeners[Index].Transport = Transport;
        Transport->Listeners[Index].Index = Index;
        QuicStatus = MsQuicListenerOpen(Transport->Registration,
                                        ZpServerQuic_ListenerCallback,
                                        &Transport->Listeners[Index],
                                        &Transport->Listeners[Index].Handle);
        if (QUIC_FAILED(QuicStatus))
        {
            Status = ZpQuic_StatusToNtStatus(QuicStatus);
            goto Cleanup;
        }
        Status = ZpQuic_ResolveAddress(Object->Config.Listeners[Index].Host,
                                       Object->Config.Listeners[Index].Port,
                                       &Address);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
        QuicStatus = MsQuicListenerStart(Transport->Listeners[Index].Handle,
                                         &ZpQuicAlpn,
                                         1,
                                         &Address);
        if (QUIC_FAILED(QuicStatus))
        {
            Status = ZpQuic_StatusToNtStatus(QuicStatus);
            goto Cleanup;
        }
        Transport->StartedListenerCount++;
    }
    Status = ZpServer_NotifyState((ZP_SERVER_HANDLE)Object,
                                  ZpServerStateRunning,
                                  STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        return STATUS_SUCCESS;
    }

Cleanup:
    ZpServerQuic_Uninitialize(Transport);
    Transport->Owner = Object;
    return Status;
}

static
VOID
NTAPI
ZpServerQuic_Stop(
    _In_opt_ PVOID Context)
{
    PZP_SERVER_QUIC_TRANSPORT Transport = Context;
    PZP_SERVER_OBJECT Object = Transport->Owner;
    ULONG Index;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Transport->Stopping = TRUE;
    Transport->StoppedListenerCount = 0;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    MsQuicRegistrationShutdown(Transport->Registration,
                               QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                               0);
    for (Index = 0; Index < Transport->StartedListenerCount; Index++)
    {
        if (Transport->Listeners[Index].Handle != NULL)
        {
            MsQuicListenerStop(Transport->Listeners[Index].Handle);
        }
    }
    ZpServerQuic_TryCompleteStop(Transport);
}

static const ZP_TRANSPORT_OPERATIONS ZpServerQuicOperations = {
    ZpServerQuic_Start,
    ZpServerQuic_Stop,
    NULL
};

VOID
ZpServerQuic_Configure(
    _Inout_ PZP_SERVER_OBJECT Object)
{
    ULONG Index;

    Object->QuicTransport.Owner = Object;
    for (Index = 0; Index < Object->Config.ListenerCount; Index++)
    {
        if (Object->Config.Listeners[Index].Transport != ZpTransportQuic)
        {
            return;
        }
    }
    if (Object->Config.ListenerCount != 0 && Object->Config.DeploymentCount != 0)
    {
        ZpServer_SetTransport((ZP_SERVER_HANDLE)Object,
                              &ZpServerQuicOperations,
                              &Object->QuicTransport);
    }
}

VOID
ZpServerQuic_Uninitialize(
    _Inout_ PZP_SERVER_QUIC_TRANSPORT Transport)
{
    ULONG Index;

    if (Transport->Registration != NULL)
    {
        MsQuicRegistrationShutdown(Transport->Registration,
                                   QUIC_CONNECTION_SHUTDOWN_FLAG_SILENT,
                                   0);
    }
    for (Index = 0; Index < Transport->Owner->Config.ListenerCount; Index++)
    {
        if (Transport->Listeners[Index].Handle != NULL)
        {
            MsQuicListenerClose(Transport->Listeners[Index].Handle);
            Transport->Listeners[Index].Handle = NULL;
        }
    }
    for (Index = 0; Index < Transport->Owner->Config.DeploymentCount; Index++)
    {
        if (Transport->Configurations[Index] != NULL)
        {
            MsQuicConfigurationClose(Transport->Configurations[Index]);
            Transport->Configurations[Index] = NULL;
        }
    }
    if (Transport->Registration != NULL)
    {
        MsQuicRegistrationClose(Transport->Registration);
        Transport->Registration = NULL;
    }
    for (Index = 0; Index < Transport->Owner->Config.DeploymentCount; Index++)
    {
        Mem_Free(Transport->ServerNames[Index]);
        Transport->ServerNames[Index] = NULL;
    }
    if (Transport->Initialized)
    {
        KNSoftQuicUninitialize();
        Transport->Initialized = FALSE;
    }
    Transport->Stopping = FALSE;
    Transport->StartedListenerCount = 0;
    Transport->StoppedListenerCount = 0;
    Transport->ActiveConnectionCount = 0;
}
