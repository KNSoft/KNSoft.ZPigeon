#include "Channel.h"

static
PZP_CLIENT_LOCAL_CHANNEL
ZpClientLocalChannel_FindLocked(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ULONGLONG ChannelId)
{
    PZP_CLIENT_LOCAL_CHANNEL Channel;
    PLIST_ENTRY Entry;

    for (Entry = Object->LocalChannels.Flink;
         Entry != &Object->LocalChannels;
         Entry = Entry->Flink)
    {
        Channel = CONTAINING_RECORD(Entry,
                                    ZP_CLIENT_LOCAL_CHANNEL,
                                    ListEntry);
        if (Channel->ChannelId == ChannelId)
        {
            return Channel;
        }
    }
    return NULL;
}

NTSTATUS
ZpClientLocalChannel_ReferenceById(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ULONGLONG ChannelId,
    _In_ USHORT ModuleId,
    _Out_ PZP_CLIENT_LOCAL_CHANNEL* Channel)
{
    PZP_CLIENT_LOCAL_CHANNEL ChannelObject;
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    ChannelObject = ZpClientLocalChannel_FindLocked(Object, ChannelId);
    if (ChannelObject == NULL)
    {
        Status = ChannelId != 0 &&
                 ChannelId < Object->NextLocalChannelId ?
                     STATUS_SUCCESS : STATUS_PROTOCOL_UNREACHABLE;
    }
    else if (ModuleId != 0 && ChannelObject->ModuleId != ModuleId)
    {
        Status = STATUS_PROTOCOL_UNREACHABLE;
    }
    else
    {
        ZpClientLocalChannel_AddRef(ChannelObject);
        *Channel = ChannelObject;
        Status = STATUS_SUCCESS;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return Status;
}

NTSTATUS
ZpClientLocalChannel_Insert(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ USHORT ModuleId,
    _In_opt_ ZP_CLIENT_LOCAL_CHANNEL_DATA_ROUTINE ReceiveData,
    _In_opt_ ZP_CLIENT_LOCAL_CHANNEL_WINDOW_ROUTINE ReceiveWindow,
    _In_ ZP_CLIENT_LOCAL_CHANNEL_CLOSE_ROUTINE ReceiveClose,
    _In_ ZP_CLIENT_LOCAL_CHANNEL_ABORT_ROUTINE Abort,
    _In_ ZP_CLIENT_LOCAL_CHANNEL_DESTROY_ROUTINE Destroy)
{
    NTSTATUS Status;

    if (ModuleId == 0 || (ReceiveData == NULL && ReceiveWindow == NULL) ||
        ReceiveClose == NULL || Abort == NULL || Destroy == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Object->State != ZpClientStateReady)
    {
        Status = STATUS_CONNECTION_DISCONNECTED;
    }
    else if (Object->NextLocalChannelId == 0)
    {
        Status = STATUS_INTEGER_OVERFLOW;
    }
    else if (Object->LocalChannelCount ==
             Object->Config.MaxChannelsPerConnection)
    {
        Status = STATUS_QUOTA_EXCEEDED;
    }
    else
    {
        Channel->Owner = Object;
        Channel->ReferenceCount = 1;
        Channel->Pending = TRUE;
        Channel->ChannelId = Object->NextLocalChannelId;
        Channel->ModuleId = ModuleId;
        Channel->ReceiveData = ReceiveData;
        Channel->ReceiveWindow = ReceiveWindow;
        Channel->ReceiveClose = ReceiveClose;
        Channel->Abort = Abort;
        Channel->Destroy = Destroy;
        Object->NextLocalChannelId++;
        InsertTailList(&Object->LocalChannels, &Channel->ListEntry);
        Object->LocalChannelCount++;
        Status = STATUS_SUCCESS;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return Status;
}

LOGICAL
ZpClientLocalChannel_RemoveLocked(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel)
{
    if (!InterlockedExchange(&Channel->Pending, FALSE))
    {
        return FALSE;
    }
    RemoveEntryList(&Channel->ListEntry);
    Channel->Owner->LocalChannelCount--;
    return TRUE;
}

NTSTATUS
ZpClientLocalChannel_ReceiveData(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message)
{
    PZP_CLIENT_LOCAL_CHANNEL Channel = NULL;
    NTSTATUS Status;

    Status = ZpClientLocalChannel_ReferenceById(Object,
                                                Message->ChannelId,
                                                0,
                                                &Channel);
    if (NT_SUCCESS(Status) && Channel != NULL)
    {
        Status = Channel->ReceiveData != NULL ?
                     Channel->ReceiveData(Channel, Message) :
                     STATUS_PROTOCOL_UNREACHABLE;
        ZpClientLocalChannel_Release(Channel);
    }
    return Status;
}

NTSTATUS
ZpClientLocalChannel_ReceiveWindow(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ULONGLONG ChannelId,
    _In_ ULONG CreditBytes)
{
    PZP_CLIENT_LOCAL_CHANNEL Channel = NULL;
    NTSTATUS Status;

    Status = ZpClientLocalChannel_ReferenceById(Object,
                                                ChannelId,
                                                0,
                                                &Channel);
    if (NT_SUCCESS(Status) && Channel != NULL)
    {
        Status = Channel->ReceiveWindow != NULL ?
                     Channel->ReceiveWindow(Channel, CreditBytes) :
                     STATUS_PROTOCOL_UNREACHABLE;
        ZpClientLocalChannel_Release(Channel);
    }
    return Status;
}

NTSTATUS
ZpClientLocalChannel_ReceiveClose(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ const ZP_CHANNEL_CLOSE* Message)
{
    PZP_CLIENT_LOCAL_CHANNEL Channel = NULL;
    NTSTATUS Status;

    Status = ZpClientLocalChannel_ReferenceById(Object,
                                                Message->ChannelId,
                                                0,
                                                &Channel);
    if (NT_SUCCESS(Status) && Channel != NULL)
    {
        Status = Channel->ReceiveClose(Channel, Message->Status);
        ZpClientLocalChannel_Release(Channel);
    }
    return Status;
}

VOID
ZpClientLocalChannel_CloseAll(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ NTSTATUS Status)
{
    PZP_CLIENT_LOCAL_CHANNEL Channel;

    for (;;)
    {
        RtlAcquireSRWLockExclusive(&Object->Lock);
        if (IsListEmpty(&Object->LocalChannels))
        {
            RtlReleaseSRWLockExclusive(&Object->Lock);
            return;
        }
        Channel = CONTAINING_RECORD(Object->LocalChannels.Flink,
                                    ZP_CLIENT_LOCAL_CHANNEL,
                                    ListEntry);
        ZpClientLocalChannel_RemoveLocked(Channel);
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Channel->Abort(Channel, Status);
        ZpClientLocalChannel_Release(Channel);
    }
}
