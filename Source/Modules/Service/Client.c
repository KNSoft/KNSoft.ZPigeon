#include "Client.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#include <Winsvc.h>

static
PWCHAR
ZpService_CopyName(
    _In_ PCZP_STRING_VIEW Name)
{
    PWCHAR Buffer;

    Buffer = Mem_Alloc(((SIZE_T)Name->Length + 1) * sizeof(WCHAR));
    if (Buffer != NULL)
    {
        RtlCopyMemory(Buffer,
                      Name->Buffer,
                      (SIZE_T)Name->Length * sizeof(WCHAR));
        Buffer[Name->Length] = UNICODE_NULL;
    }
    return Buffer;
}

static
NTSTATUS
ZpService_Enumerate(
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    LPENUM_SERVICE_STATUS_PROCESSW Entries;
    PZP_SERVICE_RECORD Services = NULL;
    SC_HANDLE Manager;
    PBYTE Buffer = NULL, Result = NULL;
    DWORD BytesNeeded = 0, Count = 0, ResumeHandle = 0;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index, Length;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (Manager == NULL)
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    if (!EnumServicesStatusExW(Manager,
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
    }
    Entries = (LPENUM_SERVICE_STATUS_PROCESSW)Buffer;
    Services = Count != 0 ? Mem_Alloc((SIZE_T)Count * sizeof(*Services)) : NULL;
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
    Status = ZpService_EncodeList(Services, Count, NULL, 0, &Length);
    Result = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Result == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpService_EncodeList(Services,
                                      Count,
                                      Result,
                                      Length,
                                      &Length);
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
    if (!NT_SUCCESS(Status))
    {
        if (Result != NULL)
        {
            Mem_Free(Result);
        }
        return Status;
    }
    *Payload = Result;
    *PayloadLength = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpService_Query(
    _In_ PCZP_STRING_VIEW Name,
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    SERVICE_STATUS_PROCESS ServiceStatus;
    LPQUERY_SERVICE_CONFIGW Config = NULL;
    ZP_SERVICE_INFO Info;
    SC_HANDLE Manager = NULL, Service = NULL;
    PWCHAR ServiceName;
    PBYTE Result = NULL;
    DWORD BytesNeeded;
    ULONG Length;
    NTSTATUS Status = STATUS_SUCCESS;

    ServiceName = ZpService_CopyName(Name);
    if (ServiceName == NULL)
    {
        return STATUS_NO_MEMORY;
    }
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
    Info.ServiceNameLength = Name->Length;
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
    Status = ZpService_EncodeInfo(&Info, NULL, 0, &Length);
    Result = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Result == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpService_EncodeInfo(&Info, Result, Length, &Length);
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
    if (!NT_SUCCESS(Status))
    {
        if (Result != NULL)
        {
            Mem_Free(Result);
        }
        return Status;
    }
    *Payload = Result;
    *PayloadLength = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpService_Control(
    _In_ PCZP_STRING_VIEW Name,
    _In_ LOGICAL Start)
{
    SERVICE_STATUS ServiceStatus;
    SC_HANDLE Manager = NULL, Service = NULL;
    PWCHAR ServiceName;
    NTSTATUS Status = STATUS_SUCCESS;

    ServiceName = ZpService_CopyName(Name);
    if (ServiceName == NULL)
    {
        return STATUS_NO_MEMORY;
    }
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
    if (Start ?
            !StartServiceW(Service, 0, NULL) :
            !ControlService(Service, SERVICE_CONTROL_STOP, &ServiceStatus))
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

NTSTATUS
ZpService_Execute(
    _In_ USHORT OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_STRING_VIEW Name;
    NTSTATUS Status;

    if (OperationId == ZP_SERVICE_OPERATION_ENUMERATE)
    {
        return RequestLength == 0 ?
                   ZpService_Enumerate(Response, ResponseLength) :
                   STATUS_INVALID_PARAMETER;
    }
    Status = ZpService_DecodeQuery(Request, RequestLength, &Name);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    switch (OperationId)
    {
        case ZP_SERVICE_OPERATION_QUERY:
            return ZpService_Query(&Name, Response, ResponseLength);

        case ZP_SERVICE_OPERATION_START:
        case ZP_SERVICE_OPERATION_STOP:
            Status = ZpService_Control(&Name,
                                       OperationId == ZP_SERVICE_OPERATION_START);
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
