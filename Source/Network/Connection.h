#pragma once

#include <KNSoft/ZPigeon/Protocol.h>
#include <WinSock2.h>

#include "Transport.h"

#define ZP_CONNECTION_INITIAL_RECEIVE_BUFFER_SIZE 4096
#define ZP_CONNECTION_MAX_CACHED_RECEIVE_BUFFER_SIZE (64 * 1024)
#define ZP_CONNECTION_VIOLATION_LIMIT 8
#define ZP_CONNECTION_VIOLATION_WINDOW_MILLISECONDS 10000
#define ZP_CONNECTION_MAX_OUTSTANDING_SEND_BYTES (32ull * 1024 * 1024)
#define ZP_GLOBAL_MAX_OUTSTANDING_SEND_BYTES (256ull * 1024 * 1024)

EXTERN_C_START

typedef BYTE ZP_CONNECTION_ROLE;

#define ZpConnectionRoleClient ((ZP_CONNECTION_ROLE)0)
#define ZpConnectionRoleServer ((ZP_CONNECTION_ROLE)1)

typedef BYTE ZP_CONNECTION_STATE;

#define ZpConnectionStateClientSendHello ((ZP_CONNECTION_STATE)0)
#define ZpConnectionStateClientWaitChallenge ((ZP_CONNECTION_STATE)1)
#define ZpConnectionStateClientSendAuthenticate ((ZP_CONNECTION_STATE)2)
#define ZpConnectionStateClientWaitReady ((ZP_CONNECTION_STATE)3)
#define ZpConnectionStateServerWaitHello ((ZP_CONNECTION_STATE)4)
#define ZpConnectionStateServerSendChallenge ((ZP_CONNECTION_STATE)5)
#define ZpConnectionStateServerWaitAuthenticate ((ZP_CONNECTION_STATE)6)
#define ZpConnectionStateServerSendReady ((ZP_CONNECTION_STATE)7)
#define ZpConnectionStateReady ((ZP_CONNECTION_STATE)8)
#define ZpConnectionStateClosed ((ZP_CONNECTION_STATE)9)

typedef struct _ZP_CONNECTION ZP_CONNECTION, *PZP_CONNECTION;

typedef struct _ZP_SEND_BUFFER
{
    PBYTE Allocation;
    ULONG Length;
    USHORT Offset;
    ZP_SEND_FLAGS Flags;
} ZP_SEND_BUFFER, *PZP_SEND_BUFFER;

typedef struct _ZP_SEND_MESSAGE
{
    const VOID* Body;
    const VOID* Payload;
    ZP_SEND_BUFFER Buffer;
    ULONG BodyLength;
    ULONG PayloadLength;
    ZP_MESSAGE_TYPE MessageType;
    BOOLEAN CompressionLockHeld;
} ZP_SEND_MESSAGE, *PZP_SEND_MESSAGE;

C_ASSERT(sizeof(ZP_SEND_BUFFER) == 16);
C_ASSERT(sizeof(ZP_SEND_MESSAGE) == 48);

typedef struct _ZP_TRANSFER_STATISTICS
{
    ULONGLONG TotalBytes;
    ULONGLONG WindowBytes;
    ULONGLONG WindowStartTickCount;
    ULONGLONG LastTickCount;
    ULONGLONG SmoothedBitsPerSecond;
    ULONGLONG LastSampleTickCount;
} ZP_TRANSFER_STATISTICS, *PZP_TRANSFER_STATISTICS;

typedef struct _ZP_NETWORK_STATISTICS
{
    ULONGLONG SentBytes;
    ULONGLONG ReceivedBytes;
    ULONGLONG SentBitsPerSecond;
    ULONGLONG ReceivedBitsPerSecond;
    ULONGLONG SentSampleTickCount;
    ULONGLONG ReceivedSampleTickCount;
    ULONGLONG OutstandingSendBytes;
    ULONGLONG MaximumOutstandingSendBytes;
    ULONGLONG MaximumSendQueueDelayMilliseconds;
    ULONGLONG RejectedSends;
    ZP_CONNECTION_POLICY Policy;
} ZP_NETWORK_STATISTICS, *PZP_NETWORK_STATISTICS;

typedef
NTSTATUS
(NTAPI *ZP_CONNECTION_MESSAGE_CALLBACK)(
    _Inout_ PZP_CONNECTION Connection,
    _In_ const ZP_FRAME_VIEW* Frame,
    _In_opt_ PVOID Context);

struct _ZP_CONNECTION
{
    ZP_CONNECTION_STATE State;
    ZP_CONNECTION_MESSAGE_CALLBACK MessageCallback;
    PVOID CallbackContext;
    BYTE ReceivePrefix[sizeof(ULONG)];
    ULONG ReceivePrefixLength;
    PBYTE ReceiveBuffer;
    ULONG ReceiveBufferLength;
    ULONG ReceiveBufferSize;
    ULONG ReceiveFrameSize;
    ULONG ReceiveViolationCount;
    ULONGLONG ReceiveViolationWindowStart;
    SRWLOCK CompressionLock;
    PVOID CompressionWorkspace;
    PBYTE CompressionBuffer;
    ULONG CompressionBufferSize;
    PBYTE DecompressionBuffer;
    ULONG DecompressionBufferSize;
    SRWLOCK StatisticsLock;
    ZP_TRANSFER_STATISTICS Sent;
    ZP_TRANSFER_STATISTICS Received;
    ULONGLONG OutstandingSendBytes;
    ULONGLONG MaximumOutstandingSendBytes;
    ULONGLONG MaximumSendQueueDelayMilliseconds;
    ULONGLONG RejectedSends;
    ZP_CONNECTION_POLICY Policy;
};

ZP_STATUS
ZpSocket_ResolveAddress(
    _In_opt_ PCWSTR Host,
    _In_ USHORT Port,
    _In_ LOGICAL Passive,
    _In_ INT SocketType,
    _In_ INT Protocol,
    _Out_ SOCKADDR_STORAGE* Address,
    _Out_ PINT AddressLength);

NTSTATUS
ZpConnection_Initialize(
    _Out_ PZP_CONNECTION Connection,
    _In_ ZP_CONNECTION_ROLE Role,
    _In_ ZP_CONNECTION_MESSAGE_CALLBACK MessageCallback,
    _In_opt_ PVOID CallbackContext);

VOID
ZpConnection_Uninitialize(
    _Inout_ PZP_CONNECTION Connection);

NTSTATUS
ZpConnection_NotifyMessageSent(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_ ULONG FrameSize);

NTSTATUS
ZpConnection_ReserveSend(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ULONG Length);

VOID
ZpConnection_CompleteSend(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ULONG Length);

VOID
ZpConnection_RecordSendQueueDelay(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ULONGLONG EnqueuedTickCount);

NTSTATUS
ZpConnection_PrepareSend(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ZP_SEND_FLAGS SendFlags,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SEND_MESSAGE Message);

NTSTATUS
ZpConnection_EncodeFrame(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ZP_SEND_FLAGS SendFlags,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ USHORT Headroom,
    _Out_ PZP_SEND_BUFFER Buffer);

VOID
ZpConnection_ReleaseSend(
    _Inout_ PZP_CONNECTION Connection,
    _Inout_ PZP_SEND_MESSAGE Message);

NTSTATUS
ZpSendBuffer_Allocate(
    _Out_ PZP_SEND_BUFFER Buffer,
    _In_ USHORT Headroom,
    _In_ ULONG Length,
    _In_ ZP_SEND_FLAGS Flags);

VOID
ZpSendBuffer_Release(
    _Inout_ PZP_SEND_BUFFER Buffer);

NTSTATUS
ZpConnection_SetPolicy(
    _Inout_ PZP_CONNECTION Connection,
    _In_ PCZP_CONNECTION_POLICY Policy);

VOID
ZpConnection_QueryStatistics(
    _Inout_ PZP_CONNECTION Connection,
    _Out_ PZP_NETWORK_STATISTICS Statistics);

NTSTATUS
ZpConnection_Receive(
    _Inout_ PZP_CONNECTION Connection,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength);

EXTERN_C_END
