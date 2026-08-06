#include <lm.h>

#include <sddl.h>
#include <userenv.h>

#include <KNSoft/NDK/Win32/API/WinSta.h>

#include "../../KNSoft.ZPigeon.Client.SDK/Core/Account.h"

#pragma comment(lib, "Netapi32.lib")
#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "KNSoft.NDK.WinAPI.lib")

static
NTSTATUS
ZpUser_QueryMicrosoftAccount(
    _Outptr_ PUNICODE_STRING* Account)
{
    static const UNICODE_STRING Prefix = RTL_CONSTANT_STRING(L"MicrosoftAccount\\");
    PTOKEN_GROUPS Groups;
    PUNICODE_STRING Name;
    PSID_IDENTIFIER_AUTHORITY Authority;
    HANDLE Token;
    ULONG Length, Index;
    NTSTATUS Status;

    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &Token);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryInformationToken(Token, TokenGroups, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL)
    {
        NtClose(Token);
        return Status;
    }
    Groups = Mem_Alloc(Length);
    if (Groups == NULL)
    {
        NtClose(Token);
        return STATUS_NO_MEMORY;
    }
    Status = NtQueryInformationToken(Token, TokenGroups, Groups, Length, &Length);
    NtClose(Token);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Groups);
        return Status;
    }
    Status = STATUS_NOT_FOUND;
    for (Index = 0; Index < Groups->GroupCount; Index++)
    {
        Authority = GetSidIdentifierAuthority(Groups->Groups[Index].Sid);
        if (Authority->Value[0] != 0 || Authority->Value[1] != 0 || Authority->Value[2] != 0 ||
            Authority->Value[3] != 0 || Authority->Value[4] != 0 || Authority->Value[5] != 11)
        {
            continue;
        }
        if (NT_SUCCESS(ZpAccount_QuerySidName(Groups->Groups[Index].Sid, &Name)))
        {
            if (RtlPrefixUnicodeString((PUNICODE_STRING)&Prefix, Name, TRUE))
            {
                *Account = Name;
                Status = STATUS_SUCCESS;
                break;
            }
            NT_FreeStringW(Name);
        }
    }
    Mem_Free(Groups);
    return Status;
}

static
NTSTATUS
ZpAdministration_AddSessions(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    PSESSIONIDW Sessions;
    WINSTATIONINFORMATION Information;
    WCHAR Identity[32], Name[512];
    ULONG Count, Index, Length;
    DWORD Error;
    DWORD CurrentSession;
    PUNICODE_STRING MicrosoftAccount = NULL;
    PCWSTR Account;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &CurrentSession)) CurrentSession = MAXDWORD;
    ZpUser_QueryMicrosoftAccount(&MicrosoftAccount);
    if (!WinStationEnumerateW(WINSTATION_CURRENT_SERVER, &Sessions, &Count))
    {
        Error = GetLastError();
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindSession,
                                             Error,
                                             ZP_ADMINISTRATION_FLAG_PARTIAL,
                                             0,
                                             L"session:error",
                                             NULL,
                                             NULL,
                                             NULL);
        if (MicrosoftAccount != NULL) NT_FreeStringW(MicrosoftAccount);
        return Status;
    }
    for (Index = 0; Index < Count && NT_SUCCESS(Status); Index++)
    {
        Error = WinStationQueryInformationW(WINSTATION_CURRENT_SERVER,
                                            Sessions[Index].SessionId,
                                            WinStationInformation,
                                            &Information,
                                            sizeof(Information),
                                            &Length) ? ERROR_SUCCESS : GetLastError();
        _snwprintf_s(Identity,
                     ARRAYSIZE(Identity),
                     _TRUNCATE,
                     L"session:%lu",
                     Sessions[Index].SessionId);
        if (Error == ERROR_SUCCESS)
        {
            if (Information.Domain[0] != UNICODE_NULL)
                _snwprintf_s(Name,
                             ARRAYSIZE(Name),
                             _TRUNCATE,
                             L"%s\\%s",
                             Information.Domain,
                             Information.UserName);
            else
                _snwprintf_s(Name, ARRAYSIZE(Name), _TRUNCATE, L"%s", Information.UserName);
        }
        Account = MicrosoftAccount != NULL && Sessions[Index].SessionId == CurrentSession ?
                      MicrosoftAccount->Buffer + RTL_NUMBER_OF(L"MicrosoftAccount\\") - 1 : NULL;
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindSession,
                                             Error == ERROR_SUCCESS ? Sessions[Index].State : Error,
                                             Error == ERROR_SUCCESS ? 0 : ZP_ADMINISTRATION_FLAG_PARTIAL,
                                             Sessions[Index].SessionId,
                                             Identity,
                                             Error == ERROR_SUCCESS ? Name : NULL,
                                             Sessions[Index].WinStationName,
                                             Error == ERROR_SUCCESS ? Account : NULL);
    }
    if (Sessions != NULL) WinStationFreeMemory(Sessions);
    if (MicrosoftAccount != NULL) NT_FreeStringW(MicrosoftAccount);
    return Status;
}

static
NTSTATUS
ZpUser_AddLogonSessionRecord(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PSECURITY_LOGON_SESSION_DATA Session,
    _In_ PCWSTR Identity,
    _In_ PCWSTR User,
    _In_ PCWSTR Domain,
    _In_reads_opt_(UpnLength) PCWCH Upn,
    _In_ ULONG UpnLength)
{
    ULONG AuthenticationPackageLength = Session->AuthenticationPackage.Length / sizeof(WCHAR);
    ULONG LogonServerLength = Session->LogonServer.Length / sizeof(WCHAR);
    ULONG DnsDomainLength = Session->DnsDomainName.Length / sizeof(WCHAR);
    ULONGLONG RequiredSize = sizeof(ULONG) * 5 +
                             ((ULONGLONG)AuthenticationPackageLength + UpnLength +
                              LogonServerLength + DnsDomainLength) * sizeof(WCHAR);
    ZP_CODEC_WRITER Writer;
    PBYTE Data;
    NTSTATUS Status;

    if (RequiredSize > ZP_CODEC_MAX_ELEMENT_COUNT) return STATUS_BUFFER_OVERFLOW;
    Data = Mem_Alloc((SIZE_T)RequiredSize);
    if (Data == NULL) return STATUS_NO_MEMORY;
    ZpCodec_InitializeWriter(&Writer, Data, (ULONG)RequiredSize);
    Status = ZpCodec_WriteUInt32(&Writer, Session->Session);
    if (NT_SUCCESS(Status))
        Status = ZpCodec_WriteString(&Writer,
                                     Session->AuthenticationPackage.Buffer,
                                     AuthenticationPackageLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Upn, UpnLength);
    if (NT_SUCCESS(Status))
        Status = ZpCodec_WriteString(&Writer, Session->LogonServer.Buffer, LogonServerLength);
    if (NT_SUCCESS(Status))
        Status = ZpCodec_WriteString(&Writer, Session->DnsDomainName.Buffer, DnsDomainLength);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddRecordData(Builder,
                                                 ZpAdministrationKindLogonSession,
                                                 Session->LogonType,
                                                 0,
                                                 Session->LogonTime.QuadPart,
                                                 Identity,
                                                 User,
                                                 Domain,
                                                 NULL,
                                                 Data,
                                                 Writer.Offset);
    }
    Mem_Free(Data);
    return Status;
}

static
NTSTATUS
ZpAdministration_AddLogonSessions(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    PSECURITY_LOGON_SESSION_DATA Data;
    PLUID Sessions;
    WCHAR Identity[32], User[512], Domain[512];
    ULONG Count, Index;
    DWORD CurrentSession;
    PUNICODE_STRING MicrosoftAccount = NULL;
    PCWCH Upn;
    INT UpnLength;
    NTSTATUS Status;

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &CurrentSession)) CurrentSession = MAXDWORD;
    ZpUser_QueryMicrosoftAccount(&MicrosoftAccount);
    Status = LsaEnumerateLogonSessions(&Count, &Sessions);
    if (!NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindLogonSession,
                                             Status,
                                             ZP_ADMINISTRATION_FLAG_PARTIAL,
                                             0,
                                             L"logon:error",
                                             NULL,
                                             NULL,
                                             NULL);
        if (MicrosoftAccount != NULL) NT_FreeStringW(MicrosoftAccount);
        return Status;
    }
    for (Index = 0; Index < Count; Index++)
    {
        Status = LsaGetLogonSessionData(&Sessions[Index], &Data);
        if (!NT_SUCCESS(Status))
        {
            _snwprintf_s(Identity,
                         ARRAYSIZE(Identity),
                         _TRUNCATE,
                         L"logon:%08lX%08lX",
                         Sessions[Index].HighPart,
                         Sessions[Index].LowPart);
            Status = ZpAdministration_AddRecord(Builder,
                                                 ZpAdministrationKindLogonSession,
                                                 Status,
                                                 ZP_ADMINISTRATION_FLAG_PARTIAL,
                                                 0,
                                                 Identity,
                                                 NULL,
                                                 NULL,
                                                 NULL);
            if (!NT_SUCCESS(Status)) break;
            continue;
        }
        if (Data == NULL) continue;
        _snwprintf_s(Identity,
                     ARRAYSIZE(Identity),
                     _TRUNCATE,
                     L"logon:%08lX%08lX",
                     Sessions[Index].HighPart,
                     Sessions[Index].LowPart);
        _snwprintf_s(User,
                     ARRAYSIZE(User),
                     _TRUNCATE,
                     L"%.*s",
                     (INT)(Data->UserName.Length / sizeof(WCHAR)),
                     Data->UserName.Buffer == NULL ? L"" : Data->UserName.Buffer);
        _snwprintf_s(Domain,
                     ARRAYSIZE(Domain),
                     _TRUNCATE,
                     L"%.*s",
                     (INT)(Data->LogonDomain.Length / sizeof(WCHAR)),
                     Data->LogonDomain.Buffer == NULL ? L"" : Data->LogonDomain.Buffer);
        Upn = Data->Upn.Buffer;
        UpnLength = (INT)(Data->Upn.Length / sizeof(WCHAR));
        if (UpnLength == 0 && MicrosoftAccount != NULL && Data->Session == CurrentSession)
        {
            Upn = MicrosoftAccount->Buffer + RTL_NUMBER_OF(L"MicrosoftAccount\\") - 1;
            UpnLength = (INT)(MicrosoftAccount->Length / sizeof(WCHAR)) -
                        (RTL_NUMBER_OF(L"MicrosoftAccount\\") - 1);
        }
        Status = ZpUser_AddLogonSessionRecord(Builder,
                                               Data,
                                               Identity,
                                               User,
                                               Domain,
                                               Upn,
                                               UpnLength);
        LsaFreeReturnBuffer(Data);
        if (!NT_SUCCESS(Status)) break;
    }
    LsaFreeReturnBuffer(Sessions);
    if (MicrosoftAccount != NULL) NT_FreeStringW(MicrosoftAccount);
    return Status;
}

static
DWORD
ZpUserProfile_QueryDirectorySize(
    _In_ PCWSTR Path,
    _In_ ULONG Depth,
    _Inout_ PULONGLONG Size)
{
    WIN32_FIND_DATAW Data;
    HANDLE Find;
    PWSTR Pattern, Child;
    SIZE_T PathLength, NameLength;
    ULONGLONG FileSize;
    DWORD Error = ERROR_SUCCESS;

    if (Depth == 128) return ERROR_DIRECTORY;
    PathLength = wcslen(Path);
    if (PathLength > 32764) return ERROR_FILENAME_EXCED_RANGE;
    Pattern = Mem_Alloc((PathLength + 3) * sizeof(WCHAR));
    if (Pattern == NULL) return ERROR_NOT_ENOUGH_MEMORY;
    _snwprintf_s(Pattern, PathLength + 3, _TRUNCATE, L"%s\\*", Path);
    Find = FindFirstFileExW(Pattern, FindExInfoBasic, &Data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    Mem_Free(Pattern);
    if (Find == INVALID_HANDLE_VALUE) return GetLastError();
    do
    {
        if (wcscmp(Data.cFileName, L".") == 0 || wcscmp(Data.cFileName, L"..") == 0) continue;
        NameLength = wcslen(Data.cFileName);
        if (PathLength + NameLength + 2 > 32767)
        {
            Error = ERROR_FILENAME_EXCED_RANGE;
            break;
        }
        if (FlagOn(Data.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY))
        {
            if (FlagOn(Data.dwFileAttributes, FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            Child = Mem_Alloc((PathLength + NameLength + 2) * sizeof(WCHAR));
            if (Child == NULL)
            {
                Error = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
            _snwprintf_s(Child, PathLength + NameLength + 2, _TRUNCATE, L"%s\\%s", Path, Data.cFileName);
            Error = ZpUserProfile_QueryDirectorySize(Child, Depth + 1, Size);
            Mem_Free(Child);
            if (Error != ERROR_SUCCESS) break;
        }
        else
        {
            FileSize = ((ULONGLONG)Data.nFileSizeHigh << 32) | Data.nFileSizeLow;
            if (FileSize > MAXULONGLONG - *Size)
            {
                Error = ERROR_ARITHMETIC_OVERFLOW;
                break;
            }
            *Size += FileSize;
        }
    } while (FindNextFileW(Find, &Data));
    if (Error == ERROR_SUCCESS)
    {
        Error = GetLastError();
        if (Error == ERROR_NO_MORE_FILES) Error = ERROR_SUCCESS;
    }
    FindClose(Find);
    return Error;
}

static
DWORD
ZpUserProfile_CopyDirectorySecurity(
    _In_ PCWSTR Source,
    _In_ PCWSTR Destination)
{
    PSECURITY_DESCRIPTOR Security;
    DWORD Length = 0, Error;

    GetFileSecurityW(Source, DACL_SECURITY_INFORMATION, NULL, 0, &Length);
    Error = GetLastError();
    if (Error != ERROR_INSUFFICIENT_BUFFER) return Error;
    Security = Mem_Alloc(Length);
    if (Security == NULL) return ERROR_NOT_ENOUGH_MEMORY;
    if (!GetFileSecurityW(Source, DACL_SECURITY_INFORMATION, Security, Length, &Length))
    {
        Error = GetLastError();
    }
    else if (!SetFileSecurityW(Destination, DACL_SECURITY_INFORMATION, Security))
    {
        Error = GetLastError();
    }
    else
    {
        Error = ERROR_SUCCESS;
    }
    Mem_Free(Security);
    return Error;
}

static
DWORD
ZpUserProfile_CopyDirectory(
    _In_ PCWSTR Source,
    _In_ PCWSTR Destination,
    _In_ ULONG Depth)
{
    WIN32_FILE_ATTRIBUTE_DATA SourceData;
    WIN32_FIND_DATAW Data;
    HANDLE Find, Directory;
    PWSTR Pattern, SourceChild, DestinationChild;
    SIZE_T SourceLength, DestinationLength, NameLength;
    DWORD Error = ERROR_SUCCESS, Flags;

    if (Depth == 128) return ERROR_DIRECTORY;
    if (!GetFileAttributesExW(Source, GetFileExInfoStandard, &SourceData)) return GetLastError();
    SourceLength = wcslen(Source);
    DestinationLength = wcslen(Destination);
    if (SourceLength > 32764 || DestinationLength > 32764) return ERROR_FILENAME_EXCED_RANGE;
    if (!CreateDirectoryW(Destination, NULL)) return GetLastError();
    Error = ZpUserProfile_CopyDirectorySecurity(Source, Destination);
    if (Error != ERROR_SUCCESS)
    {
        RemoveDirectoryW(Destination);
        return Error;
    }
    Pattern = Mem_Alloc((SourceLength + 3) * sizeof(WCHAR));
    if (Pattern == NULL) return ERROR_NOT_ENOUGH_MEMORY;
    _snwprintf_s(Pattern, SourceLength + 3, _TRUNCATE, L"%s\\*", Source);
    Find = FindFirstFileExW(Pattern, FindExInfoBasic, &Data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    Mem_Free(Pattern);
    if (Find == INVALID_HANDLE_VALUE) return GetLastError();
    do
    {
        if (wcscmp(Data.cFileName, L".") == 0 || wcscmp(Data.cFileName, L"..") == 0) continue;
        NameLength = wcslen(Data.cFileName);
        if (SourceLength + NameLength + 2 > 32767 || DestinationLength + NameLength + 2 > 32767)
        {
            Error = ERROR_FILENAME_EXCED_RANGE;
            break;
        }
        SourceChild = Mem_Alloc((SourceLength + NameLength + 2) * sizeof(WCHAR));
        if (SourceChild == NULL)
        {
            Error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        DestinationChild = Mem_Alloc((DestinationLength + NameLength + 2) * sizeof(WCHAR));
        if (DestinationChild == NULL)
        {
            Mem_Free(SourceChild);
            Error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        _snwprintf_s(SourceChild, SourceLength + NameLength + 2, _TRUNCATE, L"%s\\%s", Source, Data.cFileName);
        _snwprintf_s(DestinationChild,
                     DestinationLength + NameLength + 2,
                     _TRUNCATE,
                     L"%s\\%s",
                     Destination,
                     Data.cFileName);
        if (FlagOn(Data.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY))
        {
            if (!FlagOn(Data.dwFileAttributes, FILE_ATTRIBUTE_REPARSE_POINT))
            {
                Error = ZpUserProfile_CopyDirectory(SourceChild, DestinationChild, Depth + 1);
            }
        }
        else
        {
            Flags = COPY_FILE_FAIL_IF_EXISTS | COPY_FILE_REQUEST_SECURITY_PRIVILEGES;
            if (FlagOn(Data.dwFileAttributes, FILE_ATTRIBUTE_REPARSE_POINT)) Flags |= COPY_FILE_COPY_SYMLINK;
            if (!CopyFileExW(SourceChild, DestinationChild, NULL, NULL, NULL, Flags))
            {
                Error = GetLastError();
            }
            else
            {
                Error = ZpUserProfile_CopyDirectorySecurity(SourceChild, DestinationChild);
            }
        }
        Mem_Free(DestinationChild);
        Mem_Free(SourceChild);
        if (Error != ERROR_SUCCESS) break;
    } while (FindNextFileW(Find, &Data));
    if (Error == ERROR_SUCCESS)
    {
        Error = GetLastError();
        if (Error == ERROR_NO_MORE_FILES) Error = ERROR_SUCCESS;
    }
    FindClose(Find);
    if (Error != ERROR_SUCCESS) return Error;
    Directory = CreateFileW(Destination,
                            FILE_WRITE_ATTRIBUTES,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL,
                            OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS,
                            NULL);
    if (Directory == INVALID_HANDLE_VALUE) return GetLastError();
    if (!SetFileTime(Directory, &SourceData.ftCreationTime, &SourceData.ftLastAccessTime, &SourceData.ftLastWriteTime))
    {
        Error = GetLastError();
    }
    CloseHandle(Directory);
    if (Error == ERROR_SUCCESS && !SetFileAttributesW(Destination, SourceData.dwFileAttributes))
    {
        Error = GetLastError();
    }
    return Error;
}

static
_Ret_maybenull_z_
PWSTR
ZpUserProfile_QueryRegistryString(
    _In_ HANDLE Key,
    _In_ PCUNICODE_STRING Name)
{
    PKEY_VALUE_PARTIAL_INFORMATION Data;
    PWSTR Value, Expanded;
    SIZE_T Length;
    DWORD Required;

    if (!NT_SUCCESS(Sys_RegQueryData(Key, Name, &Data))) return NULL;
    if ((Data->Type != REG_SZ && Data->Type != REG_EXPAND_SZ) || Data->DataLength % sizeof(WCHAR) != 0)
    {
        Mem_Free(Data);
        return NULL;
    }
    Length = Data->DataLength / sizeof(WCHAR);
    while (Length != 0 && ((PCWCHAR)Data->Data)[Length - 1] == UNICODE_NULL) Length--;
    Value = Mem_Alloc((Length + 1) * sizeof(WCHAR));
    if (Value == NULL)
    {
        Mem_Free(Data);
        return NULL;
    }
    RtlCopyMemory(Value, Data->Data, Length * sizeof(WCHAR));
    Value[Length] = UNICODE_NULL;
    if (Data->Type == REG_EXPAND_SZ)
    {
        Required = ExpandEnvironmentStringsW(Value, NULL, 0);
        Expanded = Required == 0 ? NULL : Mem_Alloc((SIZE_T)Required * sizeof(WCHAR));
        if (Expanded != NULL && ExpandEnvironmentStringsW(Value, Expanded, Required) == 0)
        {
            Mem_Free(Expanded);
            Expanded = NULL;
        }
        Mem_Free(Value);
        Value = Expanded;
    }
    Mem_Free(Data);
    return Value;
}

#pragma warning(suppress : 6054 6101) // The converted Win32 error is always a failing NTSTATUS.
static
_Success_(return == STATUS_SUCCESS)
NTSTATUS
ZpUserProfile_QueryRegistryProfile(
    _In_ PCWSTR Sid,
    _Outptr_result_z_ PWSTR* Path,
    _Out_ PULONG RefCount)
{
    static const UNICODE_STRING ProfileList = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList");
    static const UNICODE_STRING ProfileImagePath = RTL_CONSTANT_STRING(L"ProfileImagePath");
    static const UNICODE_STRING RefCountName = RTL_CONSTANT_STRING(L"RefCount");
    UNICODE_STRING SidName;
    HANDLE Root, Key, LoadedKey;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PWSTR LoadedPath, Value;
    SIZE_T SidLength;
    ULONG References;
    ULONG Error;
    PSID BinarySid;
    NTSTATUS Status;

    if (!ConvertStringSidToSidW(Sid, &BinarySid))
    {
        Error = GetLastError();
        _Analysis_assume_(Error != ERROR_SUCCESS);
        return NTSTATUS_FROM_WIN32(Error);
    }
    LocalFree(BinarySid);
    Status = Sys_RegOpenKey(&Root, KEY_ENUMERATE_SUB_KEYS, &ProfileList);
    if (!NT_SUCCESS(Status)) return Status;
    RtlInitUnicodeString(&SidName, Sid);
    Status = Sys_RegOpenKeyEx(&Key, Root, KEY_QUERY_VALUE, &SidName);
    NtClose(Root);
    if (!NT_SUCCESS(Status)) return Status;
    Value = ZpUserProfile_QueryRegistryString(Key, &ProfileImagePath);
    if (Value == NULL)
    {
        NtClose(Key);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (!NT_SUCCESS(Sys_RegQueryDword(Key, &RefCountName, &References))) References = 0;
    NtClose(Key);
    if (References == 0)
    {
        SidLength = wcslen(Sid);
        LoadedPath = Mem_Alloc((SidLength + RTL_NUMBER_OF(L"\\Registry\\User\\")) * sizeof(WCHAR));
        if (LoadedPath == NULL)
        {
            Mem_Free(Value);
            return STATUS_NO_MEMORY;
        }
        _snwprintf_s(LoadedPath,
                     SidLength + RTL_NUMBER_OF(L"\\Registry\\User\\"),
                     _TRUNCATE,
                     L"\\Registry\\User\\%s",
                     Sid);
        RtlInitUnicodeString(&SidName, LoadedPath);
        InitializeObjectAttributes(&ObjectAttributes,
                                   &SidName,
                                   OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                   NULL,
                                   NULL);
        if (NT_SUCCESS(NtOpenKey(&LoadedKey, KEY_QUERY_VALUE, &ObjectAttributes)))
        {
            References = 1;
            NtClose(LoadedKey);
        }
        Mem_Free(LoadedPath);
    }
    *Path = Value;
    *RefCount = References;
    return STATUS_SUCCESS;
}

#pragma warning(suppress : 6101) // Response outputs are assigned only after successful encoding.
static
ZP_STATUS
ZpAdministration_EnumerateUserProfiles(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    static const UNICODE_STRING ProfileList = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList");
    static const UNICODE_STRING ProfileImagePath = RTL_CONSTANT_STRING(L"ProfileImagePath");
    static const UNICODE_STRING FlagsName = RTL_CONSTANT_STRING(L"Flags");
    static const UNICODE_STRING StateName = RTL_CONSTANT_STRING(L"State");
    static const UNICODE_STRING RefCountName = RTL_CONSTANT_STRING(L"RefCount");
    static const UNICODE_STRING LoadLowName = RTL_CONSTANT_STRING(L"LocalProfileLoadTimeLow");
    static const UNICODE_STRING LoadHighName = RTL_CONSTANT_STRING(L"LocalProfileLoadTimeHigh");
    static const UNICODE_STRING UnloadLowName = RTL_CONSTANT_STRING(L"LocalProfileUnloadTimeLow");
    static const UNICODE_STRING UnloadHighName = RTL_CONSTANT_STRING(L"LocalProfileUnloadTimeHigh");
    KEY_CACHED_INFORMATION Cached;
    PKEY_BASIC_INFORMATION Information;
    PUNICODE_STRING Account = NULL;
    PUSER_INFO_4 User = NULL;
    UNICODE_STRING SidName;
    HANDLE Root, Key, LoadedKey;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PWSTR Sid, Path, Detail, RoamingPath = NULL, UserName, LoadedPath;
    WCHAR ComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    PSID BinarySid;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index, Length, InformationLength, Flags, ProfileFlags, State, References;
    ULONG LoadLow, LoadHigh, UnloadLow, UnloadHigh, DetailLength;
    ULONGLONG Size, LastUse;
    DWORD Error, Result, ComputerNameLength = ARRAYSIZE(ComputerName);

    Status = Sys_RegOpenKey(&Root, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &ProfileList);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    if (!GetComputerNameW(ComputerName, &ComputerNameLength)) ComputerNameLength = 0;
    Status = NtQueryKey(Root, KeyCachedInformation, &Cached, sizeof(Cached), &Length);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Root);
        return ZpStatus_FromNtStatus(Status);
    }
    InformationLength = FIELD_OFFSET(KEY_BASIC_INFORMATION, Name) + Cached.MaxNameLength;
    Information = Mem_Alloc(InformationLength);
    if (Information == NULL)
    {
        NtClose(Root);
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; NT_SUCCESS(Status); Index++)
    {
        Status = NtEnumerateKey(Root, Index, KeyBasicInformation, Information, InformationLength, &Length);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status)) break;
        Sid = Mem_Alloc((SIZE_T)Information->NameLength + sizeof(WCHAR));
        if (Sid == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        RtlCopyMemory(Sid, Information->Name, Information->NameLength);
        Sid[Information->NameLength / sizeof(WCHAR)] = UNICODE_NULL;
        if (!ConvertStringSidToSidW(Sid, &BinarySid))
        {
            Mem_Free(Sid);
            continue;
        }
        SidName.Buffer = Information->Name;
        SidName.Length = (USHORT)Information->NameLength;
        SidName.MaximumLength = SidName.Length;
        Status = Sys_RegOpenKeyEx(&Key, Root, KEY_QUERY_VALUE, &SidName);
        if (!NT_SUCCESS(Status))
        {
            LocalFree(BinarySid);
            Mem_Free(Sid);
            break;
        }
        Path = ZpUserProfile_QueryRegistryString(Key, &ProfileImagePath);
        ProfileFlags = State = References = LoadLow = LoadHigh = UnloadLow = UnloadHigh = 0;
        Sys_RegQueryDword(Key, &FlagsName, &ProfileFlags);
        Sys_RegQueryDword(Key, &StateName, &State);
        Sys_RegQueryDword(Key, &RefCountName, &References);
        Sys_RegQueryDword(Key, &LoadLowName, &LoadLow);
        Sys_RegQueryDword(Key, &LoadHighName, &LoadHigh);
        Sys_RegQueryDword(Key, &UnloadLowName, &UnloadLow);
        Sys_RegQueryDword(Key, &UnloadHighName, &UnloadHigh);
        NtClose(Key);
        if (Path == NULL)
        {
            LocalFree(BinarySid);
            Mem_Free(Sid);
            continue;
        }
        Flags = 0;
        LoadedPath = Mem_Alloc((wcslen(Sid) + RTL_NUMBER_OF(L"\\Registry\\User\\")) * sizeof(WCHAR));
        if (LoadedPath == NULL)
        {
            Status = STATUS_NO_MEMORY;
            Mem_Free(Path);
            LocalFree(BinarySid);
            Mem_Free(Sid);
            break;
        }
        _snwprintf_s(LoadedPath,
                     wcslen(Sid) + RTL_NUMBER_OF(L"\\Registry\\User\\"),
                     _TRUNCATE,
                     L"\\Registry\\User\\%s",
                     Sid);
        RtlInitUnicodeString(&SidName, LoadedPath);
        InitializeObjectAttributes(&ObjectAttributes,
                                   &SidName,
                                   OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                   NULL,
                                   NULL);
        if (References != 0)
        {
            Flags |= ZP_ADMINISTRATION_USER_PROFILE_FLAG_LOADED;
        }
        else if (NT_SUCCESS(NtOpenKey(&LoadedKey, KEY_QUERY_VALUE, &ObjectAttributes)))
        {
            Flags |= ZP_ADMINISTRATION_USER_PROFILE_FLAG_LOADED;
            NtClose(LoadedKey);
        }
        Mem_Free(LoadedPath);
        if (IsWellKnownSid(BinarySid, WinLocalSystemSid) || IsWellKnownSid(BinarySid, WinLocalServiceSid) ||
            IsWellKnownSid(BinarySid, WinNetworkServiceSid))
        {
            Flags |= ZP_ADMINISTRATION_USER_PROFILE_FLAG_SPECIAL;
        }
        ZpAccount_QuerySidName(BinarySid, &Account);
        LocalFree(BinarySid);
        if (Account != NULL)
        {
            UserName = wcsrchr(Account->Buffer, L'\\');
            Result = UserName != NULL && (ULONG)(UserName - Account->Buffer) == ComputerNameLength &&
                             _wcsnicmp(Account->Buffer, ComputerName, ComputerNameLength) == 0 ?
                         NetUserGetInfo(NULL, UserName + 1, 4, (PBYTE*)&User) :
                         ERROR_NONE_MAPPED;
            if (Result == NERR_Success && User->usri4_profile != NULL && *User->usri4_profile != UNICODE_NULL)
            {
                RoamingPath = User->usri4_profile;
                Flags |= ZP_ADMINISTRATION_USER_PROFILE_FLAG_ROAMING_CONFIGURED;
            }
        }
        LastUse = ((ULONGLONG)UnloadHigh << 32) | UnloadLow;
        if (LastUse == 0) LastUse = ((ULONGLONG)LoadHigh << 32) | LoadLow;
        if (LastUse == 0) LastUse = Information->LastWriteTime.QuadPart;
        Size = 0;
        Error = ZpUserProfile_QueryDirectorySize(Path, 0, &Size);
        if (Error != ERROR_SUCCESS) Flags |= ZP_ADMINISTRATION_FLAG_PARTIAL;
        DetailLength = 80 + (RoamingPath == NULL ? 0 : (ULONG)wcslen(RoamingPath));
        Detail = Mem_Alloc((SIZE_T)DetailLength * sizeof(WCHAR));
        if (Detail == NULL)
        {
            Status = STATUS_NO_MEMORY;
        }
        else
        {
            _snwprintf_s(Detail,
                         DetailLength,
                         _TRUNCATE,
                         L"%llu\n%lu\n%s",
                         LastUse,
                         References,
                         RoamingPath == NULL ? L"" : RoamingPath);
            Status = ZpAdministration_AddRecord(&Builder,
                                                 ZpAdministrationKindUserProfile,
                                                 State | (ProfileFlags << 16),
                                                 Flags,
                                                 Size,
                                                 Sid,
                                                 Account != NULL ? Account->Buffer : Sid,
                                                 Path,
                                                 Detail);
            Mem_Free(Detail);
        }
        if (User != NULL)
        {
            NetApiBufferFree(User);
            User = NULL;
        }
        RoamingPath = NULL;
        if (Account != NULL)
        {
            NT_FreeStringW(Account);
            Account = NULL;
        }
        Mem_Free(Path);
        Mem_Free(Sid);
    }
    if (Account != NULL) NT_FreeStringW(Account);
    if (User != NULL) NetApiBufferFree(User);
    Mem_Free(Information);
    NtClose(Root);
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_EnumerateUsers(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PUSER_INFO_2 Users;
    PUSER_INFO_0 Names;
    DWORD EntriesRead, TotalEntries, Resume = 0, Result;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    do
    {
        Result = NetUserEnum(NULL,
                             2,
                             FILTER_NORMAL_ACCOUNT,
                             (PBYTE*)&Users,
                             MAX_PREFERRED_LENGTH,
                             &EntriesRead,
                             &TotalEntries,
                             &Resume);
        if (Result != NERR_Success && Result != ERROR_MORE_DATA) break;
        for (Index = 0; NT_SUCCESS(Status) && Index < EntriesRead; Index++)
        {
            Status = ZpAdministration_AddRecord(&Builder,
                                                 ZpAdministrationKindUser,
                                                 Users[Index].usri2_priv,
                                                 Users[Index].usri2_flags,
                                                 Users[Index].usri2_last_logon,
                                                 Users[Index].usri2_name,
                                                 Users[Index].usri2_full_name,
                                                 Users[Index].usri2_comment,
                                                 Users[Index].usri2_home_dir);
        }
        NetApiBufferFree(Users);
        if (!NT_SUCCESS(Status)) break;
    } while (Result == ERROR_MORE_DATA);
    if (Result == ERROR_ACCESS_DENIED && Builder.Count == 0)
    {
        Resume = 0;
        do
        {
            Result = NetUserEnum(NULL,
                                 0,
                                 FILTER_NORMAL_ACCOUNT,
                                 (PBYTE*)&Names,
                                 MAX_PREFERRED_LENGTH,
                                 &EntriesRead,
                                 &TotalEntries,
                                 &Resume);
            if (Result != NERR_Success && Result != ERROR_MORE_DATA) break;
            for (Index = 0; NT_SUCCESS(Status) && Index < EntriesRead; Index++)
            {
                Status = ZpAdministration_AddRecord(&Builder,
                                                     ZpAdministrationKindUser,
                                                     ERROR_ACCESS_DENIED,
                                                     ZP_ADMINISTRATION_FLAG_PARTIAL,
                                                     0,
                                                     Names[Index].usri0_name,
                                                     NULL,
                                                     NULL,
                                                     NULL);
            }
            NetApiBufferFree(Names);
            if (!NT_SUCCESS(Status)) break;
        } while (Result == ERROR_MORE_DATA);
    }
    if (Result == NERR_Success && NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }
    ZpAdministration_FreeBuilder(&Builder);
    return Result == NERR_Success ?
               ZpStatus_FromNtStatus(Status) :
               ZpStatus_FromCode(ZpStatusWin32, Result);
}

static
ZP_STATUS
ZpAdministration_EnumerateSessions(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    NTSTATUS Status = ZpAdministration_AddSessions(&Builder);

    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_EnumerateLogonSessions(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    NTSTATUS Status = ZpAdministration_AddLogonSessions(&Builder);

    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_ControlUser(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PUSER_INFO_4 User;
    USER_INFO_1 NewUser = { 0 };
    USER_INFO_0 Rename;
    USER_INFO_1003 Password;
    USER_INFO_1008 Flags;
    PWSTR Identity, Argument = NULL, Secret = NULL;
    DWORD Result, ParameterError;

    if ((Control->Action == ZpAdministrationActionDisconnect ||
         Control->Action == ZpAdministrationActionSignOut) &&
        Control->Identity.Length > 8)
    {
        UNICODE_STRING Number;
        ULONG SessionId;
        NTSTATUS Status;

        Identity = ZpAdministration_CopyView(&Control->Identity);
        if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        if (wcsncmp(Identity, L"session:", 8) != 0)
        {
            Mem_Free(Identity);
            return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
        }
        RtlInitUnicodeString(&Number, Identity + 8);
        Status = RtlUnicodeStringToInteger(&Number, 10, &SessionId);
        Mem_Free(Identity);
        if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
        Result = Control->Action == ZpAdministrationActionDisconnect ?
                     (WinStationDisconnect(WINSTATION_CURRENT_SERVER, SessionId, FALSE) ? ERROR_SUCCESS :
                                                                                         GetLastError()) :
                     (WinStationReset(WINSTATION_CURRENT_SERVER, SessionId, FALSE) ? ERROR_SUCCESS :
                                                                                    GetLastError());
        return ZpStatus_FromCode(ZpStatusWin32, Result);
    }

    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Control->Argument.Length != 0) Argument = ZpAdministration_CopyView(&Control->Argument);
    if (Control->Secret.Length != 0) Secret = ZpAdministration_CopyView(&Control->Secret);
    if (Identity == NULL || (Control->Argument.Length != 0 && Argument == NULL) ||
        (Control->Secret.Length != 0 && Secret == NULL))
    {
        Result = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
    }
    switch (Control->Action)
    {
        case ZpAdministrationActionCreate:
            if (Secret == NULL)
            {
                Result = ERROR_INVALID_PASSWORD;
                break;
            }
            NewUser.usri1_name = Identity;
            NewUser.usri1_password = Secret;
            NewUser.usri1_priv = USER_PRIV_USER;
            NewUser.usri1_home_dir = NULL;
            NewUser.usri1_comment = Argument;
            NewUser.usri1_flags = UF_SCRIPT;
            Result = NetUserAdd(NULL, 1, (PBYTE)&NewUser, &ParameterError);
            break;

        case ZpAdministrationActionDelete:
            Result = NetUserDel(NULL, Identity);
            break;

        case ZpAdministrationActionSetPassword:
            if (Secret == NULL)
            {
                Result = ERROR_INVALID_PASSWORD;
                break;
            }
            Password.usri1003_password = Secret;
            Result = NetUserSetInfo(NULL, Identity, 1003, (PBYTE)&Password, &ParameterError);
            break;

        case ZpAdministrationActionRename:
            if (Argument == NULL || *Argument == UNICODE_NULL)
            {
                Result = ERROR_INVALID_NAME;
                break;
            }
            Rename.usri0_name = Argument;
            Result = NetUserSetInfo(NULL, Identity, 0, (PBYTE)&Rename, &ParameterError);
            break;

        case ZpAdministrationActionEnable:
        case ZpAdministrationActionDisable:
            Result = NetUserGetInfo(NULL, Identity, 4, (PBYTE*)&User);
            if (Result == NERR_Success)
            {
                Flags.usri1008_flags = Control->Action == ZpAdministrationActionEnable ?
                                           User->usri4_flags & ~UF_ACCOUNTDISABLE :
                                           User->usri4_flags | UF_ACCOUNTDISABLE;
                NetApiBufferFree(User);
                Result = NetUserSetInfo(NULL, Identity, 1008, (PBYTE)&Flags, &ParameterError);
            }
            break;

        default:
            Result = ERROR_NOT_SUPPORTED;
            break;
    }
Cleanup:
    if (Secret != NULL)
    {
        RtlSecureZeroMemory(Secret, ((SIZE_T)Control->Secret.Length + 1) * sizeof(WCHAR));
        Mem_Free(Secret);
    }
    Mem_Free(Argument);
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusWin32, Result);
}

static
DWORD
ZpUserProfile_SetRoamingPath(
    _In_ PCWSTR Sid,
    _In_ PCWSTR Path)
{
    USER_INFO_1052 Profile;
    PUNICODE_STRING Account;
    PWSTR UserName;
    WCHAR ComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD Result, ParameterError, ComputerNameLength = ARRAYSIZE(ComputerName);

    if (!NT_SUCCESS(ZpAccount_QueryStringSidName(Sid, &Account))) return ERROR_NONE_MAPPED;
    UserName = wcsrchr(Account->Buffer, L'\\');
    if (!GetComputerNameW(ComputerName, &ComputerNameLength) || UserName == NULL ||
        (ULONG)(UserName - Account->Buffer) != ComputerNameLength ||
        _wcsnicmp(Account->Buffer, ComputerName, ComputerNameLength) != 0)
    {
        NT_FreeStringW(Account);
        return ERROR_NONE_MAPPED;
    }
    UserName++;
    Profile.usri1052_profile = (PWSTR)Path;
    Result = NetUserSetInfo(NULL, UserName, 1052, (PBYTE)&Profile, &ParameterError);
    NT_FreeStringW(Account);
    return Result;
}

static
ZP_STATUS
ZpAdministration_ControlUserProfile(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PWSTR Identity, Argument = NULL, Source = NULL, SourcePath = NULL, DestinationPath = NULL;
    SIZE_T SourceLength;
    ULONG RefCount;
    DWORD Required, Error = ERROR_SUCCESS;
    NTSTATUS Status;
    PSID Sid;

    if (Control->Action != ZpAdministrationActionCreate &&
        Control->Action != ZpAdministrationActionDelete &&
        Control->Action != ZpAdministrationActionConfigure)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Control->Argument.Length != 0) Argument = ZpAdministration_CopyView(&Control->Argument);
    if (Identity == NULL || (Control->Argument.Length != 0 && Argument == NULL))
    {
        Mem_Free(Argument);
        Mem_Free(Identity);
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    if (!ConvertStringSidToSidW(Identity, &Sid))
    {
        Error = GetLastError();
        goto Cleanup;
    }
    LocalFree(Sid);
    if (Control->Action == ZpAdministrationActionDelete)
    {
        if (!DeleteProfileW(Identity, NULL, NULL)) Error = GetLastError();
        goto Cleanup;
    }
    if (Control->Action == ZpAdministrationActionConfigure)
    {
        if (Argument != NULL &&
            (*Argument == UNICODE_NULL || Argument[0] != L'\\' || Argument[1] != L'\\' ||
             wcschr(Argument, L'\n') != NULL ||
              wcschr(Argument, L'\r') != NULL))
        {
            Error = ERROR_INVALID_PARAMETER;
            goto Cleanup;
        }
        Error = ZpUserProfile_SetRoamingPath(Identity, Argument == NULL ? L"" : Argument);
        goto Cleanup;
    }
    if (Argument == NULL || *Argument == UNICODE_NULL)
    {
        Error = ERROR_INVALID_PARAMETER;
        goto Cleanup;
    }
    Status = ZpUserProfile_QueryRegistryProfile(Identity, &Source, &RefCount);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Argument);
        Mem_Free(Identity);
        return ZpStatus_FromNtStatus(Status);
    }
    if (RefCount != 0)
    {
        Error = ERROR_BUSY;
        goto Cleanup;
    }
    Required = GetFullPathNameW(Source, 0, NULL, NULL);
    SourcePath = Required == 0 ? NULL : Mem_Alloc((SIZE_T)Required * sizeof(WCHAR));
    if (SourcePath == NULL || GetFullPathNameW(Source, Required, SourcePath, NULL) == 0)
    {
        Error = SourcePath == NULL ? ERROR_NOT_ENOUGH_MEMORY : GetLastError();
        goto Cleanup;
    }
    Required = GetFullPathNameW(Argument, 0, NULL, NULL);
    DestinationPath = Required == 0 ? NULL : Mem_Alloc((SIZE_T)Required * sizeof(WCHAR));
    if (DestinationPath == NULL || GetFullPathNameW(Argument, Required, DestinationPath, NULL) == 0)
    {
        Error = DestinationPath == NULL ? ERROR_NOT_ENOUGH_MEMORY : GetLastError();
        goto Cleanup;
    }
    SourceLength = wcslen(SourcePath);
    if (_wcsnicmp(SourcePath, DestinationPath, SourceLength) == 0 &&
        (DestinationPath[SourceLength] == UNICODE_NULL || DestinationPath[SourceLength] == L'\\'))
    {
        Error = ERROR_INVALID_PARAMETER;
        goto Cleanup;
    }
    Error = ZpUserProfile_CopyDirectory(SourcePath, DestinationPath, 0);
Cleanup:
    Mem_Free(DestinationPath);
    Mem_Free(SourcePath);
    Mem_Free(Source);
    Mem_Free(Argument);
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusWin32, Error);
}
