#pragma once

#include "Connection.h"
#include "Tls.h"

#include <WinSock2.h>
#include <Ws2tcpip.h>

#define ZP_TCP_RECEIVE_BUFFER_SIZE 0x10000UL

typedef struct _ZP_TCP_CONNECTION ZP_TCP_CONNECTION, *PZP_TCP_CONNECTION;

typedef
ZP_STATUS
(NTAPI *ZP_TCP_CONNECTED_ROUTINE)(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _In_opt_ PVOID Context);

typedef
NTSTATUS
(NTAPI *ZP_TCP_RECEIVE_ROUTINE)(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_TCP_CLOSED_ROUTINE)(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context);

typedef BYTE ZP_TCP_IO_TYPE;

#define ZpTcpIoReceive ((ZP_TCP_IO_TYPE)0)
#define ZpTcpIoSend ((ZP_TCP_IO_TYPE)1)

typedef struct _ZP_TCP_IO
{
    OVERLAPPED Overlapped;
    ZP_TCP_IO_TYPE Type;
} ZP_TCP_IO, *PZP_TCP_IO;

typedef struct _ZP_TCP_RECEIVE_IO
{
    ZP_TCP_IO Io;
    WSABUF Buffer;
    BYTE Data[ZP_TCP_RECEIVE_BUFFER_SIZE];
} ZP_TCP_RECEIVE_IO, *PZP_TCP_RECEIVE_IO;

struct _ZP_TCP_CONNECTION
{
    RTL_CRITICAL_SECTION Lock;
    HANDLE CompletionPort;
    SOCKET Socket;
    volatile LONG ReferenceCount;
    volatile LONG Closing;
    ZP_STATUS CloseStatus;
    ZP_TLS_CONTEXT Tls;
    ZP_TCP_CONNECTED_ROUTINE Connected;
    ZP_TCP_RECEIVE_ROUTINE Receive;
    ZP_TCP_CLOSED_ROUTINE Closed;
    PVOID CallbackContext;
    ZP_TCP_RECEIVE_IO ReceiveIo;
};

ZP_STATUS
ZpTcpConnection_Initialize(
    _Out_ PZP_TCP_CONNECTION Connection,
    _In_ HANDLE CompletionPort,
    _In_ SOCKET Socket,
    _In_ ZP_TLS_ROLE Role,
    _In_ PCredHandle Credential,
    _In_opt_ PCWSTR ServerName,
    _In_ ZP_TCP_CONNECTED_ROUTINE Connected,
    _In_ ZP_TCP_RECEIVE_ROUTINE Receive,
    _In_ ZP_TCP_CLOSED_ROUTINE Closed,
    _In_opt_ PVOID CallbackContext);

NTSTATUS
ZpTcpConnection_SendFrame(
    _Inout_ PZP_TCP_CONNECTION TcpConnection,
    _Inout_ PZP_CONNECTION ProtocolConnection,
    _In_ ZP_SEND_FLAGS SendFlags,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength);

VOID
ZpTcpConnection_Close(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _In_ ZP_STATUS Status);

DWORD
WINAPI
ZpTcp_Worker(
    _In_ PVOID CompletionPort);
