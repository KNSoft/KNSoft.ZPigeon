#pragma once

#include "Connection.h"
#include "../../SDK/Channel.h"

typedef struct _ZP_SERVER_CHANNEL_OBJECT ZP_SERVER_CHANNEL_OBJECT,
  *PZP_SERVER_CHANNEL_OBJECT;

NTSTATUS
ZpServerChannel_Reserve(
    _Inout_ PZP_CONNECTION_OBJECT Connection);

VOID
ZpServerChannel_ReleaseReservation(
    _Inout_ PZP_CONNECTION_OBJECT Connection);

NTSTATUS
ZpServerChannel_Create(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ULONG ChannelId,
    _In_ BYTE ModuleId,
    _In_ LOGICAL BoundedReceive,
    _In_ ULONGLONG RemainingBytes,
    _In_ LOGICAL BoundedSend,
    _In_ ULONGLONG RemainingSendBytes,
    _In_opt_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_opt_ ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _In_ LOGICAL Reserved,
    _Out_ PZP_SERVER_CHANNEL_OBJECT* Channel);

NTSTATUS
ZpServerChannel_SendWindow(
    _Inout_ PZP_SERVER_CHANNEL_OBJECT Channel,
    _In_ ULONG CreditBytes);

NTSTATUS
ZpServerChannel_GetId(
    _In_ PZP_SERVER_CHANNEL_OBJECT Channel,
    _In_ PZP_CONNECTION_OBJECT Connection,
    _In_ BYTE ModuleId,
    _Out_ PULONG ChannelId);

VOID
ZpServerChannel_Abort(
    _Inout_ PZP_SERVER_CHANNEL_OBJECT Channel,
    _In_ ZP_STATUS Status);

VOID
ZpServerConnection_RejectChannel(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ULONG ChannelId,
    _In_ ZP_STATUS Status);

VOID
ZpServerConnection_CloseChannels(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_STATUS Status);
