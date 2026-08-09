#include "Connection.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

static
ULONG
ZpConnection_ReadUInt32(
    _In_reads_bytes_(sizeof(ULONG)) const BYTE* Buffer)
{
    return (ULONG)Buffer[0] |
           ((ULONG)Buffer[1] << 8) |
           ((ULONG)Buffer[2] << 16) |
           ((ULONG)Buffer[3] << 24);
}

static
VOID
ZpConnection_Close(
    _Inout_ PZP_CONNECTION Connection)
{
    if (Connection->ReceiveBuffer != NULL)
    {
        RtlFreeHeap(RtlProcessHeap(), 0, Connection->ReceiveBuffer);
        Connection->ReceiveBuffer = NULL;
    }
    Connection->ReceivePrefixLength = 0;
    Connection->ReceiveBufferLength = 0;
    Connection->ReceiveBufferSize = 0;
    Connection->ReceiveFrameSize = 0;
    Connection->State = ZpConnectionStateClosed;
}

static
NTSTATUS
ZpConnection_GetFrameSize(
    _In_reads_bytes_(sizeof(ULONG)) const BYTE* Prefix,
    _Out_ PULONG FrameSize)
{
    ULONG BodyLength;

    BodyLength = ZpConnection_ReadUInt32(Prefix);
    if (BodyLength < sizeof(BYTE) || BodyLength > ZP_FRAME_MAX_BODY_SIZE)
    {
        return STATUS_DATA_ERROR;
    }
    *FrameSize = sizeof(ULONG) + BodyLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpConnection_ReserveReceiveBuffer(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ULONG RequiredSize)
{
    PBYTE Buffer;
    ULONG BufferSize;

    if (RequiredSize <= Connection->ReceiveBufferSize)
    {
        return STATUS_SUCCESS;
    }
    BufferSize = Connection->ReceiveBufferSize != 0 ?
                     Connection->ReceiveBufferSize :
                     min(ZP_CONNECTION_INITIAL_RECEIVE_BUFFER_SIZE, Connection->ReceiveFrameSize);
    while (BufferSize < RequiredSize)
    {
        BufferSize = min(BufferSize * 2, Connection->ReceiveFrameSize);
    }
    Buffer = Mem_ReAlloc(Connection->ReceiveBuffer, BufferSize);
    if (Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Connection->ReceiveBuffer = Buffer;
    Connection->ReceiveBufferSize = BufferSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpConnection_StartBufferedFrame(
    _Inout_ PZP_CONNECTION Connection,
    _In_reads_bytes_(DataLength) const BYTE* Data,
    _In_ ULONG DataLength,
    _In_ ULONG FrameSize)
{
    NTSTATUS Status;

    Connection->ReceiveFrameSize = FrameSize;
    Status = ZpConnection_ReserveReceiveBuffer(Connection, DataLength);
    if (!NT_SUCCESS(Status))
    {
        Connection->ReceiveFrameSize = 0;
        return Status;
    }
    RtlCopyMemory(Connection->ReceiveBuffer, Data, DataLength);
    Connection->ReceiveBufferLength = DataLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpConnection_GetReceiveState(
    _In_ const ZP_CONNECTION* Connection,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _Out_ ZP_CONNECTION_STATE* State)
{
    switch (Connection->State)
    {
        case ZpConnectionStateClientWaitChallenge:
            if (MessageType == ZpMessageServerChallenge)
            {
                *State = ZpConnectionStateClientSendAuthenticate;
                return STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateClientWaitReady:
            if (MessageType == ZpMessageReady)
            {
                *State = ZpConnectionStateReady;
                return STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateServerWaitHello:
            if (MessageType == ZpMessageClientHello)
            {
                *State = ZpConnectionStateServerSendChallenge;
                return STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateServerWaitAuthenticate:
            if (MessageType == ZpMessageClientAuthenticate)
            {
                *State = ZpConnectionStateServerSendReady;
                return STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateReady:
            if (MessageType >= ZpMessageRequest &&
                MessageType <= ZpMessageChannelWindow)
            {
                *State = ZpConnectionStateReady;
                return STATUS_SUCCESS;
            }
            break;
    }
    return STATUS_PROTOCOL_UNREACHABLE;
}

static
NTSTATUS
ZpConnection_DispatchFrame(
    _Inout_ PZP_CONNECTION Connection,
    _In_ const ZP_FRAME_VIEW* Frame)
{
    NTSTATUS Status;
    ZP_CONNECTION_STATE State;

    Status = ZpConnection_GetReceiveState(Connection, Frame->MessageType, &State);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Close(Connection);
        return Status;
    }
    Connection->State = State;
    Status = Connection->MessageCallback(Connection, Frame, Connection->CallbackContext);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Close(Connection);
    }
    return Status;
}

static
NTSTATUS
ZpConnection_DecodeAndDispatch(
    _Inout_ PZP_CONNECTION Connection,
    _In_reads_bytes_(FrameSize) const BYTE* Buffer,
    _In_ ULONG FrameSize)
{
    NTSTATUS Status;
    ZP_FRAME_VIEW Frame;
    ULONG BytesConsumed;

    Status = ZpFrame_Decode(Buffer, FrameSize, &Frame, &BytesConsumed);
    if (!NT_SUCCESS(Status) || BytesConsumed != FrameSize)
    {
        ZpConnection_Close(Connection);
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return ZpConnection_DispatchFrame(Connection, &Frame);
}

NTSTATUS
ZpConnection_Initialize(
    _Out_ PZP_CONNECTION Connection,
    _In_ ZP_CONNECTION_ROLE Role,
    _In_ ZP_CONNECTION_MESSAGE_CALLBACK MessageCallback,
    _In_opt_ PVOID CallbackContext)
{
    if ((Role != ZpConnectionRoleClient && Role != ZpConnectionRoleServer) || MessageCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Connection, sizeof(*Connection));
    Connection->State = Role == ZpConnectionRoleClient ?
                            ZpConnectionStateClientSendHello :
                            ZpConnectionStateServerWaitHello;
    Connection->MessageCallback = MessageCallback;
    Connection->CallbackContext = CallbackContext;
    return STATUS_SUCCESS;
}

VOID
ZpConnection_Uninitialize(
    _Inout_ PZP_CONNECTION Connection)
{
    ZpConnection_Close(Connection);
}

NTSTATUS
ZpConnection_NotifyMessageSent(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ZP_MESSAGE_TYPE MessageType)
{
    if (Connection->State == ZpConnectionStateClosed)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    switch (Connection->State)
    {
        case ZpConnectionStateClientSendHello:
            if (MessageType == ZpMessageClientHello)
            {
                Connection->State = ZpConnectionStateClientWaitChallenge;
                return STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateClientSendAuthenticate:
            if (MessageType == ZpMessageClientAuthenticate)
            {
                Connection->State = ZpConnectionStateClientWaitReady;
                return STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateServerSendChallenge:
            if (MessageType == ZpMessageServerChallenge)
            {
                Connection->State = ZpConnectionStateServerWaitAuthenticate;
                return STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateServerSendReady:
            if (MessageType == ZpMessageReady)
            {
                Connection->State = ZpConnectionStateReady;
                return STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateReady:
            if (MessageType >= ZpMessageRequest &&
                MessageType <= ZpMessageChannelWindow)
            {
                return STATUS_SUCCESS;
            }
            break;
    }
    return STATUS_INVALID_DEVICE_STATE;
}

NTSTATUS
ZpConnection_Receive(
    _Inout_ PZP_CONNECTION Connection,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    NTSTATUS Status;
    const BYTE* Input = Data;
    PBYTE FrameBuffer;
    ULONG CopyLength, FrameSize;

    if ((DataLength != 0 && Data == NULL) || Connection->State == ZpConnectionStateClosed)
    {
        return DataLength != 0 && Data == NULL ? STATUS_INVALID_PARAMETER : STATUS_INVALID_DEVICE_STATE;
    }

    while (DataLength != 0)
    {
        if (Connection->ReceiveBuffer != NULL)
        {
            CopyLength = min(DataLength, Connection->ReceiveFrameSize - Connection->ReceiveBufferLength);
            Status = ZpConnection_ReserveReceiveBuffer(Connection,
                                                       Connection->ReceiveBufferLength + CopyLength);
            if (!NT_SUCCESS(Status))
            {
                ZpConnection_Close(Connection);
                return Status;
            }
            RtlCopyMemory(Connection->ReceiveBuffer + Connection->ReceiveBufferLength, Input, CopyLength);
            Connection->ReceiveBufferLength += CopyLength;
            Input += CopyLength;
            DataLength -= CopyLength;
            if (Connection->ReceiveBufferLength != Connection->ReceiveFrameSize)
            {
                return STATUS_SUCCESS;
            }

            FrameBuffer = Connection->ReceiveBuffer;
            FrameSize = Connection->ReceiveFrameSize;
            Connection->ReceiveBuffer = NULL;
            Connection->ReceiveBufferLength = 0;
            Connection->ReceiveBufferSize = 0;
            Connection->ReceiveFrameSize = 0;
            Status = ZpConnection_DecodeAndDispatch(Connection, FrameBuffer, FrameSize);
            RtlFreeHeap(RtlProcessHeap(), 0, FrameBuffer);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            if (Connection->State == ZpConnectionStateClosed)
            {
                return STATUS_SUCCESS;
            }
            continue;
        }

        if (Connection->ReceivePrefixLength != 0)
        {
            CopyLength = min(DataLength, sizeof(ULONG) - Connection->ReceivePrefixLength);
            RtlCopyMemory(Connection->ReceivePrefix + Connection->ReceivePrefixLength, Input, CopyLength);
            Connection->ReceivePrefixLength += CopyLength;
            Input += CopyLength;
            DataLength -= CopyLength;
            if (Connection->ReceivePrefixLength != sizeof(ULONG))
            {
                return STATUS_SUCCESS;
            }

            Status = ZpConnection_GetFrameSize(Connection->ReceivePrefix, &FrameSize);
            if (!NT_SUCCESS(Status))
            {
                ZpConnection_Close(Connection);
                return Status;
            }
            Status = ZpConnection_StartBufferedFrame(Connection,
                                                     Connection->ReceivePrefix,
                                                     sizeof(ULONG),
                                                     FrameSize);
            Connection->ReceivePrefixLength = 0;
            if (!NT_SUCCESS(Status))
            {
                ZpConnection_Close(Connection);
                return Status;
            }
            continue;
        }

        if (DataLength < sizeof(ULONG))
        {
            RtlCopyMemory(Connection->ReceivePrefix, Input, DataLength);
            Connection->ReceivePrefixLength = DataLength;
            return STATUS_SUCCESS;
        }

        Status = ZpConnection_GetFrameSize(Input, &FrameSize);
        if (!NT_SUCCESS(Status))
        {
            ZpConnection_Close(Connection);
            return Status;
        }
        if (DataLength < FrameSize)
        {
            Status = ZpConnection_StartBufferedFrame(Connection, Input, DataLength, FrameSize);
            if (!NT_SUCCESS(Status))
            {
                ZpConnection_Close(Connection);
            }
            return Status;
        }

        Status = ZpConnection_DecodeAndDispatch(Connection, Input, FrameSize);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if (Connection->State == ZpConnectionStateClosed)
        {
            return STATUS_SUCCESS;
        }
        Input += FrameSize;
        DataLength -= FrameSize;
    }
    return STATUS_SUCCESS;
}
