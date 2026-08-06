#include <KNSoft/ZPigeon/Server.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef struct _ZP_RTC_CONTEXT
{
    union
    {
        ZP_STRING_CALLBACK String;
        ZP_REQUEST_STATUS_CALLBACK Status;
    } Callback;
    PVOID Context;
} ZP_RTC_CONTEXT, *PZP_RTC_CONTEXT;

static
VOID
NTAPI
ZpRtc_OpenComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_RTC_CONTEXT RtcContext = Context;
    ZP_STRING_VIEW Answer;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpRtc_DecodeAnswer(Payload->Buffer, Payload->Length, &Answer));
    }
    RtcContext->Callback.String(Request,
                                Status,
                                ZpStatus_IsSuccess(Status) ? &Answer : NULL,
                                RtcContext->Context);
    Mem_Free(RtcContext);
}

static
VOID
NTAPI
ZpRtc_CloseComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_RTC_CONTEXT RtcContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    RtcContext->Callback.Status(Request, Status, RtcContext->Context);
    Mem_Free(RtcContext);
}

NTSTATUS
NTAPI
ZpServer_OpenRtc(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ZP_RTC_SESSION_ID_SIZE) const BYTE* SessionId,
    _In_reads_(OfferLength) PCWCH Offer,
    _In_ ULONG OfferLength,
    _In_reads_opt_(IceServerCount) PCZP_RTC_ICE_SERVER IceServers,
    _In_ ULONG IceServerCount,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_RTC_CONTEXT RtcContext;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpRtc_EncodeOpenRequest(SessionId,
                                     Offer,
                                     OfferLength,
                                     IceServers,
                                     IceServerCount,
                                     NULL,
                                     0,
                                     &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    RtcContext = Payload != NULL ? Mem_Alloc(sizeof(*RtcContext)) : NULL;
    if (!NT_SUCCESS(Status) || Payload == NULL || RtcContext == NULL)
    {
        Mem_Free(RtcContext);
        Mem_Free(Payload);
        return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    }
    Status = ZpRtc_EncodeOpenRequest(SessionId,
                                     Offer,
                                     OfferLength,
                                     IceServers,
                                     IceServerCount,
                                     Payload,
                                     PayloadLength,
                                     &PayloadLength);
    if (NT_SUCCESS(Status))
    {
        RtcContext->Callback.String = Callback;
        RtcContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_RTC_MODULE_ID,
                                      ZP_RTC_OPERATION_OPEN,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpRtc_OpenComplete,
                                      RtcContext,
                                      Request);
    }
    Mem_Free(Payload);
    if (!NT_SUCCESS(Status)) Mem_Free(RtcContext);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_CloseRtc(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ZP_RTC_SESSION_ID_SIZE) const BYTE* SessionId,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_RTC_CONTEXT RtcContext;
    BYTE Payload[ZP_RTC_SESSION_ID_SIZE];
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpRtc_EncodeSessionId(SessionId, Payload, sizeof(Payload), &PayloadLength);
    RtcContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*RtcContext)) : NULL;
    if (NT_SUCCESS(Status) && RtcContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        RtcContext->Callback.Status = Callback;
        RtcContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_RTC_MODULE_ID,
                                      ZP_RTC_OPERATION_CLOSE,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpRtc_CloseComplete,
                                      RtcContext,
                                      Request);
    }
    if (!NT_SUCCESS(Status)) Mem_Free(RtcContext);
    return Status;
}
