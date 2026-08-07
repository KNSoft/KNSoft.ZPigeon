#include "UnitTest.h"

#include <KNSoft/ZPigeon/Client.h>
#include <KNSoft/ZPigeon/Server.h>

#include "../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../KNSoft.ZPigeon.Client.SDK/Network/Retry.inl"
#include "../KNSoft.ZPigeon.Server.SDK/Server.inl"
#include "../Network/Authentication.inl"
#include "../Network/Quic.inl"

typedef struct _SDK_TEST_CONTEXT
{
    NTSTATUS StartStatus;
    ULONG StartCount;
    ULONG StartEndpointIndices[8];
    ULONG StopCount;
    ULONG SendCount;
    ZP_MESSAGE_TYPE SendMessageType;
    ULONGLONG SendToken;
    ULONGLONG SendRequestId;
    USHORT SendModuleId;
    USHORT SendOperationId;
    ULONG SendPayloadLength;
    ULONGLONG SendChannelId;
    ULONG SendChannelCredit;
    ULONG SendChannelDataLength;
    NTSTATUS SendChannelStatus;
    ULONG RequestCompleteCount;
    NTSTATUS RequestStatus;
    ULONG RequestPayloadLength;
    HANDLE RequestCompleteEvent;
    ULONG FileOpenReadCount;
    NTSTATUS FileOpenReadStatus;
    ZP_CHANNEL_HANDLE FileChannel;
    ULONGLONG FileSize;
    ULONGLONG FileOffset;
    ULONG TerminalCreateCount;
    NTSTATUS TerminalCreateStatus;
    ZP_CHANNEL_HANDLE TerminalChannel;
    ULONG TerminalProcessId;
    ULONG ChannelDataCount;
    ULONG ChannelDataLength;
    ULONG ChannelWritableCount;
    ULONG ChannelWritableCredit;
    ULONG ChannelCloseCount;
    NTSTATUS ChannelCloseStatus;
    ULONG RequestStatusCount;
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
    NTSTATUS AuthorizeStatus;
    ULONG AuthorizeCount;
    ZP_REQUEST_ACCESS AuthorizedAccess;
    USHORT AuthorizedModuleId;
    USHORT AuthorizedOperationId;
    ULONG AuthorizedPayloadLength;
} SDK_TEST_CONTEXT, *PSDK_TEST_CONTEXT;

static
NTSTATUS
NTAPI
SDKTest_TransportStart(
    _In_opt_ PVOID Context,
    _In_ ULONG EndpointIndex)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    TestContext->StartEndpointIndices[TestContext->StartCount++] = EndpointIndex;
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

static
NTSTATUS
NTAPI
SDKTest_TransportSend(
    _In_opt_ PVOID Context,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PSDK_TEST_CONTEXT TestContext = Context;
    ZP_REQUEST_VIEW Request;
    ZP_CHANNEL_DATA_VIEW ChannelData;
    ZP_CHANNEL_CLOSE ChannelClose;

    TestContext->SendCount++;
    TestContext->SendMessageType = MessageType;
    if (MessageType == ZpMessagePing)
    {
        ZpMessage_DecodePing(MessageType, Body, BodyLength, &TestContext->SendToken);
    }
    else if (MessageType == ZpMessageRequest &&
             NT_SUCCESS(ZpMessage_DecodeRequest(Body, BodyLength, &Request)))
    {
        TestContext->SendRequestId = Request.RequestId;
        TestContext->SendModuleId = Request.ModuleId;
        TestContext->SendOperationId = Request.OperationId;
        TestContext->SendPayloadLength = Request.Payload.Length;
    }
    else if (MessageType == ZpMessageCancel)
    {
        ZpMessage_DecodeCancel(Body, BodyLength, &TestContext->SendRequestId);
    }
    else if (MessageType == ZpMessageChannelWindow)
    {
        ZpMessage_DecodeChannelWindow(Body,
                                      BodyLength,
                                      &TestContext->SendChannelId,
                                      &TestContext->SendChannelCredit);
    }
    else if (MessageType == ZpMessageChannelData &&
             NT_SUCCESS(ZpMessage_DecodeChannelData(Body,
                                                     BodyLength,
                                                     &ChannelData)))
    {
        TestContext->SendChannelId = ChannelData.ChannelId;
        TestContext->SendChannelDataLength = ChannelData.Data.Length;
    }
    else if (MessageType == ZpMessageChannelClose &&
             NT_SUCCESS(ZpMessage_DecodeChannelClose(Body,
                                                      BodyLength,
                                                      &ChannelClose)))
    {
        TestContext->SendChannelId = ChannelClose.ChannelId;
        TestContext->SendChannelStatus = ChannelClose.Status;
    }
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
SDKTest_RequestCompleteCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->RequestCompleteCount++;
    TestContext->RequestStatus = Status;
    TestContext->RequestPayloadLength = Payload->Length;
    if (TestContext->RequestCompleteEvent != NULL)
    {
        SetEvent(TestContext->RequestCompleteEvent);
    }
}

static
VOID
NTAPI
SDKTest_FileOpenReadCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_ ULONGLONG Offset,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->FileOpenReadCount++;
    TestContext->FileOpenReadStatus = Status;
    TestContext->FileChannel = Channel;
    TestContext->FileSize = FileSize;
    TestContext->FileOffset = Offset;
}

static
VOID
NTAPI
SDKTest_TerminalCreateCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG ProcessId,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->TerminalCreateCount++;
    TestContext->TerminalCreateStatus = Status;
    TestContext->TerminalChannel = Channel;
    TestContext->TerminalProcessId = ProcessId;
}

static
VOID
NTAPI
SDKTest_ChannelDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    TestContext->ChannelDataCount++;
    TestContext->ChannelDataLength = Data->Length;
}

static
VOID
NTAPI
SDKTest_ChannelWritableCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    TestContext->ChannelWritableCount++;
    TestContext->ChannelWritableCredit = CreditBytes;
}

static
VOID
NTAPI
SDKTest_ChannelCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    TestContext->ChannelCloseCount++;
    TestContext->ChannelCloseStatus = Status;
}

static
VOID
NTAPI
SDKTest_RequestStatusCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->RequestStatusCount++;
    TestContext->RequestStatus = Status;
}

static const ZP_TRANSPORT_OPERATIONS SDKTest_TransportOperations = {
    SDKTest_TransportStart,
    SDKTest_TransportStop,
    SDKTest_TransportSend
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

static
NTSTATUS
NTAPI
SDKTest_ServerAuthorizeCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ZP_CLIENT_ID_SIZE) const BYTE ClientId[ZP_CLIENT_ID_SIZE],
    _In_ ZP_REQUEST_ACCESS Access,
    _In_ USHORT ModuleId,
    _In_ USHORT OperationId,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PSDK_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(Connection);
    UNREFERENCED_PARAMETER(ClientId);
    TestContext->AuthorizeCount++;
    TestContext->AuthorizedAccess = Access;
    TestContext->AuthorizedModuleId = ModuleId;
    TestContext->AuthorizedOperationId = OperationId;
    TestContext->AuthorizedPayloadLength = Payload->Length;
    return TestContext->AuthorizeStatus;
}

TEST_FUNC(SDKContract)
{
    WCHAR Host[] = L"127.0.0.1", ServerName[] = L"server.example", ClientKeyName[] = L"ClientKey";
    WCHAR ListenerHost[] = L"::";
    BYTE RootCertificate[] = { 0x30, 0x01, 0x00 };
    ZP_MODULE_RECORD Modules[] = { { 1, 1, 0 }, { 2, 1, 1 } };
    ZP_ENDPOINT Endpoint = { ZpTransportQuic, Host, 443, ServerName, NULL };
    ZP_ENDPOINT MixedEndpoints[] = {
        { ZpTransportTlsTcp, Host, 443, ServerName, NULL },
        { ZpTransportQuic, Host, 443, ServerName, NULL }
    };
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
        NULL,
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
        0,
        SDKTest_ServerStateCallback,
        SDKTest_ServerConnectionCallback,
        NULL
    };
    ZP_CLIENT_HANDLE Client;
    ZP_SERVER_HANDLE Server;
    PZP_CLIENT_OBJECT ClientObject;
    PZP_SERVER_OBJECT ServerObject;
    ZP_REQUEST_HANDLE Request;
    ZP_RESPONSE_VIEW Response;
    ZP_CHANNEL_DATA_VIEW ChannelData;
    ZP_CHANNEL_CLOSE ChannelClose;
    BYTE FileOpenReadResponse[3 * sizeof(ULONGLONG)];
    ULONG FileOpenReadResponseLength;
    BYTE TerminalCreateResponse[sizeof(ULONGLONG) + sizeof(ULONG)];
    ULONG TerminalCreateResponseLength;
    BYTE TerminalInput[] = { 'e', 'x', 'i', 't' };
    BYTE TerminalTooLongInput[] = { 'e', 'x', 'i', 't', '\r' };
    ZP_BUFFER_VIEW EmptyPayload = { NULL, 0 };
    BYTE ClientId[ZP_CLIENT_ID_SIZE] = { 0 };
    SDK_TEST_CONTEXT TestContext = { STATUS_SUCCESS };
    SDK_TEST_CONTEXT TlsContext = { STATUS_ACCESS_DENIED };
    SDK_TEST_CONTEXT QuicContext = { STATUS_SUCCESS };
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
    TEST_OK(ZpClientRetry_GetBaseDelay(0) == 1000 &&
            ZpClientRetry_GetBaseDelay(1) == 2000 &&
            ZpClientRetry_GetBaseDelay(5) == 32000 &&
            ZpClientRetry_GetBaseDelay(6) == 60000 &&
            ZpClientRetry_GetBaseDelay(MAXULONG) == 60000);
    TEST_OK(ZpClientRetry_GetDelay(0, 0) == 800 &&
            ZpClientRetry_GetDelay(0, 400) == 1200);
    TEST_OK(ZpClientRetry_GetDelay(6, 0) == 48000 &&
            ZpClientRetry_GetDelay(6, 24000) == 72000);
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
    TEST_OK(ClientObject->TransportOperations[ZpTransportQuic] != NULL &&
            ClientObject->TransportContexts[ZpTransportQuic] == &ClientObject->QuicTransport);
    ClientObject->State = ZpClientStateConnecting;
    TEST_OK(ZpClient_Close(Client) == STATUS_DEVICE_BUSY);
    ClientObject->State = ZpClientStateStopped;
    TEST_OK(NT_SUCCESS(ZpClient_Close(Client)));

    Host[0] = L'1';
    ServerName[0] = L's';
    ClientKeyName[0] = L'C';
    RootCertificate[0] = 0x30;
    Modules[0].ModuleVersion = 1;
    ClientConfig.Endpoints = MixedEndpoints;
    ClientConfig.EndpointCount = ARRAYSIZE(MixedEndpoints);
    TEST_OK(NT_SUCCESS(ZpClient_Create(&ClientConfig, &Client)));
    ClientObject = (PZP_CLIENT_OBJECT)Client;
    TEST_OK(ClientObject->TransportOperations[ZpTransportQuic] != NULL &&
            ClientObject->TransportContexts[ZpTransportQuic] == &ClientObject->QuicTransport);
    TEST_OK(NT_SUCCESS(ZpClient_Close(Client)));

    ClientConfig.CallbackContext = &TlsContext;
    TEST_OK(NT_SUCCESS(ZpClient_Create(&ClientConfig, &Client)));
    ClientObject = (PZP_CLIENT_OBJECT)Client;
    TEST_OK(NT_SUCCESS(ZpClient_SetTransport(Client,
                                             ZpTransportTlsTcp,
                                             &SDKTest_TransportOperations,
                                             &TlsContext)) &&
            NT_SUCCESS(ZpClient_SetTransport(Client,
                                             ZpTransportQuic,
                                             &SDKTest_TransportOperations,
                                             &QuicContext)));
    TEST_OK(NT_SUCCESS(ZpClient_Start(Client)) &&
            TlsContext.StartCount == 1 &&
            TlsContext.StartEndpointIndices[0] == 0 &&
            QuicContext.StartCount == 1 &&
            QuicContext.StartEndpointIndices[0] == 1 &&
            ClientObject->ActiveTransport == ZpTransportQuic &&
            ClientObject->EndpointIndex == 1);
    TEST_OK(NT_SUCCESS(ZpClient_Stop(Client)) && QuicContext.StopCount == 1);
    TEST_OK(NT_SUCCESS(ZpClient_NotifyState(Client,
                                           ZpClientStateStopped,
                                           STATUS_SUCCESS)) &&
            NT_SUCCESS(ZpClient_Close(Client)));

    ClientConfig.Endpoints = &Endpoint;
    ClientConfig.EndpointCount = 1;
    ClientConfig.Size = 0;
    TEST_OK(ZpClient_Create(&ClientConfig, &Client) == STATUS_INVALID_PARAMETER);
    ClientConfig.Size = sizeof(ClientConfig);
    Endpoint.WssPath = L"/invalid";
    TEST_OK(ZpClient_Create(&ClientConfig, &Client) == STATUS_INVALID_PARAMETER);
    Endpoint.WssPath = NULL;
    Modules[1].ModuleId = Modules[0].ModuleId;
    TEST_OK(ZpClient_Create(&ClientConfig, &Client) == STATUS_INVALID_PARAMETER);
    Modules[1].ModuleId = 2;

    ServerConfig.MaxRequestsPerConnection =
        ZP_SERVER_MAX_REQUESTS_PER_CONNECTION + 1;
    TEST_OK(ZpServer_Create(&ServerConfig, &Server) == STATUS_INVALID_PARAMETER);
    ServerConfig.MaxRequestsPerConnection = 0;
    TEST_OK(NT_SUCCESS(ZpServer_Create(&ServerConfig, &Server)));
    ServerObject = (PZP_SERVER_OBJECT)Server;
    ListenerHost[0] = L'X';
    Modules[0].ModuleVersion = 2;
    TEST_OK(ServerObject->State == ZpServerStateStopped);
    TEST_OK(ServerObject->Config.MaxRequestsPerConnection ==
            ZP_SERVER_DEFAULT_MAX_REQUESTS_PER_CONNECTION);
    TEST_OK(wcscmp(ServerObject->Config.Listeners[0].Host, L"::") == 0);
    TEST_OK(ServerObject->Config.Modules[0].ModuleVersion == 1);
    TEST_OK(NT_SUCCESS(ZpServer_AuthorizeRequest(
                Server,
                (ZP_CONNECTION_HANDLE)(ULONG_PTR)1,
                ClientId,
                ZpRequestAccessRead,
                1,
                1,
                &EmptyPayload)) &&
            ZpServer_AuthorizeRequest(
                Server,
                (ZP_CONNECTION_HANDLE)(ULONG_PTR)1,
                ClientId,
                ZpRequestAccessControl,
                2,
                3,
                &EmptyPayload) == STATUS_ACCESS_DENIED);
    TestContext.AuthorizeStatus = STATUS_PRIVILEGE_NOT_HELD;
    ServerObject->Config.AuthorizeCallback = SDKTest_ServerAuthorizeCallback;
    ServerObject->Config.CallbackContext = &TestContext;
    TEST_OK(ZpServer_AuthorizeRequest(
                Server,
                (ZP_CONNECTION_HANDLE)(ULONG_PTR)1,
                ClientId,
                ZpRequestAccessControl,
                2,
                3,
                &EmptyPayload) == STATUS_PRIVILEGE_NOT_HELD &&
            TestContext.AuthorizeCount == 1 &&
            TestContext.AuthorizedAccess == ZpRequestAccessControl &&
            TestContext.AuthorizedModuleId == 2 &&
            TestContext.AuthorizedOperationId == 3 &&
            TestContext.AuthorizedPayloadLength == 0);
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
    TEST_OK(NT_SUCCESS(ZpClient_SetTransport(Client,
                                             ZpTransportTlsTcp,
                                             &SDKTest_TransportOperations,
                                             &TestContext)));
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
    TEST_OK(NT_SUCCESS(ZpClient_Ping(Client, 0x0102030405060708)) &&
            TestContext.SendCount == 1 &&
            TestContext.SendMessageType == ZpMessagePing &&
            TestContext.SendToken == 0x0102030405060708);
    TEST_OK(NT_SUCCESS(ZpClient_SendRequest(Client,
                                            1,
                                            2,
                                            1000,
                                            NULL,
                                            0,
                                            SDKTest_RequestCompleteCallback,
                                            &TestContext,
                                            &Request)) &&
            TestContext.SendMessageType == ZpMessageRequest &&
            TestContext.SendRequestId != 0);
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = STATUS_SUCCESS;
    Response.Payload.Buffer = RootCertificate;
    Response.Payload.Length = sizeof(RootCertificate);
    TEST_OK(NT_SUCCESS(ZpClient_CompleteResponse(Client, &Response)) &&
            TestContext.RequestCompleteCount == 1 &&
            TestContext.RequestStatus == STATUS_SUCCESS &&
            TestContext.RequestPayloadLength == sizeof(RootCertificate));
    ZpRequest_Close(Request);
    TEST_OK(NT_SUCCESS(ZpClient_OpenFileRead(Client,
                                             L"C:\\Test.bin",
                                             11,
                                             16,
                                             1000,
                                             SDKTest_FileOpenReadCallback,
                                             SDKTest_ChannelDataCallback,
                                             SDKTest_ChannelCloseCallback,
                                             &TestContext,
                                             &Request)) &&
            TestContext.SendMessageType == ZpMessageRequest);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeOpenReadResponse(2,
                                                     16 + sizeof(RootCertificate),
                                                     16,
                                                     FileOpenReadResponse,
                                                     sizeof(FileOpenReadResponse),
                                                     &FileOpenReadResponseLength)));
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = STATUS_SUCCESS;
    Response.Payload.Buffer = FileOpenReadResponse;
    Response.Payload.Length = FileOpenReadResponseLength;
    TEST_OK(NT_SUCCESS(ZpClient_CompleteResponse(Client, &Response)) &&
            TestContext.FileOpenReadCount == 1 &&
            TestContext.FileOpenReadStatus == STATUS_SUCCESS &&
            TestContext.FileChannel != NULL &&
            TestContext.FileSize == 16 + sizeof(RootCertificate) &&
            TestContext.FileOffset == 16 &&
            TestContext.SendMessageType == ZpMessageChannelWindow &&
            TestContext.SendChannelId == 2 &&
            TestContext.SendChannelCredit == ZP_CLIENT_DEFAULT_CHANNEL_WINDOW_SIZE);
    ZpRequest_Close(Request);
    ChannelData.ChannelId = 2;
    ChannelData.Data.Buffer = RootCertificate;
    ChannelData.Data.Length = sizeof(RootCertificate);
    TEST_OK(NT_SUCCESS(ZpClient_ReceiveChannelData(Client, &ChannelData)) &&
            TestContext.ChannelDataCount == 1 &&
            TestContext.ChannelDataLength == sizeof(RootCertificate) &&
            TestContext.SendMessageType == ZpMessageChannelWindow &&
            TestContext.SendChannelId == 2 &&
            TestContext.SendChannelCredit ==
                ZP_CLIENT_DEFAULT_CHANNEL_WINDOW_SIZE);
    ChannelClose.ChannelId = 2;
    ChannelClose.Status = STATUS_SUCCESS;
    TEST_OK(NT_SUCCESS(ZpClient_ReceiveChannelClose(Client, &ChannelClose)) &&
            TestContext.ChannelCloseCount == 1 &&
            TestContext.ChannelCloseStatus == STATUS_SUCCESS);
    ZpChannel_Close(TestContext.FileChannel);
    TestContext.FileChannel = NULL;
    TEST_OK(NT_SUCCESS(ZpClient_OpenFileRead(Client,
                                             L"C:\\Test.bin",
                                             11,
                                             0,
                                             1000,
                                             SDKTest_FileOpenReadCallback,
                                             SDKTest_ChannelDataCallback,
                                             SDKTest_ChannelCloseCallback,
                                             &TestContext,
                                             &Request)));
    TEST_OK(NT_SUCCESS(ZpFile_EncodeOpenReadResponse(4,
                                                     64,
                                                     0,
                                                     FileOpenReadResponse,
                                                     sizeof(FileOpenReadResponse),
                                                     &FileOpenReadResponseLength)));
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = STATUS_SUCCESS;
    Response.Payload.Buffer = FileOpenReadResponse;
    Response.Payload.Length = FileOpenReadResponseLength;
    TEST_OK(NT_SUCCESS(ZpClient_CompleteResponse(Client, &Response)) &&
            TestContext.FileOpenReadCount == 2 &&
            TestContext.FileChannel != NULL &&
            TestContext.SendMessageType == ZpMessageChannelWindow &&
            TestContext.SendChannelId == 4);
    ZpRequest_Close(Request);
    TEST_OK(NT_SUCCESS(ZpChannel_Cancel(TestContext.FileChannel)) &&
            TestContext.SendMessageType == ZpMessageChannelClose &&
            TestContext.SendChannelId == 4 &&
            TestContext.SendChannelStatus == STATUS_CANCELLED &&
            TestContext.ChannelCloseCount == 2 &&
            TestContext.ChannelCloseStatus == STATUS_CANCELLED);
    TEST_OK(ZpChannel_Cancel(TestContext.FileChannel) == STATUS_INVALID_DEVICE_STATE);
    ChannelClose.ChannelId = 4;
    ChannelClose.Status = STATUS_SUCCESS;
    TEST_OK(NT_SUCCESS(ZpClient_ReceiveChannelClose(Client, &ChannelClose)) &&
            TestContext.ChannelCloseCount == 2);
    ZpChannel_Close(TestContext.FileChannel);
    TestContext.FileChannel = NULL;
    TEST_OK(NT_SUCCESS(ZpClient_CreateTerminal(Client,
                                                120,
                                                30,
                                                L"cmd.exe",
                                                7,
                                                NULL,
                                                0,
                                                1000,
                                                SDKTest_TerminalCreateCallback,
                                                SDKTest_ChannelDataCallback,
                                                SDKTest_ChannelWritableCallback,
                                                SDKTest_ChannelCloseCallback,
                                                &TestContext,
                                                &Request)) &&
            TestContext.SendMessageType == ZpMessageRequest &&
            TestContext.SendModuleId == ZP_TERMINAL_MODULE_ID &&
            TestContext.SendOperationId == ZP_TERMINAL_OPERATION_CREATE &&
            TestContext.SendPayloadLength != 0);
    TEST_OK(NT_SUCCESS(ZpTerminal_EncodeCreateResponse(
                           6,
                           1234,
                           TerminalCreateResponse,
                           sizeof(TerminalCreateResponse),
                           &TerminalCreateResponseLength)));
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = STATUS_SUCCESS;
    Response.Payload.Buffer = TerminalCreateResponse;
    Response.Payload.Length = TerminalCreateResponseLength;
    TEST_OK(NT_SUCCESS(ZpClient_CompleteResponse(Client, &Response)) &&
            TestContext.TerminalCreateCount == 1 &&
            TestContext.TerminalCreateStatus == STATUS_SUCCESS &&
            TestContext.TerminalChannel != NULL &&
            TestContext.TerminalProcessId == 1234 &&
            TestContext.SendMessageType == ZpMessageChannelWindow &&
            TestContext.SendChannelId == 6 &&
            TestContext.SendChannelCredit ==
                ZP_CLIENT_DEFAULT_CHANNEL_WINDOW_SIZE);
    ZpRequest_Close(Request);
    TEST_OK(ZpChannel_Send(TestContext.TerminalChannel,
                           TerminalInput,
                           sizeof(TerminalInput)) == STATUS_RETRY);
    TEST_OK(NT_SUCCESS(ZpClient_ReceiveChannelWindow(Client,
                                                     6,
                                                     sizeof(TerminalInput))) &&
            TestContext.ChannelWritableCount == 1 &&
            TestContext.ChannelWritableCredit == sizeof(TerminalInput));
    TEST_OK(ZpChannel_Send(TestContext.TerminalChannel,
                           TerminalTooLongInput,
                           sizeof(TerminalTooLongInput)) == STATUS_RETRY);
    TEST_OK(NT_SUCCESS(ZpChannel_Send(TestContext.TerminalChannel,
                                     TerminalInput,
                                     sizeof(TerminalInput))) &&
            TestContext.SendMessageType == ZpMessageChannelData &&
            TestContext.SendChannelId == 6 &&
            TestContext.SendChannelDataLength == sizeof(TerminalInput));
    ChannelData.ChannelId = 6;
    ChannelData.Data.Buffer = TerminalInput;
    ChannelData.Data.Length = sizeof(TerminalInput);
    TEST_OK(NT_SUCCESS(ZpClient_ReceiveChannelData(Client, &ChannelData)) &&
            TestContext.ChannelDataCount == 2 &&
            TestContext.ChannelDataLength == sizeof(TerminalInput) &&
            TestContext.SendMessageType == ZpMessageChannelWindow &&
            TestContext.SendChannelId == 6 &&
            TestContext.SendChannelCredit == sizeof(TerminalInput));
    TEST_OK(NT_SUCCESS(ZpClient_ResizeTerminal(TestContext.TerminalChannel,
                                               132,
                                               40,
                                               1000,
                                               SDKTest_RequestStatusCallback,
                                               &TestContext,
                                               &Request)) &&
            TestContext.SendMessageType == ZpMessageRequest &&
            TestContext.SendModuleId == ZP_TERMINAL_MODULE_ID &&
            TestContext.SendOperationId == ZP_TERMINAL_OPERATION_RESIZE);
    Response.RequestId = TestContext.SendRequestId;
    Response.Status = STATUS_SUCCESS;
    Response.Payload = EmptyPayload;
    TEST_OK(NT_SUCCESS(ZpClient_CompleteResponse(Client, &Response)) &&
            TestContext.RequestStatusCount == 1 &&
            TestContext.RequestStatus == STATUS_SUCCESS);
    ZpRequest_Close(Request);
    ChannelClose.ChannelId = 6;
    ChannelClose.Status = STATUS_SUCCESS;
    TEST_OK(NT_SUCCESS(ZpClient_ReceiveChannelClose(Client, &ChannelClose)) &&
            TestContext.ChannelCloseCount == 3 &&
            TestContext.ChannelCloseStatus == STATUS_SUCCESS);
    ZpChannel_Close(TestContext.TerminalChannel);
    TestContext.TerminalChannel = NULL;
    TEST_OK(NT_SUCCESS(ZpClient_SendRequest(Client,
                                             1,
                                            2,
                                            0,
                                            NULL,
                                            0,
                                            SDKTest_RequestCompleteCallback,
                                            &TestContext,
                                            &Request)) &&
            NT_SUCCESS(ZpRequest_Cancel(Request)) &&
            TestContext.SendMessageType == ZpMessageCancel &&
            TestContext.RequestCompleteCount == 2 &&
            TestContext.RequestStatus == STATUS_CANCELLED &&
            TestContext.RequestPayloadLength == 0);
    ZpRequest_Close(Request);
    TestContext.RequestCompleteEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    TEST_OK(TestContext.RequestCompleteEvent != NULL &&
            NT_SUCCESS(ZpClient_SendRequest(Client,
                                            1,
                                            2,
                                            10,
                                            NULL,
                                            0,
                                            SDKTest_RequestCompleteCallback,
                                            &TestContext,
                                            &Request)) &&
            WaitForSingleObject(TestContext.RequestCompleteEvent, 1000) == WAIT_OBJECT_0 &&
            TestContext.SendMessageType == ZpMessageCancel &&
            TestContext.RequestCompleteCount == 3 &&
            TestContext.RequestStatus == STATUS_IO_TIMEOUT &&
            TestContext.RequestPayloadLength == 0);
    ZpRequest_Close(Request);
    CloseHandle(TestContext.RequestCompleteEvent);
    TestContext.RequestCompleteEvent = NULL;
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
    WaitForThreadpoolTimerCallbacks(ClientObject->RequestTimer, FALSE);
    TEST_OK(NT_SUCCESS(ZpClient_Close(Client)));

    RtlZeroMemory(&TestContext, sizeof(TestContext));
    TestContext.StartStatus = STATUS_ACCESS_DENIED;
    ClientConfig.CallbackContext = &TestContext;
    TEST_OK(NT_SUCCESS(ZpClient_Create(&ClientConfig, &Client)));
    TEST_OK(NT_SUCCESS(ZpClient_SetTransport(Client,
                                             ZpTransportTlsTcp,
                                             &SDKTest_TransportOperations,
                                             &TestContext)));
    ClientObject = (PZP_CLIENT_OBJECT)Client;
    TEST_OK(NT_SUCCESS(ZpClient_Start(Client)) &&
            ClientObject->State == ZpClientStateRetryWait &&
            ClientObject->FailureRound == 1 &&
            ClientObject->RetryPending &&
            TestContext.ClientStateCount == 2 &&
            TestContext.ClientStates[1] == ZpClientStateRetryWait &&
            TestContext.ClientStatuses[1] == STATUS_ACCESS_DENIED);
    TEST_OK(NT_SUCCESS(ZpClient_Stop(Client)) && TestContext.StopCount == 1);
    TEST_OK(NT_SUCCESS(ZpClient_NotifyState(Client,
                                           ZpClientStateStopped,
                                           STATUS_SUCCESS)));
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
