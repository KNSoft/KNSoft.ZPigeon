#include "Snapshot.h"

ULONG
ZpClientSnapshot_Add(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _Inout_ PZP_CLIENT_SNAPSHOT Snapshot,
    _In_ BYTE ModuleId,
    _In_ ZP_CLIENT_SNAPSHOT_DELETE Delete)
{
    RtlAcquireSRWLockExclusive(&Object->SnapshotLock);
    do
    {
        Object->NextSnapshotId++;
    } while (Object->NextSnapshotId == 0);
    Snapshot->Id = Object->NextSnapshotId;
    Snapshot->ModuleId = ModuleId;
    Snapshot->Delete = Delete;
    Snapshot->ReferenceCount = 1;
    InsertTailList(&Object->Snapshots, &Snapshot->ListEntry);
    RtlReleaseSRWLockExclusive(&Object->SnapshotLock);
    return Snapshot->Id;
}

_Ret_maybenull_
PZP_CLIENT_SNAPSHOT
ZpClientSnapshot_Reference(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ BYTE ModuleId,
    _In_ ULONG SnapshotId)
{
    PLIST_ENTRY Entry;
    PZP_CLIENT_SNAPSHOT Snapshot = NULL;

    RtlAcquireSRWLockShared(&Object->SnapshotLock);
    for (Entry = Object->Snapshots.Flink; Entry != &Object->Snapshots; Entry = Entry->Flink)
    {
        PZP_CLIENT_SNAPSHOT Candidate = CONTAINING_RECORD(Entry, ZP_CLIENT_SNAPSHOT, ListEntry);

        if (Candidate->ModuleId == ModuleId && Candidate->Id == SnapshotId)
        {
            InterlockedIncrement(&Candidate->ReferenceCount);
            Snapshot = Candidate;
            break;
        }
    }
    RtlReleaseSRWLockShared(&Object->SnapshotLock);
    return Snapshot;
}

VOID
ZpClientSnapshot_Dereference(
    _In_ PZP_CLIENT_SNAPSHOT Snapshot)
{
    if (InterlockedDecrement(&Snapshot->ReferenceCount) == 0) Snapshot->Delete(Snapshot);
}

NTSTATUS
ZpClientSnapshot_Close(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ BYTE ModuleId,
    _In_ ULONG SnapshotId)
{
    PLIST_ENTRY Entry;
    PZP_CLIENT_SNAPSHOT Snapshot = NULL;

    RtlAcquireSRWLockExclusive(&Object->SnapshotLock);
    for (Entry = Object->Snapshots.Flink; Entry != &Object->Snapshots; Entry = Entry->Flink)
    {
        PZP_CLIENT_SNAPSHOT Candidate = CONTAINING_RECORD(Entry, ZP_CLIENT_SNAPSHOT, ListEntry);

        if (Candidate->ModuleId == ModuleId && Candidate->Id == SnapshotId)
        {
            RemoveEntryList(Entry);
            Snapshot = Candidate;
            break;
        }
    }
    RtlReleaseSRWLockExclusive(&Object->SnapshotLock);
    if (Snapshot == NULL) return STATUS_NOT_FOUND;
    ZpClientSnapshot_Dereference(Snapshot);
    return STATUS_SUCCESS;
}

VOID
ZpClientSnapshot_CloseAll(
    _Inout_ PZP_CLIENT_OBJECT Object)
{
    LIST_ENTRY Snapshots;
    PLIST_ENTRY Entry;

    InitializeListHead(&Snapshots);
    RtlAcquireSRWLockExclusive(&Object->SnapshotLock);
    while (!IsListEmpty(&Object->Snapshots))
    {
        Entry = RemoveHeadList(&Object->Snapshots);
        InsertTailList(&Snapshots, Entry);
    }
    RtlReleaseSRWLockExclusive(&Object->SnapshotLock);
    while (!IsListEmpty(&Snapshots))
    {
        ZpClientSnapshot_Dereference(
            CONTAINING_RECORD(RemoveHeadList(&Snapshots), ZP_CLIENT_SNAPSHOT, ListEntry));
    }
}
