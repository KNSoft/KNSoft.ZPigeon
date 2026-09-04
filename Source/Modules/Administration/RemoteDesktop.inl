#define ZP_RDP_PATCH_EXECUTABLE 0x80
#define ZP_RDP_PATCH_ENABLE 1
#define ZP_RDP_PATCH_MAX_COUNT 16
#define ZP_RDP_PATCH_MAX_LENGTH 32
#define ZP_RDP_PATCH_MAX_PLAN_LENGTH 1024
#define ZP_RDP_CONFIGURATION_PORT_MASK 0x0000FFFF
#define ZP_RDP_CONFIGURATION_ENABLED 0x00010000
#define ZP_RDP_CONFIGURATION_NLA 0x00020000
#define ZP_RDP_CONFIGURATION_SAME_USER_MULTIPLE_SESSIONS 0x00040000
#define ZP_RDP_CONFIGURATION_MASK 0x0007FFFF

typedef struct _ZP_RDP_PATCH
{
    ULONG Rva;
    BYTE Length;
    BOOLEAN Executable;
    const BYTE* Value;
    BYTE Original[ZP_RDP_PATCH_MAX_LENGTH];
    BOOLEAN Changed;
} ZP_RDP_PATCH, *PZP_RDP_PATCH;

typedef struct _ZP_RDP_MODULE
{
    HANDLE Process;
    PBYTE Base;
    ULONG Size;
    ULONG ProcessId;
    LARGE_INTEGER ProcessCreationTime;
    BOOLEAN Found;
} ZP_RDP_MODULE, *PZP_RDP_MODULE;

typedef struct _ZP_RDP_PATCH_BACKUP_ENTRY
{
    ULONG Rva;
    BYTE Length;
    BOOLEAN Executable;
    BYTE Original[ZP_RDP_PATCH_MAX_LENGTH];
    BYTE Patched[ZP_RDP_PATCH_MAX_LENGTH];
} ZP_RDP_PATCH_BACKUP_ENTRY, *PZP_RDP_PATCH_BACKUP_ENTRY;

typedef struct _ZP_RDP_PATCH_BACKUP
{
    ULONG ProcessId;
    LARGE_INTEGER ProcessCreationTime;
    BYTE Count;
    ZP_RDP_PATCH_BACKUP_ENTRY Entries[ZP_RDP_PATCH_MAX_COUNT];
    BOOLEAN Valid;
} ZP_RDP_PATCH_BACKUP, *PZP_RDP_PATCH_BACKUP;

static RTL_SRWLOCK ZpRdpPatchLock = RTL_SRWLOCK_INIT;
static ZP_RDP_PATCH_BACKUP ZpRdpPatchBackup;

static const UNICODE_STRING ZpRemoteDesktopKey = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Terminal Server");
static const UNICODE_STRING ZpRemoteDesktopPortKey = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\WinStations\\RDP-Tcp");
static const UNICODE_STRING ZpRemoteDesktopEnabledValue = RTL_CONSTANT_STRING(L"fDenyTSConnections");
static const UNICODE_STRING ZpRemoteDesktopSameUserMultipleSessionsValue =
    RTL_CONSTANT_STRING(L"fSingleSessionPerUser");
static const UNICODE_STRING ZpRemoteDesktopPortValue = RTL_CONSTANT_STRING(L"PortNumber");
static const UNICODE_STRING ZpRemoteDesktopNlaValue = RTL_CONSTANT_STRING(L"UserAuthentication");

static
NTSTATUS
ZpRdp_GetTermsrvPath(
    _Out_writes_(PathCount) PWSTR Path,
    _In_ SIZE_T PathCount)
{
    UINT Length;

    Length = GetSystemDirectoryW(Path, (UINT)PathCount);
    if (Length == 0) return NTSTATUS_FROM_WIN32(GetLastError());
    if (Length >= PathCount || PathCount - Length < ARRAYSIZE(L"\\termsrv.dll")) return STATUS_NAME_TOO_LONG;
    RtlCopyMemory(Path + Length, L"\\termsrv.dll", sizeof(L"\\termsrv.dll"));
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpRdp_GetTermsrvVersion(
    _Out_ PULONGLONG Version)
{
    WCHAR Path[MAX_PATH];
    static const WCHAR VersionKey[] = L"VS_VERSION_INFO";
    VS_FIXEDFILEINFO* Information;
    HMODULE Image;
    HRSRC Resource;
    HGLOBAL ResourceData;
    PBYTE Data;
    DWORD Length;
    ULONG InformationOffset;
    NTSTATUS Status;

    Status = ZpRdp_GetTermsrvPath(Path, ARRAYSIZE(Path));
    if (!NT_SUCCESS(Status)) return Status;
    Image = LoadLibraryExW(Path, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (Image == NULL) return NTSTATUS_FROM_WIN32(GetLastError());
    Resource = FindResourceW(Image, MAKEINTRESOURCEW(1), RT_VERSION);
    if (Resource == NULL)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
    }
    else if ((Length = SizeofResource(Image, Resource)) == 0 ||
             (ResourceData = LoadResource(Image, Resource)) == NULL)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
    }
    else if ((Data = LockResource(ResourceData)) == NULL)
    {
        Status = STATUS_RESOURCE_DATA_NOT_FOUND;
    }
    else
    {
        InformationOffset = ALIGN_UP_BY(sizeof(WORD) * 3 + sizeof(VersionKey), sizeof(DWORD));
        if (Length < InformationOffset + sizeof(*Information) ||
            *(UNALIGNED WORD*)Data < InformationOffset + sizeof(*Information) ||
            *(UNALIGNED WORD*)(Data + sizeof(WORD)) < sizeof(*Information) ||
            memcmp(Data + sizeof(WORD) * 3, VersionKey, sizeof(VersionKey)) != 0)
        {
            Status = STATUS_INVALID_IMAGE_FORMAT;
        }
        else
        {
            Information = (VS_FIXEDFILEINFO*)(Data + InformationOffset);
            if (Information->dwSignature != VS_FFI_SIGNATURE)
            {
                Status = STATUS_INVALID_IMAGE_FORMAT;
            }
            else
            {
                *Version = ((ULONGLONG)Information->dwFileVersionMS << 32) | Information->dwFileVersionLS;
                Status = STATUS_SUCCESS;
            }
        }
    }
    FreeLibrary(Image);
    return Status;
}

static
NTSTATUS
ZpRdp_QueryServiceHandle(
    _In_ SC_HANDLE Service,
    _Out_ LPSERVICE_STATUS_PROCESS ServiceStatus)
{
    DWORD Length;

    return QueryServiceStatusEx(Service,
                                SC_STATUS_PROCESS_INFO,
                                (PBYTE)ServiceStatus,
                                sizeof(*ServiceStatus),
                                &Length) ?
               STATUS_SUCCESS : NTSTATUS_FROM_WIN32(GetLastError());
}

static
NTSTATUS
ZpRdp_QueryService(
    _Out_ LPSERVICE_STATUS_PROCESS ServiceStatus)
{
    SC_HANDLE Manager, Service;
    NTSTATUS Status;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (Manager == NULL) return NTSTATUS_FROM_WIN32(GetLastError());
    Service = OpenServiceW(Manager, L"TermService", SERVICE_QUERY_STATUS);
    CloseServiceHandle(Manager);
    if (Service == NULL) return NTSTATUS_FROM_WIN32(GetLastError());
    Status = ZpRdp_QueryServiceHandle(Service, ServiceStatus);
    CloseServiceHandle(Service);
    return Status;
}

static
NTSTATUS
ZpRdp_WaitForServiceState(
    _In_ SC_HANDLE Service,
    _In_ DWORD State)
{
    SERVICE_STATUS_PROCESS ServiceStatus;
    LARGE_INTEGER Delay;
    ULONG Index;
    NTSTATUS Status;

    Delay.QuadPart = -100 * 10000;
    for (Index = 0; Index < 300; Index++)
    {
        Status = ZpRdp_QueryServiceHandle(Service, &ServiceStatus);
        if (!NT_SUCCESS(Status) || ServiceStatus.dwCurrentState == State) return Status;
        NtDelayExecution(FALSE, &Delay);
    }
    return NTSTATUS_FROM_WIN32(ERROR_SERVICE_REQUEST_TIMEOUT);
}

static
NTSTATUS
ZpRdp_RestartService(VOID)
{
    SC_HANDLE Dependent, Manager, Service;
    SERVICE_STATUS_PROCESS DependentStatus, ProcessStatus;
    SERVICE_STATUS ServiceStatus;
    BOOLEAN RestartDependent = FALSE;
    NTSTATUS Status;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (Manager == NULL) return NTSTATUS_FROM_WIN32(GetLastError());
    Service = OpenServiceW(Manager,
                           L"TermService",
                           SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP);
    if (Service == NULL)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        CloseServiceHandle(Manager);
        return Status;
    }
    Dependent = OpenServiceW(Manager,
                             L"UmRdpService",
                             SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP);
    CloseServiceHandle(Manager);
    if (Dependent == NULL)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        CloseServiceHandle(Service);
        return Status;
    }
    Status = ZpRdp_QueryServiceHandle(Dependent, &DependentStatus);
    if (NT_SUCCESS(Status) && DependentStatus.dwCurrentState != SERVICE_STOPPED)
    {
        RestartDependent = TRUE;
        if (DependentStatus.dwCurrentState != SERVICE_STOP_PENDING &&
            !ControlService(Dependent, SERVICE_CONTROL_STOP, &ServiceStatus))
        {
            Status = NTSTATUS_FROM_WIN32(GetLastError());
        }
        if (NT_SUCCESS(Status)) Status = ZpRdp_WaitForServiceState(Dependent, SERVICE_STOPPED);
    }
    if (NT_SUCCESS(Status)) Status = ZpRdp_QueryServiceHandle(Service, &ProcessStatus);
    if (NT_SUCCESS(Status) && ProcessStatus.dwCurrentState != SERVICE_STOPPED)
    {
        if (ProcessStatus.dwCurrentState != SERVICE_STOP_PENDING &&
            !ControlService(Service, SERVICE_CONTROL_STOP, &ServiceStatus))
        {
            Status = NTSTATUS_FROM_WIN32(GetLastError());
        }
        if (NT_SUCCESS(Status)) Status = ZpRdp_WaitForServiceState(Service, SERVICE_STOPPED);
    }
    if (NT_SUCCESS(Status) && !StartServiceW(Service, 0, NULL))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
    }
    if (NT_SUCCESS(Status)) Status = ZpRdp_WaitForServiceState(Service, SERVICE_RUNNING);
    if (NT_SUCCESS(Status) && RestartDependent)
    {
        Status = ZpRdp_QueryServiceHandle(Dependent, &DependentStatus);
        if (NT_SUCCESS(Status) && DependentStatus.dwCurrentState != SERVICE_RUNNING)
        {
            if (DependentStatus.dwCurrentState != SERVICE_START_PENDING &&
                !StartServiceW(Dependent, 0, NULL))
            {
                Status = NTSTATUS_FROM_WIN32(GetLastError());
            }
            if (NT_SUCCESS(Status)) Status = ZpRdp_WaitForServiceState(Dependent, SERVICE_RUNNING);
        }
    }
    CloseServiceHandle(Dependent);
    CloseServiceHandle(Service);
    return Status;
}

static
NTSTATUS
ZpRdp_QueryDword(
    _In_ PCUNICODE_STRING KeyPath,
    _In_ PCUNICODE_STRING ValueName,
    _Out_ PULONG Value)
{
    HANDLE Key;
    NTSTATUS Status;

    Status = Sys_RegOpenKey(&Key, KEY_QUERY_VALUE, KeyPath);
    if (!NT_SUCCESS(Status)) return Status;
    Status = Sys_RegQueryDword(Key, ValueName, Value);
    NtClose(Key);
    return Status;
}

static
NTSTATUS
ZpAdministration_AddRemoteDesktop(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    SERVICE_STATUS_PROCESS ServiceStatus;
    ULONGLONG Version;
    ULONG Value;
    NTSTATUS Status;

    Status = ZpRdp_QueryDword(&ZpRemoteDesktopKey, &ZpRemoteDesktopEnabledValue, &Value);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemValue(Builder,
                                                 L"remoteDesktopEnabled",
                                                 NULL,
                                                 ZP_SYSTEM_INFORMATION_EDITABLE,
                                                 Value == 0);
    }
    if (NT_SUCCESS(Status)) Status = ZpRdp_QueryDword(&ZpRemoteDesktopPortKey, &ZpRemoteDesktopPortValue, &Value);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemValue(Builder,
                                                 L"remoteDesktopPort",
                                                 NULL,
                                                 ZP_SYSTEM_INFORMATION_EDITABLE |
                                                     ZP_SYSTEM_INFORMATION_RESTART_REQUIRED,
                                                 Value);
    }
    if (NT_SUCCESS(Status)) Status = ZpRdp_QueryDword(&ZpRemoteDesktopPortKey, &ZpRemoteDesktopNlaValue, &Value);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemValue(Builder,
                                                 L"remoteDesktopNla",
                                                 NULL,
                                                 ZP_SYSTEM_INFORMATION_EDITABLE,
                                                 Value != 0);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpRdp_QueryDword(&ZpRemoteDesktopKey, &ZpRemoteDesktopSameUserMultipleSessionsValue, &Value);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemValue(Builder,
                                                 L"remoteDesktopSameUserMultipleSessions",
                                                 NULL,
                                                 ZP_SYSTEM_INFORMATION_EDITABLE,
                                                 Value == 0);
    }
    if (NT_SUCCESS(Status)) Status = ZpRdp_GetTermsrvVersion(&Version);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemValue(Builder,
                                                 L"remoteDesktopVersion",
                                                 NULL,
                                                 0,
                                                 Version);
    }
    if (NT_SUCCESS(Status)) Status = ZpRdp_QueryService(&ServiceStatus);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemValue(Builder,
                                                 L"remoteDesktopServiceState",
                                                 NULL,
                                                 0,
                                                 ServiceStatus.dwCurrentState);
    }
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateRemoteDesktop(
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    NTSTATUS Status;

    Status = ZpAdministration_AddRemoteDesktop(&Builder);
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

NTSTATUS
ZpRdp_SetDword(
    _In_ HANDLE Key,
    _In_ PCUNICODE_STRING ValueName,
    _In_ ULONG Value)
{
    return NtSetValueKey(Key,
                         (PUNICODE_STRING)ValueName,
                         0,
                         REG_DWORD,
                         &Value,
                         sizeof(Value));
}

static
ZP_STATUS
ZpAdministration_ConfigureRemoteDesktop(
    _In_ PCZP_ADMINISTRATION_DATA_CONTROL_VIEW Control)
{
    HANDLE Key;
    ULONG Port, Value;
    NTSTATUS Status;

    Port = Control->Flags & ZP_RDP_CONFIGURATION_PORT_MASK;
    if (Control->Action != ZpAdministrationActionConfigure ||
        Control->Identity.Length != 0 || Control->Data.Length != 0 ||
        FlagOn(Control->Flags, ~ZP_RDP_CONFIGURATION_MASK) || Port == 0)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Status = Sys_RegOpenKey(&Key, KEY_SET_VALUE, &ZpRemoteDesktopKey);
    if (NT_SUCCESS(Status))
    {
        Value = !FlagOn(Control->Flags, ZP_RDP_CONFIGURATION_ENABLED);
        Status = ZpRdp_SetDword(Key, &ZpRemoteDesktopEnabledValue, Value);
        if (NT_SUCCESS(Status))
        {
            Value = !FlagOn(Control->Flags, ZP_RDP_CONFIGURATION_SAME_USER_MULTIPLE_SESSIONS);
            Status = ZpRdp_SetDword(Key, &ZpRemoteDesktopSameUserMultipleSessionsValue, Value);
        }
        NtClose(Key);
    }
    if (NT_SUCCESS(Status)) Status = Sys_RegOpenKey(&Key, KEY_SET_VALUE, &ZpRemoteDesktopPortKey);
    if (NT_SUCCESS(Status))
    {
        Status = ZpRdp_SetDword(Key, &ZpRemoteDesktopPortValue, Port);
        if (NT_SUCCESS(Status))
        {
            Value = !!FlagOn(Control->Flags, ZP_RDP_CONFIGURATION_NLA);
            Status = ZpRdp_SetDword(Key, &ZpRemoteDesktopNlaValue, Value);
        }
        NtClose(Key);
    }
    return ZpStatus_FromNtStatus(Status);
}

static
_Function_class_(LDR_ENUM_CALLBACK64)
VOID
NTAPI
ZpRdp_FindTermsrvModule(
    _In_ PCLDR_DATA_TABLE_ENTRY64 ModuleInformation,
    _In_opt_ PVOID Parameter,
    _Out_ PBOOLEAN Stop)
{
    static const WCHAR TermsrvName[] = L"termsrv.dll";
    PZP_RDP_MODULE Module = Parameter;
    WCHAR Name[ARRAYSIZE(TermsrvName)];

    *Stop = FALSE;
    if (ModuleInformation->BaseDllName.Length != sizeof(TermsrvName) - sizeof(WCHAR)) return;
    if (!NT_SUCCESS(NtReadVirtualMemory(Module->Process,
                                        (PVOID)(ULONG_PTR)ModuleInformation->BaseDllName.Buffer,
                                        Name,
                                        sizeof(TermsrvName) - sizeof(WCHAR),
                                        NULL)))
    {
        return;
    }
    Name[ARRAYSIZE(Name) - 1] = UNICODE_NULL;
    if (_wcsicmp(Name, TermsrvName) != 0) return;
    Module->Base = (PBYTE)(ULONG_PTR)ModuleInformation->DllBase;
    Module->Size = ModuleInformation->SizeOfImage;
    Module->Found = TRUE;
    *Stop = TRUE;
}

static
NTSTATUS
ZpRdp_OpenTermsrvProcess(
    _Out_ PZP_RDP_MODULE Module)
{
    SERVICE_STATUS_PROCESS ServiceStatus;
    KERNEL_USER_TIMES Times;
    OBJECT_ATTRIBUTES Attributes;
    CLIENT_ID ClientId;
    NTSTATUS Status;

    Status = ZpRdp_QueryService(&ServiceStatus);
    if (!NT_SUCCESS(Status)) return Status;
    if (ServiceStatus.dwProcessId == 0 || ServiceStatus.dwCurrentState == SERVICE_STOPPED)
    {
        return NTSTATUS_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE);
    }
    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    ClientId.UniqueProcess = UlongToHandle(ServiceStatus.dwProcessId);
    ClientId.UniqueThread = NULL;
    Status = NtOpenProcess(&Module->Process,
                           PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_READ |
                               PROCESS_VM_WRITE | PROCESS_SUSPEND_RESUME,
                           &Attributes,
                           &ClientId);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryInformationProcess(Module->Process,
                                       ProcessTimes,
                                       &Times,
                                       sizeof(Times),
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        Module->ProcessId = ServiceStatus.dwProcessId;
        Module->ProcessCreationTime = Times.CreateTime;
    }
    Module->Found = FALSE;
    if (NT_SUCCESS(Status)) Status = PS_RemoteEnumerateModules64(Module->Process, ZpRdp_FindTermsrvModule, Module);
    if (NT_SUCCESS(Status) && !Module->Found) Status = STATUS_DLL_NOT_FOUND;
    if (!NT_SUCCESS(Status))
    {
        NtClose(Module->Process);
        Module->Process = NULL;
    }
    return Status;
}

static
NTSTATUS
ZpRdp_MapTermsrvImage(
    _Out_ PIO_FILE_MAP Map,
    _Out_ PIMAGE_NT_HEADERS64* Headers)
{
    WCHAR Path[MAX_PATH];
    PIMAGE_DOS_HEADER Dos;
    PIMAGE_NT_HEADERS64 Nt;
    HANDLE File;
    NTSTATUS Status;

    Status = ZpRdp_GetTermsrvPath(Path, ARRAYSIZE(Path));
    if (!NT_SUCCESS(Status)) return Status;
    Status = IO_OpenWin32File(&File,
                              Path,
                              NULL,
                              FILE_READ_DATA | SYNCHRONIZE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtCreateSection(&Map->SectionHandle,
                             SECTION_MAP_READ,
                             NULL,
                             NULL,
                             PAGE_READONLY,
                             SEC_IMAGE_NO_EXECUTE,
                             File);
    NtClose(File);
    if (!NT_SUCCESS(Status)) return Status;
    Map->BaseAddress = NULL;
    Map->PageSize = 0;
    Status = NtMapViewOfSection(Map->SectionHandle,
                                NtCurrentProcess(),
                                &Map->BaseAddress,
                                0,
                                0,
                                NULL,
                                &Map->PageSize,
                                ViewUnmap,
                                0,
                                PAGE_READONLY);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Map->SectionHandle);
        return Status;
    }
    Dos = Map->BaseAddress;
    if (Map->PageSize < sizeof(*Nt) || Dos->e_magic != IMAGE_DOS_SIGNATURE || Dos->e_lfanew < 0 ||
        (SIZE_T)Dos->e_lfanew > Map->PageSize - sizeof(*Nt))
    {
        Status = STATUS_INVALID_IMAGE_FORMAT;
    }
    else
    {
        Nt = Add2Ptr(Dos, Dos->e_lfanew);
        if (Nt->Signature != IMAGE_NT_SIGNATURE || Nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            Nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            Nt->OptionalHeader.SizeOfImage > Map->PageSize)
        {
            Status = STATUS_INVALID_IMAGE_FORMAT;
        }
        else
        {
            *Headers = Nt;
            Status = STATUS_SUCCESS;
        }
    }
    if (!NT_SUCCESS(Status)) IO_UnmapFile(Map);
    return Status;
}

static
NTSTATUS
ZpRdp_ValidateRemoteImage(
    _In_ PZP_RDP_MODULE Module,
    _In_ PIMAGE_NT_HEADERS64 LocalHeaders)
{
    IMAGE_DOS_HEADER Dos;
    IMAGE_NT_HEADERS64 Nt;
    NTSTATUS Status;

    if (Module->Size != LocalHeaders->OptionalHeader.SizeOfImage) return STATUS_IMAGE_CHECKSUM_MISMATCH;
    Status = NtReadVirtualMemory(Module->Process, Module->Base, &Dos, sizeof(Dos), NULL);
    if (!NT_SUCCESS(Status)) return Status;
    if (Dos.e_magic != IMAGE_DOS_SIGNATURE || Dos.e_lfanew < 0 || Module->Size < sizeof(Nt) ||
        (ULONG)Dos.e_lfanew > Module->Size - sizeof(Nt))
    {
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    Status = NtReadVirtualMemory(Module->Process,
                                 Module->Base + Dos.e_lfanew,
                                 &Nt,
                                 sizeof(Nt),
                                 NULL);
    if (!NT_SUCCESS(Status)) return Status;
    return Nt.Signature == IMAGE_NT_SIGNATURE &&
                   Nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
                   Nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
                   Nt.OptionalHeader.SizeOfImage == LocalHeaders->OptionalHeader.SizeOfImage &&
                   Nt.FileHeader.TimeDateStamp == LocalHeaders->FileHeader.TimeDateStamp ?
               STATUS_SUCCESS : STATUS_IMAGE_CHECKSUM_MISMATCH;
}

static
NTSTATUS
ZpRdp_ValidatePatchSection(
    _In_ PZP_RDP_PATCH Patch,
    _In_ PIMAGE_NT_HEADERS64 Headers)
{
    PIMAGE_SECTION_HEADER Section;
    ULONG Index, Size;

    Section = IMAGE_FIRST_SECTION(Headers);
    for (Index = 0; Index < Headers->FileHeader.NumberOfSections; Index++)
    {
        Size = max(Section[Index].Misc.VirtualSize, Section[Index].SizeOfRawData);
        if (Size >= Patch->Length && Patch->Rva >= Section[Index].VirtualAddress &&
            Patch->Rva - Section[Index].VirtualAddress <= Size - Patch->Length)
        {
            return Patch->Executable ?
                       (Section[Index].Characteristics & IMAGE_SCN_MEM_EXECUTE ?
                            STATUS_SUCCESS : STATUS_INVALID_ADDRESS) :
                       (Section[Index].Characteristics & IMAGE_SCN_MEM_WRITE ?
                            STATUS_SUCCESS : STATUS_INVALID_ADDRESS);
        }
    }
    return STATUS_INVALID_ADDRESS;
}

static
NTSTATUS
ZpRdp_DecodePatchPlan(
    _In_reads_bytes_(Length) const BYTE* Data,
    _In_ ULONG Length,
    _In_ ULONG ImageSize,
    _In_ PIMAGE_NT_HEADERS64 Headers,
    _Out_writes_(ZP_RDP_PATCH_MAX_COUNT) PZP_RDP_PATCH Patches,
    _Out_ PBYTE Count)
{
    ZP_CODEC_READER Reader;
    ZP_BUFFER_VIEW Value;
    ULONGLONG Version, CurrentVersion;
    ULONG Index, Previous;
    BYTE Descriptor;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Data, Length);
    Status = ZpCodec_ReadUInt64(&Reader, &Version);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, Count);
    if (NT_SUCCESS(Status)) Status = ZpRdp_GetTermsrvVersion(&CurrentVersion);
    if (!NT_SUCCESS(Status)) return Status;
    if (Version != CurrentVersion) return STATUS_REVISION_MISMATCH;
    if (*Count == 0 || *Count > ZP_RDP_PATCH_MAX_COUNT) return STATUS_DATA_ERROR;
    for (Index = 0; Index < *Count; Index++)
    {
        Status = ZpCodec_ReadUInt32(&Reader, &Patches[Index].Rva);
        if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Descriptor);
        if (!NT_SUCCESS(Status)) return Status;
        Patches[Index].Length = Descriptor & ~ZP_RDP_PATCH_EXECUTABLE;
        Patches[Index].Executable = !!(Descriptor & ZP_RDP_PATCH_EXECUTABLE);
        if (Patches[Index].Length == 0 || Patches[Index].Length > ZP_RDP_PATCH_MAX_LENGTH ||
            (!Patches[Index].Executable && Patches[Index].Length != sizeof(ULONG)) ||
            ImageSize < Patches[Index].Length ||
            Patches[Index].Rva > ImageSize - Patches[Index].Length)
        {
            return STATUS_DATA_ERROR;
        }
        Status = ZpCodec_ReadData(&Reader, Patches[Index].Length, &Value);
        if (!NT_SUCCESS(Status)) return Status;
        Patches[Index].Value = Value.Buffer;
        Patches[Index].Changed = FALSE;
        Status = ZpRdp_ValidatePatchSection(&Patches[Index], Headers);
        if (!NT_SUCCESS(Status)) return Status;
        for (Previous = 0; Previous < Index; Previous++)
        {
            if (Patches[Index].Rva < Patches[Previous].Rva + Patches[Previous].Length &&
                Patches[Previous].Rva < Patches[Index].Rva + Patches[Index].Length)
            {
                return STATUS_DATA_ERROR;
            }
        }
    }
    return Reader.Offset == Reader.Size ? STATUS_SUCCESS : STATUS_DATA_ERROR;
}

static
NTSTATUS
ZpRdp_ReadPatches(
    _In_ PZP_RDP_MODULE Module,
    _Inout_updates_(Count) PZP_RDP_PATCH Patches,
    _In_ BYTE Count)
{
    SIZE_T Read;
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index < Count; Index++)
    {
        Status = NtReadVirtualMemory(Module->Process,
                                     Module->Base + Patches[Index].Rva,
                                     Patches[Index].Original,
                                     Patches[Index].Length,
                                     &Read);
        if (!NT_SUCCESS(Status)) return Status;
        if (Read != Patches[Index].Length) return STATUS_PARTIAL_COPY;
    }
    return STATUS_SUCCESS;
}

static
BOOLEAN
ZpRdp_ArePatchesApplied(
    _In_reads_(Count) PZP_RDP_PATCH Patches,
    _In_ BYTE Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (memcmp(Patches[Index].Original, Patches[Index].Value, Patches[Index].Length) != 0) return FALSE;
    }
    return TRUE;
}

static
BOOLEAN
ZpRdp_AreAnyCodePatchesApplied(
    _In_reads_(Count) PZP_RDP_PATCH Patches,
    _In_ BYTE Count,
    _In_ PVOID LocalImage)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (Patches[Index].Executable &&
            memcmp(Patches[Index].Value,
                   Add2Ptr(LocalImage, Patches[Index].Rva),
                   Patches[Index].Length) != 0 &&
            memcmp(Patches[Index].Original, Patches[Index].Value, Patches[Index].Length) == 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static
BOOLEAN
ZpRdp_IsBackupForProcess(
    _In_ PZP_RDP_MODULE Module)
{
    return ZpRdpPatchBackup.Valid &&
           ZpRdpPatchBackup.ProcessId == Module->ProcessId &&
           ZpRdpPatchBackup.ProcessCreationTime.QuadPart == Module->ProcessCreationTime.QuadPart;
}

static
BOOLEAN
ZpRdp_BackupMatches(
    _In_ PZP_RDP_MODULE Module,
    _In_reads_(Count) PZP_RDP_PATCH Patches,
    _In_ BYTE Count)
{
    PZP_RDP_PATCH_BACKUP_ENTRY Entry;
    ULONG Index;

    if (!ZpRdp_IsBackupForProcess(Module) || ZpRdpPatchBackup.Count != Count) return FALSE;
    for (Index = 0; Index < Count; Index++)
    {
        Entry = &ZpRdpPatchBackup.Entries[Index];
        if (Entry->Rva != Patches[Index].Rva ||
            Entry->Length != Patches[Index].Length ||
            Entry->Executable != Patches[Index].Executable ||
            memcmp(Entry->Patched, Patches[Index].Value, Entry->Length) != 0)
        {
            return FALSE;
        }
    }
    return TRUE;
}

static
VOID
ZpRdp_SaveBackup(
    _In_ PZP_RDP_MODULE Module,
    _In_reads_(Count) PZP_RDP_PATCH Patches,
    _In_ BYTE Count)
{
    PZP_RDP_PATCH_BACKUP_ENTRY Entry;
    ULONG Index;

    ZpRdpPatchBackup.Valid = FALSE;
    ZpRdpPatchBackup.ProcessId = Module->ProcessId;
    ZpRdpPatchBackup.ProcessCreationTime = Module->ProcessCreationTime;
    ZpRdpPatchBackup.Count = Count;
    for (Index = 0; Index < Count; Index++)
    {
        Entry = &ZpRdpPatchBackup.Entries[Index];
        Entry->Rva = Patches[Index].Rva;
        Entry->Length = Patches[Index].Length;
        Entry->Executable = Patches[Index].Executable;
        RtlCopyMemory(Entry->Original, Patches[Index].Original, Entry->Length);
        RtlCopyMemory(Entry->Patched, Patches[Index].Value, Entry->Length);
    }
    ZpRdpPatchBackup.Valid = TRUE;
}

static
NTSTATUS
ZpRdp_WritePatch(
    _In_ PZP_RDP_MODULE Module,
    _In_ PZP_RDP_PATCH Patch,
    _In_reads_bytes_(Patch->Length) const BYTE* Value,
    _Out_ PBOOLEAN Changed)
{
    PVOID Address, ProtectAddress;
    SIZE_T Length, ProtectLength, Written;
    ULONG OldProtect;
    NTSTATUS Status, RestoreStatus;

    *Changed = FALSE;
    Address = Module->Base + Patch->Rva;
    Length = Patch->Length;
    if (Patch->Executable)
    {
        ProtectAddress = Address;
        ProtectLength = Length;
        Status = NtProtectVirtualMemory(Module->Process,
                                        &ProtectAddress,
                                        &ProtectLength,
                                        PAGE_EXECUTE_READWRITE,
                                        &OldProtect);
        if (!NT_SUCCESS(Status)) return Status;
    }
    Written = 0;
    Status = NtWriteVirtualMemory(Module->Process, Address, (PVOID)Value, Length, &Written);
    if (Written != 0) *Changed = TRUE;
    if (NT_SUCCESS(Status) && Written != Length) Status = STATUS_PARTIAL_COPY;
    if (Patch->Executable)
    {
        RestoreStatus = NtProtectVirtualMemory(Module->Process,
                                               &ProtectAddress,
                                               &ProtectLength,
                                               OldProtect,
                                               &OldProtect);
        if (NT_SUCCESS(Status)) Status = RestoreStatus;
        if (NT_SUCCESS(Status)) Status = NtFlushInstructionCache(Module->Process, Address, Length);
    }
    return Status;
}

static
NTSTATUS
ZpRdp_RestorePatches(
    _In_ PZP_RDP_MODULE Module,
    _Inout_updates_(Count) PZP_RDP_PATCH Patches,
    _In_ BYTE Count)
{
    PZP_RDP_PATCH_BACKUP_ENTRY Entry;
    ULONG Index, Pass;
    BOOLEAN Changed;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!ZpRdp_BackupMatches(Module, Patches, Count)) return STATUS_NOT_FOUND;
    for (Index = 0; Index < Count; Index++)
    {
        Entry = &ZpRdpPatchBackup.Entries[Index];
        if (memcmp(Patches[Index].Original, Entry->Original, Entry->Length) != 0 &&
            memcmp(Patches[Index].Original, Entry->Patched, Entry->Length) != 0)
        {
            return STATUS_IMAGE_CHECKSUM_MISMATCH;
        }
    }
    for (Pass = 0; Pass < 2 && NT_SUCCESS(Status); Pass++)
    {
        for (Index = 0; Index < Count && NT_SUCCESS(Status); Index++)
        {
            Entry = &ZpRdpPatchBackup.Entries[Index];
            if (Patches[Index].Executable != (Pass == 0) ||
                memcmp(Patches[Index].Original, Entry->Original, Entry->Length) == 0)
            {
                continue;
            }
            Status = ZpRdp_WritePatch(Module, &Patches[Index], Entry->Original, &Changed);
            if (Changed) Patches[Index].Changed = TRUE;
        }
    }
    if (!NT_SUCCESS(Status))
    {
        for (Index = Count; Index-- != 0;)
        {
            if (Patches[Index].Changed)
            {
                ZpRdp_WritePatch(Module, &Patches[Index], Patches[Index].Original, &Changed);
            }
        }
    }
    return Status;
}

static
NTSTATUS
ZpRdp_ApplyPatches(
    _In_ PZP_RDP_MODULE Module,
    _Inout_updates_(Count) PZP_RDP_PATCH Patches,
    _In_ BYTE Count,
    _In_ PVOID LocalImage)
{
    ULONG Index, Pass;
    BOOLEAN Changed;
    NTSTATUS Status = STATUS_SUCCESS;

    for (Index = 0; Index < Count; Index++)
    {
        if (Patches[Index].Executable &&
            memcmp(Patches[Index].Original, Patches[Index].Value, Patches[Index].Length) != 0 &&
            memcmp(Patches[Index].Original,
                   Add2Ptr(LocalImage, Patches[Index].Rva),
                   Patches[Index].Length) != 0)
        {
            return STATUS_IMAGE_CHECKSUM_MISMATCH;
        }
    }
    for (Pass = 0; Pass < 2 && NT_SUCCESS(Status); Pass++)
    {
        for (Index = 0; Index < Count && NT_SUCCESS(Status); Index++)
        {
            if (Patches[Index].Executable != (Pass != 0) ||
                memcmp(Patches[Index].Original, Patches[Index].Value, Patches[Index].Length) == 0)
            {
                continue;
            }
            Status = ZpRdp_WritePatch(Module, &Patches[Index], Patches[Index].Value, &Changed);
            if (Changed) Patches[Index].Changed = TRUE;
        }
    }
    if (!NT_SUCCESS(Status))
    {
        for (Index = Count; Index-- != 0;)
        {
            if (Patches[Index].Changed)
            {
                ZpRdp_WritePatch(Module, &Patches[Index], Patches[Index].Original, &Changed);
            }
        }
    }
    return Status;
}

static
ZP_STATUS
ZpAdministration_ControlRemoteDesktopPatch(
    _In_ PCZP_ADMINISTRATION_DATA_CONTROL_VIEW Control)
{
    IO_FILE_MAP Map;
    PIMAGE_NT_HEADERS64 Headers;
    ZP_RDP_PATCH Patches[ZP_RDP_PATCH_MAX_COUNT];
    ZP_RDP_MODULE Module;
    BOOLEAN PreviousPrivilege, RestorePrivilege = FALSE, RestartService = FALSE, Suspended = FALSE;
    BYTE Count;
    NTSTATUS Status, ResumeStatus;

    if ((Control->Action != ZpAdministrationActionCheck &&
         Control->Action != ZpAdministrationActionConfigure) ||
        (Control->Action == ZpAdministrationActionCheck && Control->Flags != 0) ||
        (Control->Action == ZpAdministrationActionConfigure && Control->Flags > ZP_RDP_PATCH_ENABLE) ||
        Control->Identity.Length != 0 || Control->Data.Length == 0 ||
        Control->Data.Length > ZP_RDP_PATCH_MAX_PLAN_LENGTH)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Module.Process = NULL;
    Status = ZpRdp_MapTermsrvImage(&Map, &Headers);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    RtlAcquireSRWLockExclusive(&ZpRdpPatchLock);
    Status = RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, TRUE, FALSE, &PreviousPrivilege);
    if (NT_SUCCESS(Status))
    {
        RestorePrivilege = !PreviousPrivilege;
        Status = ZpRdp_OpenTermsrvProcess(&Module);
    }
    if (NT_SUCCESS(Status) && ZpRdpPatchBackup.Valid && !ZpRdp_IsBackupForProcess(&Module))
    {
        ZpRdpPatchBackup.Valid = FALSE;
    }
    if (NT_SUCCESS(Status)) Status = ZpRdp_ValidateRemoteImage(&Module, Headers);
    if (NT_SUCCESS(Status))
    {
        Status = ZpRdp_DecodePatchPlan(Control->Data.Buffer,
                                       Control->Data.Length,
                                       Module.Size,
                                       Headers,
                                       Patches,
                                       &Count);
    }
    if (NT_SUCCESS(Status) && Control->Action == ZpAdministrationActionConfigure)
    {
        Status = NtSuspendProcess(Module.Process);
        if (NT_SUCCESS(Status)) Suspended = TRUE;
    }
    if (NT_SUCCESS(Status)) Status = ZpRdp_ReadPatches(&Module, Patches, Count);
    if (NT_SUCCESS(Status))
    {
        if (Control->Action == ZpAdministrationActionCheck)
        {
            Status = ZpRdp_ArePatchesApplied(Patches, Count) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
        }
        else if (Control->Flags == ZP_RDP_PATCH_ENABLE)
        {
            if (!ZpRdp_ArePatchesApplied(Patches, Count))
            {
                if (ZpRdpPatchBackup.Valid && !ZpRdp_BackupMatches(&Module, Patches, Count))
                {
                    Status = STATUS_REVISION_MISMATCH;
                }
                else
                {
                    Status = ZpRdp_ApplyPatches(&Module, Patches, Count, Map.BaseAddress);
                    if (NT_SUCCESS(Status) && !ZpRdpPatchBackup.Valid)
                    {
                        ZpRdp_SaveBackup(&Module, Patches, Count);
                    }
                }
            }
        }
        else if (ZpRdp_BackupMatches(&Module, Patches, Count))
        {
            Status = ZpRdp_RestorePatches(&Module, Patches, Count);
            if (NT_SUCCESS(Status)) ZpRdpPatchBackup.Valid = FALSE;
        }
        else if (ZpRdp_AreAnyCodePatchesApplied(Patches, Count, Map.BaseAddress))
        {
            RestartService = TRUE;
        }
        else
        {
            ZpRdpPatchBackup.Valid = FALSE;
        }
    }
    if (Suspended)
    {
        ResumeStatus = NtResumeProcess(Module.Process);
        if (NT_SUCCESS(Status)) Status = ResumeStatus;
    }
    if (Module.Process != NULL) NtClose(Module.Process);
    if (RestorePrivilege)
    {
        RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, FALSE, FALSE, &PreviousPrivilege);
    }
    IO_UnmapFile(&Map);
    if (NT_SUCCESS(Status) && RestartService)
    {
        Status = ZpRdp_RestartService();
        if (NT_SUCCESS(Status)) ZpRdpPatchBackup.Valid = FALSE;
    }
    RtlReleaseSRWLockExclusive(&ZpRdpPatchLock);
    return ZpStatus_FromNtStatus(Status);
}
