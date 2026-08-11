#include <KNSoft/ZPigeon/Server.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef struct _ZP_SYSTEM_INFO_CONTEXT
{
    ZP_SYSTEM_INFO_CALLBACK Callback;
    PVOID Context;
} ZP_SYSTEM_INFO_CONTEXT, *PZP_SYSTEM_INFO_CONTEXT;

static
VOID
NTAPI
ZpSystem_InfoComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SYSTEM_INFO_CONTEXT SystemContext = Context;
    ZP_SYSTEM_INFO_VIEW Info;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpSystem_DecodeInfo(Payload->Buffer,
                                Payload->Length,
                                &Info));
    }
    SystemContext->Callback(Request,
                            Status,
                            ZpStatus_IsSuccess(Status) ? &Info : NULL,
                            SystemContext->Context);
    Mem_Free(SystemContext);
}

NTSTATUS
NTAPI
ZpServer_GetSystemInfo(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_SYSTEM_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SYSTEM_INFO_CONTEXT SystemContext;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    SystemContext = Mem_Alloc(sizeof(*SystemContext));
    if (SystemContext == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    SystemContext->Callback = Callback;
    SystemContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_SYSTEM_MODULE_ID,
                                  ZP_SYSTEM_OPERATION_INFO,
                                  TimeoutMilliseconds,
                                  NULL,
                                  0,
                                  ZpSystem_InfoComplete,
                                  SystemContext,
                                  Request);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(SystemContext);
    }
    return Status;
}
