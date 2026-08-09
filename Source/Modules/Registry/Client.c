#include "Client.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <stdlib.h>

#define ZP_REGISTRY_SNAPSHOT_MAX_COUNT 65536
#define ZP_REGISTRY_SNAPSHOT_MAX_NAME_BYTES 0x01000000UL

typedef struct _ZP_REGISTRY_KEY_ENTRY
{
    PWCHAR Name;
    ULONG NameLength;
    ULONGLONG LastWriteTime;
} ZP_REGISTRY_KEY_ENTRY, *PZP_REGISTRY_KEY_ENTRY;

typedef struct _ZP_REGISTRY_VALUE_ENTRY
{
    PWCHAR Name;
    ULONG NameLength;
    ULONG Type;
    ULONG DataLength;
} ZP_REGISTRY_VALUE_ENTRY, *PZP_REGISTRY_VALUE_ENTRY;

static
NTSTATUS
ZpRegistry_BuildPath(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ PCZP_STRING_VIEW Path,
    _Out_ PUNICODE_STRING NativePath)
{
    static const UNICODE_STRING ClassesRoot =
        RTL_CONSTANT_STRING(L"\\Registry\\Machine\\Software\\Classes");
    static const UNICODE_STRING LocalMachine =
        RTL_CONSTANT_STRING(L"\\Registry\\Machine");
    static const UNICODE_STRING Users =
        RTL_CONSTANT_STRING(L"\\Registry\\User");
    static const UNICODE_STRING CurrentConfig = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Hardware Profiles\\Current");
    UNICODE_STRING CurrentUser, RootPath;
    SIZE_T PathBytes, Length;
    PWCHAR Buffer;
    NTSTATUS Status;
    LOGICAL FreeRoot = FALSE;

    switch (Root)
    {
    case ZpRegistryClassesRoot:
        RootPath = ClassesRoot;
        break;
    case ZpRegistryCurrentUser:
        Status = RtlFormatCurrentUserKeyPath(&CurrentUser);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        RootPath = CurrentUser;
        FreeRoot = TRUE;
        break;
    case ZpRegistryLocalMachine:
        RootPath = LocalMachine;
        break;
    case ZpRegistryUsers:
        RootPath = Users;
        break;
    case ZpRegistryCurrentConfig:
        RootPath = CurrentConfig;
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }
    PathBytes = (SIZE_T)Path->Length * sizeof(WCHAR);
    Length = RootPath.Length + (PathBytes != 0 ? sizeof(WCHAR) : 0) + PathBytes;
    if (Length > MAXUSHORT)
    {
        Status = STATUS_NAME_TOO_LONG;
        goto Cleanup;
    }
    Buffer = Mem_Alloc(Length);
    if (Buffer == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    RtlCopyMemory(Buffer, RootPath.Buffer, RootPath.Length);
    if (PathBytes != 0)
    {
        Buffer[RootPath.Length / sizeof(WCHAR)] = L'\\';
        RtlCopyMemory(Add2Ptr(Buffer, RootPath.Length + sizeof(WCHAR)),
                      Path->Buffer,
                      PathBytes);
    }
    NativePath->Buffer = Buffer;
    NativePath->Length = (USHORT)Length;
    NativePath->MaximumLength = (USHORT)Length;
    Status = STATUS_SUCCESS;

Cleanup:
    if (FreeRoot)
    {
        RtlFreeUnicodeString(&CurrentUser);
    }
    return Status;
}

static
ACCESS_MASK
ZpRegistry_GetViewAccess(
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
ZpRegistry_OpenKey(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ZP_REGISTRY_VIEW View,
    _In_ PCZP_STRING_VIEW Path,
    _In_ ACCESS_MASK Access,
    _Out_ PHANDLE Key)
{
    UNICODE_STRING NativePath;
    ACCESS_MASK ViewAccess;
    NTSTATUS Status;

    ViewAccess = ZpRegistry_GetViewAccess(View);
    if (ViewAccess == MAXULONG)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpRegistry_BuildPath(Root, Path, &NativePath);
    if (NT_SUCCESS(Status))
    {
        Status = Sys_RegOpenKey(Key, Access | ViewAccess, &NativePath);
        Mem_Free(NativePath.Buffer);
    }
    return Status;
}

static
int
ZpRegistry_CompareNames(
    _In_reads_(LeftLength) PCWCH Left,
    _In_ ULONG LeftLength,
    _In_reads_(RightLength) PCWCH Right,
    _In_ ULONG RightLength)
{
    int Result;

    if (LeftLength == 0 || RightLength == 0)
    {
        return LeftLength < RightLength ? -1 : LeftLength != RightLength;
    }
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
ZpRegistry_CompareKeyEntries(
    _In_ const VOID* Left,
    _In_ const VOID* Right)
{
    const ZP_REGISTRY_KEY_ENTRY* LeftEntry = Left;
    const ZP_REGISTRY_KEY_ENTRY* RightEntry = Right;

    return ZpRegistry_CompareNames(LeftEntry->Name,
                                         LeftEntry->NameLength,
                                         RightEntry->Name,
                                         RightEntry->NameLength);
}

static
int
__cdecl
ZpRegistry_CompareValueEntries(
    _In_ const VOID* Left,
    _In_ const VOID* Right)
{
    const ZP_REGISTRY_VALUE_ENTRY* LeftEntry = Left;
    const ZP_REGISTRY_VALUE_ENTRY* RightEntry = Right;

    return ZpRegistry_CompareNames(LeftEntry->Name,
                                         LeftEntry->NameLength,
                                         RightEntry->Name,
                                         RightEntry->NameLength);
}

static
VOID
ZpRegistry_FreeKeyEntries(
    _In_reads_opt_(Count) PZP_REGISTRY_KEY_ENTRY Entries,
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
ZpRegistry_FreeValueEntries(
    _In_reads_opt_(Count) PZP_REGISTRY_VALUE_ENTRY Entries,
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
ZpRegistry_QueryKeyInformation(
    _In_ HANDLE Key,
    _Outptr_ PKEY_FULL_INFORMATION* Information)
{
    PKEY_FULL_INFORMATION Buffer;
    ULONG Length;
    NTSTATUS Status;

    Status = NtQueryKey(Key, KeyFullInformation, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
    {
        return Status;
    }
    Buffer = Mem_Alloc(Length);
    if (Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = NtQueryKey(Key,
                        KeyFullInformation,
                        Buffer,
                        Length,
                        &Length);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    *Information = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpRegistry_EnumerateKeys(
    _In_ PCZP_REGISTRY_ENUMERATE_VIEW Request,
    _Outptr_result_buffer_(*EntryCount) PZP_REGISTRY_KEY_ENTRY* Entries,
    _Out_ PULONG EntryCount)
{
    PZP_REGISTRY_KEY_ENTRY Result;
    PKEY_FULL_INFORMATION FullInfo;
    PKEY_BASIC_INFORMATION KeyInfo;
    HANDLE Key;
    SIZE_T NameBytes;
    ULONG Count, Index, Length, InfoSize;
    NTSTATUS Status;

    *Entries = NULL;
    *EntryCount = 0;
    Status = ZpRegistry_OpenKey(Request->Root,
                                Request->View,
                                &Request->Path,
                                KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE,
                                &Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpRegistry_QueryKeyInformation(Key, &FullInfo);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Key);
        return Status;
    }
    Count = FullInfo->SubKeys;
    NameBytes = (SIZE_T)Count * FullInfo->MaxNameLength;
    if (FullInfo->MaxNameLength > ZP_REGISTRY_PATH_MAX_LENGTH * sizeof(WCHAR) ||
        Count > ZP_REGISTRY_SNAPSHOT_MAX_COUNT ||
        Count > MAXSIZE_T / sizeof(*Result) ||
        NameBytes > ZP_REGISTRY_SNAPSHOT_MAX_NAME_BYTES)
    {
        Mem_Free(FullInfo);
        NtClose(Key);
        return STATUS_QUOTA_EXCEEDED;
    }
    if (Count == 0)
    {
        Mem_Free(FullInfo);
        NtClose(Key);
        return STATUS_SUCCESS;
    }
    InfoSize = FIELD_OFFSET(KEY_BASIC_INFORMATION, Name) +
               FullInfo->MaxNameLength;
    Mem_Free(FullInfo);
    Result = Mem_Alloc((SIZE_T)Count * sizeof(*Result));
    KeyInfo = Mem_Alloc(InfoSize);
    if (Result == NULL || KeyInfo == NULL)
    {
        Mem_Free(Result);
        Mem_Free(KeyInfo);
        NtClose(Key);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Result, (SIZE_T)Count * sizeof(*Result));
    for (Index = 0; Index < Count; Index++)
    {
        Status = NtEnumerateKey(Key,
                                Index,
                                KeyBasicInformation,
                                KeyInfo,
                                InfoSize,
                                &Length);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status))
        {
            break;
        }
        Result[Index].Name = Mem_Alloc(KeyInfo->NameLength);
        if (Result[Index].Name == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        RtlCopyMemory(Result[Index].Name, KeyInfo->Name, KeyInfo->NameLength);
        Result[Index].NameLength = KeyInfo->NameLength / sizeof(WCHAR);
        Result[Index].LastWriteTime = KeyInfo->LastWriteTime.QuadPart;
    }
    Mem_Free(KeyInfo);
    NtClose(Key);
    if (!NT_SUCCESS(Status))
    {
        ZpRegistry_FreeKeyEntries(Result, Index + 1);
        return Status;
    }
    Count = Index;
    qsort(Result, Count, sizeof(*Result), ZpRegistry_CompareKeyEntries);
    *Entries = Result;
    *EntryCount = Count;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpRegistry_EnumerateValues(
    _In_ PCZP_REGISTRY_ENUMERATE_VIEW Request,
    _Outptr_result_buffer_(*EntryCount) PZP_REGISTRY_VALUE_ENTRY* Entries,
    _Out_ PULONG EntryCount)
{
    PZP_REGISTRY_VALUE_ENTRY Result;
    PKEY_FULL_INFORMATION FullInfo;
    PKEY_VALUE_FULL_INFORMATION ValueInfo;
    HANDLE Key;
    SIZE_T NameBytes, Size;
    ULONG Count, Index, Length, InfoSize;
    NTSTATUS Status;

    *Entries = NULL;
    *EntryCount = 0;
    Status = ZpRegistry_OpenKey(Request->Root,
                                Request->View,
                                &Request->Path,
                                KEY_QUERY_VALUE,
                                &Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpRegistry_QueryKeyInformation(Key, &FullInfo);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Key);
        return Status;
    }
    Count = FullInfo->Values;
    NameBytes = (SIZE_T)Count * FullInfo->MaxValueNameLength;
    Size = FIELD_OFFSET(KEY_VALUE_FULL_INFORMATION, Name) +
           (SIZE_T)FullInfo->MaxValueNameLength +
           FullInfo->MaxValueDataLength + sizeof(ULONGLONG);
    if (FullInfo->MaxValueNameLength > ZP_REGISTRY_PATH_MAX_LENGTH * sizeof(WCHAR) ||
        FullInfo->MaxValueDataLength > ZP_REGISTRY_DATA_MAX_LENGTH ||
        Count > ZP_REGISTRY_SNAPSHOT_MAX_COUNT ||
        Count > MAXSIZE_T / sizeof(*Result) ||
        NameBytes > ZP_REGISTRY_SNAPSHOT_MAX_NAME_BYTES ||
        Size > MAXULONG)
    {
        Mem_Free(FullInfo);
        NtClose(Key);
        return STATUS_QUOTA_EXCEEDED;
    }
    if (Count == 0)
    {
        Mem_Free(FullInfo);
        NtClose(Key);
        return STATUS_SUCCESS;
    }
    InfoSize = (ULONG)Size;
    Mem_Free(FullInfo);
    Result = Mem_Alloc((SIZE_T)Count * sizeof(*Result));
    ValueInfo = Mem_Alloc(InfoSize);
    if (Result == NULL || ValueInfo == NULL)
    {
        Mem_Free(Result);
        Mem_Free(ValueInfo);
        NtClose(Key);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Result, (SIZE_T)Count * sizeof(*Result));
    for (Index = 0; Index < Count; Index++)
    {
        Status = NtEnumerateValueKey(Key,
                                     Index,
                                     KeyValueFullInformation,
                                     ValueInfo,
                                     InfoSize,
                                     &Length);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status))
        {
            break;
        }
        Result[Index].Name = ValueInfo->NameLength != 0 ?
                                 Mem_Alloc(ValueInfo->NameLength) :
                                 NULL;
        if (ValueInfo->NameLength != 0 && Result[Index].Name == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        if (ValueInfo->NameLength != 0)
        {
            RtlCopyMemory(Result[Index].Name,
                          ValueInfo->Name,
                          ValueInfo->NameLength);
        }
        Result[Index].NameLength = ValueInfo->NameLength / sizeof(WCHAR);
        Result[Index].Type = ValueInfo->Type;
        Result[Index].DataLength = ValueInfo->DataLength;
    }
    Mem_Free(ValueInfo);
    NtClose(Key);
    if (!NT_SUCCESS(Status))
    {
        ZpRegistry_FreeValueEntries(Result, Index + 1);
        return Status;
    }
    Count = Index;
    qsort(Result, Count, sizeof(*Result), ZpRegistry_CompareValueEntries);
    *Entries = Result;
    *EntryCount = Count;
    return STATUS_SUCCESS;
}

static
ULONG
ZpRegistry_FindKeyStart(
    _In_reads_(Count) const ZP_REGISTRY_KEY_ENTRY* Entries,
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
        if (ZpRegistry_CompareNames(Entries[Index].Name,
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
ZpRegistry_FindValueStart(
    _In_reads_(Count) const ZP_REGISTRY_VALUE_ENTRY* Entries,
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
        if (ZpRegistry_CompareNames(Entries[Index].Name,
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
ZpRegistry_EncodeKeyPageResponse(
    _In_reads_(EntryCount) const ZP_REGISTRY_KEY_ENTRY* Entries,
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
ZpRegistry_EncodeValuePageResponse(
    _In_reads_(EntryCount) const ZP_REGISTRY_VALUE_ENTRY* Entries,
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
ZpRegistry_QueryValue(
    _In_ PCZP_REGISTRY_VALUE_REQUEST_VIEW Request,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    HANDLE Key = NULL;
    UNICODE_STRING ValueName;
    PKEY_VALUE_PARTIAL_INFORMATION Data = NULL;
    NTSTATUS Status;

    Status = ZpRegistry_OpenKey(Request->Root,
                                Request->View,
                                &Request->Path,
                                KEY_QUERY_VALUE,
                                &Key);
    if (NT_SUCCESS(Status))
    {
        ValueName.Buffer = (PWSTR)Request->ValueName.Buffer;
        ValueName.Length = (USHORT)(Request->ValueName.Length * sizeof(WCHAR));
        ValueName.MaximumLength = ValueName.Length;
        Status = Sys_RegQueryData(Key, &ValueName, &Data);
    }
    if (NT_SUCCESS(Status) && Data->DataLength > ZP_REGISTRY_DATA_MAX_LENGTH)
    {
        Status = STATUS_BUFFER_OVERFLOW;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpRegistry_EncodeValue(Data->Type,
                                        Data->Data,
                                        Data->DataLength,
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
        Status = ZpRegistry_EncodeValue(Data->Type,
                                        Data->Data,
                                        Data->DataLength,
                                        *Response,
                                        *ResponseLength,
                                        ResponseLength);
    }
    Mem_Free(Data);
    if (Key != NULL)
    {
        NtClose(Key);
    }
    return Status;
}

static
NTSTATUS
ZpRegistry_SetValue(
    _In_ PCZP_REGISTRY_SET_VALUE_VIEW Request)
{
    HANDLE Key = NULL;
    UNICODE_STRING ValueName;
    NTSTATUS Status;

    Status = ZpRegistry_OpenKey(Request->Root,
                                Request->View,
                                &Request->Path,
                                KEY_SET_VALUE,
                                &Key);
    if (NT_SUCCESS(Status))
    {
        ValueName.Buffer = (PWSTR)Request->ValueName.Buffer;
        ValueName.Length = (USHORT)(Request->ValueName.Length * sizeof(WCHAR));
        ValueName.MaximumLength = ValueName.Length;
        Status = NtSetValueKey(Key,
                               &ValueName,
                               0,
                               Request->Type,
                               (PVOID)Request->Data.Buffer,
                               Request->Data.Length);
    }
    if (Key != NULL)
    {
        NtClose(Key);
    }
    return Status;
}

static
NTSTATUS
ZpRegistry_DeleteValue(
    _In_ PCZP_REGISTRY_VALUE_REQUEST_VIEW Request)
{
    HANDLE Key = NULL;
    UNICODE_STRING ValueName;
    NTSTATUS Status;

    Status = ZpRegistry_OpenKey(Request->Root,
                                Request->View,
                                &Request->Path,
                                KEY_SET_VALUE,
                                &Key);
    if (NT_SUCCESS(Status))
    {
        ValueName.Buffer = (PWSTR)Request->ValueName.Buffer;
        ValueName.Length = (USHORT)(Request->ValueName.Length * sizeof(WCHAR));
        ValueName.MaximumLength = ValueName.Length;
        Status = NtDeleteValueKey(Key, &ValueName);
    }
    if (Key != NULL)
    {
        NtClose(Key);
    }
    return Status;
}

static
NTSTATUS
ZpRegistry_CreateKey(
    _In_ PCZP_REGISTRY_KEY_REQUEST_VIEW Request)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING NativePath;
    HANDLE Key = NULL;
    ACCESS_MASK ViewAccess;
    ULONG Disposition;
    NTSTATUS Status;

    ViewAccess = ZpRegistry_GetViewAccess(Request->View);
    if (ViewAccess == MAXULONG)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpRegistry_BuildPath(Request->Root, &Request->Path, &NativePath);
    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes,
                                   &NativePath,
                                   OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                   NULL,
                                   NULL);
        Status = NtCreateKey(&Key,
                             KEY_READ | ViewAccess,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_NON_VOLATILE,
                             &Disposition);
        Mem_Free(NativePath.Buffer);
    }
    if (Key != NULL)
    {
        NtClose(Key);
    }
    return Status;
}

static
NTSTATUS
ZpRegistry_DeleteKey(
    _In_ PCZP_REGISTRY_KEY_REQUEST_VIEW Request)
{
    HANDLE Key;
    NTSTATUS Status;

    Status = ZpRegistry_OpenKey(Request->Root,
                                Request->View,
                                &Request->Path,
                                DELETE,
                                &Key);
    if (NT_SUCCESS(Status))
    {
        Status = NtDeleteKey(Key);
        NtClose(Key);
    }
    return Status;
}

NTSTATUS
ZpRegistry_Execute(
    _In_ USHORT OperationId,
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_REGISTRY_ENUMERATE_VIEW Enumerate;
    ZP_REGISTRY_VALUE_REQUEST_VIEW ValueRequest;
    ZP_REGISTRY_SET_VALUE_VIEW SetValue;
    ZP_REGISTRY_KEY_REQUEST_VIEW KeyRequest;
    PZP_REGISTRY_KEY_ENTRY KeyEntries = NULL;
    PZP_REGISTRY_VALUE_ENTRY ValueEntries = NULL;
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
            Status = ZpRegistry_EnumerateKeys(&Enumerate,
                                              &KeyEntries,
                                              &EntryCount);
        }
        if (NT_SUCCESS(Status))
        {
            Start = ZpRegistry_FindKeyStart(KeyEntries,
                                                  EntryCount,
                                                  &Enumerate);
            Status = ZpRegistry_EncodeKeyPageResponse(KeyEntries,
                                                       EntryCount,
                                                       Start,
                                                       Enumerate.MaxEntries,
                                                       Response,
                                                       ResponseLength);
        }
        ZpRegistry_FreeKeyEntries(KeyEntries, EntryCount);
        return Status;

    case ZP_REGISTRY_OPERATION_ENUMERATE_VALUES_PAGE:
        Status = ZpRegistry_DecodeEnumerateRequest(Payload,
                                                   PayloadLength,
                                                   &Enumerate);
        if (NT_SUCCESS(Status))
        {
            Status = ZpRegistry_EnumerateValues(&Enumerate,
                                                &ValueEntries,
                                                &EntryCount);
        }
        if (NT_SUCCESS(Status))
        {
            Start = ZpRegistry_FindValueStart(ValueEntries,
                                                    EntryCount,
                                                    &Enumerate);
            Status = ZpRegistry_EncodeValuePageResponse(ValueEntries,
                                                         EntryCount,
                                                         Start,
                                                         Enumerate.MaxEntries,
                                                         Response,
                                                         ResponseLength);
        }
        ZpRegistry_FreeValueEntries(ValueEntries, EntryCount);
        return Status;

    case ZP_REGISTRY_OPERATION_QUERY_VALUE:
        Status = ZpRegistry_DecodeValueRequest(Payload,
                                               PayloadLength,
                                               &ValueRequest);
        return NT_SUCCESS(Status) ?
                   ZpRegistry_QueryValue(&ValueRequest,
                                               Response,
                                               ResponseLength) :
                   Status;

    case ZP_REGISTRY_OPERATION_SET_VALUE:
        Status = ZpRegistry_DecodeSetValueRequest(Payload,
                                                  PayloadLength,
                                                  &SetValue);
        return NT_SUCCESS(Status) ?
                   ZpRegistry_SetValue(&SetValue) : Status;

    case ZP_REGISTRY_OPERATION_DELETE_VALUE:
        Status = ZpRegistry_DecodeValueRequest(Payload,
                                               PayloadLength,
                                               &ValueRequest);
        return NT_SUCCESS(Status) ?
                   ZpRegistry_DeleteValue(&ValueRequest) : Status;

    case ZP_REGISTRY_OPERATION_CREATE_KEY:
        Status = ZpRegistry_DecodeKeyRequest(Payload,
                                             PayloadLength,
                                             &KeyRequest);
        return NT_SUCCESS(Status) ?
                   ZpRegistry_CreateKey(&KeyRequest) : Status;

    case ZP_REGISTRY_OPERATION_DELETE_KEY:
        Status = ZpRegistry_DecodeKeyRequest(Payload,
                                             PayloadLength,
                                             &KeyRequest);
        return NT_SUCCESS(Status) ?
                   ZpRegistry_DeleteKey(&KeyRequest) : Status;

    default:
        return STATUS_NOT_SUPPORTED;
    }
}
