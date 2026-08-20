#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_RTC_MODULE_ID 16
#define ZP_RTC_MODULE_VERSION 1

#define ZP_RTC_OPERATION_OPEN 1
#define ZP_RTC_OPERATION_CLOSE 2

#define ZP_RTC_SESSION_ID_SIZE 16
#define ZP_RTC_MAX_SDP_LENGTH 0x00010000UL
#define ZP_RTC_MAX_ICE_SERVERS 8
#define ZP_RTC_MAX_ICE_SERVER_LENGTH 1024
#define ZP_RTC_DATA_CHUNK_SIZE 0x0000F000UL

typedef struct _ZP_RTC_ICE_SERVER
{
    PCWCH Url;
    ULONG UrlLength;
} ZP_RTC_ICE_SERVER, *PZP_RTC_ICE_SERVER;

typedef const ZP_RTC_ICE_SERVER* PCZP_RTC_ICE_SERVER;

typedef struct _ZP_RTC_OPEN_REQUEST_VIEW
{
    const BYTE* SessionId;
    ZP_STRING_VIEW Offer;
    const BYTE* IceServers;
    ULONG IceServersLength;
    ULONG IceServerCount;
} ZP_RTC_OPEN_REQUEST_VIEW, *PZP_RTC_OPEN_REQUEST_VIEW;

typedef const ZP_RTC_OPEN_REQUEST_VIEW* PCZP_RTC_OPEN_REQUEST_VIEW;

NTSTATUS
ZpRtc_EncodeOpenRequest(
    _In_reads_(ZP_RTC_SESSION_ID_SIZE) const BYTE* SessionId,
    _In_reads_(OfferLength) PCWCH Offer,
    _In_ ULONG OfferLength,
    _In_reads_opt_(IceServerCount) PCZP_RTC_ICE_SERVER IceServers,
    _In_ ULONG IceServerCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRtc_DecodeOpenRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_RTC_OPEN_REQUEST_VIEW Request);

NTSTATUS
ZpRtc_GetIceServer(
    _In_ PCZP_RTC_OPEN_REQUEST_VIEW Request,
    _In_ ULONG Index,
    _Out_ PZP_STRING_VIEW IceServer);

NTSTATUS
ZpRtc_EncodeAnswer(
    _In_reads_(AnswerLength) PCWCH Answer,
    _In_ ULONG AnswerLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRtc_DecodeAnswer(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Answer);

NTSTATUS
ZpRtc_EncodeSessionId(
    _In_reads_(ZP_RTC_SESSION_ID_SIZE) const BYTE* SessionId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpRtc_DecodeSessionId(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Outptr_result_bytebuffer_(ZP_RTC_SESSION_ID_SIZE) const BYTE** SessionId);

EXTERN_C_END
