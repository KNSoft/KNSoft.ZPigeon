NTSTATUS
NTAPI
ZpNative_EnumerateWindows(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_WINDOW_LIST_CALLBACK Callback,
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
    CallbackContext->Callback.WindowList = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateWindows(Connection,
                                  ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                  ZpNative_WindowListCallback,
                                  CallbackContext,
                                  &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateMonitors(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_WINDOW_MONITORS_CALLBACK Callback,
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
    CallbackContext->Callback.WindowMonitors = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateMonitors(Connection,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_WindowMonitorsCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryWindow(
    _In_ ULONGLONG ClientId,
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ZP_NATIVE_WINDOW_INFO_CALLBACK Callback,
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
    CallbackContext->Callback.WindowInfo = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryWindow(Connection,
                             Handle,
                             ProcessId,
                             ThreadId,
                             ZP_NATIVE_TIMEOUT_MILLISECONDS,
                             ZpNative_WindowInfoCallback,
                             CallbackContext,
                             &Request));
}

NTSTATUS
NTAPI
ZpNative_ControlWindow(
    _In_ ULONGLONG ClientId,
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ZP_WINDOW_CONTROL Control,
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
        ZpServer_ControlWindow(Connection,
                               Handle,
                               ProcessId,
                               ThreadId,
                               Control,
                               ZP_NATIVE_TIMEOUT_MILLISECONDS,
                               ZpNative_StatusCallback,
                               CallbackContext,
                               &Request));
}

NTSTATUS
NTAPI
ZpNative_UpdateWindow(
    _In_ ULONGLONG ClientId,
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ULONG Fields,
    _In_reads_opt_(CaptionLength) PCWCH Caption,
    _In_ ULONG CaptionLength,
    _In_ LONG Left,
    _In_ LONG Top,
    _In_ LONG Right,
    _In_ LONG Bottom,
    _In_ ULONG Style,
    _In_ ULONG ExStyle,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_WINDOW_UPDATE Update = {
        Handle,
        ProcessId,
        ThreadId,
        Fields,
        Caption,
        CaptionLength,
        Left,
        Top,
        Right,
        Bottom,
        Style,
        ExStyle
    };
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
        ZpServer_UpdateWindow(Connection,
                              &Update,
                              ZP_NATIVE_TIMEOUT_MILLISECONDS,
                              ZpNative_StatusCallback,
                              CallbackContext,
                              &Request));
}

NTSTATUS
NTAPI
ZpNative_CaptureWindow(
    _In_ ULONGLONG ClientId,
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ULONG Flags,
    _In_ ULONG MaxDimension,
    _In_ BYTE FrameRate,
    _In_ BYTE Quality,
    _In_ ULONG MonitorIndex,
    _In_ ZP_NATIVE_WINDOW_CAPTURE_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_WINDOW_CAPTURE_OPTIONS Options = {
        Handle,
        ProcessId,
        ThreadId,
        Flags,
        MaxDimension,
        FrameRate,
        Quality,
        0,
        MonitorIndex
    };
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
    CallbackContext->Callback.WindowCapture = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_CaptureWindow(Connection,
                               &Options,
                               ZP_NATIVE_TIMEOUT_MILLISECONDS,
                               ZpNative_WindowCaptureCallback,
                               CallbackContext,
                               &Request));
}

NTSTATUS
NTAPI
ZpNative_OpenWindowCapture(
    _In_ ULONGLONG ClientId,
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ULONG Flags,
    _In_ ULONG MaxDimension,
    _In_ BYTE FrameRate,
    _In_ BYTE Quality,
    _In_ ULONG DirectStreamId,
    _In_ ULONG MonitorIndex,
    _In_ BYTE Encoding,
    _In_ ZP_NATIVE_WINDOW_CAPTURE_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_WINDOW_CAPTURE_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_WINDOW_CAPTURE_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    ZP_WINDOW_CAPTURE_OPTIONS Options = {
        Handle,
        ProcessId,
        ThreadId,
        Flags,
        MaxDimension,
        FrameRate,
        Quality,
        DirectStreamId,
        MonitorIndex
    };
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_WINDOW_CAPTURE_STREAM Stream;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    Options.Encoding.Mode = Encoding & 3;
    Options.Encoding.Codec = (Encoding >> 2) & 1;
    Options.Encoding.Reserved = Encoding >> 3;

    if (OpenCallback == NULL || DataCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    Stream = Mem_Alloc(sizeof(*Stream));
    if (Stream == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Stream, sizeof(*Stream));
    Stream->Connection = Connection;
    Stream->OpenCallback = OpenCallback;
    Stream->DataCallback = DataCallback;
    Stream->CloseCallback = CloseCallback;
    Stream->Context = Context;
    Status = ZpServer_OpenWindowCapture(Connection,
                                        &Options,
                                        ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                        ZpNative_WindowCaptureOpenCallback,
                                        ZpNative_WindowCaptureDataCallback,
                                        ZpNative_WindowCaptureCloseCallback,
                                        Stream,
                                        &Request);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Release(Connection);
        Mem_Free(Stream);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_CloseWindowCapture(
    _In_ ZP_NATIVE_WINDOW_CAPTURE_STREAM_HANDLE Stream)
{
    ZP_CHANNEL_HANDLE Channel;
    NTSTATUS Status;

    if (Stream == NULL) return STATUS_INVALID_HANDLE;
    if (InterlockedExchange(&Stream->CallerClosed, TRUE)) return STATUS_INVALID_DEVICE_STATE;
    RtlAcquireSRWLockExclusive(&Stream->Lock);
    Channel = Stream->Channel;
    Stream->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Stream->Lock);
    Status = Channel != NULL ? ZpChannel_Cancel(Channel) : STATUS_SUCCESS;
    ZpNative_ReleaseWindowCapture(Stream);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_SendWindowCaptureInput(
    _In_ ZP_NATIVE_WINDOW_CAPTURE_STREAM_HANDLE Stream,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    NTSTATUS Status;

    if (Stream == NULL) return STATUS_INVALID_HANDLE;
    RtlAcquireSRWLockShared(&Stream->Lock);
    Status = Stream->Channel != NULL ?
                 ZpChannel_Send(Stream->Channel, Data, DataLength) :
                 STATUS_INVALID_DEVICE_STATE;
    RtlReleaseSRWLockShared(&Stream->Lock);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_EnumerateAudioDevices(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_AUDIO_DEVICES_CALLBACK Callback,
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
    CallbackContext->Callback.AudioDevices = Callback;
    return ZpNative_SendStatusRequest(CallbackContext,
                                      ZpServer_EnumerateAudioDevices(Connection,
                                                                     ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                                                     ZpNative_AudioDevicesCallback,
                                                                     CallbackContext,
                                                                     &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateAudioSessions(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_AUDIO_SESSIONS_CALLBACK Callback,
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
    CallbackContext->Callback.AudioSessions = Callback;
    return ZpNative_SendStatusRequest(CallbackContext,
                                      ZpServer_EnumerateAudioSessions(Connection,
                                                                      ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                                                      ZpNative_AudioSessionsCallback,
                                                                      CallbackContext,
                                                                      &Request));
}

NTSTATUS
NTAPI
ZpNative_ControlAudioEndpoint(
    _In_ ULONGLONG ClientId,
    _In_ BYTE Flow,
    _In_ BYTE Control,
    _In_ ULONG Value,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
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
    return ZpNative_SendStatusRequest(CallbackContext,
                                      ZpServer_ControlAudioEndpoint(Connection,
                                                                    Flow,
                                                                    Control,
                                                                    Value,
                                                                    DeviceId,
                                                                    DeviceIdLength,
                                                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                                                    ZpNative_StatusCallback,
                                                                    CallbackContext,
                                                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_ControlAudioSession(
    _In_ ULONGLONG ClientId,
    _In_ BYTE Control,
    _In_ ULONG Value,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(SessionIdLength) PCWCH SessionId,
    _In_ ULONG SessionIdLength,
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
    return ZpNative_SendStatusRequest(CallbackContext,
                                      ZpServer_ControlAudioSession(Connection,
                                                                   Control,
                                                                   Value,
                                                                   DeviceId,
                                                                   DeviceIdLength,
                                                                   SessionId,
                                                                   SessionIdLength,
                                                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                                                   ZpNative_StatusCallback,
                                                                   CallbackContext,
                                                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_OpenAudioStream(
    _In_ ULONGLONG ClientId,
    _In_ BYTE Flow,
    _In_ ULONG DirectStreamId,
    _In_reads_opt_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ZP_NATIVE_AUDIO_STREAM_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_AUDIO_STREAM_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_AUDIO_STREAM_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_AUDIO_STREAM Stream;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (OpenCallback == NULL || DataCallback == NULL || CloseCallback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    Stream = Mem_Alloc(sizeof(*Stream));
    if (Stream == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Stream, sizeof(*Stream));
    Stream->Connection = Connection;
    Stream->OpenCallback = OpenCallback;
    Stream->DataCallback = DataCallback;
    Stream->CloseCallback = CloseCallback;
    Stream->Context = Context;
    Status = ZpServer_OpenAudioStream(Connection,
                                      Flow,
                                      DirectStreamId,
                                      DeviceId,
                                      DeviceIdLength,
                                      ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                      ZpNative_AudioStreamOpenCallback,
                                      ZpNative_AudioStreamDataCallback,
                                      ZpNative_AudioStreamCloseCallback,
                                      Stream,
                                      &Request);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Release(Connection);
        Mem_Free(Stream);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_CloseAudioStream(
    _In_ ZP_NATIVE_AUDIO_STREAM_HANDLE Stream)
{
    ZP_CHANNEL_HANDLE Channel;
    NTSTATUS Status;

    if (Stream == NULL) return STATUS_INVALID_HANDLE;
    if (InterlockedExchange(&Stream->CallerClosed, TRUE)) return STATUS_INVALID_DEVICE_STATE;
    RtlAcquireSRWLockExclusive(&Stream->Lock);
    Channel = Stream->Channel;
    Stream->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Stream->Lock);
    Status = Channel != NULL ? ZpChannel_Cancel(Channel) : STATUS_SUCCESS;
    ZpNative_ReleaseAudioStream(Stream);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_EnumerateVideoDevices(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_VIDEO_DEVICES_CALLBACK Callback,
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
    CallbackContext->Callback.VideoDevices = Callback;
    return ZpNative_SendStatusRequest(CallbackContext,
                                      ZpServer_EnumerateVideoDevices(Connection,
                                                                     ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                                                     ZpNative_VideoDevicesCallback,
                                                                     CallbackContext,
                                                                     &Request));
}

NTSTATUS
NTAPI
ZpNative_OpenVideoStream(
    _In_ ULONGLONG ClientId,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG FrameRateNumerator,
    _In_ ULONG FrameRateDenominator,
    _In_ BYTE Quality,
    _In_ ULONG DirectStreamId,
    _In_ ZP_NATIVE_VIDEO_STREAM_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_VIDEO_STREAM_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_VIDEO_STREAM_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_VIDEO_STREAM Stream;
    ZP_REQUEST_HANDLE Request;
    ZP_VIDEO_FORMAT Format = { Width, Height, FrameRateNumerator, FrameRateDenominator };
    NTSTATUS Status;

    if (OpenCallback == NULL || DataCallback == NULL || CloseCallback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    Stream = Mem_Alloc(sizeof(*Stream));
    if (Stream == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Stream, sizeof(*Stream));
    Stream->Connection = Connection;
    Stream->OpenCallback = OpenCallback;
    Stream->DataCallback = DataCallback;
    Stream->CloseCallback = CloseCallback;
    Stream->Context = Context;
    Status = ZpServer_OpenVideoStream(Connection,
                                      DeviceId,
                                      DeviceIdLength,
                                      &Format,
                                      Quality,
                                      DirectStreamId,
                                      ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                      ZpNative_VideoStreamOpenCallback,
                                      ZpNative_VideoStreamDataCallback,
                                      ZpNative_VideoStreamCloseCallback,
                                      Stream,
                                      &Request);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Release(Connection);
        Mem_Free(Stream);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_UpdateVideoStream(
    _In_ ZP_NATIVE_VIDEO_STREAM_HANDLE Stream,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG FrameRateNumerator,
    _In_ ULONG FrameRateDenominator,
    _In_ BYTE Quality,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_VIDEO_FORMAT Format = { Width, Height, FrameRateNumerator, FrameRateDenominator };
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (Stream == NULL || Callback == NULL) return STATUS_INVALID_PARAMETER;
    RtlAcquireSRWLockShared(&Stream->Lock);
    if (Stream->Channel == NULL)
    {
        RtlReleaseSRWLockShared(&Stream->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    ZpConnection_AddRef(Stream->Connection);
    CallbackContext = ZpNative_CreateCallbackContext(Stream->Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Stream->Connection);
        RtlReleaseSRWLockShared(&Stream->Lock);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    Status = ZpServer_UpdateVideoStream(Stream->Connection,
                                        Stream->Channel,
                                        &Format,
                                        Quality,
                                        ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                        ZpNative_StatusCallback,
                                        CallbackContext,
                                        &Request);
    RtlReleaseSRWLockShared(&Stream->Lock);
    return ZpNative_SendStatusRequest(CallbackContext, Status);
}

NTSTATUS
NTAPI
ZpNative_CloseVideoStream(
    _In_ ZP_NATIVE_VIDEO_STREAM_HANDLE Stream)
{
    ZP_CHANNEL_HANDLE Channel;
    NTSTATUS Status;

    if (Stream == NULL) return STATUS_INVALID_HANDLE;
    if (InterlockedExchange(&Stream->CallerClosed, TRUE)) return STATUS_INVALID_DEVICE_STATE;
    RtlAcquireSRWLockExclusive(&Stream->Lock);
    Channel = Stream->Channel;
    Stream->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Stream->Lock);
    Status = Channel != NULL ? ZpChannel_Cancel(Channel) : STATUS_SUCCESS;
    ZpNative_ReleaseVideoStream(Stream);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_OpenRtc(
    _In_ ULONGLONG ClientId,
    _In_reads_(ZP_RTC_SESSION_ID_SIZE) const BYTE* SessionId,
    _In_reads_(OfferLength) PCWCH Offer,
    _In_ ULONG OfferLength,
    _In_reads_opt_(IceServersLength) PCWCH IceServers,
    _In_ ULONG IceServersLength,
    _In_ ZP_NATIVE_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_RTC_ICE_SERVER Servers[ZP_RTC_MAX_ICE_SERVERS];
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;
    ULONG Index, Start = 0, Count = 0;

    if (Callback == NULL || SessionId == NULL || Offer == NULL ||
        (IceServersLength != 0 && IceServers == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index <= IceServersLength; Index++)
    {
        if (Index != IceServersLength && IceServers[Index] != L'\n') continue;
        if (Index != Start)
        {
            if (Count == ARRAYSIZE(Servers)) return STATUS_INVALID_PARAMETER;
            Servers[Count].Url = IceServers + Start;
            Servers[Count].UrlLength = Index - Start;
            Count++;
        }
        Start = Index + 1;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.String = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_OpenRtc(Connection,
                         SessionId,
                         Offer,
                         OfferLength,
                         Servers,
                         Count,
                         ZP_NATIVE_LONG_OPERATION_TIMEOUT_MILLISECONDS,
                         ZpNative_StringCallback,
                         CallbackContext,
                         &Request));
}

NTSTATUS
NTAPI
ZpNative_CloseRtc(
    _In_ ULONGLONG ClientId,
    _In_reads_(ZP_RTC_SESSION_ID_SIZE) const BYTE* SessionId,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL || SessionId == NULL) return STATUS_INVALID_PARAMETER;
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
        ZpServer_CloseRtc(Connection,
                          SessionId,
                          ZP_NATIVE_TIMEOUT_MILLISECONDS,
                          ZpNative_StatusCallback,
                          CallbackContext,
                          &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateServices(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_SERVICE_LIST_CALLBACK Callback,
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
    CallbackContext->Callback.ServiceList = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateServices(Connection,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_ServiceListCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryService(
    _In_ ULONGLONG ClientId,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ZP_NATIVE_SERVICE_INFO_CALLBACK Callback,
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
    CallbackContext->Callback.ServiceInfo = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryService(Connection,
                              ServiceName,
                              ServiceNameLength,
                              ZP_NATIVE_TIMEOUT_MILLISECONDS,
                              ZpNative_ServiceInfoCallback,
                              CallbackContext,
                              &Request));
}

NTSTATUS
NTAPI
ZpNative_ControlService(
    _In_ ULONGLONG ClientId,
    _In_ ZP_SERVICE_CONTROL Control,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_reads_opt_(ArgumentLength) PCWCH Argument,
    _In_ ULONG ArgumentLength,
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
        ZpServer_ControlService(Connection,
                                Control,
                                ServiceName,
                                ServiceNameLength,
                                Argument,
                                ArgumentLength,
                                ZP_NATIVE_SERVICE_CONTROL_TIMEOUT_MILLISECONDS,
                                ZpNative_StatusCallback,
                                CallbackContext,
                                &Request));
}

NTSTATUS
NTAPI
ZpNative_ConfigureService(
    _In_ ULONGLONG ClientId,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG StartType,
    _In_ BOOLEAN DelayedAutoStart,
    _In_reads_(DisplayNameLength) PCWCH DisplayName,
    _In_ ULONG DisplayNameLength,
    _In_reads_opt_(DescriptionLength) PCWCH Description,
    _In_ ULONG DescriptionLength,
    _In_reads_(BinaryPathNameLength) PCWCH BinaryPathName,
    _In_ ULONG BinaryPathNameLength,
    _In_reads_opt_(LoadOrderGroupLength) PCWCH LoadOrderGroup,
    _In_ ULONG LoadOrderGroupLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;
    ZP_SERVICE_CONFIG Config;

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
    Config.StartType = StartType;
    Config.DelayedAutoStart = DelayedAutoStart;
    Config.ServiceName = ServiceName;
    Config.ServiceNameLength = ServiceNameLength;
    Config.DisplayName = DisplayName;
    Config.DisplayNameLength = DisplayNameLength;
    Config.Description = Description;
    Config.DescriptionLength = DescriptionLength;
    Config.BinaryPathName = BinaryPathName;
    Config.BinaryPathNameLength = BinaryPathNameLength;
    Config.LoadOrderGroup = LoadOrderGroup;
    Config.LoadOrderGroupLength = LoadOrderGroupLength;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ConfigureService(Connection,
                                  &Config,
                                  ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                  ZpNative_StatusCallback,
                                  CallbackContext,
                                  &Request));
}

NTSTATUS
NTAPI
ZpNative_ConfigureServiceRecovery(
    _In_ ULONGLONG ClientId,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG ErrorControl,
    _In_ BOOLEAN FailureActionsOnNonCrashFailures,
    _In_ ULONG ResetPeriodSeconds,
    _In_ ULONG RestartDelayMilliseconds,
    _In_ ULONG RebootDelayMilliseconds,
    _In_ ULONG FirstFailureAction,
    _In_ ULONG SecondFailureAction,
    _In_ ULONG ThirdFailureAction,
    _In_ ULONG SubsequentFailureAction,
    _In_reads_opt_(RebootMessageLength) PCWCH RebootMessage,
    _In_ ULONG RebootMessageLength,
    _In_reads_opt_(CommandLength) PCWCH Command,
    _In_ ULONG CommandLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;
    ZP_SERVICE_RECOVERY_CONFIG Config;

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
    Config.ErrorControl = ErrorControl;
    Config.FailureActionsOnNonCrashFailures = FailureActionsOnNonCrashFailures;
    Config.ResetPeriodSeconds = ResetPeriodSeconds;
    Config.RestartDelayMilliseconds = RestartDelayMilliseconds;
    Config.RebootDelayMilliseconds = RebootDelayMilliseconds;
    Config.FirstFailureAction = FirstFailureAction;
    Config.SecondFailureAction = SecondFailureAction;
    Config.ThirdFailureAction = ThirdFailureAction;
    Config.SubsequentFailureAction = SubsequentFailureAction;
    Config.ServiceName = ServiceName;
    Config.ServiceNameLength = ServiceNameLength;
    Config.RebootMessage = RebootMessage;
    Config.RebootMessageLength = RebootMessageLength;
    Config.Command = Command;
    Config.CommandLength = CommandLength;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ConfigureServiceRecovery(Connection,
                                          &Config,
                                          ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                          ZpNative_StatusCallback,
                                          CallbackContext,
                                          &Request));
}

NTSTATUS
NTAPI
ZpNative_ConfigureServiceAccount(
    _In_ ULONGLONG ClientId,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_reads_(StartNameLength) PCWCH StartName,
    _In_ ULONG StartNameLength,
    _In_reads_opt_(PasswordLength) PCWCH Password,
    _In_ ULONG PasswordLength,
    _In_ BOOLEAN PasswordPresent,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;
    ZP_SERVICE_ACCOUNT_CONFIG Config;

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
    Config.PasswordPresent = PasswordPresent;
    Config.ServiceName = ServiceName;
    Config.ServiceNameLength = ServiceNameLength;
    Config.StartName = StartName;
    Config.StartNameLength = StartNameLength;
    Config.Password = Password;
    Config.PasswordLength = PasswordLength;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ConfigureServiceAccount(Connection,
                                         &Config,
                                         ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                         ZpNative_StatusCallback,
                                         CallbackContext,
                                         &Request));
}
