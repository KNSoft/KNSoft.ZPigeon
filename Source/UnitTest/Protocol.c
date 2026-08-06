#include "UnitTest.h"

#include <KNSoft/ZPigeon/Protocol.h>

TEST_FUNC(ProtocolCodec)
{
    static const BYTE ByteString[] = { 1, 2, 3 };
    BYTE Buffer[64], InvalidBoolean = 2, TruncatedByteString[] = { 3, 0, 0, 0, 1, 2 };
    BYTE ByteValue;
    USHORT UInt16Value;
    ULONG UInt32Value;
    ULONGLONG UInt64Value;
    BOOLEAN BooleanValue;
    ZP_BUFFER_VIEW BufferView;
    ZP_STRING_VIEW StringView;
    ZP_CODEC_WRITER Writer;
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeWriter(&Writer, NULL, 0);
    TEST_OK(NT_SUCCESS(ZpCodec_WriteByte(&Writer, 0x5A)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt16(&Writer, 0x1234)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt32(&Writer, 0x89ABCDEF)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt64(&Writer, 0x0123456789ABCDEFULL)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteBoolean(&Writer, TRUE)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteString(&Writer, L"AZ", 2)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteByteString(&Writer, ByteString, sizeof(ByteString))));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteArrayCount(&Writer, 4)));
    TEST_OK(Writer.Offset == 35);

    ZpCodec_InitializeWriter(&Writer, Buffer, sizeof(Buffer));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteByte(&Writer, 0x5A)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt16(&Writer, 0x1234)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt32(&Writer, 0x89ABCDEF)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt64(&Writer, 0x0123456789ABCDEFULL)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteBoolean(&Writer, TRUE)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteString(&Writer, L"AZ", 2)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteByteString(&Writer, ByteString, sizeof(ByteString))));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteArrayCount(&Writer, 4)));

    ZpCodec_InitializeReader(&Reader, Buffer, Writer.Offset);
    TEST_OK(NT_SUCCESS(ZpCodec_ReadByte(&Reader, &ByteValue)) && ByteValue == 0x5A);
    TEST_OK(NT_SUCCESS(ZpCodec_ReadUInt16(&Reader, &UInt16Value)) && UInt16Value == 0x1234);
    TEST_OK(NT_SUCCESS(ZpCodec_ReadUInt32(&Reader, &UInt32Value)) && UInt32Value == 0x89ABCDEF);
    TEST_OK(NT_SUCCESS(ZpCodec_ReadUInt64(&Reader, &UInt64Value)) && UInt64Value == 0x0123456789ABCDEFULL);
    TEST_OK(NT_SUCCESS(ZpCodec_ReadBoolean(&Reader, &BooleanValue)) && BooleanValue == TRUE);
    TEST_OK(NT_SUCCESS(ZpCodec_ReadString(&Reader, &StringView)) && StringView.Length == 2 &&
            StringView.Buffer[0] == 'A' && StringView.Buffer[1] == 0 &&
            StringView.Buffer[2] == 'Z' && StringView.Buffer[3] == 0);
    TEST_OK(NT_SUCCESS(ZpCodec_ReadByteString(&Reader, &BufferView)) &&
            BufferView.Length == sizeof(ByteString) &&
            RtlCompareMemory(BufferView.Buffer, ByteString, sizeof(ByteString)) == sizeof(ByteString));
    TEST_OK(NT_SUCCESS(ZpCodec_ReadArrayCount(&Reader, &UInt32Value)) && UInt32Value == 4);
    TEST_OK(Reader.Offset == Reader.Size);

    ZpCodec_InitializeReader(&Reader, &InvalidBoolean, sizeof(InvalidBoolean));
    TEST_OK(ZpCodec_ReadBoolean(&Reader, &BooleanValue) == STATUS_DATA_ERROR);

    ZpCodec_InitializeReader(&Reader, TruncatedByteString, sizeof(TruncatedByteString));
    TEST_OK(ZpCodec_ReadByteString(&Reader, &BufferView) == STATUS_DATA_ERROR);

    ZpCodec_InitializeWriter(&Writer, Buffer, sizeof(ULONG));
    Status = ZpCodec_WriteByteString(&Writer, ByteString, sizeof(ByteString));
    TEST_OK(Status == STATUS_BUFFER_TOO_SMALL && Writer.Offset == 0);
    TEST_OK(ZpCodec_WriteBoolean(&Writer, 2) == STATUS_INVALID_PARAMETER);
    TEST_OK(ZpCodec_WriteArrayCount(&Writer, ZP_CODEC_MAX_ELEMENT_COUNT + 1) == STATUS_INVALID_BUFFER_SIZE);
}

TEST_FUNC(ProtocolFrame)
{
    BYTE PingBody[8], Frame[128], ClientHello[4 + ZP_CLIENT_PUBLIC_KEY_SIZE], InvalidFrame[5] = { 1, 0, 0, 0, 0xFF };
    ZP_CODEC_WRITER Writer;
    ZP_FRAME_VIEW View;
    ULONG FrameSize, BytesConsumed;
    NTSTATUS Status;

    ZpCodec_InitializeWriter(&Writer, PingBody, sizeof(PingBody));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt64(&Writer, 0x0123456789ABCDEFULL)));

    TEST_OK(NT_SUCCESS(ZpFrame_GetSize(sizeof(PingBody), &FrameSize)) && FrameSize == 13);
    TEST_OK(ZpFrame_GetSize(ZP_FRAME_MAX_BODY_SIZE, &FrameSize) == STATUS_INVALID_BUFFER_SIZE);
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(ZpMessagePing, PingBody, sizeof(PingBody), NULL, 0, &FrameSize)) &&
            FrameSize == 13);
    TEST_OK(ZpFrame_Encode(ZpMessagePing,
                          PingBody,
                          sizeof(PingBody),
                          Frame,
                          FrameSize - 1,
                          &BytesConsumed) == STATUS_BUFFER_TOO_SMALL &&
            BytesConsumed == FrameSize);
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(ZpMessagePing,
                                     PingBody,
                                     sizeof(PingBody),
                                     Frame,
                                     sizeof(Frame),
                                     &BytesConsumed)) &&
            BytesConsumed == FrameSize);
    TEST_OK(ZpFrame_Decode(Frame, 3, &View, &BytesConsumed) == STATUS_MORE_PROCESSING_REQUIRED);
    TEST_OK(ZpFrame_Decode(Frame, FrameSize - 1, &View, &BytesConsumed) == STATUS_MORE_PROCESSING_REQUIRED);
    TEST_OK(NT_SUCCESS(ZpFrame_Decode(Frame, FrameSize, &View, &BytesConsumed)) &&
            BytesConsumed == FrameSize &&
            View.MessageType == ZpMessagePing &&
            View.BodyLength == sizeof(PingBody) &&
            RtlCompareMemory(View.Body, PingBody, sizeof(PingBody)) == sizeof(PingBody));

    TEST_OK(ZpFrame_Decode(InvalidFrame, sizeof(InvalidFrame), &View, &BytesConsumed) == STATUS_DATA_ERROR);
    InvalidFrame[0] = 0;
    TEST_OK(ZpFrame_Decode(InvalidFrame, sizeof(InvalidFrame), &View, &BytesConsumed) == STATUS_DATA_ERROR);

    ZpCodec_InitializeWriter(&Writer, ClientHello, sizeof(ClientHello));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt16(&Writer, ZP_CORE_VERSION)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt16(&Writer, 0)));
    ClientHello[Writer.Offset] = 0x04;
    RtlZeroMemory(ClientHello + Writer.Offset + 1, ZP_CLIENT_PUBLIC_KEY_SIZE - 1);
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(ZpMessageClientHello,
                                     ClientHello,
                                     sizeof(ClientHello),
                                     Frame,
                                     sizeof(Frame),
                                     &FrameSize)));
    TEST_OK(NT_SUCCESS(ZpFrame_Decode(Frame, FrameSize, &View, &BytesConsumed)) &&
            View.MessageType == ZpMessageClientHello);

    ClientHello[0] = 2;
    Status = ZpFrame_Encode(ZpMessageClientHello,
                            ClientHello,
                            sizeof(ClientHello),
                            Frame,
                            sizeof(Frame),
                            &FrameSize);
    TEST_OK(Status == STATUS_REVISION_MISMATCH);
    ClientHello[0] = ZP_CORE_VERSION;
    ClientHello[4] = 0x03;
    TEST_OK(ZpFrame_Encode(ZpMessageClientHello,
                          ClientHello,
                          sizeof(ClientHello),
                          Frame,
                          sizeof(Frame),
                          &FrameSize) == STATUS_DATA_ERROR);
}
