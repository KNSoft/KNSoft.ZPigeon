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
    QUIC_BUFFER Buffer;
    BYTE Frame[ANYSIZE_ARRAY];
} ZP_QUIC_SEND_CONTEXT, *PZP_QUIC_SEND_CONTEXT;

static
NTSTATUS
ZpQuic_SendFrame(
    _In_ HQUIC Stream,
    _Inout_ PZP_CONNECTION Connection,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_opt_ QUIC_STATUS* SendStatus)
{
    PZP_QUIC_SEND_CONTEXT Send;
    QUIC_STATUS QuicStatus;
    NTSTATUS Status;
    ULONG FrameSize, BytesWritten;

    Status = ZpFrame_GetSize(BodyLength, &FrameSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Send = Mem_Alloc(FIELD_OFFSET(ZP_QUIC_SEND_CONTEXT, Frame) + FrameSize);
    if (Send == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = ZpFrame_Encode(MessageType,
                            Body,
                            BodyLength,
                            Send->Frame,
                            FrameSize,
                            &BytesWritten);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Send);
        return Status;
    }
    Send->Buffer.Buffer = Send->Frame;
    Send->Buffer.Length = BytesWritten;
    QuicStatus = MsQuicStreamSend(Stream,
                                  &Send->Buffer,
                                  1,
                                  QUIC_SEND_FLAG_NONE,
                                  Send);
    if (QUIC_FAILED(QuicStatus))
    {
        if (SendStatus != NULL)
        {
            *SendStatus = QuicStatus;
        }
        Mem_Free(Send);
        return STATUS_CONNECTION_DISCONNECTED;
    }
    return ZpConnection_NotifyMessageSent(Connection, MessageType);
}

static
VOID
ZpQuic_CompleteSend(
    _In_opt_ PVOID Context)
{
    Mem_Free(Context);
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
