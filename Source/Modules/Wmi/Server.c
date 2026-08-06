#include <KNSoft/ZPigeon/Server.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef struct _ZP_WMI_CONTEXT
{
    ZP_WMI_PAGE_CALLBACK Callback;
    PVOID Context;
} ZP_WMI_CONTEXT, *PZP_WMI_CONTEXT;

static
VOID
NTAPI
ZpWmi_Complete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_WMI_CONTEXT WmiContext = Context;
    ZP_WMI_PAGE_VIEW Page;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpWmi_DecodePage(Payload->Buffer, Payload->Length, &Page));
    }
    WmiContext->Callback(Request,
                         Status,
                         ZpStatus_IsSuccess(Status) ? &Page : NULL,
                         WmiContext->Context);
    Mem_Free(WmiContext);
}

static
NTSTATUS
ZpWmi_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_ ULONG Limit,
    _In_ ULONG Flags,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WMI_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_WMI_CONTEXT WmiContext;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpWmi_EncodeRequest(Namespace,
                                 NamespaceLength,
                                 Query,
                                 QueryLength,
                                 Limit,
                                 Flags,
                                 NULL,
                                 0,
                                 &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (!NT_SUCCESS(Status) || Payload == NULL)
    {
        return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    }
    Status = ZpWmi_EncodeRequest(Namespace,
                                 NamespaceLength,
                                 Query,
                                 QueryLength,
                                 Limit,
                                 Flags,
                                 Payload,
                                 PayloadLength,
                                 &PayloadLength);
    WmiContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*WmiContext)) : NULL;
    if (NT_SUCCESS(Status) && WmiContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        WmiContext->Callback = Callback;
        WmiContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_WMI_MODULE_ID,
                                      OperationId,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpWmi_Complete,
                                      WmiContext,
                                      Request);
        if (!NT_SUCCESS(Status)) Mem_Free(WmiContext);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_EnumerateWmiNamespaces(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WMI_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpWmi_Send(Connection,
                      ZP_WMI_OPERATION_ENUMERATE_NAMESPACES,
                      Namespace,
                      NamespaceLength,
                      NULL,
                      0,
                      ZP_WMI_MAX_ROWS,
                      0,
                      TimeoutMilliseconds,
                      Callback,
                      Context,
                      Request);
}

NTSTATUS
NTAPI
ZpServer_EnumerateWmiClasses(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WMI_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpWmi_Send(Connection,
                      ZP_WMI_OPERATION_ENUMERATE_CLASSES,
                      Namespace,
                      NamespaceLength,
                      NULL,
                      0,
                      ZP_WMI_MAX_ROWS,
                      0,
                      TimeoutMilliseconds,
                      Callback,
                      Context,
                      Request);
}

NTSTATUS
NTAPI
ZpServer_QueryWmi(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_reads_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_ ULONG Limit,
    _In_ ULONG Flags,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WMI_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    if (Limit > ZP_WMI_MAX_QUERY_ROWS) return STATUS_INVALID_PARAMETER;
    return ZpWmi_Send(Connection,
                      ZP_WMI_OPERATION_QUERY,
                      Namespace,
                      NamespaceLength,
                      Query,
                      QueryLength,
                      Limit,
                      Flags,
                      TimeoutMilliseconds,
                      Callback,
                      Context,
                      Request);
}
