#include "UnitTest.h"

#include "../Network/Connection.h"

typedef struct _CONNECTION_TEST_CONTEXT
{
    ULONG Count;
    ZP_MESSAGE_TYPE MessageTypes[8];
    ZP_CONNECTION_STATE States[8];
    NTSTATUS CallbackStatus;
} CONNECTION_TEST_CONTEXT, *PCONNECTION_TEST_CONTEXT;

static
NTSTATUS
NTAPI
ConnectionTest_MessageCallback(
    _Inout_ PZP_CONNECTION Connection,
    _In_ const ZP_FRAME_VIEW* Frame,
    _In_opt_ PVOID Context)
{
    PCONNECTION_TEST_CONTEXT TestContext = Context;
    ULONG Index;

    Index = TestContext->Count++;
    TestContext->MessageTypes[Index] = Frame->MessageType;
    TestContext->States[Index] = ZpConnection_GetState(Connection);
    return TestContext->CallbackStatus;
}

TEST_FUNC(NetworkConnection)
{
    ZP_CLIENT_HELLO ClientHello = { ZP_CORE_VERSION };
    ZP_READY Ready = { 0 };
    ZP_DISCONNECT Disconnect = { STATUS_ACCESS_DENIED };
    BYTE PublicKey[ZP_CLIENT_PUBLIC_KEY_SIZE] = { 0x04 };
    BYTE Challenge[ZP_SERVER_CHALLENGE_SIZE] = { 0 };
    BYTE Signature[ZP_CLIENT_SIGNATURE_SIZE] = { 0 };
    BYTE PingBody[sizeof(ULONGLONG)] = { 1 };
    BYTE HelloBody[128], ReadyBody[16], DisconnectBody[32], HelloFrame[160];
    BYTE ChallengeFrame[64], AuthenticateFrame[96], ReadyFrame[32], PingFrame[32], DisconnectFrame[64];
    BYTE CoalescedFrames[64], InvalidPrefix[sizeof(ULONG)] = { 0 };
    BYTE MaximumPrefix[sizeof(ULONG)] = { 0, 0, 0, 1 };
    CONNECTION_TEST_CONTEXT Context = { 0 };
    ZP_CONNECTION Connection;
    ULONG BodyLength, FrameLength, HelloFrameLength, ChallengeFrameLength, AuthenticateFrameLength;
    ULONG ReadyFrameLength, PingFrameLength, DisconnectFrameLength;

    ClientHello.ClientPublicKey = PublicKey;
    TEST_OK(NT_SUCCESS(ZpMessage_EncodeClientHello(&ClientHello,
                                                   HelloBody,
                                                   sizeof(HelloBody),
                                                   &BodyLength)));
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(ZpMessageClientHello,
                                     HelloBody,
                                     BodyLength,
                                     HelloFrame,
                                     sizeof(HelloFrame),
                                     &HelloFrameLength)));
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(ZpMessageServerChallenge,
                                     Challenge,
                                     sizeof(Challenge),
                                     ChallengeFrame,
                                     sizeof(ChallengeFrame),
                                     &ChallengeFrameLength)));
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(ZpMessageClientAuthenticate,
                                     Signature,
                                     sizeof(Signature),
                                     AuthenticateFrame,
                                     sizeof(AuthenticateFrame),
                                     &AuthenticateFrameLength)));
    TEST_OK(NT_SUCCESS(ZpMessage_EncodeReady(&Ready, ReadyBody, sizeof(ReadyBody), &BodyLength)));
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(ZpMessageReady,
                                     ReadyBody,
                                     BodyLength,
                                     ReadyFrame,
                                     sizeof(ReadyFrame),
                                     &ReadyFrameLength)));
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(ZpMessagePing,
                                     PingBody,
                                     sizeof(PingBody),
                                     PingFrame,
                                     sizeof(PingFrame),
                                     &PingFrameLength)));
    TEST_OK(NT_SUCCESS(ZpMessage_EncodeDisconnect(&Disconnect,
                                                  DisconnectBody,
                                                  sizeof(DisconnectBody),
                                                  &BodyLength)));
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(ZpMessageDisconnect,
                                     DisconnectBody,
                                     BodyLength,
                                     DisconnectFrame,
                                     sizeof(DisconnectFrame),
                                     &DisconnectFrameLength)));

    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                              ZpConnectionRoleClient,
                                              ConnectionTest_MessageCallback,
                                              &Context)) &&
            ZpConnection_GetState(&Connection) == ZpConnectionStateClientSendHello);
    TEST_OK(ZpConnection_Receive(&Connection,
                                 ChallengeFrame,
                                 ChallengeFrameLength) == STATUS_PROTOCOL_UNREACHABLE &&
            ZpConnection_GetState(&Connection) == ZpConnectionStateClosed &&
            Context.Count == 0);
    ZpConnection_Uninitialize(&Connection);

    RtlZeroMemory(&Context, sizeof(Context));
    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                              ZpConnectionRoleClient,
                                              ConnectionTest_MessageCallback,
                                              &Context)));
    TEST_OK(ZpConnection_Receive(&Connection, NULL, 1) == STATUS_INVALID_PARAMETER &&
            ZpConnection_GetState(&Connection) == ZpConnectionStateClientSendHello);
    TEST_OK(ZpConnection_NotifyMessageSent(&Connection, ZpMessageClientHello) == STATUS_SUCCESS &&
            ZpConnection_GetState(&Connection) == ZpConnectionStateClientWaitChallenge);
    TEST_OK(ZpConnection_NotifyMessageSent(&Connection,
                                           ZpMessageClientAuthenticate) == STATUS_INVALID_DEVICE_STATE);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, ChallengeFrame, 2)) && Context.Count == 0);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, ChallengeFrame + 2, 2)) && Context.Count == 0);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection,
                                           ChallengeFrame + sizeof(ULONG),
                                           ChallengeFrameLength - sizeof(ULONG))) &&
            Context.Count == 1 &&
            Context.MessageTypes[0] == ZpMessageServerChallenge &&
            Context.States[0] == ZpConnectionStateClientSendAuthenticate);
    TEST_OK(NT_SUCCESS(ZpConnection_NotifyMessageSent(&Connection, ZpMessageClientAuthenticate)) &&
            ZpConnection_GetState(&Connection) == ZpConnectionStateClientWaitReady);

    RtlCopyMemory(CoalescedFrames, ReadyFrame, ReadyFrameLength);
    RtlCopyMemory(CoalescedFrames + ReadyFrameLength, PingFrame, PingFrameLength);
    FrameLength = ReadyFrameLength + PingFrameLength;
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, CoalescedFrames, FrameLength)) &&
            Context.Count == 3 &&
            Context.MessageTypes[1] == ZpMessageReady &&
            Context.States[1] == ZpConnectionStateReady &&
            Context.MessageTypes[2] == ZpMessagePing &&
            Context.States[2] == ZpConnectionStateReady &&
            ZpConnection_GetState(&Connection) == ZpConnectionStateReady);
    TEST_OK(NT_SUCCESS(ZpConnection_NotifyMessageSent(&Connection, ZpMessagePong)));
    TEST_OK(ZpConnection_NotifyMessageSent(&Connection, ZpMessageReady) == STATUS_INVALID_DEVICE_STATE);
    ZpConnection_Uninitialize(&Connection);

    RtlZeroMemory(&Context, sizeof(Context));
    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                              ZpConnectionRoleServer,
                                              ConnectionTest_MessageCallback,
                                              &Context)) &&
            ZpConnection_GetState(&Connection) == ZpConnectionStateServerWaitHello);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, HelloFrame, 3)) && Context.Count == 0);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection,
                                           HelloFrame + 3,
                                           HelloFrameLength - 3)) &&
            Context.Count == 1 &&
            Context.MessageTypes[0] == ZpMessageClientHello &&
            Context.States[0] == ZpConnectionStateServerSendChallenge);
    TEST_OK(NT_SUCCESS(ZpConnection_NotifyMessageSent(&Connection, ZpMessageServerChallenge)) &&
            ZpConnection_GetState(&Connection) == ZpConnectionStateServerWaitAuthenticate);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection,
                                           AuthenticateFrame,
                                           AuthenticateFrameLength)) &&
            Context.Count == 2 &&
            Context.MessageTypes[1] == ZpMessageClientAuthenticate &&
            Context.States[1] == ZpConnectionStateServerSendReady);
    TEST_OK(NT_SUCCESS(ZpConnection_NotifyMessageSent(&Connection, ZpMessageReady)) &&
            ZpConnection_GetState(&Connection) == ZpConnectionStateReady);
    RtlCopyMemory(CoalescedFrames, DisconnectFrame, DisconnectFrameLength);
    RtlCopyMemory(CoalescedFrames + DisconnectFrameLength, PingFrame, PingFrameLength);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection,
                                           CoalescedFrames,
                                           DisconnectFrameLength + PingFrameLength)) &&
            Context.Count == 3 &&
            Context.MessageTypes[2] == ZpMessageDisconnect &&
            Context.States[2] == ZpConnectionStateClosed);
    TEST_OK(ZpConnection_Receive(&Connection, PingFrame, PingFrameLength) == STATUS_INVALID_DEVICE_STATE);
    ZpConnection_Uninitialize(&Connection);

    RtlZeroMemory(&Context, sizeof(Context));
    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                              ZpConnectionRoleServer,
                                              ConnectionTest_MessageCallback,
                                              &Context)));
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, MaximumPrefix, sizeof(MaximumPrefix))) &&
            Connection.ReceiveFrameSize == sizeof(ULONG) + ZP_FRAME_MAX_BODY_SIZE &&
            Connection.ReceiveBufferSize == ZP_CONNECTION_INITIAL_RECEIVE_BUFFER_SIZE);
    ZpConnection_Uninitialize(&Connection);

    RtlZeroMemory(&Context, sizeof(Context));
    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                              ZpConnectionRoleServer,
                                              ConnectionTest_MessageCallback,
                                              &Context)));
    TEST_OK(ZpConnection_Receive(&Connection,
                                 InvalidPrefix,
                                 sizeof(InvalidPrefix)) == STATUS_DATA_ERROR &&
            ZpConnection_GetState(&Connection) == ZpConnectionStateClosed);
    ZpConnection_Uninitialize(&Connection);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.CallbackStatus = STATUS_ACCESS_DENIED;
    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                              ZpConnectionRoleServer,
                                              ConnectionTest_MessageCallback,
                                              &Context)));
    TEST_OK(ZpConnection_Receive(&Connection, HelloFrame, HelloFrameLength) == STATUS_ACCESS_DENIED &&
            ZpConnection_GetState(&Connection) == ZpConnectionStateClosed &&
            Context.Count == 1);
    ZpConnection_Uninitialize(&Connection);
}
