#include "Protocol.inl"

static
LOGICAL
ZpMessage_IsTypeValid(
    _In_ ZP_MESSAGE_TYPE MessageType)
{
    return (MessageType >= ZpMessageClientHello && MessageType <= ZpMessageServerReject) ||
           (MessageType >= ZpMessageRequest && MessageType <= ZpMessageConnectionPolicy);
}

static
ULONG
ZpMessage_GetStatusWireSize(
    _In_ ZP_STATUS_TYPE Type)
{
    return Type == ZpStatusNone ? ZP_STATUS_MIN_WIRE_SIZE : ZP_STATUS_MAX_WIRE_SIZE;
}

static
VOID
ZpMessage_WriteStatus(
    _Inout_ PBYTE* Cursor,
    _In_ ZP_STATUS Status)
{
    ZpWire_WriteByte(Cursor, Status.Type);
    if (Status.Type != ZpStatusNone) ZpWire_WriteUInt32(Cursor, Status.Code);
}

static
NTSTATUS
ZpMessage_ReadStatus(
    _In_reads_bytes_(BufferLength) const BYTE* Buffer,
    _In_ ULONG BufferLength,
    _Out_ PZP_STATUS Status,
    _Out_ PULONG BytesRead)
{
    if (BufferLength < ZP_STATUS_MIN_WIRE_SIZE) return STATUS_DATA_ERROR;
    Status->Type = Buffer[0];
    if (Status->Type == ZpStatusNone)
    {
        Status->Code = 0;
        *BytesRead = ZP_STATUS_MIN_WIRE_SIZE;
    }
    else
    {
        if (BufferLength < ZP_STATUS_MAX_WIRE_SIZE) return STATUS_DATA_ERROR;
        Status->Code = ZpReadUInt32(Buffer + sizeof(BYTE));
        *BytesRead = ZP_STATUS_MAX_WIRE_SIZE;
    }

    return ZpStatus_IsValid(*Status) ? STATUS_SUCCESS : STATUS_DATA_ERROR;
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
    if (ZpMessage_IsCompressed(MessageType))
    {
        ULONG OriginalLength;

        MessageType = ZpMessage_GetType(MessageType);
        if (BodyLength <= sizeof(ULONG))
        {
            return STATUS_DATA_ERROR;
        }
        OriginalLength = ZpReadUInt32(Body);
        if ((MessageType != ZpMessageResponse &&
             MessageType != ZpMessageChannelData) ||
            OriginalLength < ZP_MESSAGE_COMPRESSION_MIN_SIZE ||
            OriginalLength > ZP_MESSAGE_MAX_BODY_SIZE ||
            BodyLength > OriginalLength - 64)
        {
            return STATUS_DATA_ERROR;
        }
        return STATUS_SUCCESS;
    }
    if (!ZpMessage_IsTypeValid(MessageType))
    {
        return STATUS_DATA_ERROR;
    }

    switch (MessageType)
    {
        case ZpMessageClientHello:
            if (BodyLength != ZP_CLIENT_HELLO_WIRE_SIZE)
            {
                return STATUS_DATA_ERROR;
            }
            return Body[sizeof(BYTE)] == 0x04 ? STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessageServerChallenge:
            return BodyLength == ZP_SERVER_CHALLENGE_SIZE ? STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessageClientAuthenticate:
            return BodyLength == ZP_CLIENT_SIGNATURE_SIZE ? STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessageReady:
            return BodyLength == 0 ? STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessageServerReject:
            return BodyLength == sizeof(BYTE) &&
                   Body[0] == ZpServerRejectClientVersionTooOld ?
                       STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessageRequest:
            {
                const BYTE* Cursor = Body;

                if (BodyLength < ZP_REQUEST_HEADER_WIRE_SIZE || ZpReadUInt32(Cursor) == 0)
                {
                    return STATUS_DATA_ERROR;
                }
                Cursor += sizeof(ULONG);
                return Cursor[0] != 0 && Cursor[1] != 0 ? STATUS_SUCCESS : STATUS_DATA_ERROR;
            }

        case ZpMessageResponse:
            {
                ZP_STATUS Status;
                ULONG StatusLength;

                return BodyLength >= ZP_RESPONSE_MIN_HEADER_WIRE_SIZE &&
                       ZpReadUInt32(Body) != 0 &&
                       NT_SUCCESS(ZpMessage_ReadStatus(Body + sizeof(ULONG),
                                                       BodyLength - sizeof(ULONG),
                                                       &Status,
                                                       &StatusLength)) ?
                           STATUS_SUCCESS : STATUS_DATA_ERROR;
            }

        case ZpMessageCancel:
            return BodyLength == sizeof(ULONG) && ZpReadUInt32(Body) != 0 ? STATUS_SUCCESS : STATUS_DATA_ERROR;

        case ZpMessageChannelData:
            return BodyLength > sizeof(ULONG) &&
                   BodyLength <= sizeof(ULONG) + ZP_CHANNEL_DATA_MAX_SIZE &&
                   ZpReadUInt32(Body) != 0 ?
                       STATUS_SUCCESS :
                       STATUS_DATA_ERROR;

        case ZpMessageChannelClose:
            {
                ZP_STATUS Status;
                ULONG StatusLength;

                return BodyLength >= sizeof(ULONG) + ZP_STATUS_MIN_WIRE_SIZE &&
                       ZpReadUInt32(Body) != 0 &&
                       NT_SUCCESS(ZpMessage_ReadStatus(Body + sizeof(ULONG),
                                                       BodyLength - sizeof(ULONG),
                                                       &Status,
                                                       &StatusLength)) &&
                       BodyLength == sizeof(ULONG) + StatusLength ?
                           STATUS_SUCCESS : STATUS_DATA_ERROR;
            }

        case ZpMessageChannelWindow:
            return BodyLength == 2 * sizeof(ULONG) &&
                   ZpReadUInt32(Body) != 0 &&
                   ZpReadUInt32(Body + sizeof(ULONG)) != 0 &&
                   ZpReadUInt32(Body + sizeof(ULONG)) <= ZP_CHANNEL_WINDOW_MAX_SIZE ?
                       STATUS_SUCCESS :
                       STATUS_DATA_ERROR;

        case ZpMessageConnectionPolicy:
            return BodyLength == ZP_CONNECTION_POLICY_WIRE_SIZE &&
                   (Body[0] & ZP_CONNECTION_POLICY_RESERVED_MASK) == 0 &&
                   (Body[0] & ZP_CONNECTION_POLICY_CLASS_MASK) < ZP_PERFORMANCE_CLASS_COUNT &&
                   ((Body[0] >> ZP_CONNECTION_POLICY_LATENCY_SHIFT) &
                    ZP_CONNECTION_POLICY_CLASS_MASK) < ZP_PERFORMANCE_CLASS_COUNT ?
                       STATUS_SUCCESS : STATUS_DATA_ERROR;
    }
    return STATUS_DATA_ERROR;
}

NTSTATUS
ZpMessage_EncodeClientHello(
    _In_reads_bytes_(ZP_CLIENT_PUBLIC_KEY_SIZE) const BYTE* ClientPublicKey,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    NTSTATUS Status;
    PBYTE Cursor;

    if (ClientPublicKey == NULL || ClientPublicKey[0] != 0x04)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpMessage_PrepareOutput(Buffer,
                                     BufferSize,
                                     ZP_CLIENT_HELLO_WIRE_SIZE,
                                     BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }

    Cursor = Buffer;
    ZpWire_WriteByte(&Cursor, ZP_CLIENT_VERSION);
    ZpWire_WriteData(&Cursor, ClientPublicKey, ZP_CLIENT_PUBLIC_KEY_SIZE);
    return STATUS_SUCCESS;
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
        View->ClientVersion = Buffer[0];
        View->ClientPublicKey = Buffer + sizeof(BYTE);
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
ZpMessage_EncodeConnectionPolicy(
    _In_ PCZP_CONNECTION_POLICY Policy,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    NTSTATUS Status;
    PBYTE Cursor;

    if (Policy == NULL || Policy->Reserved != 0 ||
        Policy->SpeedClass >= ZP_PERFORMANCE_CLASS_COUNT ||
        Policy->LatencyClass >= ZP_PERFORMANCE_CLASS_COUNT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpMessage_PrepareOutput(Buffer,
                                     BufferSize,
                                     ZP_CONNECTION_POLICY_WIRE_SIZE,
                                     BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }
    Cursor = Buffer;
    ZpWire_WriteByte(&Cursor, *(const BYTE*)Policy);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpMessage_DecodeConnectionPolicy(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_CONNECTION_POLICY Policy)
{
    const BYTE* Buffer = Body;
    NTSTATUS Status;

    if (Policy == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpMessage_ValidateBody(ZpMessageConnectionPolicy, Buffer, BodyLength);
    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(Policy, Buffer, sizeof(*Policy));
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodeRequestHeader(
    _In_ PCZP_REQUEST Message,
    _Out_writes_bytes_(ZP_REQUEST_HEADER_WIRE_SIZE) PVOID Buffer)
{
    PBYTE Cursor;

    if (Message->RequestId == 0 ||
        Message->ModuleId == 0 ||
        Message->OperationId == 0 ||
        Message->PayloadLength > ZP_REQUEST_MAX_PAYLOAD_SIZE ||
        (Message->PayloadLength != 0 && Message->Payload == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Cursor = Buffer;
    ZpWriteUInt32(Cursor, Message->RequestId);
    Cursor += sizeof(Message->RequestId);
    *Cursor++ = Message->ModuleId;
    *Cursor++ = Message->OperationId;
    ZpWriteUInt32(Cursor, Message->TimeoutMilliseconds);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpMessage_EncodeRequest(
    _In_ PCZP_REQUEST Message,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    BYTE Header[ZP_REQUEST_HEADER_WIRE_SIZE];
    NTSTATUS Status;
    ULONG RequiredSize;
    PBYTE Cursor;

    Status = ZpMessage_EncodeRequestHeader(Message, Header);
    if (!NT_SUCCESS(Status)) return Status;
    RequiredSize = ZP_REQUEST_HEADER_WIRE_SIZE + Message->PayloadLength;
    Status = ZpMessage_PrepareOutput(Buffer, BufferSize, RequiredSize, BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL) return Status;
    Cursor = Buffer;
    ZpWire_WriteData(&Cursor, Header, sizeof(Header));
    ZpWire_WriteData(&Cursor, Message->Payload, Message->PayloadLength);
    return STATUS_SUCCESS;
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
        View->RequestId = ZpReadUInt32(Buffer);
        Buffer += sizeof(ULONG);
        View->ModuleId = *Buffer++;
        View->OperationId = *Buffer++;
        View->TimeoutMilliseconds = ZpReadUInt32(Buffer);
        Buffer += sizeof(ULONG);
        View->Payload.Buffer = Buffer;
        View->Payload.Length = BodyLength - ZP_REQUEST_HEADER_WIRE_SIZE;
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodeResponseHeader(
    _In_ PCZP_RESPONSE Message,
    _Out_writes_bytes_to_(ZP_RESPONSE_MAX_HEADER_WIRE_SIZE, *BytesWritten) PVOID Buffer,
    _Out_ PULONG BytesWritten)
{
    ULONG HeaderSize;
    PBYTE Cursor;

    HeaderSize = sizeof(ULONG) + ZpMessage_GetStatusWireSize(Message->Status.Type);
    if (Message->RequestId == 0 ||
        !ZpStatus_IsValid(Message->Status) ||
        Message->PayloadLength > ZP_MESSAGE_MAX_BODY_SIZE - HeaderSize ||
        (Message->PayloadLength != 0 && Message->Payload == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, Message->RequestId);
    ZpMessage_WriteStatus(&Cursor, Message->Status);
    *BytesWritten = HeaderSize;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpMessage_EncodeResponse(
    _In_ PCZP_RESPONSE Message,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    BYTE Header[ZP_RESPONSE_MAX_HEADER_WIRE_SIZE];
    NTSTATUS Status;
    ULONG HeaderSize, RequiredSize;
    PBYTE Cursor;

    Status = ZpMessage_EncodeResponseHeader(Message, Header, &HeaderSize);
    if (!NT_SUCCESS(Status)) return Status;
    RequiredSize = HeaderSize + Message->PayloadLength;
    Status = ZpMessage_PrepareOutput(Buffer, BufferSize, RequiredSize, BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL) return Status;
    Cursor = Buffer;
    ZpWire_WriteData(&Cursor, Header, HeaderSize);
    ZpWire_WriteData(&Cursor, Message->Payload, Message->PayloadLength);
    return STATUS_SUCCESS;
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
        ULONG StatusLength;

        View->RequestId = ZpReadUInt32(Buffer);
        Buffer += sizeof(ULONG);
        Status = ZpMessage_ReadStatus(Buffer,
                                     BodyLength - sizeof(ULONG),
                                     &View->Status,
                                     &StatusLength);
        Buffer += StatusLength;
        View->Payload.Buffer = Buffer;
        View->Payload.Length = BodyLength - sizeof(ULONG) - StatusLength;
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodeCancel(
    _In_ ULONG RequestId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
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
    ZpWriteUInt32(Buffer, RequestId);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpMessage_DecodeCancel(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PULONG RequestId)
{
    NTSTATUS Status;

    Status = ZpMessage_ValidateBody(ZpMessageCancel, Body, BodyLength);
    if (NT_SUCCESS(Status))
    {
        *RequestId = ZpReadUInt32(Body);
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodeChannelDataHeader(
    _In_ ULONG ChannelId,
    _Out_writes_bytes_(sizeof(ULONG)) PVOID Buffer)
{
    if (ChannelId == 0) return STATUS_INVALID_PARAMETER;
    ZpWriteUInt32(Buffer, ChannelId);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpMessage_EncodeChannelData(
    _In_ ULONG ChannelId,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    NTSTATUS Status;
    ULONG RequiredSize;

    if (DataLength == 0 || DataLength > ZP_CHANNEL_DATA_MAX_SIZE || Data == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(ChannelId) + DataLength;
    Status = ZpMessage_PrepareOutput(Buffer, BufferSize, RequiredSize, BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL) return Status;
    Status = ZpMessage_EncodeChannelDataHeader(ChannelId, Buffer);
    if (!NT_SUCCESS(Status)) return Status;
    Cursor = Add2Ptr(Buffer, sizeof(ChannelId));
    ZpWire_WriteData(&Cursor, Data, DataLength);
    return STATUS_SUCCESS;
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
        View->ChannelId = ZpReadUInt32(Buffer);
        View->Data.Buffer = Buffer + sizeof(ULONG);
        View->Data.Length = BodyLength - sizeof(ULONG);
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodeChannelClose(
    _In_ ULONG ChannelId,
    _In_ ZP_STATUS StatusCode,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    NTSTATUS Status;
    ULONG RequiredSize;

    if (ChannelId == 0 || !ZpStatus_IsValid(StatusCode))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(ULONG) + ZpMessage_GetStatusWireSize(StatusCode.Type);
    Status = ZpMessage_PrepareOutput(Buffer, BufferSize, RequiredSize, BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }
    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, ChannelId);
    ZpMessage_WriteStatus(&Cursor, StatusCode);
    return STATUS_SUCCESS;
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
        ULONG StatusLength;

        Message->ChannelId = ZpReadUInt32(Buffer);
        Status = ZpMessage_ReadStatus(Buffer + sizeof(ULONG),
                                     BodyLength - sizeof(ULONG),
                                     &Message->Status,
                                     &StatusLength);
    }
    return Status;
}

NTSTATUS
ZpMessage_EncodeChannelWindow(
    _In_ ULONG ChannelId,
    _In_ ULONG CreditBytes,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    NTSTATUS Status;

    if (ChannelId == 0 ||
        CreditBytes == 0 ||
        CreditBytes > ZP_CHANNEL_WINDOW_MAX_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpMessage_PrepareOutput(Buffer,
                                     BufferSize,
                                     2 * sizeof(ULONG),
                                     BytesWritten);
    if (!NT_SUCCESS(Status) || Buffer == NULL)
    {
        return Status;
    }
    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, ChannelId);
    ZpWire_WriteUInt32(&Cursor, CreditBytes);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpMessage_DecodeChannelWindow(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PULONG ChannelId,
    _Out_ PULONG CreditBytes)
{
    const BYTE* Buffer = Body;
    NTSTATUS Status;

    Status = ZpMessage_ValidateBody(ZpMessageChannelWindow,
                                    Buffer,
                                    BodyLength);
    if (NT_SUCCESS(Status))
    {
        *ChannelId = ZpReadUInt32(Buffer);
        *CreditBytes = ZpReadUInt32(Buffer + sizeof(ULONG));
    }
    return Status;
}
