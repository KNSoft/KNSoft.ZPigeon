#pragma once

#include "Dtls.h"
#include "Connection.h"

#define ZP_UDP_HEADER_SIZE 16UL
#define ZP_UDP_MAX_DATAGRAM_SIZE (ZP_UDP_HEADER_SIZE + ZP_DTLS_MTU)

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
    ULONGLONG NextSendSequence;
    ULONGLONG NextReceiveSequence;
    ULONGLONG LastReceiveTickCount;
    ULONGLONG LastSendTickCount;
    ULONGLONG HandshakeStartTickCount;
    ULONGLONG HandshakeFlightTickCount;
    SIZE_T PendingBytes;
    ULONG ReceiveQueueCount;
    LOGICAL Connected;
    LOGICAL PeerStarted;
    LOGICAL Closed;
};

ZP_STATUS
ZpUdp_ResolveAddress(
    _In_ PCWSTR Host,
    _In_ USHORT Port,
    _In_ LOGICAL Passive,
    _Out_ PSOCKADDR_STORAGE Address,
    _Out_ PINT AddressLength);

LOGICAL
ZpUdp_DecodeHeader(
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
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
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength);

ZP_STATUS
ZpUdpConnection_Tick(
    _Inout_ PZP_UDP_CONNECTION Connection,
    _In_ ULONGLONG TickCount);

VOID
ZpUdpConnection_Close(
    _Inout_ PZP_UDP_CONNECTION Connection);

NTSTATUS
ZpUdpConnection_SendFrame(
    _Inout_ PZP_UDP_CONNECTION UdpConnection,
    _Inout_ PZP_CONNECTION ProtocolConnection,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength);

VOID
ZpUdpConnection_Uninitialize(
    _Inout_ PZP_UDP_CONNECTION Connection);
