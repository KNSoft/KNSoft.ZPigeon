#pragma once

#include "../Client.inl"

typedef struct _ZP_CLIENT_LOCAL_CHANNEL ZP_CLIENT_LOCAL_CHANNEL,
  *PZP_CLIENT_LOCAL_CHANNEL;

typedef
NTSTATUS
(*ZP_CLIENT_LOCAL_CHANNEL_DATA_ROUTINE)(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message);

typedef
NTSTATUS
(*ZP_CLIENT_LOCAL_CHANNEL_WINDOW_ROUTINE)(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ ULONG CreditBytes);

typedef
NTSTATUS
(*ZP_CLIENT_LOCAL_CHANNEL_CLOSE_ROUTINE)(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ ZP_STATUS Status);

typedef
VOID
(*ZP_CLIENT_LOCAL_CHANNEL_COMMIT_ROUTINE)(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ LOGICAL ResponseSent);

typedef
VOID
(*ZP_CLIENT_LOCAL_CHANNEL_ABORT_ROUTINE)(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ ZP_STATUS Status);

typedef
VOID
(*ZP_CLIENT_LOCAL_CHANNEL_DESTROY_ROUTINE)(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel);

struct _ZP_CLIENT_LOCAL_CHANNEL
{
    LIST_ENTRY ListEntry;
    LIST_ENTRY BucketEntry;
    PZP_CLIENT_OBJECT Owner;
    volatile LONG ReferenceCount;
    volatile LONG Pending;
    ULONG ChannelId;
    BYTE ModuleId;
    ZP_CLIENT_LOCAL_CHANNEL_DATA_ROUTINE ReceiveData;
    ZP_CLIENT_LOCAL_CHANNEL_WINDOW_ROUTINE ReceiveWindow;
    ZP_CLIENT_LOCAL_CHANNEL_CLOSE_ROUTINE ReceiveClose;
    ZP_CLIENT_LOCAL_CHANNEL_COMMIT_ROUTINE Commit;
    ZP_CLIENT_LOCAL_CHANNEL_ABORT_ROUTINE Abort;
    ZP_CLIENT_LOCAL_CHANNEL_DESTROY_ROUTINE Destroy;
};

NTSTATUS
ZpClientLocalChannel_Insert(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ BYTE ModuleId,
    _In_opt_ ZP_CLIENT_LOCAL_CHANNEL_DATA_ROUTINE ReceiveData,
    _In_opt_ ZP_CLIENT_LOCAL_CHANNEL_WINDOW_ROUTINE ReceiveWindow,
    _In_ ZP_CLIENT_LOCAL_CHANNEL_CLOSE_ROUTINE ReceiveClose,
    _In_ ZP_CLIENT_LOCAL_CHANNEL_COMMIT_ROUTINE Commit,
    _In_ ZP_CLIENT_LOCAL_CHANNEL_ABORT_ROUTINE Abort,
    _In_ ZP_CLIENT_LOCAL_CHANNEL_DESTROY_ROUTINE Destroy);

_Success_(NT_SUCCESS(return))
NTSTATUS
ZpClientLocalChannel_ReferenceById(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ULONG ChannelId,
    _In_ BYTE ModuleId,
    _Out_ PZP_CLIENT_LOCAL_CHANNEL* Channel);

LOGICAL
ZpClientLocalChannel_RemoveLocked(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel);

FORCEINLINE
VOID
ZpClientLocalChannel_AddRef(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel)
{
    InterlockedIncrement(&Channel->ReferenceCount);
}

FORCEINLINE
VOID
ZpClientLocalChannel_Release(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel)
{
    if (InterlockedDecrement(&Channel->ReferenceCount) == 0)
    {
        Channel->Destroy(Channel);
    }
}

NTSTATUS
ZpClientLocalChannel_ReceiveData(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message);

NTSTATUS
ZpClientLocalChannel_ReceiveWindow(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ULONG ChannelId,
    _In_ ULONG CreditBytes);

NTSTATUS
ZpClientLocalChannel_ReceiveClose(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ const ZP_CHANNEL_CLOSE* Message);

VOID
ZpClientLocalChannel_CloseAll(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_STATUS Status);
