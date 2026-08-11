#pragma once

#include <KNSoft/ZPigeon/Server.h>

typedef struct _ZP_CONNECTION_OBJECT ZP_CONNECTION_OBJECT, *PZP_CONNECTION_OBJECT;

typedef
NTSTATUS
(NTAPI *ZP_CONNECTION_SEND_ROUTINE)(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength);

typedef
VOID
(NTAPI *ZP_CONNECTION_DESTROY_ROUTINE)(
    _Inout_ PZP_CONNECTION_OBJECT Connection);

struct _ZP_CONNECTION_OBJECT
{
    RTL_SRWLOCK Lock;
    LIST_ENTRY Requests;
    LIST_ENTRY Channels;
    ULONGLONG NextRequestId;
    ULONGLONG HighestChannelId;
    PTP_TIMER RequestTimer;
    ZP_CONNECTION_PHASE Phase;
    ULONG RequestCount;
    ULONG MaxRequests;
    ULONG ChannelCount;
    ULONG ChannelReservations;
    ULONG MaxChannels;
    PCZP_MODULE_RECORD Modules;
    USHORT ModuleCount;
    ZP_CONNECTION_SEND_ROUTINE Send;
    volatile LONG ReferenceCount;
    ZP_CONNECTION_DESTROY_ROUTINE Destroy;
};

VOID
ZpServerConnection_Initialize(
    _Out_ PZP_CONNECTION_OBJECT Connection,
    _In_ ULONG MaxRequests,
    _In_ ULONG MaxChannels,
    _In_ ZP_CONNECTION_SEND_ROUTINE Send,
    _In_ ZP_CONNECTION_DESTROY_ROUTINE Destroy);

VOID
ZpServerConnection_SetPhase(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_CONNECTION_PHASE Phase);

VOID
ZpServerConnection_SetModules(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_reads_(ModuleCount) PCZP_MODULE_RECORD Modules,
    _In_ USHORT ModuleCount);

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
    _In_ ULONGLONG ChannelId,
    _In_ ULONG CreditBytes);

VOID
ZpServerConnection_Close(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_STATUS Status);
