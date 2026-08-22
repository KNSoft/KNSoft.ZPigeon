#include "Connection.h"

NTSTATUS
ZpServerConnection_Initialize(
    _Out_ PZP_CONNECTION_OBJECT Connection,
    _In_ ULONG MaxRequests,
    _In_ ULONG MaxChannels,
    _In_ ZP_CONNECTION_SEND_ROUTINE Send,
    _In_ ZP_CONNECTION_DESTROY_ROUTINE Destroy)
{
    NTSTATUS Status;
    ULONG Index;

    RtlInitializeSRWLock(&Connection->Lock);
    Status = RtlInitializeCriticalSectionEx(&Connection->RequestSendLock,
                                            0,
                                            RTL_CRITICAL_SECTION_FLAG_NO_DEBUG_INFO);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    InitializeListHead(&Connection->Requests);
    InitializeListHead(&Connection->TimedRequests);
    InitializeListHead(&Connection->Channels);
    for (Index = 0; Index < ZP_CONNECTION_LOOKUP_BUCKET_COUNT; Index++)
    {
        InitializeListHead(&Connection->RequestBuckets[Index]);
        InitializeListHead(&Connection->ChannelBuckets[Index]);
    }
    Connection->NextRequestId = 1;
    Connection->HighestChannelId = 0;
    Connection->RequestTimer = NULL;
    Connection->Phase = ZpConnectionPhaseConnecting;
    Connection->RequestCount = 0;
    Connection->MaxRequests = MaxRequests;
    Connection->CompletedRequests = 0;
    Connection->FailedRequests = 0;
    Connection->SmoothedRequestMilliseconds = 0;
    Connection->ConsecutiveFailures = 0;
    Connection->ChannelCount = 0;
    Connection->ChannelReservations = 0;
    Connection->MaxChannels = MaxChannels;
    Connection->Modules = NULL;
    Connection->ModuleCount = 0;
    Connection->Send = Send;
    Connection->ReferenceCount = 1;
    Connection->Destroy = Destroy;
    return STATUS_SUCCESS;
}

VOID
ZpServerConnection_SetPhase(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_CONNECTION_PHASE Phase)
{
    RtlAcquireSRWLockExclusive(&Connection->Lock);
    Connection->Phase = Phase;
    RtlReleaseSRWLockExclusive(&Connection->Lock);
}

VOID
ZpServerConnection_SetModules(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_reads_(ModuleCount) PCZP_MODULE_RECORD Modules,
    _In_ USHORT ModuleCount)
{
    RtlAcquireSRWLockExclusive(&Connection->Lock);
    Connection->Modules = Modules;
    Connection->ModuleCount = ModuleCount;
    RtlReleaseSRWLockExclusive(&Connection->Lock);
}

VOID
NTAPI
ZpConnection_AddRef(
    _Inout_ ZP_CONNECTION_HANDLE Connection)
{
    InterlockedIncrement(&Connection->ReferenceCount);
}

VOID
NTAPI
ZpConnection_Release(
    _Inout_ ZP_CONNECTION_HANDLE Connection)
{
    if (InterlockedDecrement(&Connection->ReferenceCount) == 0)
    {
        RtlDeleteCriticalSection(&Connection->RequestSendLock);
        Connection->Destroy(Connection);
    }
}
