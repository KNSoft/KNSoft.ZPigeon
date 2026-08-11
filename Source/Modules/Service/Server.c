#include <KNSoft/ZPigeon/Server.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef union _ZP_SERVICE_CALLBACK
{
    ZP_SERVICE_ENUMERATE_CALLBACK Enumerate;
    ZP_SERVICE_QUERY_CALLBACK Query;
    ZP_REQUEST_STATUS_CALLBACK Status;
} ZP_SERVICE_CALLBACK;

typedef struct _ZP_SERVICE_CONTEXT
{
    ZP_SERVICE_CALLBACK Callback;
    PVOID Context;
} ZP_SERVICE_CONTEXT, *PZP_SERVICE_CONTEXT;

static
VOID
NTAPI
ZpService_EnumerateComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVICE_CONTEXT ServiceContext = Context;
    ZP_SERVICE_LIST_VIEW Services;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpService_DecodeList(Payload->Buffer,
                                 Payload->Length,
                                 &Services));
    }
    ServiceContext->Callback.Enumerate(
        Request,
        Status,
        ZpStatus_IsSuccess(Status) ? &Services : NULL,
        ServiceContext->Context);
    Mem_Free(ServiceContext);
}

static
VOID
NTAPI
ZpService_QueryComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVICE_CONTEXT ServiceContext = Context;
    ZP_SERVICE_INFO_VIEW Info;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpService_DecodeInfo(Payload->Buffer,
                                 Payload->Length,
                                 &Info));
    }
    ServiceContext->Callback.Query(Request,
                                   Status,
                                   ZpStatus_IsSuccess(Status) ? &Info : NULL,
                                   ServiceContext->Context);
    Mem_Free(ServiceContext);
}

static
VOID
NTAPI
ZpService_StatusComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVICE_CONTEXT ServiceContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    ServiceContext->Callback.Status(Request,
                                    Status,
                                    ServiceContext->Context);
    Mem_Free(ServiceContext);
}

static
NTSTATUS
ZpService_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ USHORT OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Complete,
    _In_ ZP_SERVICE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVICE_CONTEXT ServiceContext;
    NTSTATUS Status;

    ServiceContext = Mem_Alloc(sizeof(*ServiceContext));
    if (ServiceContext == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    ServiceContext->Callback = Callback;
    ServiceContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_SERVICE_MODULE_ID,
                                  OperationId,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  Complete,
                                  ServiceContext,
                                  Request);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(ServiceContext);
    }
    return Status;
}

static
NTSTATUS
ZpService_SendName(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ USHORT OperationId,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Complete,
    _In_ ZP_SERVICE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    Status = ZpService_EncodeQuery(ServiceName,
                                   ServiceNameLength,
                                   NULL,
                                   0,
                                   &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpService_EncodeQuery(ServiceName,
                                       ServiceNameLength,
                                       Payload,
                                       PayloadLength,
                                       &PayloadLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpService_Send(Connection,
                                OperationId,
                                TimeoutMilliseconds,
                                Payload,
                                PayloadLength,
                                Complete,
                                Callback,
                                Context,
                                Request);
    }
    if (Payload != NULL)
    {
        Mem_Free(Payload);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpServer_EnumerateServices(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_SERVICE_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_SERVICE_CALLBACK ServiceCallback;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ServiceCallback.Enumerate = Callback;
    return ZpService_Send(Connection,
                          ZP_SERVICE_OPERATION_ENUMERATE,
                          TimeoutMilliseconds,
                          NULL,
                          0,
                          ZpService_EnumerateComplete,
                          ServiceCallback,
                          Context,
                          Request);
}

NTSTATUS
NTAPI
ZpServer_QueryService(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_SERVICE_QUERY_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_SERVICE_CALLBACK ServiceCallback;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ServiceCallback.Query = Callback;
    return ZpService_SendName(Connection,
                              ZP_SERVICE_OPERATION_QUERY,
                              ServiceName,
                              ServiceNameLength,
                              TimeoutMilliseconds,
                              ZpService_QueryComplete,
                              ServiceCallback,
                              Context,
                              Request);
}

static
NTSTATUS
ZpService_SendControl(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ USHORT OperationId,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_SERVICE_CALLBACK ServiceCallback;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ServiceCallback.Status = Callback;
    return ZpService_SendName(Connection,
                              OperationId,
                              ServiceName,
                              ServiceNameLength,
                              TimeoutMilliseconds,
                              ZpService_StatusComplete,
                              ServiceCallback,
                              Context,
                              Request);
}

NTSTATUS
NTAPI
ZpServer_StartService(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpService_SendControl(Connection,
                                 ZP_SERVICE_OPERATION_START,
                                 ServiceName,
                                 ServiceNameLength,
                                 TimeoutMilliseconds,
                                 Callback,
                                 Context,
                                 Request);
}

NTSTATUS
NTAPI
ZpServer_StopService(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpService_SendControl(Connection,
                                 ZP_SERVICE_OPERATION_STOP,
                                 ServiceName,
                                 ServiceNameLength,
                                 TimeoutMilliseconds,
                                 Callback,
                                 Context,
                                 Request);
}
