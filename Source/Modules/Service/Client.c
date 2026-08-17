#include "Client.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Core/Account.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>
#include <KNSoft/MakeLifeEasier/Process/Environment.h>
#include <KNSoft/MakeLifeEasier/Process/Token.h>
#include <KNSoft/MakeLifeEasier/System/Registry.h>

#include <Shlwapi.h>
#include <Winsvc.h>

#pragma comment(lib, "Shlwapi.lib")

typedef struct _ZP_SERVICE_RECORD_ALLOCATION
{
    LPQUERY_SERVICE_CONFIGW Config;
    PUNICODE_STRING Description;
} ZP_SERVICE_RECORD_ALLOCATION, *PZP_SERVICE_RECORD_ALLOCATION;

typedef struct _ZP_SERVICE_USER_CONTEXT
{
    ULONGLONG Value;
    PUNICODE_STRING AccountName;
} ZP_SERVICE_USER_CONTEXT, *PZP_SERVICE_USER_CONTEXT;

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
LOGICAL
ZpService_ParseUserContext(
    _In_reads_(NameLength) PCWCHAR Name,
    _In_ ULONG NameLength,
    _Out_ PULONGLONG Context)
{
    ULONGLONG Value = 0;
    ULONG Index, Digit;

    Index = NameLength;
    while (Index != 0 && Name[Index - 1] != L'_')
    {
        Index--;
    }
    if (Index == 0 || Index == NameLength)
    {
        return FALSE;
    }
    for (; Index < NameLength; Index++)
    {
        if (Name[Index] >= L'0' && Name[Index] <= L'9')
        {
            Digit = Name[Index] - L'0';
        }
        else if (Name[Index] >= L'a' && Name[Index] <= L'f')
        {
            Digit = Name[Index] - L'a' + 10;
        }
        else if (Name[Index] >= L'A' && Name[Index] <= L'F')
        {
            Digit = Name[Index] - L'A' + 10;
        }
        else
        {
            return FALSE;
        }
        if (Value > (MAXULONGLONG - Digit) / 16)
        {
            return FALSE;
        }
        Value = Value * 16 + Digit;
    }
    if (Value == 0)
    {
        return FALSE;
    }
    *Context = Value;
    return TRUE;
}

static
PUNICODE_STRING
ZpService_QueryUserContextAccount(
    _In_ ULONGLONG Context)
{
    PUNICODE_STRING AccountName = NULL;
    HANDLE Token;
    HRESULT Result;

    Result = UMgrQueryUserToken(Context, &Token);
    if (SUCCEEDED(Result))
    {
        ZpAccount_QueryTokenName(Token, &AccountName);
        NtClose(Token);
    }
    return AccountName;
}

static
PUNICODE_STRING
ZpService_GetUserContextAccount(
    _In_reads_(ServiceNameLength) PCWCHAR ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG ServiceType,
    _Inout_updates_(ContextCapacity) PZP_SERVICE_USER_CONTEXT Contexts,
    _In_ ULONG ContextCapacity,
    _Inout_ PULONG ContextCount)
{
    ULONGLONG Context;
    ULONG Index;

    if (!(ServiceType & SERVICE_USERSERVICE_INSTANCE) ||
        !ZpService_ParseUserContext(ServiceName, ServiceNameLength, &Context))
    {
        return NULL;
    }
    for (Index = 0; Index < *ContextCount; Index++)
    {
        if (Contexts[Index].Value == Context)
        {
            return Contexts[Index].AccountName;
        }
    }
    NT_ASSERT(*ContextCount < ContextCapacity);
    Contexts[*ContextCount].Value = Context;
    Contexts[*ContextCount].AccountName = ZpService_QueryUserContextAccount(Context);
    (*ContextCount)++;
    return Contexts[*ContextCount - 1].AccountName;
}

static
PUNICODE_STRING
ZpService_QueryDescription(
    _In_ PCWSTR ServiceName)
{
    static const WCHAR KeyPrefix[] = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\";
    static const UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"Description");
    PKEY_VALUE_PARTIAL_INFORMATION Data = NULL;
    PUNICODE_STRING Description = NULL, ResolvedDescription;
    UNICODE_STRING KeyName;
    HANDLE Key = NULL;
    PWCHAR KeyBuffer = NULL, Resolved = NULL;
    SIZE_T ServiceNameLength, KeyLength;
    ULONG DescriptionLength, ResolvedLength;

    ServiceNameLength = wcslen(ServiceName);
    KeyLength = ARRAYSIZE(KeyPrefix) - 1 + ServiceNameLength;
    if (KeyLength > (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR))
    {
        return NULL;
    }
    KeyBuffer = Mem_Alloc((KeyLength + 1) * sizeof(WCHAR));
    if (KeyBuffer == NULL)
    {
        return NULL;
    }
    RtlCopyMemory(KeyBuffer, KeyPrefix, sizeof(KeyPrefix) - sizeof(WCHAR));
    RtlCopyMemory(KeyBuffer + ARRAYSIZE(KeyPrefix) - 1,
                  ServiceName,
                  (ServiceNameLength + 1) * sizeof(WCHAR));
    RtlInitUnicodeString(&KeyName, KeyBuffer);
    if (!NT_SUCCESS(Sys_RegOpenKey(&Key, KEY_QUERY_VALUE, &KeyName)) ||
        !NT_SUCCESS(Sys_RegQueryData(Key, &ValueName, &Data)) ||
        (Data->Type != REG_SZ && Data->Type != REG_EXPAND_SZ) ||
        Data->DataLength % sizeof(WCHAR) != 0)
    {
        goto Cleanup;
    }
    DescriptionLength = Data->DataLength / sizeof(WCHAR);
    while (DescriptionLength != 0 && ((PCWCHAR)Data->Data)[DescriptionLength - 1] == UNICODE_NULL)
    {
        DescriptionLength--;
    }
    if (DescriptionLength == 0 ||
        DescriptionLength > (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR))
    {
        goto Cleanup;
    }
    Description = NT_AllocStringW((USHORT)DescriptionLength);
    if (Description == NULL)
    {
        goto Cleanup;
    }
    RtlCopyMemory(Description->Buffer, Data->Data, Description->Length);
    Description->Buffer[DescriptionLength] = UNICODE_NULL;
    if (Description->Buffer[0] != L'@')
    {
        goto Cleanup;
    }
    Resolved = Mem_Alloc(32768 * sizeof(WCHAR));
    if (Resolved == NULL || FAILED(SHLoadIndirectString(Description->Buffer, Resolved, 32768, NULL)))
    {
        goto Cleanup;
    }
    ResolvedLength = (ULONG)wcslen(Resolved);
    if (ResolvedLength > (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR))
    {
        goto Cleanup;
    }
    ResolvedDescription = NT_AllocStringW((USHORT)ResolvedLength);
    if (ResolvedDescription != NULL)
    {
        RtlCopyMemory(ResolvedDescription->Buffer, Resolved, ResolvedDescription->Length);
        ResolvedDescription->Buffer[ResolvedLength] = UNICODE_NULL;
        Mem_Free(Description);
        Description = ResolvedDescription;
    }

Cleanup:
    Mem_Free(Resolved);
    Mem_Free(Data);
    if (Key != NULL)
    {
        NtClose(Key);
    }
    Mem_Free(KeyBuffer);
    return Description;
}

static
PUNICODE_STRING
ZpService_QueryServiceDll(
    _In_ PCWSTR ServiceName)
{
    static const WCHAR KeyPrefix[] = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\";
    static const WCHAR KeySuffix[] = L"\\Parameters";
    static const UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"ServiceDll");
    PKEY_VALUE_PARTIAL_INFORMATION Data = NULL;
    PUNICODE_STRING Value = NULL;
    UNICODE_STRING KeyName;
    HANDLE Key = NULL;
    PWCHAR KeyBuffer;
    SIZE_T ServiceNameLength, KeyLength;
    ULONG Length;

    ServiceNameLength = wcslen(ServiceName);
    KeyLength = ARRAYSIZE(KeyPrefix) - 1 + ServiceNameLength + ARRAYSIZE(KeySuffix) - 1;
    if (KeyLength > (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR))
    {
        return NULL;
    }
    KeyBuffer = Mem_Alloc((KeyLength + 1) * sizeof(WCHAR));
    if (KeyBuffer == NULL)
    {
        return NULL;
    }
    RtlCopyMemory(KeyBuffer, KeyPrefix, sizeof(KeyPrefix) - sizeof(WCHAR));
    RtlCopyMemory(KeyBuffer + ARRAYSIZE(KeyPrefix) - 1,
                  ServiceName,
                  ServiceNameLength * sizeof(WCHAR));
    RtlCopyMemory(KeyBuffer + ARRAYSIZE(KeyPrefix) - 1 + ServiceNameLength,
                  KeySuffix,
                  sizeof(KeySuffix));
    RtlInitUnicodeString(&KeyName, KeyBuffer);
    if (NT_SUCCESS(Sys_RegOpenKey(&Key, KEY_QUERY_VALUE, &KeyName)) &&
        NT_SUCCESS(Sys_RegQueryData(Key, &ValueName, &Data)) &&
        (Data->Type == REG_SZ || Data->Type == REG_EXPAND_SZ) &&
        Data->DataLength % sizeof(WCHAR) == 0)
    {
        Length = Data->DataLength / sizeof(WCHAR);
        while (Length != 0 && ((PCWCHAR)Data->Data)[Length - 1] == UNICODE_NULL)
        {
            Length--;
        }
        if (Length <= (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR))
        {
            Value = NT_AllocStringW((USHORT)Length);
            if (Value != NULL)
            {
                RtlCopyMemory(Value->Buffer, Data->Data, Value->Length);
                Value->Buffer[Length] = UNICODE_NULL;
            }
        }
    }
    Mem_Free(Data);
    if (Key != NULL)
    {
        NtClose(Key);
    }
    Mem_Free(KeyBuffer);
    return Value;
}

static
ZP_STATUS
ZpService_QueryConfig(
    _In_ SC_HANDLE Service,
    _Outptr_ LPQUERY_SERVICE_CONFIGW* Config)
{
    LPQUERY_SERVICE_CONFIGW Buffer;
    DWORD BytesNeeded, Error;

    if (QueryServiceConfigW(Service, NULL, 0, &BytesNeeded))
    {
        return ZpStatus_FromCode(ZpStatusWin32, ERROR_INVALID_DATA);
    }
    Error = GetLastError();
    if (Error != ERROR_INSUFFICIENT_BUFFER)
    {
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    Buffer = Mem_Alloc(BytesNeeded);
    if (Buffer == NULL)
    {
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    if (!QueryServiceConfigW(Service, Buffer, BytesNeeded, &BytesNeeded))
    {
        Error = GetLastError();
        Mem_Free(Buffer);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    *Config = Buffer;
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ZP_STATUS
ZpService_QueryConfig2(
    _In_ SC_HANDLE Service,
    _In_ DWORD Level,
    _Outptr_ PVOID* Info)
{
    PVOID Buffer;
    DWORD BytesNeeded, Error;

    if (QueryServiceConfig2W(Service, Level, NULL, 0, &BytesNeeded))
    {
        return ZpStatus_FromCode(ZpStatusWin32, ERROR_INVALID_DATA);
    }
    Error = GetLastError();
    if (Error != ERROR_INSUFFICIENT_BUFFER)
    {
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    Buffer = Mem_Alloc(BytesNeeded);
    if (Buffer == NULL)
    {
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    if (!QueryServiceConfig2W(Service, Level, Buffer, BytesNeeded, &BytesNeeded))
    {
        Error = GetLastError();
        Mem_Free(Buffer);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    *Info = Buffer;
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ULONG
ZpService_GetMultiStringLength(
    _In_opt_ PCWCHAR Strings)
{
    PCWCHAR Current = Strings;

    if (Current == NULL)
    {
        return 0;
    }
    while (*Current != UNICODE_NULL)
    {
        Current += wcslen(Current) + 1;
    }
    return (ULONG)(Current - Strings);
}

static
PUNICODE_STRING
ZpService_QueryDependents(
    _In_ SC_HANDLE Service)
{
    LPENUM_SERVICE_STATUSW Services;
    PUNICODE_STRING Names;
    DWORD BytesNeeded = 0, Count = 0;
    ULONG Index, Length = 0, Offset = 0;

    if (EnumDependentServicesW(Service, SERVICE_STATE_ALL, NULL, 0, &BytesNeeded, &Count) ||
        GetLastError() != ERROR_MORE_DATA)
    {
        return NULL;
    }
    Services = Mem_Alloc(BytesNeeded);
    if (Services == NULL)
    {
        return NULL;
    }
    if (!EnumDependentServicesW(Service,
                                SERVICE_STATE_ALL,
                                Services,
                                BytesNeeded,
                                &BytesNeeded,
                                &Count))
    {
        Mem_Free(Services);
        return NULL;
    }
    for (Index = 0; Index < Count; Index++)
    {
        Length += (ULONG)wcslen(Services[Index].lpServiceName) + 1;
    }
    Names = Length <= (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR) ?
                NT_AllocStringW((USHORT)Length) :
                NULL;
    if (Names != NULL)
    {
        for (Index = 0; Index < Count; Index++)
        {
            ULONG NameLength = (ULONG)wcslen(Services[Index].lpServiceName) + 1;
            RtlCopyMemory(Names->Buffer + Offset,
                          Services[Index].lpServiceName,
                          (SIZE_T)NameLength * sizeof(WCHAR));
            Offset += NameLength;
        }
        Names->Buffer[Length] = UNICODE_NULL;
    }
    Mem_Free(Services);
    return Names;
}

static
int
__cdecl
ZpService_CompareRecords(
    _In_ const VOID* Left,
    _In_ const VOID* Right)
{
    PCZP_SERVICE_RECORD LeftRecord = Left, RightRecord = Right;
    INT Result;

    Result = CompareStringOrdinal(LeftRecord->ServiceName,
                                  (INT)LeftRecord->ServiceNameLength,
                                  RightRecord->ServiceName,
                                  (INT)RightRecord->ServiceNameLength,
                                  TRUE);
    if (Result == CSTR_EQUAL)
    {
        Result = CompareStringOrdinal(LeftRecord->ServiceName,
                                      (INT)LeftRecord->ServiceNameLength,
                                      RightRecord->ServiceName,
                                      (INT)RightRecord->ServiceNameLength,
                                      FALSE);
    }
    return Result - CSTR_EQUAL;
}

static
ZP_STATUS
ZpService_Enumerate(
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    LPENUM_SERVICE_STATUS_PROCESSW Entries;
    PZP_SERVICE_RECORD_ALLOCATION Allocations = NULL;
    PZP_SERVICE_USER_CONTEXT UserContexts = NULL;
    PZP_SERVICE_RECORD Services = NULL;
    PUNICODE_STRING UserAccountName;
    SC_HANDLE Manager, Service;
    PBYTE Buffer = NULL, Result = NULL;
    DWORD BytesNeeded = 0, Count = 0, ResumeHandle = 0;
    NTSTATUS CodecStatus;
    ZP_STATUS Status = { 0 };
    ULONG Index, Length, UserContextCount = 0;

    Manager = OpenSCManagerW(NULL,
                             NULL,
                             SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    if (Manager == NULL)
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (!EnumServicesStatusExW(Manager,
                               SC_ENUM_PROCESS_INFO,
                               SERVICE_TYPE_ALL,
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
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
        Buffer = Mem_Alloc(BytesNeeded);
        if (Buffer == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            goto Cleanup;
        }
        ResumeHandle = 0;
        if (!EnumServicesStatusExW(Manager,
                                   SC_ENUM_PROCESS_INFO,
                                   SERVICE_TYPE_ALL,
                                   SERVICE_STATE_ALL,
                                   Buffer,
                                   BytesNeeded,
                                   &BytesNeeded,
                                   &Count,
                                   &ResumeHandle,
                                   NULL))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
    }
    Entries = (LPENUM_SERVICE_STATUS_PROCESSW)Buffer;
    Services = Count != 0 ? Mem_Alloc((SIZE_T)Count * sizeof(*Services)) : NULL;
    Allocations = Count != 0 ? Mem_Alloc((SIZE_T)Count * sizeof(*Allocations)) : NULL;
    UserContexts = Count != 0 ? Mem_Alloc((SIZE_T)Count * sizeof(*UserContexts)) : NULL;
    if (Count != 0 && (Services == NULL || Allocations == NULL || UserContexts == NULL))
    {
        Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto Cleanup;
    }
    if (Allocations != NULL)
    {
        RtlZeroMemory(Allocations, (SIZE_T)Count * sizeof(*Allocations));
    }
    for (Index = 0; Index < Count; Index++)
    {
        Services[Index].ServiceType = Entries[Index].ServiceStatusProcess.dwServiceType;
        Services[Index].CurrentState = Entries[Index].ServiceStatusProcess.dwCurrentState;
        Services[Index].ControlsAccepted = Entries[Index].ServiceStatusProcess.dwControlsAccepted;
        Services[Index].ProcessId = Entries[Index].ServiceStatusProcess.dwProcessId;
        Services[Index].StartType = SERVICE_NO_CHANGE;
        Services[Index].ServiceName = Entries[Index].lpServiceName;
        Services[Index].ServiceNameLength = (ULONG)wcslen(Entries[Index].lpServiceName);
        Services[Index].DisplayName = Entries[Index].lpDisplayName;
        Services[Index].DisplayNameLength = (ULONG)wcslen(Entries[Index].lpDisplayName);
        Allocations[Index].Description = ZpService_QueryDescription(Entries[Index].lpServiceName);
        Services[Index].Description = Allocations[Index].Description != NULL ?
                                          Allocations[Index].Description->Buffer :
                                          NULL;
        Services[Index].DescriptionLength = Allocations[Index].Description != NULL ?
                                                Allocations[Index].Description->Length / sizeof(WCHAR) :
                                                0;
        Services[Index].StartName = NULL;
        Services[Index].StartNameLength = 0;
        Service = OpenServiceW(Manager, Entries[Index].lpServiceName, SERVICE_QUERY_CONFIG);
        if (Service != NULL)
        {
            if (ZpStatus_IsSuccess(ZpService_QueryConfig(Service, &Allocations[Index].Config)))
            {
                Services[Index].StartType = Allocations[Index].Config->dwStartType;
                Services[Index].StartName = Allocations[Index].Config->lpServiceStartName;
                Services[Index].StartNameLength = Services[Index].StartName != NULL ?
                                                      (ULONG)wcslen(Services[Index].StartName) :
                                                      0;
            }
            CloseServiceHandle(Service);
        }
        UserAccountName = ZpService_GetUserContextAccount(Services[Index].ServiceName,
                                                          Services[Index].ServiceNameLength,
                                                          Services[Index].ServiceType,
                                                          UserContexts,
                                                          Count,
                                                          &UserContextCount);
        if (UserAccountName != NULL)
        {
            Services[Index].StartName = UserAccountName->Buffer;
            Services[Index].StartNameLength = UserAccountName->Length / sizeof(WCHAR);
        }
    }
    if (Count > 1)
    {
        qsort(Services, Count, sizeof(*Services), ZpService_CompareRecords);
    }
    CodecStatus = ZpService_EncodeList(Services, Count, NULL, 0, &Length);
    Result = NT_SUCCESS(CodecStatus) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(CodecStatus) && Result == NULL)
    {
        CodecStatus = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(CodecStatus))
    {
        CodecStatus = ZpService_EncodeList(Services,
                                           Count,
                                           Result,
                                           Length,
                                           &Length);
    }
    Status = ZpStatus_FromNtStatus(CodecStatus);

Cleanup:
    if (UserContexts != NULL)
    {
        for (Index = 0; Index < UserContextCount; Index++)
        {
            Mem_Free(UserContexts[Index].AccountName);
        }
        Mem_Free(UserContexts);
    }
    if (Allocations != NULL)
    {
        for (Index = 0; Index < Count; Index++)
        {
            Mem_Free(Allocations[Index].Description);
            Mem_Free(Allocations[Index].Config);
        }
        Mem_Free(Allocations);
    }
    if (Services != NULL)
    {
        Mem_Free(Services);
    }
    if (Buffer != NULL)
    {
        Mem_Free(Buffer);
    }
    CloseServiceHandle(Manager);
    if (!ZpStatus_IsSuccess(Status))
    {
        if (Result != NULL)
        {
            Mem_Free(Result);
        }
        return Status;
    }
    *Payload = Result;
    *PayloadLength = Length;
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ZP_STATUS
ZpService_Query(
    _In_ PCZP_STRING_VIEW Name,
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    SERVICE_STATUS_PROCESS ServiceStatus;
    LPQUERY_SERVICE_CONFIGW Config = NULL;
    LPSERVICE_FAILURE_ACTIONSW FailureActions = NULL;
    PUNICODE_STRING Dependents = NULL, Description = NULL, ServiceDll = NULL, UserAccountName = NULL;
    ZP_SERVICE_INFO Info;
    SC_HANDLE DependentService = NULL, Manager = NULL, Service = NULL;
    PWCHAR ServiceName;
    PBYTE Result = NULL;
    SERVICE_DELAYED_AUTO_START_INFO DelayedInfo;
    SERVICE_FAILURE_ACTIONS_FLAG FailureFlag;
    BOOLEAN RebootDelayFound = FALSE, RestartDelayFound = FALSE;
    DWORD BytesNeeded;
    ULONG Action, Index, LastAction, Length;
    ULONGLONG UserContext;
    NTSTATUS CodecStatus;
    ZP_STATUS Status = { 0 };

    ServiceName = ZpService_CopyName(Name);
    if (ServiceName == NULL)
    {
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (Manager == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    Service = OpenServiceW(Manager,
                           ServiceName,
                           SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (Service == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    if (!QueryServiceStatusEx(Service,
                              SC_STATUS_PROCESS_INFO,
                              (PBYTE)&ServiceStatus,
                              sizeof(ServiceStatus),
                              &BytesNeeded))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    Status = ZpService_QueryConfig(Service, &Config);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Cleanup;
    }
    Description = ZpService_QueryDescription(ServiceName);
    ServiceDll = ZpService_QueryServiceDll(ServiceName);
    DelayedInfo.fDelayedAutostart = FALSE;
    if ((ServiceStatus.dwServiceType & SERVICE_WIN32) &&
        !QueryServiceConfig2W(Service,
                              SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
                              (PBYTE)&DelayedInfo,
                              sizeof(DelayedInfo),
                              &BytesNeeded))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    DependentService = OpenServiceW(Manager, ServiceName, SERVICE_ENUMERATE_DEPENDENTS);
    if (DependentService != NULL)
    {
        Dependents = ZpService_QueryDependents(DependentService);
    }
    if ((ServiceStatus.dwServiceType & SERVICE_USERSERVICE_INSTANCE) &&
        ZpService_ParseUserContext(ServiceName, Name->Length, &UserContext))
    {
        UserAccountName = ZpService_QueryUserContextAccount(UserContext);
    }
    Info.RecoverySupported = (ServiceStatus.dwServiceType & SERVICE_WIN32) != 0 &&
                             !(ServiceStatus.dwServiceFlags & SERVICE_RUNS_IN_SYSTEM_PROCESS);
    Info.FailureActionsOnNonCrashFailures = FALSE;
    Info.RecoveryActionCount = 0;
    Info.ResetPeriodSeconds = 0;
    Info.RestartDelayMilliseconds = 0;
    Info.RebootDelayMilliseconds = 0;
    Info.FirstFailureAction = SC_ACTION_NONE;
    Info.SecondFailureAction = SC_ACTION_NONE;
    Info.ThirdFailureAction = SC_ACTION_NONE;
    Info.SubsequentFailureAction = SC_ACTION_NONE;
    Info.RebootMessage = NULL;
    Info.RebootMessageLength = 0;
    Info.RecoveryCommand = NULL;
    Info.RecoveryCommandLength = 0;
    if (Info.RecoverySupported)
    {
        Status = ZpService_QueryConfig2(Service,
                                        SERVICE_CONFIG_FAILURE_ACTIONS,
                                        (PVOID*)&FailureActions);
        if (!ZpStatus_IsSuccess(Status))
        {
            goto Cleanup;
        }
        if (FailureActions->cActions != 0 && FailureActions->lpsaActions == NULL)
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, ERROR_INVALID_DATA);
            goto Cleanup;
        }
        if (!QueryServiceConfig2W(Service,
                                  SERVICE_CONFIG_FAILURE_ACTIONS_FLAG,
                                  (PBYTE)&FailureFlag,
                                  sizeof(FailureFlag),
                                  &BytesNeeded))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
        Info.FailureActionsOnNonCrashFailures = FailureFlag.fFailureActionsOnNonCrashFailures;
        Info.RecoveryActionCount = FailureActions->cActions;
        Info.ResetPeriodSeconds = FailureActions->dwResetPeriod;
        Info.RebootMessage = FailureActions->lpRebootMsg;
        Info.RebootMessageLength = FailureActions->lpRebootMsg != NULL ?
                                       (ULONG)wcslen(FailureActions->lpRebootMsg) :
                                       0;
        Info.RecoveryCommand = FailureActions->lpCommand;
        Info.RecoveryCommandLength = FailureActions->lpCommand != NULL ?
                                         (ULONG)wcslen(FailureActions->lpCommand) :
                                         0;
        LastAction = SC_ACTION_NONE;
        for (Index = 0; Index < 4; Index++)
        {
            if (Index < FailureActions->cActions)
            {
                LastAction = FailureActions->lpsaActions[Index].Type;
            }
            switch (Index)
            {
                case 0:
                    Info.FirstFailureAction = LastAction;
                    break;

                case 1:
                    Info.SecondFailureAction = LastAction;
                    break;

                case 2:
                    Info.ThirdFailureAction = LastAction;
                    break;

                default:
                    Info.SubsequentFailureAction = LastAction;
                    break;
            }
        }
        for (Index = 0; Index < FailureActions->cActions; Index++)
        {
            Action = FailureActions->lpsaActions[Index].Type;
            if (Action == SC_ACTION_RESTART && !RestartDelayFound)
            {
                Info.RestartDelayMilliseconds = FailureActions->lpsaActions[Index].Delay;
                RestartDelayFound = TRUE;
            }
            else if (Action == SC_ACTION_REBOOT && !RebootDelayFound)
            {
                Info.RebootDelayMilliseconds = FailureActions->lpsaActions[Index].Delay;
                RebootDelayFound = TRUE;
            }
        }
    }
    Info.ServiceType = ServiceStatus.dwServiceType;
    Info.CurrentState = ServiceStatus.dwCurrentState;
    Info.ControlsAccepted = ServiceStatus.dwControlsAccepted;
    Info.ProcessId = ServiceStatus.dwProcessId;
    Info.StartType = Config->dwStartType;
    Info.ErrorControl = Config->dwErrorControl;
    Info.DelayedAutoStart = DelayedInfo.fDelayedAutostart;
    Info.ServiceFlags = ServiceStatus.dwServiceFlags;
    Info.ServiceName = ServiceName;
    Info.ServiceNameLength = Name->Length;
    Info.DisplayName = Config->lpDisplayName;
    Info.DisplayNameLength = Config->lpDisplayName != NULL ?
                                 (ULONG)wcslen(Config->lpDisplayName) :
                                 0;
    Info.Description = Description != NULL ? Description->Buffer : NULL;
    Info.DescriptionLength = Description != NULL ? Description->Length / sizeof(WCHAR) : 0;
    Info.BinaryPathName = Config->lpBinaryPathName;
    Info.BinaryPathNameLength = Config->lpBinaryPathName != NULL ?
                                    (ULONG)wcslen(Config->lpBinaryPathName) :
                                    0;
    Info.StartName = UserAccountName != NULL ? UserAccountName->Buffer : Config->lpServiceStartName;
    Info.StartNameLength = UserAccountName != NULL ?
                               UserAccountName->Length / sizeof(WCHAR) :
                               Config->lpServiceStartName != NULL ?
                                   (ULONG)wcslen(Config->lpServiceStartName) :
                                   0;
    Info.LoadOrderGroup = Config->lpLoadOrderGroup;
    Info.LoadOrderGroupLength = Config->lpLoadOrderGroup != NULL ?
                                    (ULONG)wcslen(Config->lpLoadOrderGroup) :
                                    0;
    Info.Dependencies = Config->lpDependencies;
    Info.DependenciesLength = ZpService_GetMultiStringLength(Config->lpDependencies);
    Info.Dependents = Dependents != NULL ? Dependents->Buffer : NULL;
    Info.DependentsLength = Dependents != NULL ? Dependents->Length / sizeof(WCHAR) : 0;
    Info.ServiceDll = ServiceDll != NULL ? ServiceDll->Buffer : NULL;
    Info.ServiceDllLength = ServiceDll != NULL ? ServiceDll->Length / sizeof(WCHAR) : 0;
    CodecStatus = ZpService_EncodeInfo(&Info, NULL, 0, &Length);
    Result = NT_SUCCESS(CodecStatus) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(CodecStatus) && Result == NULL)
    {
        CodecStatus = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(CodecStatus))
    {
        CodecStatus = ZpService_EncodeInfo(&Info, Result, Length, &Length);
    }
    Status = ZpStatus_FromNtStatus(CodecStatus);

Cleanup:
    Mem_Free(FailureActions);
    Mem_Free(ServiceDll);
    Mem_Free(Dependents);
    Mem_Free(UserAccountName);
    Mem_Free(Description);
    if (Config != NULL)
    {
        Mem_Free(Config);
    }
    if (Service != NULL)
    {
        CloseServiceHandle(Service);
    }
    if (DependentService != NULL)
    {
        CloseServiceHandle(DependentService);
    }
    if (Manager != NULL)
    {
        CloseServiceHandle(Manager);
    }
    Mem_Free(ServiceName);
    if (!ZpStatus_IsSuccess(Status))
    {
        if (Result != NULL)
        {
            Mem_Free(Result);
        }
        return Status;
    }
    *Payload = Result;
    *PayloadLength = Length;
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ZP_STATUS
ZpService_Configure(
    _In_ PCZP_SERVICE_CONFIG_VIEW Config)
{
    SC_HANDLE Manager = NULL, Service = NULL;
    PWCHAR BinaryPathName = NULL, Description = NULL, DisplayName = NULL;
    PWCHAR LoadOrderGroup = NULL, ServiceName = NULL;
    SERVICE_DELAYED_AUTO_START_INFO DelayedInfo;
    SERVICE_DESCRIPTIONW DescriptionInfo;
    SERVICE_STATUS_PROCESS ServiceStatus;
    DWORD BytesNeeded;
    ZP_STATUS Status = { 0 };

    ServiceName = ZpService_CopyName(&Config->ServiceName);
    DisplayName = ZpService_CopyName(&Config->DisplayName);
    Description = ZpService_CopyName(&Config->Description);
    BinaryPathName = ZpService_CopyName(&Config->BinaryPathName);
    LoadOrderGroup = ZpService_CopyName(&Config->LoadOrderGroup);
    if (ServiceName == NULL || DisplayName == NULL || Description == NULL ||
        BinaryPathName == NULL || LoadOrderGroup == NULL)
    {
        Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto Cleanup;
    }
    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (Manager == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    Service = OpenServiceW(Manager,
                           ServiceName,
                           SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS);
    if (Service == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    if (!QueryServiceStatusEx(Service,
                              SC_STATUS_PROCESS_INFO,
                              (PBYTE)&ServiceStatus,
                              sizeof(ServiceStatus),
                              &BytesNeeded))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    if (!ChangeServiceConfigW(Service,
                              SERVICE_NO_CHANGE,
                              Config->StartType,
                              SERVICE_NO_CHANGE,
                              BinaryPathName,
                              LoadOrderGroup,
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              DisplayName))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    DescriptionInfo.lpDescription = Description;
    if (!ChangeServiceConfig2W(Service, SERVICE_CONFIG_DESCRIPTION, &DescriptionInfo))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    if (ServiceStatus.dwServiceType & SERVICE_WIN32)
    {
        DelayedInfo.fDelayedAutostart = Config->DelayedAutoStart;
        if (!ChangeServiceConfig2W(Service,
                                   SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
                                   &DelayedInfo))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        }
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
    Mem_Free(LoadOrderGroup);
    Mem_Free(BinaryPathName);
    Mem_Free(Description);
    Mem_Free(DisplayName);
    Mem_Free(ServiceName);
    return Status;
}

static
ZP_STATUS
ZpService_ConfigureRecovery(
    _In_ PCZP_SERVICE_RECOVERY_CONFIG_VIEW Config)
{
    SC_ACTION Actions[4];
    SC_HANDLE Manager = NULL, Service = NULL;
    PWCHAR Command = NULL, RebootMessage = NULL, ServiceName = NULL;
    SERVICE_FAILURE_ACTIONSW FailureActions;
    SERVICE_FAILURE_ACTIONS_FLAG FailureFlag;
    SERVICE_STATUS_PROCESS ServiceStatus;
    DWORD Access = SERVICE_CHANGE_CONFIG, BytesNeeded;
    ULONG Index;
    ZP_STATUS Status = { 0 };

    ServiceName = ZpService_CopyName(&Config->ServiceName);
    RebootMessage = ZpService_CopyName(&Config->RebootMessage);
    Command = ZpService_CopyName(&Config->Command);
    if (ServiceName == NULL || RebootMessage == NULL || Command == NULL)
    {
        Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto Cleanup;
    }
    Actions[0].Type = Config->FirstFailureAction;
    Actions[1].Type = Config->SecondFailureAction;
    Actions[2].Type = Config->ThirdFailureAction;
    Actions[3].Type = Config->SubsequentFailureAction;
    for (Index = 0; Index < ARRAYSIZE(Actions); Index++)
    {
        if (Actions[Index].Type == SC_ACTION_RESTART ||
            Actions[Index].Type == SC_ACTION_OWN_RESTART)
        {
            Actions[Index].Delay = Config->RestartDelayMilliseconds;
            Access |= SERVICE_START;
        }
        else
        {
            Actions[Index].Delay = Actions[Index].Type == SC_ACTION_REBOOT ?
                                       Config->RebootDelayMilliseconds :
                                       0;
        }
    }
    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (Manager == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    Service = OpenServiceW(Manager, ServiceName, Access | SERVICE_QUERY_STATUS);
    if (Service == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    if (!QueryServiceStatusEx(Service,
                              SC_STATUS_PROCESS_INFO,
                              (PBYTE)&ServiceStatus,
                              sizeof(ServiceStatus),
                              &BytesNeeded))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    if (!(ServiceStatus.dwServiceType & SERVICE_WIN32) ||
        (ServiceStatus.dwServiceFlags & SERVICE_RUNS_IN_SYSTEM_PROCESS))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, ERROR_NOT_SUPPORTED);
        goto Cleanup;
    }
    if (!ChangeServiceConfigW(Service,
                              SERVICE_NO_CHANGE,
                              SERVICE_NO_CHANGE,
                              Config->ErrorControl,
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              NULL))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    FailureActions.dwResetPeriod = Config->ResetPeriodSeconds;
    FailureActions.lpRebootMsg = RebootMessage;
    FailureActions.lpCommand = Command;
    FailureActions.cActions = ARRAYSIZE(Actions);
    FailureActions.lpsaActions = Actions;
    if (!ChangeServiceConfig2W(Service, SERVICE_CONFIG_FAILURE_ACTIONS, &FailureActions))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    FailureFlag.fFailureActionsOnNonCrashFailures = Config->FailureActionsOnNonCrashFailures;
    if (!ChangeServiceConfig2W(Service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &FailureFlag))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
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
    Mem_Free(Command);
    Mem_Free(RebootMessage);
    Mem_Free(ServiceName);
    return Status;
}

static
ZP_STATUS
ZpService_ConfigureAccount(
    _In_ PCZP_SERVICE_ACCOUNT_CONFIG_VIEW Config)
{
    SC_HANDLE Manager = NULL, Service = NULL;
    PWCHAR Password = NULL, ServiceName = NULL, StartName = NULL;
    ZP_STATUS Status = { 0 };

    ServiceName = ZpService_CopyName(&Config->ServiceName);
    StartName = ZpService_CopyName(&Config->StartName);
    Password = Config->PasswordPresent ? ZpService_CopyName(&Config->Password) : NULL;
    if (ServiceName == NULL || StartName == NULL || (Config->PasswordPresent && Password == NULL))
    {
        Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto Cleanup;
    }
    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (Manager == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    Service = OpenServiceW(Manager, ServiceName, SERVICE_CHANGE_CONFIG);
    if (Service == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    if (!ChangeServiceConfigW(Service,
                              SERVICE_NO_CHANGE,
                              SERVICE_NO_CHANGE,
                              SERVICE_NO_CHANGE,
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              StartName,
                              Password,
                              NULL))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
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
    if (Password != NULL)
    {
        RtlSecureZeroMemory(Password, ((SIZE_T)Config->Password.Length + 1) * sizeof(WCHAR));
        Mem_Free(Password);
    }
    Mem_Free(StartName);
    Mem_Free(ServiceName);
    return Status;
}

static
ZP_STATUS
ZpService_Control(
    _In_ ULONG Control,
    _In_ PCZP_STRING_VIEW Name,
    _In_ PCZP_STRING_VIEW Argument)
{
    SERVICE_STATUS ServiceStatus;
    SERVICE_STATUS_PROCESS ProcessStatus;
    SC_HANDLE Manager = NULL, Service = NULL;
    PWSTR* Arguments = NULL;
    PWCHAR ArgumentBuffer = NULL, ServiceName;
    DWORD Access, BytesNeeded, Error;
    LARGE_INTEGER Delay;
    ULONG ArgumentCount = 0, Index;
    NTSTATUS ParseStatus;
    ZP_STATUS Status = { 0 };

    ServiceName = ZpService_CopyName(Name);
    if (ServiceName == NULL)
    {
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    if (Argument->Length != 0)
    {
        ArgumentBuffer = ZpService_CopyName(Argument);
        if (ArgumentBuffer == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            goto Cleanup;
        }
        ParseStatus = PS_CommandLineToArgvW(ArgumentBuffer,
                                            &ArgumentCount,
                                            &Arguments);
        if (!NT_SUCCESS(ParseStatus))
        {
            Status = ZpStatus_FromNtStatus(ParseStatus);
            goto Cleanup;
        }
    }
    switch (Control)
    {
        case ZP_SERVICE_CONTROL_START:
            Access = SERVICE_START;
            break;

        case ZP_SERVICE_CONTROL_STOP:
            Access = SERVICE_STOP;
            break;

        case ZP_SERVICE_CONTROL_PAUSE:
        case ZP_SERVICE_CONTROL_CONTINUE:
            Access = SERVICE_PAUSE_CONTINUE;
            break;

        case ZP_SERVICE_CONTROL_RESTART:
            Access = SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS;
            break;

        default:
            Status = ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
            goto Cleanup;
    }
    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (Manager == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    Service = OpenServiceW(Manager, ServiceName, Access);
    if (Service == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    if (Control == ZP_SERVICE_CONTROL_START)
    {
        if (!StartServiceW(Service, ArgumentCount, (PCWSTR*)Arguments))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        }
        goto Cleanup;
    }
    if (Control != ZP_SERVICE_CONTROL_RESTART)
    {
        if (!ControlService(Service,
                            Control == ZP_SERVICE_CONTROL_STOP ? SERVICE_CONTROL_STOP :
                            Control == ZP_SERVICE_CONTROL_PAUSE ? SERVICE_CONTROL_PAUSE :
                                                                 SERVICE_CONTROL_CONTINUE,
                            &ServiceStatus))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        }
        goto Cleanup;
    }
    if (!QueryServiceStatusEx(Service,
                              SC_STATUS_PROCESS_INFO,
                              (PBYTE)&ProcessStatus,
                              sizeof(ProcessStatus),
                              &BytesNeeded))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    if (ProcessStatus.dwCurrentState != SERVICE_STOPPED &&
        ProcessStatus.dwCurrentState != SERVICE_STOP_PENDING &&
        !ControlService(Service, SERVICE_CONTROL_STOP, &ServiceStatus))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    Delay.QuadPart = -100 * 10000;
    for (Index = 0; ProcessStatus.dwCurrentState != SERVICE_STOPPED && Index < 300; Index++)
    {
        NtDelayExecution(FALSE, &Delay);
        if (!QueryServiceStatusEx(Service,
                                  SC_STATUS_PROCESS_INFO,
                                  (PBYTE)&ProcessStatus,
                                  sizeof(ProcessStatus),
                                  &BytesNeeded))
        {
            Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
            goto Cleanup;
        }
    }
    if (ProcessStatus.dwCurrentState != SERVICE_STOPPED)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, ERROR_SERVICE_REQUEST_TIMEOUT);
        goto Cleanup;
    }
    if (!StartServiceW(Service, ArgumentCount, (PCWSTR*)Arguments))
    {
        Error = GetLastError();
        Status = ZpStatus_FromCode(ZpStatusWin32, Error);
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
    PS_FreeCommandLineArgv(Arguments);
    Mem_Free(ArgumentBuffer);
    Mem_Free(ServiceName);
    return Status;
}

ZP_STATUS
ZpService_Execute(
    _In_ USHORT OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_SERVICE_CONFIG_VIEW Config;
    ZP_SERVICE_RECOVERY_CONFIG_VIEW RecoveryConfig;
    ZP_SERVICE_ACCOUNT_CONFIG_VIEW AccountConfig;
    ZP_STRING_VIEW Argument, Name;
    ULONG Control;
    NTSTATUS Status;

    if (OperationId == ZP_SERVICE_OPERATION_ENUMERATE)
    {
        return RequestLength == 0 ?
                   ZpService_Enumerate(Response, ResponseLength) :
                   ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    switch (OperationId)
    {
        case ZP_SERVICE_OPERATION_QUERY:
            Status = ZpService_DecodeQuery(Request, RequestLength, &Name);
            if (!NT_SUCCESS(Status))
            {
                return ZpStatus_FromNtStatus(Status);
            }
            return ZpService_Query(&Name, Response, ResponseLength);

        case ZP_SERVICE_OPERATION_CONTROL:
        {
            ZP_STATUS Result;

            Status = ZpService_DecodeControl(Request,
                                             RequestLength,
                                             &Control,
                                             &Name,
                                             &Argument);
            if (!NT_SUCCESS(Status))
            {
                return ZpStatus_FromNtStatus(Status);
            }
            Result = ZpService_Control(Control, &Name, &Argument);
            if (ZpStatus_IsSuccess(Result))
            {
                *Response = NULL;
                *ResponseLength = 0;
            }
            return Result;
        }

        case ZP_SERVICE_OPERATION_CONFIGURE_GENERAL:
        {
            ZP_STATUS Result;

            Status = ZpService_DecodeConfig(Request, RequestLength, &Config);
            if (!NT_SUCCESS(Status))
            {
                return ZpStatus_FromNtStatus(Status);
            }
            Result = ZpService_Configure(&Config);
            if (ZpStatus_IsSuccess(Result))
            {
                *Response = NULL;
                *ResponseLength = 0;
            }
            return Result;
        }

        case ZP_SERVICE_OPERATION_CONFIGURE_RECOVERY:
        {
            ZP_STATUS Result;

            Status = ZpService_DecodeRecoveryConfig(Request, RequestLength, &RecoveryConfig);
            if (!NT_SUCCESS(Status))
            {
                return ZpStatus_FromNtStatus(Status);
            }
            Result = ZpService_ConfigureRecovery(&RecoveryConfig);
            if (ZpStatus_IsSuccess(Result))
            {
                *Response = NULL;
                *ResponseLength = 0;
            }
            return Result;
        }

        case ZP_SERVICE_OPERATION_CONFIGURE_ACCOUNT:
        {
            ZP_STATUS Result;

            Status = ZpService_DecodeAccountConfig(Request, RequestLength, &AccountConfig);
            if (!NT_SUCCESS(Status))
            {
                return ZpStatus_FromNtStatus(Status);
            }
            Result = ZpService_ConfigureAccount(&AccountConfig);
            if (ZpStatus_IsSuccess(Result))
            {
                *Response = NULL;
                *ResponseLength = 0;
            }
            return Result;
        }

        default:
            return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
}
