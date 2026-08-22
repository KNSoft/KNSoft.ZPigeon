#include "Udp.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#pragma comment(lib, "Ws2_32.lib")

#define ZP_UDP_MAGIC 0x44505A4BUL
#define ZP_UDP_INNER_HEADER_SIZE 16UL
#define ZP_UDP_MAX_PENDING_BYTES (ZP_FRAME_MAX_BODY_SIZE + sizeof(ULONG))
#define ZP_UDP_MAX_REORDERED_PACKETS 64
#define ZP_UDP_SEND_WINDOW_PACKETS 32
#define ZP_UDP_HANDSHAKE_TIMEOUT_MILLISECONDS 10000
#define ZP_UDP_HANDSHAKE_RETRY_MILLISECONDS 500
#define ZP_UDP_RETRY_MILLISECONDS 250
#define ZP_UDP_MAX_RETRIES 20
#define ZP_UDP_KEEP_ALIVE_MILLISECONDS 15000
#define ZP_UDP_IDLE_TIMEOUT_MILLISECONDS 60000

static
ULONG
ZpUdp_ReadUInt32(
    _In_reads_bytes_(sizeof(ULONG)) const BYTE* Buffer)
{
    return (ULONG)Buffer[0] |
           ((ULONG)Buffer[1] << 8) |
           ((ULONG)Buffer[2] << 16) |
           ((ULONG)Buffer[3] << 24);
}

static
ULONGLONG
ZpUdp_ReadUInt64(
    _In_reads_bytes_(sizeof(ULONGLONG)) const BYTE* Buffer)
{
    return (ULONGLONG)ZpUdp_ReadUInt32(Buffer) |
           ((ULONGLONG)ZpUdp_ReadUInt32(Buffer + sizeof(ULONG)) << 32);
}

static
VOID
ZpUdp_WriteUInt32(
    _Out_writes_bytes_(sizeof(ULONG)) BYTE* Buffer,
    _In_ ULONG Value)
{
    Buffer[0] = (BYTE)Value;
    Buffer[1] = (BYTE)(Value >> 8);
    Buffer[2] = (BYTE)(Value >> 16);
    Buffer[3] = (BYTE)(Value >> 24);
}

static
VOID
ZpUdp_WriteUInt64(
    _Out_writes_bytes_(sizeof(ULONGLONG)) BYTE* Buffer,
    _In_ ULONGLONG Value)
{
    ZpUdp_WriteUInt32(Buffer, (ULONG)Value);
    ZpUdp_WriteUInt32(Buffer + sizeof(ULONG), (ULONG)(Value >> 32));
}

static
VOID
ZpUdp_EncodeHeader(
    _Out_writes_bytes_(ZP_UDP_HEADER_SIZE) BYTE* Buffer,
    _In_ BYTE Type,
    _In_ ULONGLONG ConnectionId)
{
    ZpUdp_WriteUInt32(Buffer, ZP_UDP_MAGIC);
    Buffer[4] = Type;
    ZpUdp_WriteUInt64(Buffer + 5, ConnectionId);
}

LOGICAL
ZpUdp_DecodeHeader(
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_ PBYTE Type,
    _Out_ PULONGLONG ConnectionId)
{
    const BYTE* Buffer = Data;

    if (DataLength < ZP_UDP_HEADER_SIZE ||
        ZpUdp_ReadUInt32(Buffer) != ZP_UDP_MAGIC ||
        Buffer[4] < ZP_UDP_PACKET_HANDSHAKE || Buffer[4] > ZP_UDP_PACKET_DATA)
    {
        return FALSE;
    }
    *Type = Buffer[4];
    *ConnectionId = ZpUdp_ReadUInt64(Buffer + 5);
    return *ConnectionId != 0;
}

LOGICAL
ZpUdp_IsSameAddress(
    _In_ const SOCKADDR_STORAGE* Left,
    _In_ INT LeftLength,
    _In_ const SOCKADDR_STORAGE* Right,
    _In_ INT RightLength)
{
    return LeftLength == RightLength &&
           RtlEqualMemory(Left, Right, LeftLength);
}

static
VOID
ZpUdp_FreeList(
    _Inout_ PLIST_ENTRY List)
{
    while (!IsListEmpty(List))
    {
        Mem_Free(CONTAINING_RECORD(RemoveHeadList(List), ZP_UDP_BUFFER, ListEntry));
    }
}

ZP_STATUS
ZpUdpConnection_Initialize(
    _Out_ PZP_UDP_CONNECTION Connection,
    _In_ SOCKET Socket,
    _In_reads_bytes_(RemoteAddressLength) const SOCKADDR* RemoteAddress,
    _In_ INT RemoteAddressLength,
    _In_ ULONGLONG ConnectionId,
    _In_ ZP_DTLS_ROLE Role,
    _In_ PCredHandle Credential,
    _In_opt_ PCWSTR ServerName,
    _In_ ZP_UDP_CONNECTED_ROUTINE ConnectedRoutine,
    _In_ ZP_UDP_RECEIVE_ROUTINE ReceiveRoutine,
    _In_opt_ PVOID CallbackContext)
{
    NTSTATUS Status;

    if (Socket == INVALID_SOCKET || RemoteAddress == NULL ||
        RemoteAddressLength <= 0 || RemoteAddressLength > sizeof(Connection->RemoteAddress) ||
        ConnectionId == 0 || ConnectedRoutine == NULL || ReceiveRoutine == NULL)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    RtlZeroMemory(Connection, sizeof(*Connection));
    Status = RtlInitializeCriticalSectionEx(&Connection->Lock,
                                            0,
                                            RTL_CRITICAL_SECTION_FLAG_NO_DEBUG_INFO);
    if (!NT_SUCCESS(Status))
    {
        return ZpStatus_FromNtStatus(Status);
    }
    Connection->Socket = Socket;
    RtlCopyMemory(&Connection->RemoteAddress, RemoteAddress, RemoteAddressLength);
    Connection->RemoteAddressLength = RemoteAddressLength;
    Connection->ConnectionId = ConnectionId;
    Connection->ConnectedRoutine = ConnectedRoutine;
    Connection->ReceiveRoutine = ReceiveRoutine;
    Connection->CallbackContext = CallbackContext;
    InitializeListHead(&Connection->HandshakeFlight);
    InitializeListHead(&Connection->SendQueue);
    InitializeListHead(&Connection->ReceiveQueue);
    Connection->LastReceiveTickCount = GetTickCount64();
    Connection->HandshakeStartTickCount = Connection->LastReceiveTickCount;
    ZpDtls_Initialize(&Connection->Dtls, Role, Credential, ServerName);
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ZP_STATUS
ZpUdpConnection_SendEncodedPacket(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_reads_bytes_(PacketLength) const VOID* Packet,
    _In_ ULONG PacketLength)
{
    INT Result;

    Result = sendto(Connection->Socket,
                    Packet,
                    PacketLength,
                    0,
                    (SOCKADDR*)&Connection->RemoteAddress,
                    Connection->RemoteAddressLength);
    if (Result == SOCKET_ERROR)
    {
        return ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
    }
    Connection->LastSendTickCount = GetTickCount64();
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ZP_STATUS
ZpUdpConnection_SendPacket(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_ BYTE Type,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    BYTE Packet[ZP_UDP_MAX_DATAGRAM_SIZE];
    if (DataLength > ZP_DTLS_MTU)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_BUFFER_SIZE);
    }
    ZpUdp_EncodeHeader(Packet, Type, Connection->ConnectionId);
    if (DataLength != 0)
    {
        RtlCopyMemory(Packet + ZP_UDP_HEADER_SIZE, Data, DataLength);
    }
    return ZpUdpConnection_SendEncodedPacket(Connection,
                                             Packet,
                                             ZP_UDP_HEADER_SIZE + DataLength);
}

static
ZP_STATUS
ZpUdpConnection_SendFlight(
    _Inout_ PZP_UDP_CONNECTION Connection)
{
    PLIST_ENTRY Entry;
    ZP_STATUS Status;

    for (Entry = Connection->HandshakeFlight.Flink;
         Entry != &Connection->HandshakeFlight;
         Entry = Entry->Flink)
    {
        PZP_UDP_BUFFER Buffer = CONTAINING_RECORD(Entry, ZP_UDP_BUFFER, ListEntry);

        Status = ZpUdpConnection_SendPacket(Connection,
                                            ZP_UDP_PACKET_HANDSHAKE,
                                            Buffer->Data,
                                            Buffer->DataLength);
        if (!ZpStatus_IsSuccess(Status))
        {
            return Status;
        }
    }
    Connection->HandshakeFlightTickCount = GetTickCount64();
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ZP_STATUS
ZpUdpConnection_ProcessHandshake(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    PZP_UDP_BUFFER Buffer;
    PBYTE Token;
    ULONG TokenLength;
    LOGICAL More, Complete;
    ZP_STATUS Status;

    if (Connection->Dtls.HandshakeComplete)
    {
        return Connection->Dtls.Role == ZpDtlsClient ?
                   ZpUdpConnection_SendFlight(Connection) :
                   ZpUdpConnection_SendPacket(Connection, ZP_UDP_PACKET_ESTABLISHED, NULL, 0);
    }
    ZpUdp_FreeList(&Connection->HandshakeFlight);
    do
    {
        Status = ZpDtls_Handshake(&Connection->Dtls,
                                  Data,
                                  DataLength,
                                  (SOCKADDR*)&Connection->RemoteAddress,
                                  Connection->RemoteAddressLength,
                                  &Token,
                                  &TokenLength,
                                  &More,
                                  &Complete);
        Data = NULL;
        DataLength = 0;
        if (!ZpStatus_IsSuccess(Status))
        {
            return Status;
        }
        if (TokenLength != 0)
        {
            Buffer = Mem_Alloc(FIELD_OFFSET(ZP_UDP_BUFFER, Data) + TokenLength);
            if (Buffer == NULL)
            {
                Mem_Free(Token);
                return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            }
            Buffer->DataLength = TokenLength;
            RtlCopyMemory(Buffer->Data, Token, TokenLength);
            InsertTailList(&Connection->HandshakeFlight, &Buffer->ListEntry);
            Mem_Free(Token);
        }
    } while (More);
    Status = ZpUdpConnection_SendFlight(Connection);
    if (ZpStatus_IsSuccess(Status) && Complete && Connection->Dtls.Role == ZpDtlsServer)
    {
        Status = Connection->ConnectedRoutine(Connection, Connection->CallbackContext);
        if (ZpStatus_IsSuccess(Status))
        {
            Connection->Connected = TRUE;
            Status = ZpUdpConnection_SendPacket(Connection,
                                                ZP_UDP_PACKET_ESTABLISHED,
                                                NULL,
                                                0);
        }
    }
    return Status;
}

ZP_STATUS
ZpUdpConnection_StartHandshake(
    _Inout_ PZP_UDP_CONNECTION Connection)
{
    ZP_STATUS Status;

    RtlEnterCriticalSection(&Connection->Lock);
    Status = ZpUdpConnection_ProcessHandshake(Connection, NULL, 0);
    RtlLeaveCriticalSection(&Connection->Lock);
    return Status;
}

static
VOID
ZpUdpConnection_ProcessAcknowledgment(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_ ULONGLONG Acknowledgment)
{
    while (!IsListEmpty(&Connection->SendQueue))
    {
        PZP_UDP_BUFFER Buffer = CONTAINING_RECORD(Connection->SendQueue.Flink,
                                                  ZP_UDP_BUFFER,
                                                  ListEntry);

        if (Buffer->Sequence >= Acknowledgment)
        {
            break;
        }
        RemoveEntryList(&Buffer->ListEntry);
        Connection->PendingBytes -= Buffer->DataLength;
        Mem_Free(Buffer);
    }
}

static
ZP_STATUS
ZpUdpConnection_SendReliable(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_ ULONGLONG Sequence,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    BYTE Plaintext[ZP_DTLS_MTU];
    BYTE Packet[ZP_UDP_MAX_DATAGRAM_SIZE];
    ULONG EncryptedLength;
    ZP_STATUS Status;

    ZpUdp_WriteUInt64(Plaintext, Sequence);
    ZpUdp_WriteUInt64(Plaintext + 8, Connection->NextReceiveSequence);
    if (DataLength != 0)
    {
        RtlCopyMemory(Plaintext + ZP_UDP_INNER_HEADER_SIZE, Data, DataLength);
    }
    ZpUdp_EncodeHeader(Packet, ZP_UDP_PACKET_DATA, Connection->ConnectionId);
    Status = ZpDtls_Encrypt(&Connection->Dtls,
                            Plaintext,
                            ZP_UDP_INNER_HEADER_SIZE + DataLength,
                            Packet + ZP_UDP_HEADER_SIZE,
                            ZP_DTLS_MTU,
                            &EncryptedLength);
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpUdpConnection_SendEncodedPacket(Connection,
                                                   Packet,
                                                   ZP_UDP_HEADER_SIZE + EncryptedLength);
    }
    return Status;
}

static
ZP_STATUS
ZpUdpConnection_Flush(
    _Inout_ PZP_UDP_CONNECTION Connection)
{
    PLIST_ENTRY Entry;
    ULONG InFlight = 0;
    ZP_STATUS Status;

    for (Entry = Connection->SendQueue.Flink;
         Entry != &Connection->SendQueue;
         Entry = Entry->Flink)
    {
        PZP_UDP_BUFFER Buffer = CONTAINING_RECORD(Entry,
                                                  ZP_UDP_BUFFER,
                                                  ListEntry);

        if (Buffer->SentTickCount != 0)
        {
            InFlight++;
            continue;
        }
        if (InFlight == ZP_UDP_SEND_WINDOW_PACKETS)
        {
            break;
        }
        Status = ZpUdpConnection_SendReliable(Connection,
                                              Buffer->Sequence,
                                              Buffer->Data,
                                              Buffer->DataLength);
        if (!ZpStatus_IsSuccess(Status))
        {
            return Status;
        }
        Buffer->SentTickCount = GetTickCount64();
        InFlight++;
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
NTSTATUS
ZpUdpConnection_Deliver(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    NTSTATUS Status;

    Connection->NextReceiveSequence++;
    RtlLeaveCriticalSection(&Connection->Lock);
    Status = Connection->ReceiveRoutine(Connection,
                                        Data,
                                        DataLength,
                                        Connection->CallbackContext);
    RtlEnterCriticalSection(&Connection->Lock);
    return Status;
}

static
NTSTATUS
ZpUdpConnection_ProcessPayload(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_ ULONGLONG Sequence,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    PLIST_ENTRY Entry;
    PZP_UDP_BUFFER Buffer;
    NTSTATUS Status;

    if (Sequence < Connection->NextReceiveSequence)
    {
        return STATUS_SUCCESS;
    }
    if (Sequence == Connection->NextReceiveSequence)
    {
        Status = ZpUdpConnection_Deliver(Connection, Data, DataLength);
        while (NT_SUCCESS(Status) && !IsListEmpty(&Connection->ReceiveQueue))
        {
            Buffer = CONTAINING_RECORD(Connection->ReceiveQueue.Flink,
                                       ZP_UDP_BUFFER,
                                       ListEntry);
            if (Buffer->Sequence != Connection->NextReceiveSequence)
            {
                break;
            }
            RemoveEntryList(&Buffer->ListEntry);
            Connection->ReceiveQueueCount--;
            Status = ZpUdpConnection_Deliver(Connection,
                                             Buffer->Data,
                                             Buffer->DataLength);
            Mem_Free(Buffer);
        }
        return Status;
    }
    if (Connection->ReceiveQueueCount == ZP_UDP_MAX_REORDERED_PACKETS)
    {
        return STATUS_SUCCESS;
    }
    for (Entry = Connection->ReceiveQueue.Flink;
         Entry != &Connection->ReceiveQueue;
         Entry = Entry->Flink)
    {
        Buffer = CONTAINING_RECORD(Entry, ZP_UDP_BUFFER, ListEntry);
        if (Buffer->Sequence == Sequence)
        {
            return STATUS_SUCCESS;
        }
        if (Buffer->Sequence > Sequence)
        {
            break;
        }
    }
    Buffer = Mem_Alloc(FIELD_OFFSET(ZP_UDP_BUFFER, Data) + DataLength);
    if (Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Buffer->Sequence = Sequence;
    Buffer->DataLength = DataLength;
    RtlCopyMemory(Buffer->Data, Data, DataLength);
    InsertTailList(Entry, &Buffer->ListEntry);
    Connection->ReceiveQueueCount++;
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpUdpConnection_ProcessData(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _Inout_updates_bytes_(DataLength) PVOID Data,
    _In_ ULONG DataLength)
{
    const BYTE* Plaintext;
    ULONG PlaintextLength, PayloadLength;
    ULONGLONG Sequence, Acknowledgment;
    NTSTATUS NtStatus;
    ZP_STATUS Status;

    Status = ZpDtls_Decrypt(&Connection->Dtls,
                            Data,
                            DataLength,
                            &Plaintext,
                            &PlaintextLength);
    if (!ZpStatus_IsSuccess(Status))
    {
        if (Status.Type == ZpStatusSecurity && Status.Code == SEC_E_OUT_OF_SEQUENCE)
        {
            return ZpStatus_FromNtStatus(STATUS_SUCCESS);
        }
        return Status;
    }
    if (PlaintextLength < ZP_UDP_INNER_HEADER_SIZE)
    {
        return ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    Sequence = ZpUdp_ReadUInt64(Plaintext);
    Acknowledgment = ZpUdp_ReadUInt64(Plaintext + 8);
    PayloadLength = PlaintextLength - ZP_UDP_INNER_HEADER_SIZE;
    if (Acknowledgment > Connection->NextSendSequence)
    {
        return ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    ZpUdpConnection_ProcessAcknowledgment(Connection, Acknowledgment);
    Status = ZpUdpConnection_Flush(Connection);
    if (!ZpStatus_IsSuccess(Status))
    {
        return Status;
    }
    if (PayloadLength != 0)
    {
        NtStatus = ZpUdpConnection_ProcessPayload(Connection,
                                                  Sequence,
                                                  Plaintext + ZP_UDP_INNER_HEADER_SIZE,
                                                  PayloadLength);
        if (NT_SUCCESS(NtStatus))
        {
            Status = ZpUdpConnection_SendReliable(Connection,
                                                  Connection->NextSendSequence,
                                                  NULL,
                                                  0);
        }
        else
        {
            Status = ZpStatus_FromNtStatus(NtStatus);
        }
    }
    return Status;
}

ZP_STATUS
ZpUdpConnection_ProcessDatagram(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    const BYTE* Buffer = Data;
    ULONGLONG ConnectionId;
    BYTE Type;
    ZP_STATUS Status;

    if (!ZpUdp_DecodeHeader(Data, DataLength, &Type, &ConnectionId) ||
        ConnectionId != Connection->ConnectionId)
    {
        return ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    RtlEnterCriticalSection(&Connection->Lock);
    if (Connection->Closed)
    {
        Status = ZpStatus_FromNtStatus(STATUS_CONNECTION_DISCONNECTED);
    }
    else
    {
        Connection->LastReceiveTickCount = GetTickCount64();
        if (Type == ZP_UDP_PACKET_HANDSHAKE)
        {
            Status = ZpUdpConnection_ProcessHandshake(Connection,
                                                      Buffer + ZP_UDP_HEADER_SIZE,
                                                      DataLength - ZP_UDP_HEADER_SIZE);
        }
        else if (Type == ZP_UDP_PACKET_ESTABLISHED &&
                 Connection->Dtls.Role == ZpDtlsClient &&
                 Connection->Dtls.HandshakeComplete)
        {
            if (!Connection->Connected)
            {
                Connection->Connected = TRUE;
                Status = Connection->ConnectedRoutine(Connection,
                                                      Connection->CallbackContext);
                if (!ZpStatus_IsSuccess(Status))
                {
                    Connection->Connected = FALSE;
                }
            }
            else
            {
                Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
            }
        }
        else if (Type == ZP_UDP_PACKET_DATA && Connection->Connected)
        {
            Connection->PeerStarted = TRUE;
            Status = ZpUdpConnection_ProcessData(Connection,
                                                 Buffer + ZP_UDP_HEADER_SIZE,
                                                 DataLength - ZP_UDP_HEADER_SIZE);
        }
        else
        {
            Status = ZpStatus_FromNtStatus(STATUS_PROTOCOL_UNREACHABLE);
        }
    }
    RtlLeaveCriticalSection(&Connection->Lock);
    return Status;
}

ZP_STATUS
ZpUdpConnection_Tick(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_ ULONGLONG TickCount)
{
    PLIST_ENTRY Entry;
    PZP_UDP_BUFFER Buffer;
    ZP_STATUS Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);

    RtlEnterCriticalSection(&Connection->Lock);
    if (Connection->Closed)
    {
        Status = ZpStatus_FromNtStatus(STATUS_CONNECTION_DISCONNECTED);
    }
    else if (!Connection->Connected)
    {
        if (TickCount - Connection->HandshakeStartTickCount >=
            ZP_UDP_HANDSHAKE_TIMEOUT_MILLISECONDS)
        {
            Status = ZpStatus_FromNtStatus(STATUS_IO_TIMEOUT);
        }
        else if (TickCount - Connection->HandshakeFlightTickCount >=
                 ZP_UDP_HANDSHAKE_RETRY_MILLISECONDS)
        {
            Status = Connection->Dtls.Role == ZpDtlsServer &&
                     Connection->Dtls.HandshakeComplete ?
                         ZpUdpConnection_SendPacket(Connection,
                                                    ZP_UDP_PACKET_ESTABLISHED,
                                                    NULL,
                                                    0) :
                         ZpUdpConnection_SendFlight(Connection);
        }
    }
    else if (TickCount - Connection->LastReceiveTickCount >=
             ZP_UDP_IDLE_TIMEOUT_MILLISECONDS)
    {
        Status = ZpStatus_FromNtStatus(STATUS_IO_TIMEOUT);
    }
    else
    {
        for (Entry = Connection->SendQueue.Flink;
             Entry != &Connection->SendQueue;
             Entry = Entry->Flink)
        {
            Buffer = CONTAINING_RECORD(Entry, ZP_UDP_BUFFER, ListEntry);
            if (Buffer->SentTickCount == 0)
            {
                continue;
            }
            if (TickCount - Buffer->SentTickCount < ZP_UDP_RETRY_MILLISECONDS)
            {
                continue;
            }
            if (Buffer->RetryCount == ZP_UDP_MAX_RETRIES)
            {
                Status = ZpStatus_FromNtStatus(STATUS_IO_TIMEOUT);
                break;
            }
            Status = ZpUdpConnection_SendReliable(Connection,
                                                  Buffer->Sequence,
                                                  Buffer->Data,
                                                  Buffer->DataLength);
            if (!ZpStatus_IsSuccess(Status))
            {
                break;
            }
            Buffer->SentTickCount = TickCount;
            Buffer->RetryCount++;
        }
        if (ZpStatus_IsSuccess(Status) &&
            !IsListEmpty(&Connection->SendQueue))
        {
            Status = ZpUdpConnection_Flush(Connection);
        }
        if (ZpStatus_IsSuccess(Status) &&
            TickCount - Connection->LastSendTickCount >= ZP_UDP_KEEP_ALIVE_MILLISECONDS)
        {
            Status = ZpUdpConnection_SendReliable(Connection,
                                                  Connection->NextSendSequence,
                                                  NULL,
                                                  0);
        }
    }
    RtlLeaveCriticalSection(&Connection->Lock);
    return Status;
}

static
ULONG
ZpUdp_GetRemainingMilliseconds(
    _In_ ULONGLONG Deadline,
    _In_ ULONGLONG TickCount)
{
    ULONGLONG Remaining;

    if (Deadline <= TickCount)
    {
        return 0;
    }
    Remaining = Deadline - TickCount;
    return Remaining > MAXULONG ? MAXULONG : (ULONG)Remaining;
}

ULONG
ZpUdpConnection_GetWaitMilliseconds(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_ ULONGLONG TickCount)
{
    PLIST_ENTRY Entry;
    ULONG Wait;

    RtlEnterCriticalSection(&Connection->Lock);
    if (Connection->Closed)
    {
        Wait = 0;
    }
    else if (!Connection->Connected)
    {
        Wait = min(ZpUdp_GetRemainingMilliseconds(
                       Connection->HandshakeStartTickCount + ZP_UDP_HANDSHAKE_TIMEOUT_MILLISECONDS,
                       TickCount),
                   ZpUdp_GetRemainingMilliseconds(
                       Connection->HandshakeFlightTickCount + ZP_UDP_HANDSHAKE_RETRY_MILLISECONDS,
                       TickCount));
    }
    else
    {
        Wait = min(ZpUdp_GetRemainingMilliseconds(
                       Connection->LastReceiveTickCount + ZP_UDP_IDLE_TIMEOUT_MILLISECONDS,
                       TickCount),
                   ZpUdp_GetRemainingMilliseconds(
                       Connection->LastSendTickCount + ZP_UDP_KEEP_ALIVE_MILLISECONDS,
                       TickCount));
        for (Entry = Connection->SendQueue.Flink;
             Entry != &Connection->SendQueue;
             Entry = Entry->Flink)
        {
            PZP_UDP_BUFFER Buffer = CONTAINING_RECORD(Entry, ZP_UDP_BUFFER, ListEntry);

            if (Buffer->SentTickCount != 0)
            {
                Wait = min(Wait,
                           ZpUdp_GetRemainingMilliseconds(
                               Buffer->SentTickCount + ZP_UDP_RETRY_MILLISECONDS,
                               TickCount));
            }
        }
    }
    RtlLeaveCriticalSection(&Connection->Lock);
    return Wait;
}

VOID
ZpUdpConnection_Close(
    _Inout_ PZP_UDP_CONNECTION Connection)
{
    RtlEnterCriticalSection(&Connection->Lock);
    Connection->Closed = TRUE;
    RtlLeaveCriticalSection(&Connection->Lock);
}

NTSTATUS
ZpUdpConnection_SendFrame(
    _Inout_ PZP_UDP_CONNECTION UdpConnection,
    _Inout_ PZP_CONNECTION ProtocolConnection,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    LIST_ENTRY Buffers;
    PZP_UDP_BUFFER Buffer;
    BYTE Header[sizeof(ULONG) + sizeof(BYTE)];
    ULONG FrameSize, Offset = 0, Chunk, HeaderChunk, MaximumPayload;
    NTSTATUS Status;
    ZP_STATUS TransportStatus = ZpStatus_FromNtStatus(STATUS_SUCCESS);

    Status = ZpFrame_Encode(MessageType, Body, BodyLength, NULL, 0, &FrameSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    ZpUdp_WriteUInt32(Header, sizeof(BYTE) + BodyLength);
    Header[sizeof(ULONG)] = (BYTE)MessageType;
    InitializeListHead(&Buffers);
    RtlEnterCriticalSection(&UdpConnection->Lock);
    MaximumPayload = UdpConnection->Dtls.StreamSizes.cbHeader +
                     UdpConnection->Dtls.StreamSizes.cbTrailer + ZP_UDP_INNER_HEADER_SIZE <
                     ZP_DTLS_MTU ?
                         ZP_DTLS_MTU - UdpConnection->Dtls.StreamSizes.cbHeader -
                             UdpConnection->Dtls.StreamSizes.cbTrailer - ZP_UDP_INNER_HEADER_SIZE :
                         0;
    if (!UdpConnection->Connected || UdpConnection->Closed)
    {
        Status = STATUS_CONNECTION_DISCONNECTED;
    }
    else if (FrameSize > ZP_UDP_MAX_PENDING_BYTES - UdpConnection->PendingBytes ||
             MaximumPayload == 0)
    {
        Status = STATUS_DEVICE_BUSY;
    }
    else
    {
        while (Offset < FrameSize)
        {
            Chunk = min(FrameSize - Offset, MaximumPayload);
            Buffer = Mem_Alloc(FIELD_OFFSET(ZP_UDP_BUFFER, Data) + Chunk);
            if (Buffer == NULL)
            {
                Status = STATUS_NO_MEMORY;
                break;
            }
            Buffer->Sequence = UdpConnection->NextSendSequence++;
            Buffer->DataLength = Chunk;
            Buffer->RetryCount = 0;
            Buffer->SentTickCount = 0;
            HeaderChunk = Offset < sizeof(Header) ? min(Chunk, sizeof(Header) - Offset) : 0;
            if (HeaderChunk != 0)
            {
                RtlCopyMemory(Buffer->Data, Header + Offset, HeaderChunk);
            }
            if (Chunk != HeaderChunk)
            {
                RtlCopyMemory(Buffer->Data + HeaderChunk,
                              Add2Ptr(Body, Offset + HeaderChunk - sizeof(Header)),
                              Chunk - HeaderChunk);
            }
            InsertTailList(&Buffers, &Buffer->ListEntry);
            Offset += Chunk;
        }
        if (!NT_SUCCESS(Status))
        {
            UdpConnection->NextSendSequence -= Offset == 0 ? 0 :
                (Offset + MaximumPayload - 1) / MaximumPayload;
        }
    }
    if (NT_SUCCESS(Status))
    {
        while (!IsListEmpty(&Buffers))
        {
            Buffer = CONTAINING_RECORD(RemoveHeadList(&Buffers),
                                       ZP_UDP_BUFFER,
                                       ListEntry);
            UdpConnection->PendingBytes += Buffer->DataLength;
            InsertTailList(&UdpConnection->SendQueue, &Buffer->ListEntry);
        }
        TransportStatus = ZpUdpConnection_Flush(UdpConnection);
        if (!ZpStatus_IsSuccess(TransportStatus))
        {
            Status = STATUS_CONNECTION_DISCONNECTED;
        }
    }
    ZpUdp_FreeList(&Buffers);
    RtlLeaveCriticalSection(&UdpConnection->Lock);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    return ZpConnection_NotifyMessageSent(ProtocolConnection, MessageType);
}

VOID
ZpUdpConnection_Uninitialize(
    _Inout_ PZP_UDP_CONNECTION Connection)
{
    RtlEnterCriticalSection(&Connection->Lock);
    Connection->Closed = TRUE;
    ZpUdp_FreeList(&Connection->HandshakeFlight);
    ZpUdp_FreeList(&Connection->SendQueue);
    ZpUdp_FreeList(&Connection->ReceiveQueue);
    ZpDtls_Uninitialize(&Connection->Dtls);
    RtlLeaveCriticalSection(&Connection->Lock);
    RtlDeleteCriticalSection(&Connection->Lock);
}
