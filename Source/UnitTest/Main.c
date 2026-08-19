#include <KNSoft/NDK/NDK.h>
#include <KNSoft/NDK/Package/UnitTest.inl>

TEST_DECL_FUNC(ProtocolCodec);
TEST_DECL_FUNC(ProtocolFrame);
TEST_DECL_FUNC(ProtocolMessage);
TEST_DECL_FUNC(NetworkConnection);
TEST_DECL_FUNC(SDKContract);
TEST_DECL_FUNC(SDKQuicIntegration);
TEST_DECL_FUNC(SDKTcpIntegration);
TEST_DECL_FUNC(SDKUdpIntegration);

CONST UNITTEST_ENTRY UnitTestList[] = {
    TEST_DECL_ENTRY(ProtocolCodec),
    TEST_DECL_ENTRY(ProtocolFrame),
    TEST_DECL_ENTRY(ProtocolMessage),
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
