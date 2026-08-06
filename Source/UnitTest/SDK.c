#include "UnitTest.h"

#include <KNSoft/ZPigeon/Client.h>
#include <KNSoft/ZPigeon/Server.h>

#include "../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../KNSoft.ZPigeon.Server.SDK/Server.inl"

static
VOID
NTAPI
SDKTest_ClientStateCallback(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Client);
    UNREFERENCED_PARAMETER(State);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Context);
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
    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(State);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Context);
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
}
