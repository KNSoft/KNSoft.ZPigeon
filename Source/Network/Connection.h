#pragma once

#include <KNSoft/ZPigeon/Protocol.h>
#include <WinSock2.h>

#define ZP_CONNECTION_INITIAL_RECEIVE_BUFFER_SIZE 4096

EXTERN_C_START

typedef enum _ZP_CONNECTION_ROLE
{
    ZpConnectionRoleClient,
    ZpConnectionRoleServer
} ZP_CONNECTION_ROLE;

typedef enum _ZP_CONNECTION_STATE
{
    ZpConnectionStateClientSendHello,
    ZpConnectionStateClientWaitChallenge,
    ZpConnectionStateClientSendAuthenticate,
    ZpConnectionStateClientWaitReady,
    ZpConnectionStateServerWaitHello,
    ZpConnectionStateServerSendChallenge,
    ZpConnectionStateServerWaitAuthenticate,
    ZpConnectionStateServerSendReady,
    ZpConnectionStateReady,
    ZpConnectionStateClosed
} ZP_CONNECTION_STATE;

typedef struct _ZP_CONNECTION ZP_CONNECTION, *PZP_CONNECTION;

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
ZpConnection_AllocateFrame(
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Outptr_result_bytebuffer_(*FrameSize) PBYTE* Frame,
    _Out_ PULONG FrameSize);

NTSTATUS
ZpConnection_NotifyMessageSent(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ZP_MESSAGE_TYPE MessageType);

NTSTATUS
ZpConnection_Receive(
    _Inout_ PZP_CONNECTION Connection,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength);

EXTERN_C_END
