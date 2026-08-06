#include <psapi.h>

#pragma comment(lib, "Psapi.lib")

static
NTSTATUS
ZpClientStatus_Add(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PCWSTR Group,
    _In_ PCWSTR Identity,
    _In_opt_ PCWSTR Value,
    _In_ ULONGLONG NumericValue)
{
    return ZpAdministration_AddRecord(Builder,
                                       ZpAdministrationKindClientStatus,
                                       0,
                                       0,
                                       NumericValue,
                                       Identity,
                                       NULL,
                                       Group,
                                       Value);
}

static
NTSTATUS
ZpClientStatus_AddNumber(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PCWSTR Group,
    _In_ PCWSTR Identity,
    _In_ ULONGLONG Value)
{
    return ZpClientStatus_Add(Builder, Group, Identity, NULL, Value);
}

static
NTSTATUS
ZpClientStatus_AddEnvironment(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    PWSTR Environment, Entry, Separator;
    WCHAR Name[32768];
    NTSTATUS Status = STATUS_SUCCESS;
    SIZE_T Length;

    Environment = GetEnvironmentStringsW();
    if (Environment == NULL) return NTSTATUS_FROM_WIN32(GetLastError());
    for (Entry = Environment; *Entry != UNICODE_NULL && NT_SUCCESS(Status); Entry += wcslen(Entry) + 1)
    {
        Separator = wcschr(Entry + (*Entry == L'='), L'=');
        if (Separator == NULL) continue;
        Length = Separator - Entry;
        if (Length >= ARRAYSIZE(Name))
        {
            Status = STATUS_NAME_TOO_LONG;
            break;
        }
        RtlCopyMemory(Name, Entry, Length * sizeof(WCHAR));
        Name[Length] = UNICODE_NULL;
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindClientEnvironmentVariable,
                                             0,
                                             0,
                                             0,
                                             Name,
                                             NULL,
                                             L"environment",
                                             Separator + 1);
    }
    FreeEnvironmentStringsW(Environment);
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateClientStatus(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PROCESS_MEMORY_COUNTERS_EX Memory = { sizeof(Memory) };
    PROCESS_BASIC_INFORMATION Basic;
    IO_COUNTERS Io;
    PTOKEN_MANDATORY_LABEL Integrity = NULL;
    SID_IDENTIFIER_AUTHORITY Authority = SECURITY_NT_AUTHORITY;
    PSID Administrators = NULL;
    HANDLE Process = NtCurrentProcess(), Token = NULL, Parent = NULL;
    FILETIME CreateTime, ExitTime, KernelTime, UserTime;
    USEROBJECTFLAGS WindowStationFlags;
    WCHAR Buffer[32768], ParentPath[32768], Account[256];
    DWORD Length, SessionId, HandleCount, Priority;
    USHORT ProcessMachine, NativeMachine;
    BOOL IsAdministrator = FALSE;
    ULONG IntegrityLength = 0;
    NTSTATUS Status;

#define ZP_CLIENT_STATUS_ADD(Group, Identity, Value, NumericValue) \
    Status = ZpClientStatus_Add(&Builder, Group, Identity, Value, NumericValue)
#define ZP_CLIENT_STATUS_ADD_NUMBER(Group, Identity, Value) \
    Status = ZpClientStatus_AddNumber(&Builder, Group, Identity, Value)

    Status = STATUS_SUCCESS;
    ZP_CLIENT_STATUS_ADD_NUMBER(L"process", L"processId", GetCurrentProcessId());
    Length = GetModuleFileNameW(NULL, Buffer, ARRAYSIZE(Buffer));
    if (NT_SUCCESS(Status) && Length != 0 && Length < ARRAYSIZE(Buffer))
    {
        ZP_CLIENT_STATUS_ADD(L"process", L"imagePath", Buffer, 0);
    }
    if (NT_SUCCESS(Status)) ZP_CLIENT_STATUS_ADD(L"startup", L"commandLine", GetCommandLineW(), 0);
    Length = GetCurrentDirectoryW(ARRAYSIZE(Buffer), Buffer);
    if (NT_SUCCESS(Status) && Length != 0 && Length < ARRAYSIZE(Buffer))
    {
        ZP_CLIENT_STATUS_ADD(L"startup", L"workingDirectory", Buffer, 0);
    }
    Length = ARRAYSIZE(Account);
    if (NT_SUCCESS(Status) && GetUserNameW(Account, &Length))
    {
        ZP_CLIENT_STATUS_ADD(L"security", L"account", Account, 0);
    }
    if (NT_SUCCESS(Status) && ProcessIdToSessionId(GetCurrentProcessId(), &SessionId))
    {
        ZP_CLIENT_STATUS_ADD_NUMBER(L"session", L"sessionId", SessionId);
    }
    Length = sizeof(WindowStationFlags);
    if (NT_SUCCESS(Status) &&
        GetUserObjectInformationW(GetProcessWindowStation(),
                                  UOI_FLAGS,
                                  &WindowStationFlags,
                                  Length,
                                  &Length))
    {
        ZP_CLIENT_STATUS_ADD_NUMBER(L"session",
                                    L"interactive",
                                    BooleanFlagOn(WindowStationFlags.dwFlags, WSF_VISIBLE));
    }
    Length = sizeof(Buffer);
    if (NT_SUCCESS(Status) &&
        GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, Buffer, Length, &Length))
    {
        ZP_CLIENT_STATUS_ADD(L"session", L"windowStation", Buffer, 0);
    }
    Length = sizeof(Buffer);
    if (NT_SUCCESS(Status) &&
        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, Buffer, Length, &Length))
    {
        ZP_CLIENT_STATUS_ADD(L"session", L"desktop", Buffer, 0);
    }
    if (NT_SUCCESS(Status) && GetProcessTimes(Process, &CreateTime, &ExitTime, &KernelTime, &UserTime))
    {
        ZP_CLIENT_STATUS_ADD(L"startup",
                             L"startTime",
                             NULL,
                             ((ULONGLONG)CreateTime.dwHighDateTime << 32) | CreateTime.dwLowDateTime);
        if (NT_SUCCESS(Status))
        {
            ZP_CLIENT_STATUS_ADD_NUMBER(L"resources",
                                        L"kernelTime",
                                        ((ULONGLONG)KernelTime.dwHighDateTime << 32) | KernelTime.dwLowDateTime);
        }
        if (NT_SUCCESS(Status))
        {
            ZP_CLIENT_STATUS_ADD_NUMBER(L"resources",
                                        L"userTime",
                                        ((ULONGLONG)UserTime.dwHighDateTime << 32) | UserTime.dwLowDateTime);
        }
    }
    if (NT_SUCCESS(Status) && GetProcessMemoryInfo(Process, (PPROCESS_MEMORY_COUNTERS)&Memory, sizeof(Memory)))
    {
        ZP_CLIENT_STATUS_ADD_NUMBER(L"resources", L"workingSet", Memory.WorkingSetSize);
        if (NT_SUCCESS(Status))
            ZP_CLIENT_STATUS_ADD_NUMBER(L"resources", L"peakWorkingSet", Memory.PeakWorkingSetSize);
        if (NT_SUCCESS(Status)) ZP_CLIENT_STATUS_ADD_NUMBER(L"resources", L"privateBytes", Memory.PrivateUsage);
        if (NT_SUCCESS(Status)) ZP_CLIENT_STATUS_ADD_NUMBER(L"resources", L"pageFaults", Memory.PageFaultCount);
    }
    if (NT_SUCCESS(Status) && GetProcessIoCounters(Process, &Io))
    {
        ZP_CLIENT_STATUS_ADD_NUMBER(L"resources", L"readBytes", Io.ReadTransferCount);
        if (NT_SUCCESS(Status)) ZP_CLIENT_STATUS_ADD_NUMBER(L"resources", L"writeBytes", Io.WriteTransferCount);
        if (NT_SUCCESS(Status)) ZP_CLIENT_STATUS_ADD_NUMBER(L"resources", L"otherBytes", Io.OtherTransferCount);
    }
    if (NT_SUCCESS(Status) && GetProcessHandleCount(Process, &HandleCount))
        ZP_CLIENT_STATUS_ADD_NUMBER(L"resources", L"handleCount", HandleCount);
    Priority = GetPriorityClass(Process);
    if (NT_SUCCESS(Status) && Priority != 0)
        ZP_CLIENT_STATUS_ADD_NUMBER(L"process", L"priorityClass", Priority);
    if (NT_SUCCESS(Status) && IsWow64Process2(Process, &ProcessMachine, &NativeMachine))
    {
        // Keep the two IMAGE_FILE_MACHINE values locale-neutral for the management UI.
        ZP_CLIENT_STATUS_ADD_NUMBER(L"process",
                                    L"machine",
                                    ((ULONGLONG)ProcessMachine << 16) | NativeMachine);
    }
    if (NT_SUCCESS(Status) &&
        NT_SUCCESS(NtQueryInformationProcess(Process, ProcessBasicInformation, &Basic, sizeof(Basic), NULL)))
    {
        ZP_CLIENT_STATUS_ADD_NUMBER(L"startup",
                                    L"parentProcessId",
                                    HandleToUlong(Basic.InheritedFromUniqueProcessId));
        if (NT_SUCCESS(Status))
        {
            Parent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE,
                                 HandleToUlong(Basic.InheritedFromUniqueProcessId));
            if (Parent != NULL)
            {
                Length = ARRAYSIZE(ParentPath);
                if (QueryFullProcessImageNameW(Parent, 0, ParentPath, &Length))
                    ZP_CLIENT_STATUS_ADD(L"startup", L"parentImagePath", ParentPath, 0);
                CloseHandle(Parent);
            }
        }
    }
    if (NT_SUCCESS(Status)) (VOID)OpenProcessToken(Process, TOKEN_QUERY, &Token);
    if (NT_SUCCESS(Status) && Token != NULL)
        AllocateAndInitializeSid(&Authority,
                                 2,
                                 SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 &Administrators);
    if (NT_SUCCESS(Status) && Administrators != NULL &&
        CheckTokenMembership(NULL, Administrators, &IsAdministrator))
    {
        ZP_CLIENT_STATUS_ADD_NUMBER(L"security", L"administrator", IsAdministrator != FALSE);
    }
    if (Administrators != NULL) FreeSid(Administrators);
    if (NT_SUCCESS(Status) && Token != NULL &&
        !GetTokenInformation(Token, TokenIntegrityLevel, NULL, 0, &IntegrityLength) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        IntegrityLength = 0;
    }
    if (NT_SUCCESS(Status) && IntegrityLength != 0)
    {
        Integrity = Mem_Alloc(IntegrityLength);
        if (Integrity == NULL)
        {
            Status = STATUS_NO_MEMORY;
        }
        else if (!GetTokenInformation(Token, TokenIntegrityLevel, Integrity, IntegrityLength, &IntegrityLength))
        {
            Mem_Free(Integrity);
            Integrity = NULL;
        }
    }
    if (NT_SUCCESS(Status) && Integrity != NULL)
    {
        ZP_CLIENT_STATUS_ADD_NUMBER(
            L"security",
            L"integrityLevel",
            *GetSidSubAuthority(Integrity->Label.Sid, *GetSidSubAuthorityCount(Integrity->Label.Sid) - 1));
    }
    Mem_Free(Integrity);
    if (Token != NULL) CloseHandle(Token);
    if (NT_SUCCESS(Status)) Status = ZpClientStatus_AddEnvironment(&Builder);
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
#undef ZP_CLIENT_STATUS_ADD_NUMBER
#undef ZP_CLIENT_STATUS_ADD
    return ZpStatus_FromNtStatus(Status);
}
