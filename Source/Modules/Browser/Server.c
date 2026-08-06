#include <KNSoft/ZPigeon/Server.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef struct _ZP_BROWSER_CONTEXT
{
    union
    {
        ZP_BROWSER_PAGE_CALLBACK Page;
        ZP_BROWSER_PROFILE_INSPECTION_CALLBACK Inspection;
        ZP_BROWSER_DOCUMENT_CALLBACK Document;
        ZP_REQUEST_STATUS_CALLBACK Status;
    } Callback;
    PVOID Context;
} ZP_BROWSER_CONTEXT, *PZP_BROWSER_CONTEXT;

static
VOID
NTAPI
ZpBrowser_InspectionComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_BROWSER_CONTEXT BrowserContext = Context;
    ZP_BROWSER_PROFILE_INSPECTION Inspection;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpBrowser_DecodeProfileInspection(Payload->Buffer, Payload->Length, &Inspection));
    }
    BrowserContext->Callback.Inspection(Request,
                                        Status,
                                        ZpStatus_IsSuccess(Status) ? &Inspection : NULL,
                                        BrowserContext->Context);
    Mem_Free(BrowserContext);
}

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
    BrowserContext->Callback.Page(Request,
                                  Status,
                                  ZpStatus_IsSuccess(Status) ? &Page : NULL,
                                  BrowserContext->Context);
    Mem_Free(BrowserContext);
}

static
NTSTATUS
ZpBrowser_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
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
    BrowserContext->Callback.Page = Callback;
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

static
VOID
NTAPI
ZpBrowser_DocumentComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_BROWSER_CONTEXT BrowserContext = Context;
    ZP_BROWSER_DOCUMENT_PAGE_VIEW Page;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpBrowser_DecodeDocumentPage(Payload->Buffer, Payload->Length, &Page));
    }
    BrowserContext->Callback.Document(Request,
                                      Status,
                                      ZpStatus_IsSuccess(Status) ? &Page : NULL,
                                      BrowserContext->Context);
    Mem_Free(BrowserContext);
}

static
VOID
NTAPI
ZpBrowser_StatusComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_BROWSER_CONTEXT BrowserContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    BrowserContext->Callback.Status(Request, Status, BrowserContext->Context);
    Mem_Free(BrowserContext);
}

static
NTSTATUS
ZpBrowser_SendDocument(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_BROWSER_DOCUMENT_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_BROWSER_CONTEXT BrowserContext;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    BrowserContext = Mem_Alloc(sizeof(*BrowserContext));
    if (BrowserContext == NULL) return STATUS_NO_MEMORY;
    BrowserContext->Callback.Document = Callback;
    BrowserContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_BROWSER_MODULE_ID,
                                  OperationId,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  ZpBrowser_DocumentComplete,
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

NTSTATUS
NTAPI
ZpServer_InspectBrowserProfile(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_BROWSER_TYPE Browser,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_BROWSER_PROFILE_INSPECTION_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_BROWSER_CONTEXT BrowserContext;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpBrowser_EncodeProfileInspectionRequest(Browser,
                                                       Profile,
                                                       ProfileLength,
                                                       NULL,
                                                       0,
                                                       &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpBrowser_EncodeProfileInspectionRequest(Browser,
                                                           Profile,
                                                           ProfileLength,
                                                           Payload,
                                                           PayloadLength,
                                                           &PayloadLength);
    }
    BrowserContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*BrowserContext)) : NULL;
    if (NT_SUCCESS(Status) && BrowserContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        BrowserContext->Callback.Inspection = Callback;
        BrowserContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_BROWSER_MODULE_ID,
                                      ZP_BROWSER_OPERATION_INSPECT_PROFILE,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpBrowser_InspectionComplete,
                                      BrowserContext,
                                      Request);
    }
    if (!NT_SUCCESS(Status)) Mem_Free(BrowserContext);
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_OpenBrowserDocument(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ ZP_BROWSER_KIND Kind,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_BROWSER_DOCUMENT_CALLBACK Callback,
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
                                   0,
                                   1,
                                   NULL,
                                   0,
                                   &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (!NT_SUCCESS(Status) || Payload == NULL) return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    Status = ZpBrowser_EncodeQuery(Browser,
                                   Kind,
                                   Profile,
                                   ProfileLength,
                                   0,
                                   1,
                                   Payload,
                                   PayloadLength,
                                   &PayloadLength);
    if (NT_SUCCESS(Status))
    {
        Status = ZpBrowser_SendDocument(Connection,
                                       ZP_BROWSER_OPERATION_OPEN_DOCUMENT,
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

NTSTATUS
NTAPI
ZpServer_QueryBrowserDocumentNode(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG SnapshotId,
    _In_ ULONG NodeId,
    _In_ ULONG Cursor,
    _In_ ULONG Limit,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_BROWSER_DOCUMENT_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    BYTE Payload[4 * sizeof(ULONG)];
    ULONG PayloadLength;
    NTSTATUS Status;

    Status = ZpBrowser_EncodeDocumentQuery(SnapshotId,
                                           NodeId,
                                           Cursor,
                                           Limit,
                                           Payload,
                                           sizeof(Payload),
                                           &PayloadLength);
    return NT_SUCCESS(Status) ?
               ZpBrowser_SendDocument(Connection,
                                      ZP_BROWSER_OPERATION_QUERY_DOCUMENT_NODE,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      Callback,
                                      Context,
                                      Request) :
               Status;
}

NTSTATUS
NTAPI
ZpServer_CloseBrowserDocument(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG SnapshotId,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_BROWSER_CONTEXT BrowserContext;
    BYTE Payload[sizeof(ULONG)];
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpBrowser_EncodeDocumentClose(SnapshotId,
                                           Payload,
                                           sizeof(Payload),
                                           &PayloadLength);
    if (!NT_SUCCESS(Status)) return Status;
    BrowserContext = Mem_Alloc(sizeof(*BrowserContext));
    if (BrowserContext == NULL) return STATUS_NO_MEMORY;
    BrowserContext->Callback.Status = Callback;
    BrowserContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_BROWSER_MODULE_ID,
                                  ZP_BROWSER_OPERATION_CLOSE_DOCUMENT,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  ZpBrowser_StatusComplete,
                                  BrowserContext,
                                  Request);
    if (!NT_SUCCESS(Status)) Mem_Free(BrowserContext);
    return Status;
}
