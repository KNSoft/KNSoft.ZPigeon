#include <KNSoft/MakeLifeEasier/System/Registry.h>

static const UNICODE_STRING ZpFontMachineKey = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts");
static const UNICODE_STRING ZpFontUserKey = RTL_CONSTANT_STRING(
    L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts");

#define ZP_FONT_USER 0x00000001

static
NTSTATUS
ZpFont_OpenKey(
    _In_ BOOLEAN User,
    _In_ ACCESS_MASK Access,
    _Out_ PHANDLE Key)
{
    HANDLE Root;
    OBJECT_ATTRIBUTES Attributes;
    ULONG Disposition;
    NTSTATUS Status;

    if (!User)
    {
        Status = Sys_RegOpenKey(Key, Access, &ZpFontMachineKey);
        if ((Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND) &&
            (Access & KEY_SET_VALUE) != 0)
        {
            InitializeObjectAttributes(&Attributes,
                                       (PUNICODE_STRING)&ZpFontMachineKey,
                                       OBJ_CASE_INSENSITIVE,
                                       NULL,
                                       NULL);
            Status = NtCreateKey(Key, Access, &Attributes, 0, NULL, REG_OPTION_NON_VOLATILE, &Disposition);
        }
        return Status;
    }
    Status = RtlOpenCurrentUser(KEY_READ | ((Access & KEY_SET_VALUE) != 0 ? KEY_CREATE_SUB_KEY : 0), &Root);
    if (NT_SUCCESS(Status))
    {
        Status = Sys_RegOpenKeyEx(Key, Root, Access, &ZpFontUserKey);
        if ((Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND) &&
            (Access & KEY_SET_VALUE) != 0)
        {
            InitializeObjectAttributes(&Attributes,
                                       (PUNICODE_STRING)&ZpFontUserKey,
                                       OBJ_CASE_INSENSITIVE,
                                       Root,
                                       NULL);
            Status = NtCreateKey(Key, Access, &Attributes, 0, NULL, REG_OPTION_NON_VOLATILE, &Disposition);
        }
        NtClose(Root);
    }
    return Status;
}

static
NTSTATUS
ZpFont_AddScope(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ BOOLEAN User)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL, NewInformation;
    HANDLE Key;
    PWSTR Identity = NULL, Name = NULL, Value = NULL;
    ULONG Index = 0, Length = 1024, Required, NameLength, ValueLength;
    NTSTATUS Status;

    Status = ZpFont_OpenKey(User, KEY_QUERY_VALUE, &Key);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND) return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status)) return Status;
    Information = Mem_Alloc(Length);
    if (Information == NULL)
    {
        NtClose(Key);
        return STATUS_NO_MEMORY;
    }
    for (;; Index++)
    {
        for (;;)
        {
            Status = NtEnumerateValueKey(Key,
                                         Index,
                                         KeyValueFullInformation,
                                         Information,
                                         Length,
                                         &Required);
            if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL) break;
            NewInformation = Mem_ReAlloc(Information, Required);
            if (NewInformation == NULL)
            {
                Status = STATUS_NO_MEMORY;
                break;
            }
            Information = NewInformation;
            Length = Required;
        }
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status)) break;
        if (Information->Type != REG_SZ || Information->NameLength % sizeof(WCHAR) != 0 ||
            Information->DataLength % sizeof(WCHAR) != 0)
        {
            continue;
        }
        NameLength = Information->NameLength / sizeof(WCHAR);
        ValueLength = Information->DataLength / sizeof(WCHAR);
        while (ValueLength != 0 &&
               ((PCWCHAR)Add2Ptr(Information, Information->DataOffset))[ValueLength - 1] == UNICODE_NULL)
        {
            ValueLength--;
        }
        Identity = Mem_Alloc(((SIZE_T)NameLength + 9) * sizeof(WCHAR));
        Name = Mem_Alloc(((SIZE_T)NameLength + 1) * sizeof(WCHAR));
        Value = Mem_Alloc(((SIZE_T)ValueLength + 1) * sizeof(WCHAR));
        if (Identity == NULL || Name == NULL || Value == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        RtlCopyMemory(Name, Information->Name, Information->NameLength);
        Name[NameLength] = UNICODE_NULL;
        RtlCopyMemory(Value,
                      Add2Ptr(Information, Information->DataOffset),
                      (SIZE_T)ValueLength * sizeof(WCHAR));
        Value[ValueLength] = UNICODE_NULL;
        _snwprintf_s(Identity,
                     NameLength + 9,
                     _TRUNCATE,
                     User ? L"user\n%s" : L"machine\n%s",
                     Name);
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindFont,
                                             0,
                                             User ? ZP_FONT_USER : 0,
                                             0,
                                             Identity,
                                             Name,
                                             NULL,
                                             Value);
        Mem_Free(Value);
        Mem_Free(Name);
        Mem_Free(Identity);
        Value = Name = Identity = NULL;
        if (!NT_SUCCESS(Status)) break;
    }
    Mem_Free(Value);
    Mem_Free(Name);
    Mem_Free(Identity);
    Mem_Free(Information);
    NtClose(Key);
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateFonts(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    NTSTATUS Status;

    Status = ZpFont_AddScope(&Builder, FALSE);
    if (NT_SUCCESS(Status)) Status = ZpFont_AddScope(&Builder, TRUE);
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
LOGICAL
ZpFont_IsSupportedFile(
    _In_ PCWSTR Path)
{
    PCWSTR Extension = wcsrchr(Path, L'.');

    return Extension != NULL &&
           (_wcsicmp(Extension, L".ttf") == 0 || _wcsicmp(Extension, L".otf") == 0 ||
            _wcsicmp(Extension, L".ttc") == 0 || _wcsicmp(Extension, L".fon") == 0);
}

static
NTSTATUS
ZpFont_GetDirectory(
    _In_ BOOLEAN User,
    _Out_writes_(CharacterCount) PWSTR Directory,
    _In_ ULONG CharacterCount)
{
    DWORD Length;

    if (User)
    {
        Length = GetEnvironmentVariableW(L"LOCALAPPDATA", Directory, CharacterCount);
        if (Length == 0 || Length >= CharacterCount) return NTSTATUS_FROM_WIN32(GetLastError());
        if (_snwprintf_s(Directory + Length,
                         CharacterCount - Length,
                         _TRUNCATE,
                         L"\\Microsoft\\Windows\\Fonts") < 0)
        {
            return STATUS_NAME_TOO_LONG;
        }
    }
    else
    {
        Length = GetWindowsDirectoryW(Directory, CharacterCount);
        if (Length == 0 || Length >= CharacterCount) return NTSTATUS_FROM_WIN32(GetLastError());
        if (_snwprintf_s(Directory + Length,
                         CharacterCount - Length,
                         _TRUNCATE,
                         L"\\Fonts") < 0)
        {
            return STATUS_NAME_TOO_LONG;
        }
    }
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpFont_Install(
    _In_ BOOLEAN User,
    _In_ PCWSTR Source)
{
    HANDLE Key;
    WCHAR Directory[MAX_PATH], Destination[MAX_PATH];
    PCWSTR Name;
    UNICODE_STRING ValueName;
    NTSTATUS Status;
    DWORD Error = ERROR_SUCCESS;

    if (!ZpFont_IsSupportedFile(Source)) return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    Name = wcsrchr(Source, L'\\');
    Name = Name == NULL ? Source : Name + 1;
    if (*Name == UNICODE_NULL) return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    Status = ZpFont_GetDirectory(User, Directory, ARRAYSIZE(Directory));
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    if (User && !CreateDirectoryW(Directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (_snwprintf_s(Destination, ARRAYSIZE(Destination), _TRUNCATE, L"%s\\%s", Directory, Name) < 0)
    {
        return ZpStatus_FromNtStatus(STATUS_NAME_TOO_LONG);
    }
    if (!CopyFileW(Source, Destination, TRUE)) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    if (AddFontResourceExW(Destination, 0, NULL) == 0)
    {
        Error = GetLastError();
        Status = NTSTATUS_FROM_WIN32(Error == ERROR_SUCCESS ? ERROR_INVALID_DATA : Error);
    }
    else
    {
        Status = ZpFont_OpenKey(User, KEY_SET_VALUE, &Key);
        if (NT_SUCCESS(Status))
        {
            RtlInitUnicodeString(&ValueName, Name);
            Status = NtSetValueKey(Key,
                                   &ValueName,
                                   0,
                                   REG_SZ,
                                   User ? (PVOID)Destination : (PVOID)Name,
                                   ((ULONG)wcslen(User ? Destination : Name) + 1) * sizeof(WCHAR));
            NtClose(Key);
        }
    }
    if (!NT_SUCCESS(Status))
    {
        RemoveFontResourceExW(Destination, 0, NULL);
        DeleteFileW(Destination);
    }
    else SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0, SMTO_ABORTIFHUNG, 2000, NULL);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpFont_Uninstall(
    _Inout_ PWSTR Identity)
{
    PKEY_VALUE_PARTIAL_INFORMATION Data;
    HANDLE Key;
    PWSTR Name, Path;
    WCHAR Directory[MAX_PATH], FullPath[MAX_PATH], CanonicalDirectory[MAX_PATH], CanonicalPath[MAX_PATH];
    UNICODE_STRING ValueName;
    BOOLEAN User;
    NTSTATUS Status;
    DWORD Error = ERROR_SUCCESS, DirectoryLength;

    Name = wcschr(Identity, L'\n');
    if (Name == NULL || *++Name == UNICODE_NULL) return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    Name[-1] = UNICODE_NULL;
    User = wcscmp(Identity, L"user") == 0;
    if (!User && wcscmp(Identity, L"machine") != 0)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    RtlInitUnicodeString(&ValueName, Name);
    Status = ZpFont_OpenKey(User, KEY_QUERY_VALUE | KEY_SET_VALUE, &Key);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Status = Sys_RegQueryData(Key, &ValueName, &Data);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Key);
        return ZpStatus_FromNtStatus(Status);
    }
    if (Data->Type != REG_SZ || Data->DataLength < sizeof(WCHAR) || Data->DataLength % sizeof(WCHAR) != 0 ||
        ((PCWCHAR)Data->Data)[Data->DataLength / sizeof(WCHAR) - 1] != UNICODE_NULL)
    {
        Status = STATUS_DATA_ERROR;
        goto Cleanup;
    }
    Path = (PWSTR)Data->Data;
    Status = ZpFont_GetDirectory(User, Directory, ARRAYSIZE(Directory));
    if (!NT_SUCCESS(Status)) goto Cleanup;
    if (wcschr(Path, L'\\') == NULL)
    {
        if (_snwprintf_s(FullPath, ARRAYSIZE(FullPath), _TRUNCATE, L"%s\\%s", Directory, Path) < 0)
        {
            Status = STATUS_NAME_TOO_LONG;
            goto Cleanup;
        }
        Path = FullPath;
    }
    DirectoryLength = GetFullPathNameW(Directory, ARRAYSIZE(CanonicalDirectory), CanonicalDirectory, NULL);
    if (DirectoryLength == 0)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    if (DirectoryLength >= ARRAYSIZE(CanonicalDirectory))
    {
        Status = STATUS_NAME_TOO_LONG;
        goto Cleanup;
    }
    Error = GetFullPathNameW(Path, ARRAYSIZE(CanonicalPath), CanonicalPath, NULL);
    if (Error == 0)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    if (Error >= ARRAYSIZE(CanonicalPath))
    {
        Status = STATUS_NAME_TOO_LONG;
        goto Cleanup;
    }
    if (_wcsnicmp(CanonicalPath, CanonicalDirectory, DirectoryLength) != 0 ||
        CanonicalPath[DirectoryLength] != L'\\')
    {
        Status = STATUS_ACCESS_DENIED;
        goto Cleanup;
    }
    Path = CanonicalPath;
    RemoveFontResourceExW(Path, 0, NULL);
    if (!DeleteFileW(Path))
    {
        Error = GetLastError();
        Status = NTSTATUS_FROM_WIN32(Error);
        goto Cleanup;
    }
    Status = NtDeleteValueKey(Key, &ValueName);
    if (NT_SUCCESS(Status))
    {
        SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0, SMTO_ABORTIFHUNG, 2000, NULL);
    }
Cleanup:
    Mem_Free(Data);
    NtClose(Key);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_ControlFont(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PWSTR Identity, Argument = NULL;
    ZP_STATUS Status;

    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    if (Control->Argument.Length != 0) Argument = ZpAdministration_CopyView(&Control->Argument);
    if (Control->Argument.Length != 0 && Argument == NULL)
    {
        Mem_Free(Identity);
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    if (Control->Action == ZpAdministrationActionInstall && Argument != NULL &&
        (wcscmp(Identity, L"user") == 0 || wcscmp(Identity, L"machine") == 0))
    {
        Status = ZpFont_Install(Identity[0] == L'u', Argument);
    }
    else if (Control->Action == ZpAdministrationActionUninstall)
    {
        Status = ZpFont_Uninstall(Identity);
    }
    else
    {
        Status = ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Mem_Free(Argument);
    Mem_Free(Identity);
    return Status;
}
