#include "Protocol.inl"

static
LOGICAL
ZpMessage_IsTypeValid(
    _In_ ZP_MESSAGE_TYPE MessageType)
{
    return (MessageType >= ZpMessageClientHello && MessageType <= ZpMessageReady) ||
           (MessageType >= ZpMessageRequest && MessageType <= ZpMessageChannelWindow);
}

static
NTSTATUS
ZpMessage_ValidateModuleRecords(
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
    ExpectedLength = HeaderLength + Count * sizeof(ZP_MODULE_RECORD) +
                     (HasClientPublicKey ? ZP_CLIENT_PUBLIC_KEY_SIZE : 0);
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
        Offset += sizeof(ZP_MODULE_RECORD);
    }
    if (HasClientPublicKey && Body[Offset] != 0x04)
    {
        return STATUS_DATA_ERROR;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpMessage_ValidateModules(
    _In_reads_opt_(ModuleCount) PCZP_MODULE_RECORD Modules,
    _In_ USHORT ModuleCount)
{
    USHORT Index, PreviousModuleId = 0;

    if (ModuleCount > ZP_MODULE_MAX_COUNT || (ModuleCount != 0 && Modules == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < ModuleCount; Index++)
    {
        if (Modules[Index].ModuleId == 0 ||
            Modules[Index].ModuleId <= PreviousModuleId ||
            Modules[Index].ModuleVersion == 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        PreviousModuleId = Modules[Index].ModuleId;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpMessage_PrepareOutput(
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _In_ ULONG RequiredSize,
    _Out_ PULONG BytesWritten)
{
    *BytesWritten = RequiredSize;
    return Buffer != NULL && BufferSize < RequiredSize ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}

static
NTSTATUS
ZpMessage_EncodeFixedData(
    _In_reads_bytes_(Length) const BYTE* Data,
    _In_ ULONG Length,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    NTSTATUS Status;

    if (Data == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpMessage_PrepareOutput(Buffer, BufferSize, Length, BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }
    RtlCopyMemory(Buffer, Data, Length);
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpMessage_DecodeFixedData(
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_BUFFER_VIEW View)
{
    NTSTATUS Status;

    Status = ZpMessage_ValidateBody(MessageType, Body, BodyLength);
    if (NT_SUCCESS(Status))
    {
        View->Buffer = Body;
        View->Length = BodyLength;
    }
    return Status;
}

NTSTATUS
ZpMessage_ValidateBody(
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const BYTE* Body,
    _In_ ULONG BodyLength)
{
    if (!ZpMessage_IsTypeValid(MessageType))
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
            return ZpMessage_ValidateModuleRecords(Body, BodyLength, 4, TRUE);

        case ZpMessageServerChallenge:
            return BodyLength == ZP_SERVER_CHALLENGE_SIZE ? STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessageClientAuthenticate:
            return BodyLength == ZP_CLIENT_SIGNATURE_SIZE ? STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessageReady:
            if (BodyLength < sizeof(USHORT))
            {
                return STATUS_DATA_ERROR;
            }
            return ZpMessage_ValidateModuleRecords(Body, BodyLength, sizeof(USHORT), FALSE);

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

        case ZpMessageChannelData:
            return BodyLength > 8 && BodyLength <= 8 + ZP_CHANNEL_DATA_MAX_SIZE && ZpReadUInt64(Body) != 0 ?
                       STATUS_SUCCESS :
                       STATUS_DATA_ERROR;

        case ZpMessageChannelClose:
            return BodyLength == 12 && ZpReadUInt64(Body) != 0 ? STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessageChannelWindow:
            return BodyLength == 12 &&
                   ZpReadUInt64(Body) != 0 &&
                   ZpReadUInt32(Body + 8) != 0 &&
                   ZpReadUInt32(Body + 8) <= ZP_CHANNEL_WINDOW_MAX_SIZE ?
                       STATUS_SUCCESS :
                       STATUS_DATA_ERROR;

        case ZpMessagePing:
        case ZpMessagePong:
            return BodyLength == 8 ? STATUS_SUCCESS : STATUS_DATA_ERROR;
    }
    return STATUS_DATA_ERROR;
}

NTSTATUS
ZpMessage_GetModuleRecord(
    _In_ PCZP_MODULE_LIST_VIEW Modules,
    _In_ USHORT Index,
    _Out_ PZP_MODULE_RECORD Record)
{
    const BYTE* Buffer;

    if (Index >= Modules->Count)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Buffer = Modules->Buffer + (ULONG)Index * sizeof(ZP_MODULE_RECORD);
    Record->ModuleId = ZpReadUInt16(Buffer);
    Record->ModuleVersion = ZpReadUInt16(Buffer + sizeof(USHORT));
    return STATUS_SUCCESS;
}

NTSTATUS
ZpMessage_EncodeClientHello(
    _In_ PCZP_CLIENT_HELLO Message,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    NTSTATUS Status;
    ULONG RequiredSize;
    USHORT Index;
    ZP_CODEC_WRITER Writer;

    if (Message->CoreVersion != ZP_CORE_VERSION)
    {
        return STATUS_REVISION_MISMATCH;
    }
    Status = ZpMessage_ValidateModules(Message->Modules, Message->ModuleCount);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Message->ClientPublicKey == NULL || Message->ClientPublicKey[0] != 0x04)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RequiredSize = 4 + (ULONG)Message->ModuleCount * sizeof(ZP_MODULE_RECORD) +
                   ZP_CLIENT_PUBLIC_KEY_SIZE;
    Status = ZpMessage_PrepareOutput(Buffer, BufferSize, RequiredSize, BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }

    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt16(&Writer, Message->CoreVersion);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt16(&Writer, Message->ModuleCount);
    }
    for (Index = 0; NT_SUCCESS(Status) && Index < Message->ModuleCount; Index++)
    {
        Status = ZpCodec_WriteUInt16(&Writer, Message->Modules[Index].ModuleId);
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteUInt16(&Writer, Message->Modules[Index].ModuleVersion);
        }
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteData(&Writer, Message->ClientPublicKey, ZP_CLIENT_PUBLIC_KEY_SIZE);
    }
    return Status;
}

NTSTATUS
ZpMessage_DecodeClientHello(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_CLIENT_HELLO_VIEW View)
{
    NTSTATUS Status;
    const BYTE* Buffer = Body;

    Status = ZpMessage_ValidateBody(ZpMessageClientHello, Buffer, BodyLength);
    if (NT_SUCCESS(Status))
    {
        View->CoreVersion = ZpReadUInt16(Buffer);
        View->Modules.Buffer = Buffer + 4;
        View->Modules.Count = ZpReadUInt16(Buffer + sizeof(USHORT));
        View->ClientPublicKey = View->Modules.Buffer +
                                (ULONG)View->Modules.Count * sizeof(ZP_MODULE_RECORD);
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodeServerChallenge(
    _In_reads_bytes_(ZP_SERVER_CHALLENGE_SIZE) const BYTE* Challenge,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    return ZpMessage_EncodeFixedData(Challenge,
                                     ZP_SERVER_CHALLENGE_SIZE,
                                     Buffer,
                                     BufferSize,
                                     BytesWritten);
}

NTSTATUS
ZpMessage_DecodeServerChallenge(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_BUFFER_VIEW View)
{
    return ZpMessage_DecodeFixedData(ZpMessageServerChallenge, Body, BodyLength, View);
}

NTSTATUS
ZpMessage_EncodeClientAuthenticate(
    _In_reads_bytes_(ZP_CLIENT_SIGNATURE_SIZE) const BYTE* Signature,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    return ZpMessage_EncodeFixedData(Signature,
                                     ZP_CLIENT_SIGNATURE_SIZE,
                                     Buffer,
                                     BufferSize,
                                     BytesWritten);
}

NTSTATUS
ZpMessage_DecodeClientAuthenticate(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_BUFFER_VIEW View)
{
    return ZpMessage_DecodeFixedData(ZpMessageClientAuthenticate, Body, BodyLength, View);
}

NTSTATUS
ZpMessage_EncodeReady(
    _In_ PCZP_READY Message,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    NTSTATUS Status;
    ULONG RequiredSize;
    USHORT Index;
    ZP_CODEC_WRITER Writer;

    Status = ZpMessage_ValidateModules(Message->Modules, Message->ModuleCount);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    RequiredSize = sizeof(USHORT) +
                   (ULONG)Message->ModuleCount * sizeof(ZP_MODULE_RECORD);
    Status = ZpMessage_PrepareOutput(Buffer, BufferSize, RequiredSize, BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }

    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt16(&Writer, Message->ModuleCount);
    for (Index = 0; NT_SUCCESS(Status) && Index < Message->ModuleCount; Index++)
    {
        Status = ZpCodec_WriteUInt16(&Writer, Message->Modules[Index].ModuleId);
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteUInt16(&Writer, Message->Modules[Index].ModuleVersion);
        }
    }
    return Status;
}

NTSTATUS
ZpMessage_DecodeReady(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_READY_VIEW View)
{
    NTSTATUS Status;
    const BYTE* Buffer = Body;

    Status = ZpMessage_ValidateBody(ZpMessageReady, Buffer, BodyLength);
    if (NT_SUCCESS(Status))
    {
        View->Modules.Buffer = Buffer + sizeof(USHORT);
        View->Modules.Count = ZpReadUInt16(Buffer);
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodeRequest(
    _In_ PCZP_REQUEST Message,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;
    ULONG RequiredSize;

    if (Message->RequestId == 0 ||
        Message->ModuleId == 0 ||
        Message->OperationId == 0 ||
        Message->PayloadLength > ZP_FRAME_MAX_BODY_SIZE - 16 ||
        (Message->PayloadLength != 0 && Message->Payload == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 16 + Message->PayloadLength;
    Status = ZpMessage_PrepareOutput(Buffer, BufferSize, RequiredSize, BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }

    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, Message->RequestId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt16(&Writer, Message->ModuleId);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt16(&Writer, Message->OperationId);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Message->TimeoutMilliseconds);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteData(&Writer, Message->Payload, Message->PayloadLength);
    }
    return Status;
}

NTSTATUS
ZpMessage_DecodeRequest(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_REQUEST_VIEW View)
{
    const BYTE* Buffer = Body;
    NTSTATUS Status;

    Status = ZpMessage_ValidateBody(ZpMessageRequest, Buffer, BodyLength);
    if (NT_SUCCESS(Status))
    {
        View->RequestId = ZpReadUInt64(Buffer);
        View->ModuleId = ZpReadUInt16(Buffer + 8);
        View->OperationId = ZpReadUInt16(Buffer + 10);
        View->TimeoutMilliseconds = ZpReadUInt32(Buffer + 12);
        View->Payload.Buffer = Buffer + 16;
        View->Payload.Length = BodyLength - 16;
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodeResponse(
    _In_ PCZP_RESPONSE Message,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;
    ULONG RequiredSize;

    if (Message->RequestId == 0 ||
        Message->PayloadLength > ZP_FRAME_MAX_BODY_SIZE - 12 ||
        (Message->PayloadLength != 0 && Message->Payload == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 12 + Message->PayloadLength;
    Status = ZpMessage_PrepareOutput(Buffer, BufferSize, RequiredSize, BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }

    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, Message->RequestId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Message->Status);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteData(&Writer, Message->Payload, Message->PayloadLength);
    }
    return Status;
}

NTSTATUS
ZpMessage_DecodeResponse(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_RESPONSE_VIEW View)
{
    const BYTE* Buffer = Body;
    NTSTATUS Status;

    Status = ZpMessage_ValidateBody(ZpMessageResponse, Buffer, BodyLength);
    if (NT_SUCCESS(Status))
    {
        View->RequestId = ZpReadUInt64(Buffer);
        View->Status = (NTSTATUS)ZpReadUInt32(Buffer + 8);
        View->Payload.Buffer = Buffer + 12;
        View->Payload.Length = BodyLength - 12;
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodeCancel(
    _In_ ULONGLONG RequestId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (RequestId == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpMessage_PrepareOutput(Buffer, BufferSize, sizeof(RequestId), BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteUInt64(&Writer, RequestId);
}

NTSTATUS
ZpMessage_DecodeCancel(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PULONGLONG RequestId)
{
    NTSTATUS Status;

    Status = ZpMessage_ValidateBody(ZpMessageCancel, Body, BodyLength);
    if (NT_SUCCESS(Status))
    {
        *RequestId = ZpReadUInt64(Body);
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodeChannelData(
    _In_ ULONGLONG ChannelId,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;
    ULONG RequiredSize;

    if (ChannelId == 0 ||
        DataLength == 0 ||
        DataLength > ZP_CHANNEL_DATA_MAX_SIZE ||
        Data == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(ChannelId) + DataLength;
    Status = ZpMessage_PrepareOutput(Buffer,
                                     BufferSize,
                                     RequiredSize,
                                     BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, ChannelId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteData(&Writer, Data, DataLength);
    }
    return Status;
}

NTSTATUS
ZpMessage_DecodeChannelData(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_CHANNEL_DATA_VIEW View)
{
    const BYTE* Buffer = Body;
    NTSTATUS Status;

    Status = ZpMessage_ValidateBody(ZpMessageChannelData,
                                    Buffer,
                                    BodyLength);
    if (NT_SUCCESS(Status))
    {
        View->ChannelId = ZpReadUInt64(Buffer);
        View->Data.Buffer = Buffer + sizeof(ULONGLONG);
        View->Data.Length = BodyLength - sizeof(ULONGLONG);
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodeChannelClose(
    _In_ ULONGLONG ChannelId,
    _In_ NTSTATUS StatusCode,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (ChannelId == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpMessage_PrepareOutput(Buffer,
                                     BufferSize,
                                     sizeof(ULONGLONG) + sizeof(ULONG),
                                     BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, ChannelId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, (ULONG)StatusCode);
    }
    return Status;
}

NTSTATUS
ZpMessage_DecodeChannelClose(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_CHANNEL_CLOSE Message)
{
    const BYTE* Buffer = Body;
    NTSTATUS Status;

    Status = ZpMessage_ValidateBody(ZpMessageChannelClose,
                                    Buffer,
                                    BodyLength);
    if (NT_SUCCESS(Status))
    {
        Message->ChannelId = ZpReadUInt64(Buffer);
        Message->Status = (NTSTATUS)ZpReadUInt32(Buffer + sizeof(ULONGLONG));
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodeChannelWindow(
    _In_ ULONGLONG ChannelId,
    _In_ ULONG CreditBytes,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (ChannelId == 0 ||
        CreditBytes == 0 ||
        CreditBytes > ZP_CHANNEL_WINDOW_MAX_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpMessage_PrepareOutput(Buffer,
                                     BufferSize,
                                     sizeof(ULONGLONG) + sizeof(ULONG),
                                     BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, ChannelId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, CreditBytes);
    }
    return Status;
}

NTSTATUS
ZpMessage_DecodeChannelWindow(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PULONGLONG ChannelId,
    _Out_ PULONG CreditBytes)
{
    const BYTE* Buffer = Body;
    NTSTATUS Status;

    Status = ZpMessage_ValidateBody(ZpMessageChannelWindow,
                                    Buffer,
                                    BodyLength);
    if (NT_SUCCESS(Status))
    {
        *ChannelId = ZpReadUInt64(Buffer);
        *CreditBytes = ZpReadUInt32(Buffer + sizeof(ULONGLONG));
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodePing(
    _In_ ULONGLONG Token,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    Status = ZpMessage_PrepareOutput(Buffer, BufferSize, sizeof(Token), BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteUInt64(&Writer, Token);
}

NTSTATUS
ZpMessage_DecodePing(
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PULONGLONG Token)
{
    NTSTATUS Status;

    if (MessageType != ZpMessagePing && MessageType != ZpMessagePong)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpMessage_ValidateBody(MessageType, Body, BodyLength);
    if (NT_SUCCESS(Status))
    {
        *Token = ZpReadUInt64(Body);
    }
    return Status;
}
