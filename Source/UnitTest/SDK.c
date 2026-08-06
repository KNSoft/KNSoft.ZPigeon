#include "UnitTest.h"

#include <KNSoft/ZPigeon/Client.h>
#include <KNSoft/ZPigeon/Server.h>

#include "../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../KNSoft.ZPigeon.Server.SDK/Server.inl"
#include "../Network/Authentication.inl"
#include "../Network/Quic.inl"

typedef struct _SDK_TEST_CONTEXT
{
    NTSTATUS StartStatus;
    ULONG StartCount;
    ULONG StopCount;
    ULONG ClientStateCount;
    ZP_CLIENT_STATE ClientStates[8];
    NTSTATUS ClientStatuses[8];
    LOGICAL CloseClientOnStopped;
    NTSTATUS ClientCloseStatus;
    ULONG ServerStateCount;
    ZP_SERVER_STATE ServerStates[8];
    NTSTATUS ServerStatuses[8];
    LOGICAL CloseServerOnStopped;
    NTSTATUS ServerCloseStatus;
} SDK_TEST_CONTEXT, *PSDK_TEST_CONTEXT;

static
NTSTATUS
NTAPI
SDKTest_TransportStart(
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    TestContext->StartCount++;
    return TestContext->StartStatus;
}

static
VOID
NTAPI
SDKTest_TransportStop(
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    TestContext->StopCount++;
}

static const ZP_TRANSPORT_OPERATIONS SDKTest_TransportOperations = {
    SDKTest_TransportStart,
    SDKTest_TransportStop
};

static
LOGICAL
SDKTest_AuthenticationRoundTrip(VOID)
{
    BCRYPT_ALG_HANDLE Algorithm = NULL;
    BCRYPT_KEY_HANDLE Key = NULL;
    BCRYPT_ECCKEY_BLOB* Blob;
    BYTE BlobBuffer[sizeof(BCRYPT_ECCKEY_BLOB) + 64];
    BYTE PublicKey[ZP_CLIENT_PUBLIC_KEY_SIZE];
    BYTE Challenge[ZP_SERVER_CHALLENGE_SIZE] = { 1 };
    BYTE Hash[32], ClientId[32];
    BYTE Signature[ZP_CLIENT_SIGNATURE_SIZE];
    ULONG BlobSize, SignatureSize;
    NTSTATUS Status;
    LOGICAL Result = FALSE;

    Status = BCryptOpenAlgorithmProvider(&Algorithm,
                                         BCRYPT_ECDSA_P256_ALGORITHM,
                                         NULL,
                                         0);
    if (!NT_SUCCESS(Status) ||
        !NT_SUCCESS(Status = BCryptGenerateKeyPair(Algorithm, &Key, 256, 0)) ||
        !NT_SUCCESS(Status = BCryptFinalizeKeyPair(Key, 0)) ||
        !NT_SUCCESS(Status = BCryptExportKey(Key,
                                             NULL,
                                             BCRYPT_ECCPUBLIC_BLOB,
                                             BlobBuffer,
                                             sizeof(BlobBuffer),
                                             &BlobSize,
                                             0)))
    {
        goto Cleanup;
    }
    Blob = (BCRYPT_ECCKEY_BLOB*)BlobBuffer;
    if (BlobSize != sizeof(BlobBuffer) ||
        Blob->dwMagic != BCRYPT_ECDSA_PUBLIC_P256_MAGIC ||
        Blob->cbKey != 32)
    {
        goto Cleanup;
    }
    PublicKey[0] = 0x04;
    RtlCopyMemory(PublicKey + 1, BlobBuffer + sizeof(*Blob), 64);
    Status = ZpAuthentication_Hash(Challenge, PublicKey, Hash);
    if (!NT_SUCCESS(Status) ||
        !NT_SUCCESS(Status = BCryptSignHash(Key,
                                            NULL,
                                            Hash,
                                            sizeof(Hash),
                                            Signature,
                                            sizeof(Signature),
                                            &SignatureSize,
                                            0)) ||
        SignatureSize != sizeof(Signature) ||
        !NT_SUCCESS(ZpAuthentication_Verify(PublicKey, Challenge, Signature)) ||
        !NT_SUCCESS(ZpAuthentication_GetClientId(PublicKey, ClientId)))
    {
        goto Cleanup;
    }
    Signature[0] ^= 1;
    Result = !NT_SUCCESS(ZpAuthentication_Verify(PublicKey, Challenge, Signature));

Cleanup:
    if (Key != NULL)
    {
        BCryptDestroyKey(Key);
    }
    if (Algorithm != NULL)
    {
        BCryptCloseAlgorithmProvider(Algorithm, 0);
    }
    RtlSecureZeroMemory(Hash, sizeof(Hash));
    RtlSecureZeroMemory(Signature, sizeof(Signature));
    return Result;
}

static
VOID
NTAPI
SDKTest_ClientStateCallback(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;
    ULONG Index;

    UNREFERENCED_PARAMETER(Client);
    if (TestContext != NULL)
    {
        Index = TestContext->ClientStateCount++;
        TestContext->ClientStates[Index] = State;
        TestContext->ClientStatuses[Index] = Status;
        if (TestContext->CloseClientOnStopped && State == ZpClientStateStopped)
        {
            TestContext->ClientCloseStatus = ZpClient_Close(Client);
        }
    }
}

static
VOID
NTAPI
SDKTest_ServerStateCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_SERVER_STATE State,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;
    ULONG Index;

    UNREFERENCED_PARAMETER(Server);
    if (TestContext != NULL)
    {
        Index = TestContext->ServerStateCount++;
        TestContext->ServerStates[Index] = State;
        TestContext->ServerStatuses[Index] = Status;
        if (TestContext->CloseServerOnStopped && State == ZpServerStateStopped)
        {
            TestContext->ServerCloseStatus = ZpServer_Close(Server);
        }
    }
}

static
VOID
NTAPI
SDKTest_ServerConnectionCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_CONNECTION_PHASE Phase,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(Connection);
    UNREFERENCED_PARAMETER(Phase);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Context);
}

TEST_FUNC(SDKContract)
{
    WCHAR Host[] = L"127.0.0.1", ServerName[] = L"server.example", ClientKeyName[] = L"ClientKey";
    WCHAR ListenerHost[] = L"::";
    BYTE RootCertificate[] = { 0x30, 0x01, 0x00 };
    ZP_MODULE_RECORD Modules[] = { { 1, 1, 0 }, { 2, 1, 1 } };
    ZP_ENDPOINT Endpoint = { ZpTransportQuic, Host, 443, ServerName, NULL };
    ZP_LISTENER_ENDPOINT Listener = { ZpTransportQuic, ListenerHost, 443, NULL };
    ZP_SERVER_DEPLOYMENT InvalidDeployment = { L"server.example", NULL };
    ZP_CLIENT_CONFIG ClientConfig = {
        sizeof(ZP_CLIENT_CONFIG),
        &Endpoint,
        1,
        RootCertificate,
        sizeof(RootCertificate),
        ClientKeyName,
        Modules,
        ARRAYSIZE(Modules),
        0,
        SDKTest_ClientStateCallback,
        NULL
    };
    ZP_SERVER_CONFIG ServerConfig = {
        sizeof(ZP_SERVER_CONFIG),
        &Listener,
        1,
        NULL,
        0,
        Modules,
        ARRAYSIZE(Modules),
        SDKTest_ServerStateCallback,
        SDKTest_ServerConnectionCallback,
        NULL
    };
    ZP_CLIENT_HANDLE Client;
    ZP_SERVER_HANDLE Server;
    PZP_CLIENT_OBJECT ClientObject;
    PZP_SERVER_OBJECT ServerObject;
    SDK_TEST_CONTEXT TestContext = { STATUS_SUCCESS };
    QUIC_ADDR QuicAddress;
    QUIC_STATUS QuicStatus;

    TEST_OK(ZpTransportQuic == 1 && ZpTransportTlsTcp == 2 && ZpTransportWss == 3);
    TEST_OK(Endpoint.Transport == ZpTransportQuic &&
            Endpoint.Port == 443 &&
            wcscmp(Endpoint.ServerName, L"server.example") == 0);
    TEST_OK(Listener.Transport == ZpTransportQuic &&
            wcscmp(Listener.Host, L"::") == 0 &&
            Listener.Port == 443);
    TEST_OK(ClientConfig.Size == sizeof(ZP_CLIENT_CONFIG));
    TEST_OK(ServerConfig.Size == sizeof(ZP_SERVER_CONFIG));
    TEST_OK(sizeof(ZP_CLIENT_HANDLE) == sizeof(PVOID));
    TEST_OK(sizeof(ZP_SERVER_HANDLE) == sizeof(PVOID));
    TEST_OK(sizeof(ZP_CONNECTION_HANDLE) == sizeof(PVOID));
    TEST_OK(ZP_CLIENT_DEFAULT_CONNECT_TIMEOUT_MILLISECONDS == 10000);
    TEST_OK(ZP_CLIENT_DEFAULT_RETRY_MAX_MILLISECONDS == 60000);
    TEST_OK(ZP_CLIENT_DEFAULT_STABLE_RESET_MILLISECONDS == 60000);
    TEST_OK(ZP_CLIENT_DEFAULT_RETRY_JITTER_PERCENT == 20);
    TEST_OK(ZpQuicAlpn.Length == sizeof(ZP_QUIC_ALPN) - sizeof(ANSI_NULL));
    TEST_OK(ZpQuic_StatusToNtStatus(QUIC_STATUS_CONNECTION_TIMEOUT) == STATUS_IO_TIMEOUT &&
            ZpQuic_StatusToNtStatus(QUIC_STATUS_INVALID_PARAMETER) == STATUS_INVALID_PARAMETER);
    TEST_OK(SDKTest_AuthenticationRoundTrip());
    QuicStatus = KNSoftQuicInitialize();
    TEST_OK(QUIC_SUCCEEDED(QuicStatus));
    if (QUIC_SUCCEEDED(QuicStatus))
    {
        TEST_OK(NT_SUCCESS(ZpQuic_ResolveAddress(L"127.0.0.1", 443, &QuicAddress)) &&
                QuicAddress.si_family == QUIC_ADDRESS_FAMILY_INET &&
                QuicAddrGetPort(&QuicAddress) == 443);
        KNSoftQuicUninitialize();
    }

    TEST_OK(NT_SUCCESS(ZpClient_Create(&ClientConfig, &Client)));
    ClientObject = (PZP_CLIENT_OBJECT)Client;
    Host[0] = L'X';
    ServerName[0] = L'X';
    ClientKeyName[0] = L'X';
    RootCertificate[0] = 0;
    Modules[0].ModuleVersion = 2;
    TEST_OK(ClientObject->State == ZpClientStateStopped);
    TEST_OK(ClientObject->Config.ConnectTimeoutMilliseconds ==
            ZP_CLIENT_DEFAULT_CONNECT_TIMEOUT_MILLISECONDS);
    TEST_OK(wcscmp(ClientObject->Config.Endpoints[0].Host, L"127.0.0.1") == 0 &&
            wcscmp(ClientObject->Config.Endpoints[0].ServerName, L"server.example") == 0);
    TEST_OK(wcscmp(ClientObject->Config.ClientKeyName, L"ClientKey") == 0);
    TEST_OK(ClientObject->Config.DeploymentRootCertificate[0] == 0x30);
    TEST_OK(ClientObject->Config.Modules[0].ModuleVersion == 1);
    TEST_OK(ClientObject->TransportOperations != NULL &&
            ClientObject->TransportContext == &ClientObject->QuicTransport);
    ClientObject->State = ZpClientStateConnecting;
    TEST_OK(ZpClient_Close(Client) == STATUS_DEVICE_BUSY);
    ClientObject->State = ZpClientStateStopped;
    TEST_OK(NT_SUCCESS(ZpClient_Close(Client)));

    Host[0] = L'1';
    ServerName[0] = L's';
    ClientKeyName[0] = L'C';
    RootCertificate[0] = 0x30;
    Modules[0].ModuleVersion = 1;
    ClientConfig.Size = 0;
    TEST_OK(ZpClient_Create(&ClientConfig, &Client) == STATUS_INVALID_PARAMETER);
    ClientConfig.Size = sizeof(ClientConfig);
    Endpoint.WssPath = L"/invalid";
    TEST_OK(ZpClient_Create(&ClientConfig, &Client) == STATUS_INVALID_PARAMETER);
    Endpoint.WssPath = NULL;
    Modules[1].ModuleId = Modules[0].ModuleId;
    TEST_OK(ZpClient_Create(&ClientConfig, &Client) == STATUS_INVALID_PARAMETER);
    Modules[1].ModuleId = 2;

    TEST_OK(NT_SUCCESS(ZpServer_Create(&ServerConfig, &Server)));
    ServerObject = (PZP_SERVER_OBJECT)Server;
    ListenerHost[0] = L'X';
    Modules[0].ModuleVersion = 2;
    TEST_OK(ServerObject->State == ZpServerStateStopped);
    TEST_OK(wcscmp(ServerObject->Config.Listeners[0].Host, L"::") == 0);
    TEST_OK(ServerObject->Config.Modules[0].ModuleVersion == 1);
    ServerObject->State = ZpServerStateRunning;
    TEST_OK(ZpServer_Close(Server) == STATUS_DEVICE_BUSY);
    ServerObject->State = ZpServerStateStopped;
    TEST_OK(NT_SUCCESS(ZpServer_Close(Server)));

    ListenerHost[0] = L':';
    Modules[0].ModuleVersion = 1;
    Listener.WssPath = L"/invalid";
    TEST_OK(ZpServer_Create(&ServerConfig, &Server) == STATUS_INVALID_PARAMETER);
    Listener.WssPath = NULL;
    ServerConfig.Deployments = &InvalidDeployment;
    ServerConfig.DeploymentCount = 1;
    TEST_OK(ZpServer_Create(&ServerConfig, &Server) == STATUS_INVALID_PARAMETER);

    ServerConfig.Deployments = NULL;
    ServerConfig.DeploymentCount = 0;
    Endpoint.Transport = ZpTransportTlsTcp;
    ClientConfig.CallbackContext = &TestContext;
    TEST_OK(NT_SUCCESS(ZpClient_Create(&ClientConfig, &Client)));
    ClientObject = (PZP_CLIENT_OBJECT)Client;
    TEST_OK(ZpClient_Start(Client) == STATUS_NOT_SUPPORTED &&
            ClientObject->State == ZpClientStateStopped);
    TEST_OK(NT_SUCCESS(ZpClient_SetTransport(Client, &SDKTest_TransportOperations, &TestContext)));
    TEST_OK(NT_SUCCESS(ZpClient_Start(Client)) &&
            ClientObject->State == ZpClientStateConnecting &&
            TestContext.StartCount == 1 &&
            TestContext.ClientStateCount == 1 &&
            TestContext.ClientStates[0] == ZpClientStateConnecting);
    TEST_OK(ZpClient_Start(Client) == STATUS_INVALID_DEVICE_STATE);
    TEST_OK(NT_SUCCESS(ZpClient_NotifyState(Client,
                                           ZpClientStateAuthenticating,
                                           STATUS_SUCCESS)) &&
            NT_SUCCESS(ZpClient_NotifyState(Client, ZpClientStateReady, STATUS_SUCCESS)) &&
            TestContext.ClientStateCount == 3 &&
            TestContext.ClientStates[1] == ZpClientStateAuthenticating &&
            TestContext.ClientStates[2] == ZpClientStateReady);
    TEST_OK(ZpClient_NotifyState(Client,
                                ZpClientStateAuthenticating,
                                STATUS_SUCCESS) == STATUS_INVALID_DEVICE_STATE);
    TEST_OK(NT_SUCCESS(ZpClient_Stop(Client)) &&
            ClientObject->State == ZpClientStateStopping &&
            TestContext.StopCount == 1 &&
            TestContext.ClientStates[3] == ZpClientStateStopping);
    TEST_OK(NT_SUCCESS(ZpClient_Stop(Client)) && TestContext.StopCount == 1);
    TEST_OK(ZpClient_Close(Client) == STATUS_DEVICE_BUSY);
    TestContext.CloseClientOnStopped = TRUE;
    TEST_OK(NT_SUCCESS(ZpClient_NotifyState(Client, ZpClientStateStopped, STATUS_SUCCESS)) &&
            TestContext.ClientStates[4] == ZpClientStateStopped &&
            TestContext.ClientCloseStatus == STATUS_DEVICE_BUSY);
    TEST_OK(NT_SUCCESS(ZpClient_Close(Client)));

    RtlZeroMemory(&TestContext, sizeof(TestContext));
    TestContext.StartStatus = STATUS_ACCESS_DENIED;
    ClientConfig.CallbackContext = &TestContext;
    TEST_OK(NT_SUCCESS(ZpClient_Create(&ClientConfig, &Client)));
    TEST_OK(NT_SUCCESS(ZpClient_SetTransport(Client, &SDKTest_TransportOperations, &TestContext)));
    TEST_OK(ZpClient_Start(Client) == STATUS_ACCESS_DENIED &&
            ((PZP_CLIENT_OBJECT)Client)->State == ZpClientStateStopped &&
            TestContext.ClientStateCount == 2 &&
            TestContext.ClientStates[1] == ZpClientStateStopped &&
            TestContext.ClientStatuses[1] == STATUS_ACCESS_DENIED);
    TEST_OK(NT_SUCCESS(ZpClient_Close(Client)));

    RtlZeroMemory(&TestContext, sizeof(TestContext));
    ServerConfig.CallbackContext = &TestContext;
    TEST_OK(NT_SUCCESS(ZpServer_Create(&ServerConfig, &Server)));
    ServerObject = (PZP_SERVER_OBJECT)Server;
    ServerObject->Config.DeploymentCount = 1;
    TEST_OK(ZpServer_Start(Server) == STATUS_NOT_SUPPORTED &&
            ServerObject->State == ZpServerStateStopped);
    TEST_OK(NT_SUCCESS(ZpServer_SetTransport(Server, &SDKTest_TransportOperations, &TestContext)));
    TEST_OK(NT_SUCCESS(ZpServer_Start(Server)) &&
            ServerObject->State == ZpServerStateStarting &&
            TestContext.StartCount == 1 &&
            TestContext.ServerStateCount == 1 &&
            TestContext.ServerStates[0] == ZpServerStateStarting);
    TEST_OK(NT_SUCCESS(ZpServer_NotifyState(Server, ZpServerStateRunning, STATUS_SUCCESS)) &&
            TestContext.ServerStates[1] == ZpServerStateRunning);
    TEST_OK(NT_SUCCESS(ZpServer_Stop(Server)) &&
            ServerObject->State == ZpServerStateStopping &&
            TestContext.StopCount == 1 &&
            TestContext.ServerStates[2] == ZpServerStateStopping);
    TEST_OK(NT_SUCCESS(ZpServer_Stop(Server)) && TestContext.StopCount == 1);
    TestContext.CloseServerOnStopped = TRUE;
    TEST_OK(NT_SUCCESS(ZpServer_NotifyState(Server, ZpServerStateStopped, STATUS_SUCCESS)) &&
            TestContext.ServerStates[3] == ZpServerStateStopped &&
            TestContext.ServerCloseStatus == STATUS_DEVICE_BUSY);
    ServerObject->Config.DeploymentCount = 0;
    TEST_OK(NT_SUCCESS(ZpServer_Close(Server)));
}
