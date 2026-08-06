#include "Channel.h"

static
PZP_CLIENT_LOCAL_CHANNEL
ZpClientLocalChannel_FindLocked(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ULONG ChannelId)
{
    PZP_CLIENT_LOCAL_CHANNEL Channel;
    PLIST_ENTRY Bucket = &Object->LocalChannelBuckets[ChannelId & (ZP_CLIENT_LOOKUP_BUCKET_COUNT - 1)];
    PLIST_ENTRY Entry;

    for (Entry = Bucket->Flink; Entry != Bucket; Entry = Entry->Flink)
    {
        Channel = CONTAINING_RECORD(Entry,
                                    ZP_CLIENT_LOCAL_CHANNEL,
                                    BucketEntry);
        if (Channel->ChannelId == ChannelId)
        {
            return Channel;
        }
    }
    return NULL;
}

static
VOID
ZpClientLocalChannel_SendCloseLocked(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ULONG ChannelId,
    _In_ NTSTATUS Status)
{
    BYTE Body[sizeof(ULONG) + ZP_STATUS_WIRE_SIZE];
    ULONG BodyLength;

    if (NT_SUCCESS(ZpMessage_EncodeChannelClose(ChannelId,
                                                ZpStatus_FromNtStatus(Status),
                                                Body,
                                                sizeof(Body),
                                                &BodyLength)))
    {
        ZpClient_SendLocked(Object, 0, ZpMessageChannelClose, Body, BodyLength, NULL, 0);
    }
}

static
VOID
ZpClientLocalChannel_Reject(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ULONG ChannelId,
    _In_ NTSTATUS Status)
{
    RtlAcquireSRWLockShared(&Object->Lock);
    ZpClientLocalChannel_SendCloseLocked(Object, ChannelId, Status);
    RtlReleaseSRWLockShared(&Object->Lock);
}

static
VOID
ZpClientLocalChannel_Fail(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ NTSTATUS Status,
    _In_ LOGICAL NotifyPeer)
{
    LOGICAL Removed;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(Channel);
    if (Removed && NotifyPeer)
    {
        ZpClientLocalChannel_SendCloseLocked(Object, Channel->ChannelId, Status);
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        Channel->Abort(Channel, ZpStatus_FromNtStatus(Status));
        ZpClientLocalChannel_Release(Channel);
    }
}

_Success_(NT_SUCCESS(return))
NTSTATUS
ZpClientLocalChannel_ReferenceById(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ULONG ChannelId,
    _In_ BYTE ModuleId,
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
                     STATUS_INVALID_HANDLE : STATUS_PROTOCOL_UNREACHABLE;
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
    _In_ BYTE ModuleId,
    _In_opt_ ZP_CLIENT_LOCAL_CHANNEL_DATA_ROUTINE ReceiveData,
    _In_opt_ ZP_CLIENT_LOCAL_CHANNEL_WINDOW_ROUTINE ReceiveWindow,
    _In_ ZP_CLIENT_LOCAL_CHANNEL_CLOSE_ROUTINE ReceiveClose,
    _In_ ZP_CLIENT_LOCAL_CHANNEL_COMMIT_ROUTINE Commit,
    _In_ ZP_CLIENT_LOCAL_CHANNEL_ABORT_ROUTINE Abort,
    _In_ ZP_CLIENT_LOCAL_CHANNEL_DESTROY_ROUTINE Destroy)
{
    NTSTATUS Status;

    if (ModuleId == 0 || (ReceiveData == NULL && ReceiveWindow == NULL) ||
        ReceiveClose == NULL || Commit == NULL || Abort == NULL || Destroy == NULL)
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
        Channel->Commit = Commit;
        Channel->Abort = Abort;
        Channel->Destroy = Destroy;
        Object->NextLocalChannelId++;
        InsertTailList(&Object->LocalChannels, &Channel->ListEntry);
        InsertTailList(&Object->LocalChannelBuckets[
                           Channel->ChannelId & (ZP_CLIENT_LOOKUP_BUCKET_COUNT - 1)],
                       &Channel->BucketEntry);
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
    RemoveEntryList(&Channel->BucketEntry);
    Channel->Owner->LocalChannelCount--;
    return TRUE;
}

NTSTATUS
ZpClientLocalChannel_ReceiveData(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message)
{
    PZP_CLIENT_LOCAL_CHANNEL Channel;
    NTSTATUS Status;

    Status = ZpClientLocalChannel_ReferenceById(Object,
                                                Message->ChannelId,
                                                0,
                                                &Channel);
    if (Status == STATUS_INVALID_HANDLE)
    {
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
    {
        ZpClientLocalChannel_Reject(Object, Message->ChannelId, Status);
        return STATUS_SUCCESS;
    }
    Status = Channel->ReceiveData != NULL ?
                 Channel->ReceiveData(Channel, Message) :
                 STATUS_PROTOCOL_UNREACHABLE;
    if (!NT_SUCCESS(Status))
    {
        ZpClientLocalChannel_Fail(Object, Channel, Status, TRUE);
    }
    ZpClientLocalChannel_Release(Channel);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpClientLocalChannel_ReceiveWindow(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ULONG ChannelId,
    _In_ ULONG CreditBytes)
{
    PZP_CLIENT_LOCAL_CHANNEL Channel;
    NTSTATUS Status;

    Status = ZpClientLocalChannel_ReferenceById(Object,
                                                ChannelId,
                                                0,
                                                &Channel);
    if (Status == STATUS_INVALID_HANDLE)
    {
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
    {
        ZpClientLocalChannel_Reject(Object, ChannelId, Status);
        return STATUS_SUCCESS;
    }
    Status = Channel->ReceiveWindow != NULL ?
                 Channel->ReceiveWindow(Channel, CreditBytes) :
                 STATUS_PROTOCOL_UNREACHABLE;
    if (!NT_SUCCESS(Status))
    {
        ZpClientLocalChannel_Fail(Object, Channel, Status, TRUE);
    }
    ZpClientLocalChannel_Release(Channel);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpClientLocalChannel_ReceiveClose(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ const ZP_CHANNEL_CLOSE* Message)
{
    PZP_CLIENT_LOCAL_CHANNEL Channel;
    NTSTATUS Status;

    Status = ZpClientLocalChannel_ReferenceById(Object,
                                                Message->ChannelId,
                                                0,
                                                &Channel);
    if (!NT_SUCCESS(Status))
    {
        return STATUS_SUCCESS;
    }
    Status = Channel->ReceiveClose(Channel, Message->Status);
    if (!NT_SUCCESS(Status))
    {
        ZpClientLocalChannel_Fail(Object, Channel, Status, FALSE);
    }
    ZpClientLocalChannel_Release(Channel);
    return STATUS_SUCCESS;
}

VOID
ZpClientLocalChannel_CloseAll(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_STATUS Status)
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
