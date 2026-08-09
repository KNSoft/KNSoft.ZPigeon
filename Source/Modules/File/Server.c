#include <KNSoft/MakeLifeEasier/Memory/Core.h>
#include <KNSoft/ZPigeon/Server.h>
#include "../../KNSoft.ZPigeon.Server.SDK/Core/Channel.h"
#include "../../SDK/Channel.h"

typedef union _ZP_SERVER_FILE_CALLBACK
{
    ZP_FILE_QUERY_CALLBACK Query;
    ZP_FILE_ENUMERATE_CALLBACK Enumerate;
    ZP_FILE_ENUMERATE_PAGE_CALLBACK Page;
    ZP_FILE_HASH_CALLBACK Hash;
} ZP_SERVER_FILE_CALLBACK;

typedef struct _ZP_SERVER_FILE_CONTEXT
{
    ZP_SERVER_FILE_CALLBACK Callback;
    PVOID Context;
} ZP_SERVER_FILE_CONTEXT, *PZP_SERVER_FILE_CONTEXT;

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
    _In_ NTSTATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_CONTEXT FileContext = Context;
    ZP_FILE_INFO Info;

    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_DecodeInfo(Payload->Buffer,
                                   Payload->Length,
                                   &Info);
    }
    FileContext->Callback.Query(Request,
                                Status,
                                NT_SUCCESS(Status) ? &Info : NULL,
                                FileContext->Context);
    Mem_Free(FileContext);
}

static
VOID
NTAPI
ZpServerFile_EnumerateComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_CONTEXT FileContext = Context;
    ZP_FILE_LIST_VIEW Files;

    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_DecodeList(Payload->Buffer,
                                   Payload->Length,
                                   &Files);
    }
    FileContext->Callback.Enumerate(Request,
                                    Status,
                                    NT_SUCCESS(Status) ? &Files : NULL,
                                    FileContext->Context);
    Mem_Free(FileContext);
}

static
VOID
NTAPI
ZpServerFile_PageComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_CONTEXT FileContext = Context;
    ZP_FILE_PAGE_VIEW Page;

    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_DecodePage(Payload->Buffer,
                                   Payload->Length,
                                   &Page);
    }
    FileContext->Callback.Page(Request,
                               Status,
                               NT_SUCCESS(Status) ? &Page : NULL,
                               FileContext->Context);
    Mem_Free(FileContext);
}

static
VOID
NTAPI
ZpServerFile_HashComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_CONTEXT FileContext = Context;
    ZP_FILE_HASH_VIEW Hash;

    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_DecodeHashResponse(Payload->Buffer,
                                           Payload->Length,
                                           &Hash);
    }
    FileContext->Callback.Hash(Request,
                               Status,
                               NT_SUCCESS(Status) ? &Hash : NULL,
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
ZpServerFile_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ USHORT OperationId,
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
ZpServer_EnumerateFiles(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_ENUMERATE_CALLBACK Callback,
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
        FileContext->Callback.Enumerate = Callback;
        FileContext->Context = Context;
        Status = ZpServerFile_Send(Connection,
                                   ZP_FILE_OPERATION_ENUMERATE,
                                   TimeoutMilliseconds,
                                   Payload,
                                   PayloadLength,
                                   ZpServerFile_EnumerateComplete,
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
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _In_ ULONG MaxEntries,
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
                                               Cursor,
                                               CursorLength,
                                               MaxEntries,
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
                                                   Cursor,
                                                   CursorLength,
                                                   MaxEntries,
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

static
VOID
NTAPI
ZpServerFile_OpenReadComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ NTSTATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_OPEN_READ_CONTEXT FileContext = Context;
    PZP_SERVER_CHANNEL_OBJECT Channel = NULL;
    ULONGLONG ChannelId = 0, FileSize = 0, Offset = 0, Remaining = 0;

    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_DecodeOpenReadResponse(Payload->Buffer,
                                               Payload->Length,
                                               &ChannelId,
                                               &FileSize,
                                               &Offset);
    }
    if (NT_SUCCESS(Status) && Offset > FileSize)
    {
        Status = STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status))
    {
        Remaining = FileSize - Offset;
        Status = ZpServerChannel_Create(
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
            &Channel);
    }
    else
    {
        ZpServerChannel_ReleaseReservation(FileContext->Connection);
    }
    if (!NT_SUCCESS(Status) && ChannelId != 0)
    {
        ZpServerConnection_RejectChannel(FileContext->Connection,
                                         ChannelId,
                                         Status);
    }
    FileContext->OpenCallback(
        Request,
        Status,
        NT_SUCCESS(Status) ? (ZP_CHANNEL_HANDLE)Channel : NULL,
        NT_SUCCESS(Status) ? FileSize : 0,
        NT_SUCCESS(Status) ? Offset : 0,
        FileContext->Context);
    if (Channel != NULL)
    {
        if (Remaining != 0)
        {
            Status = ZpServerChannel_SendWindow(
                Channel,
                (ULONG)min(Remaining,
                           ZP_SERVER_DEFAULT_CHANNEL_WINDOW_SIZE));
            if (!NT_SUCCESS(Status))
            {
                ZpServerChannel_Abort(Channel, Status);
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
    _In_ NTSTATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_FILE_OPEN_WRITE_CONTEXT FileContext = Context;
    PZP_SERVER_CHANNEL_OBJECT Channel = NULL;
    ULONGLONG ChannelId = 0, FileSize = 0;

    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_DecodeOpenWriteResponse(Payload->Buffer,
                                                Payload->Length,
                                                &ChannelId,
                                                &FileSize);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerChannel_Create(
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
            &Channel);
    }
    else
    {
        ZpServerChannel_ReleaseReservation(FileContext->Connection);
    }
    if (!NT_SUCCESS(Status) && ChannelId != 0)
    {
        ZpServerConnection_RejectChannel(FileContext->Connection,
                                         ChannelId,
                                         Status);
    }
    FileContext->OpenCallback(
        Request,
        Status,
        NT_SUCCESS(Status) ? (ZP_CHANNEL_HANDLE)Channel : NULL,
        NT_SUCCESS(Status) ? FileSize : 0,
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
