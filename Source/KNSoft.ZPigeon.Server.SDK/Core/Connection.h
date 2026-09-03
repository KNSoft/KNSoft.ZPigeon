#pragma once

#include <KNSoft/ZPigeon/Server.h>

#include "../../Network/Connection.h"

#define ZP_CONNECTION_LOOKUP_BUCKET_COUNT 16

typedef struct _ZP_CONNECTION_OBJECT ZP_CONNECTION_OBJECT, *PZP_CONNECTION_OBJECT;

typedef
NTSTATUS
(NTAPI *ZP_CONNECTION_SEND_ROUTINE)(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_SEND_FLAGS SendFlags,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength);

typedef
NTSTATUS
(NTAPI *ZP_CONNECTION_DISCONNECT_ROUTINE)(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_STATUS Status);

typedef
VOID
(NTAPI *ZP_CONNECTION_DESTROY_ROUTINE)(
    _Inout_ PZP_CONNECTION_OBJECT Connection);

struct _ZP_CONNECTION_OBJECT
{
    RTL_SRWLOCK Lock;
    RTL_CRITICAL_SECTION RequestSendLock;
    LIST_ENTRY Requests;
    LIST_ENTRY TimedRequests;
    LIST_ENTRY Channels;
    LIST_ENTRY RequestBuckets[ZP_CONNECTION_LOOKUP_BUCKET_COUNT];
    LIST_ENTRY ChannelBuckets[ZP_CONNECTION_LOOKUP_BUCKET_COUNT];
    ULONG NextRequestId;
    ULONG HighestChannelId;
    PTP_TIMER RequestTimer;
    ZP_CONNECTION_PHASE Phase;
    ULONG RequestCount;
    ULONG MaxRequests;
    ULONGLONG CompletedRequests;
    ULONGLONG FailedRequests;
    ULONGLONG SmoothedRequestMilliseconds;
    ULONG ConsecutiveFailures;
    ZP_TRANSPORT_TYPE Transport;
    PZP_CONNECTION ProtocolConnection;
    ULONG ChannelCount;
    ULONG ChannelReservations;
    ULONG MaxChannels;
    ZP_IP_ADDRESS RemoteAddress;
    ZP_CONNECTION_SEND_ROUTINE Send;
    ZP_CONNECTION_DISCONNECT_ROUTINE Disconnect;
    volatile LONG ReferenceCount;
    ZP_CONNECTION_DESTROY_ROUTINE Destroy;
};

NTSTATUS
ZpServerConnection_Initialize(
    _Out_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_TRANSPORT_TYPE Transport,
    _In_ ULONG MaxRequests,
    _In_ ULONG MaxChannels,
    _In_ const SOCKADDR* RemoteAddress,
    _In_ INT RemoteAddressLength,
    _In_ ZP_CONNECTION_SEND_ROUTINE Send,
    _In_ ZP_CONNECTION_DISCONNECT_ROUTINE Disconnect,
    _In_ ZP_CONNECTION_DESTROY_ROUTINE Destroy);

VOID
ZpServerConnection_SetPhase(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_CONNECTION_PHASE Phase);

NTSTATUS
ZpServerConnection_ReceiveResponse(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ PCZP_RESPONSE_VIEW Response);

NTSTATUS
ZpServerConnection_ReceiveChannelData(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message);

NTSTATUS
ZpServerConnection_ReceiveChannelClose(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ const ZP_CHANNEL_CLOSE* Message);

NTSTATUS
ZpServerConnection_ReceiveChannelWindow(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ULONG ChannelId,
    _In_ ULONG CreditBytes);

VOID
ZpServerConnection_Close(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_STATUS Status);
