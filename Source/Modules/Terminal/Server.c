#include <KNSoft/MakeLifeEasier/Memory/Core.h>
#include <KNSoft/ZPigeon/Server.h>
#include <KNSoft/ZPigeon/Terminal.h>
#include "../../KNSoft.ZPigeon.Server.SDK/Core/Channel.h"
#include "../../SDK/Channel.h"

typedef struct _ZP_SERVER_TERMINAL_CONTEXT
{
    PZP_CONNECTION_OBJECT Connection;
    ZP_TERMINAL_CREATE_CALLBACK CreateCallback;
    ZP_CHANNEL_DATA_CALLBACK DataCallback;
    ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback;
    ZP_CHANNEL_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_SERVER_TERMINAL_CONTEXT, *PZP_SERVER_TERMINAL_CONTEXT;

typedef struct _ZP_SERVER_TERMINAL_RESIZE_CONTEXT
{
    ZP_REQUEST_STATUS_CALLBACK Callback;
    PVOID Context;
} ZP_SERVER_TERMINAL_RESIZE_CONTEXT,
  *PZP_SERVER_TERMINAL_RESIZE_CONTEXT;

typedef struct _ZP_SERVER_TERMINAL_SHELL_CONTEXT
{
    ZP_TERMINAL_SHELLS_CALLBACK Callback;
    PVOID Context;
} ZP_SERVER_TERMINAL_SHELL_CONTEXT, *PZP_SERVER_TERMINAL_SHELL_CONTEXT;

static
VOID
NTAPI
ZpServerTerminal_CreateComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_TERMINAL_CONTEXT TerminalContext = Context;
    PZP_SERVER_CHANNEL_OBJECT Channel = NULL;
    ULONGLONG ChannelId = 0;
    ULONG ProcessId = 0;
    NTSTATUS ChannelStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpTerminal_DecodeCreateResponse(Payload->Buffer,
                                            Payload->Length,
                                            &ChannelId,
                                            &ProcessId));
    }
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpServerChannel_Create(
                TerminalContext->Connection,
                ChannelId,
                ZP_TERMINAL_MODULE_ID,
                FALSE,
                0,
                FALSE,
                0,
                TerminalContext->DataCallback,
                TerminalContext->WritableCallback,
                TerminalContext->CloseCallback,
                TerminalContext->Context,
                TRUE,
                &Channel));
    }
    else
    {
        ZpServerChannel_ReleaseReservation(TerminalContext->Connection);
    }
    if (!ZpStatus_IsSuccess(Status) && ChannelId != 0)
    {
        ZpServerConnection_RejectChannel(TerminalContext->Connection,
                                         ChannelId,
                                         Status);
    }
    TerminalContext->CreateCallback(
        Request,
        Status,
        ZpStatus_IsSuccess(Status) ? (ZP_CHANNEL_HANDLE)Channel : NULL,
        ZpStatus_IsSuccess(Status) ? ProcessId : 0,
        TerminalContext->Context);
    if (Channel != NULL)
    {
        ChannelStatus = ZpServerChannel_SendWindow(
            Channel,
            ZP_SERVER_DEFAULT_CHANNEL_WINDOW_SIZE);
        if (!NT_SUCCESS(ChannelStatus))
        {
            ZpServerChannel_Abort(
                Channel,
                ZpStatus_FromNtStatus(ChannelStatus));
        }
        ZpChannel_Close((ZP_CHANNEL_HANDLE)Channel);
    }
    Mem_Free(TerminalContext);
}

static
VOID
NTAPI
ZpServerTerminal_ResizeComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_TERMINAL_RESIZE_CONTEXT ResizeContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    ResizeContext->Callback(Request, Status, ResizeContext->Context);
    Mem_Free(ResizeContext);
}

static
VOID
NTAPI
ZpServerTerminal_QueryShellsComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_TERMINAL_SHELL_CONTEXT ShellContext = Context;
    ULONG Shells = 0;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpTerminal_DecodeShells(Payload->Buffer,
                                    Payload->Length,
                                    &Shells));
    }
    ShellContext->Callback(Request,
                           Status,
                           ZpStatus_IsSuccess(Status) ? Shells : 0,
                           ShellContext->Context);
    Mem_Free(ShellContext);
}

NTSTATUS
NTAPI
ZpServer_CreateTerminal(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _In_reads_(CommandLineLength) PCWCH CommandLine,
    _In_ ULONG CommandLineLength,
    _In_reads_opt_(WorkingDirectoryLength) PCWCH WorkingDirectory,
    _In_ ULONG WorkingDirectoryLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_TERMINAL_CREATE_CALLBACK CreateCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_TERMINAL_CONTEXT TerminalContext;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;
    LOGICAL Reserved = FALSE;

    if (CreateCallback == NULL || DataCallback == NULL ||
        WritableCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpTerminal_EncodeCreate(Columns,
                                     Rows,
                                     CommandLine,
                                     CommandLineLength,
                                     WorkingDirectory,
                                     WorkingDirectoryLength,
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
        Status = ZpTerminal_EncodeCreate(Columns,
                                         Rows,
                                         CommandLine,
                                         CommandLineLength,
                                         WorkingDirectory,
                                         WorkingDirectoryLength,
                                         Payload,
                                         PayloadLength,
                                         &PayloadLength);
    }
    TerminalContext = NT_SUCCESS(Status) ?
                          Mem_Alloc(sizeof(*TerminalContext)) : NULL;
    if (NT_SUCCESS(Status) && TerminalContext == NULL)
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
        TerminalContext->Connection = Connection;
        TerminalContext->CreateCallback = CreateCallback;
        TerminalContext->DataCallback = DataCallback;
        TerminalContext->WritableCallback = WritableCallback;
        TerminalContext->CloseCallback = CloseCallback;
        TerminalContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_TERMINAL_MODULE_ID,
                                      ZP_TERMINAL_OPERATION_CREATE,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpServerTerminal_CreateComplete,
                                      TerminalContext,
                                      Request);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(TerminalContext);
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

NTSTATUS
NTAPI
ZpServer_ResizeTerminal(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_TERMINAL_RESIZE_CONTEXT ResizeContext;
    BYTE Payload[sizeof(ULONGLONG) + 2 * sizeof(USHORT)];
    ULONG PayloadLength;
    ULONGLONG ChannelId;
    NTSTATUS Status;

    if (Connection == NULL || Channel == NULL || Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpServerChannel_GetId((PZP_SERVER_CHANNEL_OBJECT)Channel,
                                   Connection,
                                   ZP_TERMINAL_MODULE_ID,
                                   &ChannelId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpTerminal_EncodeResize(ChannelId,
                                         Columns,
                                         Rows,
                                         Payload,
                                         sizeof(Payload),
                                         &PayloadLength);
    }
    ResizeContext = NT_SUCCESS(Status) ?
                        Mem_Alloc(sizeof(*ResizeContext)) : NULL;
    if (NT_SUCCESS(Status) && ResizeContext == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        ResizeContext->Callback = Callback;
        ResizeContext->Context = Context;
        Status = ZpServer_SendRequest(Connection,
                                      ZP_TERMINAL_MODULE_ID,
                                      ZP_TERMINAL_OPERATION_RESIZE,
                                      TimeoutMilliseconds,
                                      Payload,
                                      PayloadLength,
                                      ZpServerTerminal_ResizeComplete,
                                      ResizeContext,
                                      Request);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(ResizeContext);
        }
    }
    return Status;
}

NTSTATUS
NTAPI
ZpServer_QueryTerminalShells(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_TERMINAL_SHELLS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_TERMINAL_SHELL_CONTEXT ShellContext;
    NTSTATUS Status;

    if (Connection == NULL || Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ShellContext = Mem_Alloc(sizeof(*ShellContext));
    if (ShellContext == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    ShellContext->Callback = Callback;
    ShellContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_TERMINAL_MODULE_ID,
                                  ZP_TERMINAL_OPERATION_QUERY_SHELLS,
                                  TimeoutMilliseconds,
                                  NULL,
                                  0,
                                  ZpServerTerminal_QueryShellsComplete,
                                  ShellContext,
                                  Request);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(ShellContext);
    }
    return Status;
}
