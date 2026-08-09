#include <KNSoft/ZPigeon/Client.h>
#include <KNSoft/ZPigeon/EventLog.h>
#include <KNSoft/ZPigeon/File.h>
#include <KNSoft/ZPigeon/Operations.h>
#include <KNSoft/ZPigeon/Process.h>
#include <KNSoft/ZPigeon/Protocol.h>
#include <KNSoft/ZPigeon/Registry.h>
#include <KNSoft/ZPigeon/SDK.h>
#include <KNSoft/ZPigeon/Server.h>
#include <KNSoft/ZPigeon/Service.h>
#include <KNSoft/ZPigeon/System.h>
#include <KNSoft/ZPigeon/Terminal.h>

int
ZpConsumer_CheckCppHeaders(
    void);

int
main(
    void)
{
    ZP_CLIENT_CONFIG ClientConfig = { sizeof(ClientConfig) };
    ZP_SERVER_CONFIG ServerConfig = { sizeof(ServerConfig) };
    ZP_CLIENT_HANDLE Client = NULL;
    ZP_SERVER_HANDLE Server = NULL;
    ULONG FrameSize;

    if (!NT_SUCCESS(ZpFrame_GetSize(0, &FrameSize)) ||
        FrameSize == 0 ||
        ZpClient_Create(&ClientConfig, &Client) != STATUS_INVALID_PARAMETER ||
        Client != NULL ||
        ZpServer_Create(&ServerConfig, &Server) != STATUS_INVALID_PARAMETER ||
        Server != NULL ||
        !ZpConsumer_CheckCppHeaders())
    {
        return 1;
    }
    return 0;
}
