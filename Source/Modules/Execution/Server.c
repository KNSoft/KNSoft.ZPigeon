#include <KNSoft/ZPigeon/Server.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef union _ZP_EXECUTION_CALLBACK
{
    ZP_EXECUTION_SESSIONS_CALLBACK Sessions;
    ZP_EXECUTION_ENVIRONMENT_CALLBACK Environment;
    ZP_EXECUTION_IMAGE_CALLBACK Image;
    ZP_EXECUTION_JOBS_CALLBACK Jobs;
    ZP_EXECUTION_STAGING_CALLBACK Staging;
    ZP_REQUEST_STATUS_CALLBACK Status;
} ZP_EXECUTION_CALLBACK;

typedef struct _ZP_EXECUTION_CONTEXT
{
    ZP_EXECUTION_CALLBACK Callback;
    PVOID Context;
} ZP_EXECUTION_CONTEXT, *PZP_EXECUTION_CONTEXT;

static
VOID
NTAPI
ZpExecution_EnvironmentComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_EXECUTION_CONTEXT ExecutionContext = Context;
    ZP_EXECUTION_ENVIRONMENT_VIEW Environment;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpExecution_DecodeEnvironment(Payload->Buffer,
                                                                     Payload->Length,
                                                                     &Environment));
    }
    ExecutionContext->Callback.Environment(Request,
                                            Status,
                                            ZpStatus_IsSuccess(Status) ? &Environment : NULL,
                                            ExecutionContext->Context);
    Mem_Free(ExecutionContext);
}

static
VOID
NTAPI
ZpExecution_ImageComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_EXECUTION_CONTEXT ExecutionContext = Context;
    ZP_EXECUTION_IMAGE_INFO Image;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpExecution_DecodeImageInfo(Payload->Buffer,
                                                                   Payload->Length,
                                                                   &Image));
    }
    ExecutionContext->Callback.Image(Request,
                                      Status,
                                      ZpStatus_IsSuccess(Status) ? &Image : NULL,
                                      ExecutionContext->Context);
    Mem_Free(ExecutionContext);
}

static
VOID
NTAPI
ZpExecution_SessionsComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_EXECUTION_CONTEXT ExecutionContext = Context;
    ZP_EXECUTION_SESSION_LIST_VIEW Sessions;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpExecution_DecodeSessions(Payload->Buffer,
                                                                  Payload->Length,
                                                                  &Sessions));
    }
    ExecutionContext->Callback.Sessions(Request,
                                         Status,
                                         ZpStatus_IsSuccess(Status) ? &Sessions : NULL,
                                         ExecutionContext->Context);
    Mem_Free(ExecutionContext);
}

static
VOID
NTAPI
ZpExecution_JobsComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_EXECUTION_CONTEXT ExecutionContext = Context;
    ZP_EXECUTION_JOB_LIST_VIEW Jobs;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpExecution_DecodeJobs(Payload->Buffer,
                                                              Payload->Length,
                                                              &Jobs));
    }
    ExecutionContext->Callback.Jobs(Request,
                                     Status,
                                     ZpStatus_IsSuccess(Status) ? &Jobs : NULL,
                                     ExecutionContext->Context);
    Mem_Free(ExecutionContext);
}

static
VOID
NTAPI
ZpExecution_StagingComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_EXECUTION_CONTEXT ExecutionContext = Context;
    ZP_STRING_VIEW Path;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpExecution_DecodeStaging(Payload->Buffer,
                                                                 Payload->Length,
                                                                 &Path));
    }
    ExecutionContext->Callback.Staging(Request,
                                        Status,
                                        ZpStatus_IsSuccess(Status) ? &Path : NULL,
                                        ExecutionContext->Context);
    Mem_Free(ExecutionContext);
}

static
VOID
NTAPI
ZpExecution_StatusComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_EXECUTION_CONTEXT ExecutionContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0) Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    ExecutionContext->Callback.Status(Request, Status, ExecutionContext->Context);
    Mem_Free(ExecutionContext);
}

static
NTSTATUS
ZpExecution_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Complete,
    _In_ ZP_EXECUTION_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_EXECUTION_CONTEXT ExecutionContext;
    NTSTATUS Status;

    ExecutionContext = Mem_Alloc(sizeof(*ExecutionContext));
    if (ExecutionContext == NULL) return STATUS_NO_MEMORY;
    ExecutionContext->Callback = Callback;
    ExecutionContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_EXECUTION_MODULE_ID,
                                  OperationId,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  Complete,
                                  ExecutionContext,
                                  Request);
    if (!NT_SUCCESS(Status)) Mem_Free(ExecutionContext);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_EnumerateExecutionSessions(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EXECUTION_SESSIONS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_EXECUTION_CALLBACK ExecutionCallback;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    ExecutionCallback.Sessions = Callback;
    return ZpExecution_Send(Connection,
                            ZP_EXECUTION_OPERATION_ENUMERATE_SESSIONS,
                            TimeoutMilliseconds,
                            NULL,
                            0,
                            ZpExecution_SessionsComplete,
                            ExecutionCallback,
                            Context,
                            Request);
}

NTSTATUS
NTAPI
ZpServer_QueryExecutionEnvironment(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EXECUTION_ENVIRONMENT_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_EXECUTION_CALLBACK ExecutionCallback;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    ExecutionCallback.Environment = Callback;
    return ZpExecution_Send(Connection,
                            ZP_EXECUTION_OPERATION_QUERY_ENVIRONMENT,
                            TimeoutMilliseconds,
                            NULL,
                            0,
                            ZpExecution_EnvironmentComplete,
                            ExecutionCallback,
                            Context,
                            Request);
}

NTSTATUS
NTAPI
ZpServer_QueryExecutionImage(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EXECUTION_IMAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_EXECUTION_CALLBACK ExecutionCallback;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpExecution_EncodeStaging(Path, PathLength, NULL, 0, &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpExecution_EncodeStaging(Path, PathLength, Payload, PayloadLength, &PayloadLength);
    }
    if (NT_SUCCESS(Status))
    {
        ExecutionCallback.Image = Callback;
        Status = ZpExecution_Send(Connection,
                                  ZP_EXECUTION_OPERATION_QUERY_IMAGE,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  ZpExecution_ImageComplete,
                                  ExecutionCallback,
                                  Context,
                                  Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_StartExecution(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ PCZP_EXECUTION_START Start,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EXECUTION_JOBS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_EXECUTION_CALLBACK ExecutionCallback;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpExecution_EncodeStart(Start, NULL, 0, &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status)) Status = ZpExecution_EncodeStart(Start, Payload, PayloadLength, &PayloadLength);
    if (NT_SUCCESS(Status))
    {
        ExecutionCallback.Jobs = Callback;
        Status = ZpExecution_Send(Connection,
                                  ZP_EXECUTION_OPERATION_START,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  ZpExecution_JobsComplete,
                                  ExecutionCallback,
                                  Context,
                                  Request);
    }
    if (Payload != NULL)
    {
        RtlSecureZeroMemory(Payload, PayloadLength);
        Mem_Free(Payload);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpServer_EnumerateExecutionJobs(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EXECUTION_JOBS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_EXECUTION_CALLBACK ExecutionCallback;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    ExecutionCallback.Jobs = Callback;
    return ZpExecution_Send(Connection,
                            ZP_EXECUTION_OPERATION_ENUMERATE_JOBS,
                            TimeoutMilliseconds,
                            NULL,
                            0,
                            ZpExecution_JobsComplete,
                            ExecutionCallback,
                            Context,
                            Request);
}

NTSTATUS
NTAPI
ZpServer_TerminateExecution(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG JobId,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_EXECUTION_CALLBACK ExecutionCallback;
    BYTE Payload[sizeof(ULONGLONG)];
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpExecution_EncodeJobId(JobId, Payload, sizeof(Payload), &PayloadLength);
    if (!NT_SUCCESS(Status)) return Status;
    ExecutionCallback.Status = Callback;
    return ZpExecution_Send(Connection,
                            ZP_EXECUTION_OPERATION_TERMINATE,
                            TimeoutMilliseconds,
                            Payload,
                            PayloadLength,
                            ZpExecution_StatusComplete,
                            ExecutionCallback,
                            Context,
                            Request);
}

NTSTATUS
NTAPI
ZpServer_CreateExecutionStaging(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EXECUTION_STAGING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_EXECUTION_CALLBACK ExecutionCallback;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpExecution_EncodeStaging(Name, NameLength, NULL, 0, &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
        Status = ZpExecution_EncodeStaging(Name,
                                           NameLength,
                                           Payload,
                                           PayloadLength,
                                           &PayloadLength);
    if (NT_SUCCESS(Status))
    {
        ExecutionCallback.Staging = Callback;
        Status = ZpExecution_Send(Connection,
                                  ZP_EXECUTION_OPERATION_CREATE_STAGING,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  ZpExecution_StagingComplete,
                                  ExecutionCallback,
                                  Context,
                                  Request);
    }
    Mem_Free(Payload);
    return Status;
}
