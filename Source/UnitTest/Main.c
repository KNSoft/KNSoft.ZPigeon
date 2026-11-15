#include <KNSoft/NDK/NDK.h>
#include <KNSoft/NDK/Package/UnitTest.inl>

TEST_DECL_FUNC(ProtocolCodec);
TEST_DECL_FUNC(ProtocolFrame);
TEST_DECL_FUNC(ProtocolConnectionPolicy);
TEST_DECL_FUNC(ProtocolRtc);
TEST_DECL_FUNC(ProtocolWindow);
TEST_DECL_FUNC(WindowMediaH264);
TEST_DECL_FUNC(WindowMediaH265);
TEST_DECL_FUNC(ProtocolProcess);
TEST_DECL_FUNC(ProtocolBrowser);
TEST_DECL_FUNC(ProtocolSerial);
TEST_DECL_FUNC(ProtocolRecording);
TEST_DECL_FUNC(ProtocolPortable);
TEST_DECL_FUNC(ProtocolFileOwners);
TEST_DECL_FUNC(ProtocolFileDownload);
TEST_DECL_FUNC(ProtocolMessage);
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BROWSER_)
TEST_DECL_FUNC(BrowserJson);
TEST_DECL_FUNC(BrowserProfiles);
#endif
TEST_DECL_FUNC(NetworkConnection);
TEST_DECL_FUNC(SDKContract);
TEST_DECL_FUNC(SDKQuicIntegration);
TEST_DECL_FUNC(SDKTcpIntegration);
TEST_DECL_FUNC(SDKUdpIntegration);

CONST UNITTEST_ENTRY UnitTestList[] = {
    TEST_DECL_ENTRY(ProtocolCodec),
    TEST_DECL_ENTRY(ProtocolFrame),
    TEST_DECL_ENTRY(ProtocolConnectionPolicy),
    TEST_DECL_ENTRY(ProtocolRtc),
    TEST_DECL_ENTRY(ProtocolWindow),
    TEST_DECL_ENTRY(WindowMediaH264),
    TEST_DECL_ENTRY(WindowMediaH265),
    TEST_DECL_ENTRY(ProtocolProcess),
    TEST_DECL_ENTRY(ProtocolBrowser),
    TEST_DECL_ENTRY(ProtocolSerial),
    TEST_DECL_ENTRY(ProtocolRecording),
    TEST_DECL_ENTRY(ProtocolPortable),
    TEST_DECL_ENTRY(ProtocolFileOwners),
    TEST_DECL_ENTRY(ProtocolFileDownload),
    TEST_DECL_ENTRY(ProtocolMessage),
#if !defined(_KNSOFT_ZPIGEON_BUILD_CUSTOM_) || defined(_KNSOFT_ZPIGEON_BUILD_MODULE_BROWSER_)
    TEST_DECL_ENTRY(BrowserJson),
    TEST_DECL_ENTRY(BrowserProfiles),
#endif
    TEST_DECL_ENTRY(NetworkConnection),
    TEST_DECL_ENTRY(SDKContract),
    TEST_DECL_ENTRY(SDKQuicIntegration),
    TEST_DECL_ENTRY(SDKTcpIntegration),
    TEST_DECL_ENTRY(SDKUdpIntegration),
    { 0 }
};

int
__cdecl
wmain(
    _In_ int argc,
    _In_reads_(argc) _Pre_z_ wchar_t** argv)
{
    return UnitTest_Main(argc, argv);
}
