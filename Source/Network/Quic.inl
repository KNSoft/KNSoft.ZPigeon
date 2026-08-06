#pragma once

#include <KNSoft.Quic.h>
#include <KNSoft/MakeLifeEasier/String/Encoding.h>

#include <Ws2tcpip.h>

#include "Connection.h"

#pragma comment(lib, "Ws2_32.lib")

#define ZP_QUIC_ALPN "knsoft-zpigeon/1"

static const QUIC_BUFFER ZpQuicAlpn = {
    sizeof(ZP_QUIC_ALPN) - sizeof(ANSI_NULL),
    (PBYTE)ZP_QUIC_ALPN
};

typedef struct _ZP_QUIC_SEND_CONTEXT
{
    ZP_SEND_BUFFER Owner;
    PZP_CONNECTION Connection;
    ULONGLONG EnqueuedTickCount;
    QUIC_BUFFER Buffer;
} ZP_QUIC_SEND_CONTEXT, *PZP_QUIC_SEND_CONTEXT;

static
NTSTATUS
ZpQuic_SendFrame(
    _In_ HQUIC Stream,
    _Inout_ PZP_CONNECTION Connection,
    _In_ ZP_SEND_FLAGS SendFlags,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _When_(return < 0, _Out_) PZP_STATUS FailureStatus)
{
    ZP_SEND_BUFFER Frame;
    PZP_QUIC_SEND_CONTEXT Send;
    QUIC_STATUS QuicStatus;
    NTSTATUS Status;

    Status = ZpConnection_EncodeFrame(Connection,
                                     SendFlags,
                                     MessageType,
                                     Body,
                                     BodyLength,
                                     Payload,
                                     PayloadLength,
                                     sizeof(*Send),
                                     &Frame);
    if (!NT_SUCCESS(Status))
    {
        *FailureStatus = ZpStatus_FromNtStatus(Status);
        return Status;
    }
    Send = (PZP_QUIC_SEND_CONTEXT)Frame.Allocation;
    Status = ZpConnection_ReserveSend(Connection, Frame.Length);
    if (!NT_SUCCESS(Status))
    {
        *FailureStatus = ZpStatus_FromNtStatus(Status);
        ZpSendBuffer_Release(&Frame);
        return Status;
    }
    Send->Owner = Frame;
    Send->Connection = Connection;
    Send->EnqueuedTickCount = GetTickCount64();
    Send->Buffer.Buffer = Frame.Allocation + Frame.Offset;
    Send->Buffer.Length = Frame.Length;
    QuicStatus = MsQuicStreamSend(Stream,
                                  &Send->Buffer,
                                  1,
                                  QUIC_SEND_FLAG_NONE,
                                  Send);
    if (QUIC_FAILED(QuicStatus))
    {
        *FailureStatus = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
        ZpConnection_CompleteSend(Connection, Frame.Length);
        ZpSendBuffer_Release(&Frame);
        return STATUS_CONNECTION_DISCONNECTED;
    }
    ZpConnection_RecordSendQueueDelay(Connection, Send->EnqueuedTickCount);
    Status = ZpConnection_NotifyMessageSent(Connection, MessageType, Frame.Length);
    if (!NT_SUCCESS(Status)) *FailureStatus = ZpStatus_FromNtStatus(Status);
    return Status;
}

static
VOID
ZpQuic_CompleteSend(
    _In_ PVOID Context)
{
    PZP_QUIC_SEND_CONTEXT Send = Context;

    ZpConnection_CompleteSend(Send->Connection, Send->Owner.Length);
    ZpSendBuffer_Release(&Send->Owner);
}

static
ZP_STATUS
ZpQuic_ResolveAddress(
    _In_opt_ PCWSTR Host,
    _In_ USHORT Port,
    _Out_ QUIC_ADDR* Address)
{
    INT Error;
    ADDRINFOW Hints = { 0 };
    PADDRINFOW Result;
    WSADATA WsaData;

    RtlZeroMemory(Address, sizeof(*Address));
    if (Host == NULL)
    {
        QuicAddrSetFamily(Address, QUIC_ADDRESS_FAMILY_UNSPEC);
        QuicAddrSetPort(Address, Port);
        return ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }

    Hints.ai_family = AF_UNSPEC;
    Hints.ai_socktype = SOCK_DGRAM;
    Hints.ai_protocol = IPPROTO_UDP;
    Error = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (Error != 0)
    {
        return ZpStatus_FromCode(ZpStatusWinsock, (ULONG)Error);
    }
    Error = GetAddrInfoW(Host, NULL, &Hints, &Result);
    if (Error != 0)
    {
        WSACleanup();
        return ZpStatus_FromCode(ZpStatusWinsock, (ULONG)Error);
    }
    if (Result->ai_addrlen > sizeof(*Address))
    {
        FreeAddrInfoW(Result);
        WSACleanup();
        return ZpStatus_FromNtStatus(STATUS_INVALID_ADDRESS);
    }
    RtlCopyMemory(Address, Result->ai_addr, Result->ai_addrlen);
    FreeAddrInfoW(Result);
    WSACleanup();
    QuicAddrSetPort(Address, Port);
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}
