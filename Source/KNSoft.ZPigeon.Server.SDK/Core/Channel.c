#include "Channel.h"

struct _ZP_SERVER_CHANNEL_OBJECT
{
    ZP_CHANNEL_HEADER Header;
    LIST_ENTRY ListEntry;
    PZP_CONNECTION_OBJECT Owner;
    volatile LONG Pending;
    ULONGLONG ChannelId;
    USHORT ModuleId;
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
};

static
NTSTATUS
NTAPI
ZpServerChannel_Cancel(
    _In_ ZP_CHANNEL_HANDLE Channel);

static
NTSTATUS
NTAPI
ZpServerChannel_Send(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength);

static
VOID
NTAPI
ZpServerChannel_Close(
    _In_ ZP_CHANNEL_HANDLE Channel);

static
PZP_SERVER_CHANNEL_OBJECT
ZpServerConnection_FindChannel(
    _In_ PZP_CONNECTION_OBJECT Connection,
    _In_ ULONGLONG ChannelId)
{
    PZP_SERVER_CHANNEL_OBJECT Channel;
    PLIST_ENTRY Entry;

    for (Entry = Connection->Channels.Flink;
         Entry != &Connection->Channels;
         Entry = Entry->Flink)
    {
        Channel = CONTAINING_RECORD(Entry,
                                    ZP_SERVER_CHANNEL_OBJECT,
                                    ListEntry);
        if (Channel->ChannelId == ChannelId)
        {
            return Channel;
        }
    }
    return NULL;
}

static
VOID
ZpServerChannel_SendClose(
    _Inout_ PZP_SERVER_CHANNEL_OBJECT Channel,
    _In_ NTSTATUS CloseStatus)
{
    PZP_CONNECTION_OBJECT Connection = Channel->Owner;
    BYTE Body[sizeof(ULONGLONG) + sizeof(ULONG)];
    ULONG BodyLength;

    if (!NT_SUCCESS(ZpMessage_EncodeChannelClose(Channel->ChannelId,
                                                 CloseStatus,
                                                 Body,
                                                 sizeof(Body),
                                                 &BodyLength)))
    {
        return;
    }
    RtlAcquireSRWLockShared(&Connection->Lock);
    if (Connection->Phase == ZpConnectionPhaseReady)
    {
        Connection->Send(Connection,
                         ZpMessageChannelClose,
                         Body,
                         BodyLength);
    }
    RtlReleaseSRWLockShared(&Connection->Lock);
}

static
VOID
ZpServerChannel_InvokeClose(
    _Inout_ PZP_SERVER_CHANNEL_OBJECT Channel,
    _In_ NTSTATUS Status)
{
    PZP_CONNECTION_OBJECT Connection = Channel->Owner;

    Channel->CloseCallback((ZP_CHANNEL_HANDLE)Channel,
                           Status,
                           Channel->Context);
    Channel->Owner = NULL;
    ZpChannel_Release(&Channel->Header);
    ZpConnection_Release((ZP_CONNECTION_HANDLE)Connection);
}

NTSTATUS
ZpServerChannel_Reserve(
    _Inout_ PZP_CONNECTION_OBJECT Connection)
{
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&Connection->Lock);
    if (Connection->Phase != ZpConnectionPhaseReady)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
    else if (Connection->ChannelCount + Connection->ChannelReservations >=
             Connection->MaxChannels)
    {
        Status = STATUS_QUOTA_EXCEEDED;
    }
    else
    {
        Connection->ChannelReservations++;
        Status = STATUS_SUCCESS;
    }
    RtlReleaseSRWLockExclusive(&Connection->Lock);
    return Status;
}

VOID
ZpServerChannel_ReleaseReservation(
    _Inout_ PZP_CONNECTION_OBJECT Connection)
{
    RtlAcquireSRWLockExclusive(&Connection->Lock);
    if (Connection->ChannelReservations != 0)
    {
        Connection->ChannelReservations--;
    }
    RtlReleaseSRWLockExclusive(&Connection->Lock);
}

static
VOID
ZpServerChannel_Complete(
    _Inout_ PZP_SERVER_CHANNEL_OBJECT Channel,
    _In_ NTSTATUS Status)
{
    PZP_CONNECTION_OBJECT Connection = Channel->Owner;

    RtlAcquireSRWLockExclusive(&Connection->Lock);
    if (!InterlockedExchange(&Channel->Pending, FALSE))
    {
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        return;
    }
    RemoveEntryList(&Channel->ListEntry);
    Connection->ChannelCount--;
    RtlReleaseSRWLockExclusive(&Connection->Lock);
    ZpServerChannel_InvokeClose(Channel, Status);
}

NTSTATUS
ZpServerChannel_Create(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ULONGLONG ChannelId,
    _In_ USHORT ModuleId,
    _In_ LOGICAL BoundedReceive,
    _In_ ULONGLONG RemainingBytes,
    _In_ LOGICAL BoundedSend,
    _In_ ULONGLONG RemainingSendBytes,
    _In_opt_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_opt_ ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _In_ LOGICAL Reserved,
    _Out_ PZP_SERVER_CHANNEL_OBJECT* Channel)
{
    PZP_SERVER_CHANNEL_OBJECT ChannelObject;
    NTSTATUS Status;

    if (ChannelId == 0 || ModuleId == 0 || CloseCallback == NULL ||
        (DataCallback == NULL && WritableCallback == NULL))
    {
        if (Reserved)
        {
            ZpServerChannel_ReleaseReservation(Connection);
        }
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    ChannelObject = Mem_Alloc(sizeof(*ChannelObject));
    if (ChannelObject == NULL)
    {
        if (Reserved)
        {
            ZpServerChannel_ReleaseReservation(Connection);
        }
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(ChannelObject, sizeof(*ChannelObject));
    ChannelObject->Header.Cancel = ZpServerChannel_Cancel;
    ChannelObject->Header.Send = ZpServerChannel_Send;
    ChannelObject->Header.Close = ZpServerChannel_Close;
    ChannelObject->Header.ReferenceCount = 3;
    ChannelObject->Owner = Connection;
    ChannelObject->Pending = TRUE;
    ChannelObject->ChannelId = ChannelId;
    ChannelObject->ModuleId = ModuleId;
    ChannelObject->BoundedReceive = BoundedReceive;
    ChannelObject->RemainingBytes = RemainingBytes;
    ChannelObject->BoundedSend = BoundedSend;
    ChannelObject->RemainingSendBytes = RemainingSendBytes;
    ChannelObject->DataCallback = DataCallback;
    ChannelObject->WritableCallback = WritableCallback;
    ChannelObject->CloseCallback = CloseCallback;
    ChannelObject->Context = Context;

    RtlAcquireSRWLockExclusive(&Connection->Lock);
    if (Reserved && Connection->ChannelReservations == 0)
    {
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        Mem_Free(ChannelObject);
        return STATUS_INTERNAL_ERROR;
    }
    if (Reserved)
    {
        Connection->ChannelReservations--;
    }
    if (Connection->Phase != ZpConnectionPhaseReady ||
        ChannelId <= Connection->HighestChannelId)
    {
        Status = Connection->Phase != ZpConnectionPhaseReady ?
                     STATUS_INVALID_DEVICE_STATE : STATUS_PROTOCOL_UNREACHABLE;
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        Mem_Free(ChannelObject);
        return Status;
    }
    Connection->HighestChannelId = ChannelId;
    if (Connection->ChannelCount == Connection->MaxChannels)
    {
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        Mem_Free(ChannelObject);
        return STATUS_QUOTA_EXCEEDED;
    }
    InsertTailList(&Connection->Channels, &ChannelObject->ListEntry);
    Connection->ChannelCount++;
    ZpConnection_AddRef((ZP_CONNECTION_HANDLE)Connection);
    RtlReleaseSRWLockExclusive(&Connection->Lock);
    *Channel = ChannelObject;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpServerChannel_SendWindow(
    _Inout_ PZP_SERVER_CHANNEL_OBJECT Channel,
    _In_ ULONG CreditBytes)
{
    PZP_CONNECTION_OBJECT Connection = Channel->Owner;
    BYTE Body[sizeof(ULONGLONG) + sizeof(ULONG)];
    ULONG BodyLength;
    NTSTATUS Status;

    if (Connection == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    Status = ZpMessage_EncodeChannelWindow(Channel->ChannelId,
                                           CreditBytes,
                                           Body,
                                           sizeof(Body),
                                           &BodyLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    RtlAcquireSRWLockExclusive(&Connection->Lock);
    if (Connection->Phase != ZpConnectionPhaseReady || !Channel->Pending)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
    else if (MAXULONGLONG - Channel->ReceiveCredit < CreditBytes)
    {
        Status = STATUS_INTEGER_OVERFLOW;
    }
    else
    {
        Channel->ReceiveCredit += CreditBytes;
        Status = Connection->Send(Connection,
                                  ZpMessageChannelWindow,
                                  Body,
                                  BodyLength);
        if (!NT_SUCCESS(Status))
        {
            Channel->ReceiveCredit -= CreditBytes;
        }
    }
    RtlReleaseSRWLockExclusive(&Connection->Lock);
    return Status;
}

NTSTATUS
ZpServerChannel_GetId(
    _In_ PZP_SERVER_CHANNEL_OBJECT Channel,
    _In_ PZP_CONNECTION_OBJECT Connection,
    _In_ USHORT ModuleId,
    _Out_ PULONGLONG ChannelId)
{
    NTSTATUS Status;

    RtlAcquireSRWLockShared(&Connection->Lock);
    if (Channel->Owner == Connection && Channel->Pending &&
        Channel->ModuleId == ModuleId)
    {
        *ChannelId = Channel->ChannelId;
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = STATUS_INVALID_HANDLE;
    }
    RtlReleaseSRWLockShared(&Connection->Lock);
    return Status;
}

VOID
ZpServerChannel_Abort(
    _Inout_ PZP_SERVER_CHANNEL_OBJECT Channel,
    _In_ NTSTATUS Status)
{
    if (Channel->Owner != NULL)
    {
        ZpServerChannel_Complete(Channel, Status);
    }
}

VOID
ZpServerConnection_RejectChannel(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ULONGLONG ChannelId,
    _In_ NTSTATUS Status)
{
    BYTE Body[sizeof(ULONGLONG) + sizeof(ULONG)];
    ULONG BodyLength;

    if (!NT_SUCCESS(ZpMessage_EncodeChannelClose(ChannelId,
                                                 Status,
                                                 Body,
                                                 sizeof(Body),
                                                 &BodyLength)))
    {
        return;
    }
    RtlAcquireSRWLockShared(&Connection->Lock);
    if (Connection->Phase == ZpConnectionPhaseReady)
    {
        Connection->Send(Connection,
                         ZpMessageChannelClose,
                         Body,
                         BodyLength);
    }
    RtlReleaseSRWLockShared(&Connection->Lock);
}

static
NTSTATUS
NTAPI
ZpServerChannel_Cancel(
    _In_ ZP_CHANNEL_HANDLE Channel)
{
    PZP_SERVER_CHANNEL_OBJECT ChannelObject =
        (PZP_SERVER_CHANNEL_OBJECT)Channel;
    PZP_CONNECTION_OBJECT Connection = ChannelObject->Owner;

    if (Connection == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    RtlAcquireSRWLockExclusive(&Connection->Lock);
    if (!InterlockedExchange(&ChannelObject->Pending, FALSE))
    {
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    RemoveEntryList(&ChannelObject->ListEntry);
    Connection->ChannelCount--;
    RtlReleaseSRWLockExclusive(&Connection->Lock);
    ZpServerChannel_SendClose(ChannelObject, STATUS_CANCELLED);
    ZpServerChannel_InvokeClose(ChannelObject, STATUS_CANCELLED);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
ZpServerChannel_Send(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    PZP_SERVER_CHANNEL_OBJECT ChannelObject =
        (PZP_SERVER_CHANNEL_OBJECT)Channel;
    PZP_CONNECTION_OBJECT Connection = ChannelObject->Owner;
    PBYTE Body;
    ULONG BodyLength;
    NTSTATUS Status;

    if (Connection == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    Status = ZpMessage_EncodeChannelData(ChannelObject->ChannelId,
                                         Data,
                                         DataLength,
                                         NULL,
                                         0,
                                         &BodyLength);
    Body = NT_SUCCESS(Status) ? Mem_Alloc(BodyLength) : NULL;
    if (!NT_SUCCESS(Status) || Body == NULL)
    {
        return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    }
    Status = ZpMessage_EncodeChannelData(ChannelObject->ChannelId,
                                         Data,
                                         DataLength,
                                         Body,
                                         BodyLength,
                                         &BodyLength);
    if (NT_SUCCESS(Status))
    {
        RtlAcquireSRWLockExclusive(&Connection->Lock);
        if (Connection->Phase != ZpConnectionPhaseReady ||
            !ChannelObject->Pending ||
            ChannelObject->WritableCallback == NULL)
        {
            Status = STATUS_INVALID_DEVICE_STATE;
        }
        else if (DataLength > ChannelObject->SendCredit ||
                 (ChannelObject->BoundedSend &&
                  DataLength > ChannelObject->RemainingSendBytes))
        {
            Status = STATUS_RETRY;
        }
        else
        {
            ChannelObject->SendCredit -= DataLength;
            Status = Connection->Send(Connection,
                                      ZpMessageChannelData,
                                      Body,
                                      BodyLength);
            if (!NT_SUCCESS(Status))
            {
                ChannelObject->SendCredit += DataLength;
            }
            else if (ChannelObject->BoundedSend)
            {
                ChannelObject->RemainingSendBytes -= DataLength;
            }
        }
        RtlReleaseSRWLockExclusive(&Connection->Lock);
    }
    Mem_Free(Body);
    return Status;
}

static
VOID
NTAPI
ZpServerChannel_Close(
    _In_ ZP_CHANNEL_HANDLE Channel)
{
    ZpChannel_Release((PZP_CHANNEL_HEADER)Channel);
}

NTSTATUS
ZpServerConnection_ReceiveChannelData(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message)
{
    PZP_SERVER_CHANNEL_OBJECT Channel;
    LOGICAL Replenish;
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&Connection->Lock);
    Channel = ZpServerConnection_FindChannel(Connection, Message->ChannelId);
    if (Channel == NULL || Channel->DataCallback == NULL ||
        Message->Data.Length > Channel->ReceiveCredit ||
        (Channel->BoundedReceive &&
         Message->Data.Length > Channel->RemainingBytes))
    {
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->ReceiveCredit -= Message->Data.Length;
    if (Channel->BoundedReceive)
    {
        Channel->RemainingBytes -= Message->Data.Length;
    }
    InterlockedIncrement(&Channel->Header.ReferenceCount);
    RtlReleaseSRWLockExclusive(&Connection->Lock);
    Channel->DataCallback((ZP_CHANNEL_HANDLE)Channel,
                          &Message->Data,
                          Channel->Context);
    RtlAcquireSRWLockExclusive(&Connection->Lock);
    Replenish = Channel->Pending &&
                (!Channel->BoundedReceive || Channel->RemainingBytes != 0);
    RtlReleaseSRWLockExclusive(&Connection->Lock);
    if (Replenish)
    {
        Status = ZpServerChannel_SendWindow(Channel, Message->Data.Length);
        if (!NT_SUCCESS(Status) && Channel->Owner != NULL)
        {
            ZpServerChannel_Complete(Channel, Status);
        }
    }
    ZpChannel_Release(&Channel->Header);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpServerConnection_ReceiveChannelClose(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ const ZP_CHANNEL_CLOSE* Message)
{
    PZP_SERVER_CHANNEL_OBJECT Channel;
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&Connection->Lock);
    Channel = ZpServerConnection_FindChannel(Connection, Message->ChannelId);
    if (Channel == NULL)
    {
        Status = Message->ChannelId != 0 &&
                 Message->ChannelId <= Connection->HighestChannelId ?
                     STATUS_SUCCESS : STATUS_PROTOCOL_UNREACHABLE;
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        return Status;
    }
    if ((NT_SUCCESS(Message->Status) &&
         ((Channel->BoundedReceive && Channel->RemainingBytes != 0) ||
          (Channel->BoundedSend && Channel->RemainingSendBytes != 0))))
    {
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    InterlockedExchange(&Channel->Pending, FALSE);
    RemoveEntryList(&Channel->ListEntry);
    Connection->ChannelCount--;
    RtlReleaseSRWLockExclusive(&Connection->Lock);
    ZpServerChannel_InvokeClose(Channel, Message->Status);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpServerConnection_ReceiveChannelWindow(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ULONGLONG ChannelId,
    _In_ ULONG CreditBytes)
{
    PZP_SERVER_CHANNEL_OBJECT Channel;
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&Connection->Lock);
    Channel = ZpServerConnection_FindChannel(Connection, ChannelId);
    if (Channel == NULL)
    {
        Status = ChannelId != 0 &&
                 ChannelId <= Connection->HighestChannelId ?
                     STATUS_SUCCESS : STATUS_PROTOCOL_UNREACHABLE;
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        return Status;
    }
    if (Channel->WritableCallback == NULL ||
        MAXULONGLONG - Channel->SendCredit < CreditBytes ||
        (Channel->BoundedSend &&
         Channel->SendCredit + CreditBytes > Channel->RemainingSendBytes))
    {
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->SendCredit += CreditBytes;
    InterlockedIncrement(&Channel->Header.ReferenceCount);
    RtlReleaseSRWLockExclusive(&Connection->Lock);
    Channel->WritableCallback((ZP_CHANNEL_HANDLE)Channel,
                              CreditBytes,
                              Channel->Context);
    ZpChannel_Release(&Channel->Header);
    return STATUS_SUCCESS;
}

VOID
ZpServerConnection_CloseChannels(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ NTSTATUS Status)
{
    PZP_SERVER_CHANNEL_OBJECT Channel;

    for (;;)
    {
        RtlAcquireSRWLockExclusive(&Connection->Lock);
        if (IsListEmpty(&Connection->Channels))
        {
            RtlReleaseSRWLockExclusive(&Connection->Lock);
            return;
        }
        Channel = CONTAINING_RECORD(Connection->Channels.Flink,
                                    ZP_SERVER_CHANNEL_OBJECT,
                                    ListEntry);
        InterlockedExchange(&Channel->Pending, FALSE);
        RemoveEntryList(&Channel->ListEntry);
        Connection->ChannelCount--;
        RtlReleaseSRWLockExclusive(&Connection->Lock);
        ZpServerChannel_InvokeClose(Channel, Status);
    }
}
