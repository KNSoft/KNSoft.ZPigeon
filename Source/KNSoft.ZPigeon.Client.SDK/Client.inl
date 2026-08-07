#pragma once

#include <KNSoft/ZPigeon/Client.h>

#include "../Network/Transport.h"
#include "Network/Quic.h"

typedef struct _ZP_REQUEST_OBJECT
{
    LIST_ENTRY ListEntry;
    struct _ZP_CLIENT_OBJECT* Owner;
    volatile LONG ReferenceCount;
    volatile LONG Pending;
    ULONGLONG RequestId;
    ULONGLONG DeadlineTickCount;
    ZP_REQUEST_COMPLETE_CALLBACK Callback;
    PVOID Context;
} ZP_REQUEST_OBJECT, *PZP_REQUEST_OBJECT;

typedef struct _ZP_CHANNEL_OBJECT
{
    LIST_ENTRY ListEntry;
    struct _ZP_CLIENT_OBJECT* Owner;
    volatile LONG ReferenceCount;
    volatile LONG Pending;
    ULONGLONG ChannelId;
    ULONGLONG ReceiveCredit;
    ULONGLONG SendCredit;
    ULONGLONG RemainingBytes;
    ULONGLONG RemainingSendBytes;
    LOGICAL BoundedReceive;
    LOGICAL BoundedSend;
    ZP_CHANNEL_DATA_CALLBACK DataCallback;
    ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback;
    ZP_CHANNEL_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_CHANNEL_OBJECT, *PZP_CHANNEL_OBJECT;

typedef struct _ZP_SUBSCRIPTION_OBJECT
{
    LIST_ENTRY ListEntry;
    struct _ZP_CLIENT_OBJECT* Owner;
    volatile LONG ReferenceCount;
    volatile LONG Pending;
    volatile LONG CancelPending;
    ULONGLONG SubscriptionId;
    ULONGLONG NextSequence;
    USHORT ModuleId;
    ZP_EVENT_LOG_RECORD_CALLBACK RecordCallback;
    ZP_EVENT_LOG_TERMINAL_CALLBACK TerminalCallback;
    PVOID Context;
} ZP_SUBSCRIPTION_OBJECT, *PZP_SUBSCRIPTION_OBJECT;

typedef struct _ZP_CLIENT_OBJECT
{
    RTL_SRWLOCK Lock;
    ZP_CLIENT_STATE State;
    ULONG CallbackCount;
    LIST_ENTRY Requests;
    LIST_ENTRY Channels;
    LIST_ENTRY Subscriptions;
    ULONGLONG HighestServerChannelId;
    ULONGLONG HighestServerSubscriptionId;
    ULONGLONG NextRequestId;
    PTP_TIMER RequestTimer;
    ZP_CLIENT_CONFIG Config;
    PCZP_TRANSPORT_OPERATIONS TransportOperations[ZpTransportWss + 1];
    PVOID TransportContexts[ZpTransportWss + 1];
    ZP_TRANSPORT_TYPE ActiveTransport;
    ULONG EndpointIndex;
    ULONG NextEndpointIndex;
    ULONG FailureRound;
    ULONGLONG ReadyTickCount;
    PTP_TIMER RetryTimer;
    LOGICAL StartPending;
    LOGICAL RetryPending;
    ULONG RetryDelay;
    ZP_CLIENT_QUIC_TRANSPORT QuicTransport;
} ZP_CLIENT_OBJECT, *PZP_CLIENT_OBJECT;

NTSTATUS
ZpClient_SetTransport(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_TRANSPORT_TYPE Transport,
    _In_ PCZP_TRANSPORT_OPERATIONS Operations,
    _In_opt_ PVOID Context);

VOID
ZpClient_TransportShutdown(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ NTSTATUS Status);

NTSTATUS
ZpClient_NotifyState(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ NTSTATUS Status);

NTSTATUS
ZpClient_NotifyPong(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONGLONG Token);

NTSTATUS
ZpClient_CompleteResponse(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ PCZP_RESPONSE_VIEW Response);

NTSTATUS
ZpClient_ReceiveEvent(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ PCZP_EVENT_VIEW Message);

NTSTATUS
ZpClient_ReceiveChannelData(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message);

NTSTATUS
ZpClient_ReceiveChannelClose(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ const ZP_CHANNEL_CLOSE* Message);

NTSTATUS
ZpClient_ReceiveChannelWindow(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONGLONG ChannelId,
    _In_ ULONG CreditBytes);
