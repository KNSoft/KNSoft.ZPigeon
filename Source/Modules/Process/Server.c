#include <KNSoft/ZPigeon/Server.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef union _ZP_PROCESS_CALLBACK
{
    ZP_PROCESS_ENUMERATE_CALLBACK Enumerate;
    ZP_PROCESS_QUERY_CALLBACK Query;
    ZP_REQUEST_STATUS_CALLBACK Status;
} ZP_PROCESS_CALLBACK;

typedef struct _ZP_PROCESS_CONTEXT
{
    ZP_PROCESS_CALLBACK Callback;
    PVOID Context;
} ZP_PROCESS_CONTEXT, *PZP_PROCESS_CONTEXT;

static
VOID
NTAPI
ZpProcess_EnumerateComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_PROCESS_CONTEXT ProcessContext = Context;
    ZP_PROCESS_LIST_VIEW Processes;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpProcess_DecodeList(Payload->Buffer,
                                 Payload->Length,
                                 &Processes));
    }
    ProcessContext->Callback.Enumerate(
        Request,
        Status,
        ZpStatus_IsSuccess(Status) ? &Processes : NULL,
        ProcessContext->Context);
    Mem_Free(ProcessContext);
}

static
VOID
NTAPI
ZpProcess_QueryComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_PROCESS_CONTEXT ProcessContext = Context;
    ZP_PROCESS_INFO_VIEW Info;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpProcess_DecodeInfo(Payload->Buffer,
                                 Payload->Length,
                                 &Info));
    }
    ProcessContext->Callback.Query(Request,
                                   Status,
                                   ZpStatus_IsSuccess(Status) ? &Info : NULL,
                                   ProcessContext->Context);
    Mem_Free(ProcessContext);
}

static
VOID
NTAPI
ZpProcess_StatusComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_PROCESS_CONTEXT ProcessContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    ProcessContext->Callback.Status(Request,
                                    Status,
                                    ProcessContext->Context);
    Mem_Free(ProcessContext);
}

static
NTSTATUS
ZpProcess_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ USHORT OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Complete,
    _In_ ZP_PROCESS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_PROCESS_CONTEXT ProcessContext;
    NTSTATUS Status;

    ProcessContext = Mem_Alloc(sizeof(*ProcessContext));
    if (ProcessContext == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    ProcessContext->Callback = Callback;
    ProcessContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_PROCESS_MODULE_ID,
                                  OperationId,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  Complete,
                                  ProcessContext,
                                  Request);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(ProcessContext);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpServer_EnumerateProcesses(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_PROCESS_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_PROCESS_CALLBACK ProcessCallback;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ProcessCallback.Enumerate = Callback;
    return ZpProcess_Send(Connection,
                          ZP_PROCESS_OPERATION_ENUMERATE,
                          TimeoutMilliseconds,
                          NULL,
                          0,
                          ZpProcess_EnumerateComplete,
                          ProcessCallback,
                          Context,
                          Request);
}

NTSTATUS
NTAPI
ZpServer_QueryProcess(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_PROCESS_QUERY_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_PROCESS_CALLBACK ProcessCallback;
    BYTE Payload[sizeof(ProcessId) + sizeof(CreateTime)];
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ProcessCallback.Query = Callback;
    Status = ZpProcess_EncodeQuery(ProcessId,
                                   CreateTime,
                                   Payload,
                                   sizeof(Payload),
                                   &PayloadLength);
    return NT_SUCCESS(Status) ?
               ZpProcess_Send(Connection,
                              ZP_PROCESS_OPERATION_QUERY,
                              TimeoutMilliseconds,
                              Payload,
                              PayloadLength,
                              ZpProcess_QueryComplete,
                              ProcessCallback,
                              Context,
                              Request) :
               Status;
}

NTSTATUS
NTAPI
ZpServer_TerminateProcess(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONG ExitCode,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_PROCESS_CALLBACK ProcessCallback;
    BYTE Payload[2 * sizeof(ULONG) + sizeof(ULONGLONG)];
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ProcessCallback.Status = Callback;
    Status = ZpProcess_EncodeTerminate(ProcessId,
                                       CreateTime,
                                       ExitCode,
                                       Payload,
                                       sizeof(Payload),
                                       &PayloadLength);
    return NT_SUCCESS(Status) ?
               ZpProcess_Send(Connection,
                              ZP_PROCESS_OPERATION_TERMINATE,
                              TimeoutMilliseconds,
                              Payload,
                              PayloadLength,
                              ZpProcess_StatusComplete,
                              ProcessCallback,
                              Context,
                              Request) :
               Status;
}
