#include "UnitTest.h"

#include "../Network/Connection.h"
#include "../Network/Udp.h"

typedef struct _CONNECTION_TEST_CONTEXT
{
    ULONG Count;
    ZP_MESSAGE_TYPE MessageTypes[8];
    ZP_CONNECTION_STATE States[8];
    const BYTE* ExpectedPayload;
    ULONG ExpectedPayloadLength;
    LOGICAL PayloadMatched;
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
    ZP_RESPONSE_VIEW Response;
    ULONG Index;

    Index = TestContext->Count++;
    TestContext->MessageTypes[Index] = Frame->MessageType;
    TestContext->States[Index] = Connection->State;
    if (Frame->MessageType == ZpMessageResponse &&
        NT_SUCCESS(ZpMessage_DecodeResponse(Frame->Body,
                                            Frame->BodyLength,
                                            &Response)))
    {
        TestContext->PayloadMatched =
            Response.Payload.Length == TestContext->ExpectedPayloadLength &&
            RtlCompareMemory(Response.Payload.Buffer,
                             TestContext->ExpectedPayload,
                             Response.Payload.Length) == Response.Payload.Length;
    }
    return TestContext->CallbackStatus;
}

TEST_FUNC(NetworkConnection)
{
    const ZP_MODULE_VERSION Modules[] = { { 1, 1 } };
    ZP_CLIENT_HELLO ClientHello = {
        ZP_PROTOCOL_REVISION, Modules, (BYTE)RTL_NUMBER_OF(Modules), NULL
    };
    ZP_READY Ready = { Modules, (BYTE)RTL_NUMBER_OF(Modules) };
    BYTE PublicKey[ZP_CLIENT_PUBLIC_KEY_SIZE] = { 0x04 };
    BYTE Challenge[ZP_SERVER_CHALLENGE_SIZE] = { 0 };
    BYTE Signature[ZP_CLIENT_SIGNATURE_SIZE] = { 0 };
    BYTE ChannelWindowBody[16];
    BYTE HelloBody[128], ReadyBody[16], PolicyBody[ZP_CONNECTION_POLICY_WIRE_SIZE], HelloFrame[160];
    BYTE ChallengeFrame[64], AuthenticateFrame[96], ReadyFrame[32], PolicyFrame[16];
    BYTE ChannelWindowFrame[32];
    BYTE InvalidPrefix[sizeof(ULONG)] = { 0 };
    BYTE MaximumPrefix[sizeof(ULONG)] = { 0, 0, 0, 1 };
    static BYTE CompressionPayload[8192], CompressionBody[sizeof(CompressionPayload) + 16];
    static BYTE CompressionFrame[sizeof(CompressionBody) + 16], RandomBody[sizeof(CompressionBody)];
    BYTE ResponseHeader[ZP_RESPONSE_HEADER_WIRE_SIZE];
    CONNECTION_TEST_CONTEXT Context = { 0 };
    ZP_RESPONSE CompressionResponse = {
        1, { ZpStatusNone, 0 }, CompressionPayload, sizeof(CompressionPayload)
    };
    ZP_SEND_MESSAGE SendMessage;
    ZP_SEND_BUFFER SendBuffer = { 0 };
    ZP_FRAME_VIEW Frame;
    ZP_RESPONSE_VIEW Response;
    ZP_CONNECTION_POLICY Policy = { ZpPerformanceClass1, ZpPerformanceClass5 };
    ZP_UDP_CONNECTION UdpConnection = { 0 };
    ZP_STATUS UdpCloseStatus = ZpStatus_FromCode(ZpStatusWinsock, WSAECONNRESET), UdpTickStatus;
    ZP_NETWORK_STATISTICS Statistics;
    ZP_CONNECTION Connection;
    ULONG BodyLength, HelloFrameLength, ChallengeFrameLength, AuthenticateFrameLength;
    ULONG ReadyFrameLength, ChannelWindowFrameLength;
    ULONG PolicyFrameLength, CompressionBodyLength, CompressionFrameLength, Index, RandomValue = 1;
    ULONG BytesConsumed;

    TEST_OK(NT_SUCCESS(RtlInitializeCriticalSectionEx(
                &UdpConnection.Lock,
                0,
                RTL_CRITICAL_SECTION_FLAG_NO_DEBUG_INFO)));
    TEST_OK(ZpStatus_IsSuccess(ZpUdpConnection_ProcessDatagram(
                &UdpConnection,
                InvalidPrefix,
                sizeof(InvalidPrefix))) &&
            !UdpConnection.Closed);
    ZpUdpConnection_Close(&UdpConnection, UdpCloseStatus);
    ZpUdpConnection_Close(&UdpConnection, ZpStatus_FromNtStatus(STATUS_SUCCESS));
    UdpTickStatus = ZpUdpConnection_Tick(&UdpConnection, GetTickCount64());
    TEST_OK(UdpTickStatus.Type == UdpCloseStatus.Type && UdpTickStatus.Code == UdpCloseStatus.Code);
    RtlDeleteCriticalSection(&UdpConnection.Lock);

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
    TEST_OK(NT_SUCCESS(ZpMessage_EncodeConnectionPolicy(&Policy,
                                                        PolicyBody,
                                                        sizeof(PolicyBody),
                                                        &BodyLength)) &&
            NT_SUCCESS(ZpFrame_Encode(ZpMessageConnectionPolicy,
                                      PolicyBody,
                                      BodyLength,
                                      PolicyFrame,
                                      sizeof(PolicyFrame),
                                      &PolicyFrameLength)));
    TEST_OK(NT_SUCCESS(ZpMessage_EncodeChannelWindow(2,
                                                     ZP_CHANNEL_DATA_MAX_SIZE,
                                                     ChannelWindowBody,
                                                     sizeof(ChannelWindowBody),
                                                     &BodyLength)));
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(ZpMessageChannelWindow,
                                     ChannelWindowBody,
                                     BodyLength,
                                     ChannelWindowFrame,
                                     sizeof(ChannelWindowFrame),
                                     &ChannelWindowFrameLength)));

    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                              ZpConnectionRoleClient,
                                              ConnectionTest_MessageCallback,
                                              &Context)) &&
            Connection.State == ZpConnectionStateClientSendHello);
    TEST_OK(NT_SUCCESS(ZpConnection_SetPolicy(&Connection, &Policy)));
    ZpConnection_QueryStatistics(&Connection, &Statistics);
    TEST_OK(Statistics.Policy.SpeedClass == Policy.SpeedClass &&
            Statistics.Policy.LatencyClass == Policy.LatencyClass);
    TEST_OK(ZpConnection_Receive(&Connection,
                                 ChallengeFrame,
                                 ChallengeFrameLength) == STATUS_PROTOCOL_UNREACHABLE &&
            Connection.State == ZpConnectionStateClosed &&
            Context.Count == 0);
    ZpConnection_Uninitialize(&Connection);

    RtlFillMemory(CompressionPayload, sizeof(CompressionPayload), 'A');
    TEST_OK(NT_SUCCESS(ZpMessage_EncodeResponse(&CompressionResponse,
                                                CompressionBody,
                                                sizeof(CompressionBody),
                                                &CompressionBodyLength)) &&
            NT_SUCCESS(ZpMessage_EncodeResponseHeader(&CompressionResponse, ResponseHeader)));
    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                              ZpConnectionRoleClient,
                                              ConnectionTest_MessageCallback,
                                              &Context)));
    TEST_OK(NT_SUCCESS(ZpConnection_PrepareSend(&Connection,
                ZP_SEND_FLAG_COMPRESSIBLE,
                ZpMessageResponse,
                ResponseHeader,
                sizeof(ResponseHeader),
                CompressionPayload,
                sizeof(CompressionPayload),
                &SendMessage)) &&
            SendMessage.MessageType ==
                (ZP_MESSAGE_TYPE)(ZpMessageResponse | ZP_MESSAGE_FLAG_COMPRESSED) &&
            SendMessage.Buffer.Allocation == NULL && SendMessage.CompressionLockHeld &&
            SendMessage.BodyLength < CompressionBodyLength);
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(SendMessage.MessageType,
                                     SendMessage.Body,
                                     SendMessage.BodyLength,
                                     CompressionFrame,
                                     sizeof(CompressionFrame),
                                     &CompressionFrameLength)));
    ZpConnection_ReleaseSend(&Connection, &SendMessage);
    ZpConnection_Uninitialize(&Connection);
    RtlZeroMemory(&Context, sizeof(Context));
    Context.ExpectedPayload = CompressionPayload;
    Context.ExpectedPayloadLength = sizeof(CompressionPayload);
    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                              ZpConnectionRoleClient,
                                              ConnectionTest_MessageCallback,
                                              &Context)));
    Connection.State = ZpConnectionStateReady;
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, CompressionFrame, 7)) &&
            Context.Count == 0);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection,
                                           CompressionFrame + 7,
                                           CompressionFrameLength - 7)) &&
            Context.Count == 1 &&
            Context.MessageTypes[0] == ZpMessageResponse &&
            Context.PayloadMatched);
    ZpConnection_Uninitialize(&Connection);

    for (Index = 0; Index < sizeof(RandomBody); Index++)
    {
        RandomValue = RandomValue * 1664525 + 1013904223;
        RandomBody[Index] = (BYTE)(RandomValue >> 24);
    }
    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                              ZpConnectionRoleClient,
                                              ConnectionTest_MessageCallback,
                                              &Context)));
    TEST_OK(NT_SUCCESS(ZpConnection_PrepareSend(&Connection,
                ZP_SEND_FLAG_COMPRESSIBLE,
                ZpMessageResponse,
                RandomBody,
                sizeof(RandomBody),
                NULL,
                0,
                &SendMessage)) &&
            SendMessage.MessageType == ZpMessageResponse &&
            SendMessage.Body == RandomBody &&
            SendMessage.Buffer.Allocation == NULL && !SendMessage.CompressionLockHeld);
    ZpConnection_ReleaseSend(&Connection, &SendMessage);
    ZpConnection_Uninitialize(&Connection);

    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                               ZpConnectionRoleClient,
                                               ConnectionTest_MessageCallback,
                                               &Context)) &&
            NT_SUCCESS(ZpConnection_EncodeFrame(&Connection,
                                                0,
                                                ZpMessageResponse,
                                                ResponseHeader,
                                                sizeof(ResponseHeader),
                                                CompressionPayload,
                                                sizeof(CompressionPayload),
                                                16,
                                                &SendBuffer)) &&
            SendBuffer.Offset == 16 &&
            NT_SUCCESS(ZpFrame_Decode(SendBuffer.Allocation + SendBuffer.Offset,
                                      SendBuffer.Length,
                                      &Frame,
                                      &BytesConsumed)) &&
            BytesConsumed == SendBuffer.Length &&
            NT_SUCCESS(ZpMessage_DecodeResponse(Frame.Body, Frame.BodyLength, &Response)) &&
            Response.Payload.Length == sizeof(CompressionPayload) &&
            RtlCompareMemory(Response.Payload.Buffer,
                             CompressionPayload,
                             sizeof(CompressionPayload)) == sizeof(CompressionPayload));
    ZpSendBuffer_Release(&SendBuffer);
    TEST_OK(NT_SUCCESS(ZpConnection_ReserveSend(&Connection, 1024)) &&
            ZpConnection_ReserveSend(&Connection,
                                     (ULONG)ZP_CONNECTION_MAX_OUTSTANDING_SEND_BYTES) ==
                STATUS_QUOTA_EXCEEDED);
    ZpConnection_QueryStatistics(&Connection, &Statistics);
    TEST_OK(Statistics.OutstandingSendBytes == 1024 &&
            Statistics.MaximumOutstandingSendBytes == 1024 &&
            Statistics.RejectedSends == 1);
    ZpConnection_CompleteSend(&Connection, 1024);
    ZpConnection_Uninitialize(&Connection);

    RtlZeroMemory(&Context, sizeof(Context));
    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                              ZpConnectionRoleClient,
                                              ConnectionTest_MessageCallback,
                                              &Context)));
    TEST_OK(ZpConnection_Receive(&Connection, NULL, 1) == STATUS_INVALID_PARAMETER &&
            Connection.State == ZpConnectionStateClientSendHello);
    TEST_OK(ZpConnection_NotifyMessageSent(&Connection, ZpMessageClientHello, 1) == STATUS_SUCCESS &&
            Connection.State == ZpConnectionStateClientWaitChallenge);
    TEST_OK(ZpConnection_NotifyMessageSent(&Connection,
                                           ZpMessageClientAuthenticate,
                                           1) == STATUS_INVALID_DEVICE_STATE);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, ChallengeFrame, 2)) && Context.Count == 0);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, ChallengeFrame + 2, 2)) && Context.Count == 0);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection,
                                           ChallengeFrame + sizeof(ULONG),
                                           ChallengeFrameLength - sizeof(ULONG))) &&
            Context.Count == 1 &&
            Context.MessageTypes[0] == ZpMessageServerChallenge &&
            Context.States[0] == ZpConnectionStateClientSendAuthenticate);
    TEST_OK(NT_SUCCESS(ZpConnection_NotifyMessageSent(&Connection,
                                                      ZpMessageClientAuthenticate,
                                                      1)) &&
            Connection.State == ZpConnectionStateClientWaitReady);

    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, ReadyFrame, ReadyFrameLength)) &&
            Context.Count == 2 &&
            Context.MessageTypes[1] == ZpMessageReady &&
            Context.States[1] == ZpConnectionStateReady &&
            Connection.State == ZpConnectionStateReady);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, PolicyFrame, PolicyFrameLength)) &&
            Context.Count == 3 &&
            Context.MessageTypes[2] == ZpMessageConnectionPolicy &&
            Context.States[2] == ZpConnectionStateReady);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection,
                                           ChannelWindowFrame,
                                           ChannelWindowFrameLength)) &&
            Context.Count == 4 &&
            Context.MessageTypes[3] == ZpMessageChannelWindow &&
            Context.States[3] == ZpConnectionStateReady);
    TEST_OK(NT_SUCCESS(ZpConnection_NotifyMessageSent(&Connection, ZpMessageChannelWindow, 1)));
    TEST_OK(ZpConnection_NotifyMessageSent(&Connection,
                                           ZpMessageReady,
                                           1) == STATUS_INVALID_DEVICE_STATE);
    ZpConnection_Uninitialize(&Connection);

    RtlZeroMemory(&Context, sizeof(Context));
    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                              ZpConnectionRoleServer,
                                              ConnectionTest_MessageCallback,
                                              &Context)) &&
            Connection.State == ZpConnectionStateServerWaitHello);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, HelloFrame, 3)) && Context.Count == 0);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection,
                                           HelloFrame + 3,
                                           HelloFrameLength - 3)) &&
            Context.Count == 1 &&
            Context.MessageTypes[0] == ZpMessageClientHello &&
            Context.States[0] == ZpConnectionStateServerSendChallenge &&
            Connection.ReceiveBuffer != NULL &&
            Connection.ReceiveBufferLength == 0 &&
            Connection.ReceiveFrameSize == 0);
    TEST_OK(NT_SUCCESS(ZpConnection_NotifyMessageSent(&Connection,
                                                      ZpMessageServerChallenge,
                                                      1)) &&
            Connection.State == ZpConnectionStateServerWaitAuthenticate);
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection,
                                           AuthenticateFrame,
                                           AuthenticateFrameLength)) &&
            Context.Count == 2 &&
            Context.MessageTypes[1] == ZpMessageClientAuthenticate &&
            Context.States[1] == ZpConnectionStateServerSendReady);
    TEST_OK(NT_SUCCESS(ZpConnection_NotifyMessageSent(&Connection, ZpMessageReady, 1)) &&
            Connection.State == ZpConnectionStateReady);
    ZpConnection_Uninitialize(&Connection);
    TEST_OK(ZpConnection_Receive(&Connection, ReadyFrame, ReadyFrameLength) == STATUS_INVALID_DEVICE_STATE);

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
            Connection.State == ZpConnectionStateClosed);
    ZpConnection_Uninitialize(&Connection);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.CallbackStatus = STATUS_ACCESS_DENIED;
    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                              ZpConnectionRoleServer,
                                              ConnectionTest_MessageCallback,
                                              &Context)));
    TEST_OK(ZpConnection_Receive(&Connection, HelloFrame, HelloFrameLength) == STATUS_ACCESS_DENIED &&
            Connection.State == ZpConnectionStateClosed &&
            Context.Count == 1);
    ZpConnection_Uninitialize(&Connection);

    RtlZeroMemory(&Context, sizeof(Context));
    Context.CallbackStatus = STATUS_ACCESS_DENIED;
    TEST_OK(NT_SUCCESS(ZpConnection_Initialize(&Connection,
                                               ZpConnectionRoleServer,
                                               ConnectionTest_MessageCallback,
                                               &Context)));
    Connection.State = ZpConnectionStateReady;
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, PolicyFrame, PolicyFrameLength)) &&
            Connection.State == ZpConnectionStateReady && Context.Count == 1);
    Context.CallbackStatus = STATUS_SUCCESS;
    PolicyFrame[sizeof(ULONG)] = MAXBYTE;
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, PolicyFrame, PolicyFrameLength)) &&
            Connection.State == ZpConnectionStateReady && Context.Count == 1);
    PolicyFrame[sizeof(ULONG)] = ZpMessageConnectionPolicy;
    TEST_OK(NT_SUCCESS(ZpConnection_Receive(&Connection, PolicyFrame, PolicyFrameLength)) &&
            Connection.State == ZpConnectionStateReady && Context.Count == 2);
    ZpConnection_Uninitialize(&Connection);
}
