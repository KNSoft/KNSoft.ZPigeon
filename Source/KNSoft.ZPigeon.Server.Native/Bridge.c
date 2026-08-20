#include "Bridge.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#define ZP_NATIVE_TIMEOUT_MILLISECONDS 10000
#define ZP_NATIVE_SERVICE_CONTROL_TIMEOUT_MILLISECONDS 35000
#define ZP_NATIVE_LONG_OPERATION_TIMEOUT_MILLISECONDS 300000
#define ZP_NATIVE_PROCESS_DUMP_TIMEOUT_MILLISECONDS 600000
#define ZP_NATIVE_WMI_TIMEOUT_MILLISECONDS 60000

typedef struct _ZP_NATIVE_CALLBACK_CONTEXT
{
    ZP_CONNECTION_HANDLE Connection;
    union
    {
        ZP_NATIVE_SYSTEM_INFO_CALLBACK SystemInfo;
        ZP_NATIVE_STATUS_CALLBACK Status;
        ZP_NATIVE_FILE_PAGE_CALLBACK FilePage;
        ZP_NATIVE_FILE_INFO_CALLBACK FileInfo;
        ZP_NATIVE_FILE_HASH_CALLBACK FileHash;
        ZP_NATIVE_FILE_VOLUME_CALLBACK FileVolume;
        ZP_NATIVE_STRING_CALLBACK String;
        ZP_NATIVE_PROCESS_LIST_CALLBACK ProcessList;
        ZP_NATIVE_PROCESS_INFO_CALLBACK ProcessInfo;
        ZP_NATIVE_PROCESS_DUMP_CALLBACK ProcessDump;
        ZP_NATIVE_PROCESS_MEMORY_CALLBACK ProcessMemory;
        ZP_NATIVE_EXECUTION_SESSIONS_CALLBACK ExecutionSessions;
        ZP_NATIVE_EXECUTION_JOBS_CALLBACK ExecutionJobs;
        ZP_NATIVE_EXECUTION_STAGING_CALLBACK ExecutionStaging;
        ZP_NATIVE_WINDOW_LIST_CALLBACK WindowList;
        ZP_NATIVE_WINDOW_INFO_CALLBACK WindowInfo;
        ZP_NATIVE_WINDOW_CAPTURE_CALLBACK WindowCapture;
        ZP_NATIVE_AUDIO_DEVICES_CALLBACK AudioDevices;
        ZP_NATIVE_AUDIO_SESSIONS_CALLBACK AudioSessions;
        ZP_NATIVE_VIDEO_DEVICES_CALLBACK VideoDevices;
        ZP_NATIVE_SERVICE_LIST_CALLBACK ServiceList;
        ZP_NATIVE_SERVICE_INFO_CALLBACK ServiceInfo;
        ZP_NATIVE_ADMINISTRATION_CALLBACK Administration;
        ZP_NATIVE_BROWSER_CALLBACK Browser;
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

static RTL_SRWLOCK ZpNativeLock = RTL_SRWLOCK_INIT;
static ZP_SERVER_HANDLE ZpNativeServer;
static ZP_CONNECTION_HANDLE ZpNativeConnection;
static HANDLE ZpNativeStateEvent;
static ZP_SERVER_STATE ZpNativeState = ZpServerStateStopped;
static ZP_STATUS ZpNativeStateStatus;

static
NTSTATUS
ZpNative_SendStatusRequest(
    _In_ PZP_NATIVE_CALLBACK_CONTEXT CallbackContext,
    _In_ NTSTATUS Status);

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
ZpNative_FilePageCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_FILE_RECORD Records = NULL;
    ZP_FILE_RECORD_VIEW Record;
    ULONG Index;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Page->Files.Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Page->Files.Count * sizeof(*Records));
        if (Records == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
    }
    for (Index = 0;
         ZpStatus_IsSuccess(Status) && Index < Page->Files.Count;
         Index++)
    {
        DecodeStatus = ZpFile_GetRecord(&Page->Files, Index, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Attributes = Record.Info.Attributes;
        Records[Index].Size = Record.Info.Size;
        Records[Index].CreationTime = Record.Info.CreationTime;
        Records[Index].LastAccessTime = Record.Info.LastAccessTime;
        Records[Index].LastWriteTime = Record.Info.LastWriteTime;
        Records[Index].Name = (PCWCH)Record.Name.Buffer;
        Records[Index].NameLength = Record.Name.Length;
        Records[Index].HasChildren = Record.Info.HasChildren;
    }
    CallbackContext->Callback.FilePage(
        Status,
        ZpStatus_IsSuccess(Status) ? Page->EnumerationId : 0,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Page->Files.Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_FileInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_INFO Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.FileInfo(
        Status,
        ZpStatus_IsSuccess(Status) ? Info->Attributes : 0,
        ZpStatus_IsSuccess(Status) ? Info->Size : 0,
        ZpStatus_IsSuccess(Status) ? Info->CreationTime : 0,
        ZpStatus_IsSuccess(Status) ? Info->LastAccessTime : 0,
        ZpStatus_IsSuccess(Status) ? Info->LastWriteTime : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_FileHashCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_HASH_VIEW Hash,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.FileHash(
        Status,
        ZpStatus_IsSuccess(Status) ? Hash->FileSize : 0,
        ZpStatus_IsSuccess(Status) ? Hash->Digest.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Hash->Digest.Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ProcessListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_PROCESS_LIST_VIEW Processes,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_PROCESS_RECORD Records = NULL;
    ZP_PROCESS_RECORD_VIEW Record;
    ULONG Index;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Processes->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Processes->Count * sizeof(*Records));
        if (Records == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
    }
    for (Index = 0;
         ZpStatus_IsSuccess(Status) && Index < Processes->Count;
         Index++)
    {
        DecodeStatus = ZpProcess_GetRecord(Processes, Index, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].ProcessId = Record.ProcessId;
        Records[Index].ParentProcessId = Record.ParentProcessId;
        Records[Index].SessionId = Record.SessionId;
        Records[Index].ThreadCount = Record.ThreadCount;
        Records[Index].HandleCount = Record.HandleCount;
        Records[Index].Flags = Record.Flags;
        Records[Index].MachineType = Record.MachineType;
        Records[Index].PriorityClass = Record.PriorityClass;
        Records[Index].CreateTime = Record.CreateTime;
        Records[Index].UserTime = Record.UserTime;
        Records[Index].KernelTime = Record.KernelTime;
        Records[Index].WorkingSetBytes = Record.WorkingSetBytes;
        Records[Index].PrivateBytes = Record.PrivateBytes;
        Records[Index].ImageName = (PCWCH)Record.ImageName.Buffer;
        Records[Index].ImageNameLength = Record.ImageName.Length;
        Records[Index].UserName = (PCWCH)Record.UserName.Buffer;
        Records[Index].UserNameLength = Record.UserName.Length;
        Records[Index].ImagePath = (PCWCH)Record.ImagePath.Buffer;
        Records[Index].ImagePathLength = Record.ImagePath.Length;
        Records[Index].ServiceNames = (PCWCH)Record.ServiceNames.Buffer;
        Records[Index].ServiceNamesLength = Record.ServiceNames.Length;
    }
    CallbackContext->Callback.ProcessList(
        Status,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Processes->Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ProcessInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_PROCESS_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.ProcessInfo(Status,
                                          ZpStatus_IsSuccess(Status) ? Info : NULL,
                                          CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ProcessDumpCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_STRING_VIEW Path,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.ProcessDump(Status,
                                          ZpStatus_IsSuccess(Status) ? (PCWCH)Path->Buffer : NULL,
                                          ZpStatus_IsSuccess(Status) ? Path->Length : 0,
                                          CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ProcessMemoryCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.ProcessMemory(Status,
                                             ZpStatus_IsSuccess(Status) ? Data->Buffer : NULL,
                                             ZpStatus_IsSuccess(Status) ? Data->Length : 0,
                                             CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_StringCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_STRING_VIEW Value,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.String(Status,
                                     ZpStatus_IsSuccess(Status) ? (PCWCH)Value->Buffer : NULL,
                                     ZpStatus_IsSuccess(Status) ? Value->Length : 0,
                                     CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_FileVolumeCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_FILE_VOLUME_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.FileVolume(
        Status,
        ZpStatus_IsSuccess(Status) ? Info->TotalBytes : 0,
        ZpStatus_IsSuccess(Status) ? Info->FreeBytes : 0,
        ZpStatus_IsSuccess(Status) ? Info->SerialNumber : 0,
        ZpStatus_IsSuccess(Status) ? Info->MaximumComponentLength : 0,
        ZpStatus_IsSuccess(Status) ? Info->FileSystemFlags : 0,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Info->Label.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Info->Label.Length : 0,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Info->FileSystem.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Info->FileSystem.Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ExecutionSessionsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_EXECUTION_SESSION_LIST_VIEW Sessions,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_EXECUTION_SESSION_RECORD Records = NULL;
    ZP_EXECUTION_SESSION_RECORD_VIEW Record;
    ULONG Index;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Sessions->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Sessions->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Sessions->Count; Index++)
    {
        DecodeStatus = ZpExecution_GetSession(Sessions, Index, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].SessionId = Record.SessionId;
        Records[Index].State = Record.State;
        Records[Index].Flags = Record.Flags;
        Records[Index].StationName = (PCWCH)Record.StationName.Buffer;
        Records[Index].StationNameLength = Record.StationName.Length;
        Records[Index].UserName = (PCWCH)Record.UserName.Buffer;
        Records[Index].UserNameLength = Record.UserName.Length;
    }
    CallbackContext->Callback.ExecutionSessions(
        Status,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Sessions->Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ExecutionJobsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_EXECUTION_JOB_LIST_VIEW Jobs,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_EXECUTION_JOB_RECORD Records = NULL;
    ZP_EXECUTION_JOB_RECORD_VIEW Record;
    ULONG Index;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Jobs->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Jobs->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Jobs->Count; Index++)
    {
        DecodeStatus = ZpExecution_GetJob(Jobs, Index, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].JobId = Record.JobId;
        Records[Index].CreateTime = Record.CreateTime;
        Records[Index].ExitTime = Record.ExitTime;
        Records[Index].ProcessId = Record.ProcessId;
        Records[Index].SessionId = Record.SessionId;
        Records[Index].ExitCode = Record.ExitCode;
        Records[Index].Flags = Record.Flags;
        Records[Index].Engine = Record.Engine;
        Records[Index].Identity = Record.Identity;
        Records[Index].State = Record.State;
        Records[Index].FileName = (PCWCH)Record.FileName.Buffer;
        Records[Index].FileNameLength = Record.FileName.Length;
    }
    CallbackContext->Callback.ExecutionJobs(
        Status,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Jobs->Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ExecutionStagingCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_STRING_VIEW Path,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.ExecutionStaging(
        Status,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Path->Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Path->Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_WindowListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_WINDOW_LIST_VIEW Windows,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_WINDOW_RECORD Records = NULL;
    ZP_WINDOW_RECORD_VIEW Record;
    ULONG Index;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Windows->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Windows->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Windows->Count; Index++)
    {
        DecodeStatus = ZpWindow_GetRecord(Windows, Index, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Handle = Record.Handle;
        Records[Index].ParentHandle = Record.ParentHandle;
        Records[Index].ProcessId = Record.ProcessId;
        Records[Index].ThreadId = Record.ThreadId;
        Records[Index].Style = Record.Style;
        Records[Index].ExStyle = Record.ExStyle;
        Records[Index].Flags = Record.Flags;
        Records[Index].Caption = (PCWCH)Record.Caption.Buffer;
        Records[Index].CaptionLength = Record.Caption.Length;
        Records[Index].ClassName = (PCWCH)Record.ClassName.Buffer;
        Records[Index].ClassNameLength = Record.ClassName.Length;
    }
    CallbackContext->Callback.WindowList(
        Status,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Windows->Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_WindowInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_WINDOW_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.WindowInfo(Status,
                                          ZpStatus_IsSuccess(Status) ? Info : NULL,
                                          CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_WindowCaptureCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BUFFER_VIEW Image,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.WindowCapture(
        Status,
        ZpStatus_IsSuccess(Status) ? Image->Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Image->Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
ZpNative_ReleaseWindowCapture(
    _Inout_ PZP_NATIVE_WINDOW_CAPTURE_STREAM Stream)
{
    if (InterlockedDecrement(&Stream->ReferenceCount) == 0)
    {
        ZpConnection_Release(Stream->Connection);
        Mem_Free(Stream);
    }
}

static
VOID
NTAPI
ZpNative_WindowCaptureOpenCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_WINDOW_CAPTURE_STREAM Stream = Context;

    if (ZpStatus_IsSuccess(Status))
    {
        Stream->Channel = Channel;
        Stream->ReferenceCount = 2;
    }
    Stream->OpenCallback(Status,
                         ZpStatus_IsSuccess(Status) ? Stream : NULL,
                         Stream->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Stream->Connection);
        Mem_Free(Stream);
    }
}

static
VOID
NTAPI
ZpNative_WindowCaptureDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_WINDOW_CAPTURE_STREAM Stream = Context;

    if (!Stream->DataCallback(Data->Buffer, Data->Length, Stream->Context))
    {
        ZpChannel_Cancel(Channel);
    }
}

static
VOID
NTAPI
ZpNative_WindowCaptureCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_WINDOW_CAPTURE_STREAM Stream = Context;

    RtlAcquireSRWLockExclusive(&Stream->Lock);
    Stream->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Stream->Lock);
    Stream->CloseCallback(Status, Stream->Context);
    ZpChannel_Close(Channel);
    ZpNative_ReleaseWindowCapture(Stream);
}

static
VOID
NTAPI
ZpNative_AudioDevicesCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_AUDIO_DEVICE_LIST_VIEW Devices,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_AUDIO_DEVICE_RECORD Records = NULL;
    ZP_AUDIO_DEVICE_VIEW Device;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Devices->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Devices->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Devices->Count; Index++)
    {
        DecodeStatus = ZpAudio_GetNextDevice(Devices, &Offset, &Device);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Flow = Device.Flow;
        Records[Index].State = Device.State;
        Records[Index].Flags = Device.Flags;
        Records[Index].Volume = Device.Volume;
        Records[Index].Id = (PCWCH)Device.Id.Buffer;
        Records[Index].IdLength = Device.Id.Length;
        Records[Index].Name = (PCWCH)Device.Name.Buffer;
        Records[Index].NameLength = Device.Name.Length;
    }
    CallbackContext->Callback.AudioDevices(Status,
                                            ZpStatus_IsSuccess(Status) ? Records : NULL,
                                            ZpStatus_IsSuccess(Status) ? Devices->Count : 0,
                                            CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_AudioSessionsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_AUDIO_SESSION_LIST_VIEW Sessions,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_AUDIO_SESSION_RECORD Records = NULL;
    ZP_AUDIO_SESSION_VIEW Session;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Sessions->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Sessions->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Sessions->Count; Index++)
    {
        DecodeStatus = ZpAudio_GetNextSession(Sessions, &Offset, &Session);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].ProcessId = Session.ProcessId;
        Records[Index].State = Session.State;
        Records[Index].Flags = Session.Flags;
        Records[Index].Volume = Session.Volume;
        Records[Index].DeviceId = (PCWCH)Session.DeviceId.Buffer;
        Records[Index].DeviceIdLength = Session.DeviceId.Length;
        Records[Index].Id = (PCWCH)Session.Id.Buffer;
        Records[Index].IdLength = Session.Id.Length;
        Records[Index].Name = (PCWCH)Session.Name.Buffer;
        Records[Index].NameLength = Session.Name.Length;
    }
    CallbackContext->Callback.AudioSessions(Status,
                                             ZpStatus_IsSuccess(Status) ? Records : NULL,
                                             ZpStatus_IsSuccess(Status) ? Sessions->Count : 0,
                                             CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
ZpNative_ReleaseAudioStream(
    _Inout_ PZP_NATIVE_AUDIO_STREAM Stream)
{
    if (InterlockedDecrement(&Stream->ReferenceCount) == 0)
    {
        ZpConnection_Release(Stream->Connection);
        Mem_Free(Stream);
    }
}

static
VOID
NTAPI
ZpNative_AudioStreamOpenCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_AUDIO_STREAM Stream = Context;

    if (ZpStatus_IsSuccess(Status))
    {
        Stream->Channel = Channel;
        Stream->ReferenceCount = 2;
    }
    Stream->OpenCallback(Status, ZpStatus_IsSuccess(Status) ? Stream : NULL, Stream->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Stream->Connection);
        Mem_Free(Stream);
    }
}

static
VOID
NTAPI
ZpNative_AudioStreamDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_AUDIO_STREAM Stream = Context;

    if (!Stream->DataCallback(Data->Buffer, Data->Length, Stream->Context)) ZpChannel_Cancel(Channel);
}

static
VOID
NTAPI
ZpNative_AudioStreamCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_AUDIO_STREAM Stream = Context;

    RtlAcquireSRWLockExclusive(&Stream->Lock);
    Stream->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Stream->Lock);
    Stream->CloseCallback(Status, Stream->Context);
    ZpChannel_Close(Channel);
    ZpNative_ReleaseAudioStream(Stream);
}

static
VOID
NTAPI
ZpNative_VideoDevicesCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_VIDEO_DEVICE_LIST_VIEW Devices,
    _In_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_VIDEO_DEVICE_RECORD Records = NULL;
    ZP_VIDEO_DEVICE_VIEW Device;
    ULONG Count = 0, Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        if (Devices == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
        }
        else
        {
            Count = Devices->Count;
            Records = Count != 0 ? Mem_Alloc((SIZE_T)Count * sizeof(*Records)) : NULL;
            if (Count != 0 && Records == NULL)
            {
                Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            }
            else
            {
                for (Index = 0; Index < Count; Index++)
                {
                    DecodeStatus = ZpVideo_GetNextDevice(Devices, &Offset, &Device);
                    if (!NT_SUCCESS(DecodeStatus))
                    {
                        Status = ZpStatus_FromNtStatus(DecodeStatus);
                        break;
                    }
                    Records[Index].Id = (PCWCH)Device.Id.Buffer;
                    Records[Index].IdLength = Device.Id.Length;
                    Records[Index].Name = (PCWCH)Device.Name.Buffer;
                    Records[Index].NameLength = Device.Name.Length;
                }
            }
        }
    }
    CallbackContext->Callback.VideoDevices(Status,
                                            ZpStatus_IsSuccess(Status) ? Records : NULL,
                                            ZpStatus_IsSuccess(Status) ? Count : 0,
                                            CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
ZpNative_ReleaseVideoStream(
    _In_ PZP_NATIVE_VIDEO_STREAM Stream)
{
    if (InterlockedDecrement(&Stream->ReferenceCount) == 0)
    {
        ZpConnection_Release(Stream->Connection);
        Mem_Free(Stream);
    }
}

static
VOID
NTAPI
ZpNative_VideoStreamOpenCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ PVOID Context)
{
    PZP_NATIVE_VIDEO_STREAM Stream = Context;

    if (ZpStatus_IsSuccess(Status))
    {
        Stream->Channel = Channel;
        Stream->ReferenceCount = 2;
    }
    Stream->OpenCallback(Status, ZpStatus_IsSuccess(Status) ? Stream : NULL, Stream->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Stream->Connection);
        Mem_Free(Stream);
    }
}

static
VOID
NTAPI
ZpNative_VideoStreamDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_ PVOID Context)
{
    PZP_NATIVE_VIDEO_STREAM Stream = Context;

    if (!Stream->DataCallback(Data->Buffer, Data->Length, Stream->Context)) ZpChannel_Cancel(Channel);
}

static
VOID
NTAPI
ZpNative_VideoStreamCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_ PVOID Context)
{
    PZP_NATIVE_VIDEO_STREAM Stream = Context;

    RtlAcquireSRWLockExclusive(&Stream->Lock);
    Stream->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Stream->Lock);
    Stream->CloseCallback(Status, Stream->Context);
    ZpChannel_Close(Channel);
    ZpNative_ReleaseVideoStream(Stream);
}

static
VOID
NTAPI
ZpNative_ServiceListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_SERVICE_LIST_VIEW Services,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_SERVICE_RECORD Records = NULL;
    ZP_SERVICE_RECORD_VIEW Record;
    ULONG Index;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Services->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Services->Count * sizeof(*Records));
        if (Records == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
    }
    for (Index = 0;
         ZpStatus_IsSuccess(Status) && Index < Services->Count;
         Index++)
    {
        DecodeStatus = ZpService_GetRecord(Services, Index, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].ServiceType = Record.ServiceType;
        Records[Index].CurrentState = Record.CurrentState;
        Records[Index].ControlsAccepted = Record.ControlsAccepted;
        Records[Index].ProcessId = Record.ProcessId;
        Records[Index].StartType = Record.StartType;
        Records[Index].ServiceName = (PCWCH)Record.ServiceName.Buffer;
        Records[Index].ServiceNameLength = Record.ServiceName.Length;
        Records[Index].DisplayName = (PCWCH)Record.DisplayName.Buffer;
        Records[Index].DisplayNameLength = Record.DisplayName.Length;
        Records[Index].Description = (PCWCH)Record.Description.Buffer;
        Records[Index].DescriptionLength = Record.Description.Length;
        Records[Index].StartName = (PCWCH)Record.StartName.Buffer;
        Records[Index].StartNameLength = Record.StartName.Length;
    }
    CallbackContext->Callback.ServiceList(
        Status,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Services->Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ServiceInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_SERVICE_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.ServiceInfo(Status,
                                          ZpStatus_IsSuccess(Status) ? Info : NULL,
                                          CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
ZpNative_ReleaseFileTransfer(
    _Inout_ PZP_NATIVE_FILE_TRANSFER Transfer)
{
    if (InterlockedDecrement(&Transfer->ReferenceCount) == 0)
    {
        ZpConnection_Release(Transfer->Connection);
        Mem_Free(Transfer);
    }
}

static
VOID
NTAPI
ZpNative_FileReadOpenCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_ ULONGLONG Offset,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer = Context;

    UNREFERENCED_PARAMETER(Offset);
    if (ZpStatus_IsSuccess(Status))
    {
        Transfer->Channel = Channel;
        Transfer->ReferenceCount = 2;
    }
    Transfer->OpenCallback(Status,
                           ZpStatus_IsSuccess(Status) ? Transfer : NULL,
                           FileSize,
                           Transfer->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Transfer->Connection);
        Mem_Free(Transfer);
    }
}

static
VOID
NTAPI
ZpNative_FileWriteOpenCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer = Context;

    if (ZpStatus_IsSuccess(Status))
    {
        Transfer->Channel = Channel;
        Transfer->ReferenceCount = 2;
    }
    Transfer->OpenCallback(Status,
                           ZpStatus_IsSuccess(Status) ? Transfer : NULL,
                           FileSize,
                           Transfer->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Transfer->Connection);
        Mem_Free(Transfer);
    }
}

static
VOID
NTAPI
ZpNative_FileDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer = Context;

    if (!Transfer->DataCallback(Data->Buffer, Data->Length, Transfer->Context))
    {
        ZpChannel_Cancel(Channel);
    }
}

static
VOID
NTAPI
ZpNative_FileWritableCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer = Context;

    UNREFERENCED_PARAMETER(Channel);
    Transfer->WritableCallback(CreditBytes, Transfer->Context);
}

static
VOID
NTAPI
ZpNative_FileCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer = Context;

    RtlAcquireSRWLockExclusive(&Transfer->Lock);
    Transfer->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Transfer->Lock);
    Transfer->CloseCallback(Status, Transfer->Context);
    ZpChannel_Close(Channel);
    ZpNative_ReleaseFileTransfer(Transfer);
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
ZpNative_AdministrationCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_ADMINISTRATION_LIST_VIEW List,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_ADMINISTRATION_RECORD Records = NULL;
    ZP_ADMINISTRATION_RECORD_VIEW Record;
    ULONG Index;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && List->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)List->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < List->Count; Index++)
    {
        DecodeStatus = ZpAdministration_GetRecord(List, Index, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Kind = Record.Kind;
        Records[Index].State = Record.State;
        Records[Index].Flags = Record.Flags;
        Records[Index].Value = Record.Value;
#define ZP_NATIVE_ADMINISTRATION_STRING(Field) \
        Records[Index].Field = (PCWCH)Record.Field.Buffer; \
        Records[Index].Field##Length = Record.Field.Length
        ZP_NATIVE_ADMINISTRATION_STRING(Identity);
        ZP_NATIVE_ADMINISTRATION_STRING(Name);
        ZP_NATIVE_ADMINISTRATION_STRING(Description);
        ZP_NATIVE_ADMINISTRATION_STRING(Detail);
#undef ZP_NATIVE_ADMINISTRATION_STRING
    }
    CallbackContext->Callback.Administration(Status,
                                               ZpStatus_IsSuccess(Status) ? Records : NULL,
                                               ZpStatus_IsSuccess(Status) ? List->Count : 0,
                                               CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_BrowserCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BROWSER_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_BROWSER_RECORD Records = NULL;
    ZP_BROWSER_RECORD_VIEW Record;
    ULONG Index;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Page->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Page->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Page->Count; Index++)
    {
        DecodeStatus = ZpBrowser_GetRecord(Page, Index, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Kind = Record.Kind;
        Records[Index].Browser = Record.Browser;
        Records[Index].State = Record.State;
        Records[Index].Flags = Record.Flags;
        Records[Index].Id = Record.Id;
        Records[Index].Time = Record.Time;
        Records[Index].Value = Record.Value;
#define ZP_NATIVE_BROWSER_STRING(Field) \
        Records[Index].Field = (PCWCH)Record.Field.Buffer; \
        Records[Index].Field##Length = Record.Field.Length
        ZP_NATIVE_BROWSER_STRING(Identity);
        ZP_NATIVE_BROWSER_STRING(Name);
        ZP_NATIVE_BROWSER_STRING(Location);
        ZP_NATIVE_BROWSER_STRING(Detail);
#undef ZP_NATIVE_BROWSER_STRING
    }
    CallbackContext->Callback.Browser(Status,
                                      ZpStatus_IsSuccess(Status) ? Page->NextCursor : 0,
                                      ZpStatus_IsSuccess(Status) ? Records : NULL,
                                      ZpStatus_IsSuccess(Status) ? Page->Count : 0,
                                      CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_WmiCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_WMI_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_WMI_ROW Rows = NULL;
    PZP_NATIVE_WMI_CELL Cells = NULL, CellCursor;
    ZP_WMI_ROW_VIEW Row;
    ZP_WMI_CELL Cell;
    ULONG RowIndex, CellIndex, CellCount = 0, RowOffset = 0, CellOffset;
    NTSTATUS DecodeStatus;

    for (RowIndex = 0; ZpStatus_IsSuccess(Status) && RowIndex < Page->RowCount; RowIndex++)
    {
        DecodeStatus = ZpWmi_GetNextRow(Page, &RowOffset, &Row);
        if (!NT_SUCCESS(DecodeStatus) || CellCount > MAXULONG - Row.CellCount)
        {
            Status = ZpStatus_FromNtStatus(
                NT_SUCCESS(DecodeStatus) ? STATUS_INTEGER_OVERFLOW : DecodeStatus);
            break;
        }
        CellCount += Row.CellCount;
    }
    if (ZpStatus_IsSuccess(Status) && Page->RowCount != 0)
    {
        Rows = Mem_Alloc((SIZE_T)Page->RowCount * sizeof(*Rows));
        Cells = CellCount == 0 ? NULL : Mem_Alloc((SIZE_T)CellCount * sizeof(*Cells));
        if (Rows == NULL || (CellCount != 0 && Cells == NULL))
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
    }
    CellCursor = Cells;
    RowOffset = 0;
    for (RowIndex = 0; ZpStatus_IsSuccess(Status) && RowIndex < Page->RowCount; RowIndex++)
    {
        DecodeStatus = ZpWmi_GetNextRow(Page, &RowOffset, &Row);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Rows[RowIndex].Cells = CellCursor;
        Rows[RowIndex].CellCount = Row.CellCount;
        CellOffset = 0;
        for (CellIndex = 0; CellIndex < Row.CellCount; CellIndex++, CellCursor++)
        {
            DecodeStatus = ZpWmi_GetNextCell(&Row, &CellOffset, &Cell);
            if (!NT_SUCCESS(DecodeStatus))
            {
                Status = ZpStatus_FromNtStatus(DecodeStatus);
                break;
            }
            CellCursor->Type = Cell.Type;
            CellCursor->Name = Cell.Name;
            CellCursor->NameLength = Cell.NameLength;
            CellCursor->Value = Cell.Value;
            CellCursor->ValueLength = Cell.ValueLength;
        }
        if (!NT_SUCCESS(DecodeStatus)) break;
    }
    CallbackContext->Callback.Wmi(Status,
                                  ZpStatus_IsSuccess(Status) ? Rows : NULL,
                                  ZpStatus_IsSuccess(Status) ? Page->RowCount : 0,
                                  CallbackContext->Context);
    Mem_Free(Cells);
    Mem_Free(Rows);
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
ZpNative_EventLogChannelsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_EVENT_LOG_CHANNEL_LIST_VIEW Channels,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_STRING_VIEW Values = NULL;
    ULONG Index;
    NTSTATUS DecodeStatus = STATUS_SUCCESS;

    if (ZpStatus_IsSuccess(Status) && Channels->Count != 0)
    {
        Values = Mem_Alloc((SIZE_T)Channels->Count * sizeof(*Values));
        if (Values == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Channels->Count; Index++)
    {
        DecodeStatus = ZpEventLog_GetChannel(Channels, Index, &Values[Index]);
        if (!NT_SUCCESS(DecodeStatus)) Status = ZpStatus_FromNtStatus(DecodeStatus);
    }
    CallbackContext->Callback.EventLogChannels(Status,
                                                ZpStatus_IsSuccess(Status) ? Values : NULL,
                                                ZpStatus_IsSuccess(Status) ? Channels->Count : 0,
                                                CallbackContext->Context);
    Mem_Free(Values);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_EventLogChannelInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_EVENT_LOG_CHANNEL_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.EventLogChannelInfo(
        Status,
        ZpStatus_IsSuccess(Status) ? Info->Enabled : FALSE,
        ZpStatus_IsSuccess(Status) ? Info->Type : 0,
        ZpStatus_IsSuccess(Status) ? Info->RetentionMode : 0,
        ZpStatus_IsSuccess(Status) ? Info->MaximumSize : 0,
        ZpStatus_IsSuccess(Status) ? Info->FileSize : 0,
        ZpStatus_IsSuccess(Status) ? Info->CreationTime : 0,
        ZpStatus_IsSuccess(Status) ? Info->LastAccessTime : 0,
        ZpStatus_IsSuccess(Status) ? Info->LastWriteTime : 0,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Info->LogFilePath.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Info->LogFilePath.Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_RegistryKeyPageCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_REGISTRY_KEY_RECORD Records = NULL;
    ZP_REGISTRY_KEY_RECORD_VIEW Record;
    ULONG Index;
    NTSTATUS DecodeStatus;

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
        DecodeStatus = ZpRegistry_GetKeyRecord(&Page->Records, Index, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Name = (PCWCH)Record.Name.Buffer;
        Records[Index].NameLength = Record.Name.Length;
        Records[Index].LastWriteTime = Record.LastWriteTime;
        Records[Index].HasChildren = Record.HasChildren;
    }
    CallbackContext->Callback.RegistryKeyPage(
        Status,
        ZpStatus_IsSuccess(Status) ? Page->HasMore : FALSE,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Page->NextCursor.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Page->NextCursor.Length : 0,
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
ZpNative_RegistryValuePageCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_REGISTRY_VALUE_RECORD Records = NULL;
    ZP_REGISTRY_VALUE_RECORD_VIEW Record;
    ULONG Index;
    NTSTATUS DecodeStatus;

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
        DecodeStatus = ZpRegistry_GetValueRecord(&Page->Records,
                                                  Index,
                                                  &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Name = (PCWCH)Record.Name.Buffer;
        Records[Index].NameLength = Record.Name.Length;
        Records[Index].Type = Record.Type;
        Records[Index].DataLength = Record.DataLength;
        Records[Index].Preview = Record.Preview.Buffer;
        Records[Index].PreviewLength = Record.Preview.Length;
    }
    CallbackContext->Callback.RegistryValuePage(
        Status,
        ZpStatus_IsSuccess(Status) ? Page->HasMore : FALSE,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Page->NextCursor.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Page->NextCursor.Length : 0,
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
ZpNative_RegistryValueCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_VALUE_VIEW Value,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.RegistryValue(
        Status,
        ZpStatus_IsSuccess(Status) ? Value->Type : 0,
        ZpStatus_IsSuccess(Status) ? Value->Data.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Value->Data.Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_RegistryRangeCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_RANGE_VIEW Range,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.RegistryRange(
        Status,
        ZpStatus_IsSuccess(Status) ? Range->TotalLength : 0,
        ZpStatus_IsSuccess(Status) ? Range->Data.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Range->Data.Length : 0,
        CallbackContext->Context);
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
VOID
ZpNative_ReleaseTunnel(
    _Inout_ PZP_NATIVE_TUNNEL Tunnel)
{
    if (InterlockedDecrement(&Tunnel->ReferenceCount) == 0)
    {
        ZpConnection_Release(Tunnel->Connection);
        Mem_Free(Tunnel);
    }
}

static
VOID
NTAPI
ZpNative_TunnelOpenCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TUNNEL Tunnel = Context;

    if (ZpStatus_IsSuccess(Status))
    {
        Tunnel->Channel = Channel;
        Tunnel->ReferenceCount = 2;
    }
    Tunnel->OpenCallback(Status,
                         ZpStatus_IsSuccess(Status) ? Tunnel : NULL,
                         Tunnel->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Tunnel->Connection);
        Mem_Free(Tunnel);
    }
}

static
VOID
NTAPI
ZpNative_TunnelDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TUNNEL Tunnel = Context;

    if (!Tunnel->DataCallback(Data->Buffer, Data->Length, Tunnel->Context))
    {
        ZpChannel_Cancel(Channel);
    }
}

static
VOID
NTAPI
ZpNative_TunnelWritableCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TUNNEL Tunnel = Context;

    UNREFERENCED_PARAMETER(Channel);
    Tunnel->WritableCallback(CreditBytes, Tunnel->Context);
}

static
VOID
NTAPI
ZpNative_TunnelCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TUNNEL Tunnel = Context;

    RtlAcquireSRWLockExclusive(&Tunnel->Lock);
    Tunnel->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Tunnel->Lock);
    Tunnel->CloseCallback(Status, Tunnel->Context);
    ZpChannel_Close(Channel);
    ZpNative_ReleaseTunnel(Tunnel);
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
        { ZP_REGISTRY_MODULE_ID, ZP_REGISTRY_MODULE_VERSION },
        { ZP_WINDOW_MODULE_ID, ZP_WINDOW_MODULE_VERSION },
        { ZP_ADMINISTRATION_MODULE_ID, ZP_ADMINISTRATION_MODULE_VERSION },
        { ZP_EXECUTION_MODULE_ID, ZP_EXECUTION_MODULE_VERSION },
        { ZP_TUNNEL_MODULE_ID, ZP_TUNNEL_MODULE_VERSION },
        { ZP_BROWSER_MODULE_ID, ZP_BROWSER_MODULE_VERSION },
        { ZP_WMI_MODULE_ID, ZP_WMI_MODULE_VERSION },
        { ZP_AUDIO_MODULE_ID, ZP_AUDIO_MODULE_VERSION },
        { ZP_VIDEO_MODULE_ID, ZP_VIDEO_MODULE_VERSION }
    };
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
    RtlReleaseSRWLockExclusive(&ZpNativeLock);
    ZpNativeStateEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (ZpNativeStateEvent == NULL)
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
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

NTSTATUS
NTAPI
ZpNative_EnumerateFilesPage(
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG EnumerationId,
    _In_ ZP_NATIVE_FILE_PAGE_CALLBACK Callback,
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
    CallbackContext->Callback.FilePage = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateFilesPage(Connection,
                                    Path,
                                    PathLength,
                                    EnumerationId,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_FilePageCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryFile(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_FILE_INFO_CALLBACK Callback,
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
    CallbackContext->Callback.FileInfo = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryFile(Connection,
                           Path,
                           PathLength,
                           ZP_NATIVE_TIMEOUT_MILLISECONDS,
                           ZpNative_FileInfoCallback,
                           CallbackContext,
                           &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryFileSecurity(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_STRING_CALLBACK Callback,
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
    CallbackContext->Callback.String = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryFileSecurity(Connection,
                                   Path,
                                   PathLength,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_StringCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_SetFileSecurity(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(SddlLength) PCWCH Sddl,
    _In_ ULONG SddlLength,
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
        ZpServer_SetFileSecurity(Connection,
                                 Path,
                                 PathLength,
                                 Sddl,
                                 SddlLength,
                                 ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                 ZpNative_StatusCallback,
                                 CallbackContext,
                                 &Request));
}

NTSTATUS
NTAPI
ZpNative_ResolveAccountName(
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ZP_NATIVE_STRING_CALLBACK Callback,
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
    CallbackContext->Callback.String = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ResolveAccountName(Connection,
                                    Name,
                                    NameLength,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_StringCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_ResolveAccountSid(
    _In_reads_(SidLength) PCWCH Sid,
    _In_ ULONG SidLength,
    _In_ ZP_NATIVE_STRING_CALLBACK Callback,
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
    CallbackContext->Callback.String = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ResolveAccountSid(Connection,
                                   Sid,
                                   SidLength,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_StringCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryFileVolume(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_FILE_VOLUME_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.FileVolume = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryFileVolume(Connection,
                                 Path,
                                 PathLength,
                                 ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                 ZpNative_FileVolumeCallback,
                                 CallbackContext,
                                 &Request));
}

NTSTATUS
NTAPI
ZpNative_SetFileVolumeLabel(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(LabelLength) PCWCH Label,
    _In_ ULONG LabelLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
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
        ZpServer_SetFileVolumeLabel(Connection,
                                    Path,
                                    PathLength,
                                    Label,
                                    LabelLength,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_StatusCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_HashFile(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_FILE_HASH_ALGORITHM Algorithm,
    _In_ ZP_NATIVE_FILE_HASH_CALLBACK Callback,
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
    CallbackContext->Callback.FileHash = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_HashFile(Connection,
                          Path,
                          PathLength,
                          Algorithm,
                          ZP_NATIVE_TIMEOUT_MILLISECONDS,
                          ZpNative_FileHashCallback,
                          CallbackContext,
                          &Request));
}

NTSTATUS
NTAPI
ZpNative_DeleteFile(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
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
        ZpServer_DeleteFile(Connection,
                            Path,
                            PathLength,
                            ZP_NATIVE_TIMEOUT_MILLISECONDS,
                            ZpNative_StatusCallback,
                            CallbackContext,
                            &Request));
}

NTSTATUS
NTAPI
ZpNative_RenameFile(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(NewPathLength) PCWCH NewPath,
    _In_ ULONG NewPathLength,
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
        ZpServer_RenameFile(Connection,
                            Path,
                            PathLength,
                            NewPath,
                            NewPathLength,
                            ZP_NATIVE_TIMEOUT_MILLISECONDS,
                            ZpNative_StatusCallback,
                            CallbackContext,
                            &Request));
}

NTSTATUS
NTAPI
ZpNative_SetFileAttributes(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG Attributes,
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
        ZpServer_SetFileAttributes(Connection,
                                   Path,
                                   PathLength,
                                   Attributes,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_StatusCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_OpenFileRead(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG Offset,
    _In_ ZP_NATIVE_FILE_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_FILE_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_FILE_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer;
    ZP_CONNECTION_HANDLE Connection;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (OpenCallback == NULL || DataCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection();
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    Transfer = Mem_Alloc(sizeof(*Transfer));
    if (Transfer == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Transfer, sizeof(*Transfer));
    Transfer->Connection = Connection;
    Transfer->OpenCallback = OpenCallback;
    Transfer->DataCallback = DataCallback;
    Transfer->CloseCallback = CloseCallback;
    Transfer->Context = Context;
    Status = ZpServer_OpenFileRead(Connection,
                                   Path,
                                   PathLength,
                                   Offset,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_FileReadOpenCallback,
                                   ZpNative_FileDataCallback,
                                   ZpNative_FileCloseCallback,
                                   Transfer,
                                   &Request);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Release(Connection);
        Mem_Free(Transfer);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_WriteFileRange(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
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
        ZpServer_WriteFileRange(Connection,
                                Path,
                                PathLength,
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
ZpNative_OpenFileWrite(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG FileSize,
    _In_ LOGICAL Overwrite,
    _In_ ZP_NATIVE_FILE_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_FILE_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_NATIVE_FILE_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer;
    ZP_CONNECTION_HANDLE Connection;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (OpenCallback == NULL || WritableCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection();
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    Transfer = Mem_Alloc(sizeof(*Transfer));
    if (Transfer == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Transfer, sizeof(*Transfer));
    Transfer->Connection = Connection;
    Transfer->OpenCallback = OpenCallback;
    Transfer->WritableCallback = WritableCallback;
    Transfer->CloseCallback = CloseCallback;
    Transfer->Context = Context;
    Status = ZpServer_OpenFileWrite(Connection,
                                    Path,
                                    PathLength,
                                    FileSize,
                                    Overwrite ? ZpFileCreateAlways : ZpFileCreateNew,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_FileWriteOpenCallback,
                                    ZpNative_FileWritableCallback,
                                    ZpNative_FileCloseCallback,
                                    Transfer,
                                    &Request);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Release(Connection);
        Mem_Free(Transfer);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_FileSend(
    _In_ ZP_NATIVE_FILE_TRANSFER_HANDLE Transfer,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    NTSTATUS Status;

    if (Transfer == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }
    RtlAcquireSRWLockShared(&Transfer->Lock);
    Status = Transfer->Channel != NULL ?
                 ZpChannel_Send(Transfer->Channel, Data, DataLength) :
                 STATUS_INVALID_DEVICE_STATE;
    RtlReleaseSRWLockShared(&Transfer->Lock);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_CloseFileTransfer(
    _In_ ZP_NATIVE_FILE_TRANSFER_HANDLE Transfer)
{
    ZP_CHANNEL_HANDLE Channel;
    NTSTATUS Status;

    if (Transfer == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (InterlockedExchange(&Transfer->CallerClosed, TRUE))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    RtlAcquireSRWLockExclusive(&Transfer->Lock);
    Channel = Transfer->Channel;
    Transfer->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Transfer->Lock);
    Status = Channel != NULL ? ZpChannel_Cancel(Channel) : STATUS_SUCCESS;
    ZpNative_ReleaseFileTransfer(Transfer);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_EnumerateProcesses(
    _In_ ZP_NATIVE_PROCESS_LIST_CALLBACK Callback,
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
    CallbackContext->Callback.ProcessList = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateProcesses(Connection,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_ProcessListCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryProcess(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ZP_NATIVE_PROCESS_INFO_CALLBACK Callback,
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
    CallbackContext->Callback.ProcessInfo = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryProcess(Connection,
                              ProcessId,
                              CreateTime,
                              ZP_NATIVE_TIMEOUT_MILLISECONDS,
                              ZpNative_ProcessInfoCallback,
                              CallbackContext,
                              &Request));
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
ZpNative_ControlProcess(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ZP_PROCESS_CONTROL Control,
    _In_ ULONG Value,
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
        ZpServer_ControlProcess(Connection,
                                ProcessId,
                                CreateTime,
                                Control,
                                Value,
                                ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                ZpNative_StatusCallback,
                                CallbackContext,
                                &Request));
}

NTSTATUS
NTAPI
ZpNative_CreateProcessDump(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONG DumpType,
    _In_ ZP_NATIVE_PROCESS_DUMP_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ProcessDump = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_CreateProcessDump(Connection,
                                   ProcessId,
                                   CreateTime,
                                   DumpType,
                                   ZP_NATIVE_PROCESS_DUMP_TIMEOUT_MILLISECONDS,
                                   ZpNative_ProcessDumpCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_ReadProcessMemory(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONGLONG Address,
    _In_ ULONG Length,
    _In_ ZP_NATIVE_PROCESS_MEMORY_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ProcessMemory = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ReadProcessMemory(Connection,
                                   ProcessId,
                                   CreateTime,
                                   Address,
                                   Length,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_ProcessMemoryCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_WriteProcessMemory(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONGLONG Address,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
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
        ZpServer_WriteProcessMemory(Connection,
                                    ProcessId,
                                    CreateTime,
                                    Address,
                                    Data,
                                    DataLength,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_StatusCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateExecutionSessions(
    _In_ ZP_NATIVE_EXECUTION_SESSIONS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ExecutionSessions = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateExecutionSessions(Connection,
                                            ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                            ZpNative_ExecutionSessionsCallback,
                                            CallbackContext,
                                            &Request));
}

NTSTATUS
NTAPI
ZpNative_StartExecution(
    _In_ USHORT Engine,
    _In_ USHORT Identity,
    _In_ ULONG SessionId,
    _In_ ULONG Flags,
    _In_reads_(FileNameLength) PCWCH FileName,
    _In_ ULONG FileNameLength,
    _In_reads_opt_(ArgumentsLength) PCWCH Arguments,
    _In_ ULONG ArgumentsLength,
    _In_reads_opt_(WorkingDirectoryLength) PCWCH WorkingDirectory,
    _In_ ULONG WorkingDirectoryLength,
    _In_reads_opt_(VerbLength) PCWCH Verb,
    _In_ ULONG VerbLength,
    _In_reads_opt_(UserNameLength) PCWCH UserName,
    _In_ ULONG UserNameLength,
    _In_reads_opt_(PasswordLength) PCWCH Password,
    _In_ ULONG PasswordLength,
    _In_ ZP_NATIVE_EXECUTION_JOBS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_EXECUTION_START Start = {
        Engine,
        Identity,
        SessionId,
        Flags,
        FileName,
        FileNameLength,
        Arguments,
        ArgumentsLength,
        WorkingDirectory,
        WorkingDirectoryLength,
        Verb,
        VerbLength,
        UserName,
        UserNameLength,
        Password,
        PasswordLength
    };
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ExecutionJobs = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_StartExecution(Connection,
                                &Start,
                                ZP_NATIVE_LONG_OPERATION_TIMEOUT_MILLISECONDS,
                                ZpNative_ExecutionJobsCallback,
                                CallbackContext,
                                &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateExecutionJobs(
    _In_ ZP_NATIVE_EXECUTION_JOBS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ExecutionJobs = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateExecutionJobs(Connection,
                                        ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                        ZpNative_ExecutionJobsCallback,
                                        CallbackContext,
                                        &Request));
}

NTSTATUS
NTAPI
ZpNative_TerminateExecution(
    _In_ ULONG JobId,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
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
        ZpServer_TerminateExecution(Connection,
                                    JobId,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_StatusCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_CreateExecutionStaging(
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ZP_NATIVE_EXECUTION_STAGING_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ExecutionStaging = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_CreateExecutionStaging(Connection,
                                        Name,
                                        NameLength,
                                        ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                        ZpNative_ExecutionStagingCallback,
                                        CallbackContext,
                                        &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateWindows(
    _In_ ZP_NATIVE_WINDOW_LIST_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
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
ZpNative_QueryWindow(
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
    Connection = ZpNative_GetConnection();
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
    Connection = ZpNative_GetConnection();
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
    Connection = ZpNative_GetConnection();
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
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ULONG Flags,
    _In_ ULONG MaxDimension,
    _In_ USHORT FrameRate,
    _In_ USHORT Quality,
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
        Quality
    };
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
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
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ULONG Flags,
    _In_ ULONG MaxDimension,
    _In_ USHORT FrameRate,
    _In_ USHORT Quality,
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
        Quality
    };
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_WINDOW_CAPTURE_STREAM Stream;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (OpenCallback == NULL || DataCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection();
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
ZpNative_EnumerateAudioDevices(
    _In_ ZP_NATIVE_AUDIO_DEVICES_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
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
    _In_ ZP_NATIVE_AUDIO_SESSIONS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
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
    _In_ USHORT Flow,
    _In_ USHORT Control,
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
    Connection = ZpNative_GetConnection();
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
    _In_ USHORT Control,
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
    Connection = ZpNative_GetConnection();
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
    _In_ USHORT Flow,
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
    Connection = ZpNative_GetConnection();
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
    _In_ ZP_NATIVE_VIDEO_DEVICES_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
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
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ULONG MaxDimension,
    _In_ USHORT FrameRate,
    _In_ USHORT Quality,
    _In_ ZP_NATIVE_VIDEO_STREAM_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_VIDEO_STREAM_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_VIDEO_STREAM_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_VIDEO_STREAM Stream;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (OpenCallback == NULL || DataCallback == NULL || CloseCallback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
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
                                      MaxDimension,
                                      FrameRate,
                                      Quality,
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
ZpNative_EnumerateServices(
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
    _In_ ULONG Control,
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

NTSTATUS
NTAPI
ZpNative_EnumerateAdministration(
    _In_ BYTE OperationId,
    _In_ ZP_NATIVE_ADMINISTRATION_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Administration = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateAdministration(Connection,
                                         OperationId,
                                         (OperationId == ZP_ADMINISTRATION_OPERATION_ENUMERATE_UPDATES ||
                                          OperationId == ZP_ADMINISTRATION_OPERATION_ENUMERATE_FEATURES) ?
                                             ZP_NATIVE_LONG_OPERATION_TIMEOUT_MILLISECONDS :
                                             ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                         ZpNative_AdministrationCallback,
                                         CallbackContext,
                                         &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryAdministration(
    _In_ BYTE OperationId,
    _In_reads_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_ ZP_NATIVE_ADMINISTRATION_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Administration = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryAdministration(Connection,
                                     OperationId,
                                     Identity,
                                     IdentityLength,
                                     ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                     ZpNative_AdministrationCallback,
                                     CallbackContext,
                                     &Request));
}

NTSTATUS
NTAPI
ZpNative_ControlAdministration(
    _In_ BYTE OperationId,
    _In_ USHORT Action,
    _In_reads_opt_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_reads_opt_(ArgumentLength) PCWCH Argument,
    _In_ ULONG ArgumentLength,
    _In_reads_opt_(SecretLength) PCWCH Secret,
    _In_ ULONG SecretLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
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
        ZpServer_ControlAdministration(Connection,
                                       OperationId,
                                       Action,
                                       Identity,
                                       IdentityLength,
                                       Argument,
                                       ArgumentLength,
                                       Secret,
                                       SecretLength,
                                       (OperationId == ZP_ADMINISTRATION_OPERATION_CONTROL_UPDATE ||
                                        OperationId == ZP_ADMINISTRATION_OPERATION_CONTROL_FEATURE) ?
                                           ZP_NATIVE_LONG_OPERATION_TIMEOUT_MILLISECONDS :
                                           ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                       ZpNative_StatusCallback,
                                       CallbackContext,
                                       &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateBrowsers(
    _In_ ZP_NATIVE_BROWSER_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Browser = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateBrowsers(Connection,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_BrowserCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryBrowser(
    _In_ USHORT Browser,
    _In_ USHORT Kind,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_ ULONGLONG Cursor,
    _In_ ULONG Limit,
    _In_ ZP_NATIVE_BROWSER_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Browser = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryBrowser(Connection,
                              Browser,
                              Kind,
                              Profile,
                              ProfileLength,
                              Cursor,
                              Limit,
                              ZP_NATIVE_TIMEOUT_MILLISECONDS,
                              ZpNative_BrowserCallback,
                              CallbackContext,
                              &Request));
}

static
NTSTATUS
ZpNative_Wmi(
    _In_ BYTE OperationId,
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_ ULONG Limit,
    _In_ ULONG Flags,
    _In_ ZP_NATIVE_WMI_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Wmi = Callback;
    Status = OperationId == ZP_WMI_OPERATION_ENUMERATE_NAMESPACES ?
                 ZpServer_EnumerateWmiNamespaces(Connection,
                                                 Namespace,
                                                 NamespaceLength,
                                                 ZP_NATIVE_WMI_TIMEOUT_MILLISECONDS,
                                                 ZpNative_WmiCallback,
                                                 CallbackContext,
                                                 &Request) :
             OperationId == ZP_WMI_OPERATION_ENUMERATE_CLASSES ?
                 ZpServer_EnumerateWmiClasses(Connection,
                                              Namespace,
                                              NamespaceLength,
                                              ZP_NATIVE_WMI_TIMEOUT_MILLISECONDS,
                                              ZpNative_WmiCallback,
                                              CallbackContext,
                                              &Request) :
                 ZpServer_QueryWmi(Connection,
                                   Namespace,
                                   NamespaceLength,
                                   Query,
                                   QueryLength,
                                   Limit,
                                   Flags,
                                   ZP_NATIVE_WMI_TIMEOUT_MILLISECONDS,
                                   ZpNative_WmiCallback,
                                   CallbackContext,
                                   &Request);
    return ZpNative_SendStatusRequest(CallbackContext, Status);
}

NTSTATUS
NTAPI
ZpNative_EnumerateWmiNamespaces(
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_ ZP_NATIVE_WMI_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    return ZpNative_Wmi(ZP_WMI_OPERATION_ENUMERATE_NAMESPACES,
                        Namespace,
                        NamespaceLength,
                        NULL,
                        0,
                        ZP_WMI_MAX_ROWS,
                        0,
                        Callback,
                        Context);
}

NTSTATUS
NTAPI
ZpNative_EnumerateWmiClasses(
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_ ZP_NATIVE_WMI_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    return ZpNative_Wmi(ZP_WMI_OPERATION_ENUMERATE_CLASSES,
                        Namespace,
                        NamespaceLength,
                        NULL,
                        0,
                        ZP_WMI_MAX_ROWS,
                        0,
                        Callback,
                        Context);
}

NTSTATUS
NTAPI
ZpNative_QueryWmi(
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_reads_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_ ULONG Limit,
    _In_ ULONG Flags,
    _In_ ZP_NATIVE_WMI_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    return ZpNative_Wmi(ZP_WMI_OPERATION_QUERY,
                        Namespace,
                        NamespaceLength,
                        Query,
                        QueryLength,
                        Limit,
                        Flags,
                        Callback,
                        Context);
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
ZpNative_OpenTunnel(
    _In_reads_(HostLength) PCWCH Host,
    _In_ ULONG HostLength,
    _In_ USHORT Port,
    _In_ USHORT Protocol,
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
    Connection = ZpNative_GetConnection();
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
    _In_ ZP_NATIVE_EVENT_LOG_CHANNELS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
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
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ ZP_NATIVE_EVENT_LOG_CHANNEL_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection();
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

NTSTATUS
NTAPI
ZpNative_ConfigureEventLogChannel(
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
    Connection = ZpNative_GetConnection();
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
    Connection = ZpNative_GetConnection();
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
    Connection = ZpNative_GetConnection();
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
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
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
    CallbackContext->Callback.RegistryValue = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryRegistrySecurity(Connection,
                                       Root,
                                       Path,
                                       PathLength,
                                       ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                       ZpNative_RegistryValueCallback,
                                       CallbackContext,
                                       &Request));
}

NTSTATUS
NTAPI
ZpNative_SetRegistrySecurity(
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(SddlLength) PCWCH Sddl,
    _In_ ULONG SddlLength,
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
        ZpServer_SetRegistrySecurity(Connection,
                                     Root,
                                     Path,
                                     PathLength,
                                     Sddl,
                                     SddlLength,
                                     ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                     ZpNative_StatusCallback,
                                     CallbackContext,
                                     &Request));
}

__declspec(dllexport)
NTSTATUS
NTAPI
ZpNative_ExecuteRegistryStatus(
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
