#include "UnitTest.h"

#include <KNSoft/ZPigeon/Protocol.h>

TEST_FUNC(ProtocolMessage)
{
    ZP_MODULE_RECORD Modules[] = {
        { 1, 1, 0x01020304 },
        { 3, 2, 0xA0B0C0D0 }
    };
    BYTE PublicKey[ZP_CLIENT_PUBLIC_KEY_SIZE] = { 0x04 };
    BYTE Challenge[ZP_SERVER_CHALLENGE_SIZE], Signature[ZP_CLIENT_SIGNATURE_SIZE], Buffer[256];
    ZP_CLIENT_HELLO ClientHello = { ZP_CORE_VERSION, Modules, ARRAYSIZE(Modules), PublicKey };
    ZP_CLIENT_HELLO_VIEW ClientHelloView;
    ZP_READY Ready = { Modules, ARRAYSIZE(Modules) };
    ZP_READY_VIEW ReadyView;
    ZP_DISCONNECT Disconnect = { STATUS_ACCESS_DENIED, L"Denied", 6 };
    ZP_DISCONNECT_VIEW DisconnectView;
    ZP_MODULE_RECORD Module;
    ZP_BUFFER_VIEW BufferView;
    ULONG Length, Index;

    for (Index = 0; Index < ARRAYSIZE(Challenge); Index++)
    {
        Challenge[Index] = (BYTE)Index;
    }
    for (Index = 0; Index < ARRAYSIZE(Signature); Index++)
    {
        Signature[Index] = (BYTE)(ARRAYSIZE(Signature) - Index);
    }

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeClientHello(&ClientHello, NULL, 0, &Length)) && Length == 85);
    TEST_OK(ZpMessage_EncodeClientHello(&ClientHello, Buffer, Length - 1, &Index) == STATUS_BUFFER_TOO_SMALL &&
            Index == Length);
    TEST_OK(NT_SUCCESS(ZpMessage_EncodeClientHello(&ClientHello, Buffer, sizeof(Buffer), &Length)) && Length == 85);
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeClientHello(Buffer, Length, &ClientHelloView)) &&
            ClientHelloView.CoreVersion == ZP_CORE_VERSION &&
            ClientHelloView.Modules.Count == ARRAYSIZE(Modules) &&
            RtlCompareMemory(ClientHelloView.ClientPublicKey,
                             PublicKey,
                             sizeof(PublicKey)) == sizeof(PublicKey));
    TEST_OK(NT_SUCCESS(ZpMessage_GetModuleRecord(&ClientHelloView.Modules, 0, &Module)) &&
            Module.ModuleId == Modules[0].ModuleId &&
            Module.ModuleVersion == Modules[0].ModuleVersion &&
            Module.Capabilities == Modules[0].Capabilities);
    TEST_OK(NT_SUCCESS(ZpMessage_GetModuleRecord(&ClientHelloView.Modules, 1, &Module)) &&
            Module.ModuleId == Modules[1].ModuleId &&
            Module.ModuleVersion == Modules[1].ModuleVersion &&
            Module.Capabilities == Modules[1].Capabilities);
    TEST_OK(ZpMessage_GetModuleRecord(&ClientHelloView.Modules,
                                      ClientHelloView.Modules.Count,
                                      &Module) == STATUS_INVALID_PARAMETER);

    Modules[1].ModuleId = Modules[0].ModuleId;
    TEST_OK(ZpMessage_EncodeClientHello(&ClientHello, NULL, 0, &Length) == STATUS_INVALID_PARAMETER);
    Modules[1].ModuleId = 3;
    PublicKey[0] = 0x03;
    TEST_OK(ZpMessage_EncodeClientHello(&ClientHello, NULL, 0, &Length) == STATUS_INVALID_PARAMETER);
    PublicKey[0] = 0x04;
    ClientHello.CoreVersion++;
    TEST_OK(ZpMessage_EncodeClientHello(&ClientHello, NULL, 0, &Length) == STATUS_REVISION_MISMATCH);
    ClientHello.CoreVersion = ZP_CORE_VERSION;

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeServerChallenge(Challenge, Buffer, sizeof(Buffer), &Length)) &&
            Length == sizeof(Challenge));
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeServerChallenge(Buffer, Length, &BufferView)) &&
            BufferView.Length == sizeof(Challenge) &&
            RtlCompareMemory(BufferView.Buffer, Challenge, sizeof(Challenge)) == sizeof(Challenge));
    TEST_OK(ZpMessage_DecodeServerChallenge(Buffer, Length - 1, &BufferView) == STATUS_DATA_ERROR);

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeClientAuthenticate(Signature, Buffer, sizeof(Buffer), &Length)) &&
            Length == sizeof(Signature));
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeClientAuthenticate(Buffer, Length, &BufferView)) &&
            BufferView.Length == sizeof(Signature) &&
            RtlCompareMemory(BufferView.Buffer, Signature, sizeof(Signature)) == sizeof(Signature));

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeReady(&Ready, Buffer, sizeof(Buffer), &Length)) && Length == 18);
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeReady(Buffer, Length, &ReadyView)) &&
            ReadyView.Modules.Count == ARRAYSIZE(Modules));
    TEST_OK(NT_SUCCESS(ZpMessage_GetModuleRecord(&ReadyView.Modules, 1, &Module)) &&
            Module.ModuleId == Modules[1].ModuleId);

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeDisconnect(&Disconnect, Buffer, sizeof(Buffer), &Length)) && Length == 20);
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeDisconnect(Buffer, Length, &DisconnectView)) &&
            DisconnectView.Status == STATUS_ACCESS_DENIED &&
            DisconnectView.Reason.Length == Disconnect.ReasonLength &&
            RtlCompareMemory(DisconnectView.Reason.Buffer,
                             Disconnect.Reason,
                             Disconnect.ReasonLength * sizeof(WCHAR)) == Disconnect.ReasonLength * sizeof(WCHAR));
    TEST_OK(ZpMessage_DecodeDisconnect(Buffer, Length - 1, &DisconnectView) == STATUS_DATA_ERROR);
    Disconnect.ReasonLength = ZP_CODEC_MAX_ELEMENT_COUNT + 1;
    TEST_OK(ZpMessage_EncodeDisconnect(&Disconnect, NULL, 0, &Length) == STATUS_INVALID_PARAMETER);
}
