#include "Connection.h"

#include <ws2ipdef.h>

NTSTATUS
ZpServerConnection_Initialize(
    _Out_ PZP_CONNECTION_OBJECT Connection,
    _In_ ZP_TRANSPORT_TYPE Transport,
    _In_ ULONG MaxRequests,
    _In_ ULONG MaxChannels,
    _In_ const SOCKADDR* RemoteAddress,
    _In_ INT RemoteAddressLength,
    _In_ ZP_CONNECTION_SEND_ROUTINE Send,
    _In_ ZP_CONNECTION_DISCONNECT_ROUTINE Disconnect,
    _In_ ZP_CONNECTION_DESTROY_ROUTINE Destroy)
{
    NTSTATUS Status;
    ULONG Index;

    if (RemoteAddress == NULL ||
        (RemoteAddress->sa_family == AF_INET && RemoteAddressLength < sizeof(SOCKADDR_IN)) ||
        (RemoteAddress->sa_family == AF_INET6 && RemoteAddressLength < sizeof(SOCKADDR_IN6)) ||
        (RemoteAddress->sa_family != AF_INET && RemoteAddress->sa_family != AF_INET6))
    {
        return STATUS_INVALID_ADDRESS;
    }

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
    Connection->Transport = Transport;
    Connection->ProtocolConnection = NULL;
    Connection->ChannelCount = 0;
    Connection->ChannelReservations = 0;
    Connection->MaxChannels = MaxChannels;
    Connection->ModuleMask = 0;
    Connection->RemoteAddress.Family = RemoteAddress->sa_family;
    if (RemoteAddress->sa_family == AF_INET)
    {
        RtlCopyMemory(Connection->RemoteAddress.Value,
                      &((const SOCKADDR_IN*)RemoteAddress)->sin_addr,
                      sizeof(IN_ADDR));
    }
    else
    {
        RtlCopyMemory(Connection->RemoteAddress.Value,
                      &((const SOCKADDR_IN6*)RemoteAddress)->sin6_addr,
                      sizeof(IN6_ADDR));
    }
    Connection->Send = Send;
    Connection->Disconnect = Disconnect;
    Connection->ReferenceCount = 1;
    Connection->Destroy = Destroy;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ZpServer_DisconnectConnection(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_STATUS Status)
{
    PZP_CONNECTION_OBJECT ConnectionObject = Connection;
    ZP_CONNECTION_DISCONNECT_ROUTINE Disconnect;

    if (ConnectionObject == NULL) return STATUS_INVALID_PARAMETER;
    RtlAcquireSRWLockShared(&ConnectionObject->Lock);
    Disconnect = ConnectionObject->Phase != ZpConnectionPhaseClosed ?
                     ConnectionObject->Disconnect : NULL;
    RtlReleaseSRWLockShared(&ConnectionObject->Lock);
    return Disconnect != NULL ?
               Disconnect(ConnectionObject, Status) :
               STATUS_INVALID_DEVICE_STATE;
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
ZpServerConnection_SetModuleMask(
    _Inout_ PZP_CONNECTION_OBJECT Connection,
    _In_ ULONGLONG ModuleMask)
{
    RtlAcquireSRWLockExclusive(&Connection->Lock);
    Connection->ModuleMask = ModuleMask;
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
