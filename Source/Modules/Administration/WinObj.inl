#include <KNSoft/NDK/NT/Ob/Misc.h>

static
PWSTR
ZpAdministration_QuerySymbolicLink(
    _In_ PCWSTR Path)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name, Target;
    HANDLE Link;
    PWSTR Buffer;
    PWSTR NewBuffer;
    ULONG Length;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenSymbolicLinkObject(&Link, SYMBOLIC_LINK_QUERY, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return NULL;
    Buffer = Mem_Alloc(0x1000 + sizeof(WCHAR));
    if (Buffer != NULL)
    {
        Target.Buffer = Buffer;
        Target.Length = 0;
        Target.MaximumLength = 0x1000;
        Status = NtQuerySymbolicLinkObject(Link, &Target, &Length);
        if (Status == STATUS_BUFFER_TOO_SMALL && Length <= MAXUSHORT)
        {
            NewBuffer = Mem_ReAlloc(Buffer, (SIZE_T)Length + sizeof(WCHAR));
            if (NewBuffer != NULL)
            {
                Buffer = NewBuffer;
                Target.Buffer = Buffer;
                Target.MaximumLength = (USHORT)Length;
                Status = NtQuerySymbolicLinkObject(Link, &Target, NULL);
            }
            else Status = STATUS_NO_MEMORY;
        }
        if (Buffer != NULL && NT_SUCCESS(Status)) Buffer[Target.Length / sizeof(WCHAR)] = UNICODE_NULL;
        else
        {
            Mem_Free(Buffer);
            Buffer = NULL;
        }
    }
    NtClose(Link);
    return Buffer;
}

static
ZP_STATUS
ZpAdministration_QueryObjectDirectory(
    _In_ PCZP_STRING_VIEW Identity,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    static const UNICODE_STRING DirectoryType = RTL_CONSTANT_STRING(L"Directory");
    static const UNICODE_STRING SymbolicLinkType = RTL_CONSTANT_STRING(L"SymbolicLink");
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    OBJECT_ATTRIBUTES ObjectAttributes;
    POBJECT_DIRECTORY_INFORMATION Entries;
    UNICODE_STRING Name;
    HANDLE Directory;
    PWSTR Path, FullPath, EntryName, TypeName, Target;
    PVOID Buffer;
    ULONG BufferLength = 0x10000, Context = 0, ReturnLength, Index;
    NTSTATUS Status;
    BOOLEAN RestartScan = TRUE;

    if (Identity->Length == 0 || Identity->Length > 32766 || Identity->Buffer[0] != L'\\' ||
        (Identity->Length > 1 && Identity->Buffer[Identity->Length - 1] == L'\\'))
    {
        return ZpStatus_FromNtStatus(STATUS_OBJECT_PATH_SYNTAX_BAD);
    }
    for (Index = 0; Index < Identity->Length; Index++)
    {
        if (Identity->Buffer[Index] == UNICODE_NULL)
        {
            return ZpStatus_FromNtStatus(STATUS_OBJECT_PATH_SYNTAX_BAD);
        }
    }
    Path = ZpAdministration_CopyView(Identity);
    if (Path == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenDirectoryObject(&Directory, DIRECTORY_QUERY, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Path);
        return ZpStatus_FromNtStatus(Status);
    }
    Buffer = Mem_Alloc(BufferLength);
    if (Buffer == NULL) Status = STATUS_NO_MEMORY;
    while (NT_SUCCESS(Status))
    {
        Status = NtQueryDirectoryObject(Directory,
                                        Buffer,
                                        BufferLength,
                                        FALSE,
                                        RestartScan,
                                        &Context,
                                        &ReturnLength);
        RestartScan = FALSE;
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status)) break;
        Entries = Buffer;
        for (Index = 0; Entries[Index].Name.Buffer != NULL; Index++)
        {
            SIZE_T FullLength = Identity->Length + (Identity->Length == 1 ? 0 : 1) +
                                Entries[Index].Name.Length / sizeof(WCHAR);

            FullPath = Mem_Alloc((FullLength + 1) * sizeof(WCHAR));
            EntryName = Mem_Alloc((SIZE_T)Entries[Index].Name.Length + sizeof(WCHAR));
            TypeName = Mem_Alloc((SIZE_T)Entries[Index].TypeName.Length + sizeof(WCHAR));
            if (FullPath == NULL || EntryName == NULL || TypeName == NULL)
            {
                Mem_Free(FullPath);
                Mem_Free(EntryName);
                Mem_Free(TypeName);
                Status = STATUS_NO_MEMORY;
                break;
            }
            RtlCopyMemory(FullPath, Path, (SIZE_T)Identity->Length * sizeof(WCHAR));
            if (Identity->Length != 1) FullPath[Identity->Length] = L'\\';
            RtlCopyMemory(FullPath + Identity->Length + (Identity->Length == 1 ? 0 : 1),
                          Entries[Index].Name.Buffer,
                          Entries[Index].Name.Length);
            FullPath[FullLength] = UNICODE_NULL;
            RtlCopyMemory(EntryName, Entries[Index].Name.Buffer, Entries[Index].Name.Length);
            EntryName[Entries[Index].Name.Length / sizeof(WCHAR)] = UNICODE_NULL;
            RtlCopyMemory(TypeName, Entries[Index].TypeName.Buffer, Entries[Index].TypeName.Length);
            TypeName[Entries[Index].TypeName.Length / sizeof(WCHAR)] = UNICODE_NULL;
            Target = RtlEqualUnicodeString(&Entries[Index].TypeName,
                                           (PUNICODE_STRING)&SymbolicLinkType,
                                           TRUE) ?
                         ZpAdministration_QuerySymbolicLink(FullPath) : NULL;
            Status = ZpAdministration_AddRecord(
                &Builder,
                RtlEqualUnicodeString(&Entries[Index].TypeName,
                                      (PUNICODE_STRING)&DirectoryType,
                                      TRUE) ?
                    ZpAdministrationKindObjectDirectory : ZpAdministrationKindObject,
                0,
                0,
                0,
                FullPath,
                EntryName,
                TypeName,
                Target);
            Mem_Free(Target);
            Mem_Free(TypeName);
            Mem_Free(EntryName);
            Mem_Free(FullPath);
            if (!NT_SUCCESS(Status)) break;
        }
    }
    Mem_Free(Buffer);
    NtClose(Directory);
    Mem_Free(Path);
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}
