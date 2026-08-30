#include "Bridge.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#define ZP_NATIVE_TIMEOUT_MILLISECONDS 10000
#define ZP_NATIVE_SERVICE_CONTROL_TIMEOUT_MILLISECONDS 35000
#define ZP_NATIVE_LONG_OPERATION_TIMEOUT_MILLISECONDS 300000
#define ZP_NATIVE_PROCESS_DUMP_TIMEOUT_MILLISECONDS 600000
#define ZP_NATIVE_WMI_TIMEOUT_MILLISECONDS 60000

typedef struct _ZP_NATIVE_CALLBACK_CONTEXT
{
    LIST_ENTRY OperationEntry;
    volatile LONG ReferenceCount;
    LOGICAL Active;
    ZP_CONNECTION_HANDLE Connection;
    ZP_REQUEST_HANDLE Request;
    union
    {
        ZP_NATIVE_SYSTEM_INFO_CALLBACK SystemInfo;
        ZP_NATIVE_STATUS_CALLBACK Status;
        ZP_NATIVE_FILE_PAGE_CALLBACK FilePage;
        ZP_NATIVE_FILE_INFO_CALLBACK FileInfo;
        ZP_NATIVE_FILE_HASH_CALLBACK FileHash;
        ZP_NATIVE_FILE_PREVIEW_CALLBACK FilePreview;
        ZP_NATIVE_FILE_VOLUME_CALLBACK FileVolume;
        ZP_NATIVE_FILE_OWNERS_CALLBACK FileOwners;
        ZP_NATIVE_FILE_OWNER_CONTROL_CALLBACK FileOwnerControl;
        ZP_NATIVE_FILE_DOWNLOADS_CALLBACK FileDownloads;
        ZP_NATIVE_PORTABLE_DEVICES_CALLBACK PortableDevices;
        ZP_NATIVE_PORTABLE_OBJECTS_CALLBACK PortableObjects;
        ZP_NATIVE_SECURITY_DESCRIPTOR_CALLBACK SecurityDescriptor;
        ZP_NATIVE_STRING_CALLBACK String;
        ZP_NATIVE_PROCESS_LIST_CALLBACK ProcessList;
        ZP_NATIVE_PROCESS_INFO_CALLBACK ProcessInfo;
        ZP_NATIVE_PROCESS_MODULES_CALLBACK ProcessModules;
        ZP_NATIVE_PROCESS_HANDLES_CALLBACK ProcessHandles;
        ZP_NATIVE_PROCESS_DUMP_CALLBACK ProcessDump;
        ZP_NATIVE_PROCESS_MEMORY_CALLBACK ProcessMemory;
        ZP_NATIVE_PROCESS_MEMORY_ALLOCATIONS_CALLBACK ProcessMemoryAllocations;
        ZP_NATIVE_PROCESS_MEMORY_REGIONS_CALLBACK ProcessMemoryRegions;
        ZP_NATIVE_EXECUTION_SESSIONS_CALLBACK ExecutionSessions;
        ZP_NATIVE_EXECUTION_ENVIRONMENT_CALLBACK ExecutionEnvironment;
        ZP_NATIVE_EXECUTION_IMAGE_CALLBACK ExecutionImage;
        ZP_NATIVE_EXECUTION_JOBS_CALLBACK ExecutionJobs;
        ZP_NATIVE_EXECUTION_STAGING_CALLBACK ExecutionStaging;
        ZP_NATIVE_WINDOW_LIST_CALLBACK WindowList;
        ZP_NATIVE_WINDOW_MONITORS_CALLBACK WindowMonitors;
        ZP_NATIVE_WINDOW_INFO_CALLBACK WindowInfo;
        ZP_NATIVE_WINDOW_CAPTURE_CALLBACK WindowCapture;
        ZP_NATIVE_AUDIO_DEVICES_CALLBACK AudioDevices;
        ZP_NATIVE_AUDIO_SESSIONS_CALLBACK AudioSessions;
        ZP_NATIVE_VIDEO_DEVICES_CALLBACK VideoDevices;
        ZP_NATIVE_SERIAL_PORTS_CALLBACK SerialPorts;
        ZP_NATIVE_RECORDING_CAPABILITIES_CALLBACK RecordingCapabilities;
        ZP_NATIVE_RECORDING_RECORDS_CALLBACK RecordingRecords;
        ZP_NATIVE_SERVICE_LIST_CALLBACK ServiceList;
        ZP_NATIVE_SERVICE_INFO_CALLBACK ServiceInfo;
        ZP_NATIVE_ADMINISTRATION_CALLBACK Administration;
        ZP_NATIVE_ADMINISTRATION_DATA_CALLBACK AdministrationData;
        ZP_NATIVE_BROWSER_CALLBACK Browser;
        ZP_NATIVE_BROWSER_PROFILE_INSPECTION_CALLBACK BrowserProfileInspection;
        ZP_NATIVE_BROWSER_DOCUMENT_CALLBACK BrowserDocument;
        ZP_NATIVE_WMI_CALLBACK Wmi;
        ZP_NATIVE_EVENT_LOG_CALLBACK EventLog;
        ZP_NATIVE_EVENT_LOG_CHANNELS_CALLBACK EventLogChannels;
        ZP_NATIVE_EVENT_LOG_CHANNEL_INFO_CALLBACK EventLogChannelInfo;
        ZP_NATIVE_REGISTRY_KEY_PAGE_CALLBACK RegistryKeyPage;
        ZP_NATIVE_REGISTRY_VALUE_PAGE_CALLBACK RegistryValuePage;
        ZP_NATIVE_REGISTRY_VALUE_CALLBACK RegistryValue;
        ZP_NATIVE_REGISTRY_RANGE_CALLBACK RegistryRange;
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

typedef struct _ZP_NATIVE_FILE_TRANSFER
{
    RTL_SRWLOCK Lock;
    volatile LONG ReferenceCount;
    volatile LONG CallerClosed;
    ZP_CONNECTION_HANDLE Connection;
    ZP_CHANNEL_HANDLE Channel;
    ZP_NATIVE_FILE_OPEN_CALLBACK OpenCallback;
    ZP_NATIVE_FILE_DATA_CALLBACK DataCallback;
    ZP_NATIVE_FILE_WRITABLE_CALLBACK WritableCallback;
    ZP_NATIVE_FILE_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_NATIVE_FILE_TRANSFER, *PZP_NATIVE_FILE_TRANSFER;

typedef struct _ZP_NATIVE_TUNNEL
{
    RTL_SRWLOCK Lock;
    volatile LONG ReferenceCount;
    volatile LONG CallerClosed;
    ZP_CONNECTION_HANDLE Connection;
    ZP_CHANNEL_HANDLE Channel;
    ZP_NATIVE_TUNNEL_OPEN_CALLBACK OpenCallback;
    ZP_NATIVE_TUNNEL_DATA_CALLBACK DataCallback;
    ZP_NATIVE_TUNNEL_WRITABLE_CALLBACK WritableCallback;
    ZP_NATIVE_TUNNEL_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_NATIVE_TUNNEL, *PZP_NATIVE_TUNNEL;

typedef struct _ZP_NATIVE_WINDOW_CAPTURE_STREAM
{
    RTL_SRWLOCK Lock;
    volatile LONG ReferenceCount;
    volatile LONG CallerClosed;
    ZP_CONNECTION_HANDLE Connection;
    ZP_CHANNEL_HANDLE Channel;
    ZP_NATIVE_WINDOW_CAPTURE_OPEN_CALLBACK OpenCallback;
    ZP_NATIVE_WINDOW_CAPTURE_DATA_CALLBACK DataCallback;
    ZP_NATIVE_WINDOW_CAPTURE_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_NATIVE_WINDOW_CAPTURE_STREAM, *PZP_NATIVE_WINDOW_CAPTURE_STREAM;

typedef struct _ZP_NATIVE_AUDIO_STREAM
{
    RTL_SRWLOCK Lock;
    volatile LONG ReferenceCount;
    volatile LONG CallerClosed;
    ZP_CONNECTION_HANDLE Connection;
    ZP_CHANNEL_HANDLE Channel;
    ZP_NATIVE_AUDIO_STREAM_OPEN_CALLBACK OpenCallback;
    ZP_NATIVE_AUDIO_STREAM_DATA_CALLBACK DataCallback;
    ZP_NATIVE_AUDIO_STREAM_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_NATIVE_AUDIO_STREAM, *PZP_NATIVE_AUDIO_STREAM;

typedef struct _ZP_NATIVE_VIDEO_STREAM
{
    RTL_SRWLOCK Lock;
    volatile LONG ReferenceCount;
    volatile LONG CallerClosed;
    ZP_CONNECTION_HANDLE Connection;
    ZP_CHANNEL_HANDLE Channel;
    ZP_NATIVE_VIDEO_STREAM_OPEN_CALLBACK OpenCallback;
    ZP_NATIVE_VIDEO_STREAM_DATA_CALLBACK DataCallback;
    ZP_NATIVE_VIDEO_STREAM_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_NATIVE_VIDEO_STREAM, *PZP_NATIVE_VIDEO_STREAM;

typedef struct _ZP_NATIVE_CLIENT_ENTRY
{
    LIST_ENTRY ListEntry;
    LIST_ENTRY IdHashEntry;
    LIST_ENTRY ConnectionHashEntry;
    // Process-local routing key; the registry owns one Connection reference.
    ULONGLONG ClientId;
    ZP_CONNECTION_HANDLE Connection;
    BYTE PublicKey[ZP_CLIENT_PUBLIC_KEY_SIZE];
} ZP_NATIVE_CLIENT_ENTRY, *PZP_NATIVE_CLIENT_ENTRY;

static RTL_SRWLOCK ZpNativeLock = RTL_SRWLOCK_INIT;
static ZP_SERVER_HANDLE ZpNativeServer;
static LIST_ENTRY ZpNativeClients = { &ZpNativeClients, &ZpNativeClients };
static LIST_ENTRY ZpNativeOperations = { &ZpNativeOperations, &ZpNativeOperations };
#define ZP_NATIVE_CLIENT_BUCKET_COUNT 64
static LIST_ENTRY ZpNativeClientIdBuckets[ZP_NATIVE_CLIENT_BUCKET_COUNT];
static LIST_ENTRY ZpNativeClientConnectionBuckets[ZP_NATIVE_CLIENT_BUCKET_COUNT];
static volatile LONG64 ZpNativeNextClientId;
static HANDLE ZpNativeStateEvent;
static HANDLE ZpNativeClientChangeEvent;
static ZP_SERVER_STATE ZpNativeState = ZpServerStateStopped;
static ZP_STATUS ZpNativeStateStatus;

static
ULONG
ZpNative_ClientIdBucket(
    _In_ ULONGLONG ClientId)
{
    return (ULONG)(ClientId & (ZP_NATIVE_CLIENT_BUCKET_COUNT - 1));
}

static
ULONG
ZpNative_ClientConnectionBucket(
    _In_ ZP_CONNECTION_HANDLE Connection)
{
    return (ULONG)(((ULONG_PTR)Connection >> 4) & (ZP_NATIVE_CLIENT_BUCKET_COUNT - 1));
}

static
NTSTATUS
ZpNative_CompleteRequestStart(
    _In_ PZP_NATIVE_CALLBACK_CONTEXT CallbackContext,
    _In_ NTSTATUS Status,
    _When_(NT_SUCCESS(Status), _In_) ZP_REQUEST_HANDLE* Request);

#define ZpNative_SendStatusRequest(CallbackContext, Status) \
    ZpNative_CompleteRequestStart((CallbackContext), (Status), &Request)

#include "Bridge.Callbacks.inl"
ZP_STATUS
NTAPI
ZpNative_Start(
    _In_ PCCERT_CONTEXT Certificate,
    _In_ USHORT Port)
{
    ZP_LISTENER_ENDPOINT Listener = {
        ZpTransportQuic,
        L"127.0.0.1",
        Port
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
    for (ULONG Index = 0; Index < ZP_NATIVE_CLIENT_BUCKET_COUNT; Index++)
    {
        InitializeListHead(&ZpNativeClientIdBuckets[Index]);
        InitializeListHead(&ZpNativeClientConnectionBuckets[Index]);
    }
    RtlReleaseSRWLockExclusive(&ZpNativeLock);
    ZpNativeStateEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (ZpNativeStateEvent == NULL)
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    ZpNativeClientChangeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (ZpNativeClientChangeEvent == NULL)
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        CloseHandle(ZpNativeStateEvent);
        ZpNativeStateEvent = NULL;
        return Status;
    }
    Config.Listeners = &Listener;
    Config.ListenerCount = 1;
    Config.Deployments = &Deployment;
    Config.DeploymentCount = 1;
    Config.Modules = ZpBuiltinModules;
    Config.ModuleCount = ZP_BUILTIN_MODULE_COUNT;
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
    PZP_NATIVE_CLIENT_ENTRY Client;
    PLIST_ENTRY Entry;
    NTSTATUS Status = STATUS_SUCCESS, CloseStatus;
    ULONGLONG CloseStartTickCount;

    RtlAcquireSRWLockExclusive(&ZpNativeLock);
    Server = ZpNativeServer;
    ZpNativeServer = NULL;
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
        CloseStartTickCount = GetTickCount64();
        do
        {
            CloseStatus = ZpServer_Close(Server);
            if (CloseStatus != STATUS_DEVICE_BUSY) break;
            SwitchToThread();
        } while (GetTickCount64() - CloseStartTickCount <
                 ZP_NATIVE_TIMEOUT_MILLISECONDS);
        if (NT_SUCCESS(Status))
        {
            Status = CloseStatus;
        }
    }
    for (;;)
    {
        RtlAcquireSRWLockExclusive(&ZpNativeLock);
        if (IsListEmpty(&ZpNativeClients))
        {
            RtlReleaseSRWLockExclusive(&ZpNativeLock);
            break;
        }
        Entry = RemoveHeadList(&ZpNativeClients);
        Client = CONTAINING_RECORD(Entry, ZP_NATIVE_CLIENT_ENTRY, ListEntry);
        RemoveEntryList(&Client->IdHashEntry);
        RemoveEntryList(&Client->ConnectionHashEntry);
        RtlReleaseSRWLockExclusive(&ZpNativeLock);
        ZpConnection_Release(Client->Connection);
        Mem_Free(Client);
        SetEvent(ZpNativeClientChangeEvent);
    }
    if (ZpNativeStateEvent != NULL)
    {
        CloseHandle(ZpNativeStateEvent);
        ZpNativeStateEvent = NULL;
    }
    if (ZpNativeClientChangeEvent != NULL)
    {
        CloseHandle(ZpNativeClientChangeEvent);
        ZpNativeClientChangeEvent = NULL;
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

HANDLE
NTAPI
ZpNative_GetClientChangeEvent(VOID)
{
    HANDLE Event;

    RtlAcquireSRWLockShared(&ZpNativeLock);
    Event = ZpNativeClientChangeEvent;
    RtlReleaseSRWLockShared(&ZpNativeLock);
    return Event;
}

NTSTATUS
NTAPI
ZpNative_EnumerateClients(
    _Out_writes_to_opt_(Capacity, *Count) PZP_NATIVE_CLIENT_INFO Clients,
    _In_ ULONG Capacity,
    _Out_ PULONG Count)
{
    PZP_NATIVE_CLIENT_ENTRY Client;
    PLIST_ENTRY Entry;
    ULONG Required = 0;

    if (Count == NULL || (Capacity != 0 && Clients == NULL)) return STATUS_INVALID_PARAMETER;
    RtlAcquireSRWLockShared(&ZpNativeLock);
    for (Entry = ZpNativeClients.Flink;
         Entry != &ZpNativeClients;
         Entry = Entry->Flink)
    {
        Required++;
    }
    *Count = Required;
    if (Clients == NULL)
    {
        RtlReleaseSRWLockShared(&ZpNativeLock);
        return STATUS_SUCCESS;
    }
    if (Capacity < Required)
    {
        RtlReleaseSRWLockShared(&ZpNativeLock);
        return STATUS_BUFFER_TOO_SMALL;
    }
    Required = 0;
    for (Entry = ZpNativeClients.Flink;
         Entry != &ZpNativeClients;
         Entry = Entry->Flink)
    {
        Client = CONTAINING_RECORD(Entry, ZP_NATIVE_CLIENT_ENTRY, ListEntry);
        Clients[Required].ClientId = Client->ClientId;
        RtlCopyMemory(Clients[Required].PublicKey,
                      Client->PublicKey,
                      sizeof(Clients[Required].PublicKey));
        if (!NT_SUCCESS(ZpServer_QueryConnectionRemoteAddress(Client->Connection,
                                                               &Clients[Required].Address)) ||
            !NT_SUCCESS(ZpServer_QueryConnectionStatistics(Client->Connection,
                                                            &Clients[Required].Statistics)))
        {
            RtlReleaseSRWLockShared(&ZpNativeLock);
            return STATUS_DEVICE_NOT_CONNECTED;
        }
        Required++;
    }
    RtlReleaseSRWLockShared(&ZpNativeLock);
    return STATUS_SUCCESS;
}

LOGICAL
NTAPI
ZpNative_IsClientConnected(
    _In_ ULONGLONG ClientId)
{
    PZP_NATIVE_CLIENT_ENTRY Client;
    PLIST_ENTRY Entry;
    LOGICAL Connected = FALSE;

    RtlAcquireSRWLockShared(&ZpNativeLock);
    for (Entry = ZpNativeClientIdBuckets[ZpNative_ClientIdBucket(ClientId)].Flink;
         Entry != &ZpNativeClientIdBuckets[ZpNative_ClientIdBucket(ClientId)];
         Entry = Entry->Flink)
    {
        Client = CONTAINING_RECORD(Entry, ZP_NATIVE_CLIENT_ENTRY, IdHashEntry);
        if (Client->ClientId == ClientId)
        {
            Connected = TRUE;
            break;
        }
    }
    RtlReleaseSRWLockShared(&ZpNativeLock);
    return Connected;
}

NTSTATUS
NTAPI
ZpNative_CancelOperation(
    _In_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request = NULL;
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    if (Context == NULL) return STATUS_INVALID_PARAMETER;
    RtlAcquireSRWLockShared(&ZpNativeLock);
    // Managed GCHandle values can be reused after completion; the newest match owns the active handle.
    for (Entry = ZpNativeOperations.Blink;
         Entry != &ZpNativeOperations;
         Entry = Entry->Blink)
    {
        CallbackContext = CONTAINING_RECORD(Entry,
                                            ZP_NATIVE_CALLBACK_CONTEXT,
                                            OperationEntry);
        if (CallbackContext->Context == Context && CallbackContext->Request != NULL)
        {
            Request = CallbackContext->Request;
            ZpRequest_AddRef(Request);
            break;
        }
    }
    RtlReleaseSRWLockShared(&ZpNativeLock);
    if (Request == NULL) return STATUS_NOT_FOUND;
    Status = ZpRequest_Cancel(Request);
    ZpRequest_Close(Request);
    return Status;
}

static
VOID
NTAPI
ZpNative_WindowMonitorsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_WINDOW_MONITOR_LIST_VIEW Monitors,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_WINDOW_MONITOR Records = NULL;
    ZP_WINDOW_MONITOR_VIEW Monitor;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Monitors->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Monitors->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Monitors->Count; Index++)
    {
        DecodeStatus = ZpWindow_GetNextMonitor(Monitors, &Offset, &Monitor);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Index = Monitor.Index;
        Records[Index].Flags = Monitor.Flags;
        Records[Index].Left = Monitor.Left;
        Records[Index].Top = Monitor.Top;
        Records[Index].Right = Monitor.Right;
        Records[Index].Bottom = Monitor.Bottom;
        Records[Index].WorkLeft = Monitor.WorkLeft;
        Records[Index].WorkTop = Monitor.WorkTop;
        Records[Index].WorkRight = Monitor.WorkRight;
        Records[Index].WorkBottom = Monitor.WorkBottom;
        Records[Index].Device = (PCWCH)Monitor.Device.Buffer;
        Records[Index].DeviceLength = Monitor.Device.Length;
    }
    CallbackContext->Callback.WindowMonitors(
        Status,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Monitors->Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

NTSTATUS
NTAPI
ZpNative_QueryConnectionStatistics(
    _In_ ULONGLONG ClientId,
    _Out_ PZP_SERVER_CONNECTION_STATISTICS Statistics)
{
    ZP_CONNECTION_HANDLE Connection = ZpNative_GetConnection(ClientId);
    NTSTATUS Status;

    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    Status = ZpServer_QueryConnectionStatistics(Connection, Statistics);
    ZpConnection_Release(Connection);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_QueryClientAddress(
    _In_ ULONGLONG ClientId,
    _Out_writes_bytes_(16) PBYTE Address,
    _Out_ PULONG AddressLength)
{
    ZP_CONNECTION_HANDLE Connection;
    ZP_IP_ADDRESS RemoteAddress;
    NTSTATUS Status;

    if (Address == NULL || AddressLength == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    Status = ZpServer_QueryConnectionRemoteAddress(Connection, &RemoteAddress);
    ZpConnection_Release(Connection);
    if (!NT_SUCCESS(Status)) return Status;
    if (RemoteAddress.Family == AF_INET)
    {
        RtlCopyMemory(Address, RemoteAddress.Value, sizeof(IN_ADDR));
        *AddressLength = sizeof(IN_ADDR);
        return STATUS_SUCCESS;
    }
    if (RemoteAddress.Family == AF_INET6)
    {
        RtlCopyMemory(Address, RemoteAddress.Value, sizeof(RemoteAddress.Value));
        *AddressLength = sizeof(RemoteAddress.Value);
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_ADDRESS;
}

NTSTATUS
NTAPI
ZpNative_SetConnectionPolicy(
    _In_ ULONGLONG ClientId,
    _In_ ZP_PERFORMANCE_CLASS SpeedClass,
    _In_ ZP_PERFORMANCE_CLASS LatencyClass)
{
    ZP_CONNECTION_HANDLE Connection;
    ZP_CONNECTION_POLICY Policy = { SpeedClass, LatencyClass };
    NTSTATUS Status;

    if (SpeedClass >= ZP_PERFORMANCE_CLASS_COUNT ||
        LatencyClass >= ZP_PERFORMANCE_CLASS_COUNT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    Status = ZpServer_SetConnectionPolicy(Connection, &Policy);
    ZpConnection_Release(Connection);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_ProbeConnection(
    _In_ ULONGLONG ClientId,
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
        ZpServer_ProbeConnection(Connection,
                                 5000,
                                 ZpNative_StatusCallback,
                                 CallbackContext,
                                 &Request));
}

#include "Bridge.StorageProcess.inl"
#include "Bridge.DesktopMedia.inl"
#include "Bridge.Administration.inl"
#include "Bridge.StreamsEvents.inl"
