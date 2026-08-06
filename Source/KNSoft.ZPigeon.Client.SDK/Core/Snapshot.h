#pragma once

#include "../Client.inl"

typedef struct _ZP_CLIENT_SNAPSHOT ZP_CLIENT_SNAPSHOT, *PZP_CLIENT_SNAPSHOT;

typedef
VOID
(NTAPI *ZP_CLIENT_SNAPSHOT_DELETE)(
    _In_ PZP_CLIENT_SNAPSHOT Snapshot);

typedef struct _ZP_CLIENT_SNAPSHOT
{
    LIST_ENTRY ListEntry;
    volatile LONG ReferenceCount;
    ULONG Id;
    BYTE ModuleId;
    ZP_CLIENT_SNAPSHOT_DELETE Delete;
} ZP_CLIENT_SNAPSHOT, *PZP_CLIENT_SNAPSHOT;

ULONG
ZpClientSnapshot_Add(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _Inout_ PZP_CLIENT_SNAPSHOT Snapshot,
    _In_ BYTE ModuleId,
    _In_ ZP_CLIENT_SNAPSHOT_DELETE Delete);

_Ret_maybenull_
PZP_CLIENT_SNAPSHOT
ZpClientSnapshot_Reference(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ BYTE ModuleId,
    _In_ ULONG SnapshotId);

VOID
ZpClientSnapshot_Dereference(
    _In_ PZP_CLIENT_SNAPSHOT Snapshot);

NTSTATUS
ZpClientSnapshot_Close(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ BYTE ModuleId,
    _In_ ULONG SnapshotId);

VOID
ZpClientSnapshot_CloseAll(
    _Inout_ PZP_CLIENT_OBJECT Object);
