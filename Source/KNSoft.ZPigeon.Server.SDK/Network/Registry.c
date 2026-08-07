#include "Registry.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>
#include <stdlib.h>

#pragma comment(lib, "Advapi32.lib")

#define ZP_SERVER_REGISTRY_SNAPSHOT_MAX_COUNT 65536
#define ZP_SERVER_REGISTRY_SNAPSHOT_MAX_NAME_BYTES 0x01000000UL

typedef struct _ZP_SERVER_REGISTRY_KEY_ENTRY
{
    PWCHAR Name;
    ULONG NameLength;
    ULONGLONG LastWriteTime;
} ZP_SERVER_REGISTRY_KEY_ENTRY, *PZP_SERVER_REGISTRY_KEY_ENTRY;

typedef struct _ZP_SERVER_REGISTRY_VALUE_ENTRY
{
    PWCHAR Name;
    ULONG NameLength;
    ULONG Type;
    ULONG DataLength;
} ZP_SERVER_REGISTRY_VALUE_ENTRY, *PZP_SERVER_REGISTRY_VALUE_ENTRY;

static
NTSTATUS
ZpServerRegistry_FromWin32(
    _In_ LSTATUS Error)
{
    return Error == ERROR_SUCCESS ?
               STATUS_SUCCESS :
               NTSTATUS_FROM_WIN32(Error);
}

static
HKEY
ZpServerRegistry_GetRoot(
    _In_ ZP_REGISTRY_ROOT Root)
{
    switch (Root)
    {
    case ZpRegistryClassesRoot:
        return HKEY_CLASSES_ROOT;
    case ZpRegistryCurrentUser:
        return HKEY_CURRENT_USER;
    case ZpRegistryLocalMachine:
        return HKEY_LOCAL_MACHINE;
    case ZpRegistryUsers:
        return HKEY_USERS;
    case ZpRegistryCurrentConfig:
        return HKEY_CURRENT_CONFIG;
    default:
        return NULL;
    }
}

static
REGSAM
ZpServerRegistry_GetView(
    _In_ ZP_REGISTRY_VIEW View)
{
    switch (View)
    {
    case ZpRegistryViewDefault:
        return 0;
    case ZpRegistryView32:
        return KEY_WOW64_32KEY;
    case ZpRegistryView64:
        return KEY_WOW64_64KEY;
    default:
        return MAXULONG;
    }
}

static
NTSTATUS
ZpServerRegistry_CopyString(
    _In_ PCZP_STRING_VIEW String,
    _Outptr_result_z_ PWCHAR* Copy)
{
    SIZE_T StringSize, Size;
    PWCHAR Buffer;

    StringSize = (SIZE_T)String->Length * sizeof(WCHAR);
    if (StringSize > MAXSIZE_T - sizeof(WCHAR))
    {
        return STATUS_INTEGER_OVERFLOW;
    }
    Size = StringSize + sizeof(WCHAR);
    Buffer = Mem_Alloc(Size);
    if (Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    if (StringSize != 0)
    {
        RtlCopyMemory(Buffer,
                      String->Buffer,
                      StringSize);
    }
    *(PWCHAR)Add2Ptr(Buffer, StringSize) = UNICODE_NULL;
    *Copy = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpServerRegistry_OpenKey(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ZP_REGISTRY_VIEW View,
    _In_ PCZP_STRING_VIEW Path,
    _In_ REGSAM Access,
    _Out_ PHKEY Key)
{
    PWCHAR PathString = NULL;
    HKEY RootKey;
    REGSAM ViewFlags;
    NTSTATUS Status;

    RootKey = ZpServerRegistry_GetRoot(Root);
    ViewFlags = ZpServerRegistry_GetView(View);
    if (RootKey == NULL || ViewFlags == MAXULONG)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpServerRegistry_CopyString(Path, &PathString);
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerRegistry_FromWin32(
                     RegOpenKeyExW(RootKey,
                                   PathString,
                                   0,
                                   Access | ViewFlags,
                                   Key));
    }
    Mem_Free(PathString);
    return Status;
}

static
int
ZpServerRegistry_CompareNames(
    _In_reads_(LeftLength) PCWCH Left,
    _In_ ULONG LeftLength,
    _In_reads_(RightLength) PCWCH Right,
    _In_ ULONG RightLength)
{
    int Result;

    Result = CompareStringOrdinal(Left,
                                  (INT)LeftLength,
                                  Right,
                                  (INT)RightLength,
                                  TRUE);
    if (Result == CSTR_EQUAL)
    {
        Result = CompareStringOrdinal(Left,
                                      (INT)LeftLength,
                                      Right,
                                      (INT)RightLength,
                                      FALSE);
    }
    return Result == CSTR_LESS_THAN ? -1 :
           Result == CSTR_GREATER_THAN ? 1 : 0;
}

static
int
__cdecl
ZpServerRegistry_CompareKeyEntries(
    _In_ const VOID* Left,
    _In_ const VOID* Right)
{
    const ZP_SERVER_REGISTRY_KEY_ENTRY* LeftEntry = Left;
    const ZP_SERVER_REGISTRY_KEY_ENTRY* RightEntry = Right;

    return ZpServerRegistry_CompareNames(LeftEntry->Name,
                                         LeftEntry->NameLength,
                                         RightEntry->Name,
                                         RightEntry->NameLength);
}

static
int
__cdecl
ZpServerRegistry_CompareValueEntries(
    _In_ const VOID* Left,
    _In_ const VOID* Right)
{
    const ZP_SERVER_REGISTRY_VALUE_ENTRY* LeftEntry = Left;
    const ZP_SERVER_REGISTRY_VALUE_ENTRY* RightEntry = Right;

    return ZpServerRegistry_CompareNames(LeftEntry->Name,
                                         LeftEntry->NameLength,
                                         RightEntry->Name,
                                         RightEntry->NameLength);
}

static
VOID
ZpServerRegistry_FreeKeyEntries(
    _In_reads_opt_(Count) PZP_SERVER_REGISTRY_KEY_ENTRY Entries,
    _In_ ULONG Count)
{
    ULONG Index;

    if (Entries == NULL)
    {
        return;
    }
    for (Index = 0; Index < Count; Index++)
    {
        Mem_Free(Entries[Index].Name);
    }
    Mem_Free(Entries);
}

static
VOID
ZpServerRegistry_FreeValueEntries(
    _In_reads_opt_(Count) PZP_SERVER_REGISTRY_VALUE_ENTRY Entries,
    _In_ ULONG Count)
{
    ULONG Index;

    if (Entries == NULL)
    {
        return;
    }
    for (Index = 0; Index < Count; Index++)
    {
        Mem_Free(Entries[Index].Name);
    }
    Mem_Free(Entries);
}

static
NTSTATUS
ZpServerRegistry_EnumerateKeys(
    _In_ PCZP_REGISTRY_ENUMERATE_VIEW Request,
    _Outptr_result_buffer_(*EntryCount) PZP_SERVER_REGISTRY_KEY_ENTRY* Entries,
    _Out_ PULONG EntryCount)
{
    PZP_SERVER_REGISTRY_KEY_ENTRY Result = NULL;
    HKEY Key = NULL;
    DWORD Count, MaxNameLength, Index;
    NTSTATUS Status;
    LSTATUS Error;

    *Entries = NULL;
    *EntryCount = 0;
    Status = ZpServerRegistry_OpenKey(Request->Root,
                                      Request->View,
                                      &Request->Path,
                                      KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE,
                                      &Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Error = RegQueryInfoKeyW(Key,
                             NULL,
                             NULL,
                             NULL,
                             &Count,
                             &MaxNameLength,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL);
    Status = ZpServerRegistry_FromWin32(Error);
    if (!NT_SUCCESS(Status) || Count == 0)
    {
        RegCloseKey(Key);
        return Status;
    }
    if (MaxNameLength > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Count > ZP_SERVER_REGISTRY_SNAPSHOT_MAX_COUNT ||
        Count > MAXSIZE_T / sizeof(*Result) ||
        (ULONGLONG)Count * (MaxNameLength + 1) * sizeof(WCHAR) >
            ZP_SERVER_REGISTRY_SNAPSHOT_MAX_NAME_BYTES)
    {
        RegCloseKey(Key);
        return STATUS_QUOTA_EXCEEDED;
    }
    Result = Mem_Alloc((SIZE_T)Count * sizeof(*Result));
    if (Result == NULL)
    {
        RegCloseKey(Key);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Result, (SIZE_T)Count * sizeof(*Result));
    for (Index = 0; Index < Count; Index++)
    {
        DWORD NameLength = MaxNameLength + 1;
        FILETIME LastWriteTime;
        ULARGE_INTEGER Time;

        Result[Index].Name = Mem_Alloc(
                                 ((SIZE_T)MaxNameLength + 1) * sizeof(WCHAR));
        if (Result[Index].Name == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        Error = RegEnumKeyExW(Key,
                              Index,
                              Result[Index].Name,
                              &NameLength,
                              NULL,
                              NULL,
                              NULL,
                              &LastWriteTime);
        if (Error == ERROR_NO_MORE_ITEMS)
        {
            Mem_Free(Result[Index].Name);
            Result[Index].Name = NULL;
            break;
        }
        if (Error != ERROR_SUCCESS)
        {
            Status = ZpServerRegistry_FromWin32(Error);
            break;
        }
        Time.LowPart = LastWriteTime.dwLowDateTime;
        Time.HighPart = LastWriteTime.dwHighDateTime;
        Result[Index].NameLength = NameLength;
        Result[Index].LastWriteTime = Time.QuadPart;
    }
    RegCloseKey(Key);
    if (!NT_SUCCESS(Status))
    {
        ZpServerRegistry_FreeKeyEntries(Result,
                                        Index < Count ? Index + 1 : Count);
        return Status;
    }
    Count = Index;
    qsort(Result,
          Count,
          sizeof(*Result),
          ZpServerRegistry_CompareKeyEntries);
    *Entries = Result;
    *EntryCount = Count;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpServerRegistry_EnumerateValues(
    _In_ PCZP_REGISTRY_ENUMERATE_VIEW Request,
    _Outptr_result_buffer_(*EntryCount) PZP_SERVER_REGISTRY_VALUE_ENTRY* Entries,
    _Out_ PULONG EntryCount)
{
    PZP_SERVER_REGISTRY_VALUE_ENTRY Result = NULL;
    HKEY Key = NULL;
    DWORD Count, MaxNameLength, Index;
    NTSTATUS Status;
    LSTATUS Error;

    *Entries = NULL;
    *EntryCount = 0;
    Status = ZpServerRegistry_OpenKey(Request->Root,
                                      Request->View,
                                      &Request->Path,
                                      KEY_QUERY_VALUE,
                                      &Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Error = RegQueryInfoKeyW(Key,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             &Count,
                             &MaxNameLength,
                             NULL,
                             NULL,
                             NULL);
    Status = ZpServerRegistry_FromWin32(Error);
    if (!NT_SUCCESS(Status) || Count == 0)
    {
        RegCloseKey(Key);
        return Status;
    }
    if (MaxNameLength > ZP_REGISTRY_PATH_MAX_LENGTH ||
        Count > ZP_SERVER_REGISTRY_SNAPSHOT_MAX_COUNT ||
        Count > MAXSIZE_T / sizeof(*Result) ||
        (ULONGLONG)Count * (MaxNameLength + 1) * sizeof(WCHAR) >
            ZP_SERVER_REGISTRY_SNAPSHOT_MAX_NAME_BYTES)
    {
        RegCloseKey(Key);
        return STATUS_QUOTA_EXCEEDED;
    }
    Result = Mem_Alloc((SIZE_T)Count * sizeof(*Result));
    if (Result == NULL)
    {
        RegCloseKey(Key);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Result, (SIZE_T)Count * sizeof(*Result));
    for (Index = 0; Index < Count; Index++)
    {
        DWORD NameLength = MaxNameLength + 1;
        DWORD DataLength = 0;

        Result[Index].Name = Mem_Alloc(
                                 ((SIZE_T)MaxNameLength + 1) * sizeof(WCHAR));
        if (Result[Index].Name == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        Error = RegEnumValueW(Key,
                              Index,
                              Result[Index].Name,
                              &NameLength,
                              NULL,
                              &Result[Index].Type,
                              NULL,
                              &DataLength);
        if (Error == ERROR_NO_MORE_ITEMS)
        {
            Mem_Free(Result[Index].Name);
            Result[Index].Name = NULL;
            break;
        }
        if (Error != ERROR_SUCCESS)
        {
            Status = ZpServerRegistry_FromWin32(Error);
            break;
        }
        if (DataLength > ZP_REGISTRY_DATA_MAX_LENGTH)
        {
            Status = STATUS_BUFFER_OVERFLOW;
            break;
        }
        Result[Index].NameLength = NameLength;
        Result[Index].DataLength = DataLength;
    }
    RegCloseKey(Key);
    if (!NT_SUCCESS(Status))
    {
        ZpServerRegistry_FreeValueEntries(Result,
                                          Index < Count ? Index + 1 : Count);
        return Status;
    }
    Count = Index;
    qsort(Result,
          Count,
          sizeof(*Result),
          ZpServerRegistry_CompareValueEntries);
    *Entries = Result;
    *EntryCount = Count;
    return STATUS_SUCCESS;
}

static
ULONG
ZpServerRegistry_FindKeyStart(
    _In_reads_(Count) const ZP_SERVER_REGISTRY_KEY_ENTRY* Entries,
    _In_ ULONG Count,
    _In_ PCZP_REGISTRY_ENUMERATE_VIEW Request)
{
    ULONG Index;

    if (!Request->CursorPresent)
    {
        return 0;
    }
    for (Index = 0; Index < Count; Index++)
    {
        if (ZpServerRegistry_CompareNames(Entries[Index].Name,
                                          Entries[Index].NameLength,
                                          (PCWCH)Request->Cursor.Buffer,
                                          Request->Cursor.Length) > 0)
        {
            break;
        }
    }
    return Index;
}

static
ULONG
ZpServerRegistry_FindValueStart(
    _In_reads_(Count) const ZP_SERVER_REGISTRY_VALUE_ENTRY* Entries,
    _In_ ULONG Count,
    _In_ PCZP_REGISTRY_ENUMERATE_VIEW Request)
{
    ULONG Index;

    if (!Request->CursorPresent)
    {
        return 0;
    }
    for (Index = 0; Index < Count; Index++)
    {
        if (ZpServerRegistry_CompareNames(Entries[Index].Name,
                                          Entries[Index].NameLength,
                                          (PCWCH)Request->Cursor.Buffer,
                                          Request->Cursor.Length) > 0)
        {
            break;
        }
    }
    return Index;
}

static
NTSTATUS
ZpServerRegistry_EncodeKeyPage(
    _In_reads_(EntryCount) const ZP_SERVER_REGISTRY_KEY_ENTRY* Entries,
    _In_ ULONG EntryCount,
    _In_ ULONG Start,
    _In_ ULONG MaxEntries,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_REGISTRY_KEY_RECORD Records = NULL;
    ULONG Available = EntryCount - Start;
    ULONG Count = min(Available, MaxEntries);
    ULONG Index;
    PCWCH NextCursor = NULL;
    ULONG NextCursorLength = 0;
    BOOLEAN HasMore;
    NTSTATUS Status;

    if (Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Count * sizeof(*Records));
        if (Records == NULL)
        {
            return STATUS_NO_MEMORY;
        }
        for (Index = 0; Index < Count; Index++)
        {
            Records[Index].Name = Entries[Start + Index].Name;
            Records[Index].NameLength = Entries[Start + Index].NameLength;
            Records[Index].LastWriteTime =
                Entries[Start + Index].LastWriteTime;
        }
    }
    do
    {
        HasMore = Count < Available;
        if (HasMore && (Records == NULL || Count == 0))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        NextCursor = HasMore ? Records[Count - 1].Name : NULL;
        NextCursorLength = HasMore ? Records[Count - 1].NameLength : 0;
        Status = ZpRegistry_EncodeKeyPage(
                     HasMore,
                     Records,
                     Count,
                     NextCursor,
                     NextCursorLength,
                     NULL,
                     0,
                     ResponseLength);
        if (Status == STATUS_BUFFER_OVERFLOW && Count > 1)
        {
            Count /= 2;
        }
        else
        {
            break;
        }
    } while (TRUE);
    *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
    if (NT_SUCCESS(Status) && *Response == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpRegistry_EncodeKeyPage(
                     HasMore,
                     Records,
                     Count,
                     NextCursor,
                     NextCursorLength,
                     *Response,
                     *ResponseLength,
                     ResponseLength);
    }
    Mem_Free(Records);
    return Status;
}

static
NTSTATUS
ZpServerRegistry_EncodeValuePage(
    _In_reads_(EntryCount) const ZP_SERVER_REGISTRY_VALUE_ENTRY* Entries,
    _In_ ULONG EntryCount,
    _In_ ULONG Start,
    _In_ ULONG MaxEntries,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_REGISTRY_VALUE_RECORD Records = NULL;
    ULONG Available = EntryCount - Start;
    ULONG Count = min(Available, MaxEntries);
    ULONG Index;
    PCWCH NextCursor = NULL;
    ULONG NextCursorLength = 0;
    BOOLEAN HasMore;
    NTSTATUS Status;

    if (Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Count * sizeof(*Records));
        if (Records == NULL)
        {
            return STATUS_NO_MEMORY;
        }
        for (Index = 0; Index < Count; Index++)
        {
            Records[Index].Name = Entries[Start + Index].Name;
            Records[Index].NameLength = Entries[Start + Index].NameLength;
            Records[Index].Type = Entries[Start + Index].Type;
            Records[Index].DataLength = Entries[Start + Index].DataLength;
        }
    }
    do
    {
        HasMore = Count < Available;
        if (HasMore && (Records == NULL || Count == 0))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        NextCursor = HasMore ? Records[Count - 1].Name : NULL;
        NextCursorLength = HasMore ? Records[Count - 1].NameLength : 0;
        Status = ZpRegistry_EncodeValuePage(
                     HasMore,
                     Records,
                     Count,
                     NextCursor,
                     NextCursorLength,
                     NULL,
                     0,
                     ResponseLength);
        if (Status == STATUS_BUFFER_OVERFLOW && Count > 1)
        {
            Count /= 2;
        }
        else
        {
            break;
        }
    } while (TRUE);
    *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
    if (NT_SUCCESS(Status) && *Response == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpRegistry_EncodeValuePage(
                     HasMore,
                     Records,
                     Count,
                     NextCursor,
                     NextCursorLength,
                     *Response,
                     *ResponseLength,
                     ResponseLength);
    }
    Mem_Free(Records);
    return Status;
}

static
NTSTATUS
ZpServerRegistry_QueryValue(
    _In_ PCZP_REGISTRY_VALUE_REQUEST_VIEW Request,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    HKEY Key = NULL;
    PWCHAR ValueName = NULL;
    PBYTE Data = NULL;
    DWORD Type, DataLength = 0;
    NTSTATUS Status;
    LSTATUS Error;

    Status = ZpServerRegistry_OpenKey(Request->Root,
                                      Request->View,
                                      &Request->Path,
                                      KEY_QUERY_VALUE,
                                      &Key);
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerRegistry_CopyString(&Request->ValueName, &ValueName);
    }
    if (NT_SUCCESS(Status))
    {
        Error = RegQueryValueExW(Key,
                                 ValueName,
                                 NULL,
                                 &Type,
                                 NULL,
                                 &DataLength);
        Status = ZpServerRegistry_FromWin32(Error);
    }
    if (NT_SUCCESS(Status) && DataLength > ZP_REGISTRY_DATA_MAX_LENGTH)
    {
        Status = STATUS_BUFFER_OVERFLOW;
    }
    Data = NT_SUCCESS(Status) && DataLength != 0 ?
               Mem_Alloc(DataLength) : NULL;
    if (NT_SUCCESS(Status) && DataLength != 0 && Data == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status) && DataLength != 0)
    {
        Error = RegQueryValueExW(Key,
                                 ValueName,
                                 NULL,
                                 &Type,
                                 Data,
                                 &DataLength);
        Status = ZpServerRegistry_FromWin32(Error);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpRegistry_EncodeValue(Type,
                                        Data,
                                        DataLength,
                                        NULL,
                                        0,
                                        ResponseLength);
    }
    *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
    if (NT_SUCCESS(Status) && *Response == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpRegistry_EncodeValue(Type,
                                        Data,
                                        DataLength,
                                        *Response,
                                        *ResponseLength,
                                        ResponseLength);
    }
    Mem_Free(Data);
    Mem_Free(ValueName);
    if (Key != NULL)
    {
        RegCloseKey(Key);
    }
    return Status;
}

static
NTSTATUS
ZpServerRegistry_SetValue(
    _In_ PCZP_REGISTRY_SET_VALUE_VIEW Request)
{
    HKEY Key = NULL;
    PWCHAR ValueName = NULL;
    NTSTATUS Status;

    Status = ZpServerRegistry_OpenKey(Request->Root,
                                      Request->View,
                                      &Request->Path,
                                      KEY_SET_VALUE,
                                      &Key);
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerRegistry_CopyString(&Request->ValueName, &ValueName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerRegistry_FromWin32(
                     RegSetValueExW(Key,
                                    ValueName,
                                    0,
                                    Request->Type,
                                    Request->Data.Buffer,
                                    Request->Data.Length));
    }
    Mem_Free(ValueName);
    if (Key != NULL)
    {
        RegCloseKey(Key);
    }
    return Status;
}

static
NTSTATUS
ZpServerRegistry_DeleteValue(
    _In_ PCZP_REGISTRY_VALUE_REQUEST_VIEW Request)
{
    HKEY Key = NULL;
    PWCHAR ValueName = NULL;
    NTSTATUS Status;

    Status = ZpServerRegistry_OpenKey(Request->Root,
                                      Request->View,
                                      &Request->Path,
                                      KEY_SET_VALUE,
                                      &Key);
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerRegistry_CopyString(&Request->ValueName, &ValueName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerRegistry_FromWin32(
                     RegDeleteValueW(Key, ValueName));
    }
    Mem_Free(ValueName);
    if (Key != NULL)
    {
        RegCloseKey(Key);
    }
    return Status;
}

static
NTSTATUS
ZpServerRegistry_CreateKey(
    _In_ PCZP_REGISTRY_KEY_REQUEST_VIEW Request)
{
    HKEY RootKey, Key = NULL;
    PWCHAR Path = NULL;
    REGSAM ViewFlags;
    NTSTATUS Status;

    RootKey = ZpServerRegistry_GetRoot(Request->Root);
    ViewFlags = ZpServerRegistry_GetView(Request->View);
    if (RootKey == NULL || ViewFlags == MAXULONG)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpServerRegistry_CopyString(&Request->Path, &Path);
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerRegistry_FromWin32(
                     RegCreateKeyExW(RootKey,
                                     Path,
                                     0,
                                     NULL,
                                     REG_OPTION_NON_VOLATILE,
                                     KEY_READ | ViewFlags,
                                     NULL,
                                     &Key,
                                     NULL));
    }
    Mem_Free(Path);
    if (Key != NULL)
    {
        RegCloseKey(Key);
    }
    return Status;
}

static
NTSTATUS
ZpServerRegistry_DeleteKey(
    _In_ PCZP_REGISTRY_KEY_REQUEST_VIEW Request)
{
    HKEY RootKey;
    PWCHAR Path = NULL;
    REGSAM ViewFlags;
    NTSTATUS Status;

    RootKey = ZpServerRegistry_GetRoot(Request->Root);
    ViewFlags = ZpServerRegistry_GetView(Request->View);
    if (RootKey == NULL || ViewFlags == MAXULONG)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpServerRegistry_CopyString(&Request->Path, &Path);
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerRegistry_FromWin32(
                     RegDeleteKeyExW(RootKey, Path, ViewFlags, 0));
    }
    Mem_Free(Path);
    return Status;
}

NTSTATUS
ZpServerRegistry_ProcessRequest(
    _In_ USHORT OperationId,
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_REGISTRY_ENUMERATE_VIEW Enumerate;
    ZP_REGISTRY_VALUE_REQUEST_VIEW ValueRequest;
    ZP_REGISTRY_SET_VALUE_VIEW SetValue;
    ZP_REGISTRY_KEY_REQUEST_VIEW KeyRequest;
    PZP_SERVER_REGISTRY_KEY_ENTRY KeyEntries = NULL;
    PZP_SERVER_REGISTRY_VALUE_ENTRY ValueEntries = NULL;
    ULONG EntryCount = 0, Start;
    NTSTATUS Status;

    *Response = NULL;
    *ResponseLength = 0;
    switch (OperationId)
    {
    case ZP_REGISTRY_OPERATION_ENUMERATE_KEYS_PAGE:
        Status = ZpRegistry_DecodeEnumerateRequest(Payload,
                                                   PayloadLength,
                                                   &Enumerate);
        if (NT_SUCCESS(Status))
        {
            Status = ZpServerRegistry_EnumerateKeys(&Enumerate,
                                                    &KeyEntries,
                                                    &EntryCount);
        }
        if (NT_SUCCESS(Status))
        {
            Start = ZpServerRegistry_FindKeyStart(KeyEntries,
                                                  EntryCount,
                                                  &Enumerate);
            Status = ZpServerRegistry_EncodeKeyPage(KeyEntries,
                                                    EntryCount,
                                                    Start,
                                                    Enumerate.MaxEntries,
                                                    Response,
                                                    ResponseLength);
        }
        ZpServerRegistry_FreeKeyEntries(KeyEntries, EntryCount);
        return Status;

    case ZP_REGISTRY_OPERATION_ENUMERATE_VALUES_PAGE:
        Status = ZpRegistry_DecodeEnumerateRequest(Payload,
                                                   PayloadLength,
                                                   &Enumerate);
        if (NT_SUCCESS(Status))
        {
            Status = ZpServerRegistry_EnumerateValues(&Enumerate,
                                                      &ValueEntries,
                                                      &EntryCount);
        }
        if (NT_SUCCESS(Status))
        {
            Start = ZpServerRegistry_FindValueStart(ValueEntries,
                                                    EntryCount,
                                                    &Enumerate);
            Status = ZpServerRegistry_EncodeValuePage(ValueEntries,
                                                      EntryCount,
                                                      Start,
                                                      Enumerate.MaxEntries,
                                                      Response,
                                                      ResponseLength);
        }
        ZpServerRegistry_FreeValueEntries(ValueEntries, EntryCount);
        return Status;

    case ZP_REGISTRY_OPERATION_QUERY_VALUE:
        Status = ZpRegistry_DecodeValueRequest(Payload,
                                               PayloadLength,
                                               &ValueRequest);
        return NT_SUCCESS(Status) ?
                   ZpServerRegistry_QueryValue(&ValueRequest,
                                               Response,
                                               ResponseLength) :
                   Status;

    case ZP_REGISTRY_OPERATION_SET_VALUE:
        Status = ZpRegistry_DecodeSetValueRequest(Payload,
                                                  PayloadLength,
                                                  &SetValue);
        return NT_SUCCESS(Status) ?
                   ZpServerRegistry_SetValue(&SetValue) : Status;

    case ZP_REGISTRY_OPERATION_DELETE_VALUE:
        Status = ZpRegistry_DecodeValueRequest(Payload,
                                               PayloadLength,
                                               &ValueRequest);
        return NT_SUCCESS(Status) ?
                   ZpServerRegistry_DeleteValue(&ValueRequest) : Status;

    case ZP_REGISTRY_OPERATION_CREATE_KEY:
        Status = ZpRegistry_DecodeKeyRequest(Payload,
                                             PayloadLength,
                                             &KeyRequest);
        return NT_SUCCESS(Status) ?
                   ZpServerRegistry_CreateKey(&KeyRequest) : Status;

    case ZP_REGISTRY_OPERATION_DELETE_KEY:
        Status = ZpRegistry_DecodeKeyRequest(Payload,
                                             PayloadLength,
                                             &KeyRequest);
        return NT_SUCCESS(Status) ?
                   ZpServerRegistry_DeleteKey(&KeyRequest) : Status;

    default:
        return STATUS_NOT_SUPPORTED;
    }
}
