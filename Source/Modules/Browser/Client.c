#include "Client.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <winsqlite/winsqlite3.h>
#include <strsafe.h>

#pragma comment(lib, "Version.lib")

typedef int (SQLITE_API *ZP_SQLITE_OPEN_V2)(const char*, sqlite3**, int, const char*);
typedef int (SQLITE_API *ZP_SQLITE_CLOSE)(sqlite3*);
typedef int (SQLITE_API *ZP_SQLITE_PREPARE_V2)(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
typedef int (SQLITE_API *ZP_SQLITE_STEP)(sqlite3_stmt*);
typedef int (SQLITE_API *ZP_SQLITE_FINALIZE)(sqlite3_stmt*);
typedef int (SQLITE_API *ZP_SQLITE_BIND_INT)(sqlite3_stmt*, int, int);
typedef int (SQLITE_API *ZP_SQLITE_BIND_INT64)(sqlite3_stmt*, int, sqlite3_int64);
typedef int (SQLITE_API *ZP_SQLITE_DESERIALIZE)(sqlite3*, const char*, unsigned char*, sqlite3_int64, sqlite3_int64, unsigned);
typedef int (SQLITE_API *ZP_SQLITE_COLUMN_INT)(sqlite3_stmt*, int);
typedef sqlite3_int64 (SQLITE_API *ZP_SQLITE_COLUMN_INT64)(sqlite3_stmt*, int);
typedef const void* (SQLITE_API *ZP_SQLITE_COLUMN_TEXT16)(sqlite3_stmt*, int);
typedef int (SQLITE_API *ZP_SQLITE_COLUMN_BYTES16)(sqlite3_stmt*, int);

typedef struct _ZP_SQLITE
{
    HMODULE Module;
    ZP_SQLITE_OPEN_V2 OpenV2;
    ZP_SQLITE_CLOSE Close;
    ZP_SQLITE_PREPARE_V2 PrepareV2;
    ZP_SQLITE_STEP Step;
    ZP_SQLITE_FINALIZE Finalize;
    ZP_SQLITE_BIND_INT BindInt;
    ZP_SQLITE_BIND_INT64 BindInt64;
    ZP_SQLITE_DESERIALIZE Deserialize;
    ZP_SQLITE_COLUMN_INT ColumnInt;
    ZP_SQLITE_COLUMN_INT64 ColumnInt64;
    ZP_SQLITE_COLUMN_TEXT16 ColumnText16;
    ZP_SQLITE_COLUMN_BYTES16 ColumnBytes16;
} ZP_SQLITE, *PZP_SQLITE;

typedef struct _ZP_BROWSER_BUILDER
{
    PZP_BROWSER_RECORD Records;
    ULONG Count;
    ULONG Capacity;
} ZP_BROWSER_BUILDER, *PZP_BROWSER_BUILDER;

static const WCHAR ZpBrowserChromeName[] = L"Google Chrome";
static const WCHAR ZpBrowserEdgeName[] = L"Microsoft Edge";

static
NTSTATUS
ZpBrowser_AddRecord(
    _Inout_ PZP_BROWSER_BUILDER Builder,
    _In_ ZP_BROWSER_KIND Kind,
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ ULONG State,
    _In_ ULONG Flags,
    _In_ ULONGLONG Id,
    _In_ ULONGLONG Time,
    _In_ ULONGLONG Value,
    _In_ PCWSTR Identity,
    _In_opt_ PCWSTR Name,
    _In_opt_ PCWSTR Location,
    _In_opt_ PCWSTR Detail)
{
    ULONG IdentityLength = (ULONG)wcslen(Identity);
    ULONG NameLength = Name == NULL ? 0 : (ULONG)wcslen(Name);
    ULONG LocationLength = Location == NULL ? 0 : (ULONG)wcslen(Location);
    ULONG DetailLength = Detail == NULL ? 0 : (ULONG)wcslen(Detail);
    SIZE_T CharacterCount = (SIZE_T)IdentityLength + NameLength + LocationLength + DetailLength + 4;
    PZP_BROWSER_RECORD Records, Record;
    PWCHAR Strings, Cursor;

    if (Builder->Count == ZP_CODEC_MAX_ELEMENT_COUNT || CharacterCount > MAXULONG)
    {
        return STATUS_QUOTA_EXCEEDED;
    }
    if (Builder->Count == Builder->Capacity)
    {
        ULONG Capacity = Builder->Capacity == 0 ? 16 : min(Builder->Capacity * 2, ZP_CODEC_MAX_ELEMENT_COUNT);

        Records = Mem_ReAlloc(Builder->Records, (SIZE_T)Capacity * sizeof(*Records));
        if (Records == NULL) return STATUS_NO_MEMORY;
        Builder->Records = Records;
        Builder->Capacity = Capacity;
    }
    Strings = Mem_Alloc(CharacterCount * sizeof(WCHAR));
    if (Strings == NULL) return STATUS_NO_MEMORY;
    Record = &Builder->Records[Builder->Count++];
    Record->Kind = Kind;
    Record->Browser = Browser;
    Record->State = State;
    Record->Flags = Flags;
    Record->Id = Id;
    Record->Time = Time;
    Record->Value = Value;
    Cursor = Strings;
#define ZP_BROWSER_COPY_STRING(Field, Source, Count) \
    Record->Field = Cursor; \
    Record->Field##Length = Count; \
    if (Count != 0) RtlCopyMemory(Cursor, Source, (SIZE_T)Count * sizeof(WCHAR)); \
    Cursor[Count] = UNICODE_NULL; \
    Cursor += (SIZE_T)Count + 1
    ZP_BROWSER_COPY_STRING(Identity, Identity, IdentityLength);
    ZP_BROWSER_COPY_STRING(Name, Name, NameLength);
    ZP_BROWSER_COPY_STRING(Location, Location, LocationLength);
    ZP_BROWSER_COPY_STRING(Detail, Detail, DetailLength);
#undef ZP_BROWSER_COPY_STRING
    return STATUS_SUCCESS;
}

static
VOID
ZpBrowser_FreeBuilder(
    _Inout_ PZP_BROWSER_BUILDER Builder)
{
    ULONG Index;

    for (Index = 0; Index < Builder->Count; Index++)
    {
        Mem_Free((PVOID)Builder->Records[Index].Identity);
    }
    Mem_Free(Builder->Records);
}

static
NTSTATUS
ZpBrowser_EncodeBuilder(
    _In_ PZP_BROWSER_BUILDER Builder,
    _In_ ULONGLONG NextCursor,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    NTSTATUS Status;

    Status = ZpBrowser_EncodePage(
        Builder->Records,
        Builder->Count,
        NextCursor,
        NULL,
        0,
        ResponseLength);
    *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
    if (!NT_SUCCESS(Status) || *Response == NULL)
    {
        return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    }
    Status = ZpBrowser_EncodePage(
        Builder->Records,
        Builder->Count,
        NextCursor,
        *Response,
        *ResponseLength,
        ResponseLength);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(*Response);
        *Response = NULL;
    }
    return Status;
}

static
LOGICAL
ZpBrowser_FileExists(
    _In_ PCWSTR Path)
{
    ULONG Attributes = GetFileAttributesW(Path);

    return Attributes != INVALID_FILE_ATTRIBUTES && !(Attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static
NTSTATUS
ZpBrowser_GetEnvironment(
    _In_ PCWSTR Name,
    _Out_writes_(Capacity) PWSTR Value,
    _In_ ULONG Capacity)
{
    UNICODE_STRING NameString, ValueString;

    RtlInitUnicodeString(&NameString, Name);
    RtlInitEmptyUnicodeString(&ValueString, Value, (USHORT)min(Capacity * sizeof(WCHAR), MAXUSHORT));
    return RtlQueryEnvironmentVariable_U(NULL, &NameString, &ValueString);
}

static
NTSTATUS
ZpBrowser_GetPaths(
    _In_ ZP_BROWSER_TYPE Browser,
    _Out_writes_(MAX_PATH) PWSTR Executable,
    _Out_writes_(MAX_PATH) PWSTR UserData)
{
    WCHAR LocalAppData[MAX_PATH], ProgramFiles[MAX_PATH], ProgramFilesX86[MAX_PATH];
    PCWSTR Name;
    NTSTATUS Status, ProgramFilesStatus, ProgramFilesX86Status;

    Status = ZpBrowser_GetEnvironment(L"LOCALAPPDATA", LocalAppData, ARRAYSIZE(LocalAppData));
    if (!NT_SUCCESS(Status)) return Status;
    ProgramFilesStatus = ZpBrowser_GetEnvironment(L"ProgramFiles", ProgramFiles, ARRAYSIZE(ProgramFiles));
    ProgramFilesX86Status = ZpBrowser_GetEnvironment(
        L"ProgramFiles(x86)",
        ProgramFilesX86,
        ARRAYSIZE(ProgramFilesX86));
    Name = Browser == ZpBrowserChrome ? L"Google\\Chrome" : L"Microsoft\\Edge";
    Status = StringCchPrintfW(UserData, MAX_PATH, L"%s\\%s\\User Data", LocalAppData, Name);
    if (!NT_SUCCESS(Status)) return Status;
    Status = StringCchPrintfW(Executable,
                              MAX_PATH,
                              L"%s\\%s\\Application\\%s",
                              LocalAppData,
                              Name,
                              Browser == ZpBrowserChrome ? L"chrome.exe" : L"msedge.exe");
    if (SUCCEEDED(Status) && ZpBrowser_FileExists(Executable)) return STATUS_SUCCESS;
    if (NT_SUCCESS(ProgramFilesStatus))
    {
        Status = StringCchPrintfW(Executable,
                                  MAX_PATH,
                                  L"%s\\%s\\Application\\%s",
                                  ProgramFiles,
                                  Name,
                                  Browser == ZpBrowserChrome ? L"chrome.exe" : L"msedge.exe");
        if (SUCCEEDED(Status) && ZpBrowser_FileExists(Executable)) return STATUS_SUCCESS;
    }
    if (NT_SUCCESS(ProgramFilesX86Status))
    {
        Status = StringCchPrintfW(Executable,
                                  MAX_PATH,
                                  L"%s\\%s\\Application\\%s",
                                  ProgramFilesX86,
                                  Name,
                                  Browser == ZpBrowserChrome ? L"chrome.exe" : L"msedge.exe");
        if (SUCCEEDED(Status) && ZpBrowser_FileExists(Executable)) return STATUS_SUCCESS;
    }
    return STATUS_NOT_FOUND;
}

static
NTSTATUS
ZpBrowser_GetVersion(
    _In_ PCWSTR Path,
    _Out_writes_(VersionCapacity) PWSTR Version,
    _In_ ULONG VersionCapacity)
{
    VS_FIXEDFILEINFO* Info;
    PBYTE Buffer;
    UINT InfoLength;
    DWORD Handle, Size = GetFileVersionInfoSizeW(Path, &Handle);
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    if (Size == 0) return NTSTATUS_FROM_WIN32(GetLastError());
    Buffer = Mem_Alloc(Size);
    if (Buffer == NULL) return STATUS_NO_MEMORY;
    if (GetFileVersionInfoW(Path, 0, Size, Buffer) &&
        VerQueryValueW(Buffer, L"\\", (PVOID*)&Info, &InfoLength) &&
        InfoLength >= sizeof(*Info))
    {
        Status = StringCchPrintfW(Version,
                                  VersionCapacity,
                                  L"%hu.%hu.%hu.%hu",
                                  HIWORD(Info->dwFileVersionMS),
                                  LOWORD(Info->dwFileVersionMS),
                                  HIWORD(Info->dwFileVersionLS),
                                  LOWORD(Info->dwFileVersionLS));
    }
    Mem_Free(Buffer);
    return Status;
}

static
ZP_STATUS
ZpBrowser_EnumerateProfiles(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ PCWSTR UserData,
    _Inout_ PZP_BROWSER_BUILDER Builder)
{
    WIN32_FIND_DATAW Data;
    WCHAR Pattern[MAX_PATH], Preferences[MAX_PATH], Location[MAX_PATH];
    HANDLE Find;
    NTSTATUS Status;

    Status = StringCchPrintfW(Pattern, ARRAYSIZE(Pattern), L"%s\\*", UserData);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Find = FindFirstFileExW(Pattern, FindExInfoBasic, &Data, FindExSearchLimitToDirectories, NULL, 0);
    if (Find == INVALID_HANDLE_VALUE)
    {
        ULONG Error = GetLastError();

        return Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND ?
                   ZpStatus_Make(ZpStatusNone, 0) :
                   ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    Status = STATUS_SUCCESS;
    do
    {
        if (!(Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (Data.cFileName[0] == L'.' &&
             (Data.cFileName[1] == UNICODE_NULL ||
              (Data.cFileName[1] == L'.' && Data.cFileName[2] == UNICODE_NULL))))
        {
            continue;
        }
        if (FAILED(StringCchPrintfW(Preferences,
                                     ARRAYSIZE(Preferences),
                                     L"%s\\%s\\Preferences",
                                     UserData,
                                     Data.cFileName)) ||
            !ZpBrowser_FileExists(Preferences) ||
            FAILED(StringCchPrintfW(Location,
                                     ARRAYSIZE(Location),
                                     L"%s\\%s",
                                     UserData,
                                     Data.cFileName)))
        {
            continue;
        }
        Status = ZpBrowser_AddRecord(Builder,
                                     ZpBrowserKindProfile,
                                     Browser,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0,
                                     Data.cFileName,
                                     Data.cFileName,
                                     Location,
                                     NULL);
    } while (NT_SUCCESS(Status) && FindNextFileW(Find, &Data));
    FindClose(Find);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpBrowser_Enumerate(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_BROWSER_BUILDER Builder = { 0 };
    WCHAR Executable[MAX_PATH], UserData[MAX_PATH], Version[64];
    ZP_BROWSER_TYPE Browser;
    NTSTATUS NtStatus = STATUS_SUCCESS;
    ZP_STATUS Status = ZpStatus_Make(ZpStatusNone, 0);

    for (Browser = ZpBrowserChrome; ZpStatus_IsSuccess(Status) && Browser <= ZpBrowserEdge; Browser++)
    {
        NtStatus = ZpBrowser_GetPaths(Browser, Executable, UserData);
        if (NtStatus == STATUS_NOT_FOUND)
        {
            continue;
        }
        if (!NT_SUCCESS(NtStatus))
        {
            Status = ZpStatus_FromNtStatus(NtStatus);
            break;
        }
        if (!NT_SUCCESS(ZpBrowser_GetVersion(Executable, Version, ARRAYSIZE(Version)))) Version[0] = UNICODE_NULL;
        NtStatus = ZpBrowser_AddRecord(&Builder,
                                       ZpBrowserKindBrowser,
                                       Browser,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       Browser == ZpBrowserChrome ? L"chrome" : L"edge",
                                       Browser == ZpBrowserChrome ? ZpBrowserChromeName : ZpBrowserEdgeName,
                                       Executable,
                                       Version);
        Status = ZpStatus_FromNtStatus(NtStatus);
        if (ZpStatus_IsSuccess(Status)) Status = ZpBrowser_EnumerateProfiles(Browser, UserData, &Builder);
    }
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpBrowser_EncodeBuilder(&Builder, 0, Response, ResponseLength));
    }
    ZpBrowser_FreeBuilder(&Builder);
    return Status;
}

static
LOGICAL
ZpBrowser_IsProfileValid(
    _In_ PCZP_STRING_VIEW Profile)
{
    ULONG Index;

    if (Profile->Length == 0 || Profile->Length >= MAX_PATH ||
        (Profile->Length == 1 && Profile->Buffer[0] == L'.') ||
        (Profile->Length == 2 && Profile->Buffer[0] == L'.' && Profile->Buffer[1] == L'.'))
    {
        return FALSE;
    }
    for (Index = 0; Index < Profile->Length; Index++)
    {
        if (Profile->Buffer[Index] == L'\\' || Profile->Buffer[Index] == L'/' ||
            Profile->Buffer[Index] == L':')
        {
            return FALSE;
        }
    }
    return TRUE;
}

static
NTSTATUS
ZpBrowser_CopyProfile(
    _In_ PCZP_STRING_VIEW Profile,
    _Out_writes_(MAX_PATH) PWSTR Value)
{
    if (!ZpBrowser_IsProfileValid(Profile)) return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(Value, Profile->Buffer, (SIZE_T)Profile->Length * sizeof(WCHAR));
    Value[Profile->Length] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpBrowser_LoadSqlite(
    _Out_ PZP_SQLITE Api)
{
#define ZP_BROWSER_LOAD_SQLITE(Field, Name) \
    Api->Field = (PVOID)GetProcAddress(Api->Module, Name); \
    if (Api->Field == NULL) goto MissingExport
    Api->Module = LoadLibraryExW(L"winsqlite3.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (Api->Module == NULL) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    ZP_BROWSER_LOAD_SQLITE(OpenV2, "sqlite3_open_v2");
    ZP_BROWSER_LOAD_SQLITE(Close, "sqlite3_close");
    ZP_BROWSER_LOAD_SQLITE(PrepareV2, "sqlite3_prepare_v2");
    ZP_BROWSER_LOAD_SQLITE(Step, "sqlite3_step");
    ZP_BROWSER_LOAD_SQLITE(Finalize, "sqlite3_finalize");
    ZP_BROWSER_LOAD_SQLITE(BindInt, "sqlite3_bind_int");
    ZP_BROWSER_LOAD_SQLITE(BindInt64, "sqlite3_bind_int64");
    Api->Deserialize = (PVOID)GetProcAddress(Api->Module, "sqlite3_deserialize");
    ZP_BROWSER_LOAD_SQLITE(ColumnInt, "sqlite3_column_int");
    ZP_BROWSER_LOAD_SQLITE(ColumnInt64, "sqlite3_column_int64");
    ZP_BROWSER_LOAD_SQLITE(ColumnText16, "sqlite3_column_text16");
    ZP_BROWSER_LOAD_SQLITE(ColumnBytes16, "sqlite3_column_bytes16");
#undef ZP_BROWSER_LOAD_SQLITE
    return ZpStatus_Make(ZpStatusNone, 0);

MissingExport:
    FreeLibrary(Api->Module);
    return ZpStatus_FromCode(ZpStatusWin32, ERROR_PROC_NOT_FOUND);
}

static
PSTR
ZpBrowser_PathToUri(
    _In_ PCWSTR Path)
{
    static const CHAR Prefix[] = "file:";
    static const CHAR Suffix[] = "?mode=ro&nolock=1";
    static const CHAR Hex[] = "0123456789ABCDEF";
    CHAR Utf8[MAX_PATH * 3];
    ULONG Bytes, Index;
    PSTR Value, Output;
    NTSTATUS Status;

    Status = RtlUnicodeToUTF8N(Utf8,
                               sizeof(Utf8),
                               &Bytes,
                               Path,
                               (ULONG)wcslen(Path) * sizeof(WCHAR));
    if (!NT_SUCCESS(Status)) return NULL;
    Value = Mem_Alloc(sizeof(Prefix) - 1 + (SIZE_T)Bytes * 3 + sizeof(Suffix));
    if (Value == NULL) return NULL;
    Output = Value;
    RtlCopyMemory(Output, Prefix, sizeof(Prefix) - 1);
    Output += sizeof(Prefix) - 1;
    for (Index = 0; Index < Bytes; Index++)
    {
        if (Utf8[Index] == '%' || Utf8[Index] == '?' || Utf8[Index] == '#')
        {
            *Output++ = '%';
            *Output++ = Hex[(UCHAR)Utf8[Index] >> 4];
            *Output++ = Hex[(UCHAR)Utf8[Index] & 0xF];
        }
        else
        {
            *Output++ = Utf8[Index] == '\\' ? '/' : Utf8[Index];
        }
    }
    RtlCopyMemory(Output, Suffix, sizeof(Suffix));
    return Value;
}

static
NTSTATUS
ZpBrowser_QueryProcessHandles(
    _In_ HANDLE Process,
    _Outptr_ PPROCESS_HANDLE_SNAPSHOT_INFORMATION* Handles)
{
    ULONG Length = 64 * 1024, Required;
    NTSTATUS Status;

    for (;;)
    {
        *Handles = Mem_Alloc(Length);
        if (*Handles == NULL) return STATUS_NO_MEMORY;
        Status = NtQueryInformationProcess(Process, ProcessHandleInformation, *Handles, Length, &Required);
        if (Status != STATUS_INFO_LENGTH_MISMATCH) break;
        Mem_Free(*Handles);
        Length = max(Length * 2, Required);
        if (Length > 16 * 1024 * 1024) return STATUS_QUOTA_EXCEEDED;
    }
    if (!NT_SUCCESS(Status)) Mem_Free(*Handles);
    return Status;
}

static
NTSTATUS
ZpBrowser_QueryDatabaseId(
    _In_ PCWSTR DatabasePath,
    _Out_ PFILE_ID_INFORMATION Id)
{
    DECLSPEC_ALIGN(8) BYTE Buffer[PAGE_SIZE];
    PFILE_ID_EXTD_DIR_INFORMATION Information = (PVOID)Buffer;
    WCHAR DirectoryPath[MAX_PATH];
    OBJECT_ATTRIBUTES Object;
    UNICODE_STRING NtPath, Search;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Directory;
    PWSTR Name;
    BOOL HasData;
    NTSTATUS Status;

    RtlCopyMemory(DirectoryPath, DatabasePath, (wcslen(DatabasePath) + 1) * sizeof(WCHAR));
    Name = wcsrchr(DirectoryPath, L'\\');
    if (Name == NULL || Name[1] == UNICODE_NULL) return STATUS_INVALID_PARAMETER;
    RtlInitUnicodeString(&Search, Name + 1);
    *Name = UNICODE_NULL;
    Status = NT_InitWin32PathObject(&Object, DirectoryPath, NULL, &NtPath);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtOpenFile(&Directory,
                        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                        &Object,
                        &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    NT_FreeNtPath(&NtPath);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryInformationFile(Directory, &IoStatus, Id, sizeof(*Id), FileIdInformation);
    if (NT_SUCCESS(Status))
    {
        Status = IO_FindFile(Directory,
                             Buffer,
                             sizeof(Buffer),
                             FileIdExtdDirectoryInformation,
                             &Search,
                             TRUE,
                             &HasData);
    }
    NtClose(Directory);
    if (NT_SUCCESS(Status) && !HasData) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (NT_SUCCESS(Status)) Id->FileId = Information->FileId;
    return Status;
}

static
NTSTATUS
ZpBrowser_MapDatabaseHandle(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ PCWSTR DatabasePath,
    _Out_ PIO_FILE_MAP Map)
{
    PSYSTEM_PROCESS_INFORMATION Processes, Process;
    PPROCESS_HANDLE_SNAPSHOT_INFORMATION Handles;
    FILE_ID_INFORMATION Id, CandidateId;
    UNICODE_STRING ImageName;
    IO_STATUS_BLOCK IoStatus;
    HANDLE BrowserProcess, Candidate;
    ULONG_PTR Index;
    NTSTATUS Status;

    Status = ZpBrowser_QueryDatabaseId(DatabasePath, &Id);
    if (!NT_SUCCESS(Status)) return Status;
    RtlInitUnicodeString(&ImageName, Browser == ZpBrowserChrome ? L"chrome.exe" : L"msedge.exe");
    Status = Sys_QueryDynamicInfo(SystemProcessInformation, (PVOID*)&Processes);
    if (!NT_SUCCESS(Status)) return Status;
    Status = STATUS_OBJECT_NAME_NOT_FOUND;
    Process = Processes;
    for (;;)
    {
        if (RtlEqualUnicodeString(&Process->ImageName, &ImageName, TRUE) &&
            NT_SUCCESS(PS_OpenProcess(&BrowserProcess,
                                      PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION,
                                      (ULONG)(ULONG_PTR)Process->UniqueProcessId)))
        {
            if (NT_SUCCESS(ZpBrowser_QueryProcessHandles(BrowserProcess, &Handles)))
            {
                for (Index = 0; Index < Handles->NumberOfHandles; Index++)
                {
                    if (!NT_SUCCESS(NtDuplicateObject(BrowserProcess,
                                                      Handles->Handles[Index].HandleValue,
                                                      NtCurrentProcess(),
                                                      &Candidate,
                                                      FILE_READ_DATA | FILE_READ_ATTRIBUTES,
                                                      0,
                                                      0)))
                    {
                        continue;
                    }
                    if (GetFileType(Candidate) == FILE_TYPE_DISK &&
                        NT_SUCCESS(NtQueryInformationFile(Candidate,
                                                          &IoStatus,
                                                          &CandidateId,
                                                          sizeof(CandidateId),
                                                          FileIdInformation)) &&
                        RtlEqualMemory(&CandidateId, &Id, sizeof(Id)))
                    {
                        Status = IO_MapReadOnlyFile(Candidate, Map);
                        NtClose(Candidate);
                        break;
                    }
                    NtClose(Candidate);
                }
                Mem_Free(Handles);
            }
            NtClose(BrowserProcess);
            if (NT_SUCCESS(Status)) break;
        }
        if (Process->NextEntryOffset == 0) break;
        Process = Add2Ptr(Process, Process->NextEntryOffset);
    }
    Sys_FreeInfo(Processes);
    if (NT_SUCCESS(Status) && (ULONGLONG)Map->FileSize > (ULONGLONG)MAXLONGLONG)
    {
        IO_UnmapFile(Map);
        Status = STATUS_FILE_TOO_LARGE;
    }
    return Status;
}

static
PCWSTR
ZpBrowser_ColumnText(
    _In_ PZP_SQLITE Api,
    _In_ sqlite3_stmt* Statement,
    _In_ ULONG Column)
{
    PCWSTR Value = Api->ColumnText16(Statement, Column);

    return Value != NULL && Api->ColumnBytes16(Statement, Column) != 0 ? Value : L"";
}

static
ULONGLONG
ZpBrowser_ChromiumTime(
    _In_ sqlite3_int64 Value)
{
    return Value > 0 && (ULONGLONG)Value <= MAXULONGLONG / 10 ? (ULONGLONG)Value * 10 : 0;
}

static
NTSTATUS
ZpBrowser_AddSqliteRecord(
    _Inout_ PZP_BROWSER_BUILDER Builder,
    _In_ PZP_SQLITE Api,
    _In_ sqlite3_stmt* Statement,
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ ZP_BROWSER_KIND Kind)
{
    WCHAR Detail[256];
    ULONGLONG Id = (ULONGLONG)Api->ColumnInt64(Statement, 0);
    PCWSTR Identity, Name, Location;
    ULONGLONG Time, Value;
    ULONG State, Flags;

    if (Kind == ZpBrowserKindHistory)
    {
        Identity = ZpBrowser_ColumnText(Api, Statement, 1);
        Name = ZpBrowser_ColumnText(Api, Statement, 2);
        Location = Identity;
        Time = ZpBrowser_ChromiumTime(Api->ColumnInt64(Statement, 3));
        Value = (ULONGLONG)Api->ColumnInt64(Statement, 4);
        State = (ULONG)Api->ColumnInt(Statement, 5);
        Flags = 0;
        StringCchPrintfW(Detail, ARRAYSIZE(Detail), L"访问 %llu 次，输入 %lu 次", Value, State);
    }
    else if (Kind == ZpBrowserKindDownload)
    {
        Identity = ZpBrowser_ColumnText(Api, Statement, 1);
        Name = ZpBrowser_ColumnText(Api, Statement, 2);
        Location = Identity;
        Time = ZpBrowser_ChromiumTime(Api->ColumnInt64(Statement, 3));
        Value = (ULONGLONG)Api->ColumnInt64(Statement, 5);
        State = (ULONG)Api->ColumnInt(Statement, 7);
        Flags = (ULONG)Api->ColumnInt(Statement, 8);
        StringCchPrintfW(Detail,
                         ARRAYSIZE(Detail),
                         L"已接收 %llu / %llu 字节；结束时间 %llu",
                         Value,
                         (ULONGLONG)Api->ColumnInt64(Statement, 6),
                         ZpBrowser_ChromiumTime(Api->ColumnInt64(Statement, 4)));
    }
    else
    {
        Identity = ZpBrowser_ColumnText(Api, Statement, 1);
        Name = ZpBrowser_ColumnText(Api, Statement, 2);
        Location = ZpBrowser_ColumnText(Api, Statement, 3);
        Time = ZpBrowser_ChromiumTime(Api->ColumnInt64(Statement, 6));
        Value = ZpBrowser_ChromiumTime(Api->ColumnInt64(Statement, 5));
        State = 0;
        Flags = (Api->ColumnInt(Statement, 7) ? 1UL : 0UL) |
                (Api->ColumnInt(Statement, 8) ? 2UL : 0UL);
        StringCchPrintfW(Detail,
                         ARRAYSIZE(Detail),
                         L"创建时间 %llu；SameSite %d",
                         ZpBrowser_ChromiumTime(Api->ColumnInt64(Statement, 4)),
                         Api->ColumnInt(Statement, 9));
    }
    return ZpBrowser_AddRecord(
        Builder,
        Kind,
        Browser,
        State,
        Flags,
        Id,
        Time,
        Value,
        Identity,
        Name,
        Location,
        Detail);
}

static
ZP_STATUS
ZpBrowser_QueryDatabase(
    _In_ PCZP_BROWSER_QUERY_VIEW Query,
    _In_ PCWSTR DatabasePath,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    static const char HistorySql[] =
        "SELECT id,url,title,last_visit_time,visit_count,typed_count FROM urls "
        "WHERE (?1=0 OR id<?1) ORDER BY id DESC LIMIT ?2";
    static const char DownloadSql[] =
        "SELECT d.id,COALESCE((SELECT url FROM downloads_url_chains u WHERE u.id=d.id ORDER BY chain_index "
        "LIMIT 1),''),d.target_path,d.start_time,d.end_time,d.received_bytes,d.total_bytes,d.state,"
        "d.interrupt_reason FROM downloads d WHERE (?1=0 OR d.id<?1) ORDER BY d.id DESC LIMIT ?2";
    static const char CookieSql[] =
        "SELECT rowid,host_key,name,path,creation_utc,expires_utc,last_access_utc,is_secure,is_httponly,samesite "
        "FROM cookies WHERE (?1=0 OR rowid<?1) ORDER BY rowid DESC LIMIT ?2";
    ZP_BROWSER_BUILDER Builder = { 0 };
    ZP_SQLITE Api;
    sqlite3* Database = NULL;
    sqlite3_stmt* Statement = NULL;
    IO_FILE_MAP Map;
    PCSTR Sql = Query->Kind == ZpBrowserKindHistory ? HistorySql :
                  Query->Kind == ZpBrowserKindDownload ? DownloadSql : CookieSql;
    PSTR Utf8Path;
    ULONGLONG NextCursor = 0;
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS MapStatus = STATUS_NOT_FOUND;
    ZP_STATUS LoadStatus;
    int Result;

    LoadStatus = ZpBrowser_LoadSqlite(&Api);
    if (!ZpStatus_IsSuccess(LoadStatus)) return LoadStatus;
    Utf8Path = ZpBrowser_PathToUri(DatabasePath);
    if (Utf8Path == NULL)
    {
        FreeLibrary(Api.Module);
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    Result = Api.OpenV2(Utf8Path,
                        &Database,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_URI,
                        NULL);
    Mem_Free(Utf8Path);
    if (Result == SQLITE_CANTOPEN && Query->Kind == ZpBrowserKindCookie && Api.Deserialize != NULL)
    {
        if (Database != NULL)
        {
            Api.Close(Database);
            Database = NULL;
        }
        MapStatus = ZpBrowser_MapDatabaseHandle(Query->Browser, DatabasePath, &Map);
        if (NT_SUCCESS(MapStatus))
        {
            Result = Api.OpenV2(":memory:",
                                &Database,
                                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                                NULL);
            if (Result == SQLITE_OK)
            {
                Result = Api.Deserialize(Database,
                                         "main",
                                         Map.BaseAddress,
                                         (sqlite3_int64)Map.FileSize,
                                         (sqlite3_int64)Map.FileSize,
                                         SQLITE_DESERIALIZE_READONLY);
            }
        }
    }
    if (Result == SQLITE_OK) Result = Api.PrepareV2(Database, Sql, -1, &Statement, NULL);
    if (Result == SQLITE_OK) Result = Api.BindInt64(Statement, 1, (sqlite3_int64)Query->Cursor);
    if (Result == SQLITE_OK) Result = Api.BindInt(Statement, 2, (int)Query->Limit);
    if (Result == SQLITE_OK)
    {
        while ((Result = Api.Step(Statement)) == SQLITE_ROW)
        {
            Status = ZpBrowser_AddSqliteRecord(&Builder, &Api, Statement, Query->Browser, Query->Kind);
            if (!NT_SUCCESS(Status)) break;
            NextCursor = Builder.Records[Builder.Count - 1].Id;
        }
    }
    if (Result == SQLITE_DONE) Result = SQLITE_OK;
    if (Statement != NULL)
    {
        int FinalizeResult = Api.Finalize(Statement);

        if (Result == SQLITE_OK && FinalizeResult != SQLITE_OK) Result = FinalizeResult;
    }
    if (Database != NULL)
    {
        int CloseResult = Api.Close(Database);

        if (Result == SQLITE_OK && CloseResult != SQLITE_OK) Result = CloseResult;
    }
    if (NT_SUCCESS(MapStatus)) IO_UnmapFile(&Map);
    FreeLibrary(Api.Module);
    if (!NT_SUCCESS(Status)) Result = SQLITE_OK;
    if (NT_SUCCESS(Status) && Result == SQLITE_OK)
    {
        if (Builder.Count < Query->Limit) NextCursor = 0;
        Status = ZpBrowser_EncodeBuilder(&Builder, NextCursor, Response, ResponseLength);
    }
    ZpBrowser_FreeBuilder(&Builder);
    return !NT_SUCCESS(Status) ? ZpStatus_FromNtStatus(Status) : ZpStatus_FromCode(ZpStatusSqlite, Result);
}

static
NTSTATUS
ZpBrowser_ReadDocument(
    _In_ PCWSTR Path,
    _Outptr_result_z_ PWSTR* Text)
{
    FILE_STANDARD_INFORMATION Information;
    UNICODE_STRING NtPath;
    IO_STATUS_BLOCK IoStatus;
    OBJECT_ATTRIBUTES Object;
    LARGE_INTEGER Offset = { 0 };
    PBYTE Bytes;
    PWSTR Value;
    HANDLE File;
    ULONG UnicodeBytes;
    NTSTATUS Status;

    Status = RtlDosPathNameToNtPathName_U_WithStatus(Path, &NtPath, NULL, NULL);
    if (!NT_SUCCESS(Status)) return Status;
    InitializeObjectAttributes(&Object, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&File,
                        FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                        &Object,
                        &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    RtlFreeUnicodeString(&NtPath);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryInformationFile(File,
                                    &IoStatus,
                                    &Information,
                                    sizeof(Information),
                                    FileStandardInformation);
    if (NT_SUCCESS(Status) && Information.EndOfFile.QuadPart > ZP_BROWSER_DOCUMENT_MAX_SIZE)
    {
        Status = STATUS_FILE_TOO_LARGE;
    }
    Bytes = NT_SUCCESS(Status) ? Mem_Alloc((SIZE_T)Information.EndOfFile.QuadPart) : NULL;
    if (NT_SUCCESS(Status) && Bytes == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = IO_ReadFile(File,
                             &Offset,
                             Bytes,
                             (ULONG)Information.EndOfFile.QuadPart,
                             NULL);
    }
    NtClose(File);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Bytes);
        return Status;
    }
    Status = RtlUTF8ToUnicodeN(NULL,
                               0,
                               &UnicodeBytes,
                               (PCCH)Bytes,
                               (ULONG)Information.EndOfFile.QuadPart);
    if (Status != STATUS_BUFFER_TOO_SMALL && !NT_SUCCESS(Status))
    {
        Mem_Free(Bytes);
        return Status;
    }
    Value = Mem_Alloc((SIZE_T)UnicodeBytes + sizeof(WCHAR));
    if (Value == NULL)
    {
        Mem_Free(Bytes);
        return STATUS_NO_MEMORY;
    }
    Status = RtlUTF8ToUnicodeN(Value,
                               UnicodeBytes,
                               &UnicodeBytes,
                               (PCCH)Bytes,
                               (ULONG)Information.EndOfFile.QuadPart);
    Mem_Free(Bytes);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Value);
        return Status;
    }
    Value[UnicodeBytes / sizeof(WCHAR)] = UNICODE_NULL;
    *Text = Value;
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpBrowser_QueryDocument(
    _In_ PCZP_BROWSER_QUERY_VIEW Query,
    _In_ PCWSTR Path,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_BROWSER_BUILDER Builder = { 0 };
    PWSTR Text;
    NTSTATUS Status;

    Status = ZpBrowser_ReadDocument(Path, &Text);
    if (NT_SUCCESS(Status))
    {
        Status = ZpBrowser_AddRecord(&Builder,
                                     Query->Kind,
                                     Query->Browser,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0,
                                     Query->Kind == ZpBrowserKindBookmark ? L"Bookmarks" : L"Preferences",
                                     Query->Kind == ZpBrowserKindBookmark ? L"书签" : L"浏览器设置",
                                     Path,
                                     Text);
        Mem_Free(Text);
    }
    if (NT_SUCCESS(Status)) Status = ZpBrowser_EncodeBuilder(&Builder, 0, Response, ResponseLength);
    ZpBrowser_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpBrowser_QueryExtensions(
    _In_ PCZP_BROWSER_QUERY_VIEW Query,
    _In_ PCWSTR Path,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_BROWSER_BUILDER Builder = { 0 };
    WIN32_FIND_DATAW Data;
    WCHAR Pattern[MAX_PATH], Location[MAX_PATH];
    HANDLE Find;
    NTSTATUS Status;

    Status = StringCchPrintfW(Pattern, ARRAYSIZE(Pattern), L"%s\\*", Path);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Find = FindFirstFileExW(Pattern, FindExInfoBasic, &Data, FindExSearchLimitToDirectories, NULL, 0);
    if (Find == INVALID_HANDLE_VALUE)
    {
        ULONG Error = GetLastError();

        return Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND ?
                   ZpStatus_FromNtStatus(
                       ZpBrowser_EncodeBuilder(&Builder, 0, Response, ResponseLength)) :
                   ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    do
    {
        if (!(Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || Data.cFileName[0] == L'.') continue;
        if (FAILED(StringCchPrintfW(Location,
                                     ARRAYSIZE(Location),
                                     L"%s\\%s",
                                     Path,
                                     Data.cFileName)))
        {
            continue;
        }
        Status = ZpBrowser_AddRecord(&Builder,
                                     ZpBrowserKindExtension,
                                     Query->Browser,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0,
                                     Data.cFileName,
                                     Data.cFileName,
                                     Location,
                                     NULL);
    } while (NT_SUCCESS(Status) && FindNextFileW(Find, &Data));
    FindClose(Find);
    if (NT_SUCCESS(Status)) Status = ZpBrowser_EncodeBuilder(&Builder, 0, Response, ResponseLength);
    ZpBrowser_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpBrowser_Query(
    _In_ PCZP_BROWSER_QUERY_VIEW Query,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    WCHAR Executable[MAX_PATH], UserData[MAX_PATH], Profile[MAX_PATH], Path[MAX_PATH];
    NTSTATUS Status;

    Status = ZpBrowser_CopyProfile(&Query->Profile, Profile);
    if (NT_SUCCESS(Status)) Status = ZpBrowser_GetPaths(Query->Browser, Executable, UserData);
    if (NT_SUCCESS(Status))
    {
        Status = StringCchPrintfW(Path,
                                  ARRAYSIZE(Path),
                                  Query->Kind == ZpBrowserKindCookie ? L"%s\\%s\\Network\\Cookies" :
                                  Query->Kind == ZpBrowserKindBookmark ? L"%s\\%s\\Bookmarks" :
                                  Query->Kind == ZpBrowserKindSetting ? L"%s\\%s\\Preferences" :
                                  Query->Kind == ZpBrowserKindExtension ? L"%s\\%s\\Extensions" :
                                                                         L"%s\\%s\\History",
                                  UserData,
                                  Profile);
    }
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    if (Query->Kind == ZpBrowserKindHistory || Query->Kind == ZpBrowserKindDownload ||
        Query->Kind == ZpBrowserKindCookie)
    {
        return ZpBrowser_QueryDatabase(Query, Path, Response, ResponseLength);
    }
    if (Query->Kind == ZpBrowserKindBookmark || Query->Kind == ZpBrowserKindSetting)
    {
        return ZpBrowser_QueryDocument(Query, Path, Response, ResponseLength);
    }
    return ZpBrowser_QueryExtensions(Query, Path, Response, ResponseLength);
}

ZP_STATUS
ZpBrowser_Execute(
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_BROWSER_QUERY_VIEW Query;
    NTSTATUS Status;

    if (OperationId == ZP_BROWSER_OPERATION_ENUMERATE)
    {
        return RequestLength == 0 ?
                   ZpBrowser_Enumerate(Response, ResponseLength) :
                   ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    if (OperationId != ZP_BROWSER_OPERATION_QUERY)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Status = ZpBrowser_DecodeQuery(Request, RequestLength, &Query);
    return NT_SUCCESS(Status) ?
               ZpBrowser_Query(&Query, Response, ResponseLength) :
               ZpStatus_FromNtStatus(Status);
}
