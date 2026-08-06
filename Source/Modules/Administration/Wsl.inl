#define ZP_WSL_LIST_OUTPUT_MAX_SIZE 0x00040000
#define ZP_WSL_PROCESS_OUTPUT_MAX_SIZE 0x00800000

#define ZP_WSL_DISTRIBUTION_NAME_MAX_LENGTH 256
#define ZP_WSL_REGISTRATION_NAME_MAX_LENGTH 64
#define ZP_WSL_DISTRIBUTION_STATE_INSTALLED 1
#define ZP_WSL_DISTRIBUTION_STATE_RUNNING 2

#include "ProcessCapture.h"

typedef HRESULT (WINAPI *ZP_WSL_LAUNCH)(
    PCWSTR,
    PCWSTR,
    BOOL,
    HANDLE,
    HANDLE,
    HANDLE,
    PHANDLE);

typedef struct _ZP_WSL_DISTRIBUTION
{
    PWSTR RegistrationName;
    PWSTR Name;
    ULONG Version;
    ULONG DefaultUid;
    ULONG Flags;
    ULONG State;
    LOGICAL Default;
} ZP_WSL_DISTRIBUTION, *PZP_WSL_DISTRIBUTION;

static
NTSTATUS
ZpWsl_OpenRegistry(
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE Key)
{
    static const WCHAR KeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Lxss";
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE UserKey;
    NTSTATUS Status;

    Status = RtlOpenCurrentUser(KEY_READ, &UserKey);
    if (!NT_SUCCESS(Status)) return Status;
    RtlInitUnicodeString(&Name, KeyPath);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, UserKey, NULL);
    Status = NtOpenKey(Key, DesiredAccess, &ObjectAttributes);
    NtClose(UserKey);
    return Status;
}

static
NTSTATUS
ZpWsl_QueryRegistryDword(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _Out_ PULONG Value)
{
    BYTE Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    UNICODE_STRING ValueName;
    ULONG Length;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    Status = NtQueryValueKey(Key,
                             &ValueName,
                             KeyValuePartialInformation,
                             Information,
                             sizeof(Buffer),
                             &Length);
    if (!NT_SUCCESS(Status)) return Status;
    if (Information->Type != REG_DWORD || Information->DataLength != sizeof(ULONG)) return STATUS_DATA_ERROR;
    RtlCopyMemory(Value, Information->Data, sizeof(ULONG));
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpWsl_QueryRegistryString(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _In_ ULONG MaximumLength,
    _Outptr_result_z_ PWSTR* Value)
{
    PKEY_VALUE_PARTIAL_INFORMATION Information;
    UNICODE_STRING ValueName;
    ULONG Length, Characters;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    Status = NtQueryValueKey(Key, &ValueName, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
    if (Length < UFIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(WCHAR) ||
        Length > UFIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) +
                     ((SIZE_T)MaximumLength + 1) * sizeof(WCHAR))
    {
        return STATUS_DATA_ERROR;
    }
    Information = Mem_Alloc(Length);
    if (Information == NULL) return STATUS_NO_MEMORY;
    Status = NtQueryValueKey(Key,
                             &ValueName,
                             KeyValuePartialInformation,
                             Information,
                             Length,
                             &Length);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Information);
        return Status;
    }
    Characters = Information->DataLength / sizeof(WCHAR);
    if (Information->Type != REG_SZ ||
        Length < UFIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) ||
        Information->DataLength < sizeof(WCHAR) ||
        (Information->DataLength & (sizeof(WCHAR) - 1)) != 0 ||
        Information->DataLength > Length - UFIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) ||
        ((PCWSTR)Information->Data)[Characters - 1] != UNICODE_NULL ||
        wcslen((PCWSTR)Information->Data) != Characters - 1)
    {
        Mem_Free(Information);
        return STATUS_DATA_ERROR;
    }
    RtlMoveMemory(Information, Information->Data, Information->DataLength);
    *Value = (PWSTR)Information;
    return STATUS_SUCCESS;
}

static
VOID
ZpWsl_FreeDistributions(
    _In_reads_(Count) PZP_WSL_DISTRIBUTION Distributions,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        Mem_Free(Distributions[Index].RegistrationName);
        Mem_Free(Distributions[Index].Name);
    }
    Mem_Free(Distributions);
}

static
NTSTATUS
ZpWsl_EnumerateRegistryDistributions(
    _Outptr_result_buffer_maybenull_(*Count) PZP_WSL_DISTRIBUTION* Distributions,
    _Out_ PULONG Count)
{
    KEY_CACHED_INFORMATION CachedInformation;
    PKEY_BASIC_INFORMATION KeyInformation;
    PZP_WSL_DISTRIBUTION Result;
    UNICODE_STRING Name, DefaultName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Key, DistributionKey;
    PWSTR DefaultDistribution, RegistrationName, DisplayName;
    ULONG BufferLength, InformationLength, Index, ResultCount = 0;
    NTSTATUS Status;
    LOGICAL HasDefault;

    Status = ZpWsl_OpenRegistry(KEY_READ, &Key);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND)
    {
        *Distributions = NULL;
        *Count = 0;
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryKey(Key,
                        KeyCachedInformation,
                        &CachedInformation,
                        sizeof(CachedInformation),
                        &InformationLength);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Key);
        return Status;
    }
    if (CachedInformation.SubKeys == 0)
    {
        NtClose(Key);
        *Distributions = NULL;
        *Count = 0;
        return STATUS_SUCCESS;
    }
    if (CachedInformation.SubKeys > ZP_CODEC_MAX_ELEMENT_COUNT ||
        CachedInformation.MaxNameLength > ZP_WSL_REGISTRATION_NAME_MAX_LENGTH * sizeof(WCHAR))
    {
        NtClose(Key);
        return STATUS_QUOTA_EXCEEDED;
    }
    Status = ZpWsl_QueryRegistryString(Key,
                                       L"DefaultDistribution",
                                       ZP_WSL_REGISTRATION_NAME_MAX_LENGTH,
                                       &DefaultDistribution);
    HasDefault = NT_SUCCESS(Status);
    if (!HasDefault && Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        NtClose(Key);
        return Status;
    }
    if (HasDefault) RtlInitUnicodeString(&DefaultName, DefaultDistribution);
    Result = Mem_Alloc((SIZE_T)CachedInformation.SubKeys * sizeof(*Result));
    if (Result == NULL)
    {
        if (HasDefault) Mem_Free(DefaultDistribution);
        NtClose(Key);
        return STATUS_NO_MEMORY;
    }
    BufferLength = FIELD_OFFSET(KEY_BASIC_INFORMATION, Name) + CachedInformation.MaxNameLength;
    KeyInformation = Mem_Alloc(BufferLength);
    if (KeyInformation == NULL)
    {
        Mem_Free(Result);
        if (HasDefault) Mem_Free(DefaultDistribution);
        NtClose(Key);
        return STATUS_NO_MEMORY;
    }
    for (Index = 0; Index < CachedInformation.SubKeys; Index++)
    {
        Status = NtEnumerateKey(Key,
                                Index,
                                KeyBasicInformation,
                                KeyInformation,
                                BufferLength,
                                &InformationLength);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status)) break;
        Name.Buffer = KeyInformation->Name;
        Name.Length = (USHORT)KeyInformation->NameLength;
        Name.MaximumLength = Name.Length;
        InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, Key, NULL);
        Status = NtOpenKey(&DistributionKey, KEY_QUERY_VALUE, &ObjectAttributes);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_KEY_DELETED)
        {
            Status = STATUS_SUCCESS;
            continue;
        }
        if (!NT_SUCCESS(Status)) break;
        Status = ZpWsl_QueryRegistryDword(DistributionKey, L"State", &Result[ResultCount].State);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND ||
            (NT_SUCCESS(Status) &&
             Result[ResultCount].State != ZP_WSL_DISTRIBUTION_STATE_INSTALLED &&
             Result[ResultCount].State != ZP_WSL_DISTRIBUTION_STATE_RUNNING))
        {
            NtClose(DistributionKey);
            Status = STATUS_SUCCESS;
            continue;
        }
        if (!NT_SUCCESS(Status))
        {
            NtClose(DistributionKey);
            break;
        }
        Status = ZpWsl_QueryRegistryString(DistributionKey,
                                           L"DistributionName",
                                           ZP_WSL_DISTRIBUTION_NAME_MAX_LENGTH,
                                           &DisplayName);
        if (!NT_SUCCESS(Status))
        {
            NtClose(DistributionKey);
            break;
        }
        Status = ZpWsl_QueryRegistryDword(DistributionKey, L"Version", &Result[ResultCount].Version);
        if (NT_SUCCESS(Status))
            Status = ZpWsl_QueryRegistryDword(DistributionKey, L"DefaultUid", &Result[ResultCount].DefaultUid);
        if (NT_SUCCESS(Status))
            Status = ZpWsl_QueryRegistryDword(DistributionKey, L"Flags", &Result[ResultCount].Flags);
        NtClose(DistributionKey);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(DisplayName);
            break;
        }
        RegistrationName = Mem_Alloc((SIZE_T)Name.Length + sizeof(WCHAR));
        if (RegistrationName == NULL)
        {
            Mem_Free(DisplayName);
            Status = STATUS_NO_MEMORY;
            break;
        }
        RtlCopyMemory(RegistrationName, Name.Buffer, Name.Length);
        RegistrationName[Name.Length / sizeof(WCHAR)] = UNICODE_NULL;
        Result[ResultCount].RegistrationName = RegistrationName;
        Result[ResultCount].Name = DisplayName;
        Result[ResultCount].Default = HasDefault && RtlEqualUnicodeString(&Name, &DefaultName, TRUE);
        ResultCount++;
    }
    Mem_Free(KeyInformation);
    if (HasDefault) Mem_Free(DefaultDistribution);
    NtClose(Key);
    if (!NT_SUCCESS(Status))
    {
        ZpWsl_FreeDistributions(Result, ResultCount);
        return Status;
    }
    if (ResultCount == 0)
    {
        Mem_Free(Result);
        Result = NULL;
    }
    *Distributions = Result;
    *Count = ResultCount;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpWsl_Run(
    _In_reads_(ArgumentCount) PCWSTR const* Arguments,
    _In_ ULONG ArgumentCount,
    _In_ ULONG MaximumOutputLength,
    _Outptr_result_bytebuffer_maybenull_(*OutputLength) PBYTE* Output,
    _Out_ PULONG OutputLength,
    _Out_ PULONG ExitCode)
{
    WCHAR Application[MAX_PATH];
    ULONG ApplicationLength;

    ApplicationLength = GetSystemDirectoryW(Application, ARRAYSIZE(Application));
    if (ApplicationLength == 0) return NTSTATUS_FROM_WIN32(GetLastError());
    if (ApplicationLength >= ARRAYSIZE(Application) - RTL_NUMBER_OF(L"\\wsl.exe")) return STATUS_NAME_TOO_LONG;
    wcscpy_s(Application + ApplicationLength,
             ARRAYSIZE(Application) - ApplicationLength,
             L"\\wsl.exe");
    return ZpAdministration_RunProcess(Application,
                                       Arguments,
                                       ArgumentCount,
                                       MaximumOutputLength,
                                       INFINITE,
                                       TRUE,
                                       Output,
                                       OutputLength,
                                       ExitCode);
}
static
NTSTATUS
ZpWsl_TerminatedUtf8Text(
    _Inout_updates_bytes_(*Length) PBYTE Buffer,
    _Inout_ PULONG Length,
    _Outptr_ PWSTR* Text)
{
    PWSTR Value;
    ULONG Characters;

    Characters = MultiByteToWideChar(CP_UTF8,
                                     MB_ERR_INVALID_CHARS,
                                     (PCCH)Buffer,
                                     *Length,
                                     NULL,
                                     0);
    if (Characters == 0)
    {
        Mem_Free(Buffer);
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    Value = Mem_Alloc(((SIZE_T)Characters + 1) * sizeof(WCHAR));
    if (Value == NULL)
    {
        Mem_Free(Buffer);
        return STATUS_NO_MEMORY;
    }
    if (MultiByteToWideChar(CP_UTF8,
                            MB_ERR_INVALID_CHARS,
                            (PCCH)Buffer,
                            *Length,
                            Value,
                            Characters) != (INT)Characters)
    {
        Mem_Free(Value);
        Mem_Free(Buffer);
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    Mem_Free(Buffer);
    Value[Characters] = UNICODE_NULL;
    *Text = Value;
    *Length = Characters;
    return STATUS_SUCCESS;
}

static
PWSTR
ZpWsl_NextLine(
    _Inout_ PWSTR* Cursor)
{
    PWSTR Line = *Cursor, End;

    while (*Line == L'\r' || *Line == L'\n') Line++;
    if (*Line == UNICODE_NULL)
    {
        *Cursor = Line;
        return NULL;
    }
    End = Line;
    while (*End != UNICODE_NULL && *End != L'\r' && *End != L'\n') End++;
    if (*End != UNICODE_NULL) *End++ = UNICODE_NULL;
    *Cursor = End;
    return Line;
}

static
ZP_STATUS
ZpAdministration_EnumerateWslDistributions(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PZP_WSL_DISTRIBUTION Distributions;
    ULONG Count, Index;
    NTSTATUS Status;

    Status = ZpWsl_EnumerateRegistryDistributions(&Distributions, &Count);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    for (Index = 0; Index < Count; Index++)
    {
        Status = ZpAdministration_AddRecord(&Builder,
                                             ZpAdministrationKindWslDistribution,
                                             Distributions[Index].State ==
                                                 ZP_WSL_DISTRIBUTION_STATE_RUNNING,
                                             Distributions[Index].Flags |
                                                 (Distributions[Index].Default ?
                                                      ZP_ADMINISTRATION_WSL_FLAG_DEFAULT : 0),
                                             ((ULONGLONG)Distributions[Index].Version << 32) |
                                                 Distributions[Index].DefaultUid,
                                             Distributions[Index].Name,
                                             NULL,
                                             NULL,
                                             NULL);
        if (!NT_SUCCESS(Status)) break;
    }
    ZpWsl_FreeDistributions(Distributions, Count);
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpWsl_RunStatus(
    _In_reads_(ArgumentCount) PCWSTR const* Arguments,
    _In_ ULONG ArgumentCount)
{
    PBYTE Output;
    ULONG OutputLength, ExitCode;
    NTSTATUS Status = ZpWsl_Run(Arguments,
                                ArgumentCount,
                                ZP_WSL_LIST_OUTPUT_MAX_SIZE,
                                &Output,
                                &OutputLength,
                                &ExitCode);

    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Mem_Free(Output);
    return ExitCode == ERROR_SUCCESS ?
               ZpStatus_Make(ZpStatusNone, 0) :
               ZpStatus_FromCode(ZpStatusProcessExit, ExitCode);
}

static
ZP_STATUS
ZpWsl_StartDistribution(
    _In_ PCWSTR Name)
{
    HANDLE NullHandle, Process;
    HMODULE Module;
    ZP_WSL_LAUNCH Launch;
    HRESULT Result;

    Module = LoadLibraryExW(L"wslapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    Launch = Module == NULL ? NULL : (ZP_WSL_LAUNCH)GetProcAddress(Module, "WslLaunch");
    if (Launch == NULL)
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        if (Module != NULL) FreeLibrary(Module);
        return ZpStatus_FromCode(ZpStatusWin32, HRESULT_CODE(Result));
    }
    NullHandle = CreateFileW(L"NUL",
                             GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
    if (NullHandle == INVALID_HANDLE_VALUE)
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        FreeLibrary(Module);
        return ZpStatus_FromCode(ZpStatusWin32, HRESULT_CODE(Result));
    }
    Result = Launch(Name, L"sleep infinity", FALSE, NullHandle, NullHandle, NullHandle, &Process);
    CloseHandle(NullHandle);
    FreeLibrary(Module);
    if (FAILED(Result)) return ZpStatus_FromCode(ZpStatusHResult, Result);
    CloseHandle(Process);
    return ZpStatus_Make(ZpStatusNone, 0);
}

static
ZP_STATUS
ZpWsl_SetDefaultDistribution(
    _In_ PCWSTR Name)
{
    PZP_WSL_DISTRIBUTION Distributions;
    UNICODE_STRING ValueName;
    HANDLE Key;
    ULONG Count, Index;
    NTSTATUS Status;

    Status = ZpWsl_EnumerateRegistryDistributions(&Distributions, &Count);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    for (Index = 0; Index < Count; Index++)
        if (_wcsicmp(Distributions[Index].Name, Name) == 0) break;
    if (Index == Count)
    {
        ZpWsl_FreeDistributions(Distributions, Count);
        return ZpStatus_FromNtStatus(STATUS_NOT_FOUND);
    }
    Status = ZpWsl_OpenRegistry(KEY_SET_VALUE, &Key);
    if (NT_SUCCESS(Status))
    {
        RtlInitUnicodeString(&ValueName, L"DefaultDistribution");
        Status = NtSetValueKey(Key,
                               &ValueName,
                               0,
                               REG_SZ,
                               Distributions[Index].RegistrationName,
                               ((ULONG)wcslen(Distributions[Index].RegistrationName) + 1) * sizeof(WCHAR));
        NtClose(Key);
    }
    ZpWsl_FreeDistributions(Distributions, Count);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_ControlWslDistribution(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PCWSTR Arguments[3];
    PWSTR Name = ZpAdministration_CopyView(&Control->Identity);
    ZP_STATUS Status;

    if (Name == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    if (Control->Argument.Length != 0 || Control->Secret.Length != 0)
    {
        Mem_Free(Name);
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    if (Control->Action == ZpAdministrationActionRun)
    {
        Status = ZpWsl_StartDistribution(Name);
    }
    else if (Control->Action == ZpAdministrationActionStop ||
             Control->Action == ZpAdministrationActionRestart)
    {
        Arguments[0] = L"--terminate";
        Arguments[1] = Name;
        Status = ZpWsl_RunStatus(Arguments, 2);
        if (ZpStatus_IsSuccess(Status) && Control->Action == ZpAdministrationActionRestart)
            Status = ZpWsl_StartDistribution(Name);
    }
    else if (Control->Action == ZpAdministrationActionActivate)
    {
        Status = ZpWsl_SetDefaultDistribution(Name);
    }
    else
    {
        Status = ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Mem_Free(Name);
    return Status;
}

static
PWSTR
ZpWsl_NextField(
    _Inout_ PWSTR* Cursor)
{
    PWSTR Field = *Cursor;

    while (*Field == L' ' || *Field == L'\t') Field++;
    if (*Field == UNICODE_NULL) return NULL;
    *Cursor = Field;
    while (**Cursor != UNICODE_NULL && **Cursor != L' ' && **Cursor != L'\t') (*Cursor)++;
    if (**Cursor != UNICODE_NULL) *(*Cursor)++ = UNICODE_NULL;
    return Field;
}

static
LOGICAL
ZpWsl_ParseNumber(
    _In_ PCWSTR Text,
    _Out_ PULONG Value)
{
    PWSTR End;
    ULONG Result = wcstoul(Text, &End, 10);

    if (*Text == UNICODE_NULL || *End != UNICODE_NULL) return FALSE;
    *Value = Result;
    return TRUE;
}

static
NTSTATUS
ZpWsl_AddProcesses(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PCWSTR Distribution,
    _Inout_ PWSTR Output)
{
    PWSTR Cursor = Output, Line, FieldCursor, Fields[11], Arguments, Identity, Start;
    ULONG FieldIndex, ProcessId, ParentProcessId, UserId, Elapsed, IdentityLength;
    NTSTATUS Status = STATUS_SUCCESS;

    while ((Line = ZpWsl_NextLine(&Cursor)) != NULL)
    {
        FieldCursor = Line;
        for (FieldIndex = 0; FieldIndex < ARRAYSIZE(Fields); FieldIndex++)
        {
            Fields[FieldIndex] = ZpWsl_NextField(&FieldCursor);
            if (Fields[FieldIndex] == NULL) break;
        }
        if (FieldIndex != ARRAYSIZE(Fields) ||
            !ZpWsl_ParseNumber(Fields[0], &ProcessId) ||
            !ZpWsl_ParseNumber(Fields[1], &ParentProcessId) ||
            !ZpWsl_ParseNumber(Fields[2], &UserId) ||
            !ZpWsl_ParseNumber(Fields[4], &Elapsed) || wcscmp(Fields[10], L"ps") == 0)
        {
            continue;
        }
        while (*FieldCursor == L' ' || *FieldCursor == L'\t') FieldCursor++;
        Arguments = FieldCursor;
        Start = Fields[5];
        Fields[5][wcslen(Fields[5])] = L' ';
        Fields[6][wcslen(Fields[6])] = L' ';
        Fields[7][wcslen(Fields[7])] = L' ';
        Fields[8][wcslen(Fields[8])] = L' ';
        IdentityLength = (ULONG)wcslen(Distribution) + 1 + 10 + 1 + (ULONG)wcslen(Start) + 1;
        Identity = Mem_Alloc((SIZE_T)IdentityLength * sizeof(WCHAR));
        if (Identity == NULL) return STATUS_NO_MEMORY;
        _snwprintf_s(Identity,
                     IdentityLength,
                     _TRUNCATE,
                     L"%s\n%lu\n%s",
                     Distribution,
                     ProcessId,
                     Start);
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindWslProcess,
                                             Fields[3][0],
                                             UserId,
                                             ((ULONGLONG)Elapsed << 32) | ParentProcessId,
                                             Identity,
                                             Fields[10],
                                             Distribution,
                                             Arguments);
        Mem_Free(Identity);
        if (!NT_SUCCESS(Status)) break;
    }
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateWslProcesses(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    static PCWSTR const ProcessArguments[] = {
        L"--distribution", NULL, L"--exec", L"env", L"LC_ALL=C", L"ps", L"-ww", L"-eo",
        L"pid=,ppid=,uid=,stat=,etimes=,lstart=,comm=,args="
    };
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PZP_WSL_DISTRIBUTION Distributions;
    PCWSTR Arguments[ARRAYSIZE(ProcessArguments)];
    PBYTE Buffer;
    PWSTR Output;
    ULONG ArgumentIndex, DistributionIndex, DistributionCount, Length, ExitCode;
    NTSTATUS Status;

    Status = ZpWsl_EnumerateRegistryDistributions(&Distributions, &DistributionCount);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    for (ArgumentIndex = 0; ArgumentIndex < ARRAYSIZE(Arguments); ArgumentIndex++)
        Arguments[ArgumentIndex] = ProcessArguments[ArgumentIndex];
    for (DistributionIndex = 0; DistributionIndex < DistributionCount; DistributionIndex++)
    {
        if (Distributions[DistributionIndex].State != ZP_WSL_DISTRIBUTION_STATE_RUNNING) continue;
        Arguments[1] = Distributions[DistributionIndex].Name;
        Status = ZpWsl_Run(Arguments,
                           ARRAYSIZE(Arguments),
                           ZP_WSL_PROCESS_OUTPUT_MAX_SIZE,
                           &Buffer,
                           &Length,
                           &ExitCode);
        if (!NT_SUCCESS(Status)) break;
        if (ExitCode != ERROR_SUCCESS)
        {
            Mem_Free(Buffer);
            ZpWsl_FreeDistributions(Distributions, DistributionCount);
            ZpAdministration_FreeBuilder(&Builder);
            return ZpStatus_FromCode(ZpStatusProcessExit, ExitCode);
        }
        Status = ZpWsl_TerminatedUtf8Text(Buffer, &Length, &Output);
        if (!NT_SUCCESS(Status)) break;
        Status = ZpWsl_AddProcesses(&Builder, Distributions[DistributionIndex].Name, Output);
        Mem_Free(Output);
        if (!NT_SUCCESS(Status)) break;
    }
    ZpWsl_FreeDistributions(Distributions, DistributionCount);
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_ControlWslProcess(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    static PCWSTR const ScriptFormat =
        L"test \"$(LC_ALL=C ps -o lstart= -p \"$1\" | sed 's/^ *//;s/  */ /g')\" = \"$2\" && "
        L"kill -%s \"$1\"";
    PCWSTR Signal;
    PCWSTR Arguments[] = { L"--distribution", NULL, L"--exec", L"/bin/sh", L"-c", NULL, L"zpigeon", NULL, NULL };
    WCHAR Script[256];
    PWSTR Identity = ZpAdministration_CopyView(&Control->Identity), ProcessId, Start;
    ZP_STATUS Status;

    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    if (Control->Argument.Length != 0 || Control->Secret.Length != 0)
    {
        Mem_Free(Identity);
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    ProcessId = wcschr(Identity, L'\n');
    Start = ProcessId == NULL ? NULL : wcschr(ProcessId + 1, L'\n');
    if (ProcessId == NULL || Start == NULL)
    {
        Mem_Free(Identity);
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    *ProcessId++ = UNICODE_NULL;
    *Start++ = UNICODE_NULL;
    if (Control->Action == ZpAdministrationActionStop) Signal = L"TERM";
    else if (Control->Action == ZpAdministrationActionDisable) Signal = L"STOP";
    else if (Control->Action == ZpAdministrationActionEnable) Signal = L"CONT";
    else
    {
        Mem_Free(Identity);
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    _snwprintf_s(Script, ARRAYSIZE(Script), _TRUNCATE, ScriptFormat, Signal);
    Arguments[1] = Identity;
    Arguments[5] = Script;
    Arguments[7] = ProcessId;
    Arguments[8] = Start;
    Status = ZpWsl_RunStatus(Arguments, ARRAYSIZE(Arguments));
    Mem_Free(Identity);
    return Status;
}
