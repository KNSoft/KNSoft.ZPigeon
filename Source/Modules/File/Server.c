#include <KNSoft/MakeLifeEasier/Memory/Core.h>
#include <KNSoft/ZPigeon/Server.h>
#include "../../KNSoft.ZPigeon.Server.SDK/Core/Channel.h"
#include "../../SDK/Channel.h"

typedef union _ZP_SERVER_FILE_CALLBACK
{
    ZP_FILE_QUERY_CALLBACK Query;
    ZP_FILE_VOLUME_CALLBACK Volume;
    ZP_FILE_ENUMERATE_PAGE_CALLBACK Page;
    ZP_FILE_HASH_CALLBACK Hash;
    ZP_STRING_CALLBACK String;
    ZP_REQUEST_STATUS_CALLBACK Status;
} ZP_SERVER_FILE_CALLBACK;

typedef struct _ZP_SERVER_FILE_CONTEXT
{
    ZP_SERVER_FILE_CALLBACK Callback;
    PVOID Context;
} ZP_SERVER_FILE_CONTEXT, *PZP_SERVER_FILE_CONTEXT;

static
NTSTATUS
ZpServerFile_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Complete,
    _In_ PZP_SERVER_FILE_CONTEXT FileContext,
    _Out_ ZP_REQUEST_HANDLE* Request);

static
VOID
NTAPI
ZpServerFile_VolumeComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_CONTEXT FileContext = Context;
    ZP_FILE_VOLUME_INFO_VIEW Info;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpFile_DecodeVolumeInfo(Payload->Buffer, Payload->Length, &Info));
    }
    FileContext->Callback.Volume(Request,
                                 Status,
                                 ZpStatus_IsSuccess(Status) ? &Info : NULL,
                                 FileContext->Context);
    Mem_Free(FileContext);
}

static
VOID
NTAPI
ZpServerFile_StatusComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_CONTEXT FileContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    FileContext->Callback.Status(Request, Status, FileContext->Context);
    Mem_Free(FileContext);
}

static
VOID
NTAPI
ZpServerFile_StringComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_CONTEXT FileContext = Context;
    ZP_STRING_VIEW Value;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpFile_DecodePath(Payload->Buffer, Payload->Length, &Value));
    }
    FileContext->Callback.String(Request,
                                 Status,
                                 ZpStatus_IsSuccess(Status) ? &Value : NULL,
                                 FileContext->Context);
    Mem_Free(FileContext);
}

typedef struct _ZP_SERVER_FILE_OPEN_READ_CONTEXT
{
    PZP_CONNECTION_OBJECT Connection;
    ZP_FILE_OPEN_READ_CALLBACK OpenCallback;
    ZP_CHANNEL_DATA_CALLBACK DataCallback;
    ZP_CHANNEL_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_SERVER_FILE_OPEN_READ_CONTEXT,
  *PZP_SERVER_FILE_OPEN_READ_CONTEXT;

typedef struct _ZP_SERVER_FILE_OPEN_WRITE_CONTEXT
{
    PZP_CONNECTION_OBJECT Connection;
    ZP_FILE_OPEN_WRITE_CALLBACK OpenCallback;
    ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback;
    ZP_CHANNEL_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_SERVER_FILE_OPEN_WRITE_CONTEXT,
  *PZP_SERVER_FILE_OPEN_WRITE_CONTEXT;

static
VOID
NTAPI
ZpServerFile_QueryComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_CONTEXT FileContext = Context;
    ZP_FILE_INFO Info;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpFile_DecodeInfo(Payload->Buffer,
                              Payload->Length,
                              &Info));
    }
    FileContext->Callback.Query(Request,
                                Status,
                                ZpStatus_IsSuccess(Status) ? &Info : NULL,
                                FileContext->Context);
    Mem_Free(FileContext);
}

static
VOID
NTAPI
ZpServerFile_PageComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_CONTEXT FileContext = Context;
    ZP_FILE_PAGE_VIEW Page;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpFile_DecodePage(Payload->Buffer,
                              Payload->Length,
                              &Page));
    }
    FileContext->Callback.Page(Request,
                               Status,
                               ZpStatus_IsSuccess(Status) ? &Page : NULL,
                               FileContext->Context);
    Mem_Free(FileContext);
}

static
VOID
NTAPI
ZpServerFile_HashComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_CONTEXT FileContext = Context;
    ZP_FILE_HASH_VIEW Hash;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpFile_DecodeHashResponse(Payload->Buffer,
                                      Payload->Length,
                                      &Hash));
    }
    FileContext->Callback.Hash(Request,
                               Status,
                               ZpStatus_IsSuccess(Status) ? &Hash : NULL,
                               FileContext->Context);
    Mem_Free(FileContext);
}

static
NTSTATUS
ZpServerFile_EncodePath(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    NTSTATUS Status;

    Status = ZpFile_EncodePath(Path,
                               PathLength,
                               NULL,
                               0,
                               PayloadLength);
    *Payload = NT_SUCCESS(Status) ? Mem_Alloc(*PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && *Payload == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    return NT_SUCCESS(Status) ?
               ZpFile_EncodePath(Path,
                                 PathLength,
                                 *Payload,
                                 *PayloadLength,
                                 PayloadLength) :
               Status;
}

static
NTSTATUS
ZpServerFile_SendStringRequest(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_reads_(ValueLength) PCWCH Value,
    _In_ ULONG ValueLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_FILE_CONTEXT FileContext;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpServerFile_EncodePath(Value,
                                     ValueLength,
                                     &Payload,
                                     &PayloadLength);
    FileContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*FileContext)) : NULL;
    if (NT_SUCCESS(Status) && FileContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        FileContext->Callback.String = Callback;
        FileContext->Context = Context;
        Status = ZpServerFile_Send(Connection,
                                   OperationId,
                                   TimeoutMilliseconds,
                                   Payload,
                                   PayloadLength,
                                   ZpServerFile_StringComplete,
                                   FileContext,
                                   Request);
    }
    Mem_Free(Payload);
    return Status;
}

static
NTSTATUS
ZpServerFile_SendPairStatusRequest(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_reads_(FirstLength) PCWCH First,
    _In_ ULONG FirstLength,
    _In_reads_(SecondLength) PCWCH Second,
    _In_ ULONG SecondLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_FILE_CONTEXT FileContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpFile_EncodeRenameRequest(First,
                                        FirstLength,
                                        Second,
                                        SecondLength,
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
        Status = ZpFile_EncodeRenameRequest(First,
                                            FirstLength,
                                            Second,
                                            SecondLength,
                                            Payload,
                                            PayloadLength,
                                            &PayloadLength);
    }
    FileContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*FileContext)) : NULL;
    if (NT_SUCCESS(Status) && FileContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        FileContext->Callback.Status = Callback;
        FileContext->Context = Context;
        Status = ZpServerFile_Send(Connection,
                                   OperationId,
                                   TimeoutMilliseconds,
                                   Payload,
                                   PayloadLength,
                                   ZpServerFile_StatusComplete,
                                   FileContext,
                                   Request);
    }
    Mem_Free(Payload);
    return Status;
}

static
NTSTATUS
ZpServerFile_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Complete,
    _In_ PZP_SERVER_FILE_CONTEXT FileContext,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    NTSTATUS Status;

    Status = ZpServer_SendRequest(Connection,
                                  ZP_FILE_MODULE_ID,
                                  OperationId,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  Complete,
                                  FileContext,
                                  Request);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(FileContext);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpServer_QueryFile(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_QUERY_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_FILE_CONTEXT FileContext;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpServerFile_EncodePath(Path,
                                     PathLength,
                                     &Payload,
                                     &PayloadLength);
    FileContext = NT_SUCCESS(Status) ?
                      Mem_Alloc(sizeof(*FileContext)) : NULL;
    if (NT_SUCCESS(Status) && FileContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        FileContext->Callback.Query = Callback;
        FileContext->Context = Context;
        Status = ZpServerFile_Send(Connection,
                                   ZP_FILE_OPERATION_QUERY,
                                   TimeoutMilliseconds,
                                   Payload,
                                   PayloadLength,
                                   ZpServerFile_QueryComplete,
                                   FileContext,
                                   Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_QueryFileSecurity(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpServerFile_SendStringRequest(Connection,
                                          ZP_FILE_OPERATION_QUERY_SECURITY,
                                          Path,
                                          PathLength,
                                          TimeoutMilliseconds,
                                          Callback,
                                          Context,
                                          Request);
}

NTSTATUS
NTAPI
ZpServer_SetFileSecurity(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(SddlLength) PCWCH Sddl,
    _In_ ULONG SddlLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpServerFile_SendPairStatusRequest(Connection,
                                              ZP_FILE_OPERATION_SET_SECURITY,
                                              Path,
                                              PathLength,
                                              Sddl,
                                              SddlLength,
                                              TimeoutMilliseconds,
                                              Callback,
                                              Context,
                                              Request);
}

NTSTATUS
NTAPI
ZpServer_ResolveAccountName(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpServerFile_SendStringRequest(Connection,
                                          ZP_FILE_OPERATION_RESOLVE_ACCOUNT,
                                          Name,
                                          NameLength,
                                          TimeoutMilliseconds,
                                          Callback,
                                          Context,
                                          Request);
}

NTSTATUS
NTAPI
ZpServer_ResolveAccountSid(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(SidLength) PCWCH Sid,
    _In_ ULONG SidLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpServerFile_SendStringRequest(Connection,
                                          ZP_FILE_OPERATION_RESOLVE_SID,
                                          Sid,
                                          SidLength,
                                          TimeoutMilliseconds,
                                          Callback,
                                          Context,
                                          Request);
}

NTSTATUS
NTAPI
ZpServer_QueryFileVolume(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_VOLUME_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_FILE_CONTEXT FileContext;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpServerFile_EncodePath(Path, PathLength, &Payload, &PayloadLength);
    FileContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*FileContext)) : NULL;
    if (NT_SUCCESS(Status) && FileContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        FileContext->Callback.Volume = Callback;
        FileContext->Context = Context;
        Status = ZpServerFile_Send(Connection,
                                   ZP_FILE_OPERATION_QUERY_VOLUME,
                                   TimeoutMilliseconds,
                                   Payload,
                                   PayloadLength,
                                   ZpServerFile_VolumeComplete,
                                   FileContext,
                                   Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_SetFileVolumeLabel(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(LabelLength) PCWCH Label,
    _In_ ULONG LabelLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_FILE_CONTEXT FileContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpFile_EncodeRenameRequest(Path, PathLength, Label, LabelLength, NULL, 0, &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeRenameRequest(Path,
                                            PathLength,
                                            Label,
                                            LabelLength,
                                            Payload,
                                            PayloadLength,
                                            &PayloadLength);
    }
    FileContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*FileContext)) : NULL;
    if (NT_SUCCESS(Status) && FileContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        FileContext->Callback.Status = Callback;
        FileContext->Context = Context;
        Status = ZpServerFile_Send(Connection,
                                   ZP_FILE_OPERATION_SET_VOLUME_LABEL,
                                   TimeoutMilliseconds,
                                   Payload,
                                   PayloadLength,
                                   ZpServerFile_StatusComplete,
                                   FileContext,
                                   Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_EnumerateFilesPage(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG EnumerationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_ENUMERATE_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_FILE_CONTEXT FileContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpFile_EncodeEnumeratePageRequest(Path,
                                               PathLength,
                                               EnumerationId,
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
        Status = ZpFile_EncodeEnumeratePageRequest(Path,
                                                   PathLength,
                                                   EnumerationId,
                                                   Payload,
                                                   PayloadLength,
                                                   &PayloadLength);
    }
    FileContext = NT_SUCCESS(Status) ?
                      Mem_Alloc(sizeof(*FileContext)) : NULL;
    if (NT_SUCCESS(Status) && FileContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        FileContext->Callback.Page = Callback;
        FileContext->Context = Context;
        Status = ZpServerFile_Send(Connection,
                                   ZP_FILE_OPERATION_ENUMERATE_PAGE,
                                   TimeoutMilliseconds,
                                   Payload,
                                   PayloadLength,
                                   ZpServerFile_PageComplete,
                                   FileContext,
                                   Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_HashFile(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_FILE_HASH_ALGORITHM Algorithm,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_HASH_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_FILE_CONTEXT FileContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpFile_EncodeHashRequest(Algorithm,
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
        Status = ZpFile_EncodeHashRequest(Algorithm,
                                          Path,
                                          PathLength,
                                          Payload,
                                          PayloadLength,
                                          &PayloadLength);
    }
    FileContext = NT_SUCCESS(Status) ?
                      Mem_Alloc(sizeof(*FileContext)) : NULL;
    if (NT_SUCCESS(Status) && FileContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        FileContext->Callback.Hash = Callback;
        FileContext->Context = Context;
        Status = ZpServerFile_Send(Connection,
                                   ZP_FILE_OPERATION_HASH,
                                   TimeoutMilliseconds,
                                   Payload,
                                   PayloadLength,
                                   ZpServerFile_HashComplete,
                                   FileContext,
                                   Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_DeleteFile(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_FILE_CONTEXT FileContext;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpServerFile_EncodePath(Path, PathLength, &Payload, &PayloadLength);
    FileContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*FileContext)) : NULL;
    if (NT_SUCCESS(Status) && FileContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        FileContext->Callback.Status = Callback;
        FileContext->Context = Context;
        Status = ZpServerFile_Send(Connection,
                                   ZP_FILE_OPERATION_DELETE,
                                   TimeoutMilliseconds,
                                   Payload,
                                   PayloadLength,
                                   ZpServerFile_StatusComplete,
                                   FileContext,
                                   Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_RenameFile(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(NewPathLength) PCWCH NewPath,
    _In_ ULONG NewPathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_FILE_CONTEXT FileContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpFile_EncodeRenameRequest(Path, PathLength, NewPath, NewPathLength, NULL, 0, &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeRenameRequest(Path,
                                            PathLength,
                                            NewPath,
                                            NewPathLength,
                                            Payload,
                                            PayloadLength,
                                            &PayloadLength);
    }
    FileContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*FileContext)) : NULL;
    if (NT_SUCCESS(Status) && FileContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        FileContext->Callback.Status = Callback;
        FileContext->Context = Context;
        Status = ZpServerFile_Send(Connection,
                                   ZP_FILE_OPERATION_RENAME,
                                   TimeoutMilliseconds,
                                   Payload,
                                   PayloadLength,
                                   ZpServerFile_StatusComplete,
                                   FileContext,
                                   Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_SetFileAttributes(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG Attributes,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_FILE_CONTEXT FileContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpFile_EncodeSetAttributesRequest(Path,
                                               PathLength,
                                               Attributes,
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
        Status = ZpFile_EncodeSetAttributesRequest(Path,
                                                   PathLength,
                                                   Attributes,
                                                   Payload,
                                                   PayloadLength,
                                                   &PayloadLength);
    }
    FileContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*FileContext)) : NULL;
    if (NT_SUCCESS(Status) && FileContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        FileContext->Callback.Status = Callback;
        FileContext->Context = Context;
        Status = ZpServerFile_Send(Connection,
                                   ZP_FILE_OPERATION_SET_ATTRIBUTES,
                                   TimeoutMilliseconds,
                                   Payload,
                                   PayloadLength,
                                   ZpServerFile_StatusComplete,
                                   FileContext,
                                   Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_WriteFileRange(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_FILE_CONTEXT FileContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpFile_EncodeWriteRangeRequest(Path,
                                            PathLength,
                                            Offset,
                                            Data,
                                            DataLength,
                                            NULL,
                                            0,
                                            &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeWriteRangeRequest(Path,
                                                PathLength,
                                                Offset,
                                                Data,
                                                DataLength,
                                                Payload,
                                                PayloadLength,
                                                &PayloadLength);
    }
    FileContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*FileContext)) : NULL;
    if (NT_SUCCESS(Status) && FileContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        FileContext->Callback.Status = Callback;
        FileContext->Context = Context;
        Status = ZpServerFile_Send(Connection,
                                   ZP_FILE_OPERATION_WRITE_RANGE,
                                   TimeoutMilliseconds,
                                   Payload,
                                   PayloadLength,
                                   ZpServerFile_StatusComplete,
                                   FileContext,
                                   Request);
    }
    Mem_Free(Payload);
    return Status;
}

static
VOID
NTAPI
ZpServerFile_OpenReadComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_OPEN_READ_CONTEXT FileContext = Context;
    PZP_SERVER_CHANNEL_OBJECT Channel = NULL;
    ULONG ChannelId = 0;
    ULONGLONG FileSize = 0, Offset = 0, Remaining = 0;
    NTSTATUS ChannelStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpFile_DecodeOpenReadResponse(Payload->Buffer,
                                          Payload->Length,
                                          &ChannelId,
                                          &FileSize,
                                          &Offset));
    }
    if (ZpStatus_IsSuccess(Status) && Offset > FileSize)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    if (ZpStatus_IsSuccess(Status))
    {
        Remaining = FileSize - Offset;
        Status = ZpStatus_FromNtStatus(
            ZpServerChannel_Create(
                FileContext->Connection,
                ChannelId,
                ZP_FILE_MODULE_ID,
                TRUE,
                Remaining,
                FALSE,
                0,
                FileContext->DataCallback,
                NULL,
                FileContext->CloseCallback,
                FileContext->Context,
                TRUE,
                &Channel));
    }
    else
    {
        ZpServerChannel_ReleaseReservation(FileContext->Connection);
    }
    if (!ZpStatus_IsSuccess(Status) && ChannelId != 0)
    {
        ZpServerConnection_RejectChannel(FileContext->Connection,
                                         ChannelId,
                                         Status);
    }
    FileContext->OpenCallback(
        Request,
        Status,
        ZpStatus_IsSuccess(Status) ? (ZP_CHANNEL_HANDLE)Channel : NULL,
        ZpStatus_IsSuccess(Status) ? FileSize : 0,
        ZpStatus_IsSuccess(Status) ? Offset : 0,
        FileContext->Context);
    if (Channel != NULL)
    {
        if (Remaining != 0)
        {
            ChannelStatus = ZpServerChannel_SendWindow(
                Channel,
                (ULONG)min(Remaining,
                           ZP_SERVER_DEFAULT_CHANNEL_WINDOW_SIZE));
            if (!NT_SUCCESS(ChannelStatus))
            {
                ZpServerChannel_Abort(Channel,
                                      ZpStatus_FromNtStatus(ChannelStatus));
            }
        }
        ZpChannel_Close((ZP_CHANNEL_HANDLE)Channel);
    }
    Mem_Free(FileContext);
}

NTSTATUS
NTAPI
ZpServer_OpenFileRead(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG Offset,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_OPEN_READ_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_FILE_OPEN_READ_CONTEXT FileContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;
    LOGICAL Reserved = FALSE;

    if (OpenCallback == NULL || DataCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpFile_EncodeOpenReadRequest(Path,
                                          PathLength,
                                          Offset,
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
        Status = ZpFile_EncodeOpenReadRequest(Path,
                                              PathLength,
                                              Offset,
                                              Payload,
                                              PayloadLength,
                                              &PayloadLength);
    }
    FileContext = NT_SUCCESS(Status) ?
                      Mem_Alloc(sizeof(*FileContext)) : NULL;
    if (NT_SUCCESS(Status) && FileContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerChannel_Reserve(Connection);
        Reserved = NT_SUCCESS(Status);
    }
    if (NT_SUCCESS(Status))
    {
        FileContext->Connection = Connection;
        FileContext->OpenCallback = OpenCallback;
        FileContext->DataCallback = DataCallback;
        FileContext->CloseCallback = CloseCallback;
        FileContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_FILE_MODULE_ID,
                                      ZP_FILE_OPERATION_OPEN_READ,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpServerFile_OpenReadComplete,
                                      FileContext,
                                      Request);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(FileContext);
        }
        else
        {
            Reserved = FALSE;
        }
    }
    if (Reserved)
    {
        ZpServerChannel_ReleaseReservation(Connection);
    }
    Mem_Free(Payload);
    return Status;
}

static
VOID
NTAPI
ZpServerFile_OpenWriteComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_OPEN_WRITE_CONTEXT FileContext = Context;
    PZP_SERVER_CHANNEL_OBJECT Channel = NULL;
    ULONG ChannelId = 0;
    ULONGLONG FileSize = 0;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpFile_DecodeOpenWriteResponse(Payload->Buffer,
                                           Payload->Length,
                                           &ChannelId,
                                           &FileSize));
    }
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpServerChannel_Create(
                FileContext->Connection,
                ChannelId,
                ZP_FILE_MODULE_ID,
                FALSE,
                0,
                TRUE,
                FileSize,
                NULL,
                FileContext->WritableCallback,
                FileContext->CloseCallback,
                FileContext->Context,
                TRUE,
                &Channel));
    }
    else
    {
        ZpServerChannel_ReleaseReservation(FileContext->Connection);
    }
    if (!ZpStatus_IsSuccess(Status) && ChannelId != 0)
    {
        ZpServerConnection_RejectChannel(FileContext->Connection,
                                         ChannelId,
                                         Status);
    }
    FileContext->OpenCallback(
        Request,
        Status,
        ZpStatus_IsSuccess(Status) ? (ZP_CHANNEL_HANDLE)Channel : NULL,
        ZpStatus_IsSuccess(Status) ? FileSize : 0,
        FileContext->Context);
    if (Channel != NULL)
    {
        ZpChannel_Close((ZP_CHANNEL_HANDLE)Channel);
    }
    Mem_Free(FileContext);
}

NTSTATUS
NTAPI
ZpServer_OpenFileWrite(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG FileSize,
    _In_ ZP_FILE_CREATE_DISPOSITION Disposition,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_OPEN_WRITE_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_FILE_OPEN_WRITE_CONTEXT FileContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;
    LOGICAL Reserved = FALSE;

    if (OpenCallback == NULL || WritableCallback == NULL ||
        CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpFile_EncodeOpenWriteRequest(Path,
                                           PathLength,
                                           FileSize,
                                           Disposition,
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
        Status = ZpFile_EncodeOpenWriteRequest(Path,
                                               PathLength,
                                               FileSize,
                                               Disposition,
                                               Payload,
                                               PayloadLength,
                                               &PayloadLength);
    }
    FileContext = NT_SUCCESS(Status) ?
                      Mem_Alloc(sizeof(*FileContext)) : NULL;
    if (NT_SUCCESS(Status) && FileContext == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerChannel_Reserve(Connection);
        Reserved = NT_SUCCESS(Status);
    }
    if (NT_SUCCESS(Status))
    {
        FileContext->Connection = Connection;
        FileContext->OpenCallback = OpenCallback;
        FileContext->WritableCallback = WritableCallback;
        FileContext->CloseCallback = CloseCallback;
        FileContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_FILE_MODULE_ID,
                                      ZP_FILE_OPERATION_OPEN_WRITE,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpServerFile_OpenWriteComplete,
                                      FileContext,
                                      Request);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(FileContext);
        }
        else
        {
            Reserved = FALSE;
        }
    }
    if (Reserved)
    {
        ZpServerChannel_ReleaseReservation(Connection);
    }
    Mem_Free(Payload);
    return Status;
}
