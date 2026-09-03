#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Rtc.h"

#include "../../KNSoft.ZPigeon.Protocol/Core/Protocol.inl"

static
LOGICAL
ZpRtc_IsStringValid(
    _In_reads_opt_(Length) PCWCH String,
    _In_ ULONG Length,
    _In_ ULONG MaximumLength)
{
    return Length != 0 && Length <= MaximumLength && String != NULL;
}

NTSTATUS
ZpRtc_EncodeOpenRequest(
    _In_reads_(ZP_RTC_SESSION_ID_SIZE) const BYTE* SessionId,
    _In_reads_(OfferLength) PCWCH Offer,
    _In_ ULONG OfferLength,
    _In_reads_opt_(IceServerCount) PCZP_RTC_ICE_SERVER IceServers,
    _In_ ULONG IceServerCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;
    ULONG Index;

    if (SessionId == NULL || !ZpRtc_IsStringValid(Offer, OfferLength, ZP_RTC_MAX_SDP_LENGTH) ||
        IceServerCount > ZP_RTC_MAX_ICE_SERVERS || (IceServerCount != 0 && IceServers == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < IceServerCount; Index++)
    {
        if (!ZpRtc_IsStringValid(IceServers[Index].Url,
                                 IceServers[Index].UrlLength,
                                 ZP_RTC_MAX_ICE_SERVER_LENGTH))
        {
            return STATUS_INVALID_PARAMETER;
        }
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteData(&Writer, SessionId, ZP_RTC_SESSION_ID_SIZE);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Offer, OfferLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, (BYTE)IceServerCount);
    for (Index = 0; NT_SUCCESS(Status) && Index < IceServerCount; Index++)
    {
        Status = ZpCodec_WriteString(&Writer, IceServers[Index].Url, IceServers[Index].UrlLength);
    }
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpRtc_DecodeOpenRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_RTC_OPEN_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    ZP_BUFFER_VIEW SessionId;
    ZP_STRING_VIEW IceServer;
    BYTE IceServerCount;
    NTSTATUS Status;
    ULONG Index;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadData(&Reader, ZP_RTC_SESSION_ID_SIZE, &SessionId);
    if (NT_SUCCESS(Status)) Request->SessionId = SessionId.Buffer;
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->Offer);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &IceServerCount);
    if (NT_SUCCESS(Status)) Request->IceServerCount = IceServerCount;
    if (NT_SUCCESS(Status) &&
        (Request->Offer.Length == 0 || Request->Offer.Length > ZP_RTC_MAX_SDP_LENGTH ||
         Request->IceServerCount > ZP_RTC_MAX_ICE_SERVERS))
    {
        return STATUS_DATA_ERROR;
    }
    Request->IceServers = Add2Ptr(Payload, Reader.Offset);
    for (Index = 0; NT_SUCCESS(Status) && Index < Request->IceServerCount; Index++)
    {
        Status = ZpCodec_ReadString(&Reader, &IceServer);
        if (NT_SUCCESS(Status) &&
            (IceServer.Length == 0 || IceServer.Length > ZP_RTC_MAX_ICE_SERVER_LENGTH))
        {
            Status = STATUS_DATA_ERROR;
        }
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    Request->IceServersLength = PayloadLength - (ULONG)(Request->IceServers - (const BYTE*)Payload);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpRtc_GetNextIceServer(
    _In_ PCZP_RTC_OPEN_REQUEST_VIEW Request,
    _Inout_ PULONG Offset,
    _Out_ PZP_STRING_VIEW IceServer)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= Request->IceServersLength) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader,
                             Add2Ptr(Request->IceServers, *Offset),
                             Request->IceServersLength - *Offset);
    Status = ZpCodec_ReadString(&Reader, IceServer);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

NTSTATUS
ZpRtc_EncodeAnswer(
    _In_reads_(AnswerLength) PCWCH Answer,
    _In_ ULONG AnswerLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (!ZpRtc_IsStringValid(Answer, AnswerLength, ZP_RTC_MAX_SDP_LENGTH)) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteTailString(&Writer, Answer, AnswerLength);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpRtc_DecodeAnswer(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Answer)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadTailString(&Reader, Answer);
    return NT_SUCCESS(Status) && Reader.Offset == PayloadLength && Answer->Length != 0 &&
           Answer->Length <= ZP_RTC_MAX_SDP_LENGTH ? STATUS_SUCCESS :
               NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
}

NTSTATUS
ZpRtc_EncodeSessionId(
    _In_reads_(ZP_RTC_SESSION_ID_SIZE) const BYTE* SessionId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (SessionId == NULL) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteData(&Writer, SessionId, ZP_RTC_SESSION_ID_SIZE);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpRtc_DecodeSessionId(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Outptr_result_bytebuffer_(ZP_RTC_SESSION_ID_SIZE) const BYTE** SessionId)
{
    ZP_CODEC_READER Reader;
    ZP_BUFFER_VIEW View;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadData(&Reader, ZP_RTC_SESSION_ID_SIZE, &View);
    if (NT_SUCCESS(Status) && Reader.Offset == PayloadLength) *SessionId = View.Buffer;
    return NT_SUCCESS(Status) && Reader.Offset == PayloadLength ? STATUS_SUCCESS :
               NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
}
