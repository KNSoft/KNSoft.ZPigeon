NTSTATUS
NTAPI
ZpNative_QueryTerminalShells(
    _In_ ULONGLONG ClientId,
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
    Connection = ZpNative_GetConnection(ClientId);
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
    _In_ ULONGLONG ClientId,
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _In_ BYTE Identity,
    _In_ ULONG SessionId,
    _In_ ULONG Flags,
    _In_reads_(FileNameLength) PCWCH FileName,
    _In_ ULONG FileNameLength,
    _In_reads_opt_(ArgumentsLength) PCWCH Arguments,
    _In_ ULONG ArgumentsLength,
    _In_reads_opt_(WorkingDirectoryLength) PCWCH WorkingDirectory,
    _In_ ULONG WorkingDirectoryLength,
    _In_reads_opt_(UserNameLength) PCWCH UserName,
    _In_ ULONG UserNameLength,
    _In_reads_opt_(PasswordLength) PCWCH Password,
    _In_ ULONG PasswordLength,
    _In_reads_opt_(AppContainerSidLength) PCWCH AppContainerSid,
    _In_ ULONG AppContainerSidLength,
    _In_reads_bytes_opt_(CustomTokenLength) const VOID* CustomToken,
    _In_ ULONG CustomTokenLength,
    _In_ ZP_NATIVE_TERMINAL_CREATE_CALLBACK CreateCallback,
    _In_ ZP_NATIVE_TERMINAL_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_TERMINAL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_NATIVE_TERMINAL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    ZP_EXECUTION_START Start = {
        ZpExecutionEngineCreateProcess,
        Identity,
        SessionId,
        Flags,
        FileName,
        FileNameLength,
        Arguments,
        ArgumentsLength,
        WorkingDirectory,
        WorkingDirectoryLength,
        NULL,
        0,
        UserName,
        UserNameLength,
        Password,
        PasswordLength,
        AppContainerSid,
        AppContainerSidLength,
        CustomToken,
        CustomTokenLength
    };
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_TERMINAL Terminal;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (CreateCallback == NULL || DataCallback == NULL ||
        WritableCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
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
                                     &Start,
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
ZpNative_OpenTunnel(
    _In_ ULONGLONG ClientId,
    _In_reads_(HostLength) PCWCH Host,
    _In_ ULONG HostLength,
    _In_ USHORT Port,
    _In_ ZP_TUNNEL_PROTOCOL Protocol,
    _In_ ZP_NATIVE_TUNNEL_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_TUNNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_TUNNEL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_NATIVE_TUNNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_TUNNEL Tunnel;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (Host == NULL || HostLength == 0 || Port == 0 || OpenCallback == NULL || DataCallback == NULL ||
        WritableCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    Tunnel = Mem_Alloc(sizeof(*Tunnel));
    if (Tunnel == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Tunnel, sizeof(*Tunnel));
    Tunnel->Connection = Connection;
    Tunnel->OpenCallback = OpenCallback;
    Tunnel->DataCallback = DataCallback;
    Tunnel->WritableCallback = WritableCallback;
    Tunnel->CloseCallback = CloseCallback;
    Tunnel->Context = Context;
    Status = ZpServer_OpenTunnel(Connection,
                                 Host,
                                 HostLength,
                                 Port,
                                 Protocol,
                                 ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                 ZpNative_TunnelOpenCallback,
                                 ZpNative_TunnelDataCallback,
                                 ZpNative_TunnelWritableCallback,
                                 ZpNative_TunnelCloseCallback,
                                 Tunnel,
                                 &Request);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Release(Connection);
        Mem_Free(Tunnel);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_EnumerateSerialPorts(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_SERIAL_PORTS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.SerialPorts = Callback;
    return ZpNative_SendStatusRequest(CallbackContext,
                                      ZpServer_EnumerateSerialPorts(Connection,
                                                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                                                    ZpNative_SerialPortsCallback,
                                                                    CallbackContext,
                                                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryRecordingCapabilities(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_RECORDING_CAPABILITIES_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.RecordingCapabilities = Callback;
    return ZpNative_SendStatusRequest(CallbackContext,
                                      ZpServer_QueryRecordingCapabilities(Connection,
                                                                           ZpNative_RecordingCapabilitiesCallback,
                                                                           CallbackContext,
                                                                           &Request));
}

NTSTATUS
NTAPI
ZpNative_StartRecording(
    _In_ ULONGLONG ClientId,
    _In_ BYTE Source,
    _In_ BYTE Codec,
    _In_ BYTE FrameRate,
    _In_ BYTE AudioSource,
    _In_ BYTE Flags,
    _In_ ULONG MaxDimension,
    _In_ ULONG VideoBitRate,
    _In_ ULONG AudioBitRate,
    _In_ ULONGLONG WindowHandle,
    _In_reads_opt_(SourceIdLength) PCWCH SourceId,
    _In_ ULONG SourceIdLength,
    _In_reads_opt_(AudioDeviceIdLength) PCWCH AudioDeviceId,
    _In_ ULONG AudioDeviceIdLength,
    _In_ ZP_NATIVE_RECORDING_RECORDS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_RECORDING_START Start;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    Start.Source = Source;
    Start.Codec = Codec;
    Start.FrameRate = FrameRate;
    Start.AudioSource = AudioSource;
    Start.Flags = Flags;
    Start.MaxDimension = MaxDimension;
    Start.VideoBitRate = VideoBitRate;
    Start.AudioBitRate = AudioBitRate;
    Start.WindowHandle = WindowHandle;
    Start.SourceId = SourceId;
    Start.SourceIdLength = SourceIdLength;
    Start.AudioDeviceId = AudioDeviceId;
    Start.AudioDeviceIdLength = AudioDeviceIdLength;
    CallbackContext->Callback.RecordingRecords = Callback;
    return ZpNative_SendStatusRequest(CallbackContext,
                                      ZpServer_StartRecording(Connection,
                                                              &Start,
                                                              ZpNative_RecordingRecordsCallback,
                                                              CallbackContext,
                                                              &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateRecordings(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_RECORDING_RECORDS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.RecordingRecords = Callback;
    return ZpNative_SendStatusRequest(CallbackContext,
                                      ZpServer_EnumerateRecordings(Connection,
                                                                  ZpNative_RecordingRecordsCallback,
                                                                  CallbackContext,
                                                                  &Request));
}

static
NTSTATUS
ZpNative_RecordingStatus(
    _In_ ULONGLONG ClientId,
    _In_ ULONG RecordingId,
    _In_ LOGICAL Delete,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    Status = Delete ? ZpServer_DeleteRecording(Connection,
                                                RecordingId,
                                                ZpNative_StatusCallback,
                                                CallbackContext,
                                                &Request) :
                      ZpServer_StopRecording(Connection,
                                              RecordingId,
                                              ZpNative_StatusCallback,
                                              CallbackContext,
                                              &Request);
    return ZpNative_SendStatusRequest(CallbackContext, Status);
}

NTSTATUS
NTAPI
ZpNative_StopRecording(
    _In_ ULONGLONG ClientId,
    _In_ ULONG RecordingId,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    return ZpNative_RecordingStatus(ClientId, RecordingId, FALSE, Callback, Context);
}

NTSTATUS
NTAPI
ZpNative_DeleteRecording(
    _In_ ULONGLONG ClientId,
    _In_ ULONG RecordingId,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    return ZpNative_RecordingStatus(ClientId, RecordingId, TRUE, Callback, Context);
}

NTSTATUS
NTAPI
ZpNative_OpenSerialPort(
    _In_ ULONGLONG ClientId,
    _In_reads_(PortLength) PCWCH Port,
    _In_ ULONG PortLength,
    _In_ ULONG BaudRate,
    _In_ BYTE DataBits,
    _In_ BYTE Parity,
    _In_ BYTE StopBits,
    _In_ BYTE FlowControl,
    _In_ ZP_NATIVE_TUNNEL_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_TUNNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_TUNNEL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_NATIVE_TUNNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_TUNNEL Tunnel;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (Port == NULL || PortLength == 0 || OpenCallback == NULL || DataCallback == NULL ||
        WritableCallback == NULL || CloseCallback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    Tunnel = Mem_Alloc(sizeof(*Tunnel));
    if (Tunnel == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Tunnel, sizeof(*Tunnel));
    Tunnel->Connection = Connection;
    Tunnel->OpenCallback = OpenCallback;
    Tunnel->DataCallback = DataCallback;
    Tunnel->WritableCallback = WritableCallback;
    Tunnel->CloseCallback = CloseCallback;
    Tunnel->Context = Context;
    Status = ZpServer_OpenSerialPort(Connection,
                                     Port,
                                     PortLength,
                                     BaudRate,
                                     DataBits,
                                     Parity,
                                     StopBits,
                                     FlowControl,
                                     ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                     ZpNative_TunnelOpenCallback,
                                     ZpNative_TunnelDataCallback,
                                     ZpNative_TunnelWritableCallback,
                                     ZpNative_TunnelCloseCallback,
                                     Tunnel,
                                     &Request);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Release(Connection);
        Mem_Free(Tunnel);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_TunnelSend(
    _In_ ZP_NATIVE_TUNNEL_HANDLE Tunnel,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    NTSTATUS Status;

    if (Tunnel == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }
    RtlAcquireSRWLockShared(&Tunnel->Lock);
    Status = Tunnel->Channel != NULL ?
                 ZpChannel_Send(Tunnel->Channel, Data, DataLength) :
                 STATUS_INVALID_DEVICE_STATE;
    RtlReleaseSRWLockShared(&Tunnel->Lock);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_CloseTunnel(
    _In_ ZP_NATIVE_TUNNEL_HANDLE Tunnel)
{
    ZP_CHANNEL_HANDLE Channel;
    NTSTATUS Status;

    if (Tunnel == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (InterlockedExchange(&Tunnel->CallerClosed, TRUE))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    RtlAcquireSRWLockExclusive(&Tunnel->Lock);
    Channel = Tunnel->Channel;
    Tunnel->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Tunnel->Lock);
    Status = Channel != NULL ? ZpChannel_Cancel(Channel) : STATUS_SUCCESS;
    ZpNative_ReleaseTunnel(Tunnel);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_EnumerateEventLogChannels(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_EVENT_LOG_CHANNELS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.EventLogChannels = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateEventLogChannels(Connection,
                                            ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                            ZpNative_EventLogChannelsCallback,
                                            CallbackContext,
                                            &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryEventLogChannelInfo(
    _In_ ULONGLONG ClientId,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ ZP_NATIVE_EVENT_LOG_CHANNEL_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.EventLogChannelInfo = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryEventLogChannelInfo(Connection,
                                           ChannelPath,
                                           ChannelPathLength,
                                           ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                           ZpNative_EventLogChannelInfoCallback,
                                           CallbackContext,
                                           &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryEventLogPage(
    _In_ ULONGLONG ClientId,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_reads_opt_(BookmarkLength) PCWCH Bookmark,
    _In_ ULONG BookmarkLength,
    _In_ BOOLEAN Forward,
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
    Connection = ZpNative_GetConnection(ClientId);
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
            Forward ?
                (BookmarkLength == 0 ? ZpEventLogStartForward :
                                       ZpEventLogStartAfterBookmarkForward) :
                (BookmarkLength == 0 ? ZpEventLogStartOldest :
                                       ZpEventLogStartAfterBookmark),
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
    _In_ ULONGLONG ClientId,
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
    Connection = ZpNative_GetConnection(ClientId);
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
    _In_ ULONGLONG ClientId,
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
    Connection = ZpNative_GetConnection(ClientId);
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

NTSTATUS
NTAPI
ZpNative_ConfigureEventLogChannel(
    _In_ ULONGLONG ClientId,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ BOOLEAN Enabled,
    _In_ ZP_EVENT_LOG_RETENTION_MODE RetentionMode,
    _In_ ULONGLONG MaximumSize,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ConfigureEventLogChannel(Connection,
                                           ChannelPath,
                                           ChannelPathLength,
                                           Enabled,
                                           RetentionMode,
                                           MaximumSize,
                                           ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                           ZpNative_StatusCallback,
                                           CallbackContext,
                                           &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateRegistryKeysPage(
    _In_ ULONGLONG ClientId,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _In_ ULONG MaxEntries,
    _In_ ZP_NATIVE_REGISTRY_KEY_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
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
    CallbackContext->Callback.RegistryKeyPage = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateRegistryKeysPage(
            Connection,
            Root,
            Path,
            PathLength,
            Cursor,
            CursorLength,
            MaxEntries,
            ZP_NATIVE_TIMEOUT_MILLISECONDS,
            ZpNative_RegistryKeyPageCallback,
            CallbackContext,
            &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateRegistryValuesPage(
    _In_ ULONGLONG ClientId,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _In_ ULONG MaxEntries,
    _In_ ZP_NATIVE_REGISTRY_VALUE_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
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
    CallbackContext->Callback.RegistryValuePage = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateRegistryValuesPage(
            Connection,
            Root,
            Path,
            PathLength,
            Cursor,
            CursorLength,
            MaxEntries,
            ZP_NATIVE_TIMEOUT_MILLISECONDS,
            ZpNative_RegistryValuePageCallback,
            CallbackContext,
            &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryRegistryValue(
    _In_ ULONGLONG ClientId,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ZP_NATIVE_REGISTRY_VALUE_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
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
    CallbackContext->Callback.RegistryValue = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryRegistryValue(Connection,
                                    Root,
                                    Path,
                                    PathLength,
                                    Name,
                                    NameLength,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_RegistryValueCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryRegistryValueRange(
    _In_ ULONGLONG ClientId,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _In_ ZP_NATIVE_REGISTRY_RANGE_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.RegistryRange = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryRegistryValueRange(Connection,
                                         Root,
                                         Path,
                                         PathLength,
                                         Name,
                                         NameLength,
                                         Offset,
                                         Length,
                                         ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                         ZpNative_RegistryRangeCallback,
                                         CallbackContext,
                                         &Request));
}

NTSTATUS
NTAPI
ZpNative_WriteRegistryValueRange(
    _In_ ULONGLONG ClientId,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONG Offset,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_WriteRegistryValueRange(Connection,
                                         Root,
                                         Path,
                                         PathLength,
                                         Name,
                                         NameLength,
                                         Offset,
                                         Data,
                                         DataLength,
                                         ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                         ZpNative_StatusCallback,
                                         CallbackContext,
                                         &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryRegistrySecurity(
    _In_ ULONGLONG ClientId,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_SECURITY_DESCRIPTOR_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
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
    CallbackContext->Callback.SecurityDescriptor = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryRegistrySecurity(Connection,
                                       Root,
                                       Path,
                                       PathLength,
                                       ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                       ZpNative_SecurityDescriptorCallback,
                                       CallbackContext,
                                       &Request));
}

NTSTATUS
NTAPI
ZpNative_SetRegistrySecurity(
    _In_ ULONGLONG ClientId,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(SddlLength) PCWCH Sddl,
    _In_ ULONG SddlLength,
    _In_ BOOLEAN DaclProtected,
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
    Connection = ZpNative_GetConnection(ClientId);
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
        ZpServer_SetRegistrySecurity(Connection,
                                     Root,
                                     Path,
                                     PathLength,
                                     Sddl,
                                     SddlLength,
                                     DaclProtected,
                                     ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                     ZpNative_StatusCallback,
                                     CallbackContext,
                                     &Request));
}

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ExecuteRegistryStatus(
    _In_ ULONGLONG ClientId,
    _In_ BYTE OperationId,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_reads_opt_(NewNameLength) PCWCH NewName,
    _In_ ULONG NewNameLength,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
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
    Connection = ZpNative_GetConnection(ClientId);
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
    switch (OperationId)
    {
    case ZP_REGISTRY_OPERATION_SET_VALUE:
        Status = ZpServer_SetRegistryValue(Connection,
                                           Root,
                                           Path,
                                           PathLength,
                                           Name,
                                           NameLength,
                                           Type,
                                           Data,
                                           DataLength,
                                           ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                           ZpNative_StatusCallback,
                                           CallbackContext,
                                           &Request);
        break;
    case ZP_REGISTRY_OPERATION_DELETE_VALUE:
        Status = ZpServer_DeleteRegistryValue(Connection,
                                              Root,
                                              Path,
                                              PathLength,
                                              Name,
                                              NameLength,
                                              ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                              ZpNative_StatusCallback,
                                              CallbackContext,
                                              &Request);
        break;
    case ZP_REGISTRY_OPERATION_CREATE_KEY:
        Status = ZpServer_CreateRegistryKey(Connection,
                                            Root,
                                            Path,
                                            PathLength,
                                            ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                            ZpNative_StatusCallback,
                                            CallbackContext,
                                            &Request);
        break;
    case ZP_REGISTRY_OPERATION_DELETE_KEY:
        Status = ZpServer_DeleteRegistryKey(Connection,
                                            Root,
                                            Path,
                                            PathLength,
                                            ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                            ZpNative_StatusCallback,
                                            CallbackContext,
                                            &Request);
        break;
    case ZP_REGISTRY_OPERATION_RENAME_KEY:
        Status = ZpServer_RenameRegistryKey(Connection,
                                            Root,
                                            Path,
                                            PathLength,
                                            Name,
                                            NameLength,
                                            NewName,
                                            NewNameLength,
                                            ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                            ZpNative_StatusCallback,
                                            CallbackContext,
                                            &Request);
        break;
    case ZP_REGISTRY_OPERATION_RENAME_VALUE:
        Status = ZpServer_RenameRegistryValue(Connection,
                                              Root,
                                              Path,
                                              PathLength,
                                              Name,
                                              NameLength,
                                              NewName,
                                              NewNameLength,
                                              ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                              ZpNative_StatusCallback,
                                              CallbackContext,
                                              &Request);
        break;
    default:
        Status = STATUS_NOT_SUPPORTED;
        break;
    }
    return ZpNative_SendStatusRequest(CallbackContext, Status);
}
