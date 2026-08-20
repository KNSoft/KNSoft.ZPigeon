#include <KNSoft/ZPigeon/Server.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef union _ZP_RECORDING_CALLBACK
{
    ZP_RECORDING_CAPABILITIES_CALLBACK Capabilities;
    ZP_RECORDING_RECORDS_CALLBACK Records;
    ZP_REQUEST_STATUS_CALLBACK Status;
} ZP_RECORDING_CALLBACK;

typedef struct _ZP_RECORDING_CONTEXT
{
    ZP_RECORDING_CALLBACK Callback;
    PVOID Context;
} ZP_RECORDING_CONTEXT, *PZP_RECORDING_CONTEXT;

static
VOID
NTAPI
ZpRecording_CapabilitiesComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_RECORDING_CONTEXT RecordingContext = Context;
    ULONG Codecs = 0;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpRecording_DecodeCapabilities(Payload->Buffer,
                                                                      Payload->Length,
                                                                      &Codecs));
    }
    RecordingContext->Callback.Capabilities(Request,
                                             Status,
                                             Codecs,
                                             RecordingContext->Context);
    Mem_Free(RecordingContext);
}

static
VOID
NTAPI
ZpRecording_RecordsComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_RECORDING_CONTEXT RecordingContext = Context;
    ZP_RECORDING_LIST_VIEW Records;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpRecording_DecodeRecords(Payload->Buffer,
                                                                 Payload->Length,
                                                                 &Records));
    }
    RecordingContext->Callback.Records(Request,
                                        Status,
                                        ZpStatus_IsSuccess(Status) ? &Records : NULL,
                                        RecordingContext->Context);
    Mem_Free(RecordingContext);
}

static
VOID
NTAPI
ZpRecording_StatusComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_RECORDING_CONTEXT RecordingContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0) Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    RecordingContext->Callback.Status(Request, Status, RecordingContext->Context);
    Mem_Free(RecordingContext);
}

static
NTSTATUS
ZpRecording_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Complete,
    _In_ ZP_RECORDING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_RECORDING_CONTEXT RecordingContext;
    NTSTATUS Status;

    RecordingContext = Mem_Alloc(sizeof(*RecordingContext));
    if (RecordingContext == NULL) return STATUS_NO_MEMORY;
    RecordingContext->Callback = Callback;
    RecordingContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_RECORDING_MODULE_ID,
                                  OperationId,
                                  30000,
                                  Payload,
                                  PayloadLength,
                                  Complete,
                                  RecordingContext,
                                  Request);
    if (!NT_SUCCESS(Status)) Mem_Free(RecordingContext);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_QueryRecordingCapabilities(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_RECORDING_CAPABILITIES_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_RECORDING_CALLBACK RecordingCallback;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    RecordingCallback.Capabilities = Callback;
    return ZpRecording_Send(Connection,
                            ZP_RECORDING_OPERATION_QUERY_CAPABILITIES,
                            NULL,
                            0,
                            ZpRecording_CapabilitiesComplete,
                            RecordingCallback,
                            Context,
                            Request);
}

NTSTATUS
NTAPI
ZpServer_StartRecording(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ PCZP_RECORDING_START Start,
    _In_ ZP_RECORDING_RECORDS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_RECORDING_CALLBACK RecordingCallback;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpRecording_EncodeStart(Start, NULL, 0, &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status)) Status = ZpRecording_EncodeStart(Start, Payload, PayloadLength, &PayloadLength);
    if (NT_SUCCESS(Status))
    {
        RecordingCallback.Records = Callback;
        Status = ZpRecording_Send(Connection,
                                  ZP_RECORDING_OPERATION_START,
                                  Payload,
                                  PayloadLength,
                                  ZpRecording_RecordsComplete,
                                  RecordingCallback,
                                  Context,
                                  Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_EnumerateRecordings(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_RECORDING_RECORDS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_RECORDING_CALLBACK RecordingCallback;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    RecordingCallback.Records = Callback;
    return ZpRecording_Send(Connection,
                            ZP_RECORDING_OPERATION_ENUMERATE,
                            NULL,
                            0,
                            ZpRecording_RecordsComplete,
                            RecordingCallback,
                            Context,
                            Request);
}

static
NTSTATUS
ZpRecording_SendId(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_ ULONG RecordingId,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_RECORDING_CALLBACK RecordingCallback;
    BYTE Payload[sizeof(ULONG)];
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpRecording_EncodeId(RecordingId, Payload, sizeof(Payload), &PayloadLength);
    if (!NT_SUCCESS(Status)) return Status;
    RecordingCallback.Status = Callback;
    return ZpRecording_Send(Connection,
                            OperationId,
                            Payload,
                            PayloadLength,
                            ZpRecording_StatusComplete,
                            RecordingCallback,
                            Context,
                            Request);
}

NTSTATUS
NTAPI
ZpServer_StopRecording(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG RecordingId,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpRecording_SendId(Connection,
                              ZP_RECORDING_OPERATION_STOP,
                              RecordingId,
                              Callback,
                              Context,
                              Request);
}

NTSTATUS
NTAPI
ZpServer_DeleteRecording(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG RecordingId,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpRecording_SendId(Connection,
                              ZP_RECORDING_OPERATION_DELETE,
                              RecordingId,
                              Callback,
                              Context,
                              Request);
}
