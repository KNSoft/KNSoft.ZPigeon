#include "Protocol.inl"

static
LOGICAL
ZpFrame_IsMessageTypeValid(
    _In_ ZP_MESSAGE_TYPE MessageType)
{
    return (MessageType >= ZpMessageClientHello && MessageType <= ZpMessageDisconnect) ||
           (MessageType >= ZpMessageRequest && MessageType <= ZpMessagePong);
}

static
NTSTATUS
ZpFrame_ValidateModuleRecords(
    _In_reads_bytes_(BodyLength) const BYTE* Body,
    _In_ ULONG BodyLength,
    _In_ ULONG HeaderLength,
    _In_ LOGICAL HasClientPublicKey)
{
    ULONG Count, ExpectedLength, Index, Offset;
    USHORT ModuleId, PreviousModuleId = 0;

    Count = ZpReadUInt16(Body + HeaderLength - sizeof(USHORT));
    if (Count > ZP_MODULE_MAX_COUNT)
    {
        return STATUS_DATA_ERROR;
    }
    ExpectedLength = HeaderLength + Count * 8 + (HasClientPublicKey ? ZP_CLIENT_PUBLIC_KEY_SIZE : 0);
    if (BodyLength != ExpectedLength)
    {
        return STATUS_DATA_ERROR;
    }
    Offset = HeaderLength;
    for (Index = 0; Index < Count; Index++)
    {
        ModuleId = ZpReadUInt16(Body + Offset);
        if (ModuleId == 0 || ModuleId <= PreviousModuleId || ZpReadUInt16(Body + Offset + sizeof(USHORT)) == 0)
        {
            return STATUS_DATA_ERROR;
        }
        PreviousModuleId = ModuleId;
        Offset += 8;
    }
    if (HasClientPublicKey && Body[Offset] != 0x04)
    {
        return STATUS_DATA_ERROR;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpFrame_ValidateBody(
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const BYTE* Body,
    _In_ ULONG BodyLength)
{
    ULONG Length;

    if (!ZpFrame_IsMessageTypeValid(MessageType))
    {
        return STATUS_DATA_ERROR;
    }

    switch (MessageType)
    {
        case ZpMessageClientHello:
            if (BodyLength < 4 + ZP_CLIENT_PUBLIC_KEY_SIZE)
            {
                return STATUS_DATA_ERROR;
            }
            if (ZpReadUInt16(Body) != ZP_CORE_VERSION)
            {
                return STATUS_REVISION_MISMATCH;
            }
            return ZpFrame_ValidateModuleRecords(Body, BodyLength, 4, TRUE);

        case ZpMessageServerChallenge:
            return BodyLength == ZP_SERVER_CHALLENGE_SIZE ? STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessageClientAuthenticate:
            return BodyLength == ZP_CLIENT_SIGNATURE_SIZE ? STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessageReady:
            if (BodyLength < sizeof(USHORT))
            {
                return STATUS_DATA_ERROR;
            }
            return ZpFrame_ValidateModuleRecords(Body, BodyLength, sizeof(USHORT), FALSE);

        case ZpMessageDisconnect:
            if (BodyLength < sizeof(NTSTATUS) + sizeof(ULONG))
            {
                return STATUS_DATA_ERROR;
            }
            Length = ZpReadUInt32(Body + sizeof(NTSTATUS));
            if (Length > ZP_CODEC_MAX_ELEMENT_COUNT ||
                BodyLength != sizeof(NTSTATUS) + sizeof(ULONG) + Length * sizeof(WCHAR))
            {
                return STATUS_DATA_ERROR;
            }
            return STATUS_SUCCESS;

        case ZpMessageRequest:
            if (BodyLength < 16 || ZpReadUInt64(Body) == 0 || ZpReadUInt16(Body + 8) == 0 ||
                ZpReadUInt16(Body + 10) == 0)
            {
                return STATUS_DATA_ERROR;
            }
            return STATUS_SUCCESS;

        case ZpMessageResponse:
            return BodyLength >= 12 && ZpReadUInt64(Body) != 0 ? STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessageCancel:
            return BodyLength == 8 && ZpReadUInt64(Body) != 0 ? STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessageEvent:
            if (BodyLength < 12 || ZpReadUInt64(Body) == 0 || ZpReadUInt16(Body + 8) == 0 ||
                ZpReadUInt16(Body + 10) == 0)
            {
                return STATUS_DATA_ERROR;
            }
            return STATUS_SUCCESS;

        case ZpMessageChannelData:
            return BodyLength > 8 && BodyLength <= 8 + ZP_CHANNEL_DATA_MAX_SIZE && ZpReadUInt64(Body) != 0 ?
                       STATUS_SUCCESS :
                       STATUS_DATA_ERROR;

        case ZpMessageChannelClose:
            return BodyLength == 12 && ZpReadUInt64(Body) != 0 ? STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessagePing:
        case ZpMessagePong:
            return BodyLength == 8 ? STATUS_SUCCESS : STATUS_DATA_ERROR;
    }
    return STATUS_DATA_ERROR;
}

NTSTATUS
ZpFrame_GetSize(
    _In_ ULONG MessageBodyLength,
    _Out_ PULONG FrameSize)
{
    if (MessageBodyLength > ZP_FRAME_MAX_BODY_SIZE - sizeof(BYTE))
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    *FrameSize = sizeof(ULONG) + sizeof(BYTE) + MessageBodyLength;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFrame_Encode(
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(MessageBodyLength) const VOID* MessageBody,
    _In_ ULONG MessageBodyLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    NTSTATUS Status;
    ULONG FrameSize;
    PBYTE Output;

    if (MessageBodyLength != 0 && MessageBody == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpFrame_GetSize(MessageBodyLength, &FrameSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpFrame_ValidateBody(MessageType, MessageBody, MessageBodyLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Buffer == NULL)
    {
        *BytesWritten = FrameSize;
        return STATUS_SUCCESS;
    }
    if (BufferSize < FrameSize)
    {
        *BytesWritten = FrameSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Output = Buffer;
    ZpWriteUInt32(Output, sizeof(BYTE) + MessageBodyLength);
    Output[sizeof(ULONG)] = (BYTE)MessageType;
    if (MessageBodyLength != 0)
    {
        RtlCopyMemory(Output + sizeof(ULONG) + sizeof(BYTE), MessageBody, MessageBodyLength);
    }
    *BytesWritten = FrameSize;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpFrame_Decode(
    _In_reads_bytes_(BufferSize) const VOID* Buffer,
    _In_ ULONG BufferSize,
    _Out_ PZP_FRAME_VIEW View,
    _Out_ PULONG BytesConsumed)
{
    NTSTATUS Status;
    const BYTE* Input = Buffer;
    ULONG BodyLength, FrameSize, MessageBodyLength;
    ZP_MESSAGE_TYPE MessageType;

    if (BufferSize < sizeof(ULONG))
    {
        return STATUS_MORE_PROCESSING_REQUIRED;
    }
    BodyLength = ZpReadUInt32(Input);
    if (BodyLength < sizeof(BYTE) || BodyLength > ZP_FRAME_MAX_BODY_SIZE)
    {
        return STATUS_DATA_ERROR;
    }
    FrameSize = sizeof(ULONG) + BodyLength;
    if (BufferSize < FrameSize)
    {
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    MessageType = (ZP_MESSAGE_TYPE)Input[sizeof(ULONG)];
    MessageBodyLength = BodyLength - sizeof(BYTE);
    Status = ZpFrame_ValidateBody(MessageType,
                                  Input + sizeof(ULONG) + sizeof(BYTE),
                                  MessageBodyLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    View->MessageType = MessageType;
    View->Body = Input + sizeof(ULONG) + sizeof(BYTE);
    View->BodyLength = MessageBodyLength;
    *BytesConsumed = FrameSize;
    return STATUS_SUCCESS;
}
