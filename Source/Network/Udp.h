#pragma once

#include "Dtls.h"
#include "Connection.h"

#define ZP_UDP_HEADER_SIZE (sizeof(ULONG) + sizeof(BYTE) + sizeof(ULONGLONG))
#define ZP_UDP_MAX_DATAGRAM_SIZE (ZP_UDP_HEADER_SIZE + ZP_DTLS_MTU)
#define ZP_UDP_PACKET_HANDSHAKE 1
#define ZP_UDP_PACKET_ESTABLISHED 2
#define ZP_UDP_PACKET_DATA 3

typedef struct _ZP_UDP_CONNECTION ZP_UDP_CONNECTION, *PZP_UDP_CONNECTION;

typedef
ZP_STATUS
(NTAPI *ZP_UDP_CONNECTED_ROUTINE)(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_opt_ PVOID Context);

typedef
NTSTATUS
(NTAPI *ZP_UDP_RECEIVE_ROUTINE)(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context);

typedef struct _ZP_UDP_BUFFER
{
    LIST_ENTRY ListEntry;
    ULONGLONG Sequence;
    ULONGLONG SentTickCount;
    ULONG RetryCount;
    ULONG DataLength;
    BYTE Data[ANYSIZE_ARRAY];
} ZP_UDP_BUFFER, *PZP_UDP_BUFFER;

typedef struct _ZP_UDP_SEND_FRAME
{
    LIST_ENTRY ListEntry;
    PZP_CONNECTION ProtocolConnection;
    ULONGLONG EnqueuedTickCount;
    ULONG Length;
    ULONG NextOffset;
    ULONG AcknowledgedBytes;
    ZP_SEND_FLAGS Flags;
    BYTE Data[ANYSIZE_ARRAY];
} ZP_UDP_SEND_FRAME, *PZP_UDP_SEND_FRAME;

typedef struct _ZP_UDP_INFLIGHT
{
    PZP_UDP_SEND_FRAME Frame;
    ULONGLONG Sequence;
    ULONGLONG SentTickCount;
    ULONG Offset;
    ULONG Length;
    ULONG RetryCount;
} ZP_UDP_INFLIGHT, *PZP_UDP_INFLIGHT;

#define ZP_UDP_SEND_WINDOW_PACKETS 32

struct _ZP_UDP_CONNECTION
{
    RTL_CRITICAL_SECTION Lock;
    SOCKET Socket;
    SOCKADDR_STORAGE RemoteAddress;
    INT RemoteAddressLength;
    ULONGLONG ConnectionId;
    ZP_DTLS_CONTEXT Dtls;
    ZP_UDP_CONNECTED_ROUTINE ConnectedRoutine;
    ZP_UDP_RECEIVE_ROUTINE ReceiveRoutine;
    PVOID CallbackContext;
    LIST_ENTRY HandshakeFlight;
    LIST_ENTRY SendQueue;
    LIST_ENTRY ReceiveQueue;
    ZP_UDP_INFLIGHT InFlight[ZP_UDP_SEND_WINDOW_PACKETS];
    ULONG InFlightCount;
    ULONGLONG NextSendSequence;
    ULONGLONG NextReceiveSequence;
    ULONGLONG LastReceiveTickCount;
    ULONGLONG LastSendTickCount;
    ULONGLONG HandshakeStartTickCount;
    ULONGLONG HandshakeFlightTickCount;
    SIZE_T PendingBytes;
    ZP_STATUS CloseStatus;
    ULONG ReceiveQueueCount;
    BOOLEAN Connected;
    BOOLEAN PeerStarted;
    BOOLEAN Closed;
};

_Success_(return != FALSE)
LOGICAL
ZpUdp_DecodeHeader(
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_ PBYTE Type,
    _Out_ PULONGLONG ConnectionId);

LOGICAL
ZpUdp_IsSameAddress(
    _In_ const SOCKADDR_STORAGE* Left,
    _In_ INT LeftLength,
    _In_ const SOCKADDR_STORAGE* Right,
    _In_ INT RightLength);

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
    _In_opt_ PVOID CallbackContext);

ZP_STATUS
ZpUdpConnection_StartHandshake(
    _Inout_ PZP_UDP_CONNECTION Connection);

ZP_STATUS
ZpUdpConnection_ProcessDatagram(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _Inout_updates_bytes_(DataLength) PVOID Data,
    _In_ ULONG DataLength);

ZP_STATUS
ZpUdpConnection_Tick(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_ ULONGLONG TickCount);

ULONG
ZpUdpConnection_GetWaitMilliseconds(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_ ULONGLONG TickCount);

VOID
ZpUdpConnection_Close(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_ ZP_STATUS Status);

NTSTATUS
ZpUdpConnection_SendFrame(
    _Inout_ PZP_UDP_CONNECTION UdpConnection,
    _Inout_ PZP_CONNECTION ProtocolConnection,
    _In_ ZP_SEND_FLAGS SendFlags,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength);

VOID
ZpUdpConnection_Uninitialize(
    _Inout_ PZP_UDP_CONNECTION Connection);
