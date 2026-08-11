#include "Bridge.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#define ZP_NATIVE_TIMEOUT_MILLISECONDS 10000

typedef struct _ZP_NATIVE_CALLBACK_CONTEXT
{
    ZP_CONNECTION_HANDLE Connection;
    union
    {
        ZP_NATIVE_SYSTEM_INFO_CALLBACK SystemInfo;
        ZP_NATIVE_STATUS_CALLBACK Status;
        ZP_NATIVE_EVENT_LOG_CALLBACK EventLog;
        ZP_NATIVE_TERMINAL_SHELLS_CALLBACK TerminalShells;
    } Callback;
    PVOID Context;
} ZP_NATIVE_CALLBACK_CONTEXT, *PZP_NATIVE_CALLBACK_CONTEXT;

typedef struct _ZP_NATIVE_TERMINAL
{
    RTL_SRWLOCK Lock;
    volatile LONG ReferenceCount;
    volatile LONG CallerClosed;
    ZP_CONNECTION_HANDLE Connection;
    ZP_CHANNEL_HANDLE Channel;
    ZP_NATIVE_TERMINAL_CREATE_CALLBACK CreateCallback;
    ZP_NATIVE_TERMINAL_DATA_CALLBACK DataCallback;
    ZP_NATIVE_TERMINAL_WRITABLE_CALLBACK WritableCallback;
    ZP_NATIVE_TERMINAL_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_NATIVE_TERMINAL, *PZP_NATIVE_TERMINAL;

static RTL_SRWLOCK ZpNativeLock = RTL_SRWLOCK_INIT;
static ZP_SERVER_HANDLE ZpNativeServer;
static ZP_CONNECTION_HANDLE ZpNativeConnection;
static HANDLE ZpNativeStateEvent;
static ZP_SERVER_STATE ZpNativeState = ZpServerStateStopped;
static ZP_STATUS ZpNativeStateStatus;

static
VOID
NTAPI
ZpNative_ServerStateCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_SERVER_STATE State,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(Context);
    RtlAcquireSRWLockExclusive(&ZpNativeLock);
    ZpNativeState = State;
    ZpNativeStateStatus = Status;
    RtlReleaseSRWLockExclusive(&ZpNativeLock);
    SetEvent(ZpNativeStateEvent);
}

static
VOID
NTAPI
ZpNative_ServerConnectionCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_CONNECTION_PHASE Phase,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE ReleasedConnection = NULL;

    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Context);
    RtlAcquireSRWLockExclusive(&ZpNativeLock);
    if (Phase == ZpConnectionPhaseReady)
    {
        ZpConnection_AddRef(Connection);
        ReleasedConnection = ZpNativeConnection;
        ZpNativeConnection = Connection;
    }
    else if (Phase == ZpConnectionPhaseClosed &&
             ZpNativeConnection == Connection)
    {
        ReleasedConnection = ZpNativeConnection;
        ZpNativeConnection = NULL;
    }
    RtlReleaseSRWLockExclusive(&ZpNativeLock);
    if (ReleasedConnection != NULL)
    {
        ZpConnection_Release(ReleasedConnection);
    }
}

static
ZP_CONNECTION_HANDLE
ZpNative_GetConnection(VOID)
{
    ZP_CONNECTION_HANDLE Connection;

    RtlAcquireSRWLockShared(&ZpNativeLock);
    Connection = ZpNativeConnection;
    if (Connection != NULL)
    {
        ZpConnection_AddRef(Connection);
    }
    RtlReleaseSRWLockShared(&ZpNativeLock);
    return Connection;
}

static
VOID
ZpNative_FreeCallbackContext(
    _In_ PZP_NATIVE_CALLBACK_CONTEXT CallbackContext)
{
    ZpConnection_Release(CallbackContext->Connection);
    Mem_Free(CallbackContext);
}

static
VOID
NTAPI
ZpNative_SystemInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_SYSTEM_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.SystemInfo(
        Status,
        ZpStatus_IsSuccess(Status) ? Info->Architecture : 0,
        ZpStatus_IsSuccess(Status) ? Info->MajorVersion : 0,
        ZpStatus_IsSuccess(Status) ? Info->MinorVersion : 0,
        ZpStatus_IsSuccess(Status) ? Info->BuildNumber : 0,
        ZpStatus_IsSuccess(Status) ? Info->ProcessorCount : 0,
        ZpStatus_IsSuccess(Status) ? Info->PhysicalMemoryBytes : 0,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Info->ComputerName.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Info->ComputerName.Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_StatusCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.Status(Status, CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_EventLogCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_EVENT_LOG_PAGE_VIEW* Page,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_EVENT_LOG_RECORD Records = NULL;
    ZP_EVENT_LOG_RECORD_VIEW Record;
    ULONG Index;
    NTSTATUS DecodeStatus = STATUS_SUCCESS;

    if (ZpStatus_IsSuccess(Status) && Page->Records.Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Page->Records.Count * sizeof(*Records));
        if (Records == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
    }
    for (Index = 0;
         ZpStatus_IsSuccess(Status) && Index < Page->Records.Count;
         Index++)
    {
        DecodeStatus = ZpEventLog_GetRecord(&Page->Records, Index, &Record);
        if (NT_SUCCESS(DecodeStatus))
        {
            Records[Index].Bookmark = (PCWCH)Record.Bookmark.Buffer;
            Records[Index].BookmarkLength = Record.Bookmark.Length;
            Records[Index].Xml = (PCWCH)Record.Xml.Buffer;
            Records[Index].XmlLength = Record.Xml.Length;
        }
        else
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
        }
    }
    CallbackContext->Callback.EventLog(
        Status,
        ZpStatus_IsSuccess(Status) ? Page->HasMore : FALSE,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Page->NextBookmark.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Page->NextBookmark.Length : 0,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Page->Records.Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_TerminalShellsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ ULONG Shells,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.TerminalShells(Status,
                                             Shells,
                                             CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
ZpNative_ReleaseTerminal(
    _Inout_ PZP_NATIVE_TERMINAL Terminal)
{
    if (InterlockedDecrement(&Terminal->ReferenceCount) == 0)
    {
        ZpConnection_Release(Terminal->Connection);
        Mem_Free(Terminal);
    }
}

static
VOID
NTAPI
ZpNative_TerminalCreateCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG ProcessId,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TERMINAL Terminal = Context;

    if (ZpStatus_IsSuccess(Status))
    {
        Terminal->Channel = Channel;
        Terminal->ReferenceCount = 2;
    }
    Terminal->CreateCallback(Status,
                             ZpStatus_IsSuccess(Status) ? Terminal : NULL,
                             ProcessId,
                             Terminal->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Terminal->Connection);
        Mem_Free(Terminal);
    }
}

static
VOID
NTAPI
ZpNative_TerminalDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TERMINAL Terminal = Context;

    if (!Terminal->DataCallback(Data->Buffer,
                                Data->Length,
                                Terminal->Context))
    {
        ZpChannel_Cancel(Channel);
    }
}

static
VOID
NTAPI
ZpNative_TerminalWritableCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TERMINAL Terminal = Context;

    UNREFERENCED_PARAMETER(Channel);
    Terminal->WritableCallback(CreditBytes, Terminal->Context);
}

static
VOID
NTAPI
ZpNative_TerminalCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TERMINAL Terminal = Context;

    RtlAcquireSRWLockExclusive(&Terminal->Lock);
    Terminal->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Terminal->Lock);
    Terminal->CloseCallback(Status, Terminal->Context);
    ZpChannel_Close(Channel);
    ZpNative_ReleaseTerminal(Terminal);
}

static
PZP_NATIVE_CALLBACK_CONTEXT
ZpNative_CreateCallbackContext(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;

    CallbackContext = Mem_Alloc(sizeof(*CallbackContext));
    if (CallbackContext != NULL)
    {
        CallbackContext->Connection = Connection;
        CallbackContext->Context = Context;
    }
    return CallbackContext;
}

ZP_STATUS
NTAPI
ZpNative_Start(
    _In_ PCCERT_CONTEXT Certificate,
    _In_ USHORT Port)
{
    static const ZP_MODULE_RECORD Modules[] = {
        { ZP_SYSTEM_MODULE_ID, ZP_SYSTEM_MODULE_VERSION },
        { ZP_PROCESS_MODULE_ID, ZP_PROCESS_MODULE_VERSION },
        { ZP_SERVICE_MODULE_ID, ZP_SERVICE_MODULE_VERSION },
        { ZP_FILE_MODULE_ID, ZP_FILE_MODULE_VERSION },
        { ZP_TERMINAL_MODULE_ID, ZP_TERMINAL_MODULE_VERSION },
        { ZP_EVENT_LOG_MODULE_ID, ZP_EVENT_LOG_MODULE_VERSION },
        { ZP_REGISTRY_MODULE_ID, ZP_REGISTRY_MODULE_VERSION }
    };
    ZP_LISTENER_ENDPOINT Listener = {
        ZpTransportQuic,
        L"127.0.0.1",
        Port,
        NULL
    };
    ZP_SERVER_DEPLOYMENT Deployment = { L"localhost", Certificate };
    ZP_SERVER_CONFIG Config = { 0 };
    NTSTATUS CreateStatus;
    ZP_STATUS Status;

    if (Certificate == NULL || Port == 0)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    RtlAcquireSRWLockExclusive(&ZpNativeLock);
    if (ZpNativeServer != NULL)
    {
        RtlReleaseSRWLockExclusive(&ZpNativeLock);
        return ZpStatus_FromNtStatus(STATUS_INVALID_DEVICE_STATE);
    }
    RtlReleaseSRWLockExclusive(&ZpNativeLock);
    ZpNativeStateEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (ZpNativeStateEvent == NULL)
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    Config.Size = sizeof(Config);
    Config.Listeners = &Listener;
    Config.ListenerCount = 1;
    Config.Deployments = &Deployment;
    Config.DeploymentCount = 1;
    Config.Modules = Modules;
    Config.ModuleCount = ARRAYSIZE(Modules);
    Config.StateCallback = ZpNative_ServerStateCallback;
    Config.ConnectionCallback = ZpNative_ServerConnectionCallback;
    CreateStatus = ZpServer_Create(&Config, &ZpNativeServer);
    if (NT_SUCCESS(CreateStatus))
    {
        Status = ZpServer_Start(ZpNativeServer);
    }
    else
    {
        Status = ZpStatus_FromNtStatus(CreateStatus);
    }
    if (ZpStatus_IsSuccess(Status) &&
        WaitForSingleObject(ZpNativeStateEvent,
                            ZP_NATIVE_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_IO_TIMEOUT);
    }
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpNativeState == ZpServerStateRunning ?
                     ZpNativeStateStatus :
                     ZpStatus_FromNtStatus(STATUS_INVALID_DEVICE_STATE);
    }
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpNative_Stop();
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_Stop(VOID)
{
    ZP_SERVER_HANDLE Server;
    ZP_CONNECTION_HANDLE Connection;
    NTSTATUS Status = STATUS_SUCCESS, CloseStatus;

    RtlAcquireSRWLockExclusive(&ZpNativeLock);
    Server = ZpNativeServer;
    ZpNativeServer = NULL;
    Connection = ZpNativeConnection;
    ZpNativeConnection = NULL;
    RtlReleaseSRWLockExclusive(&ZpNativeLock);
    if (Server != NULL)
    {
        ResetEvent(ZpNativeStateEvent);
        Status = ZpServer_Stop(Server);
        if (NT_SUCCESS(Status) &&
            WaitForSingleObject(ZpNativeStateEvent,
                                ZP_NATIVE_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0)
        {
            Status = STATUS_IO_TIMEOUT;
        }
        CloseStatus = ZpServer_Close(Server);
        if (NT_SUCCESS(Status))
        {
            Status = CloseStatus;
        }
    }
    if (Connection != NULL)
    {
        ZpConnection_Release(Connection);
    }
    if (ZpNativeStateEvent != NULL)
    {
        CloseHandle(ZpNativeStateEvent);
        ZpNativeStateEvent = NULL;
    }
    return Status;
}

ZP_SERVER_STATE
NTAPI
ZpNative_GetState(VOID)
{
    ZP_SERVER_STATE State;

    RtlAcquireSRWLockShared(&ZpNativeLock);
    State = ZpNativeState;
    RtlReleaseSRWLockShared(&ZpNativeLock);
    return State;
}

LOGICAL
NTAPI
ZpNative_IsClientConnected(VOID)
{
    LOGICAL Connected;

    RtlAcquireSRWLockShared(&ZpNativeLock);
    Connected = ZpNativeConnection != NULL;
    RtlReleaseSRWLockShared(&ZpNativeLock);
    return Connected;
}

NTSTATUS
NTAPI
ZpNative_GetSystemInfo(
    _In_ ZP_NATIVE_SYSTEM_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection();
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.SystemInfo = Callback;
    Status = ZpServer_GetSystemInfo(Connection,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_SystemInfoCallback,
                                    CallbackContext,
                                    &Request);
    if (!NT_SUCCESS(Status))
    {
        ZpNative_FreeCallbackContext(CallbackContext);
    }
    return Status;
}

static
NTSTATUS
ZpNative_SendStatusRequest(
    _In_ PZP_NATIVE_CALLBACK_CONTEXT CallbackContext,
    _In_ NTSTATUS Status)
{
    if (!NT_SUCCESS(Status))
    {
        ZpNative_FreeCallbackContext(CallbackContext);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_TerminateProcess(
    _In_ ULONG ProcessId,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection();
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_TerminateProcess(Connection,
                                  ProcessId,
                                  0,
                                  ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                  ZpNative_StatusCallback,
                                  CallbackContext,
                                  &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryTerminalShells(
    _In_ ZP_NATIVE_TERMINAL_SHELLS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection();
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.TerminalShells = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryTerminalShells(Connection,
                                     ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                     ZpNative_TerminalShellsCallback,
                                     CallbackContext,
                                     &Request));
}

NTSTATUS
NTAPI
ZpNative_CreateTerminal(
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _In_reads_(CommandLineLength) PCWCH CommandLine,
    _In_ ULONG CommandLineLength,
    _In_reads_opt_(WorkingDirectoryLength) PCWCH WorkingDirectory,
    _In_ ULONG WorkingDirectoryLength,
    _In_ ZP_NATIVE_TERMINAL_CREATE_CALLBACK CreateCallback,
    _In_ ZP_NATIVE_TERMINAL_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_TERMINAL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_NATIVE_TERMINAL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_TERMINAL Terminal;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (CreateCallback == NULL || DataCallback == NULL ||
        WritableCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection();
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    Terminal = Mem_Alloc(sizeof(*Terminal));
    if (Terminal == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Terminal, sizeof(*Terminal));
    Terminal->Connection = Connection;
    Terminal->CreateCallback = CreateCallback;
    Terminal->DataCallback = DataCallback;
    Terminal->WritableCallback = WritableCallback;
    Terminal->CloseCallback = CloseCallback;
    Terminal->Context = Context;
    Status = ZpServer_CreateTerminal(Connection,
                                     Columns,
                                     Rows,
                                     CommandLine,
                                     CommandLineLength,
                                     WorkingDirectory,
                                     WorkingDirectoryLength,
                                     ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                     ZpNative_TerminalCreateCallback,
                                     ZpNative_TerminalDataCallback,
                                     ZpNative_TerminalWritableCallback,
                                     ZpNative_TerminalCloseCallback,
                                     Terminal,
                                     &Request);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Release(Connection);
        Mem_Free(Terminal);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_TerminalSend(
    _In_ ZP_NATIVE_TERMINAL_HANDLE Terminal,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    NTSTATUS Status;

    if (Terminal == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }
    RtlAcquireSRWLockShared(&Terminal->Lock);
    Status = Terminal->Channel != NULL ?
                 ZpChannel_Send(Terminal->Channel, Data, DataLength) :
                 STATUS_INVALID_DEVICE_STATE;
    RtlReleaseSRWLockShared(&Terminal->Lock);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_ResizeTerminal(
    _In_ ZP_NATIVE_TERMINAL_HANDLE Terminal,
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (Terminal == NULL || Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlAcquireSRWLockShared(&Terminal->Lock);
    if (Terminal->Channel == NULL)
    {
        RtlReleaseSRWLockShared(&Terminal->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    ZpConnection_AddRef(Terminal->Connection);
    CallbackContext = ZpNative_CreateCallbackContext(Terminal->Connection,
                                                     Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Terminal->Connection);
        RtlReleaseSRWLockShared(&Terminal->Lock);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    Status = ZpServer_ResizeTerminal(Terminal->Connection,
                                     Terminal->Channel,
                                     Columns,
                                     Rows,
                                     ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                     ZpNative_StatusCallback,
                                     CallbackContext,
                                     &Request);
    RtlReleaseSRWLockShared(&Terminal->Lock);
    return ZpNative_SendStatusRequest(CallbackContext, Status);
}

NTSTATUS
NTAPI
ZpNative_CloseTerminal(
    _In_ ZP_NATIVE_TERMINAL_HANDLE Terminal)
{
    ZP_CHANNEL_HANDLE Channel;
    NTSTATUS Status;

    if (Terminal == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (InterlockedExchange(&Terminal->CallerClosed, TRUE))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    RtlAcquireSRWLockExclusive(&Terminal->Lock);
    Channel = Terminal->Channel;
    Terminal->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Terminal->Lock);
    Status = Channel != NULL ? ZpChannel_Cancel(Channel) : STATUS_SUCCESS;
    ZpNative_ReleaseTerminal(Terminal);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_QueryEventLogPage(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_reads_opt_(BookmarkLength) PCWCH Bookmark,
    _In_ ULONG BookmarkLength,
    _In_ ULONG MaxEvents,
    _In_ ZP_NATIVE_EVENT_LOG_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection();
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.EventLog = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryEventLogPage(
            Connection,
            BookmarkLength == 0 ? ZpEventLogStartOldest :
                                  ZpEventLogStartAfterBookmark,
            MaxEvents,
            ChannelPath,
            ChannelPathLength,
            Query,
            QueryLength,
            Bookmark,
            BookmarkLength,
            ZP_NATIVE_TIMEOUT_MILLISECONDS,
            ZpNative_EventLogCallback,
            CallbackContext,
            &Request));
}

NTSTATUS
NTAPI
ZpNative_SetEventLogChannelEnabled(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ BOOLEAN Enabled,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection();
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_SetEventLogChannelEnabled(
            Connection,
            ChannelPath,
            ChannelPathLength,
            Enabled,
            ZP_NATIVE_TIMEOUT_MILLISECONDS,
            ZpNative_StatusCallback,
            CallbackContext,
            &Request));
}

NTSTATUS
NTAPI
ZpNative_ClearEventLog(
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection();
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ClearEventLog(Connection,
                               ChannelPath,
                               ChannelPathLength,
                               ZP_NATIVE_TIMEOUT_MILLISECONDS,
                               ZpNative_StatusCallback,
                               CallbackContext,
                               &Request));
}
