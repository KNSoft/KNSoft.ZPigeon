#include "Udp.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#pragma comment(lib, "Ws2_32.lib")

#define ZP_UDP_MAGIC 0x44505A4BUL
#define ZP_UDP_INNER_HEADER_SIZE (2 * sizeof(ULONGLONG))
#define ZP_UDP_MAX_PENDING_BYTES (ZP_FRAME_MAX_BODY_SIZE + sizeof(ULONG))
#define ZP_UDP_MAX_REORDERED_PACKETS 64
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
    PBYTE Cursor = Buffer;

    ZpUdp_WriteUInt32(Cursor, ZP_UDP_MAGIC);
    Cursor += sizeof(ULONG);
    *Cursor++ = Type;
    ZpUdp_WriteUInt64(Cursor, ConnectionId);
}

_Success_(return != FALSE)
LOGICAL
ZpUdp_DecodeHeader(
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_ PBYTE Type,
    _Out_ PULONGLONG ConnectionId)
{
    const BYTE* Buffer = Data;
    const BYTE* Cursor;

    if (DataLength < ZP_UDP_HEADER_SIZE || ZpUdp_ReadUInt32(Buffer) != ZP_UDP_MAGIC)
    {
        return FALSE;
    }
    Cursor = Buffer + sizeof(ULONG);
    *Type = *Cursor++;
    if (*Type < ZP_UDP_PACKET_HANDSHAKE || *Type > ZP_UDP_PACKET_DATA)
    {
        return FALSE;
    }
    *ConnectionId = ZpUdp_ReadUInt64(Cursor);
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
    ULONG Count = 0;

    while (Count < Connection->InFlightCount &&
           Connection->InFlight[Count].Sequence < Acknowledgment)
    {
        PZP_UDP_INFLIGHT InFlight = &Connection->InFlight[Count++];

        InFlight->Frame->AcknowledgedBytes += InFlight->Length;
        Connection->PendingBytes -= InFlight->Length;
    }
    if (Count != 0)
    {
        Connection->InFlightCount -= Count;
        RtlMoveMemory(Connection->InFlight,
                      Connection->InFlight + Count,
                      Connection->InFlightCount * sizeof(*Connection->InFlight));
    }
    while (!IsListEmpty(&Connection->SendQueue))
    {
        PZP_UDP_SEND_FRAME Frame = CONTAINING_RECORD(Connection->SendQueue.Flink,
                                                     ZP_UDP_SEND_FRAME,
                                                     ListEntry);

        if (Frame->AcknowledgedBytes != Frame->Length)
        {
            break;
        }
        RemoveEntryList(&Frame->ListEntry);
        if (FlagOn(Frame->Flags, ZP_SEND_FLAG_SENSITIVE))
        {
            RtlSecureZeroMemory(Frame->Data, Frame->Length);
        }
        ZpConnection_CompleteSend(Frame->ProtocolConnection, Frame->Length);
        Mem_Free(Frame);
    }
}

static
VOID
ZpUdp_FreeSendFrames(
    _Inout_ PLIST_ENTRY List)
{
    while (!IsListEmpty(List))
    {
        PZP_UDP_SEND_FRAME Frame = CONTAINING_RECORD(RemoveHeadList(List),
                                                     ZP_UDP_SEND_FRAME,
                                                     ListEntry);

        if (FlagOn(Frame->Flags, ZP_SEND_FLAG_SENSITIVE))
        {
            RtlSecureZeroMemory(Frame->Data, Frame->Length);
        }
        ZpConnection_CompleteSend(Frame->ProtocolConnection, Frame->Length);
        Mem_Free(Frame);
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
    PBYTE Cursor = Plaintext;
    ULONG EncryptedLength;
    ZP_STATUS Status;

    ZpUdp_WriteUInt64(Cursor, Sequence);
    Cursor += sizeof(ULONGLONG);
    ZpUdp_WriteUInt64(Cursor, Connection->NextReceiveSequence);
    Cursor += sizeof(ULONGLONG);
    if (DataLength != 0)
    {
        RtlCopyMemory(Cursor, Data, DataLength);
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
    PZP_UDP_SEND_FRAME Frame;
    PZP_UDP_INFLIGHT InFlight;
    ULONG MaximumPayload;
    ZP_STATUS Status;

    MaximumPayload = Connection->Dtls.StreamSizes.cbHeader +
                     Connection->Dtls.StreamSizes.cbTrailer + ZP_UDP_INNER_HEADER_SIZE < ZP_DTLS_MTU ?
                         ZP_DTLS_MTU - Connection->Dtls.StreamSizes.cbHeader -
                             Connection->Dtls.StreamSizes.cbTrailer - ZP_UDP_INNER_HEADER_SIZE : 0;
    if (MaximumPayload == 0)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_BUFFER_SIZE);
    }
    for (Entry = Connection->SendQueue.Flink;
         Entry != &Connection->SendQueue && Connection->InFlightCount < ZP_UDP_SEND_WINDOW_PACKETS;
         Entry = Entry->Flink)
    {
        Frame = CONTAINING_RECORD(Entry, ZP_UDP_SEND_FRAME, ListEntry);
        while (Frame->NextOffset < Frame->Length &&
               Connection->InFlightCount < ZP_UDP_SEND_WINDOW_PACKETS)
        {
            if (Frame->NextOffset == 0)
            {
                ZpConnection_RecordSendQueueDelay(Frame->ProtocolConnection,
                                                  Frame->EnqueuedTickCount);
            }
            InFlight = &Connection->InFlight[Connection->InFlightCount];
            InFlight->Frame = Frame;
            InFlight->Sequence = Connection->NextSendSequence;
            InFlight->Offset = Frame->NextOffset;
            InFlight->Length = min(Frame->Length - Frame->NextOffset, MaximumPayload);
            Status = ZpUdpConnection_SendReliable(Connection,
                                                  InFlight->Sequence,
                                                  Frame->Data + InFlight->Offset,
                                                  InFlight->Length);
            if (!ZpStatus_IsSuccess(Status))
            {
                return Status;
            }
            InFlight->SentTickCount = GetTickCount64();
            InFlight->RetryCount = 0;
            Frame->NextOffset += InFlight->Length;
            Connection->NextSendSequence++;
            Connection->InFlightCount++;
        }
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
        return STATUS_SUCCESS;
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
    const BYTE* Cursor;
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
        return ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }
    if (PlaintextLength < ZP_UDP_INNER_HEADER_SIZE)
    {
        return ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }
    Cursor = Plaintext;
    Sequence = ZpUdp_ReadUInt64(Cursor);
    Cursor += sizeof(ULONGLONG);
    Acknowledgment = ZpUdp_ReadUInt64(Cursor);
    Cursor += sizeof(ULONGLONG);
    PayloadLength = PlaintextLength - ZP_UDP_INNER_HEADER_SIZE;
    if (Acknowledgment > Connection->NextSendSequence)
    {
        return ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }
    Connection->LastReceiveTickCount = GetTickCount64();
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
                                                  Cursor,
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
    _Inout_updates_bytes_(DataLength) PVOID Data,
    _In_ ULONG DataLength)
{
    PBYTE Buffer = Data;
    ULONGLONG ConnectionId;
    BYTE Type;
    ZP_STATUS Status;

    if (!ZpUdp_DecodeHeader(Data, DataLength, &Type, &ConnectionId) ||
        ConnectionId != Connection->ConnectionId)
    {
        return ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }
    RtlEnterCriticalSection(&Connection->Lock);
    if (Connection->Closed)
    {
        Status = Connection->CloseStatus;
    }
    else
    {
        if (Type == ZP_UDP_PACKET_HANDSHAKE)
        {
            Status = ZpUdpConnection_ProcessHandshake(Connection,
                                                      Buffer + ZP_UDP_HEADER_SIZE,
                                                      DataLength - ZP_UDP_HEADER_SIZE);
            if (ZpStatus_IsSuccess(Status)) Connection->LastReceiveTickCount = GetTickCount64();
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
            Connection->LastReceiveTickCount = GetTickCount64();
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
            Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
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
    PZP_UDP_INFLIGHT InFlight;
    ULONG Index;
    ZP_STATUS Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);

    RtlEnterCriticalSection(&Connection->Lock);
    if (Connection->Closed)
    {
        Status = Connection->CloseStatus;
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
        for (Index = 0; Index < Connection->InFlightCount; Index++)
        {
            InFlight = &Connection->InFlight[Index];
            if (TickCount - InFlight->SentTickCount < ZP_UDP_RETRY_MILLISECONDS)
            {
                continue;
            }
            if (InFlight->RetryCount == ZP_UDP_MAX_RETRIES)
            {
                Status = ZpStatus_FromNtStatus(STATUS_IO_TIMEOUT);
                break;
            }
            Status = ZpUdpConnection_SendReliable(Connection,
                                                  InFlight->Sequence,
                                                  InFlight->Frame->Data + InFlight->Offset,
                                                  InFlight->Length);
            if (!ZpStatus_IsSuccess(Status))
            {
                break;
            }
            InFlight->SentTickCount = TickCount;
            InFlight->RetryCount++;
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
    ULONG Index, RetryWait, Wait;

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
        if (Connection->InFlightCount != 0)
        {
            for (Index = 0; Index < Connection->InFlightCount; Index++)
            {
                RetryWait = ZpUdp_GetRemainingMilliseconds(
                    Connection->InFlight[Index].SentTickCount + ZP_UDP_RETRY_MILLISECONDS,
                    TickCount);
                Wait = min(Wait, RetryWait);
            }
        }
    }
    RtlLeaveCriticalSection(&Connection->Lock);
    return Wait;
}

VOID
ZpUdpConnection_Close(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_ ZP_STATUS Status)
{
    RtlEnterCriticalSection(&Connection->Lock);
    if (!Connection->Closed)
    {
        Connection->CloseStatus = Status;
        Connection->Closed = TRUE;
    }
    RtlLeaveCriticalSection(&Connection->Lock);
}

NTSTATUS
ZpUdpConnection_SendFrame(
    _Inout_ PZP_UDP_CONNECTION UdpConnection,
    _Inout_ PZP_CONNECTION ProtocolConnection,
    _In_ ZP_SEND_FLAGS SendFlags,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength)
{
    ZP_SEND_BUFFER Buffer;
    PZP_UDP_SEND_FRAME Frame;
    ULONG FrameLength;
    NTSTATUS Status;
    ZP_STATUS TransportStatus = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    LOGICAL Queued = FALSE;

    Status = ZpConnection_EncodeFrame(ProtocolConnection,
                                     SendFlags,
                                     MessageType,
                                     Body,
                                     BodyLength,
                                     Payload,
                                     PayloadLength,
                                     FIELD_OFFSET(ZP_UDP_SEND_FRAME, Data),
                                     &Buffer);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Frame = (PZP_UDP_SEND_FRAME)Buffer.Allocation;
    Status = ZpConnection_ReserveSend(ProtocolConnection, Buffer.Length);
    if (!NT_SUCCESS(Status))
    {
        ZpSendBuffer_Release(&Buffer);
        return Status;
    }
    Frame->ProtocolConnection = ProtocolConnection;
    Frame->EnqueuedTickCount = GetTickCount64();
    Frame->Length = Buffer.Length;
    FrameLength = Buffer.Length;
    Frame->NextOffset = 0;
    Frame->AcknowledgedBytes = 0;
    Frame->Flags = Buffer.Flags;
    RtlEnterCriticalSection(&UdpConnection->Lock);
    if (!UdpConnection->Connected || UdpConnection->Closed)
    {
        Status = STATUS_CONNECTION_DISCONNECTED;
    }
    else if (Frame->Length > ZP_UDP_MAX_PENDING_BYTES - UdpConnection->PendingBytes)
    {
        Status = STATUS_DEVICE_BUSY;
    }
    else
    {
        UdpConnection->PendingBytes += Frame->Length;
        InsertTailList(&UdpConnection->SendQueue, &Frame->ListEntry);
        Queued = TRUE;
        TransportStatus = ZpUdpConnection_Flush(UdpConnection);
        if (!ZpStatus_IsSuccess(TransportStatus))
        {
            UdpConnection->CloseStatus = TransportStatus;
            UdpConnection->Closed = TRUE;
            Status = STATUS_CONNECTION_DISCONNECTED;
        }
    }
    RtlLeaveCriticalSection(&UdpConnection->Lock);
    if (!NT_SUCCESS(Status))
    {
        if (!Queued)
        {
            ZpConnection_CompleteSend(ProtocolConnection, Buffer.Length);
            ZpSendBuffer_Release(&Buffer);
        }
        return Status;
    }
    return ZpConnection_NotifyMessageSent(ProtocolConnection, MessageType, FrameLength);
}

VOID
ZpUdpConnection_Uninitialize(
    _Inout_ PZP_UDP_CONNECTION Connection)
{
    RtlEnterCriticalSection(&Connection->Lock);
    Connection->Closed = TRUE;
    ZpUdp_FreeList(&Connection->HandshakeFlight);
    ZpUdp_FreeSendFrames(&Connection->SendQueue);
    ZpUdp_FreeList(&Connection->ReceiveQueue);
    ZpDtls_Uninitialize(&Connection->Dtls);
    RtlLeaveCriticalSection(&Connection->Lock);
    RtlDeleteCriticalSection(&Connection->Lock);
}
