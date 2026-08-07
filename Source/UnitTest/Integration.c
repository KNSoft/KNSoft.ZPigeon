#include "UnitTest.h"

#include <KNSoft/ZPigeon/Client.h>
#include <KNSoft/ZPigeon/Server.h>

#include "../KNSoft.ZPigeon.Client.SDK/Client.inl"

#include <Bcrypt.h>
#include <Ncrypt.h>
#include <Ws2tcpip.h>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Ncrypt.lib")
#pragma comment(lib, "Ws2_32.lib")

#define SDK_INTEGRATION_TIMEOUT_MILLISECONDS 10000

typedef struct _SDK_INTEGRATION_CONTEXT
{
    HANDLE ServerRunningEvent;
    HANDLE ClientReadyEvent;
    HANDLE ClientRetryWaitEvent;
    HANDLE ClientStoppedEvent;
    HANDLE ServerReadyEvent;
    HANDLE ServerStoppedEvent;
    HANDLE ClientPongEvent;
    HANDLE SystemInfoEvent;
    HANDLE ProcessListEvent;
    HANDLE ProcessInfoEvent;
    HANDLE ServiceListEvent;
    HANDLE ServiceInfoEvent;
    HANDLE ProcessTerminateEvent;
    HANDLE ServiceControlEvent;
    HANDLE FileInfoEvent;
    HANDLE FileListEvent;
    HANDLE FileHashEvent;
    HANDLE FileReadEvent;
    HANDLE TerminalWritableEvent;
    HANDLE TerminalResizeEvent;
    HANDLE TerminalCloseEvent;
    volatile LONG ClientReadyStatus;
    volatile LONG ClientStoppedStatus;
    volatile LONG ServerReadyStatus;
    volatile LONG ServerStoppedStatus;
    ULONGLONG ClientPongToken;
    volatile LONG SystemInfoStatus;
    ZP_SYSTEM_ARCHITECTURE SystemArchitecture;
    ULONG SystemProcessorCount;
    ULONGLONG SystemPhysicalMemoryBytes;
    ULONG SystemComputerNameLength;
    volatile LONG ProcessListStatus;
    ULONG ProcessCount;
    LOGICAL FoundCurrentProcess;
    volatile LONG ProcessCompletionCount;
    LONG ExpectedProcessCompletions;
    LOGICAL CollectProcessDetails;
    volatile LONG ProcessInfoStatus;
    ULONG ProcessInfoId;
    ULONG ProcessInfoThreadCount;
    ULONGLONG ProcessInfoCreateTime;
    ULONG ProcessInfoImageNameLength;
    volatile LONG ServiceListStatus;
    ULONG ServiceCount;
    LOGICAL FoundNamedService;
    WCHAR ServiceName[256];
    ULONG ServiceNameLength;
    volatile LONG ServiceInfoStatus;
    ULONG ServiceInfoType;
    ULONG ServiceInfoStartType;
    ULONG ServiceInfoNameLength;
    ULONG ServiceInfoDisplayNameLength;
    ULONG ServiceInfoBinaryPathLength;
    volatile LONG AuthorizationCount;
    volatile LONG SawAuthenticatedClientId;
    volatile LONG AllowControl;
    volatile LONG ProcessTerminateStatus;
    volatile LONG ServiceControlStatus;
    volatile LONG FileInfoStatus;
    ULONG FileAttributes;
    ULONGLONG FileSize;
    ULONGLONG FileLastWriteTime;
    volatile LONG FileListStatus;
    ULONG FileCount;
    WCHAR ExpectedFileName[MAX_PATH];
    ULONG ExpectedFileNameLength;
    LOGICAL FoundExpectedFile;
    volatile LONG FileHashStatus;
    ZP_FILE_HASH_ALGORITHM FileHashAlgorithm;
    ULONGLONG FileHashSize;
    BYTE FileDigest[ZP_FILE_SHA256_SIZE];
    volatile LONG FileOpenReadStatus;
    volatile LONG FileReadCloseStatus;
    ZP_CHANNEL_HANDLE FileReadChannel;
    ULONGLONG FileReadSize;
    ULONGLONG FileReadOffset;
    ULONGLONG FileReadBytes;
    ULONGLONG FileReadHash;
    volatile LONG TerminalCreateStatus;
    volatile LONG TerminalResizeStatus;
    volatile LONG TerminalCloseStatus;
    ZP_CHANNEL_HANDLE TerminalChannel;
    ULONG TerminalProcessId;
    ULONG TerminalWritableCredit;
    ULONGLONG TerminalDataBytes;
} SDK_INTEGRATION_CONTEXT, *PSDK_INTEGRATION_CONTEXT;

static
VOID
NTAPI
SDKIntegration_ClientStateCallback(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Client);
    if (State == ZpClientStateReady)
    {
        InterlockedExchange(&TestContext->ClientReadyStatus, Status);
        SetEvent(TestContext->ClientReadyEvent);
    }
    else if (State == ZpClientStateRetryWait)
    {
        SetEvent(TestContext->ClientRetryWaitEvent);
    }
    else if (State == ZpClientStateStopped)
    {
        InterlockedExchange(&TestContext->ClientStoppedStatus, Status);
        SetEvent(TestContext->ClientStoppedEvent);
    }
}

static
VOID
NTAPI
SDKIntegration_ClientPongCallback(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONGLONG Token,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Client);
    TestContext->ClientPongToken = Token;
    SetEvent(TestContext->ClientPongEvent);
}

static
VOID
NTAPI
SDKIntegration_SystemInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ const ZP_SYSTEM_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    if (NT_SUCCESS(Status))
    {
        TestContext->SystemArchitecture = Info->Architecture;
        TestContext->SystemProcessorCount = Info->ProcessorCount;
        TestContext->SystemPhysicalMemoryBytes = Info->PhysicalMemoryBytes;
        TestContext->SystemComputerNameLength = Info->ComputerName.Length;
    }
    InterlockedExchange(&TestContext->SystemInfoStatus, Status);
    SetEvent(TestContext->SystemInfoEvent);
}

static
VOID
NTAPI
SDKIntegration_ProcessListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_PROCESS_LIST_VIEW Processes,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;
    ZP_PROCESS_RECORD_VIEW Process;
    ULONG Index;

    UNREFERENCED_PARAMETER(Request);
    if (NT_SUCCESS(Status) && TestContext->CollectProcessDetails)
    {
        TestContext->ProcessCount = Processes->Count;
        for (Index = 0; Index < Processes->Count; Index++)
        {
            Status = ZpProcess_GetRecord(Processes, Index, &Process);
            if (!NT_SUCCESS(Status))
            {
                break;
            }
            if (Process.ProcessId == GetCurrentProcessId())
            {
                TestContext->FoundCurrentProcess = TRUE;
            }
        }
    }
    InterlockedExchange(&TestContext->ProcessListStatus, Status);
    if (InterlockedIncrement(&TestContext->ProcessCompletionCount) >=
        TestContext->ExpectedProcessCompletions)
    {
        SetEvent(TestContext->ProcessListEvent);
    }
}

static
VOID
NTAPI
SDKIntegration_ProcessInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ const ZP_PROCESS_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    if (NT_SUCCESS(Status))
    {
        TestContext->ProcessInfoId = Info->ProcessId;
        TestContext->ProcessInfoThreadCount = Info->ThreadCount;
        TestContext->ProcessInfoCreateTime = Info->CreateTime;
        TestContext->ProcessInfoImageNameLength = Info->ImageName.Length;
    }
    InterlockedExchange(&TestContext->ProcessInfoStatus, Status);
    SetEvent(TestContext->ProcessInfoEvent);
}

static
VOID
NTAPI
SDKIntegration_ServiceListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_SERVICE_LIST_VIEW Services,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;
    ZP_SERVICE_RECORD_VIEW Service;
    ULONG Index;

    UNREFERENCED_PARAMETER(Request);
    if (NT_SUCCESS(Status))
    {
        TestContext->ServiceCount = Services->Count;
        for (Index = 0; Index < Services->Count; Index++)
        {
            Status = ZpService_GetRecord(Services, Index, &Service);
            if (!NT_SUCCESS(Status))
            {
                break;
            }
            if (Service.ServiceName.Length != 0 &&
                Service.DisplayName.Length != 0)
            {
                TestContext->FoundNamedService = TRUE;
                if (TestContext->ServiceNameLength == 0 &&
                    Service.ServiceName.Length <
                        ARRAYSIZE(TestContext->ServiceName))
                {
                    RtlCopyMemory(TestContext->ServiceName,
                                  Service.ServiceName.Buffer,
                                  (SIZE_T)Service.ServiceName.Length *
                                      sizeof(WCHAR));
                    TestContext->ServiceName[Service.ServiceName.Length] =
                        UNICODE_NULL;
                    TestContext->ServiceNameLength =
                        Service.ServiceName.Length;
                }
            }
        }
    }
    InterlockedExchange(&TestContext->ServiceListStatus, Status);
    SetEvent(TestContext->ServiceListEvent);
}

static
VOID
NTAPI
SDKIntegration_ServiceInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ const ZP_SERVICE_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    if (NT_SUCCESS(Status))
    {
        TestContext->ServiceInfoType = Info->ServiceType;
        TestContext->ServiceInfoStartType = Info->StartType;
        TestContext->ServiceInfoNameLength = Info->ServiceName.Length;
        TestContext->ServiceInfoDisplayNameLength = Info->DisplayName.Length;
        TestContext->ServiceInfoBinaryPathLength = Info->BinaryPathName.Length;
    }
    InterlockedExchange(&TestContext->ServiceInfoStatus, Status);
    SetEvent(TestContext->ServiceInfoEvent);
}

static
VOID
NTAPI
SDKIntegration_ProcessTerminateCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    InterlockedExchange(&TestContext->ProcessTerminateStatus, Status);
    SetEvent(TestContext->ProcessTerminateEvent);
}

static
VOID
NTAPI
SDKIntegration_ServiceControlCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    InterlockedExchange(&TestContext->ServiceControlStatus, Status);
    SetEvent(TestContext->ServiceControlEvent);
}

static
VOID
NTAPI
SDKIntegration_FileInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_FILE_INFO Info,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    if (NT_SUCCESS(Status))
    {
        TestContext->FileAttributes = Info->Attributes;
        TestContext->FileSize = Info->Size;
        TestContext->FileLastWriteTime = Info->LastWriteTime;
    }
    InterlockedExchange(&TestContext->FileInfoStatus, Status);
    SetEvent(TestContext->FileInfoEvent);
}

static
VOID
NTAPI
SDKIntegration_FileListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_FILE_LIST_VIEW Files,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;
    ZP_FILE_RECORD_VIEW File;
    ULONG Index;

    UNREFERENCED_PARAMETER(Request);
    if (NT_SUCCESS(Status))
    {
        TestContext->FileCount = Files->Count;
        for (Index = 0; Index < Files->Count; Index++)
        {
            Status = ZpFile_GetRecord(Files, Index, &File);
            if (!NT_SUCCESS(Status))
            {
                break;
            }
            if (File.Name.Length == TestContext->ExpectedFileNameLength &&
                RtlCompareMemory(File.Name.Buffer,
                                 TestContext->ExpectedFileName,
                                 (SIZE_T)File.Name.Length * sizeof(WCHAR)) ==
                    (SIZE_T)File.Name.Length * sizeof(WCHAR))
            {
                TestContext->FoundExpectedFile = TRUE;
            }
        }
    }
    InterlockedExchange(&TestContext->FileListStatus, Status);
    SetEvent(TestContext->FileListEvent);
}

static
VOID
NTAPI
SDKIntegration_FileHashCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PCZP_FILE_HASH_VIEW Hash,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    if (NT_SUCCESS(Status))
    {
        TestContext->FileHashAlgorithm = Hash->Algorithm;
        TestContext->FileHashSize = Hash->FileSize;
        RtlCopyMemory(TestContext->FileDigest,
                      Hash->Digest.Buffer,
                      Hash->Digest.Length);
    }
    InterlockedExchange(&TestContext->FileHashStatus, Status);
    SetEvent(TestContext->FileHashEvent);
}

static
VOID
NTAPI
SDKIntegration_FileOpenReadCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_ ULONGLONG Offset,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->FileReadChannel = Channel;
    TestContext->FileReadSize = FileSize;
    TestContext->FileReadOffset = Offset;
    InterlockedExchange(&TestContext->FileOpenReadStatus, Status);
    if (!NT_SUCCESS(Status))
    {
        SetEvent(TestContext->FileReadEvent);
    }
}

static
VOID
NTAPI
SDKIntegration_ChannelDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;
    ULONG Index;

    UNREFERENCED_PARAMETER(Channel);
    for (Index = 0; Index < Data->Length; Index++)
    {
        TestContext->FileReadHash ^= Data->Buffer[Index];
        TestContext->FileReadHash *= 1099511628211ULL;
    }
    TestContext->FileReadBytes += Data->Length;
}

static
VOID
NTAPI
SDKIntegration_ChannelCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    InterlockedExchange(&TestContext->FileReadCloseStatus, Status);
    SetEvent(TestContext->FileReadEvent);
}

static
VOID
NTAPI
SDKIntegration_TerminalCreateCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG ProcessId,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->TerminalChannel = Channel;
    TestContext->TerminalProcessId = ProcessId;
    InterlockedExchange(&TestContext->TerminalCreateStatus, Status);
    if (!NT_SUCCESS(Status))
    {
        SetEvent(TestContext->TerminalCloseEvent);
    }
}

static
VOID
NTAPI
SDKIntegration_TerminalDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    TestContext->TerminalDataBytes += Data->Length;
}

static
VOID
NTAPI
SDKIntegration_TerminalWritableCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    TestContext->TerminalWritableCredit += CreditBytes;
    SetEvent(TestContext->TerminalWritableEvent);
}

static
VOID
NTAPI
SDKIntegration_TerminalResizeCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    InterlockedExchange(&TestContext->TerminalResizeStatus, Status);
    SetEvent(TestContext->TerminalResizeEvent);
}

static
VOID
NTAPI
SDKIntegration_TerminalCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    InterlockedExchange(&TestContext->TerminalCloseStatus, Status);
    SetEvent(TestContext->TerminalCloseEvent);
}

static
LOGICAL
SDKIntegration_HashFile(
    _In_ PCWSTR Path,
    _In_ ULONGLONG Offset,
    _Out_ PULONGLONG Bytes,
    _Out_ PULONGLONG Hash)
{
    BYTE Buffer[0x10000];
    LARGE_INTEGER Position;
    HANDLE File;
    DWORD BytesRead;
    ULONG Index;
    LOGICAL Result = FALSE;

    File = CreateFileW(Path,
                       GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                       NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    Position.QuadPart = Offset;
    if (!SetFilePointerEx(File, Position, NULL, FILE_BEGIN))
    {
        goto Cleanup;
    }
    *Bytes = 0;
    *Hash = 1469598103934665603ULL;
    do
    {
        if (!ReadFile(File, Buffer, sizeof(Buffer), &BytesRead, NULL))
        {
            goto Cleanup;
        }
        for (Index = 0; Index < BytesRead; Index++)
        {
            *Hash ^= Buffer[Index];
            *Hash *= 1099511628211ULL;
        }
        *Bytes += BytesRead;
    } while (BytesRead != 0);
    Result = TRUE;

Cleanup:
    CloseHandle(File);
    return Result;
}

static
LOGICAL
SDKIntegration_HashFileSha256(
    _In_ PCWSTR Path,
    _Out_writes_bytes_(ZP_FILE_SHA256_SIZE) BYTE* Digest)
{
    BCRYPT_ALG_HANDLE Algorithm = NULL;
    BCRYPT_HASH_HANDLE Hash = NULL;
    BYTE Buffer[0x10000];
    HANDLE File;
    DWORD BytesRead;
    NTSTATUS Status;
    LOGICAL Result = FALSE;

    File = CreateFileW(Path,
                       GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                       NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    Status = BCryptOpenAlgorithmProvider(&Algorithm,
                                         BCRYPT_SHA256_ALGORITHM,
                                         NULL,
                                         0);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    Status = BCryptCreateHash(Algorithm, &Hash, NULL, 0, NULL, 0, 0);
    while (NT_SUCCESS(Status))
    {
        if (!ReadFile(File, Buffer, sizeof(Buffer), &BytesRead, NULL))
        {
            goto Cleanup;
        }
        if (BytesRead == 0)
        {
            break;
        }
        Status = BCryptHashData(Hash, Buffer, BytesRead, 0);
    }
    if (NT_SUCCESS(Status))
    {
        Status = BCryptFinishHash(Hash, Digest, ZP_FILE_SHA256_SIZE, 0);
    }
    Result = NT_SUCCESS(Status);

Cleanup:
    if (Hash != NULL)
    {
        BCryptDestroyHash(Hash);
    }
    if (Algorithm != NULL)
    {
        BCryptCloseAlgorithmProvider(Algorithm, 0);
    }
    CloseHandle(File);
    return Result;
}

static
VOID
NTAPI
SDKIntegration_ServerStateCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_SERVER_STATE State,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Server);
    if (State == ZpServerStateRunning)
    {
        if (NT_SUCCESS(Status))
        {
            SetEvent(TestContext->ServerRunningEvent);
        }
    }
    else if (State == ZpServerStateStopped)
    {
        InterlockedExchange(&TestContext->ServerStoppedStatus, Status);
        SetEvent(TestContext->ServerStoppedEvent);
    }
}

static
VOID
NTAPI
SDKIntegration_ServerConnectionCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_CONNECTION_PHASE Phase,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(Connection);
    if (Phase == ZpConnectionPhaseReady)
    {
        InterlockedExchange(&TestContext->ServerReadyStatus, Status);
        SetEvent(TestContext->ServerReadyEvent);
    }
}

static
NTSTATUS
NTAPI
SDKIntegration_ServerAuthorizeCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ZP_CLIENT_ID_SIZE) const BYTE ClientId[ZP_CLIENT_ID_SIZE],
    _In_ ZP_REQUEST_ACCESS Access,
    _In_ USHORT ModuleId,
    _In_ USHORT OperationId,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;
    ULONG Index;

    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(Connection);
    UNREFERENCED_PARAMETER(ModuleId);
    UNREFERENCED_PARAMETER(OperationId);
    UNREFERENCED_PARAMETER(Payload);
    if (Access == ZpRequestAccessControl && !TestContext->AllowControl)
    {
        return STATUS_ACCESS_DENIED;
    }
    for (Index = 0; Index < ZP_CLIENT_ID_SIZE; Index++)
    {
        if (ClientId[Index] != 0)
        {
            InterlockedExchange(&TestContext->SawAuthenticatedClientId, TRUE);
            break;
        }
    }
    InterlockedIncrement(&TestContext->AuthorizationCount);
    return STATUS_SUCCESS;
}

static
PCCERT_CONTEXT
SDKIntegration_CreateCertificate(
    _Out_ HCERTSTORE* Store)
{
    static const WCHAR Subject[] = L"CN=localhost";
    static const WCHAR ServerName[] = L"localhost";
    static PSTR EnhancedKeyUsages[] = { szOID_PKIX_KP_SERVER_AUTH };
    CERT_ALT_NAME_ENTRY AlternateNameEntry = { CERT_ALT_NAME_DNS_NAME };
    CERT_ALT_NAME_INFO AlternateNames = { 1, &AlternateNameEntry };
    CERT_ENHKEY_USAGE EnhancedKeyUsage = { ARRAYSIZE(EnhancedKeyUsages), EnhancedKeyUsages };
    CERT_EXTENSION Extensions[3] = { 0 };
    CERT_EXTENSIONS CertificateExtensions = { ARRAYSIZE(Extensions), Extensions };
    CERT_NAME_BLOB SubjectName = { 0 };
    SYSTEMTIME StartTime = { 0 }, EndTime = { 0 };
    PCCERT_CONTEXT Certificate = NULL, StoredCertificate = NULL;
    PBYTE AlternateNamesEncoded = NULL, EnhancedKeyUsageEncoded = NULL;
    BYTE KeyUsage = CERT_DIGITAL_SIGNATURE_KEY_USAGE;
    CRYPT_BIT_BLOB KeyUsageBlob = { sizeof(KeyUsage), &KeyUsage, 0 };
    DWORD AlternateNamesSize = 0, EnhancedKeyUsageSize = 0;
    DWORD KeyUsageSize = sizeof(KeyUsage);

    *Store = NULL;
    if (!CertStrToNameW(X509_ASN_ENCODING,
                        Subject,
                        CERT_X500_NAME_STR,
                        NULL,
                        NULL,
                        &SubjectName.cbData,
                        NULL))
    {
        goto Cleanup;
    }
    SubjectName.pbData = HeapAlloc(GetProcessHeap(), 0, SubjectName.cbData);
    if (SubjectName.pbData == NULL ||
        !CertStrToNameW(X509_ASN_ENCODING,
                        Subject,
                        CERT_X500_NAME_STR,
                        NULL,
                        SubjectName.pbData,
                        &SubjectName.cbData,
                        NULL))
    {
        goto Cleanup;
    }

    AlternateNameEntry.pwszDNSName = (PWSTR)ServerName;
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING,
                             X509_ALTERNATE_NAME,
                             &AlternateNames,
                             CRYPT_ENCODE_ALLOC_FLAG,
                             NULL,
                             &AlternateNamesEncoded,
                             &AlternateNamesSize) ||
        !CryptEncodeObjectEx(X509_ASN_ENCODING,
                             X509_ENHANCED_KEY_USAGE,
                             &EnhancedKeyUsage,
                             CRYPT_ENCODE_ALLOC_FLAG,
                             NULL,
                             &EnhancedKeyUsageEncoded,
                             &EnhancedKeyUsageSize))
    {
        goto Cleanup;
    }
    Extensions[0].pszObjId = szOID_SUBJECT_ALT_NAME2;
    Extensions[0].Value.cbData = AlternateNamesSize;
    Extensions[0].Value.pbData = AlternateNamesEncoded;
    Extensions[1].pszObjId = szOID_ENHANCED_KEY_USAGE;
    Extensions[1].Value.cbData = EnhancedKeyUsageSize;
    Extensions[1].Value.pbData = EnhancedKeyUsageEncoded;
    Extensions[2].pszObjId = szOID_KEY_USAGE;
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING,
                             X509_KEY_USAGE,
                             &KeyUsageBlob,
                             CRYPT_ENCODE_ALLOC_FLAG,
                             NULL,
                             &Extensions[2].Value.pbData,
                             &KeyUsageSize))
    {
        goto Cleanup;
    }
    Extensions[2].Value.cbData = KeyUsageSize;

    GetSystemTime(&StartTime);
    EndTime = StartTime;
    if (StartTime.wDay > 1)
    {
        StartTime.wDay--;
    }
    EndTime.wYear += 1;
    Certificate = CertCreateSelfSignCertificate(
        0,
        &SubjectName,
        0,
        NULL,
        NULL,
        &StartTime,
        &EndTime,
        &CertificateExtensions);
    if (Certificate != NULL)
    {
        *Store = CertOpenStore(CERT_STORE_PROV_MEMORY,
                               X509_ASN_ENCODING,
                               0,
                               CERT_STORE_CREATE_NEW_FLAG,
                               NULL);
        if (*Store != NULL &&
            CertAddCertificateContextToStore(*Store,
                                             Certificate,
                                             CERT_STORE_ADD_ALWAYS,
                                             &StoredCertificate))
        {
            CertFreeCertificateContext(Certificate);
            Certificate = StoredCertificate;
        }
        else
        {
            CertFreeCertificateContext(Certificate);
            Certificate = NULL;
        }
    }

Cleanup:
    if (SubjectName.pbData != NULL)
    {
        HeapFree(GetProcessHeap(), 0, SubjectName.pbData);
    }
    LocalFree(AlternateNamesEncoded);
    LocalFree(EnhancedKeyUsageEncoded);
    LocalFree(Extensions[2].Value.pbData);
    if (Certificate == NULL)
    {
        if (*Store != NULL)
        {
            CertCloseStore(*Store, 0);
            *Store = NULL;
        }
    }
    return Certificate;
}

static
VOID
SDKIntegration_DeleteCertificateKey(
    _In_ PCCERT_CONTEXT Certificate)
{
    PCRYPT_KEY_PROV_INFO KeyProviderInfo;
    NCRYPT_PROV_HANDLE KeyProvider = 0;
    NCRYPT_KEY_HANDLE Key = 0;
    HCRYPTPROV Provider = 0;
    DWORD Size = 0;

    if (!CertGetCertificateContextProperty(Certificate,
                                           CERT_KEY_PROV_INFO_PROP_ID,
                                           NULL,
                                           &Size))
    {
        return;
    }
    KeyProviderInfo = HeapAlloc(GetProcessHeap(), 0, Size);
    if (KeyProviderInfo == NULL ||
        !CertGetCertificateContextProperty(Certificate,
                                           CERT_KEY_PROV_INFO_PROP_ID,
                                           KeyProviderInfo,
                                           &Size))
    {
        if (KeyProviderInfo != NULL)
        {
            HeapFree(GetProcessHeap(), 0, KeyProviderInfo);
        }
        return;
    }
    if (KeyProviderInfo->dwKeySpec == CERT_NCRYPT_KEY_SPEC)
    {
        if (NCryptOpenStorageProvider(&KeyProvider,
                                      KeyProviderInfo->pwszProvName,
                                      0) == ERROR_SUCCESS &&
            NCryptOpenKey(KeyProvider,
                          &Key,
                          KeyProviderInfo->pwszContainerName,
                          0,
                          KeyProviderInfo->dwFlags & CRYPT_MACHINE_KEYSET ?
                              NCRYPT_MACHINE_KEY_FLAG :
                              0) == ERROR_SUCCESS)
        {
            NCryptDeleteKey(Key, NCRYPT_SILENT_FLAG);
        }
        if (KeyProvider != 0)
        {
            NCryptFreeObject(KeyProvider);
        }
    }
    else
    {
        CryptAcquireContextW(&Provider,
                             KeyProviderInfo->pwszContainerName,
                             KeyProviderInfo->pwszProvName,
                             KeyProviderInfo->dwProvType,
                             CRYPT_DELETEKEYSET | CRYPT_SILENT |
                                 (KeyProviderInfo->dwFlags & CRYPT_MACHINE_KEYSET));
    }
    HeapFree(GetProcessHeap(), 0, KeyProviderInfo);
}

static
USHORT
SDKIntegration_GetFreePort(VOID)
{
    WSADATA WinsockData;
    SOCKADDR_IN Address = { 0 };
    int AddressLength = sizeof(Address);
    SOCKET Socket = INVALID_SOCKET;
    USHORT Port = 0;

    if (WSAStartup(MAKEWORD(2, 2), &WinsockData) != 0)
    {
        return 0;
    }
    Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (Socket == INVALID_SOCKET)
    {
        goto Cleanup;
    }
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(Socket, (PSOCKADDR)&Address, sizeof(Address)) == SOCKET_ERROR ||
        getsockname(Socket, (PSOCKADDR)&Address, &AddressLength) == SOCKET_ERROR)
    {
        goto Cleanup;
    }
    Port = ntohs(Address.sin_port);

Cleanup:
    if (Socket != INVALID_SOCKET)
    {
        closesocket(Socket);
    }
    WSACleanup();
    return Port;
}

TEST_FUNC(SDKQuicIntegration)
{
    static const WCHAR ServerName[] = L"localhost";
    static const WCHAR MissingServiceName[] =
        L"KNSoft.ZPigeon.UnitTest.DoesNotExist";
    static const WCHAR TerminalCommandLine[] =
        L"powershell.exe -NoLogo -NoProfile -Command \"Write-Output "
        L"ZPIGEON_TERMINAL_OK; Start-Sleep -Milliseconds 500; exit 7\"";
    static const BYTE TerminalInput[] =
        "ZPIGEON_TERMINAL_OK\r\n";
    SDK_INTEGRATION_CONTEXT TestContext = { 0 };
    ZP_MODULE_RECORD ClientModules[] = {
        { 1, 3, 0x0F },
        { 2, 1, 0x03 },
        { 3, 1, 0x01 },
        { 4, 1, 0x03 },
        { 5, 1, 0x00 }
    };
    ZP_MODULE_RECORD ServerModules[] = {
        { 1, 2, 0x05 },
        { 2, 1, 0x03 },
        { 3, 1, 0x01 },
        { 4, 1, 0x03 },
        { 5, 1, 0x00 }
    };
    ZP_ENDPOINT Endpoint = { ZpTransportQuic, L"127.0.0.1", 0, ServerName, NULL };
    ZP_LISTENER_ENDPOINT Listener = { ZpTransportQuic, L"127.0.0.1", 0, NULL };
    ZP_SERVER_DEPLOYMENT Deployment = { ServerName, NULL };
    ZP_CLIENT_CONFIG ClientConfig = { 0 };
    ZP_SERVER_CONFIG ServerConfig = { 0 };
    ZP_CLIENT_HANDLE Client = NULL;
    ZP_SERVER_HANDLE Server = NULL;
    ZP_REQUEST_HANDLE Request = NULL;
    NCRYPT_PROV_HANDLE IdentityProvider = 0;
    NCRYPT_KEY_HANDLE IdentityKey = 0;
    HCERTSTORE CertificateStore = NULL;
    PCCERT_CONTEXT Certificate = NULL;
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION TemporaryProcess = { 0 };
    WCHAR TemporaryCommand[] = L"ping.exe -n 30 127.0.0.1";
    WCHAR ModulePath[MAX_PATH];
    NTSTATUS Status;
    DWORD WaitStatus;
    DWORD ServerStopWait = MAXDWORD, RetryWait = MAXDWORD, ProcessWait = MAXDWORD;
    ULONG Index;
    ULONGLONG ExpectedFileReadBytes, ExpectedFileReadHash;
    BYTE ExpectedFileDigest[ZP_FILE_SHA256_SIZE];
    LOGICAL Result = FALSE;
    HANDLE Events[] = {
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL)
    };

    for (Index = 0; Index < ARRAYSIZE(Events); Index++)
    {
        if (Events[Index] == NULL)
        {
            goto Cleanup;
        }
    }
    TestContext.ServerRunningEvent = Events[0];
    TestContext.ClientReadyEvent = Events[1];
    TestContext.ClientRetryWaitEvent = Events[2];
    TestContext.ClientStoppedEvent = Events[3];
    TestContext.ServerReadyEvent = Events[4];
    TestContext.ServerStoppedEvent = Events[5];
    TestContext.ClientPongEvent = Events[6];
    TestContext.SystemInfoEvent = Events[7];
    TestContext.ProcessListEvent = Events[8];
    TestContext.ProcessInfoEvent = Events[9];
    TestContext.ServiceListEvent = Events[10];
    TestContext.ServiceInfoEvent = Events[11];
    TestContext.ProcessTerminateEvent = Events[12];
    TestContext.ServiceControlEvent = Events[13];
    TestContext.FileInfoEvent = Events[14];
    TestContext.FileListEvent = Events[15];
    TestContext.FileHashEvent = Events[16];
    TestContext.FileReadEvent = Events[17];
    TestContext.TerminalWritableEvent = Events[18];
    TestContext.TerminalResizeEvent = Events[19];
    TestContext.TerminalCloseEvent = Events[20];
    if (NCryptOpenStorageProvider(&IdentityProvider,
                                  MS_KEY_STORAGE_PROVIDER,
                                  0) != ERROR_SUCCESS ||
        NCryptCreatePersistedKey(IdentityProvider,
                                 &IdentityKey,
                                 NCRYPT_ECDSA_P256_ALGORITHM,
                                 NULL,
                                 0,
                                 0) != ERROR_SUCCESS ||
        NCryptFinalizeKey(IdentityKey, NCRYPT_SILENT_FLAG) != ERROR_SUCCESS)
    {
        goto Cleanup;
    }

    Certificate = SDKIntegration_CreateCertificate(&CertificateStore);
    Endpoint.Port = Listener.Port = SDKIntegration_GetFreePort();
    if (Certificate == NULL || Endpoint.Port == 0)
    {
        goto Cleanup;
    }
    Deployment.Certificate = Certificate;

    ClientConfig.Size = sizeof(ClientConfig);
    ClientConfig.Endpoints = &Endpoint;
    ClientConfig.EndpointCount = 1;
    ClientConfig.DeploymentRootCertificate = Certificate->pbCertEncoded;
    ClientConfig.DeploymentRootCertificateLength = Certificate->cbCertEncoded;
    ClientConfig.ClientKeyName = NULL;
    ClientConfig.Modules = ClientModules;
    ClientConfig.ModuleCount = ARRAYSIZE(ClientModules);
    ClientConfig.ConnectTimeoutMilliseconds = SDK_INTEGRATION_TIMEOUT_MILLISECONDS;
    ClientConfig.StateCallback = SDKIntegration_ClientStateCallback;
    ClientConfig.PongCallback = SDKIntegration_ClientPongCallback;
    ClientConfig.CallbackContext = &TestContext;

    ServerConfig.Size = sizeof(ServerConfig);
    ServerConfig.Listeners = &Listener;
    ServerConfig.ListenerCount = 1;
    ServerConfig.Deployments = &Deployment;
    ServerConfig.DeploymentCount = 1;
    ServerConfig.Modules = ServerModules;
    ServerConfig.ModuleCount = ARRAYSIZE(ServerModules);
    ServerConfig.MaxRequestsPerConnection = 4;
    ServerConfig.StateCallback = SDKIntegration_ServerStateCallback;
    ServerConfig.ConnectionCallback = SDKIntegration_ServerConnectionCallback;
    ServerConfig.CallbackContext = &TestContext;
    ServerConfig.AuthorizeCallback = SDKIntegration_ServerAuthorizeCallback;

    Status = ZpServer_Create(&ServerConfig, &Server);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    Status = ZpServer_Start(Server);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    if (
        WaitForSingleObject(TestContext.ServerRunningEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0)
    {
        goto Cleanup;
    }
    Status = ZpClient_Create(&ClientConfig, &Client);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    ((PZP_CLIENT_OBJECT)Client)->QuicTransport.ExternalKey = IdentityKey;
    Status = ZpClient_Start(Client);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    if (
        WaitForSingleObject(TestContext.ClientReadyEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        WaitForSingleObject(TestContext.ServerReadyEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.ClientReadyStatus) ||
        !NT_SUCCESS(TestContext.ServerReadyStatus))
    {
        goto Cleanup;
    }

    Status = ZpClient_Ping(Client, 0x0102030405060708);
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ClientPongEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        TestContext.ClientPongToken != 0x0102030405060708)
    {
        goto Cleanup;
    }

    Status = ZpClient_GetSystemInfo(Client,
                                    SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                    SDKIntegration_SystemInfoCallback,
                                    &TestContext,
                                    &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.SystemInfoEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.SystemInfoStatus) ||
        TestContext.SystemArchitecture < ZpSystemArchitectureX86 ||
        TestContext.SystemArchitecture > ZpSystemArchitectureArm64 ||
        TestContext.SystemProcessorCount == 0 ||
        TestContext.SystemPhysicalMemoryBytes == 0 ||
        TestContext.SystemComputerNameLength == 0)
    {
        goto Cleanup;
    }

    TestContext.ExpectedProcessCompletions = 1;
    TestContext.CollectProcessDetails = TRUE;
    Status = ZpClient_EnumerateProcesses(Client,
                                         SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                         SDKIntegration_ProcessListCallback,
                                         &TestContext,
                                         &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ProcessListEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.ProcessListStatus) ||
        TestContext.ProcessCount == 0 ||
        !TestContext.FoundCurrentProcess)
    {
        goto Cleanup;
    }

    Status = ZpClient_QueryProcess(Client,
                                   GetCurrentProcessId(),
                                   SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                   SDKIntegration_ProcessInfoCallback,
                                   &TestContext,
                                   &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ProcessInfoEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.ProcessInfoStatus) ||
        TestContext.ProcessInfoId != GetCurrentProcessId() ||
        TestContext.ProcessInfoThreadCount == 0 ||
        TestContext.ProcessInfoCreateTime == 0 ||
        TestContext.ProcessInfoImageNameLength == 0)
    {
        goto Cleanup;
    }

    Status = ZpClient_EnumerateServices(Client,
                                        SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                        SDKIntegration_ServiceListCallback,
                                        &TestContext,
                                        &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ServiceListEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.ServiceListStatus) ||
        TestContext.ServiceCount == 0 ||
        !TestContext.FoundNamedService)
    {
        goto Cleanup;
    }

    Status = ZpClient_QueryService(Client,
                                   TestContext.ServiceName,
                                   TestContext.ServiceNameLength,
                                   SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                   SDKIntegration_ServiceInfoCallback,
                                   &TestContext,
                                   &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ServiceInfoEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.ServiceInfoStatus) ||
        TestContext.ServiceInfoType == 0 ||
        TestContext.ServiceInfoNameLength != TestContext.ServiceNameLength ||
        TestContext.ServiceInfoDisplayNameLength == 0 ||
        TestContext.ServiceInfoBinaryPathLength == 0 ||
        TestContext.AuthorizationCount < 5 ||
        !TestContext.SawAuthenticatedClientId)
    {
        goto Cleanup;
    }

    if (!CreateProcessW(NULL,
                        TemporaryCommand,
                        NULL,
                        NULL,
                        FALSE,
                        CREATE_NO_WINDOW,
                        NULL,
                        NULL,
                        &StartupInfo,
                        &TemporaryProcess))
    {
        goto Cleanup;
    }
    Status = ZpClient_TerminateProcess(Client,
                                       TemporaryProcess.dwProcessId,
                                       0x10203040,
                                       SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                       SDKIntegration_ProcessTerminateCallback,
                                       &TestContext,
                                       &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ProcessTerminateEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        TestContext.ProcessTerminateStatus != STATUS_ACCESS_DENIED ||
        WaitForSingleObject(TemporaryProcess.hProcess, 0) != WAIT_TIMEOUT)
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.ProcessTerminateEvent);
    InterlockedExchange(&TestContext.AllowControl, TRUE);
    Status = ZpClient_TerminateProcess(Client,
                                       TemporaryProcess.dwProcessId,
                                       0x10203040,
                                       SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                       SDKIntegration_ProcessTerminateCallback,
                                       &TestContext,
                                       &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ProcessTerminateEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.ProcessTerminateStatus) ||
        WaitForSingleObject(TemporaryProcess.hProcess,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0)
    {
        goto Cleanup;
    }

    InterlockedExchange(&TestContext.AllowControl, FALSE);
    Status = ZpClient_StopService(Client,
                                  TestContext.ServiceName,
                                  TestContext.ServiceNameLength,
                                  SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                  SDKIntegration_ServiceControlCallback,
                                  &TestContext,
                                  &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ServiceControlEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        TestContext.ServiceControlStatus != STATUS_ACCESS_DENIED)
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.ServiceControlEvent);
    InterlockedExchange(&TestContext.AllowControl, TRUE);
    Status = ZpClient_StartService(Client,
                                   MissingServiceName,
                                   ARRAYSIZE(MissingServiceName) - 1,
                                   SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                   SDKIntegration_ServiceControlCallback,
                                   &TestContext,
                                   &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ServiceControlEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        NT_SUCCESS(TestContext.ServiceControlStatus) ||
        TestContext.ServiceControlStatus == STATUS_ACCESS_DENIED)
    {
        goto Cleanup;
    }

    Index = GetModuleFileNameW(NULL, ModulePath, ARRAYSIZE(ModulePath));
    if (Index == 0 || Index == ARRAYSIZE(ModulePath))
    {
        goto Cleanup;
    }
    Status = ZpClient_QueryFile(Client,
                                ModulePath,
                                Index,
                                SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                SDKIntegration_FileInfoCallback,
                                &TestContext,
                                &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.FileInfoEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.FileInfoStatus) ||
        TestContext.FileAttributes == INVALID_FILE_ATTRIBUTES ||
        TestContext.FileSize == 0 ||
        TestContext.FileLastWriteTime == 0)
    {
        goto Cleanup;
    }
    if (!SDKIntegration_HashFileSha256(ModulePath, ExpectedFileDigest))
    {
        goto Cleanup;
    }
    Status = ZpClient_HashFile(Client,
                               ModulePath,
                               Index,
                               ZpFileHashSha256,
                               SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                               SDKIntegration_FileHashCallback,
                               &TestContext,
                               &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.FileHashEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.FileHashStatus) ||
        TestContext.FileHashAlgorithm != ZpFileHashSha256 ||
        TestContext.FileHashSize != TestContext.FileSize ||
        RtlCompareMemory(TestContext.FileDigest,
                         ExpectedFileDigest,
                         sizeof(ExpectedFileDigest)) != sizeof(ExpectedFileDigest))
    {
        goto Cleanup;
    }
    TestContext.FileReadHash = 1469598103934665603ULL;
    if (!SDKIntegration_HashFile(ModulePath,
                                 17,
                                 &ExpectedFileReadBytes,
                                 &ExpectedFileReadHash))
    {
        goto Cleanup;
    }
    Status = ZpClient_OpenFileRead(Client,
                                  ModulePath,
                                  Index,
                                  17,
                                  SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                  SDKIntegration_FileOpenReadCallback,
                                  SDKIntegration_ChannelDataCallback,
                                  SDKIntegration_ChannelCloseCallback,
                                  &TestContext,
                                  &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.FileReadEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.FileOpenReadStatus) ||
        !NT_SUCCESS(TestContext.FileReadCloseStatus) ||
        TestContext.FileReadChannel == NULL ||
        TestContext.FileReadSize != TestContext.FileSize ||
        TestContext.FileReadOffset != 17 ||
        TestContext.FileReadBytes != ExpectedFileReadBytes ||
        TestContext.FileReadHash != ExpectedFileReadHash)
    {
        goto Cleanup;
    }
    ZpChannel_Close(TestContext.FileReadChannel);
    TestContext.FileReadChannel = NULL;
    for (; Index != 0; Index--)
    {
        if (ModulePath[Index - 1] == L'\\' || ModulePath[Index - 1] == L'/')
        {
            break;
        }
    }
    if (Index <= 1 ||
        ARRAYSIZE(ModulePath) - Index >=
            ARRAYSIZE(TestContext.ExpectedFileName))
    {
        goto Cleanup;
    }
    TestContext.ExpectedFileNameLength =
        (ULONG)wcslen(&ModulePath[Index]);
    RtlCopyMemory(TestContext.ExpectedFileName,
                  &ModulePath[Index],
                  ((SIZE_T)TestContext.ExpectedFileNameLength + 1) *
                      sizeof(WCHAR));
    ModulePath[Index - 1] = UNICODE_NULL;
    Status = ZpClient_EnumerateFiles(Client,
                                     ModulePath,
                                     Index - 1,
                                     SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                     SDKIntegration_FileListCallback,
                                     &TestContext,
                                     &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.FileListEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.FileListStatus) ||
        TestContext.FileCount == 0 ||
        !TestContext.FoundExpectedFile)
    {
        goto Cleanup;
    }

    Status = ZpClient_CreateTerminal(Client,
                                      80,
                                      25,
                                      TerminalCommandLine,
                                      ARRAYSIZE(TerminalCommandLine) - 1,
                                      NULL,
                                      0,
                                      SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                      SDKIntegration_TerminalCreateCallback,
                                      SDKIntegration_TerminalDataCallback,
                                      SDKIntegration_TerminalWritableCallback,
                                      SDKIntegration_TerminalCloseCallback,
                                      &TestContext,
                                      &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.TerminalWritableEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.TerminalCreateStatus) ||
        TestContext.TerminalChannel == NULL ||
        TestContext.TerminalProcessId == 0 ||
        TestContext.TerminalWritableCredit < sizeof(TerminalInput) - 1)
    {
        goto Cleanup;
    }
    Status = ZpClient_ResizeTerminal(TestContext.TerminalChannel,
                                     100,
                                     30,
                                     SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                     SDKIntegration_TerminalResizeCallback,
                                     &TestContext,
                                     &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.TerminalResizeEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.TerminalResizeStatus))
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.TerminalWritableEvent);
    Status = ZpChannel_Send(TestContext.TerminalChannel,
                            TerminalInput,
                            sizeof(TerminalInput) - 1);
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.TerminalWritableEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0)
    {
        goto Cleanup;
    }
    if (
        WaitForSingleObject(TestContext.TerminalCloseEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        TestContext.TerminalCloseStatus != 7 ||
        TestContext.TerminalDataBytes == 0)
    {
        goto Cleanup;
    }
    ZpChannel_Close(TestContext.TerminalChannel);
    TestContext.TerminalChannel = NULL;

    ResetEvent(TestContext.ServerRunningEvent);
    ResetEvent(TestContext.ClientReadyEvent);
    ResetEvent(TestContext.ServerReadyEvent);
    ResetEvent(TestContext.ProcessListEvent);
    InterlockedExchange(&TestContext.ProcessCompletionCount, 0);
    TestContext.ExpectedProcessCompletions = 8;
    TestContext.CollectProcessDetails = FALSE;
    for (Index = 0; Index < 8; Index++)
    {
        Status = ZpClient_EnumerateProcesses(Client,
                                             SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                             SDKIntegration_ProcessListCallback,
                                             &TestContext,
                                             &Request);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
        ZpRequest_Close(Request);
        Request = NULL;
    }
    Status = ZpServer_Stop(Server);
    ServerStopWait = WaitForSingleObject(TestContext.ServerStoppedEvent,
                                         SDK_INTEGRATION_TIMEOUT_MILLISECONDS);
    RetryWait = WaitForSingleObject(TestContext.ClientRetryWaitEvent,
                                    SDK_INTEGRATION_TIMEOUT_MILLISECONDS);
    ProcessWait = WaitForSingleObject(TestContext.ProcessListEvent,
                                      SDK_INTEGRATION_TIMEOUT_MILLISECONDS);
    if (!NT_SUCCESS(Status) ||
        ServerStopWait != WAIT_OBJECT_0 ||
        RetryWait != WAIT_OBJECT_0 ||
        ProcessWait != WAIT_OBJECT_0 ||
        TestContext.ProcessCompletionCount !=
            TestContext.ExpectedProcessCompletions)
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.ServerStoppedEvent);
    Status = ZpServer_Start(Server);
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ServerRunningEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        WaitForSingleObject(TestContext.ClientReadyEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        WaitForSingleObject(TestContext.ServerReadyEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !NT_SUCCESS(TestContext.ClientReadyStatus) ||
        !NT_SUCCESS(TestContext.ServerReadyStatus))
    {
        goto Cleanup;
    }
    Result = TRUE;

Cleanup:
    if (Request != NULL)
    {
        ZpRequest_Close(Request);
    }
    if (TestContext.FileReadChannel != NULL)
    {
        ZpChannel_Cancel(TestContext.FileReadChannel);
        ZpChannel_Close(TestContext.FileReadChannel);
        TestContext.FileReadChannel = NULL;
    }
    if (TestContext.TerminalChannel != NULL)
    {
        ZpChannel_Cancel(TestContext.TerminalChannel);
        ZpChannel_Close(TestContext.TerminalChannel);
        TestContext.TerminalChannel = NULL;
    }
    if (Client != NULL)
    {
        ZpClient_Stop(Client);
        WaitStatus = WaitForSingleObject(TestContext.ClientStoppedEvent,
                                         SDK_INTEGRATION_TIMEOUT_MILLISECONDS);
        Status = ZpClient_Close(Client);
        Result = Result && WaitStatus == WAIT_OBJECT_0 &&
                 NT_SUCCESS(TestContext.ClientStoppedStatus) && NT_SUCCESS(Status);
    }
    if (Server != NULL)
    {
        ZpServer_Stop(Server);
        WaitStatus = WaitForSingleObject(TestContext.ServerStoppedEvent,
                                         SDK_INTEGRATION_TIMEOUT_MILLISECONDS);
        Status = ZpServer_Close(Server);
        Result = Result && WaitStatus == WAIT_OBJECT_0 &&
                 NT_SUCCESS(TestContext.ServerStoppedStatus) && NT_SUCCESS(Status);
    }
    if (IdentityKey != 0)
    {
        NCryptFreeObject(IdentityKey);
    }
    if (IdentityProvider != 0)
    {
        NCryptFreeObject(IdentityProvider);
    }
    if (TemporaryProcess.hProcess != NULL)
    {
        if (WaitForSingleObject(TemporaryProcess.hProcess, 0) == WAIT_TIMEOUT)
        {
            TerminateProcess(TemporaryProcess.hProcess, STATUS_CANCELLED);
            WaitForSingleObject(TemporaryProcess.hProcess,
                                SDK_INTEGRATION_TIMEOUT_MILLISECONDS);
        }
        CloseHandle(TemporaryProcess.hProcess);
    }
    if (TemporaryProcess.hThread != NULL)
    {
        CloseHandle(TemporaryProcess.hThread);
    }
    if (Certificate != NULL)
    {
        SDKIntegration_DeleteCertificateKey(Certificate);
        CertFreeCertificateContext(Certificate);
    }
    if (CertificateStore != NULL)
    {
        CertCloseStore(CertificateStore, 0);
    }
    for (Index = 0; Index < ARRAYSIZE(Events); Index++)
    {
        if (Events[Index] != NULL)
        {
            CloseHandle(Events[Index]);
        }
    }
    TEST_OK(Result);
}
