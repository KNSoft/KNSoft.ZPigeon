#pragma once

#include <KNSoft.Quic.h>
#include <KNSoft/MakeLifeEasier/String/Encoding.h>

#include <Ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

#define ZP_QUIC_ALPN "knsoft-zpigeon/1"

static const QUIC_BUFFER ZpQuicAlpn = {
    sizeof(ZP_QUIC_ALPN) - sizeof(ANSI_NULL),
    (PBYTE)ZP_QUIC_ALPN
};

static
NTSTATUS
ZpQuic_StatusToNtStatus(
    _In_ QUIC_STATUS QuicStatus)
{
    if (QUIC_SUCCEEDED(QuicStatus))
    {
        return STATUS_SUCCESS;
    }
    if (QuicStatus == QUIC_STATUS_OUT_OF_MEMORY)
    {
        return STATUS_NO_MEMORY;
    }
    if (QuicStatus == QUIC_STATUS_INVALID_PARAMETER)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (QuicStatus == QUIC_STATUS_INVALID_STATE)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (QuicStatus == QUIC_STATUS_NOT_SUPPORTED)
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (QuicStatus == QUIC_STATUS_BUFFER_TOO_SMALL)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (QuicStatus == QUIC_STATUS_ABORTED || QuicStatus == QUIC_STATUS_USER_CANCELED)
    {
        return STATUS_CANCELLED;
    }
    if (QuicStatus == QUIC_STATUS_CONNECTION_TIMEOUT || QuicStatus == QUIC_STATUS_CONNECTION_IDLE)
    {
        return STATUS_IO_TIMEOUT;
    }
    if (QuicStatus == QUIC_STATUS_UNREACHABLE)
    {
        return STATUS_HOST_UNREACHABLE;
    }
    if (QuicStatus == QUIC_STATUS_CONNECTION_REFUSED)
    {
        return STATUS_CONNECTION_REFUSED;
    }
    if (QuicStatus == QUIC_STATUS_HANDSHAKE_FAILURE ||
        QuicStatus == QUIC_STATUS_TLS_ERROR ||
        QuicStatus == QUIC_STATUS_BAD_CERTIFICATE ||
        QuicStatus == QUIC_STATUS_UNSUPPORTED_CERTIFICATE ||
        QuicStatus == QUIC_STATUS_REVOKED_CERTIFICATE ||
        QuicStatus == QUIC_STATUS_EXPIRED_CERTIFICATE ||
        QuicStatus == QUIC_STATUS_UNKNOWN_CERTIFICATE ||
        QuicStatus == QUIC_STATUS_REQUIRED_CERTIFICATE ||
        QuicStatus == QUIC_STATUS_CERT_EXPIRED ||
        QuicStatus == QUIC_STATUS_CERT_UNTRUSTED_ROOT ||
        QuicStatus == QUIC_STATUS_CERT_NO_CERT)
    {
        return STATUS_TRUST_FAILURE;
    }
    if (HRESULT_FACILITY(QuicStatus) == FACILITY_WIN32)
    {
        return NTSTATUS_FROM_WIN32(HRESULT_CODE(QuicStatus));
    }
    return STATUS_CONNECTION_DISCONNECTED;
}

static
NTSTATUS
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
        return STATUS_SUCCESS;
    }

    Hints.ai_family = AF_UNSPEC;
    Hints.ai_socktype = SOCK_DGRAM;
    Hints.ai_protocol = IPPROTO_UDP;
    Error = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (Error != 0)
    {
        return NTSTATUS_FROM_WIN32(Error);
    }
    Error = GetAddrInfoW(Host, NULL, &Hints, &Result);
    if (Error != 0)
    {
        WSACleanup();
        return NTSTATUS_FROM_WIN32(Error);
    }
    if (Result->ai_addrlen > sizeof(*Address))
    {
        FreeAddrInfoW(Result);
        WSACleanup();
        return STATUS_INVALID_ADDRESS;
    }
    RtlCopyMemory(Address, Result->ai_addr, Result->ai_addrlen);
    FreeAddrInfoW(Result);
    WSACleanup();
    QuicAddrSetPort(Address, Port);
    return STATUS_SUCCESS;
}
