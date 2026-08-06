#pragma once

#include <KNSoft/ZPigeon/Client.h>
#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#include "../Network/Transport.h"
#include "Transport/Quic.h"
#include "Transport/Tcp.h"
#include "Transport/Udp.h"

#define ZP_CLIENT_LOOKUP_BUCKET_COUNT 16

typedef struct _ZP_CLIENT_OBJECT
{
    RTL_SRWLOCK Lock;
    RTL_SRWLOCK FileEnumerationLock;
    RTL_SRWLOCK ArchiveEnumerationLock;
    RTL_SRWLOCK ExecutionLock;
    RTL_SRWLOCK RecordingLock;
    RTL_SRWLOCK SnapshotLock;
    ZP_CLIENT_STATE State;
    ZP_CONNECTION_POLICY ConnectionPolicy;
    ULONG CallbackCount;
    LIST_ENTRY InboundRequests;
    LIST_ENTRY LocalChannels;
    LIST_ENTRY InboundRequestBuckets[ZP_CLIENT_LOOKUP_BUCKET_COUNT];
    LIST_ENTRY LocalChannelBuckets[ZP_CLIENT_LOOKUP_BUCKET_COUNT];
    LIST_ENTRY ExecutionJobs;
    LIST_ENTRY RecordingJobs;
    LIST_ENTRY FileEnumerations;
    LIST_ENTRY ArchiveEnumerations;
    LIST_ENTRY Snapshots;
    ULONG HighestInboundRequestId;
    ULONG NextLocalChannelId;
    ULONG NextFileEnumerationId;
    ULONG NextArchiveEnumerationId;
    ULONG NextExecutionJobId;
    ULONG NextRecordingId;
    ULONG NextSnapshotId;
    ULONG InboundRequestCount;
    ULONGLONG InboundRequestPayloadBytes;
    ULONG LocalChannelCount;
    ULONG ExecutionJobCount;
    ULONG RecordingJobCount;
    ULONG FileEnumerationCount;
    ULONG ArchiveEnumerationCount;
    ULONGLONG ActiveModuleMask;
    ZP_CLIENT_CONFIG Config;
    PCZP_TRANSPORT_OPERATIONS TransportOperations[ZpTransportCount];
    PVOID TransportContexts[ZpTransportCount];
    ZP_TRANSPORT_TYPE ActiveTransport;
    ULONG EndpointIndex;
    ULONG NextEndpointIndex;
    ULONG FailureRound;
    ULONGLONG ReadyTickCount;
    PTP_TIMER RetryTimer;
    BOOLEAN StartPending;
    BOOLEAN RetryPending;
    ULONG RetryDelay;
    NCRYPT_KEY_HANDLE ExternalIdentityKey;
    ZP_CLIENT_QUIC_TRANSPORT QuicTransport;
    ZP_CLIENT_TCP_TRANSPORT TcpTransport;
    ZP_CLIENT_UDP_TRANSPORT UdpTransport;
    PVOID RtcSession;
} ZP_CLIENT_OBJECT, *PZP_CLIENT_OBJECT;

FORCEINLINE
NTSTATUS
ZpClient_SendLocked(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_SEND_FLAGS SendFlags,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength)
{
    PCZP_TRANSPORT_OPERATIONS Operations = Object->TransportOperations[Object->ActiveTransport];

    return Object->State == ZpClientStateReady && Operations->Send != NULL ?
               Operations->Send(Object->TransportContexts[Object->ActiveTransport],
                                SendFlags,
                                MessageType,
                                Body,
                                BodyLength,
                                Payload,
                                PayloadLength) :
               STATUS_CONNECTION_DISCONNECTED;
}

NTSTATUS
ZpClient_SetTransport(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_TRANSPORT_TYPE Transport,
    _In_ PCZP_TRANSPORT_OPERATIONS Operations,
    _In_opt_ PVOID Context);

VOID
ZpClient_TransportShutdown(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_STATUS Status);

NTSTATUS
ZpClient_NotifyState(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ ZP_STATUS Status);

NTSTATUS
ZpClient_QueueRequest(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ PCZP_REQUEST_VIEW Request);

NTSTATUS
ZpClient_CancelInboundRequest(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONG RequestId);

VOID
ZpClient_CloseInboundRequests(
    _In_ ZP_CLIENT_HANDLE Client);
