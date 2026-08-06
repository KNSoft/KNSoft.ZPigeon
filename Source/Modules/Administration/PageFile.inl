#include <KNSoft/MakeLifeEasier/System/Registry.h>

static const UNICODE_STRING ZpPageFileKey = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management");
static const UNICODE_STRING ZpPageFileValue = RTL_CONSTANT_STRING(L"PagingFiles");

static
NTSTATUS
ZpPageFile_Parse(
    _Inout_ PWSTR Entry,
    _Out_ PWSTR* Path,
    _Out_ PULONG InitialSize,
    _Out_ PULONG MaximumSize)
{
    PWSTR Separator, End;
    ULONGLONG Value;

    Separator = wcsrchr(Entry, L' ');
    if (Separator == NULL)
    {
        if (*Entry == UNICODE_NULL) return STATUS_DATA_ERROR;
        *Path = Entry;
        *InitialSize = *MaximumSize = 0;
        return STATUS_SUCCESS;
    }
    *Separator++ = UNICODE_NULL;
    Value = _wcstoui64(Separator, &End, 10);
    if (*Separator == UNICODE_NULL || *End != UNICODE_NULL || Value > MAXULONG) return STATUS_DATA_ERROR;
    *MaximumSize = (ULONG)Value;
    Separator = wcsrchr(Entry, L' ');
    if (Separator == NULL) return STATUS_DATA_ERROR;
    *Separator++ = UNICODE_NULL;
    Value = _wcstoui64(Separator, &End, 10);
    if (*Separator == UNICODE_NULL || *End != UNICODE_NULL || Value > MAXULONG || *Entry == UNICODE_NULL)
    {
        return STATUS_DATA_ERROR;
    }
    *InitialSize = (ULONG)Value;
    *Path = Entry;
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpAdministration_EnumeratePageFiles(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PKEY_VALUE_PARTIAL_INFORMATION Data;
    HANDLE Key;
    PWSTR Entry, Path;
    SIZE_T Remaining, Length;
    ULONG InitialSize, MaximumSize;
    NTSTATUS Status;

    Status = Sys_RegOpenKey(&Key, KEY_QUERY_VALUE, &ZpPageFileKey);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Status = Sys_RegQueryData(Key, &ZpPageFileValue, &Data);
    NtClose(Key);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    if (Data->Type != REG_MULTI_SZ || Data->DataLength % sizeof(WCHAR) != 0)
    {
        Status = STATUS_DATA_ERROR;
        goto Cleanup;
    }
    Entry = (PWSTR)Data->Data;
    Remaining = Data->DataLength / sizeof(WCHAR);
    while (Remaining != 0 && *Entry != UNICODE_NULL)
    {
        Length = wcsnlen_s(Entry, Remaining);
        if (Length == Remaining)
        {
            Status = STATUS_DATA_ERROR;
            break;
        }
        Status = ZpPageFile_Parse(Entry, &Path, &InitialSize, &MaximumSize);
        if (!NT_SUCCESS(Status)) break;
        Status = ZpAdministration_AddRecord(&Builder,
                                             ZpAdministrationKindPageFile,
                                             InitialSize,
                                             0,
                                             MaximumSize,
                                             Path,
                                             Path,
                                             NULL,
                                             NULL);
        Entry += Length + 1;
        Remaining -= Length + 1;
    }
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
Cleanup:
    ZpAdministration_FreeBuilder(&Builder);
    Mem_Free(Data);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_ControlPageFile(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PKEY_VALUE_PARTIAL_INFORMATION Data = NULL;
    HANDLE Key;
    PWSTR Identity, Argument = NULL, Entries, Cursor, Entry, Path, Separator, End;
    SIZE_T Remaining, Length, Capacity;
    ULONG InitialSize, MaximumSize, DesiredInitialSize = 0, DesiredMaximumSize = 0;
    ULONGLONG Value;
    BOOLEAN Found = FALSE;
    NTSTATUS Status;

    if (Control->Action != ZpAdministrationActionConfigure &&
        Control->Action != ZpAdministrationActionDelete)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Control->Action == ZpAdministrationActionConfigure)
    {
        Argument = ZpAdministration_CopyView(&Control->Argument);
    }
    if (Identity == NULL || Control->Action == ZpAdministrationActionConfigure && Argument == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    if (*Identity == UNICODE_NULL || wcschr(Identity, L'\n') != NULL || wcschr(Identity, L'\r') != NULL)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
    if (Argument != NULL)
    {
        Separator = wcschr(Argument, L'|');
        if (Separator == NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        *Separator++ = UNICODE_NULL;
        Value = _wcstoui64(Argument, &End, 10);
        if (*Argument == UNICODE_NULL || *End != UNICODE_NULL || Value > MAXULONG)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        InitialSize = (ULONG)Value;
        Value = _wcstoui64(Separator, &End, 10);
        if (*Separator == UNICODE_NULL || *End != UNICODE_NULL || Value > MAXULONG ||
            (InitialSize == 0) != (Value == 0) || Value != 0 && Value < InitialSize)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        MaximumSize = (ULONG)Value;
        DesiredInitialSize = InitialSize;
        DesiredMaximumSize = MaximumSize;
    }
    Status = Sys_RegOpenKey(&Key, KEY_QUERY_VALUE | KEY_SET_VALUE, &ZpPageFileKey);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = Sys_RegQueryData(Key, &ZpPageFileValue, &Data);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Key);
        goto Cleanup;
    }
    if (Data->Type != REG_MULTI_SZ || Data->DataLength % sizeof(WCHAR) != 0)
    {
        Status = STATUS_DATA_ERROR;
        NtClose(Key);
        goto Cleanup;
    }
    Capacity = Data->DataLength / sizeof(WCHAR) + wcslen(Identity) + 32;
    Entries = Mem_Alloc(Capacity * sizeof(WCHAR));
    if (Entries == NULL)
    {
        Status = STATUS_NO_MEMORY;
        NtClose(Key);
        goto Cleanup;
    }
    Cursor = Entries;
    Entry = (PWSTR)Data->Data;
    Remaining = Data->DataLength / sizeof(WCHAR);
    while (Remaining != 0 && *Entry != UNICODE_NULL)
    {
        Length = wcsnlen_s(Entry, Remaining);
        if (Length == Remaining || !NT_SUCCESS(ZpPageFile_Parse(Entry, &Path, &InitialSize, &MaximumSize)))
        {
            Status = STATUS_DATA_ERROR;
            goto WriteCleanup;
        }
        if (_wcsicmp(Path, Identity) == 0)
        {
            Found = TRUE;
        }
        else
        {
            Cursor += _snwprintf_s(Cursor,
                                    Capacity - (Cursor - Entries),
                                    _TRUNCATE,
                                    L"%s %lu %lu",
                                    Path,
                                    InitialSize,
                                    MaximumSize) + 1;
        }
        Entry += Length + 1;
        Remaining -= Length + 1;
    }
    if (Control->Action == ZpAdministrationActionDelete && !Found)
    {
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
        goto WriteCleanup;
    }
    if (Control->Action == ZpAdministrationActionConfigure)
    {
        Cursor += _snwprintf_s(Cursor,
                                Capacity - (Cursor - Entries),
                                _TRUNCATE,
                                L"%s %lu %lu",
                                Identity,
                                DesiredInitialSize,
                                DesiredMaximumSize) + 1;
    }
    *Cursor++ = UNICODE_NULL;
    Status = NtSetValueKey(Key,
                           (PUNICODE_STRING)&ZpPageFileValue,
                           0,
                           REG_MULTI_SZ,
                           Entries,
                           (ULONG)((PBYTE)Cursor - (PBYTE)Entries));
WriteCleanup:
    Mem_Free(Entries);
    NtClose(Key);
Cleanup:
    Mem_Free(Data);
    Mem_Free(Argument);
    Mem_Free(Identity);
    return ZpStatus_FromNtStatus(Status);
}
