#include "Client.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Security.h"
#include <stdlib.h>

#define ZP_REGISTRY_SNAPSHOT_MAX_COUNT 65536
#define ZP_REGISTRY_SNAPSHOT_MAX_NAME_BYTES 0x01000000UL
#define ZP_REGISTRY_DELETE_MAX_DEPTH 512

typedef ZP_REGISTRY_KEY_RECORD ZP_REGISTRY_KEY_ENTRY, *PZP_REGISTRY_KEY_ENTRY;
typedef ZP_REGISTRY_VALUE_RECORD ZP_REGISTRY_VALUE_ENTRY, *PZP_REGISTRY_VALUE_ENTRY;

static
NTSTATUS
ZpRegistry_CopyUnicodeString(
    _In_ PCZP_STRING_VIEW View,
    _Out_ PUNICODE_STRING String)
{
    SIZE_T Length = (SIZE_T)View->Length * sizeof(WCHAR);

    if (Length == 0)
    {
        RtlInitEmptyUnicodeString(String, NULL, 0);
        return STATUS_SUCCESS;
    }
    String->Buffer = Mem_Alloc(Length);
    if (String->Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlCopyMemory(String->Buffer, View->Buffer, Length);
    String->Length = (USHORT)Length;
    String->MaximumLength = String->Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpRegistry_BuildPath(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ PCZP_STRING_VIEW Path,
    _Out_ PUNICODE_STRING NativePath)
{
    static const UNICODE_STRING ClassesRoot = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\Software\\Classes");
    static const UNICODE_STRING LocalMachine = RTL_CONSTANT_STRING(L"\\Registry\\Machine");
    static const UNICODE_STRING Users = RTL_CONSTANT_STRING(L"\\Registry\\User");
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
        // The native namespace includes every currently mounted machine hive.
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
        RtlCopyMemory(Add2Ptr(Buffer, RootPath.Length + sizeof(WCHAR)), Path->Buffer, PathBytes);
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
NTSTATUS
ZpRegistry_OpenKey(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ PCZP_STRING_VIEW Path,
    _In_ ACCESS_MASK Access,
    _Out_ PHANDLE Key)
{
    UNICODE_STRING NativePath;
    NTSTATUS Status;

    Status = ZpRegistry_BuildPath(Root, Path, &NativePath);
    if (NT_SUCCESS(Status))
    {
        Status = Sys_RegOpenKey(Key, Access, &NativePath);
        Mem_Free(NativePath.Buffer);
    }
    return Status;
}

static
NTSTATUS
ZpRegistry_QuerySecurity(
    _In_ PCZP_REGISTRY_SECURITY_REQUEST_VIEW Request,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PUNICODE_STRING Sddl;
    BOOLEAN DaclProtected;
    HANDLE Key;
    NTSTATUS Status;

    Status = ZpRegistry_OpenKey(Request->Root, &Request->Path, READ_CONTROL, &Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpSecurity_QueryDacl(Key, &Sddl, &DaclProtected);
    NtClose(Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpRegistry_EncodeSecurityDescriptor(Sddl->Buffer,
                                                 Sddl->Length / sizeof(WCHAR),
                                                 DaclProtected,
                                                 NULL,
                                                 0,
                                                 ResponseLength);
    if (!NT_SUCCESS(Status))
    {
        NT_FreeStringW(Sddl);
        return Status;
    }
    *Response = Mem_Alloc(*ResponseLength);
    if (*Response == NULL)
    {
        NT_FreeStringW(Sddl);
        return STATUS_NO_MEMORY;
    }
    Status = ZpRegistry_EncodeSecurityDescriptor(Sddl->Buffer,
                                                 Sddl->Length / sizeof(WCHAR),
                                                 DaclProtected,
                                                 *Response,
                                                 *ResponseLength,
                                                 ResponseLength);
    NT_FreeStringW(Sddl);
    return Status;
}

static
NTSTATUS
ZpRegistry_SetSecurity(
    _In_ PCZP_REGISTRY_SECURITY_REQUEST_VIEW Request)
{
    PUNICODE_STRING Sddl;
    HANDLE Key;
    NTSTATUS Status;

    if (Request->Sddl.Length > (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR))
    {
        return STATUS_NAME_TOO_LONG;
    }
    Sddl = NT_AllocStringW((USHORT)Request->Sddl.Length);
    if (Sddl == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlCopyMemory(Sddl->Buffer,
                  Request->Sddl.Buffer,
                  (SIZE_T)Request->Sddl.Length * sizeof(WCHAR));
    Sddl->Buffer[Request->Sddl.Length] = UNICODE_NULL;
    Status = ZpRegistry_OpenKey(Request->Root,
                                &Request->Path,
                                READ_CONTROL | WRITE_DAC,
                                &Key);
    if (NT_SUCCESS(Status))
    {
        Status = ZpSecurity_SetDacl(Key,
                                    SE_REGISTRY_KEY,
                                    Sddl->Buffer,
                                    Request->DaclProtected);
        NtClose(Key);
    }
    NT_FreeStringW(Sddl);
    return Status;
}

static
NTSTATUS
ZpRegistry_JoinPath(
    _In_ PCZP_STRING_VIEW Path,
    _In_ PCZP_STRING_VIEW Name,
    _Out_ PZP_STRING_VIEW Result)
{
    SIZE_T Length = (SIZE_T)Path->Length + (Path->Length != 0) + Name->Length;
    PWCHAR Buffer;

    if (Length > ZP_REGISTRY_PATH_MAX_LENGTH)
    {
        return STATUS_NAME_TOO_LONG;
    }
    Buffer = Mem_Alloc(Length * sizeof(WCHAR));
    if (Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    if (Path->Length != 0)
    {
        RtlCopyMemory(Buffer, Path->Buffer, (SIZE_T)Path->Length * sizeof(WCHAR));
        Buffer[Path->Length] = L'\\';
    }
    RtlCopyMemory(Buffer + Path->Length + (Path->Length != 0), Name->Buffer, (SIZE_T)Name->Length * sizeof(WCHAR));
    Result->Buffer = Buffer;
    Result->Length = (ULONG)Length;
    return STATUS_SUCCESS;
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
    Result = CompareStringOrdinal(Left, (INT)LeftLength, Right, (INT)RightLength, TRUE);
    if (Result == CSTR_EQUAL)
    {
        Result = CompareStringOrdinal(Left, (INT)LeftLength, Right, (INT)RightLength, FALSE);
    }
    return Result == CSTR_LESS_THAN ? -1 : Result == CSTR_GREATER_THAN ? 1 : 0;
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

    return ZpRegistry_CompareNames(LeftEntry->Name, LeftEntry->NameLength, RightEntry->Name, RightEntry->NameLength);
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

    return ZpRegistry_CompareNames(LeftEntry->Name, LeftEntry->NameLength, RightEntry->Name, RightEntry->NameLength);
}

static
VOID
ZpRegistry_FreeKeyEntries(_In_opt_ PZP_REGISTRY_KEY_ENTRY Entries, _In_ ULONG Count)
{
    ULONG Index;

    if (Entries == NULL)
    {
        return;
    }
    for (Index = 0; Index < Count; Index++)
    {
        Mem_Free((PVOID)Entries[Index].Name);
    }
    Mem_Free(Entries);
}

static
VOID
ZpRegistry_FreeValueEntries(_In_opt_ PZP_REGISTRY_VALUE_ENTRY Entries, _In_ ULONG Count)
{
    ULONG Index;

    if (Entries == NULL)
    {
        return;
    }
    for (Index = 0; Index < Count; Index++)
    {
        Mem_Free((PVOID)Entries[Index].Name);
        Mem_Free((PVOID)Entries[Index].Preview);
    }
    Mem_Free(Entries);
}

static
NTSTATUS
ZpRegistry_QueryCachedInformation(_In_ HANDLE Key, _Out_ PKEY_CACHED_INFORMATION Information)
{
    ULONG Length;

    return NtQueryKey(Key, KeyCachedInformation, Information, sizeof(*Information), &Length);
}

static
NTSTATUS
ZpRegistry_QueryHasChildren(
    _In_ HANDLE Parent,
    _In_ PKEY_BASIC_INFORMATION Information,
    _Out_ PBOOLEAN HasChildren)
{
    KEY_CACHED_INFORMATION KeyInformation;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    HANDLE Key;
    NTSTATUS Status;

    Name.Buffer = Information->Name;
    Name.Length = (USHORT)Information->NameLength;
    Name.MaximumLength = Name.Length;
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Parent, NULL);
    Status = NtOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_ACCESS_DENIED || Status == STATUS_PRIVILEGE_NOT_HELD)
        {
            // Keep inaccessible keys expandable so opening them reports the actual error.
            *HasChildren = TRUE;
            return STATUS_SUCCESS;
        }
        return Status;
    }
    Status = ZpRegistry_QueryCachedInformation(Key, &KeyInformation);
    NtClose(Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    *HasChildren = KeyInformation.SubKeys != 0;
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
    PKEY_BASIC_INFORMATION KeyInfo;
    KEY_CACHED_INFORMATION Information;
    HANDLE Key;
    SIZE_T NameBytes;
    ULONG Capacity, Count = 0, Index, Length, InfoSize;
    NTSTATUS Status;

    Status = ZpRegistry_OpenKey(Request->Root, &Request->Path, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpRegistry_QueryCachedInformation(Key, &Information);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Key);
        return Status;
    }
    Capacity = Information.SubKeys;
    NameBytes = (SIZE_T)Capacity * Information.MaxNameLength;
    if (Information.MaxNameLength > ZP_REGISTRY_PATH_MAX_LENGTH * sizeof(WCHAR) ||
        Capacity > ZP_REGISTRY_SNAPSHOT_MAX_COUNT ||
        Capacity > MAXSIZE_T / sizeof(*Result) ||
        NameBytes > ZP_REGISTRY_SNAPSHOT_MAX_NAME_BYTES)
    {
        NtClose(Key);
        return STATUS_QUOTA_EXCEEDED;
    }
    if (Capacity == 0)
    {
        *Entries = NULL;
        *EntryCount = 0;
        NtClose(Key);
        return STATUS_SUCCESS;
    }
    InfoSize = FIELD_OFFSET(KEY_BASIC_INFORMATION, Name) + Information.MaxNameLength;
    Result = Mem_Alloc((SIZE_T)Capacity * sizeof(*Result));
    if (Result == NULL)
    {
        NtClose(Key);
        return STATUS_NO_MEMORY;
    }
    KeyInfo = Mem_Alloc(InfoSize);
    if (KeyInfo == NULL)
    {
        Mem_Free(Result);
        NtClose(Key);
        return STATUS_NO_MEMORY;
    }
    for (Index = 0; Index < Capacity; Index++)
    {
        Status = NtEnumerateKey(Key, Index, KeyBasicInformation, KeyInfo, InfoSize, &Length);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status))
        {
            break;
        }
        Status = ZpRegistry_QueryHasChildren(Key, KeyInfo, &Result[Count].HasChildren);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND ||
            Status == STATUS_OBJECT_PATH_NOT_FOUND ||
            Status == STATUS_KEY_DELETED)
        {
            Status = STATUS_SUCCESS;
            continue;
        }
        if (!NT_SUCCESS(Status))
        {
            break;
        }
        Result[Count].Name = Mem_Alloc(KeyInfo->NameLength);
        if (Result[Count].Name == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        RtlCopyMemory((PVOID)Result[Count].Name, KeyInfo->Name, KeyInfo->NameLength);
        Result[Count].NameLength = KeyInfo->NameLength / sizeof(WCHAR);
        Result[Count].LastWriteTime = KeyInfo->LastWriteTime.QuadPart;
        Count++;
    }
    Mem_Free(KeyInfo);
    NtClose(Key);
    if (!NT_SUCCESS(Status))
    {
        ZpRegistry_FreeKeyEntries(Result, Count);
        return Status;
    }
    if (Count > 1)
    {
        qsort(Result, Count, sizeof(*Result), ZpRegistry_CompareKeyEntries);
    }
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
    PKEY_VALUE_FULL_INFORMATION ValueInfo;
    KEY_CACHED_INFORMATION Information;
    HANDLE Key;
    PVOID Name, Preview;
    SIZE_T NameBytes, Size;
    ULONG AvailableData, Capacity, Count = 0, Index, Length, InfoSize, PreviewLength;
    NTSTATUS Status;

    Status = ZpRegistry_OpenKey(Request->Root, &Request->Path, KEY_QUERY_VALUE, &Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpRegistry_QueryCachedInformation(Key, &Information);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Key);
        return Status;
    }
    Capacity = Information.Values;
    NameBytes = (SIZE_T)Capacity * Information.MaxValueNameLength;
    Size = FIELD_OFFSET(KEY_VALUE_FULL_INFORMATION, Name) + (SIZE_T)Information.MaxValueNameLength +
           ZP_REGISTRY_VALUE_PREVIEW_MAX_LENGTH + sizeof(ULONGLONG);
    if (Information.MaxValueNameLength > ZP_REGISTRY_PATH_MAX_LENGTH * sizeof(WCHAR) ||
        Capacity > ZP_REGISTRY_SNAPSHOT_MAX_COUNT ||
        Capacity > MAXSIZE_T / sizeof(*Result) ||
        NameBytes > ZP_REGISTRY_SNAPSHOT_MAX_NAME_BYTES ||
        Size > MAXULONG)
    {
        NtClose(Key);
        return STATUS_QUOTA_EXCEEDED;
    }
    if (Capacity == 0)
    {
        *Entries = NULL;
        *EntryCount = 0;
        NtClose(Key);
        return STATUS_SUCCESS;
    }
    InfoSize = (ULONG)Size;
    Result = Mem_Alloc((SIZE_T)Capacity * sizeof(*Result));
    if (Result == NULL)
    {
        NtClose(Key);
        return STATUS_NO_MEMORY;
    }
    ValueInfo = Mem_Alloc(InfoSize);
    if (ValueInfo == NULL)
    {
        Mem_Free(Result);
        NtClose(Key);
        return STATUS_NO_MEMORY;
    }
    for (Index = 0; Index < Capacity; Index++)
    {
        Status = NtEnumerateValueKey(Key, Index, KeyValueFullInformation, ValueInfo, InfoSize, &Length);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW)
        {
            break;
        }
        Status = STATUS_SUCCESS;
        Name = ValueInfo->NameLength != 0 ? Mem_Alloc(ValueInfo->NameLength) : NULL;
        if (ValueInfo->NameLength != 0 && Name == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        if (ValueInfo->NameLength != 0)
        {
            RtlCopyMemory(Name, ValueInfo->Name, ValueInfo->NameLength);
        }
        AvailableData = ValueInfo->DataOffset < InfoSize ? InfoSize - ValueInfo->DataOffset : 0;
        PreviewLength = min(ValueInfo->DataLength, min(ZP_REGISTRY_VALUE_PREVIEW_MAX_LENGTH, AvailableData));
        Preview = PreviewLength != 0 ? Mem_Alloc(PreviewLength) : NULL;
        if (PreviewLength != 0 && Preview == NULL)
        {
            Mem_Free(Name);
            Status = STATUS_NO_MEMORY;
            break;
        }
        if (PreviewLength != 0)
        {
            RtlCopyMemory(Preview, Add2Ptr(ValueInfo, ValueInfo->DataOffset), PreviewLength);
        }
        Result[Count].Name = Name;
        Result[Count].NameLength = ValueInfo->NameLength / sizeof(WCHAR);
        Result[Count].Type = ValueInfo->Type;
        Result[Count].DataLength = ValueInfo->DataLength;
        Result[Count].Preview = Preview;
        Result[Count].PreviewLength = PreviewLength;
        Count++;
    }
    Mem_Free(ValueInfo);
    NtClose(Key);
    if (!NT_SUCCESS(Status))
    {
        ZpRegistry_FreeValueEntries(Result, Count);
        return Status;
    }
    if (Count > 1)
    {
        qsort(Result, Count, sizeof(*Result), ZpRegistry_CompareValueEntries);
    }
    *Entries = Result;
    *EntryCount = Count;
    return STATUS_SUCCESS;
}

static
ULONG
ZpRegistry_FindKeyStart(
    _In_reads_(Count) PCZP_REGISTRY_KEY_RECORD Entries,
    _In_ ULONG Count,
    _In_ PCZP_REGISTRY_ENUMERATE_VIEW Request)
{
    ULONG Left = 0, Middle, Right = Count;

    if (!Request->CursorPresent)
    {
        return 0;
    }
    while (Left < Right)
    {
        Middle = Left + (Right - Left) / 2;
        if (ZpRegistry_CompareNames(Entries[Middle].Name,
                                   Entries[Middle].NameLength,
                                   (PCWCH)Request->Cursor.Buffer,
                                   Request->Cursor.Length) > 0)
        {
            Right = Middle;
        }
        else
        {
            Left = Middle + 1;
        }
    }
    return Left;
}

static
ULONG
ZpRegistry_FindValueStart(
    _In_reads_(Count) PCZP_REGISTRY_VALUE_RECORD Entries,
    _In_ ULONG Count,
    _In_ PCZP_REGISTRY_ENUMERATE_VIEW Request)
{
    ULONG Left = 0, Middle, Right = Count;

    if (!Request->CursorPresent)
    {
        return 0;
    }
    while (Left < Right)
    {
        Middle = Left + (Right - Left) / 2;
        if (ZpRegistry_CompareNames(Entries[Middle].Name,
                                   Entries[Middle].NameLength,
                                   (PCWCH)Request->Cursor.Buffer,
                                   Request->Cursor.Length) > 0)
        {
            Right = Middle;
        }
        else
        {
            Left = Middle + 1;
        }
    }
    return Left;
}

static
NTSTATUS
ZpRegistry_QueryValueData(_In_ HANDLE Key,
                          _In_ PCUNICODE_STRING ValueName,
                          _Outptr_ PKEY_VALUE_PARTIAL_INFORMATION* Data)
{
    PKEY_VALUE_PARTIAL_INFORMATION Buffer;
    ULONG Length;
    NTSTATUS Status;

    Status = NtQueryValueKey(Key, (PUNICODE_STRING)ValueName, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
    {
        // A successful zero-length probe is not a valid NtQueryValueKey result.
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
    }
    if (Length < UFIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) ||
        Length - UFIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) > ZP_REGISTRY_DATA_MAX_LENGTH)
    {
        return STATUS_BUFFER_OVERFLOW;
    }
    Buffer = Mem_Alloc(Length);
    if (Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = NtQueryValueKey(Key, (PUNICODE_STRING)ValueName, KeyValuePartialInformation, Buffer, Length, &Length);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    if (Length < UFIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) ||
        Buffer->DataLength > ZP_REGISTRY_DATA_MAX_LENGTH ||
        Buffer->DataLength > Length - UFIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data))
    {
        Mem_Free(Buffer);
        return STATUS_BUFFER_OVERFLOW;
    }
    *Data = Buffer;
    return STATUS_SUCCESS;
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
    ULONG Available = EntryCount - Start;
    ULONG Count = min(Available, MaxEntries);
    PCZP_REGISTRY_KEY_RECORD Records = Count != 0 ? Entries + Start : NULL;
    PBYTE Buffer;
    PCWCH NextCursor = NULL;
    ULONG Length, NextCursorLength = 0;
    BOOLEAN HasMore;
    NTSTATUS Status;

    do
    {
        HasMore = Count < Available;
        NextCursor = HasMore ? Records[Count - 1].Name : NULL;
        NextCursorLength = HasMore ? Records[Count - 1].NameLength : 0;
        Status = ZpRegistry_EncodeKeyPage(HasMore, Records, Count, NextCursor, NextCursorLength, NULL, 0, &Length);
        if (Status == STATUS_BUFFER_OVERFLOW && Count > 1)
        {
            Count /= 2;
        }
        else
        {
            break;
        }
    } while (TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Buffer = Mem_Alloc(Length);
    if (Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = ZpRegistry_EncodeKeyPage(
        HasMore, Records, Count, NextCursor, NextCursorLength, Buffer, Length, &Length);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    *Response = Buffer;
    *ResponseLength = Length;
    return STATUS_SUCCESS;
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
    ULONG Available = EntryCount - Start;
    ULONG Count = min(Available, MaxEntries);
    PCZP_REGISTRY_VALUE_RECORD Records = Count != 0 ? Entries + Start : NULL;
    PBYTE Buffer;
    PCWCH NextCursor = NULL;
    ULONG Length, NextCursorLength = 0;
    BOOLEAN HasMore;
    NTSTATUS Status;

    do
    {
        HasMore = Count < Available;
        NextCursor = HasMore ? Records[Count - 1].Name : NULL;
        NextCursorLength = HasMore ? Records[Count - 1].NameLength : 0;
        Status = ZpRegistry_EncodeValuePage(HasMore, Records, Count, NextCursor, NextCursorLength, NULL, 0, &Length);
        if (Status == STATUS_BUFFER_OVERFLOW && Count > 1)
        {
            Count /= 2;
        }
        else
        {
            break;
        }
    } while (TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Buffer = Mem_Alloc(Length);
    if (Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = ZpRegistry_EncodeValuePage(
        HasMore, Records, Count, NextCursor, NextCursorLength, Buffer, Length, &Length);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    *Response = Buffer;
    *ResponseLength = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpRegistry_QueryValue(
    _In_ PCZP_REGISTRY_VALUE_REQUEST_VIEW Request,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PKEY_VALUE_PARTIAL_INFORMATION Data;
    UNICODE_STRING ValueName;
    HANDLE Key;
    PBYTE Buffer;
    ULONG Length;
    NTSTATUS Status;

    Status = ZpRegistry_OpenKey(Request->Root, &Request->Path, KEY_QUERY_VALUE, &Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpRegistry_CopyUnicodeString(&Request->ValueName, &ValueName);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Key);
        return Status;
    }
    Status = ZpRegistry_QueryValueData(Key, &ValueName, &Data);
    Mem_Free(ValueName.Buffer);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Key);
        return Status;
    }
    Status = ZpRegistry_EncodeValue(Data->Type, Data->Data, Data->DataLength, NULL, 0, &Length);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Data);
        NtClose(Key);
        return Status;
    }
    Buffer = Mem_Alloc(Length);
    if (Buffer == NULL)
    {
        Mem_Free(Data);
        NtClose(Key);
        return STATUS_NO_MEMORY;
    }
    Status = ZpRegistry_EncodeValue(Data->Type, Data->Data, Data->DataLength, Buffer, Length, &Length);
    Mem_Free(Data);
    NtClose(Key);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    *Response = Buffer;
    *ResponseLength = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpRegistry_QueryValueRange(
    _In_ PCZP_REGISTRY_RANGE_REQUEST_VIEW Request,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PKEY_VALUE_PARTIAL_INFORMATION Data;
    UNICODE_STRING ValueName;
    HANDLE Key;
    PBYTE Buffer;
    ULONG Length, RangeLength;
    NTSTATUS Status;

    Status = ZpRegistry_OpenKey(Request->Root, &Request->Path, KEY_QUERY_VALUE, &Key);
    if (!NT_SUCCESS(Status)) return Status;
    Status = ZpRegistry_CopyUnicodeString(&Request->ValueName, &ValueName);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Key);
        return Status;
    }
    Status = ZpRegistry_QueryValueData(Key, &ValueName, &Data);
    Mem_Free(ValueName.Buffer);
    NtClose(Key);
    if (!NT_SUCCESS(Status)) return Status;
    if (Data->Type != REG_BINARY)
    {
        Mem_Free(Data);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    if (Request->Offset > Data->DataLength)
    {
        Mem_Free(Data);
        return STATUS_END_OF_FILE;
    }
    RangeLength = min(Request->Length, Data->DataLength - Request->Offset);
    Status = ZpRegistry_EncodeRange(Data->DataLength,
                                    Data->Data + Request->Offset,
                                    RangeLength,
                                    NULL,
                                    0,
                                    &Length);
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpRegistry_EncodeRange(Data->DataLength,
                                        Data->Data + Request->Offset,
                                        RangeLength,
                                        Buffer,
                                        Length,
                                        &Length);
    }
    Mem_Free(Data);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    *Response = Buffer;
    *ResponseLength = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpRegistry_WriteValueRange(
    _In_ PCZP_REGISTRY_RANGE_WRITE_VIEW Request)
{
    PKEY_VALUE_PARTIAL_INFORMATION Data;
    UNICODE_STRING ValueName;
    HANDLE Key;
    NTSTATUS Status;

    Status = ZpRegistry_OpenKey(Request->Root,
                                &Request->Path,
                                KEY_QUERY_VALUE | KEY_SET_VALUE,
                                &Key);
    if (!NT_SUCCESS(Status)) return Status;
    Status = ZpRegistry_CopyUnicodeString(&Request->ValueName, &ValueName);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Key);
        return Status;
    }
    Status = ZpRegistry_QueryValueData(Key, &ValueName, &Data);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(ValueName.Buffer);
        NtClose(Key);
        return Status;
    }
    if (Data->Type != REG_BINARY)
    {
        Status = STATUS_OBJECT_TYPE_MISMATCH;
    }
    else if (Request->Offset > Data->DataLength ||
             Request->Data.Length > Data->DataLength - Request->Offset)
    {
        Status = STATUS_END_OF_FILE;
    }
    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(Data->Data + Request->Offset,
                      Request->Data.Buffer,
                      Request->Data.Length);
        Status = NtSetValueKey(Key,
                               &ValueName,
                               0,
                               REG_BINARY,
                               Data->Data,
                               Data->DataLength);
    }
    Mem_Free(Data);
    Mem_Free(ValueName.Buffer);
    NtClose(Key);
    return Status;
}

static
NTSTATUS
ZpRegistry_SetValue(
    _In_ PCZP_REGISTRY_SET_VALUE_VIEW Request)
{
    HANDLE Key;
    UNICODE_STRING ValueName;
    NTSTATUS Status;

    Status = ZpRegistry_OpenKey(Request->Root, &Request->Path, KEY_SET_VALUE, &Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpRegistry_CopyUnicodeString(&Request->ValueName, &ValueName);
    if (NT_SUCCESS(Status))
    {
        Status = NtSetValueKey(Key,
                               &ValueName,
                               0,
                               Request->Type,
                               (PVOID)Request->Data.Buffer,
                               Request->Data.Length);
        Mem_Free(ValueName.Buffer);
    }
    NtClose(Key);
    return Status;
}

static
NTSTATUS
ZpRegistry_DeleteValue(
    _In_ PCZP_REGISTRY_VALUE_REQUEST_VIEW Request)
{
    HANDLE Key;
    UNICODE_STRING ValueName;
    NTSTATUS Status;

    Status = ZpRegistry_OpenKey(Request->Root, &Request->Path, KEY_SET_VALUE, &Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpRegistry_CopyUnicodeString(&Request->ValueName, &ValueName);
    if (NT_SUCCESS(Status))
    {
        Status = NtDeleteValueKey(Key, &ValueName);
        Mem_Free(ValueName.Buffer);
    }
    NtClose(Key);
    return Status;
}

static
NTSTATUS
ZpRegistry_CreateKey(
    _In_ PCZP_REGISTRY_KEY_REQUEST_VIEW Request)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING NativePath;
    HANDLE Key;
    ULONG Disposition;
    NTSTATUS Status;

    Status = ZpRegistry_BuildPath(Request->Root, &Request->Path, &NativePath);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    InitializeObjectAttributes(
        &ObjectAttributes, &NativePath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtCreateKey(&Key, 0, &ObjectAttributes, 0, NULL, REG_OPTION_NON_VOLATILE, &Disposition);
    Mem_Free(NativePath.Buffer);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    NtClose(Key);
    return Disposition == REG_OPENED_EXISTING_KEY ? STATUS_OBJECT_NAME_COLLISION : STATUS_SUCCESS;
}

static
NTSTATUS
ZpRegistry_DeleteKeyTree(
    _In_ HANDLE Key,
    _In_ ULONG Depth)
{
    PKEY_BASIC_INFORMATION BasicInfo;
    KEY_CACHED_INFORMATION Information;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    HANDLE Child;
    ULONG BufferSize, ResultLength;
    NTSTATUS Status;

    if (Depth == ZP_REGISTRY_DELETE_MAX_DEPTH)
    {
        return STATUS_NAME_TOO_LONG;
    }
    Status = ZpRegistry_QueryCachedInformation(Key, &Information);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    BufferSize = FIELD_OFFSET(KEY_BASIC_INFORMATION, Name) + Information.MaxNameLength;
    BasicInfo = Mem_Alloc(BufferSize);
    if (BasicInfo == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    while (NT_SUCCESS(Status))
    {
        // Always remove index zero: deleting it compacts the remaining list.
        Status = NtEnumerateKey(Key, 0, KeyBasicInformation, BasicInfo, BufferSize, &ResultLength);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status))
        {
            break;
        }
        Name.Buffer = BasicInfo->Name;
        Name.Length = (USHORT)BasicInfo->NameLength;
        Name.MaximumLength = Name.Length;
        InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Key, NULL);
        Status = NtOpenKey(&Child, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | DELETE, &ObjectAttributes);
        if (NT_SUCCESS(Status))
        {
            Status = ZpRegistry_DeleteKeyTree(Child, Depth + 1);
            NtClose(Child);
        }
    }
    if (NT_SUCCESS(Status))
    {
        Status = NtDeleteKey(Key);
    }
    Mem_Free(BasicInfo);
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
                                &Request->Path,
                                KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | DELETE,
                                &Key);
    if (NT_SUCCESS(Status))
    {
        Status = ZpRegistry_DeleteKeyTree(Key, 0);
        NtClose(Key);
    }
    return Status;
}

static
NTSTATUS
ZpRegistry_RenameKey(
    _In_ PCZP_REGISTRY_RENAME_REQUEST_VIEW Request)
{
    ZP_STRING_VIEW Path;
    UNICODE_STRING NewName;
    HANDLE Key;
    NTSTATUS Status;

    Status = ZpRegistry_JoinPath(&Request->Path, &Request->Name, &Path);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpRegistry_OpenKey(Request->Root, &Path, KEY_WRITE, &Key);
    Mem_Free((PVOID)Path.Buffer);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpRegistry_CopyUnicodeString(&Request->NewName, &NewName);
    if (NT_SUCCESS(Status))
    {
        Status = NtRenameKey(Key, &NewName);
        Mem_Free(NewName.Buffer);
    }
    NtClose(Key);
    return Status;
}

static
NTSTATUS
ZpRegistry_RenameValue(
    _In_ PCZP_REGISTRY_RENAME_REQUEST_VIEW Request)
{
    PKEY_VALUE_PARTIAL_INFORMATION Data;
    UNICODE_STRING Name, NewName;
    HANDLE Key;
    ULONG RequiredLength;
    NTSTATUS Status;
    LOGICAL SameName;

    Status = ZpRegistry_OpenKey(Request->Root, &Request->Path, KEY_QUERY_VALUE | KEY_SET_VALUE, &Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpRegistry_CopyUnicodeString(&Request->Name, &Name);
    if (!NT_SUCCESS(Status))
    {
        NtClose(Key);
        return Status;
    }
    Status = ZpRegistry_CopyUnicodeString(&Request->NewName, &NewName);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Name.Buffer);
        NtClose(Key);
        return Status;
    }
    SameName = CompareStringOrdinal(Name.Buffer,
                                    Name.Length / sizeof(WCHAR),
                                    NewName.Buffer,
                                    NewName.Length / sizeof(WCHAR),
                                    TRUE) == CSTR_EQUAL;
    // Registry values have no native rename; copy first to avoid data loss.
    if (!SameName)
    {
        Status = NtQueryValueKey(Key, &NewName, KeyValueBasicInformation, NULL, 0, &RequiredLength);
        if (Status == STATUS_BUFFER_TOO_SMALL || Status == STATUS_BUFFER_OVERFLOW)
        {
            Mem_Free(NewName.Buffer);
            Mem_Free(Name.Buffer);
            NtClose(Key);
            return STATUS_OBJECT_NAME_COLLISION;
        }
        if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
        {
            Mem_Free(NewName.Buffer);
            Mem_Free(Name.Buffer);
            NtClose(Key);
            return Status;
        }
    }
    Status = ZpRegistry_QueryValueData(Key, &Name, &Data);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(NewName.Buffer);
        Mem_Free(Name.Buffer);
        NtClose(Key);
        return Status;
    }
    Status = NtSetValueKey(Key, &NewName, 0, Data->Type, Data->Data, Data->DataLength);
    if (NT_SUCCESS(Status) && !SameName)
    {
        Status = NtDeleteValueKey(Key, &Name);
    }
    Mem_Free(Data);
    Mem_Free(NewName.Buffer);
    Mem_Free(Name.Buffer);
    NtClose(Key);
    return Status;
}

NTSTATUS
ZpRegistry_Execute(
    _In_ BYTE OperationId,
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_REGISTRY_ENUMERATE_VIEW Enumerate;
    ZP_REGISTRY_VALUE_REQUEST_VIEW ValueRequest;
    ZP_REGISTRY_RANGE_REQUEST_VIEW RangeRequest;
    ZP_REGISTRY_RANGE_WRITE_VIEW RangeWrite;
    ZP_REGISTRY_SET_VALUE_VIEW SetValue;
    ZP_REGISTRY_KEY_REQUEST_VIEW KeyRequest;
    ZP_REGISTRY_RENAME_REQUEST_VIEW RenameRequest;
    ZP_REGISTRY_SECURITY_REQUEST_VIEW SecurityRequest;
    PZP_REGISTRY_KEY_ENTRY KeyEntries;
    PZP_REGISTRY_VALUE_ENTRY ValueEntries;
    ULONG EntryCount, Start;
    NTSTATUS Status;

    if (OperationId == ZP_REGISTRY_OPERATION_ENUMERATE_KEYS_PAGE)
    {
        Status = ZpRegistry_DecodeEnumerateRequest(Payload, PayloadLength, &Enumerate);
        if (!NT_SUCCESS(Status)) return Status;
        Status = ZpRegistry_EnumerateKeys(&Enumerate, &KeyEntries, &EntryCount);
        if (!NT_SUCCESS(Status)) return Status;
        Start = ZpRegistry_FindKeyStart(KeyEntries, EntryCount, &Enumerate);
        Status = ZpRegistry_EncodeKeyPageResponse(
            KeyEntries, EntryCount, Start, Enumerate.MaxEntries, Response, ResponseLength);
        ZpRegistry_FreeKeyEntries(KeyEntries, EntryCount);
        return Status;
    }
    else if (OperationId == ZP_REGISTRY_OPERATION_ENUMERATE_VALUES_PAGE)
    {
        Status = ZpRegistry_DecodeEnumerateRequest(Payload, PayloadLength, &Enumerate);
        if (!NT_SUCCESS(Status)) return Status;
        Status = ZpRegistry_EnumerateValues(&Enumerate, &ValueEntries, &EntryCount);
        if (!NT_SUCCESS(Status)) return Status;
        Start = ZpRegistry_FindValueStart(ValueEntries, EntryCount, &Enumerate);
        Status = ZpRegistry_EncodeValuePageResponse(
            ValueEntries, EntryCount, Start, Enumerate.MaxEntries, Response, ResponseLength);
        ZpRegistry_FreeValueEntries(ValueEntries, EntryCount);
        return Status;
    }
    else if (OperationId == ZP_REGISTRY_OPERATION_QUERY_VALUE)
    {
        Status = ZpRegistry_DecodeValueRequest(Payload, PayloadLength, &ValueRequest);
        return NT_SUCCESS(Status) ? ZpRegistry_QueryValue(&ValueRequest, Response, ResponseLength) : Status;
    }
    else if (OperationId == ZP_REGISTRY_OPERATION_QUERY_VALUE_RANGE)
    {
        Status = ZpRegistry_DecodeRangeRequest(Payload, PayloadLength, &RangeRequest);
        return NT_SUCCESS(Status) ?
                   ZpRegistry_QueryValueRange(&RangeRequest, Response, ResponseLength) : Status;
    }
    else if (OperationId == ZP_REGISTRY_OPERATION_WRITE_VALUE_RANGE)
    {
        Status = ZpRegistry_DecodeRangeWriteRequest(Payload, PayloadLength, &RangeWrite);
        if (NT_SUCCESS(Status)) Status = ZpRegistry_WriteValueRange(&RangeWrite);
    }
    else if (OperationId == ZP_REGISTRY_OPERATION_SET_VALUE)
    {
        Status = ZpRegistry_DecodeSetValueRequest(Payload, PayloadLength, &SetValue);
        if (NT_SUCCESS(Status)) Status = ZpRegistry_SetValue(&SetValue);
    }
    else if (OperationId == ZP_REGISTRY_OPERATION_DELETE_VALUE)
    {
        Status = ZpRegistry_DecodeValueRequest(Payload, PayloadLength, &ValueRequest);
        if (NT_SUCCESS(Status)) Status = ZpRegistry_DeleteValue(&ValueRequest);
    }
    else if (OperationId == ZP_REGISTRY_OPERATION_CREATE_KEY)
    {
        Status = ZpRegistry_DecodeKeyRequest(Payload, PayloadLength, &KeyRequest);
        if (NT_SUCCESS(Status)) Status = ZpRegistry_CreateKey(&KeyRequest);
    }
    else if (OperationId == ZP_REGISTRY_OPERATION_DELETE_KEY)
    {
        Status = ZpRegistry_DecodeKeyRequest(Payload, PayloadLength, &KeyRequest);
        if (NT_SUCCESS(Status)) Status = ZpRegistry_DeleteKey(&KeyRequest);
    }
    else if (OperationId == ZP_REGISTRY_OPERATION_RENAME_KEY ||
             OperationId == ZP_REGISTRY_OPERATION_RENAME_VALUE)
    {
        Status = ZpRegistry_DecodeRenameRequest(Payload, PayloadLength, &RenameRequest);
        if (NT_SUCCESS(Status))
        {
            Status = OperationId == ZP_REGISTRY_OPERATION_RENAME_KEY ? ZpRegistry_RenameKey(&RenameRequest) :
                                                                      ZpRegistry_RenameValue(&RenameRequest);
        }
    }
    else if (OperationId == ZP_REGISTRY_OPERATION_QUERY_SECURITY)
    {
        Status = ZpRegistry_DecodeSecurityRequest(Payload,
                                                  PayloadLength,
                                                  &SecurityRequest);
        return NT_SUCCESS(Status) ?
                   ZpRegistry_QuerySecurity(&SecurityRequest,
                                            Response,
                                            ResponseLength) : Status;
    }
    else if (OperationId == ZP_REGISTRY_OPERATION_SET_SECURITY)
    {
        Status = ZpRegistry_DecodeSecurityRequest(Payload,
                                                  PayloadLength,
                                                  &SecurityRequest);
        if (NT_SUCCESS(Status)) Status = ZpRegistry_SetSecurity(&SecurityRequest);
    }
    else
    {
        return STATUS_NOT_SUPPORTED;
    }
    return Status;
}
