#include "UnitTest.h"

#include <KNSoft/ZPigeon/Client.h>
#include <KNSoft/ZPigeon/Server.h>

#include "../KNSoft.ZPigeon.Client.SDK/Client.inl"

#include <Ncrypt.h>
#include <Ws2tcpip.h>

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
    SDK_INTEGRATION_CONTEXT TestContext = { 0 };
    ZP_MODULE_RECORD ClientModules[] = { { 1, 3, 0x0F }, { 2, 1, 0x03 } };
    ZP_MODULE_RECORD ServerModules[] = {
        { 1, 2, 0x05 },
        { 2, 1, 0x03 },
        { 3, 1, 0x01 }
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
    NTSTATUS Status;
    DWORD WaitStatus;
    DWORD ServerStopWait = MAXDWORD, RetryWait = MAXDWORD, ProcessWait = MAXDWORD;
    ULONG Index;
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
