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

extern "C"
int
ZpConsumer_CheckCppHeaders(
    void)
{
    ULONG FrameSize;

    static_assert(ZP_CORE_VERSION == 1);
    return NT_SUCCESS(ZpFrame_GetSize(0, &FrameSize)) &&
           FrameSize != 0;
}
