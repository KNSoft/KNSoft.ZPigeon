#define ZP_ARCHIVE_OK 0
#define ZP_ARCHIVE_EOF 1
#define ZP_ARCHIVE_WARN (-20)
#define ZP_ARCHIVE_DIRECTORY 0040000
#define ZP_ARCHIVE_MAX_ENTRY_NAME_LENGTH 32767

// archiveint.dll exports libarchive's public C ABI declared by archive.h and archive_entry.h.
typedef PVOID(__cdecl* ZP_ARCHIVE_READ_NEW)(VOID);
typedef INT(__cdecl* ZP_ARCHIVE_READ_CONFIGURE)(PVOID Archive);
typedef INT(__cdecl* ZP_ARCHIVE_READ_OPEN_FILENAME_W)(PVOID Archive, PCWSTR Path, SIZE_T BlockSize);
typedef INT(__cdecl* ZP_ARCHIVE_READ_NEXT_HEADER)(PVOID Archive, PVOID* Entry);
typedef INT(__cdecl* ZP_ARCHIVE_READ_DATA_SKIP)(PVOID Archive);
typedef INT(__cdecl* ZP_ARCHIVE_READ_FREE)(PVOID Archive);
typedef PCWSTR(__cdecl* ZP_ARCHIVE_ENTRY_PATHNAME_W)(PVOID Entry);
typedef LONGLONG(__cdecl* ZP_ARCHIVE_ENTRY_SIZE)(PVOID Entry);
typedef USHORT(__cdecl* ZP_ARCHIVE_ENTRY_FILETYPE)(PVOID Entry);
typedef __time64_t(__cdecl* ZP_ARCHIVE_ENTRY_MTIME)(PVOID Entry);
typedef INT(__cdecl* ZP_ARCHIVE_ENTRY_MTIME_IS_SET)(PVOID Entry);

typedef struct _ZP_ARCHIVE_API
{
    HMODULE Module;
    ZP_ARCHIVE_READ_NEW ReadNew;
    ZP_ARCHIVE_READ_CONFIGURE SupportFilters;
    ZP_ARCHIVE_READ_CONFIGURE SupportFormats;
    ZP_ARCHIVE_READ_CONFIGURE SupportRawFormat;
    ZP_ARCHIVE_READ_OPEN_FILENAME_W OpenFilename;
    ZP_ARCHIVE_READ_NEXT_HEADER NextHeader;
    ZP_ARCHIVE_READ_DATA_SKIP SkipData;
    ZP_ARCHIVE_READ_FREE Free;
    ZP_ARCHIVE_ENTRY_PATHNAME_W EntryPathname;
    ZP_ARCHIVE_ENTRY_SIZE EntrySize;
    ZP_ARCHIVE_ENTRY_FILETYPE EntryFiletype;
    ZP_ARCHIVE_ENTRY_MTIME EntryMtime;
    ZP_ARCHIVE_ENTRY_MTIME_IS_SET EntryMtimeIsSet;
} ZP_ARCHIVE_API, *PZP_ARCHIVE_API;

typedef struct _ZP_ARCHIVE_ENUMERATION
{
    LIST_ENTRY ListEntry;
    ULONG Id;
    ZP_ARCHIVE_API Api;
    PVOID Archive;
    PVOID Current;
} ZP_ARCHIVE_ENUMERATION, *PZP_ARCHIVE_ENUMERATION;

FORCEINLINE
BOOLEAN
ZpFile_ArchiveSucceeded(
    _In_ INT Result)
{
    return Result == ZP_ARCHIVE_OK || Result == ZP_ARCHIVE_WARN;
}

static
NTSTATUS
ZpFile_LoadArchiveApi(
    _Out_ PZP_ARCHIVE_API Api)
{
    NTSTATUS Status = STATUS_SUCCESS;

    RtlZeroMemory(Api, sizeof(*Api));
    Api->Module = LoadLibraryExW(L"archiveint.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (Api->Module == NULL) return STATUS_DLL_NOT_FOUND;
    Api->ReadNew = (ZP_ARCHIVE_READ_NEW)GetProcAddress(Api->Module, "archive_read_new");
    Api->SupportFilters = (ZP_ARCHIVE_READ_CONFIGURE)GetProcAddress(
        Api->Module,
        "archive_read_support_filter_all");
    Api->SupportFormats = (ZP_ARCHIVE_READ_CONFIGURE)GetProcAddress(
        Api->Module,
        "archive_read_support_format_all");
    Api->SupportRawFormat = (ZP_ARCHIVE_READ_CONFIGURE)GetProcAddress(
        Api->Module,
        "archive_read_support_format_raw");
    Api->OpenFilename = (ZP_ARCHIVE_READ_OPEN_FILENAME_W)GetProcAddress(
        Api->Module,
        "archive_read_open_filename_w");
    Api->NextHeader = (ZP_ARCHIVE_READ_NEXT_HEADER)GetProcAddress(Api->Module, "archive_read_next_header");
    Api->SkipData = (ZP_ARCHIVE_READ_DATA_SKIP)GetProcAddress(Api->Module, "archive_read_data_skip");
    Api->Free = (ZP_ARCHIVE_READ_FREE)GetProcAddress(Api->Module, "archive_read_free");
    Api->EntryPathname = (ZP_ARCHIVE_ENTRY_PATHNAME_W)GetProcAddress(
        Api->Module,
        "archive_entry_pathname_w");
    Api->EntrySize = (ZP_ARCHIVE_ENTRY_SIZE)GetProcAddress(Api->Module, "archive_entry_size");
    Api->EntryFiletype = (ZP_ARCHIVE_ENTRY_FILETYPE)GetProcAddress(Api->Module, "archive_entry_filetype");
    Api->EntryMtime = (ZP_ARCHIVE_ENTRY_MTIME)GetProcAddress(Api->Module, "archive_entry_mtime");
    Api->EntryMtimeIsSet = (ZP_ARCHIVE_ENTRY_MTIME_IS_SET)GetProcAddress(
        Api->Module,
        "archive_entry_mtime_is_set");
    if (Api->ReadNew == NULL || Api->SupportFilters == NULL || Api->SupportFormats == NULL ||
        Api->SupportRawFormat == NULL ||
        Api->OpenFilename == NULL || Api->NextHeader == NULL || Api->SkipData == NULL ||
        Api->Free == NULL || Api->EntryPathname == NULL || Api->EntrySize == NULL ||
        Api->EntryFiletype == NULL || Api->EntryMtime == NULL || Api->EntryMtimeIsSet == NULL)
    {
        Status = STATUS_PROCEDURE_NOT_FOUND;
    }
    if (!NT_SUCCESS(Status))
    {
        FreeLibrary(Api->Module);
        Api->Module = NULL;
    }
    return Status;
}

static
ULONGLONG
ZpFile_ArchiveTimeToFileTime(
    _In_ __time64_t Value)
{
    const LONGLONG Epoch = 11644473600LL;

    return Value < -Epoch || Value > (LONGLONG)(MAXULONGLONG / 10000000ULL) - Epoch ?
               0 :
               (ULONGLONG)(Value + Epoch) * 10000000ULL;
}

static
VOID
ZpFile_DestroyArchiveEnumeration(
    _In_ PZP_ARCHIVE_ENUMERATION Enumeration)
{
    if (Enumeration->Archive != NULL) Enumeration->Api.Free(Enumeration->Archive);
    FreeLibrary(Enumeration->Api.Module);
    Mem_Free(Enumeration);
}

static
VOID
ZpFile_ResetArchiveEnumerations(
    _Inout_ PZP_CLIENT_OBJECT Client)
{
    LIST_ENTRY Enumerations;
    PLIST_ENTRY Entry;
    PZP_ARCHIVE_ENUMERATION Enumeration;

    InitializeListHead(&Enumerations);
    RtlAcquireSRWLockExclusive(&Client->ArchiveEnumerationLock);
    while (!IsListEmpty(&Client->ArchiveEnumerations))
    {
        Entry = RemoveHeadList(&Client->ArchiveEnumerations);
        InsertTailList(&Enumerations, Entry);
    }
    Client->ArchiveEnumerationCount = 0;
    RtlReleaseSRWLockExclusive(&Client->ArchiveEnumerationLock);
    while (!IsListEmpty(&Enumerations))
    {
        Enumeration = CONTAINING_RECORD(RemoveHeadList(&Enumerations),
                                        ZP_ARCHIVE_ENUMERATION,
                                        ListEntry);
        ZpFile_DestroyArchiveEnumeration(Enumeration);
    }
}

static
NTSTATUS
ZpFile_CreateArchiveEnumeration(
    _In_ PCZP_STRING_VIEW Path,
    _In_ ULONG EnumerationId,
    _In_ volatile LONG* Pending,
    _Outptr_ PZP_ARCHIVE_ENUMERATION* Result)
{
    PZP_ARCHIVE_ENUMERATION Enumeration;
    PUNICODE_STRING PathString = NULL;
    INT ArchiveResult;
    NTSTATUS Status;

    Enumeration = Mem_Alloc(sizeof(*Enumeration));
    if (Enumeration == NULL) return STATUS_NO_MEMORY;
    Enumeration->Id = EnumerationId;
    Enumeration->Archive = NULL;
    Enumeration->Current = NULL;
    Status = ZpFile_LoadArchiveApi(&Enumeration->Api);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Enumeration);
        return Status;
    }
    PathString = ZpFile_CopyPath(Path);
    if (PathString == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    Enumeration->Archive = Enumeration->Api.ReadNew();
    if (Enumeration->Archive == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    ArchiveResult = Enumeration->Api.SupportFilters(Enumeration->Archive);
    if (ZpFile_ArchiveSucceeded(ArchiveResult))
    {
        ArchiveResult = Enumeration->Api.SupportFormats(Enumeration->Archive);
    }
    if (ZpFile_ArchiveSucceeded(ArchiveResult))
    {
        ArchiveResult = Enumeration->Api.SupportRawFormat(Enumeration->Archive);
    }
    if (ZpFile_ArchiveSucceeded(ArchiveResult))
    {
        ArchiveResult = Enumeration->Api.OpenFilename(Enumeration->Archive,
                                                       PathString->Buffer,
                                                       0x10000);
    }
    if (!ZpFile_ArchiveSucceeded(ArchiveResult))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }
    if (!InterlockedCompareExchange(Pending, TRUE, TRUE))
    {
        Status = STATUS_CANCELLED;
        goto Cleanup;
    }
    ArchiveResult = Enumeration->Api.NextHeader(Enumeration->Archive, &Enumeration->Current);
    if (!ZpFile_ArchiveSucceeded(ArchiveResult) && ArchiveResult != ZP_ARCHIVE_EOF)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }
    if (ArchiveResult == ZP_ARCHIVE_EOF) Enumeration->Current = NULL;
    NT_FreeStringW(PathString);
    *Result = Enumeration;
    return STATUS_SUCCESS;
Cleanup:
    if (PathString != NULL) NT_FreeStringW(PathString);
    ZpFile_DestroyArchiveEnumeration(Enumeration);
    return Status;
}

static
NTSTATUS
ZpFile_ReadArchivePage(
    _Inout_ PZP_ARCHIVE_ENUMERATION Enumeration,
    _In_ volatile LONG* Pending,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_FILE_ENTRY Entries[ZP_FILE_PAGE_COUNT];
    ZP_FILE_RECORD Records[ZP_FILE_PAGE_COUNT];
    ULONG Count = 0, Index;
    NTSTATUS Status = STATUS_SUCCESS;

    while (Count < ZP_FILE_PAGE_COUNT)
    {
        PCWSTR Name;
        SIZE_T NameLength, AllocationSize;
        LONGLONG Size;
        PZP_FILE_ENTRY Entry;
        INT ArchiveResult;

        if (Enumeration->Current == NULL) break;
        if (!InterlockedCompareExchange(Pending, TRUE, TRUE))
        {
            Status = STATUS_CANCELLED;
            goto Cleanup;
        }
        Name = Enumeration->Api.EntryPathname(Enumeration->Current);
        NameLength = Name != NULL ? wcsnlen(Name, ZP_ARCHIVE_MAX_ENTRY_NAME_LENGTH + 1) : 0;
        if (NameLength == 0 || NameLength > ZP_ARCHIVE_MAX_ENTRY_NAME_LENGTH)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }
        AllocationSize = UFIELD_OFFSET(ZP_FILE_ENTRY, Name) + NameLength * sizeof(WCHAR);
        Entry = Mem_Alloc(AllocationSize);
        if (Entry == NULL)
        {
            Status = STATUS_NO_MEMORY;
            goto Cleanup;
        }
        RtlZeroMemory(&Entry->Info, sizeof(Entry->Info));
        Entry->NameLength = (ULONG)NameLength;
        RtlCopyMemory(Entry->Name, Name, NameLength * sizeof(WCHAR));
        Size = Enumeration->Api.EntrySize(Enumeration->Current);
        Entry->Info.Size = Size > 0 ? (ULONGLONG)Size : 0;
        if (Enumeration->Api.EntryMtimeIsSet(Enumeration->Current))
        {
            Entry->Info.LastWriteTime = ZpFile_ArchiveTimeToFileTime(
                Enumeration->Api.EntryMtime(Enumeration->Current));
        }
        if (Enumeration->Api.EntryFiletype(Enumeration->Current) == ZP_ARCHIVE_DIRECTORY)
        {
            Entry->Info.Attributes = FILE_ATTRIBUTE_DIRECTORY;
        }
        Entries[Count] = Entry;
        Records[Count].Info = Entry->Info;
        Records[Count].Name = Entry->Name;
        Records[Count].NameLength = Entry->NameLength;
        Count++;
        if (!ZpFile_ArchiveSucceeded(Enumeration->Api.SkipData(Enumeration->Archive)))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }
        ArchiveResult = Enumeration->Api.NextHeader(Enumeration->Archive,
                                                     &Enumeration->Current);
        if (ArchiveResult == ZP_ARCHIVE_EOF)
        {
            Enumeration->Current = NULL;
        }
        else if (!ZpFile_ArchiveSucceeded(ArchiveResult))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }
    }
    Status = ZpFile_EncodePage(Records,
                               Count,
                               Enumeration->Current != NULL ? Enumeration->Id : 0,
                               NULL,
                               0,
                               ResponseLength);
    if (NT_SUCCESS(Status))
    {
        *Response = Mem_Alloc(*ResponseLength);
        if (*Response == NULL) Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodePage(Records,
                                   Count,
                                   Enumeration->Current != NULL ? Enumeration->Id : 0,
                                   *Response,
                                   *ResponseLength,
                                   ResponseLength);
    }
Cleanup:
    for (Index = 0; Index < Count; Index++) Mem_Free(Entries[Index]);
    return Status;
}

static
NTSTATUS
ZpFile_EnumerateArchivePage(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PCZP_STRING_VIEW Path,
    _In_ ULONG EnumerationId,
    _In_ volatile LONG* Pending,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PLIST_ENTRY Entry;
    PZP_ARCHIVE_ENUMERATION Enumeration = NULL;
    NTSTATUS Status;

    RtlAcquireSRWLockShared(&Client->Lock);
    if (Client->State != ZpClientStateReady)
    {
        RtlReleaseSRWLockShared(&Client->Lock);
        return STATUS_CONNECTION_DISCONNECTED;
    }
    RtlAcquireSRWLockExclusive(&Client->ArchiveEnumerationLock);
    RtlReleaseSRWLockShared(&Client->Lock);
    if (EnumerationId == 0)
    {
        if (Client->ArchiveEnumerationCount >= ZP_FILE_ENUMERATION_MAX_COUNT)
        {
            RtlReleaseSRWLockExclusive(&Client->ArchiveEnumerationLock);
            return STATUS_QUOTA_EXCEEDED;
        }
        EnumerationId = Client->NextArchiveEnumerationId++;
        if (Client->NextArchiveEnumerationId == 0) Client->NextArchiveEnumerationId = 1;
        Status = ZpFile_CreateArchiveEnumeration(Path,
                                                 EnumerationId,
                                                 Pending,
                                                 &Enumeration);
        if (!NT_SUCCESS(Status))
        {
            RtlReleaseSRWLockExclusive(&Client->ArchiveEnumerationLock);
            return Status;
        }
        InsertTailList(&Client->ArchiveEnumerations, &Enumeration->ListEntry);
        Client->ArchiveEnumerationCount++;
    }
    else
    {
        for (Entry = Client->ArchiveEnumerations.Flink;
             Entry != &Client->ArchiveEnumerations;
             Entry = Entry->Flink)
        {
            Enumeration = CONTAINING_RECORD(Entry,
                                            ZP_ARCHIVE_ENUMERATION,
                                            ListEntry);
            if (Enumeration->Id == EnumerationId) break;
            Enumeration = NULL;
        }
        if (Enumeration == NULL)
        {
            RtlReleaseSRWLockExclusive(&Client->ArchiveEnumerationLock);
            return STATUS_INVALID_HANDLE;
        }
    }
    Status = ZpFile_ReadArchivePage(Enumeration, Pending, Response, ResponseLength);
    if (!NT_SUCCESS(Status) || Enumeration->Current == NULL)
    {
        RemoveEntryList(&Enumeration->ListEntry);
        Client->ArchiveEnumerationCount--;
        ZpFile_DestroyArchiveEnumeration(Enumeration);
    }
    RtlReleaseSRWLockExclusive(&Client->ArchiveEnumerationLock);
    return Status;
}
