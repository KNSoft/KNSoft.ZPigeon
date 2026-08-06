#include <KNSoft/NDK/NDK.h>
#include <KNSoft/NDK/Package/UnitTest.inl>

TEST_DECL_FUNC(ProtocolCodec);
TEST_DECL_FUNC(ProtocolFrame);

CONST UNITTEST_ENTRY UnitTestList[] = {
    TEST_DECL_ENTRY(ProtocolCodec),
    TEST_DECL_ENTRY(ProtocolFrame),
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
