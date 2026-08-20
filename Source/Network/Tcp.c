#include "Tcp.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#include <Mstcpip.h>

#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Ws2_32.lib")

typedef struct _ZP_TCP_SEND_IO
{
    ZP_TCP_IO Io;
    WSABUF Buffer;
    ULONG Offset;
    ULONG Length;
    PBYTE Data;
} ZP_TCP_SEND_IO, *PZP_TCP_SEND_IO;

#define ZP_TCP_KEEP_ALIVE_TIMEOUT_MILLISECONDS 20000
#define ZP_TCP_KEEP_ALIVE_INTERVAL_MILLISECONDS 5000

static
VOID
ZpTcpConnection_Release(
    _Inout_ PZP_TCP_CONNECTION Connection)
{
    ZP_STATUS Status;

    if (InterlockedDecrement(&Connection->ReferenceCount) != 0)
    {
        return;
    }
    Status = Connection->CloseStatus;
    ZpTls_Uninitialize(&Connection->Tls);
    RtlDeleteCriticalSection(&Connection->Lock);
    Connection->Closed(Connection, Status, Connection->CallbackContext);
}

VOID
ZpTcpConnection_Close(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _In_ ZP_STATUS Status)
{
    SOCKET Socket;

    if (InterlockedCompareExchange(&Connection->Closing, TRUE, FALSE) != FALSE)
    {
        return;
    }
    RtlEnterCriticalSection(&Connection->Lock);
    Connection->CloseStatus = Status;
    Socket = Connection->Socket;
    Connection->Socket = INVALID_SOCKET;
    RtlLeaveCriticalSection(&Connection->Lock);
    if (Socket != INVALID_SOCKET)
    {
        shutdown(Socket, SD_BOTH);
        closesocket(Socket);
    }
    ZpTcpConnection_Release(Connection);
}

static
ZP_STATUS
ZpTcpConnection_PostSend(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _Post_invalid_ _In_reads_bytes_(DataLength) PBYTE Data,
    _In_ ULONG DataLength)
{
    PZP_TCP_SEND_IO Send;
    DWORD BytesSent;
    INT Error;

    if (InterlockedCompareExchange(&Connection->Closing, FALSE, FALSE))
    {
        Mem_Free(Data);
        return ZpStatus_FromNtStatus(STATUS_CONNECTION_DISCONNECTED);
    }
    Send = Mem_Alloc(sizeof(*Send));
    if (Send == NULL)
    {
        Mem_Free(Data);
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    RtlZeroMemory(&Send->Io.Overlapped, sizeof(Send->Io.Overlapped));
    Send->Io.Type = ZpTcpIoSend;
    Send->Buffer.buf = (PCHAR)Data;
    Send->Buffer.len = DataLength;
    Send->Offset = 0;
    Send->Length = DataLength;
    Send->Data = Data;
    InterlockedIncrement(&Connection->ReferenceCount);
    Error = WSASend(Connection->Socket,
                    &Send->Buffer,
                    1,
                    &BytesSent,
                    0,
                    &Send->Io.Overlapped,
                    NULL);
    if (Error == SOCKET_ERROR && (Error = WSAGetLastError()) != WSA_IO_PENDING)
    {
        Mem_Free(Data);
        Mem_Free(Send);
        ZpTcpConnection_Release(Connection);
        return ZpStatus_FromCode(ZpStatusWinsock, (ULONG)Error);
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
NTSTATUS
NTAPI
ZpTcpConnection_DeliverPlaintext(
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context)
{
    PZP_TCP_CONNECTION Connection = Context;

    return Connection->Receive(Connection,
                               Data,
                               DataLength,
                               Connection->CallbackContext);
}

static
ZP_STATUS
ZpTcpConnection_ProcessHandshake(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    PBYTE Token;
    ULONG TokenLength;
    LOGICAL Complete;
    ZP_STATUS Status;

    Status = ZpTls_Handshake(&Connection->Tls,
                             Data,
                             DataLength,
                             &Token,
                             &TokenLength,
                             &Complete);
    if (!ZpStatus_IsSuccess(Status))
    {
        return Status;
    }
    if (TokenLength != 0)
    {
        Status = ZpTcpConnection_PostSend(Connection, Token, TokenLength);
        if (!ZpStatus_IsSuccess(Status))
        {
            return Status;
        }
    }
    if (Complete)
    {
        Status = Connection->Connected(Connection, Connection->CallbackContext);
        if (ZpStatus_IsSuccess(Status) && Connection->Tls.InputLength != 0)
        {
            Status = ZpTls_Decrypt(&Connection->Tls,
                                   NULL,
                                   0,
                                   ZpTcpConnection_DeliverPlaintext,
                                   Connection);
        }
    }
    return Status;
}

static
ZP_STATUS
ZpTcpConnection_PostReceive(
    _Inout_ PZP_TCP_CONNECTION Connection)
{
    DWORD Flags = 0, BytesReceived;
    INT Error;

    RtlZeroMemory(&Connection->ReceiveIo.Io.Overlapped,
                  sizeof(Connection->ReceiveIo.Io.Overlapped));
    InterlockedIncrement(&Connection->ReferenceCount);
    Error = WSARecv(Connection->Socket,
                    &Connection->ReceiveIo.Buffer,
                    1,
                    &BytesReceived,
                    &Flags,
                    &Connection->ReceiveIo.Io.Overlapped,
                    NULL);
    if (Error == SOCKET_ERROR && (Error = WSAGetLastError()) != WSA_IO_PENDING)
    {
        ZpTcpConnection_Release(Connection);
        return ZpStatus_FromCode(ZpStatusWinsock, (ULONG)Error);
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

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
    _In_opt_ PVOID CallbackContext)
{
    struct tcp_keepalive KeepAlive = {
        TRUE,
        ZP_TCP_KEEP_ALIVE_TIMEOUT_MILLISECONDS,
        ZP_TCP_KEEP_ALIVE_INTERVAL_MILLISECONDS
    };
    DWORD BytesReturned;
    NTSTATUS NtStatus;
    ZP_STATUS Status;

    RtlZeroMemory(Connection, sizeof(*Connection));
    NtStatus = RtlInitializeCriticalSectionEx(&Connection->Lock,
                                              0,
                                              RTL_CRITICAL_SECTION_FLAG_NO_DEBUG_INFO);
    if (!NT_SUCCESS(NtStatus))
    {
        return ZpStatus_FromNtStatus(NtStatus);
    }
    Connection->CompletionPort = CompletionPort;
    Connection->Socket = Socket;
    Connection->ReferenceCount = 1;
    Connection->Connected = Connected;
    Connection->Receive = Receive;
    Connection->Closed = Closed;
    Connection->CallbackContext = CallbackContext;
    Connection->ReceiveIo.Io.Type = ZpTcpIoReceive;
    Connection->ReceiveIo.Buffer.buf = (PCHAR)Connection->ReceiveIo.Data;
    Connection->ReceiveIo.Buffer.len = sizeof(Connection->ReceiveIo.Data);
    ZpTls_Initialize(&Connection->Tls, Role, Credential, ServerName);
    if (WSAIoctl(Socket,
                 SIO_KEEPALIVE_VALS,
                 &KeepAlive,
                 sizeof(KeepAlive),
                 NULL,
                 0,
                 &BytesReturned,
                 NULL,
                 NULL) == SOCKET_ERROR)
    {
        Status = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
        ZpTcpConnection_Close(Connection, Status);
        return Status;
    }
    if (CreateIoCompletionPort((HANDLE)Socket,
                               CompletionPort,
                               (ULONG_PTR)Connection,
                               0) == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        ZpTcpConnection_Close(Connection, Status);
        return Status;
    }
    Status = ZpTcpConnection_PostReceive(Connection);
    if (ZpStatus_IsSuccess(Status) && Role == ZpTlsClient)
    {
        RtlEnterCriticalSection(&Connection->Lock);
        Status = ZpTcpConnection_ProcessHandshake(Connection, NULL, 0);
        RtlLeaveCriticalSection(&Connection->Lock);
    }
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpTcpConnection_Close(Connection, Status);
    }
    return Status;
}

NTSTATUS
ZpTcpConnection_SendFrame(
    _Inout_ PZP_TCP_CONNECTION TcpConnection,
    _Inout_ PZP_CONNECTION ProtocolConnection,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PBYTE Frame, Encrypted;
    ULONG FrameSize, EncryptedLength;
    NTSTATUS Status;
    ZP_STATUS TransportStatus;

    Status = ZpConnection_AllocateFrame(MessageType, Body, BodyLength, &Frame, &FrameSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    RtlEnterCriticalSection(&TcpConnection->Lock);
    TransportStatus = ZpTls_Encrypt(&TcpConnection->Tls,
                                    Frame,
                                    FrameSize,
                                    &Encrypted,
                                    &EncryptedLength);
    if (ZpStatus_IsSuccess(TransportStatus))
    {
        TransportStatus = ZpTcpConnection_PostSend(TcpConnection,
                                                   Encrypted,
                                                   EncryptedLength);
    }
    RtlLeaveCriticalSection(&TcpConnection->Lock);
    Mem_Free(Frame);
    if (!ZpStatus_IsSuccess(TransportStatus))
    {
        ZpTcpConnection_Close(TcpConnection, TransportStatus);
        return STATUS_CONNECTION_DISCONNECTED;
    }
    return ZpConnection_NotifyMessageSent(ProtocolConnection, MessageType);
}

static
VOID
ZpTcpConnection_ProcessCompletion(
    _Inout_ PZP_TCP_CONNECTION Connection,
    _Inout_ PZP_TCP_IO Io,
    _In_ ULONG BytesTransferred,
    _In_ ULONG Error)
{
    PZP_TCP_SEND_IO Send;
    ZP_STATUS Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    DWORD BytesSent;
    INT Result;

    if (Error != ERROR_SUCCESS)
    {
        Status = ZpStatus_FromCode(ZpStatusWinsock, Error);
    }
    else if (Io->Type == ZpTcpIoReceive)
    {
        if (BytesTransferred == 0)
        {
            Status = ZpStatus_FromNtStatus(STATUS_CONNECTION_DISCONNECTED);
        }
        else
        {
            RtlEnterCriticalSection(&Connection->Lock);
            Status = Connection->Tls.HandshakeComplete ?
                         ZpTls_Decrypt(&Connection->Tls,
                                      Connection->ReceiveIo.Data,
                                      BytesTransferred,
                                      ZpTcpConnection_DeliverPlaintext,
                                      Connection) :
                         ZpTcpConnection_ProcessHandshake(Connection,
                                                          Connection->ReceiveIo.Data,
                                                          BytesTransferred);
            RtlLeaveCriticalSection(&Connection->Lock);
            if (ZpStatus_IsSuccess(Status) &&
                !InterlockedCompareExchange(&Connection->Closing, FALSE, FALSE))
            {
                Status = ZpTcpConnection_PostReceive(Connection);
            }
        }
    }
    else
    {
        Send = CONTAINING_RECORD(Io, ZP_TCP_SEND_IO, Io);
        Send->Offset += BytesTransferred;
        if (Send->Offset < Send->Length && ZpStatus_IsSuccess(Status))
        {
            Send->Buffer.buf = (PCHAR)Send->Data + Send->Offset;
            Send->Buffer.len = Send->Length - Send->Offset;
            RtlZeroMemory(&Send->Io.Overlapped, sizeof(Send->Io.Overlapped));
            Result = WSASend(Connection->Socket,
                             &Send->Buffer,
                             1,
                             &BytesSent,
                             0,
                             &Send->Io.Overlapped,
                             NULL);
            if (Result != SOCKET_ERROR || WSAGetLastError() == WSA_IO_PENDING)
            {
                return;
            }
            Status = ZpStatus_FromCode(ZpStatusWinsock, WSAGetLastError());
        }
        Mem_Free(Send->Data);
        Mem_Free(Send);
    }
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpTcpConnection_Close(Connection, Status);
    }
    ZpTcpConnection_Release(Connection);
}

DWORD
WINAPI
ZpTcp_Worker(
    _In_ PVOID CompletionPort)
{
    PZP_TCP_CONNECTION Connection;
    PZP_TCP_IO Io;
    OVERLAPPED* Overlapped;
    ULONG_PTR CompletionKey;
    DWORD BytesTransferred, Error;
    LOGICAL Result;

    for (;;)
    {
        Result = GetQueuedCompletionStatus(CompletionPort,
                                           &BytesTransferred,
                                           &CompletionKey,
                                           &Overlapped,
                                           INFINITE);
        if (Overlapped == NULL)
        {
            break;
        }
        Error = Result ? ERROR_SUCCESS : GetLastError();
        Connection = (PZP_TCP_CONNECTION)CompletionKey;
        Io = CONTAINING_RECORD(Overlapped, ZP_TCP_IO, Overlapped);
        ZpTcpConnection_ProcessCompletion(Connection,
                                          Io,
                                          BytesTransferred,
                                          Error);
    }
    return 0;
}
