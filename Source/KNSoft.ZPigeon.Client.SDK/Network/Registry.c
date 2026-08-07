#include "../Client.inl"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef struct _ZP_CLIENT_REGISTRY_PAGE_CONTEXT
{
    ZP_REGISTRY_PAGE_CALLBACK Callback;
    PVOID Context;
    LOGICAL Values;
} ZP_CLIENT_REGISTRY_PAGE_CONTEXT, *PZP_CLIENT_REGISTRY_PAGE_CONTEXT;

typedef struct _ZP_CLIENT_REGISTRY_VALUE_CONTEXT
{
    ZP_REGISTRY_VALUE_CALLBACK Callback;
    PVOID Context;
} ZP_CLIENT_REGISTRY_VALUE_CONTEXT, *PZP_CLIENT_REGISTRY_VALUE_CONTEXT;

typedef struct _ZP_CLIENT_REGISTRY_STATUS_CONTEXT
{
    ZP_REQUEST_STATUS_CALLBACK Callback;
    PVOID Context;
} ZP_CLIENT_REGISTRY_STATUS_CONTEXT, *PZP_CLIENT_REGISTRY_STATUS_CONTEXT;

static
VOID
NTAPI
ZpClient_RegistryPageComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_REGISTRY_PAGE_CONTEXT RegistryContext = Context;
    ZP_REGISTRY_PAGE_VIEW Page;

    if (NT_SUCCESS(Status))
    {
        Status = RegistryContext->Values ?
                     ZpRegistry_DecodeValuePage(Payload->Buffer,
                                                Payload->Length,
                                                &Page) :
                     ZpRegistry_DecodeKeyPage(Payload->Buffer,
                                              Payload->Length,
                                              &Page);
    }
    RegistryContext->Callback(Request,
                              Status,
                              NT_SUCCESS(Status) ? &Page : NULL,
                              RegistryContext->Context);
    Mem_Free(RegistryContext);
}

static
NTSTATUS
ZpClient_EnumerateRegistryPage(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ USHORT OperationId,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ZP_REGISTRY_VIEW View,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _In_ ULONG MaxEntries,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REGISTRY_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_CLIENT_REGISTRY_PAGE_CONTEXT RegistryContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpRegistry_EncodeEnumerateRequest(Root,
                                               View,
                                               MaxEntries,
                                               Cursor != NULL,
                                               Path,
                                               PathLength,
                                               Cursor,
                                               CursorLength,
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
        Status = ZpRegistry_EncodeEnumerateRequest(Root,
                                                   View,
                                                   MaxEntries,
                                                   Cursor != NULL,
                                                   Path,
                                                   PathLength,
                                                   Cursor,
                                                   CursorLength,
                                                   Payload,
                                                   PayloadLength,
                                                   &PayloadLength);
    }
    RegistryContext = NT_SUCCESS(Status) ?
                          Mem_Alloc(sizeof(*RegistryContext)) :
                          NULL;
    if (NT_SUCCESS(Status) && RegistryContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        RegistryContext->Callback = Callback;
        RegistryContext->Context = Context;
        RegistryContext->Values =
            OperationId == ZP_REGISTRY_OPERATION_ENUMERATE_VALUES_PAGE;
        Status = ZpClient_SendRequest(Client,
                                      ZP_REGISTRY_MODULE_ID,
                                      OperationId,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpClient_RegistryPageComplete,
                                      RegistryContext,
                                      Request);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(RegistryContext);
        }
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpClient_EnumerateRegistryKeysPage(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ZP_REGISTRY_VIEW View,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _In_ ULONG MaxEntries,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REGISTRY_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpClient_EnumerateRegistryPage(
               Client,
               ZP_REGISTRY_OPERATION_ENUMERATE_KEYS_PAGE,
               Root,
               View,
               Path,
               PathLength,
               Cursor,
               CursorLength,
               MaxEntries,
               TimeoutMilliseconds,
               Callback,
               Context,
               Request);
}

NTSTATUS
NTAPI
ZpClient_EnumerateRegistryValuesPage(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ZP_REGISTRY_VIEW View,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _In_ ULONG MaxEntries,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REGISTRY_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpClient_EnumerateRegistryPage(
               Client,
               ZP_REGISTRY_OPERATION_ENUMERATE_VALUES_PAGE,
               Root,
               View,
               Path,
               PathLength,
               Cursor,
               CursorLength,
               MaxEntries,
               TimeoutMilliseconds,
               Callback,
               Context,
               Request);
}

static
VOID
NTAPI
ZpClient_RegistryValueComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_REGISTRY_VALUE_CONTEXT RegistryContext = Context;
    ZP_REGISTRY_VALUE_VIEW Value;

    if (NT_SUCCESS(Status))
    {
        Status = ZpRegistry_DecodeValue(Payload->Buffer,
                                        Payload->Length,
                                        &Value);
    }
    RegistryContext->Callback(Request,
                              Status,
                              NT_SUCCESS(Status) ? &Value : NULL,
                              RegistryContext->Context);
    Mem_Free(RegistryContext);
}

NTSTATUS
NTAPI
ZpClient_QueryRegistryValue(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ZP_REGISTRY_VIEW View,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REGISTRY_VALUE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_CLIENT_REGISTRY_VALUE_CONTEXT RegistryContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpRegistry_EncodeValueRequest(Root,
                                           View,
                                           Path,
                                           PathLength,
                                           ValueName,
                                           ValueNameLength,
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
        Status = ZpRegistry_EncodeValueRequest(Root,
                                               View,
                                               Path,
                                               PathLength,
                                               ValueName,
                                               ValueNameLength,
                                               Payload,
                                               PayloadLength,
                                               &PayloadLength);
    }
    RegistryContext = NT_SUCCESS(Status) ?
                          Mem_Alloc(sizeof(*RegistryContext)) :
                          NULL;
    if (NT_SUCCESS(Status) && RegistryContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        RegistryContext->Callback = Callback;
        RegistryContext->Context = Context;
        Status = ZpClient_SendRequest(Client,
                                      ZP_REGISTRY_MODULE_ID,
                                      ZP_REGISTRY_OPERATION_QUERY_VALUE,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpClient_RegistryValueComplete,
                                      RegistryContext,
                                      Request);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(RegistryContext);
        }
    }
    Mem_Free(Payload);
    return Status;
}

static
VOID
NTAPI
ZpClient_RegistryStatusComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_REGISTRY_STATUS_CONTEXT RegistryContext = Context;

    if (NT_SUCCESS(Status) && Payload->Length != 0)
    {
        Status = STATUS_DATA_ERROR;
    }
    RegistryContext->Callback(Request, Status, RegistryContext->Context);
    Mem_Free(RegistryContext);
}

static
NTSTATUS
ZpClient_SendRegistryStatusRequest(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ USHORT OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_CLIENT_REGISTRY_STATUS_CONTEXT RegistryContext;
    NTSTATUS Status;

    RegistryContext = Mem_Alloc(sizeof(*RegistryContext));
    if (RegistryContext == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RegistryContext->Callback = Callback;
    RegistryContext->Context = Context;
    Status = ZpClient_SendRequest(Client,
                                  ZP_REGISTRY_MODULE_ID,
                                  OperationId,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  ZpClient_RegistryStatusComplete,
                                  RegistryContext,
                                  Request);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(RegistryContext);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpClient_SetRegistryValue(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ZP_REGISTRY_VIEW View,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpRegistry_EncodeSetValueRequest(Root,
                                              View,
                                              Type,
                                              Path,
                                              PathLength,
                                              ValueName,
                                              ValueNameLength,
                                              Data,
                                              DataLength,
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
        Status = ZpRegistry_EncodeSetValueRequest(Root,
                                                  View,
                                                  Type,
                                                  Path,
                                                  PathLength,
                                                  ValueName,
                                                  ValueNameLength,
                                                  Data,
                                                  DataLength,
                                                  Payload,
                                                  PayloadLength,
                                                  &PayloadLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpClient_SendRegistryStatusRequest(
                     Client,
                     ZP_REGISTRY_OPERATION_SET_VALUE,
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

static
NTSTATUS
ZpClient_SendRegistryValueStatusRequest(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ USHORT OperationId,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ZP_REGISTRY_VIEW View,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpRegistry_EncodeValueRequest(Root,
                                           View,
                                           Path,
                                           PathLength,
                                           ValueName,
                                           ValueNameLength,
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
        Status = ZpRegistry_EncodeValueRequest(Root,
                                               View,
                                               Path,
                                               PathLength,
                                               ValueName,
                                               ValueNameLength,
                                               Payload,
                                               PayloadLength,
                                               &PayloadLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpClient_SendRegistryStatusRequest(Client,
                                                     OperationId,
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
ZpClient_DeleteRegistryValue(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ZP_REGISTRY_VIEW View,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpClient_SendRegistryValueStatusRequest(
               Client,
               ZP_REGISTRY_OPERATION_DELETE_VALUE,
               Root,
               View,
               Path,
               PathLength,
               ValueName,
               ValueNameLength,
               TimeoutMilliseconds,
               Callback,
               Context,
               Request);
}

static
NTSTATUS
ZpClient_SendRegistryKeyStatusRequest(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ USHORT OperationId,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ZP_REGISTRY_VIEW View,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpRegistry_EncodeKeyRequest(Root,
                                         View,
                                         Path,
                                         PathLength,
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
        Status = ZpRegistry_EncodeKeyRequest(Root,
                                             View,
                                             Path,
                                             PathLength,
                                             Payload,
                                             PayloadLength,
                                             &PayloadLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpClient_SendRegistryStatusRequest(Client,
                                                     OperationId,
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
ZpClient_CreateRegistryKey(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ZP_REGISTRY_VIEW View,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpClient_SendRegistryKeyStatusRequest(
               Client,
               ZP_REGISTRY_OPERATION_CREATE_KEY,
               Root,
               View,
               Path,
               PathLength,
               TimeoutMilliseconds,
               Callback,
               Context,
               Request);
}

NTSTATUS
NTAPI
ZpClient_DeleteRegistryKey(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_ ZP_REGISTRY_VIEW View,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpClient_SendRegistryKeyStatusRequest(
               Client,
               ZP_REGISTRY_OPERATION_DELETE_KEY,
               Root,
               View,
               Path,
               PathLength,
               TimeoutMilliseconds,
               Callback,
               Context,
               Request);
}
