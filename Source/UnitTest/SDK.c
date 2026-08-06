#include "UnitTest.h"

#include <KNSoft/ZPigeon/Client.h>
#include <KNSoft/ZPigeon/Server.h>

TEST_FUNC(SDKContract)
{
    ZP_ENDPOINT Endpoint = { ZpTransportQuic, L"127.0.0.1", 443, L"server.example", NULL };
    ZP_LISTENER_ENDPOINT Listener = { ZpTransportQuic, NULL, 443, NULL };
    ZP_CLIENT_CONFIG ClientConfig = { sizeof(ZP_CLIENT_CONFIG) };
    ZP_SERVER_CONFIG ServerConfig = { sizeof(ZP_SERVER_CONFIG) };

    TEST_OK(ZpTransportQuic == 1 && ZpTransportTlsTcp == 2 && ZpTransportWss == 3);
    TEST_OK(Endpoint.Transport == ZpTransportQuic &&
            Endpoint.Port == 443 &&
            wcscmp(Endpoint.ServerName, L"server.example") == 0);
    TEST_OK(Listener.Transport == ZpTransportQuic && Listener.Host == NULL && Listener.Port == 443);
    TEST_OK(ClientConfig.Size == sizeof(ZP_CLIENT_CONFIG));
    TEST_OK(ServerConfig.Size == sizeof(ZP_SERVER_CONFIG));
    TEST_OK(sizeof(ZP_CLIENT_HANDLE) == sizeof(PVOID));
    TEST_OK(sizeof(ZP_SERVER_HANDLE) == sizeof(PVOID));
    TEST_OK(sizeof(ZP_CONNECTION_HANDLE) == sizeof(PVOID));
    TEST_OK(ZP_CLIENT_DEFAULT_CONNECT_TIMEOUT_MILLISECONDS == 10000);
    TEST_OK(ZP_CLIENT_DEFAULT_RETRY_MAX_MILLISECONDS == 60000);
    TEST_OK(ZP_CLIENT_DEFAULT_STABLE_RESET_MILLISECONDS == 60000);
    TEST_OK(ZP_CLIENT_DEFAULT_RETRY_JITTER_PERCENT == 20);
}
