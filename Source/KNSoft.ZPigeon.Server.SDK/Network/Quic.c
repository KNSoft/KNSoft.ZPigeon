#include "../Server.inl"
#include "../../Network/Authentication.inl"
#include "../../Network/Quic.inl"

#include <KNSoft/ZPigeon/File.h>
#include <KNSoft/ZPigeon/Process.h>
#include <KNSoft/ZPigeon/Service.h>
#include <KNSoft/ZPigeon/System.h>
#include <KNSoft/ZPigeon/Terminal.h>

#include <Bcrypt.h>
#include <stdlib.h>
#include <Winsvc.h>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Bcrypt.lib")

#define ZP_SERVER_FILE_CHANNEL_CHUNK_SIZE 0x00010000UL
#define ZP_SERVER_FILE_WRITE_WINDOW_SIZE 0x00100000UL
#define ZP_SERVER_TERMINAL_CHANNEL_CHUNK_SIZE 0x00010000UL
#define ZP_SERVER_TERMINAL_INPUT_WINDOW_SIZE 0x00001000UL

typedef enum _ZP_SERVER_QUIC_CHANNEL_TYPE
{
    ZpServerQuicChannelFileRead,
    ZpServerQuicChannelFileWrite,
    ZpServerQuicChannelTerminal
} ZP_SERVER_QUIC_CHANNEL_TYPE;

typedef struct _ZP_SERVER_QUIC_CONNECTION
{
    PZP_SERVER_QUIC_TRANSPORT Transport;
    RTL_SRWLOCK RequestLock;
    LIST_ENTRY Requests;
    RTL_SRWLOCK ChannelLock;
    LIST_ENTRY Channels;
    ULONGLONG NextChannelId;
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

typedef struct _ZP_SERVER_QUIC_CHANNEL
{
    LIST_ENTRY ListEntry;
    PZP_SERVER_QUIC_CONNECTION Connection;
    volatile LONG Pending;
    LOGICAL WorkerActive;
    ZP_SERVER_QUIC_CHANNEL_TYPE Type;
    ULONGLONG ChannelId;
    ULONGLONG Credit;
    ULONGLONG ReceiveCredit;
    ULONGLONG RemainingBytes;
    HANDLE File;
    HPCON PseudoConsole;
    HANDLE Input;
    HANDLE Output;
    HANDLE Process;
    ULONG ProcessId;
    PWCHAR FinalPath;
    PWCHAR TemporaryPath;
    ZP_FILE_CREATE_DISPOSITION FileDisposition;
} ZP_SERVER_QUIC_CHANNEL, *PZP_SERVER_QUIC_CHANNEL;

typedef struct _ZP_SERVER_FILE_ENTRY
{
    LIST_ENTRY ListEntry;
    WIN32_FIND_DATAW Data;
} ZP_SERVER_FILE_ENTRY, *PZP_SERVER_FILE_ENTRY;

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
ZpServerQuic_QueryFile(
    _In_ PCZP_STRING_VIEW PathView,
    _Out_ PZP_FILE_INFO Info)
{
    WIN32_FILE_ATTRIBUTE_DATA Data;
    ULARGE_INTEGER Value;
    PWCHAR Path;
    NTSTATUS Status = STATUS_SUCCESS;

    Path = Mem_Alloc(((SIZE_T)PathView->Length + 1) * sizeof(WCHAR));
    if (Path == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlCopyMemory(Path,
                  PathView->Buffer,
                  (SIZE_T)PathView->Length * sizeof(WCHAR));
    Path[PathView->Length] = UNICODE_NULL;
    if (!GetFileAttributesExW(Path, GetFileExInfoStandard, &Data))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    Info->Attributes = Data.dwFileAttributes;
    Value.HighPart = Data.nFileSizeHigh;
    Value.LowPart = Data.nFileSizeLow;
    Info->Size = Value.QuadPart;
    Value.HighPart = Data.ftCreationTime.dwHighDateTime;
    Value.LowPart = Data.ftCreationTime.dwLowDateTime;
    Info->CreationTime = Value.QuadPart;
    Value.HighPart = Data.ftLastAccessTime.dwHighDateTime;
    Value.LowPart = Data.ftLastAccessTime.dwLowDateTime;
    Info->LastAccessTime = Value.QuadPart;
    Value.HighPart = Data.ftLastWriteTime.dwHighDateTime;
    Value.LowPart = Data.ftLastWriteTime.dwLowDateTime;
    Info->LastWriteTime = Value.QuadPart;

Cleanup:
    Mem_Free(Path);
    return Status;
}

static
NTSTATUS
ZpServerQuic_HashFile(
    _In_ PCZP_STRING_VIEW PathView,
    _In_ ZP_FILE_HASH_ALGORITHM Algorithm,
    _In_ volatile LONG* Pending,
    _Out_ PULONGLONG FileSize,
    _Out_writes_bytes_(ZP_FILE_SHA256_SIZE) BYTE* Digest)
{
    BCRYPT_ALG_HANDLE AlgorithmHandle = NULL;
    BCRYPT_HASH_HANDLE HashHandle = NULL;
    PBYTE HashObject = NULL, Buffer = NULL;
    HANDLE File = INVALID_HANDLE_VALUE;
    LARGE_INTEGER Size;
    PWCHAR Path = NULL;
    ULONG HashObjectLength, HashLength, ResultLength;
    DWORD BytesRead;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Algorithm != ZpFileHashSha256)
    {
        return STATUS_NOT_SUPPORTED;
    }
    Path = Mem_Alloc(((SIZE_T)PathView->Length + 1) * sizeof(WCHAR));
    if (Path == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlCopyMemory(Path,
                  PathView->Buffer,
                  (SIZE_T)PathView->Length * sizeof(WCHAR));
    Path[PathView->Length] = UNICODE_NULL;
    File = CreateFileW(Path,
                       GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                       NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    if (!GetFileSizeEx(File, &Size))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    Status = BCryptOpenAlgorithmProvider(&AlgorithmHandle,
                                         BCRYPT_SHA256_ALGORITHM,
                                         NULL,
                                         0);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    Status = BCryptGetProperty(AlgorithmHandle,
                               BCRYPT_OBJECT_LENGTH,
                               (PBYTE)&HashObjectLength,
                               sizeof(HashObjectLength),
                               &ResultLength,
                               0);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    Status = BCryptGetProperty(AlgorithmHandle,
                               BCRYPT_HASH_LENGTH,
                               (PBYTE)&HashLength,
                               sizeof(HashLength),
                               &ResultLength,
                               0);
    if (!NT_SUCCESS(Status) || HashLength != ZP_FILE_SHA256_SIZE)
    {
        Status = NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
        goto Cleanup;
    }
    HashObject = Mem_Alloc(HashObjectLength);
    Buffer = Mem_Alloc(ZP_SERVER_FILE_CHANNEL_CHUNK_SIZE);
    if (HashObject == NULL || Buffer == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    Status = BCryptCreateHash(AlgorithmHandle,
                              &HashHandle,
                              HashObject,
                              HashObjectLength,
                              NULL,
                              0,
                              0);
    while (NT_SUCCESS(Status))
    {
        if (!InterlockedCompareExchange(Pending, TRUE, TRUE))
        {
            Status = STATUS_CANCELLED;
            break;
        }
        if (!ReadFile(File,
                      Buffer,
                      ZP_SERVER_FILE_CHANNEL_CHUNK_SIZE,
                      &BytesRead,
                      NULL))
        {
            Status = NTSTATUS_FROM_WIN32(GetLastError());
            break;
        }
        if (BytesRead == 0)
        {
            break;
        }
        Status = BCryptHashData(HashHandle, Buffer, BytesRead, 0);
    }
    if (NT_SUCCESS(Status))
    {
        Status = BCryptFinishHash(HashHandle,
                                  Digest,
                                  ZP_FILE_SHA256_SIZE,
                                  0);
    }
    if (NT_SUCCESS(Status))
    {
        *FileSize = (ULONGLONG)Size.QuadPart;
    }

Cleanup:
    if (HashHandle != NULL)
    {
        BCryptDestroyHash(HashHandle);
    }
    if (AlgorithmHandle != NULL)
    {
        BCryptCloseAlgorithmProvider(AlgorithmHandle, 0);
    }
    if (File != INVALID_HANDLE_VALUE)
    {
        CloseHandle(File);
    }
    if (Buffer != NULL)
    {
        Mem_Free(Buffer);
    }
    if (HashObject != NULL)
    {
        Mem_Free(HashObject);
    }
    Mem_Free(Path);
    return Status;
}

static
VOID
ZpServerQuic_GetFileInfo(
    _In_ const WIN32_FIND_DATAW* Data,
    _Out_ PZP_FILE_INFO Info)
{
    ULARGE_INTEGER Value;

    Info->Attributes = Data->dwFileAttributes;
    Value.HighPart = Data->nFileSizeHigh;
    Value.LowPart = Data->nFileSizeLow;
    Info->Size = Value.QuadPart;
    Value.HighPart = Data->ftCreationTime.dwHighDateTime;
    Value.LowPart = Data->ftCreationTime.dwLowDateTime;
    Info->CreationTime = Value.QuadPart;
    Value.HighPart = Data->ftLastAccessTime.dwHighDateTime;
    Value.LowPart = Data->ftLastAccessTime.dwLowDateTime;
    Info->LastAccessTime = Value.QuadPart;
    Value.HighPart = Data->ftLastWriteTime.dwHighDateTime;
    Value.LowPart = Data->ftLastWriteTime.dwLowDateTime;
    Info->LastWriteTime = Value.QuadPart;
}

static
int
__cdecl
ZpServerQuic_CompareFileEntries(
    _In_ const VOID* Left,
    _In_ const VOID* Right)
{
    const PZP_SERVER_FILE_ENTRY* LeftEntry = Left;
    const PZP_SERVER_FILE_ENTRY* RightEntry = Right;
    int Result;

    Result = CompareStringOrdinal((*LeftEntry)->Data.cFileName,
                                  -1,
                                  (*RightEntry)->Data.cFileName,
                                  -1,
                                  TRUE);
    if (Result == CSTR_EQUAL)
    {
        Result = CompareStringOrdinal((*LeftEntry)->Data.cFileName,
                                      -1,
                                      (*RightEntry)->Data.cFileName,
                                      -1,
                                      FALSE);
    }
    return Result == CSTR_LESS_THAN ? -1 : Result == CSTR_GREATER_THAN ? 1 : 0;
}

static
NTSTATUS
ZpServerQuic_EnumerateFiles(
    _In_ PCZP_STRING_VIEW PathView,
    _In_opt_ PCZP_STRING_VIEW Cursor,
    _In_ ULONG MaxEntries,
    _In_ LOGICAL Paged,
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    LIST_ENTRY Entries;
    WIN32_FIND_DATAW Data;
    PZP_SERVER_FILE_ENTRY Entry;
    PZP_SERVER_FILE_ENTRY* SortedEntries = NULL;
    PZP_FILE_RECORD Files = NULL;
    PLIST_ENTRY ListEntry;
    HANDLE FindHandle = INVALID_HANDLE_VALUE;
    PWCHAR SearchPath = NULL;
    SIZE_T SearchLength;
    ULONG Count = 0, Index = 0, StartIndex = 0, PageCount;
    DWORD Error;
    int CompareResult;
    NTSTATUS Status = STATUS_SUCCESS;

    *Payload = NULL;
    *PayloadLength = 0;
    InitializeListHead(&Entries);
    SearchLength = (SIZE_T)PathView->Length + 3;
    SearchPath = Mem_Alloc(SearchLength * sizeof(WCHAR));
    if (SearchPath == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlCopyMemory(SearchPath,
                  PathView->Buffer,
                  (SIZE_T)PathView->Length * sizeof(WCHAR));
    Index = PathView->Length;
    if (SearchPath[Index - 1] != L'\\' && SearchPath[Index - 1] != L'/')
    {
        SearchPath[Index++] = L'\\';
    }
    SearchPath[Index++] = L'*';
    SearchPath[Index] = UNICODE_NULL;

    FindHandle = FindFirstFileW(SearchPath, &Data);
    if (FindHandle == INVALID_HANDLE_VALUE)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    do
    {
        if ((Data.cFileName[0] == L'.' && Data.cFileName[1] == UNICODE_NULL) ||
            (Data.cFileName[0] == L'.' && Data.cFileName[1] == L'.' &&
             Data.cFileName[2] == UNICODE_NULL))
        {
            continue;
        }
        if (Count == MAXULONG)
        {
            Status = STATUS_BUFFER_OVERFLOW;
            goto Cleanup;
        }
        Entry = Mem_Alloc(sizeof(*Entry));
        if (Entry == NULL)
        {
            Status = STATUS_NO_MEMORY;
            goto Cleanup;
        }
        Entry->Data = Data;
        InsertTailList(&Entries, &Entry->ListEntry);
        Count++;
    } while (FindNextFileW(FindHandle, &Data));
    Error = GetLastError();
    if (Error != ERROR_NO_MORE_FILES)
    {
        Status = NTSTATUS_FROM_WIN32(Error);
        goto Cleanup;
    }

    SortedEntries = Count != 0 ?
                        Mem_Alloc((SIZE_T)Count * sizeof(*SortedEntries)) :
                        NULL;
    if (Count != 0 && SortedEntries == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    Index = 0;
    for (ListEntry = Entries.Flink;
         ListEntry != &Entries;
         ListEntry = ListEntry->Flink)
    {
        Entry = CONTAINING_RECORD(ListEntry,
                                  ZP_SERVER_FILE_ENTRY,
                                  ListEntry);
        SortedEntries[Index++] = Entry;
    }
    qsort(SortedEntries,
          Count,
          sizeof(*SortedEntries),
          ZpServerQuic_CompareFileEntries);
    if (Paged && Cursor->Length != 0)
    {
        while (StartIndex < Count)
        {
            CompareResult = CompareStringOrdinal(
                SortedEntries[StartIndex]->Data.cFileName,
                -1,
                (PCWCH)Cursor->Buffer,
                Cursor->Length,
                TRUE);
            if (CompareResult == CSTR_EQUAL)
            {
                CompareResult = CompareStringOrdinal(
                    SortedEntries[StartIndex]->Data.cFileName,
                    -1,
                    (PCWCH)Cursor->Buffer,
                    Cursor->Length,
                    FALSE);
            }
            if (CompareResult == CSTR_GREATER_THAN)
            {
                break;
            }
            StartIndex++;
        }
    }
    PageCount = min(MaxEntries, Count - StartIndex);
    Files = PageCount != 0 ?
                Mem_Alloc((SIZE_T)PageCount * sizeof(*Files)) :
                NULL;
    if (PageCount != 0 && Files == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    for (Index = 0; Index < PageCount; Index++)
    {
        Entry = SortedEntries[StartIndex + Index];
        ZpServerQuic_GetFileInfo(&Entry->Data, &Files[Index].Info);
        Files[Index].Name = Entry->Data.cFileName;
        Files[Index].NameLength = (ULONG)wcslen(Entry->Data.cFileName);
    }
    Status = Paged ?
                 ZpFile_EncodePage(
                     Files,
                     PageCount,
                     StartIndex + PageCount < Count ?
                         Files[PageCount - 1].Name :
                         NULL,
                     StartIndex + PageCount < Count ?
                         Files[PageCount - 1].NameLength :
                         0,
                     NULL,
                     0,
                     PayloadLength) :
                 ZpFile_EncodeList(Files,
                                   PageCount,
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
        Status = Paged ?
                     ZpFile_EncodePage(
                         Files,
                         PageCount,
                         StartIndex + PageCount < Count ?
                             Files[PageCount - 1].Name :
                             NULL,
                         StartIndex + PageCount < Count ?
                             Files[PageCount - 1].NameLength :
                             0,
                         *Payload,
                         *PayloadLength,
                         PayloadLength) :
                     ZpFile_EncodeList(Files,
                                       PageCount,
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
    if (Files != NULL)
    {
        Mem_Free(Files);
    }
    if (SortedEntries != NULL)
    {
        Mem_Free(SortedEntries);
    }
    while (!IsListEmpty(&Entries))
    {
        Entry = CONTAINING_RECORD(RemoveHeadList(&Entries),
                                  ZP_SERVER_FILE_ENTRY,
                                  ListEntry);
        Mem_Free(Entry);
    }
    if (FindHandle != INVALID_HANDLE_VALUE)
    {
        FindClose(FindHandle);
    }
    if (SearchPath != NULL)
    {
        Mem_Free(SearchPath);
    }
    return Status;
}

static
NTSTATUS
ZpServerQuic_SendChannelClose(
    _Inout_ PZP_SERVER_QUIC_CHANNEL Channel,
    _In_ NTSTATUS CloseStatus)
{
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Channel->Connection;
    BYTE Body[sizeof(ULONGLONG) + sizeof(ULONG)];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelClose(Channel->ChannelId,
                                          CloseStatus,
                                          Body,
                                          sizeof(Body),
                                          &BodyLength);
    if (NT_SUCCESS(Status))
    {
        Status = ZpQuic_SendFrame(QuicConnection->Stream,
                                  &QuicConnection->ProtocolConnection,
                                  ZpMessageChannelClose,
                                  Body,
                                  BodyLength);
    }
    return Status;
}

static
VOID
ZpServerQuic_DestroyChannel(
    _Inout_ PZP_SERVER_QUIC_CHANNEL Channel)
{
    if (Channel->Type == ZpServerQuicChannelTerminal)
    {
        if (Channel->Process != NULL &&
            WaitForSingleObject(Channel->Process, 0) == WAIT_TIMEOUT)
        {
            TerminateProcess(Channel->Process, STATUS_CANCELLED);
            WaitForSingleObject(Channel->Process, 1000);
        }
        if (Channel->PseudoConsole != NULL)
        {
            ClosePseudoConsole(Channel->PseudoConsole);
        }
        if (Channel->Input != NULL)
        {
            CloseHandle(Channel->Input);
        }
        if (Channel->Output != NULL)
        {
            CloseHandle(Channel->Output);
        }
        if (Channel->Process != NULL)
        {
            CloseHandle(Channel->Process);
        }
    }
    if (Channel->File != NULL && Channel->File != INVALID_HANDLE_VALUE)
    {
        CloseHandle(Channel->File);
    }
    if (Channel->TemporaryPath != NULL)
    {
        DeleteFileW(Channel->TemporaryPath);
        Mem_Free(Channel->TemporaryPath);
    }
    if (Channel->FinalPath != NULL)
    {
        Mem_Free(Channel->FinalPath);
    }
    Mem_Free(Channel);
}

static
PZP_SERVER_QUIC_CHANNEL
ZpServerQuic_FindChannel(
    _In_ PZP_SERVER_QUIC_CONNECTION QuicConnection,
    _In_ ULONGLONG ChannelId)
{
    PZP_SERVER_QUIC_CHANNEL Channel;
    PLIST_ENTRY Entry;

    for (Entry = QuicConnection->Channels.Flink;
         Entry != &QuicConnection->Channels;
         Entry = Entry->Flink)
    {
        Channel = CONTAINING_RECORD(Entry,
                                    ZP_SERVER_QUIC_CHANNEL,
                                    ListEntry);
        if (Channel->ChannelId == ChannelId)
        {
            return Channel;
        }
    }
    return NULL;
}

static
NTSTATUS
ZpServerQuic_CreateFileChannel(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection,
    _In_ PCZP_STRING_VIEW PathView,
    _In_ ULONGLONG Offset,
    _Out_ PZP_SERVER_QUIC_CHANNEL* Channel,
    _Out_ PULONGLONG FileSize)
{
    PZP_SERVER_QUIC_CHANNEL ChannelObject;
    LARGE_INTEGER Size, Position;
    PWCHAR Path;
    NTSTATUS Status = STATUS_SUCCESS;

    Path = Mem_Alloc(((SIZE_T)PathView->Length + 1) * sizeof(WCHAR));
    ChannelObject = Mem_Alloc(sizeof(*ChannelObject));
    if (ChannelObject != NULL)
    {
        RtlZeroMemory(ChannelObject, sizeof(*ChannelObject));
        ChannelObject->Type = ZpServerQuicChannelFileRead;
        ChannelObject->File = INVALID_HANDLE_VALUE;
    }
    if (Path == NULL || ChannelObject == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    RtlCopyMemory(Path,
                  PathView->Buffer,
                  (SIZE_T)PathView->Length * sizeof(WCHAR));
    Path[PathView->Length] = UNICODE_NULL;
    ChannelObject->Connection = QuicConnection;
    ChannelObject->File = CreateFileW(Path,
                                      GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      NULL,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                      NULL);
    if (ChannelObject->File == INVALID_HANDLE_VALUE)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    if (!GetFileSizeEx(ChannelObject->File, &Size))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    if (Offset > (ULONGLONG)Size.QuadPart)
    {
        Status = STATUS_END_OF_FILE;
        goto Cleanup;
    }
    Position.QuadPart = Offset;
    if (!SetFilePointerEx(ChannelObject->File,
                          Position,
                          NULL,
                          FILE_BEGIN))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
    ChannelObject->ChannelId = QuicConnection->NextChannelId;
    QuicConnection->NextChannelId += 2;
    if (QuicConnection->NextChannelId == 0)
    {
        QuicConnection->NextChannelId = 2;
    }
    RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
    ChannelObject->RemainingBytes = (ULONGLONG)Size.QuadPart - Offset;
    *FileSize = (ULONGLONG)Size.QuadPart;
    *Channel = ChannelObject;
    ChannelObject = NULL;

Cleanup:
    if (ChannelObject != NULL)
    {
        ZpServerQuic_DestroyChannel(ChannelObject);
    }
    if (Path != NULL)
    {
        Mem_Free(Path);
    }
    return Status;
}

static
NTSTATUS
ZpServerQuic_CreateFileWriteChannel(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection,
    _In_ PCZP_STRING_VIEW PathView,
    _In_ ULONGLONG FileSize,
    _In_ ZP_FILE_CREATE_DISPOSITION Disposition,
    _Out_ PZP_SERVER_QUIC_CHANNEL* Channel)
{
    PZP_SERVER_QUIC_CHANNEL ChannelObject;
    SIZE_T PathCapacity;
    ULONGLONG RandomValue;
    ULONG Attempt;
    NTSTATUS Status = STATUS_SUCCESS;

    ChannelObject = Mem_Alloc(sizeof(*ChannelObject));
    if (ChannelObject == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(ChannelObject, sizeof(*ChannelObject));
    ChannelObject->Type = ZpServerQuicChannelFileWrite;
    ChannelObject->File = INVALID_HANDLE_VALUE;
    PathCapacity = (SIZE_T)PathView->Length + 32;
    ChannelObject->FinalPath = Mem_Alloc(
        ((SIZE_T)PathView->Length + 1) * sizeof(WCHAR));
    ChannelObject->TemporaryPath = Mem_Alloc(PathCapacity * sizeof(WCHAR));
    if (ChannelObject->FinalPath == NULL ||
        ChannelObject->TemporaryPath == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    RtlCopyMemory(ChannelObject->FinalPath,
                  PathView->Buffer,
                  (SIZE_T)PathView->Length * sizeof(WCHAR));
    ChannelObject->FinalPath[PathView->Length] = UNICODE_NULL;
    for (Attempt = 0; Attempt < 16; Attempt++)
    {
        Status = BCryptGenRandom(NULL,
                                 (PBYTE)&RandomValue,
                                 sizeof(RandomValue),
                                 BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
        _snwprintf_s(ChannelObject->TemporaryPath,
                     PathCapacity,
                     _TRUNCATE,
                     L"%s.%016llX.zpigeon.tmp",
                     ChannelObject->FinalPath,
                     RandomValue);
        ChannelObject->File = CreateFileW(ChannelObject->TemporaryPath,
                                           GENERIC_WRITE,
                                           FILE_SHARE_READ,
                                           NULL,
                                           CREATE_NEW,
                                           FILE_ATTRIBUTE_TEMPORARY |
                                               FILE_FLAG_SEQUENTIAL_SCAN,
                                           NULL);
        if (ChannelObject->File != INVALID_HANDLE_VALUE ||
            GetLastError() != ERROR_FILE_EXISTS)
        {
            break;
        }
    }
    if (ChannelObject->File == INVALID_HANDLE_VALUE)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    ChannelObject->Connection = QuicConnection;
    ChannelObject->RemainingBytes = FileSize;
    ChannelObject->FileDisposition = Disposition;
    RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
    ChannelObject->ChannelId = QuicConnection->NextChannelId;
    QuicConnection->NextChannelId += 2;
    if (QuicConnection->NextChannelId == 0)
    {
        QuicConnection->NextChannelId = 2;
    }
    RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
    *Channel = ChannelObject;
    return STATUS_SUCCESS;

Cleanup:
    ZpServerQuic_DestroyChannel(ChannelObject);
    return Status;
}

static
NTSTATUS
ZpServerQuic_CommitFileWriteChannel(
    _Inout_ PZP_SERVER_QUIC_CHANNEL Channel)
{
    DWORD Flags = MOVEFILE_WRITE_THROUGH;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!FlushFileBuffers(Channel->File))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
    }
    CloseHandle(Channel->File);
    Channel->File = INVALID_HANDLE_VALUE;
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Channel->FileDisposition == ZpFileCreateAlways)
    {
        Flags |= MOVEFILE_REPLACE_EXISTING;
    }
    if (!MoveFileExW(Channel->TemporaryPath, Channel->FinalPath, Flags))
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    Mem_Free(Channel->TemporaryPath);
    Channel->TemporaryPath = NULL;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpServerQuic_SendChannelWindow(
    _Inout_ PZP_SERVER_QUIC_CHANNEL Channel,
    _In_ ULONG CreditBytes)
{
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Channel->Connection;
    BYTE Body[sizeof(ULONGLONG) + sizeof(ULONG)];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelWindow(Channel->ChannelId,
                                           CreditBytes,
                                           Body,
                                           sizeof(Body),
                                           &BodyLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
    if (!Channel->Pending ||
        MAXULONGLONG - Channel->ReceiveCredit < CreditBytes)
    {
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Channel->ReceiveCredit += CreditBytes;
    Status = ZpQuic_SendFrame(QuicConnection->Stream,
                              &QuicConnection->ProtocolConnection,
                              ZpMessageChannelWindow,
                              Body,
                              BodyLength);
    if (!NT_SUCCESS(Status))
    {
        Channel->ReceiveCredit -= CreditBytes;
    }
    RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
    return Status;
}

static
NTSTATUS
ZpServerQuic_CreateTerminalChannel(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection,
    _In_ const ZP_TERMINAL_CREATE_VIEW* Create,
    _Out_ PZP_SERVER_QUIC_CHANNEL* Channel)
{
    STARTUPINFOEXW StartupInfo = { 0 };
    PROCESS_INFORMATION ProcessInfo = { 0 };
    PZP_SERVER_QUIC_CHANNEL ChannelObject = NULL;
    PPROC_THREAD_ATTRIBUTE_LIST AttributeList = NULL;
    HANDLE InputRead = NULL, InputWrite = NULL;
    HANDLE OutputRead = NULL, OutputWrite = NULL;
    HPCON PseudoConsole = NULL;
    PWCHAR CommandLine = NULL, WorkingDirectory = NULL;
    SIZE_T AttributeListSize = 0;
    COORD Size;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL AttributeListInitialized = FALSE;

    if (Create->Columns > MAXSHORT || Create->Rows > MAXSHORT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ChannelObject = Mem_Alloc(sizeof(*ChannelObject));
    CommandLine = Mem_Alloc(((SIZE_T)Create->CommandLine.Length + 1) *
                            sizeof(WCHAR));
    if (Create->WorkingDirectory.Length != 0)
    {
        WorkingDirectory = Mem_Alloc(
            ((SIZE_T)Create->WorkingDirectory.Length + 1) * sizeof(WCHAR));
    }
    if (ChannelObject == NULL ||
        CommandLine == NULL ||
        (Create->WorkingDirectory.Length != 0 && WorkingDirectory == NULL))
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    RtlZeroMemory(ChannelObject, sizeof(*ChannelObject));
    ChannelObject->Type = ZpServerQuicChannelTerminal;
    RtlCopyMemory(CommandLine,
                  Create->CommandLine.Buffer,
                  (SIZE_T)Create->CommandLine.Length * sizeof(WCHAR));
    CommandLine[Create->CommandLine.Length] = UNICODE_NULL;
    if (WorkingDirectory != NULL)
    {
        RtlCopyMemory(WorkingDirectory,
                      Create->WorkingDirectory.Buffer,
                      (SIZE_T)Create->WorkingDirectory.Length * sizeof(WCHAR));
        WorkingDirectory[Create->WorkingDirectory.Length] = UNICODE_NULL;
    }
    if (!CreatePipe(&InputRead, &InputWrite, NULL, 0) ||
        !CreatePipe(&OutputRead, &OutputWrite, NULL, 0))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    Size.X = (SHORT)Create->Columns;
    Size.Y = (SHORT)Create->Rows;
    Result = CreatePseudoConsole(Size,
                                 InputRead,
                                 OutputWrite,
                                 0,
                                 &PseudoConsole);
    if (FAILED(Result))
    {
        Status = NTSTATUS_FROM_WIN32(HRESULT_CODE(Result));
        goto Cleanup;
    }
    InitializeProcThreadAttributeList(NULL,
                                      1,
                                      0,
                                      &AttributeListSize);
    AttributeList = Mem_Alloc(AttributeListSize);
    if (AttributeList == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    if (!InitializeProcThreadAttributeList(AttributeList,
                                           1,
                                           0,
                                           &AttributeListSize))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    AttributeListInitialized = TRUE;
    if (!UpdateProcThreadAttribute(AttributeList,
                                   0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   PseudoConsole,
                                   sizeof(PseudoConsole),
                                   NULL,
                                   NULL))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    StartupInfo.StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.StartupInfo.hStdInput = NULL;
    StartupInfo.StartupInfo.hStdOutput = NULL;
    StartupInfo.StartupInfo.hStdError = NULL;
    StartupInfo.lpAttributeList = AttributeList;
    if (!CreateProcessW(NULL,
                        CommandLine,
                        NULL,
                        NULL,
                        FALSE,
                        EXTENDED_STARTUPINFO_PRESENT |
                            CREATE_UNICODE_ENVIRONMENT,
                        NULL,
                        WorkingDirectory,
                        &StartupInfo.StartupInfo,
                        &ProcessInfo))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    CloseHandle(InputRead);
    InputRead = NULL;
    CloseHandle(OutputWrite);
    OutputWrite = NULL;
    CloseHandle(ProcessInfo.hThread);
    ProcessInfo.hThread = NULL;
    ChannelObject->Connection = QuicConnection;
    ChannelObject->PseudoConsole = PseudoConsole;
    ChannelObject->Input = InputWrite;
    ChannelObject->Output = OutputRead;
    ChannelObject->Process = ProcessInfo.hProcess;
    ChannelObject->ProcessId = ProcessInfo.dwProcessId;
    PseudoConsole = NULL;
    InputWrite = NULL;
    OutputRead = NULL;
    ProcessInfo.hProcess = NULL;
    RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
    ChannelObject->ChannelId = QuicConnection->NextChannelId;
    QuicConnection->NextChannelId += 2;
    if (QuicConnection->NextChannelId == 0)
    {
        QuicConnection->NextChannelId = 2;
    }
    RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
    *Channel = ChannelObject;
    ChannelObject = NULL;

Cleanup:
    if (AttributeListInitialized)
    {
        DeleteProcThreadAttributeList(AttributeList);
    }
    if (AttributeList != NULL)
    {
        Mem_Free(AttributeList);
    }
    if (ProcessInfo.hThread != NULL)
    {
        CloseHandle(ProcessInfo.hThread);
    }
    if (ProcessInfo.hProcess != NULL)
    {
        TerminateProcess(ProcessInfo.hProcess, STATUS_CANCELLED);
        CloseHandle(ProcessInfo.hProcess);
    }
    if (PseudoConsole != NULL)
    {
        ClosePseudoConsole(PseudoConsole);
    }
    if (InputRead != NULL)
    {
        CloseHandle(InputRead);
    }
    if (InputWrite != NULL)
    {
        CloseHandle(InputWrite);
    }
    if (OutputRead != NULL)
    {
        CloseHandle(OutputRead);
    }
    if (OutputWrite != NULL)
    {
        CloseHandle(OutputWrite);
    }
    if (WorkingDirectory != NULL)
    {
        Mem_Free(WorkingDirectory);
    }
    if (CommandLine != NULL)
    {
        Mem_Free(CommandLine);
    }
    if (ChannelObject != NULL)
    {
        ZpServerQuic_DestroyChannel(ChannelObject);
    }
    return Status;
}

static
NTSTATUS
ZpServerQuic_ActivateChannel(
    _Inout_ PZP_SERVER_QUIC_CHANNEL Channel)
{
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Channel->Connection;

    RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
    if (QuicConnection->Closing ||
        ZpServerQuic_FindChannel(QuicConnection, Channel->ChannelId) != NULL)
    {
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
        return STATUS_CONNECTION_DISCONNECTED;
    }
    Channel->Pending = TRUE;
    InsertTailList(&QuicConnection->Channels, &Channel->ListEntry);
    RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
    return STATUS_SUCCESS;
}

static
VOID
ZpServerQuic_CompleteChannelWorker(
    _Inout_ PZP_SERVER_QUIC_CHANNEL Channel,
    _In_ NTSTATUS Status,
    _In_ LOGICAL SendClose)
{
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Channel->Connection;
    LOGICAL WasPending;

    RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
    WasPending = InterlockedExchange(&Channel->Pending, FALSE);
    if (WasPending)
    {
        RemoveEntryList(&Channel->ListEntry);
    }
    Channel->WorkerActive = FALSE;
    RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
    if (WasPending && SendClose && !QuicConnection->Closing)
    {
        ZpServerQuic_SendChannelClose(Channel, Status);
    }
    ZpServerQuic_DestroyChannel(Channel);
    ZpServerQuic_ReleaseConnection(QuicConnection);
}

static
VOID
CALLBACK
ZpServerQuic_ChannelCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_QUIC_CHANNEL Channel = Context;
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Channel->Connection;
    PBYTE Body;
    ULONG ReadLength, BytesRead, BodyLength;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Instance);
    Body = Mem_Alloc(sizeof(ULONGLONG) + ZP_SERVER_FILE_CHANNEL_CHUNK_SIZE);
    if (Body == NULL)
    {
        ZpServerQuic_CompleteChannelWorker(Channel,
                                           STATUS_NO_MEMORY,
                                           TRUE);
        return;
    }
    for (;;)
    {
        RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
        if (!Channel->Pending)
        {
            Channel->WorkerActive = FALSE;
            RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
            break;
        }
        if (Channel->RemainingBytes == 0)
        {
            RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
            Mem_Free(Body);
            ZpServerQuic_CompleteChannelWorker(Channel,
                                               STATUS_SUCCESS,
                                               TRUE);
            return;
        }
        if (Channel->Credit == 0)
        {
            Channel->WorkerActive = FALSE;
            RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
            Mem_Free(Body);
            ZpServerQuic_ReleaseConnection(QuicConnection);
            return;
        }
        ReadLength = (ULONG)min(min(Channel->Credit,
                                    Channel->RemainingBytes),
                                ZP_SERVER_FILE_CHANNEL_CHUNK_SIZE);
        Channel->Credit -= ReadLength;
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);

        if (!ReadFile(Channel->File,
                      Body + sizeof(ULONGLONG),
                      ReadLength,
                      &BytesRead,
                      NULL))
        {
            Status = NTSTATUS_FROM_WIN32(GetLastError());
            Mem_Free(Body);
            ZpServerQuic_CompleteChannelWorker(Channel, Status, TRUE);
            return;
        }
        if (BytesRead == 0)
        {
            Mem_Free(Body);
            ZpServerQuic_CompleteChannelWorker(Channel,
                                               STATUS_END_OF_FILE,
                                               TRUE);
            return;
        }
        Status = ZpMessage_EncodeChannelData(Channel->ChannelId,
                                             Body + sizeof(ULONGLONG),
                                             BytesRead,
                                             Body,
                                             sizeof(ULONGLONG) +
                                                 ZP_SERVER_FILE_CHANNEL_CHUNK_SIZE,
                                             &BodyLength);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(Body);
            ZpServerQuic_CompleteChannelWorker(Channel, Status, TRUE);
            return;
        }

        RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
        if (!Channel->Pending)
        {
            Channel->WorkerActive = FALSE;
            RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
            break;
        }
        Channel->Credit += ReadLength - BytesRead;
        Channel->RemainingBytes -= BytesRead;
        Status = ZpQuic_SendFrame(QuicConnection->Stream,
                                  &QuicConnection->ProtocolConnection,
                                  ZpMessageChannelData,
                                  Body,
                                  BodyLength);
        if (!NT_SUCCESS(Status))
        {
            InterlockedExchange(&Channel->Pending, FALSE);
            RemoveEntryList(&Channel->ListEntry);
            Channel->WorkerActive = FALSE;
        }
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(Body);
            ZpServerQuic_DestroyChannel(Channel);
            InterlockedExchange((volatile LONG*)&QuicConnection->ShutdownStatus,
                                Status);
            MsQuicConnectionShutdown(QuicConnection->Connection,
                                     QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                     0);
            ZpServerQuic_ReleaseConnection(QuicConnection);
            return;
        }
    }
    Mem_Free(Body);
    ZpServerQuic_DestroyChannel(Channel);
    ZpServerQuic_ReleaseConnection(QuicConnection);
}

static
VOID
CALLBACK
ZpServerQuic_ClosePseudoConsoleCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_QUIC_CHANNEL Channel = Context;
    HPCON PseudoConsole = (HPCON)InterlockedCompareExchangePointer(
        (PVOID volatile*)&Channel->PseudoConsole,
        NULL,
        NULL);

    UNREFERENCED_PARAMETER(Instance);
    ClosePseudoConsole(PseudoConsole);
    InterlockedExchangePointer((PVOID volatile*)&Channel->PseudoConsole,
                               NULL);
}

static
VOID
CALLBACK
ZpServerQuic_TerminalChannelCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_QUIC_CHANNEL Channel = Context;
    PZP_SERVER_QUIC_CONNECTION QuicConnection = Channel->Connection;
    PBYTE Body;
    DWORD Available, BytesRead, ExitCode;
    ULONG ReadLength, BodyLength;
    NTSTATUS Status;
    LOGICAL ProcessExited = FALSE;
    LOGICAL PseudoConsoleCloseQueued = FALSE;

    CallbackMayRunLong(Instance);
    Body = Mem_Alloc(sizeof(ULONGLONG) +
                     ZP_SERVER_TERMINAL_CHANNEL_CHUNK_SIZE);
    if (Body == NULL)
    {
        ZpServerQuic_CompleteChannelWorker(Channel,
                                           STATUS_NO_MEMORY,
                                           TRUE);
        return;
    }
    for (;;)
    {
        RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
        if (!Channel->Pending)
        {
            if (PseudoConsoleCloseQueued &&
                InterlockedCompareExchangePointer(
                    (PVOID volatile*)&Channel->PseudoConsole,
                    NULL,
                    NULL) != NULL)
            {
                RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
                Sleep(1);
                continue;
            }
            Channel->WorkerActive = FALSE;
            RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
            break;
        }
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);

        if (!ProcessExited &&
            WaitForSingleObject(Channel->Process, 0) == WAIT_OBJECT_0)
        {
            ProcessExited = TRUE;
        }
        Available = 0;
        if (!PeekNamedPipe(Channel->Output,
                           NULL,
                           0,
                           NULL,
                           &Available,
                           NULL))
        {
            if (GetLastError() == ERROR_BROKEN_PIPE && ProcessExited)
            {
                Available = 0;
            }
            else
            {
                Status = NTSTATUS_FROM_WIN32(GetLastError());
                Mem_Free(Body);
                ZpServerQuic_CompleteChannelWorker(Channel, Status, TRUE);
                return;
            }
        }
        if (Available == 0)
        {
            if (ProcessExited)
            {
                if (InterlockedCompareExchangePointer(
                        (PVOID volatile*)&Channel->PseudoConsole,
                        NULL,
                        NULL) != NULL)
                {
                    if (!PseudoConsoleCloseQueued)
                    {
                        if (!TrySubmitThreadpoolCallback(
                                ZpServerQuic_ClosePseudoConsoleCallback,
                                Channel,
                                NULL))
                        {
                            Status = STATUS_NO_MEMORY;
                            Mem_Free(Body);
                            ZpServerQuic_CompleteChannelWorker(Channel,
                                                               Status,
                                                               TRUE);
                            return;
                        }
                        PseudoConsoleCloseQueued = TRUE;
                    }
                    Sleep(1);
                    continue;
                }
                if (!GetExitCodeProcess(Channel->Process, &ExitCode))
                {
                    Status = NTSTATUS_FROM_WIN32(GetLastError());
                }
                else
                {
                    Status = (NTSTATUS)ExitCode;
                }
                Mem_Free(Body);
                ZpServerQuic_CompleteChannelWorker(Channel, Status, TRUE);
                return;
            }
            WaitForSingleObject(Channel->Process, 10);
            continue;
        }

        RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
        if (!Channel->Pending)
        {
            Channel->WorkerActive = FALSE;
            RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
            break;
        }
        ReadLength = (ULONG)min(min(Channel->Credit,
                                    (ULONGLONG)Available),
                                ZP_SERVER_TERMINAL_CHANNEL_CHUNK_SIZE);
        if (ReadLength == 0)
        {
            RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
            WaitForSingleObject(Channel->Process, 10);
            continue;
        }
        Channel->Credit -= ReadLength;
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);

        if (!ReadFile(Channel->Output,
                      Body + sizeof(ULONGLONG),
                      ReadLength,
                      &BytesRead,
                      NULL))
        {
            Status = NTSTATUS_FROM_WIN32(GetLastError());
            Mem_Free(Body);
            ZpServerQuic_CompleteChannelWorker(Channel, Status, TRUE);
            return;
        }
        Status = ZpMessage_EncodeChannelData(Channel->ChannelId,
                                             Body + sizeof(ULONGLONG),
                                             BytesRead,
                                             Body,
                                             sizeof(ULONGLONG) +
                                                 ZP_SERVER_TERMINAL_CHANNEL_CHUNK_SIZE,
                                             &BodyLength);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(Body);
            ZpServerQuic_CompleteChannelWorker(Channel, Status, TRUE);
            return;
        }
        RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
        if (!Channel->Pending)
        {
            Channel->WorkerActive = FALSE;
            RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
            break;
        }
        Channel->Credit += ReadLength - BytesRead;
        Status = ZpQuic_SendFrame(QuicConnection->Stream,
                                  &QuicConnection->ProtocolConnection,
                                  ZpMessageChannelData,
                                  Body,
                                  BodyLength);
        if (!NT_SUCCESS(Status))
        {
            InterlockedExchange(&Channel->Pending, FALSE);
            RemoveEntryList(&Channel->ListEntry);
            Channel->WorkerActive = FALSE;
        }
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(Body);
            ZpServerQuic_DestroyChannel(Channel);
            InterlockedExchange((volatile LONG*)&QuicConnection->ShutdownStatus,
                                Status);
            MsQuicConnectionShutdown(QuicConnection->Connection,
                                     QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                     0);
            ZpServerQuic_ReleaseConnection(QuicConnection);
            return;
        }
    }
    Mem_Free(Body);
    ZpServerQuic_DestroyChannel(Channel);
    ZpServerQuic_ReleaseConnection(QuicConnection);
}

static
NTSTATUS
ZpServerQuic_AddChannelWindow(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection,
    _In_ ULONGLONG ChannelId,
    _In_ ULONG CreditBytes)
{
    PZP_SERVER_QUIC_CHANNEL Channel;
    LOGICAL QueueWorker = FALSE;

    RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
    Channel = ZpServerQuic_FindChannel(QuicConnection, ChannelId);
    if (Channel == NULL)
    {
        NTSTATUS Status = (ChannelId & 1) == 0 &&
                          ChannelId < QuicConnection->NextChannelId ?
                              STATUS_SUCCESS :
                              STATUS_PROTOCOL_UNREACHABLE;

        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
        return Status;
    }
    if (Channel->Type == ZpServerQuicChannelFileWrite)
    {
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    if (MAXULONGLONG - Channel->Credit < CreditBytes)
    {
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->Credit += CreditBytes;
    if (!Channel->WorkerActive)
    {
        Channel->WorkerActive = TRUE;
        InterlockedIncrement(&QuicConnection->ReferenceCount);
        QueueWorker = TRUE;
    }
    RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
    if (QueueWorker &&
        !TrySubmitThreadpoolCallback(
            Channel->Type == ZpServerQuicChannelTerminal ?
                ZpServerQuic_TerminalChannelCallback :
                ZpServerQuic_ChannelCallback,
            Channel,
            NULL))
    {
        ZpServerQuic_CompleteChannelWorker(Channel,
                                           STATUS_NO_MEMORY,
                                           TRUE);
        return STATUS_SUCCESS;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpServerQuic_CloseRemoteChannel(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection,
    _In_ ULONGLONG ChannelId)
{
    PZP_SERVER_QUIC_CHANNEL Channel;
    LOGICAL Destroy;

    RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
    Channel = ZpServerQuic_FindChannel(QuicConnection, ChannelId);
    if (Channel == NULL)
    {
        NTSTATUS Status = (ChannelId & 1) == 0 &&
                          ChannelId < QuicConnection->NextChannelId ?
                              STATUS_SUCCESS :
                              STATUS_PROTOCOL_UNREACHABLE;

        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
        return Status;
    }
    if (!InterlockedExchange(&Channel->Pending, FALSE))
    {
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    RemoveEntryList(&Channel->ListEntry);
    Destroy = !Channel->WorkerActive;
    RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
    if (Destroy)
    {
        ZpServerQuic_DestroyChannel(Channel);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpServerQuic_ReceiveChannelData(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message)
{
    PZP_SERVER_QUIC_CHANNEL Channel;
    BYTE Body[sizeof(ULONGLONG) + sizeof(ULONG)];
    ULONG BodyLength, BytesWritten, CreditBytes;
    NTSTATUS Status;
    LOGICAL Destroy = FALSE;
    LOGICAL WriteSucceeded;

    Status = ZpMessage_EncodeChannelWindow(Message->ChannelId,
                                           Message->Data.Length,
                                           Body,
                                           sizeof(Body),
                                           &BodyLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
    Channel = ZpServerQuic_FindChannel(QuicConnection, Message->ChannelId);
    if (Channel != NULL && Channel->Type == ZpServerQuicChannelFileWrite)
    {
        if (Message->Data.Length > Channel->ReceiveCredit ||
            Message->Data.Length > Channel->RemainingBytes)
        {
            RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
            return STATUS_PROTOCOL_UNREACHABLE;
        }
        Channel->ReceiveCredit -= Message->Data.Length;
        WriteSucceeded = WriteFile(Channel->File,
                                   Message->Data.Buffer,
                                   Message->Data.Length,
                                   &BytesWritten,
                                   NULL);
        if (!WriteSucceeded || BytesWritten != Message->Data.Length)
        {
            Status = WriteSucceeded ?
                         STATUS_UNSUCCESSFUL :
                         NTSTATUS_FROM_WIN32(GetLastError());
        }
        else
        {
            Channel->RemainingBytes -= BytesWritten;
            Status = Channel->RemainingBytes == 0 ?
                         ZpServerQuic_CommitFileWriteChannel(Channel) :
                         STATUS_SUCCESS;
        }
        if (!NT_SUCCESS(Status) || Channel->RemainingBytes == 0)
        {
            InterlockedExchange(&Channel->Pending, FALSE);
            RemoveEntryList(&Channel->ListEntry);
            ZpServerQuic_SendChannelClose(Channel, Status);
            RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
            ZpServerQuic_DestroyChannel(Channel);
            return STATUS_SUCCESS;
        }
        CreditBytes = (ULONG)min(Message->Data.Length,
                                 Channel->RemainingBytes -
                                     Channel->ReceiveCredit);
        if (CreditBytes == 0)
        {
            RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
            return STATUS_SUCCESS;
        }
        Status = ZpMessage_EncodeChannelWindow(Message->ChannelId,
                                               CreditBytes,
                                               Body,
                                               sizeof(Body),
                                               &BodyLength);
        if (!NT_SUCCESS(Status))
        {
            RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
            return Status;
        }
        Channel->ReceiveCredit += CreditBytes;
        Status = ZpQuic_SendFrame(QuicConnection->Stream,
                                  &QuicConnection->ProtocolConnection,
                                  ZpMessageChannelWindow,
                                  Body,
                                  BodyLength);
        if (!NT_SUCCESS(Status))
        {
            Channel->ReceiveCredit -= CreditBytes;
        }
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
        return Status;
    }
    if (Channel == NULL ||
        Channel->Type != ZpServerQuicChannelTerminal ||
        Message->Data.Length > Channel->ReceiveCredit)
    {
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->ReceiveCredit -= Message->Data.Length;
    WriteSucceeded = WriteFile(Channel->Input,
                               Message->Data.Buffer,
                               Message->Data.Length,
                               &BytesWritten,
                               NULL);
    if (!WriteSucceeded || BytesWritten != Message->Data.Length)
    {
        Status = WriteSucceeded ?
                     STATUS_UNSUCCESSFUL :
                     NTSTATUS_FROM_WIN32(GetLastError());
        InterlockedExchange(&Channel->Pending, FALSE);
        RemoveEntryList(&Channel->ListEntry);
        Destroy = !Channel->WorkerActive;
        ZpServerQuic_SendChannelClose(Channel, Status);
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
        if (Destroy)
        {
            ZpServerQuic_DestroyChannel(Channel);
        }
        return STATUS_SUCCESS;
    }
    Channel->ReceiveCredit += Message->Data.Length;
    Status = ZpQuic_SendFrame(QuicConnection->Stream,
                              &QuicConnection->ProtocolConnection,
                              ZpMessageChannelWindow,
                              Body,
                              BodyLength);
    if (!NT_SUCCESS(Status))
    {
        Channel->ReceiveCredit -= Message->Data.Length;
    }
    RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
    return Status;
}

static
NTSTATUS
ZpServerQuic_ResizeTerminalChannel(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection,
    _In_ ULONGLONG ChannelId,
    _In_ USHORT Columns,
    _In_ USHORT Rows)
{
    PZP_SERVER_QUIC_CHANNEL Channel;
    COORD Size;
    HRESULT Result;

    if (Columns > MAXSHORT || Rows > MAXSHORT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
    Channel = ZpServerQuic_FindChannel(QuicConnection, ChannelId);
    if (Channel == NULL ||
        Channel->Type != ZpServerQuicChannelTerminal ||
        !Channel->Pending)
    {
        RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
        return STATUS_INVALID_HANDLE;
    }
    Size.X = (SHORT)Columns;
    Size.Y = (SHORT)Rows;
    Result = ResizePseudoConsole(Channel->PseudoConsole, Size);
    RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
    return SUCCEEDED(Result) ?
               STATUS_SUCCESS :
               NTSTATUS_FROM_WIN32(HRESULT_CODE(Result));
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
    PZP_SERVER_QUIC_CHANNEL Channel = NULL;
    ULONG PayloadLength = 0;
    ULONG ProcessId, ExitCode, MaxEntries;
    ZP_STRING_VIEW ServiceName, FilePath, FileCursor;
    ZP_FILE_INFO FileInfo;
    ZP_FILE_HASH_ALGORITHM FileHashAlgorithm;
    ZP_FILE_CREATE_DISPOSITION FileDisposition;
    BYTE FileDigest[ZP_FILE_SHA256_SIZE];
    ZP_TERMINAL_CREATE_VIEW TerminalCreate;
    ULONGLONG FileSize, FileOffset, TerminalChannelId;
    USHORT Columns, Rows;
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
                   Request->OperationId == ZP_SERVICE_OPERATION_STOP)) ||
                 Request->ModuleId == ZP_TERMINAL_MODULE_ID ||
                 (Request->ModuleId == ZP_FILE_MODULE_ID &&
                  Request->OperationId == ZP_FILE_OPERATION_OPEN_WRITE) ?
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
        else if (Request->ModuleId == ZP_FILE_MODULE_ID &&
                 Request->OperationId == ZP_FILE_OPERATION_QUERY)
        {
            Status = ZpFile_DecodePath(Request->Payload,
                                       Request->PayloadLength,
                                       &FilePath);
            if (NT_SUCCESS(Status))
            {
                Status = ZpServerQuic_QueryFile(&FilePath, &FileInfo);
            }
            if (NT_SUCCESS(Status))
            {
                Status = ZpFile_EncodeInfo(&FileInfo,
                                           Payload,
                                           sizeof(Payload),
                                           &PayloadLength);
                ResponsePayload = Payload;
            }
        }
        else if (Request->ModuleId == ZP_FILE_MODULE_ID &&
                 Request->OperationId == ZP_FILE_OPERATION_ENUMERATE)
        {
            Status = ZpFile_DecodePath(Request->Payload,
                                       Request->PayloadLength,
                                       &FilePath);
            if (NT_SUCCESS(Status))
            {
                Status = ZpServerQuic_EnumerateFiles(&FilePath,
                                                     NULL,
                                                     MAXULONG,
                                                     FALSE,
                                                     &AllocatedPayload,
                                                     &PayloadLength);
                ResponsePayload = AllocatedPayload;
            }
        }
        else if (Request->ModuleId == ZP_FILE_MODULE_ID &&
                 Request->OperationId == ZP_FILE_OPERATION_ENUMERATE_PAGE)
        {
            Status = ZpFile_DecodeEnumeratePageRequest(Request->Payload,
                                                       Request->PayloadLength,
                                                       &FilePath,
                                                       &FileCursor,
                                                       &MaxEntries);
            if (NT_SUCCESS(Status))
            {
                Status = ZpServerQuic_EnumerateFiles(&FilePath,
                                                     &FileCursor,
                                                     MaxEntries,
                                                     TRUE,
                                                     &AllocatedPayload,
                                                     &PayloadLength);
                ResponsePayload = AllocatedPayload;
            }
        }
        else if (Request->ModuleId == ZP_FILE_MODULE_ID &&
                 Request->OperationId == ZP_FILE_OPERATION_OPEN_READ)
        {
            Status = ZpFile_DecodeOpenReadRequest(Request->Payload,
                                                  Request->PayloadLength,
                                                  &FilePath,
                                                  &FileOffset);
            if (NT_SUCCESS(Status))
            {
                Status = ZpServerQuic_CreateFileChannel(QuicConnection,
                                                        &FilePath,
                                                        FileOffset,
                                                        &Channel,
                                                        &FileSize);
            }
            if (NT_SUCCESS(Status))
            {
                Status = ZpFile_EncodeOpenReadResponse(Channel->ChannelId,
                                                       FileSize,
                                                       FileOffset,
                                                       Payload,
                                                       sizeof(Payload),
                                                       &PayloadLength);
                ResponsePayload = Payload;
            }
        }
        else if (Request->ModuleId == ZP_FILE_MODULE_ID &&
                 Request->OperationId == ZP_FILE_OPERATION_HASH)
        {
            Status = ZpFile_DecodeHashRequest(Request->Payload,
                                              Request->PayloadLength,
                                              &FileHashAlgorithm,
                                              &FilePath);
            if (NT_SUCCESS(Status))
            {
                Status = ZpServerQuic_HashFile(&FilePath,
                                               FileHashAlgorithm,
                                               &Request->Pending,
                                               &FileSize,
                                               FileDigest);
            }
            if (NT_SUCCESS(Status))
            {
                Status = ZpFile_EncodeHashResponse(FileHashAlgorithm,
                                                   FileSize,
                                                   FileDigest,
                                                   sizeof(FileDigest),
                                                   Payload,
                                                   sizeof(Payload),
                                                   &PayloadLength);
                ResponsePayload = Payload;
            }
        }
        else if (Request->ModuleId == ZP_FILE_MODULE_ID &&
                 Request->OperationId == ZP_FILE_OPERATION_OPEN_WRITE)
        {
            Status = ZpFile_DecodeOpenWriteRequest(Request->Payload,
                                                   Request->PayloadLength,
                                                   &FilePath,
                                                   &FileSize,
                                                   &FileDisposition);
            if (NT_SUCCESS(Status))
            {
                Status = ZpServerQuic_CreateFileWriteChannel(
                    QuicConnection,
                    &FilePath,
                    FileSize,
                    FileDisposition,
                    &Channel);
            }
            if (NT_SUCCESS(Status))
            {
                Status = ZpFile_EncodeOpenWriteResponse(Channel->ChannelId,
                                                        FileSize,
                                                        Payload,
                                                        sizeof(Payload),
                                                        &PayloadLength);
                ResponsePayload = Payload;
            }
        }
        else if (Request->ModuleId == ZP_TERMINAL_MODULE_ID &&
                 Request->OperationId == ZP_TERMINAL_OPERATION_CREATE)
        {
            Status = ZpTerminal_DecodeCreate(Request->Payload,
                                             Request->PayloadLength,
                                             &TerminalCreate);
            if (NT_SUCCESS(Status))
            {
                Status = ZpServerQuic_CreateTerminalChannel(
                    QuicConnection,
                    &TerminalCreate,
                    &Channel);
            }
            if (NT_SUCCESS(Status))
            {
                Status = ZpTerminal_EncodeCreateResponse(Channel->ChannelId,
                                                          Channel->ProcessId,
                                                          Payload,
                                                          sizeof(Payload),
                                                          &PayloadLength);
                ResponsePayload = Payload;
            }
        }
        else if (Request->ModuleId == ZP_TERMINAL_MODULE_ID &&
                 Request->OperationId == ZP_TERMINAL_OPERATION_RESIZE)
        {
            Status = ZpTerminal_DecodeResize(Request->Payload,
                                             Request->PayloadLength,
                                             &TerminalChannelId,
                                             &Columns,
                                             &Rows);
            if (NT_SUCCESS(Status))
            {
                Status = ZpServerQuic_ResizeTerminalChannel(
                    QuicConnection,
                    TerminalChannelId,
                    Columns,
                    Rows);
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
        NTSTATUS SendStatus;

        RemoveEntryList(&Request->ListEntry);
        QuicConnection->ActiveRequestCount--;
        if (NT_SUCCESS(Status) && Channel != NULL)
        {
            Status = ZpServerQuic_ActivateChannel(Channel);
        }
        SendStatus = ZpServerQuic_SendResponse(
            QuicConnection,
            &QuicConnection->ProtocolConnection,
            Request->RequestId,
            Status,
            NT_SUCCESS(Status) ? ResponsePayload : NULL,
            NT_SUCCESS(Status) ? PayloadLength : 0);
        if (Channel != NULL)
        {
            if (!NT_SUCCESS(Status))
            {
                ZpServerQuic_DestroyChannel(Channel);
                Channel = NULL;
            }
            else if (!NT_SUCCESS(SendStatus))
            {
                ZpServerQuic_CloseRemoteChannel(QuicConnection,
                                                Channel->ChannelId);
                Channel = NULL;
            }
            else if (Channel->Type == ZpServerQuicChannelFileWrite)
            {
                NTSTATUS WindowStatus;

                if (Channel->RemainingBytes == 0)
                {
                    RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
                    WindowStatus = ZpServerQuic_CommitFileWriteChannel(Channel);
                    InterlockedExchange(&Channel->Pending, FALSE);
                    RemoveEntryList(&Channel->ListEntry);
                    ZpServerQuic_SendChannelClose(Channel, WindowStatus);
                    RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
                    ZpServerQuic_DestroyChannel(Channel);
                }
                else
                {
                    WindowStatus = ZpServerQuic_SendChannelWindow(
                        Channel,
                        (ULONG)min(Channel->RemainingBytes,
                                   ZP_SERVER_FILE_WRITE_WINDOW_SIZE));
                    if (!NT_SUCCESS(WindowStatus))
                    {
                        ZpServerQuic_SendChannelClose(Channel, WindowStatus);
                        ZpServerQuic_CloseRemoteChannel(QuicConnection,
                                                        Channel->ChannelId);
                    }
                }
                Channel = NULL;
            }
            else if (Channel->Type == ZpServerQuicChannelTerminal)
            {
                NTSTATUS WindowStatus = ZpServerQuic_SendChannelWindow(
                    Channel,
                    ZP_SERVER_TERMINAL_INPUT_WINDOW_SIZE);

                if (!NT_SUCCESS(WindowStatus))
                {
                    ZpServerQuic_SendChannelClose(Channel, WindowStatus);
                    ZpServerQuic_CloseRemoteChannel(QuicConnection,
                                                    Channel->ChannelId);
                }
                Channel = NULL;
            }
            else
            {
                Channel = NULL;
            }
        }
    }
    RtlReleaseSRWLockExclusive(&QuicConnection->RequestLock);

Cleanup:
    if (AllocatedPayload != NULL)
    {
        Mem_Free(AllocatedPayload);
    }
    if (Channel != NULL)
    {
        ZpServerQuic_DestroyChannel(Channel);
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
    ZP_CHANNEL_DATA_VIEW ChannelData;
    ZP_CHANNEL_CLOSE ChannelClose;
    ZP_READY Ready;
    BYTE Body[sizeof(USHORT) + ZP_MODULE_MAX_COUNT * 8];
    ULONG BodyLength;
    ULONG CreditBytes;
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

        case ZpMessageChannelWindow:
            Status = ZpMessage_DecodeChannelWindow(Frame->Body,
                                                    Frame->BodyLength,
                                                    &Token,
                                                    &CreditBytes);
            return NT_SUCCESS(Status) ?
                       ZpServerQuic_AddChannelWindow(QuicConnection,
                                                     Token,
                                                     CreditBytes) :
                       Status;

        case ZpMessageChannelData:
            Status = ZpMessage_DecodeChannelData(Frame->Body,
                                                  Frame->BodyLength,
                                                  &ChannelData);
            return NT_SUCCESS(Status) ?
                       ZpServerQuic_ReceiveChannelData(QuicConnection,
                                                       &ChannelData) :
                       Status;

        case ZpMessageChannelClose:
            Status = ZpMessage_DecodeChannelClose(Frame->Body,
                                                   Frame->BodyLength,
                                                   &ChannelClose);
            return NT_SUCCESS(Status) ?
                       ZpServerQuic_CloseRemoteChannel(
                           QuicConnection,
                           ChannelClose.ChannelId) :
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
VOID
ZpServerQuic_CancelChannels(
    _Inout_ PZP_SERVER_QUIC_CONNECTION QuicConnection)
{
    PZP_SERVER_QUIC_CHANNEL Channel;

    RtlAcquireSRWLockExclusive(&QuicConnection->ChannelLock);
    while (!IsListEmpty(&QuicConnection->Channels))
    {
        Channel = CONTAINING_RECORD(QuicConnection->Channels.Flink,
                                    ZP_SERVER_QUIC_CHANNEL,
                                    ListEntry);
        InterlockedExchange(&Channel->Pending, FALSE);
        RemoveEntryList(&Channel->ListEntry);
        if (!Channel->WorkerActive)
        {
            ZpServerQuic_DestroyChannel(Channel);
        }
    }
    RtlReleaseSRWLockExclusive(&QuicConnection->ChannelLock);
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
            ZpServerQuic_CancelChannels(QuicConnection);
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
            RtlInitializeSRWLock(&QuicConnection->ChannelLock);
            InitializeListHead(&QuicConnection->Channels);
            QuicConnection->NextChannelId = 2;
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
