#include "Protocol.inl"

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
    Status = ZpMessage_ValidateBody(MessageType, MessageBody, MessageBodyLength);
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
    Status = ZpMessage_ValidateBody(MessageType,
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
