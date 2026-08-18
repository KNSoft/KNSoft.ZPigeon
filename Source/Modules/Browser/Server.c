#include <KNSoft/ZPigeon/Server.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef struct _ZP_BROWSER_CONTEXT
{
    ZP_BROWSER_PAGE_CALLBACK Callback;
    PVOID Context;
} ZP_BROWSER_CONTEXT, *PZP_BROWSER_CONTEXT;

static
VOID
NTAPI
ZpBrowser_Complete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_BROWSER_CONTEXT BrowserContext = Context;
    ZP_BROWSER_PAGE_VIEW Page;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpBrowser_DecodePage(Payload->Buffer, Payload->Length, &Page));
    }
    BrowserContext->Callback(Request,
                             Status,
                             ZpStatus_IsSuccess(Status) ? &Page : NULL,
                             BrowserContext->Context);
    Mem_Free(BrowserContext);
}

static
NTSTATUS
ZpBrowser_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ USHORT OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_BROWSER_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_BROWSER_CONTEXT BrowserContext;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    BrowserContext = Mem_Alloc(sizeof(*BrowserContext));
    if (BrowserContext == NULL) return STATUS_NO_MEMORY;
    BrowserContext->Callback = Callback;
    BrowserContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_BROWSER_MODULE_ID,
                                  OperationId,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  ZpBrowser_Complete,
                                  BrowserContext,
                                  Request);
    if (!NT_SUCCESS(Status)) Mem_Free(BrowserContext);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_EnumerateBrowsers(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_BROWSER_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpBrowser_Send(Connection,
                          ZP_BROWSER_OPERATION_ENUMERATE,
                          TimeoutMilliseconds,
                          NULL,
                          0,
                          Callback,
                          Context,
                          Request);
}

NTSTATUS
NTAPI
ZpServer_QueryBrowser(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ ZP_BROWSER_KIND Kind,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_ ULONGLONG Cursor,
    _In_ ULONG Limit,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_BROWSER_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    Status = ZpBrowser_EncodeQuery(Browser,
                                   Kind,
                                   Profile,
                                   ProfileLength,
                                   Cursor,
                                   Limit,
                                   NULL,
                                   0,
                                   &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (!NT_SUCCESS(Status) || Payload == NULL)
    {
        return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    }
    Status = ZpBrowser_EncodeQuery(Browser,
                                   Kind,
                                   Profile,
                                   ProfileLength,
                                   Cursor,
                                   Limit,
                                   Payload,
                                   PayloadLength,
                                   &PayloadLength);
    if (NT_SUCCESS(Status))
    {
        Status = ZpBrowser_Send(Connection,
                               ZP_BROWSER_OPERATION_QUERY,
                               TimeoutMilliseconds,
                               Payload,
                               PayloadLength,
                               Callback,
                               Context,
                               Request);
    }
    Mem_Free(Payload);
    return Status;
}
