#include <KNSoft/MakeLifeEasier/IO/File.h>
#include <KNSoft/ZPigeon/Client.h>
#include <KNSoft/ZPigeon/Operations.h>

#include <stdio.h>

#define ZP_CLIENT_PORT 4433
#define ZP_CLIENT_LOG_CACHE_SIZE 32

typedef struct _ZP_CLIENT_LOG
{
    PCWSTR Name;
    HANDLE Handle;
} ZP_CLIENT_LOG, *PZP_CLIENT_LOG;

static WCHAR ZpClientDirectory[MAX_PATH];
static HANDLE ZpClientStopEvent;
static HANDLE ZpClientStoppedEvent;
static RTL_SRWLOCK ZpClientLogLock = RTL_SRWLOCK_INIT;
static ZP_CLIENT_LOG ZpClientLogs[ZP_CLIENT_LOG_CACHE_SIZE];

static
PCSTR
ZpClient_GetStateName(
    _In_ ZP_CLIENT_STATE State)
{
    switch (State)
    {
        case ZpClientStateStopped: return "Stopped";
        case ZpClientStateConnecting: return "Connecting";
        case ZpClientStateAuthenticating: return "Authenticating";
        case ZpClientStateReady: return "Ready";
        case ZpClientStateRetryWait: return "RetryWait";
        case ZpClientStateStopping: return "Stopping";
        default: return "Unknown";
    }
}

static
PCSTR
ZpClient_GetStatusTypeName(
    _In_ ZP_STATUS_TYPE Type)
{
    switch (Type)
    {
        case ZpStatusNone: return "Success";
        case ZpStatusNtStatus: return "NTSTATUS";
        case ZpStatusWin32: return "Win32";
        case ZpStatusWinsock: return "Winsock";
        case ZpStatusHResult: return "HRESULT";
        case ZpStatusSecurity: return "Security";
        case ZpStatusQuic: return "QUIC";
        case ZpStatusProcessExit: return "ProcessExit";
        case ZpStatusConfigurationManager: return "ConfigurationManager";
        default: return "Unknown";
    }
}

static
PCWSTR
ZpClient_GetModuleLogName(
    _In_ BYTE ModuleId)
{
    switch (ModuleId)
    {
        case ZP_SYSTEM_MODULE_ID: return L"system.log";
        case ZP_PROCESS_MODULE_ID: return L"process.log";
        case ZP_SERVICE_MODULE_ID: return L"service.log";
        case ZP_FILE_MODULE_ID: return L"file.log";
        case ZP_PORTABLE_DEVICE_MODULE_ID: return L"portable-device.log";
        case ZP_TERMINAL_MODULE_ID: return L"terminal.log";
        case ZP_EVENT_LOG_MODULE_ID: return L"eventlog.log";
        case ZP_EXECUTION_MODULE_ID: return L"execution.log";
        case ZP_TUNNEL_MODULE_ID: return L"tunnel.log";
        case ZP_REGISTRY_MODULE_ID: return L"registry.log";
        case ZP_WINDOW_MODULE_ID: return L"window.log";
        case ZP_AUDIO_MODULE_ID: return L"audio.log";
        case ZP_VIDEO_MODULE_ID: return L"video.log";
        case ZP_SERIAL_MODULE_ID: return L"serial.log";
        case ZP_RECORDING_MODULE_ID: return L"recording.log";
        case ZP_BROWSER_MODULE_ID: return L"browser.log";
        case ZP_WMI_MODULE_ID: return L"wmi.log";
        case ZP_USER_MODULE_ID: return L"user.log";
        case ZP_SOFTWARE_MODULE_ID: return L"software.log";
        case ZP_HARDWARE_MODULE_ID: return L"hardware.log";
        case ZP_UPDATE_MODULE_ID: return L"update.log";
        case ZP_TASK_MODULE_ID: return L"task.log";
        case ZP_FIREWALL_MODULE_ID: return L"firewall.log";
        case ZP_POWER_MODULE_ID: return L"power.log";
        case ZP_SYSTEM_ADMINISTRATION_MODULE_ID: return L"system.log";
        case ZP_WLAN_MODULE_ID: return L"wlan.log";
        case ZP_CERTIFICATE_MODULE_ID: return L"certificate.log";
        case ZP_CLIPBOARD_MODULE_ID: return L"clipboard.log";
        case ZP_CREDENTIAL_MODULE_ID: return L"credential.log";
        case ZP_FIRMWARE_MODULE_ID: return L"firmware.log";
        case ZP_NETWORK_SHARE_MODULE_ID: return L"network-share.log";
        case ZP_NETWORK_STATUS_MODULE_ID: return L"network.log";
        case ZP_PAGE_FILE_MODULE_ID: return L"page-file.log";
        case ZP_SHADOW_COPY_MODULE_ID: return L"shadow-copy.log";
        case ZP_BITLOCKER_MODULE_ID: return L"bitlocker.log";
        case ZP_BLUETOOTH_MODULE_ID: return L"bluetooth.log";
        case ZP_KEYBOARD_MODULE_ID: return L"keyboard.log";
        case ZP_LOCATION_MODULE_ID: return L"location.log";
        case ZP_FONT_MODULE_ID: return L"font.log";
        case ZP_APP_CONTAINER_MODULE_ID: return L"app-container.log";
        case ZP_WINOBJ_MODULE_ID: return L"winobj.log";
        case ZP_WSL_MODULE_ID: return L"wsl.log";
        case ZP_UI_AUTOMATION_MODULE_ID: return L"uia.log";
        case ZP_PROXY_VPN_MODULE_ID: return L"proxy-vpn.log";
        default: return L"modules.log";
    }
}

static
VOID
ZpClient_WriteLog(
    _In_ PCWSTR FileName,
    _In_ PCSTR Event,
    _In_ ZP_STATUS Status)
{
    WCHAR Path[MAX_PATH];
    CHAR Line[256];
    PZP_CLIENT_LOG Log = NULL;
    TIME_FIELDS TimeFields;
    LARGE_INTEGER SystemTime;
    LARGE_INTEGER Offset;
    ULONG Index;
    int Length;

    if (!NT_SUCCESS(NtQuerySystemTime(&SystemTime)))
    {
        return;
    }
    RtlTimeToTimeFields(&SystemTime, &TimeFields);
    Length = Status.Type == ZpStatusNone && Status.Code == 0 ?
                 sprintf_s(Line,
                           sizeof(Line),
                           "%04hd-%02hd-%02hdT%02hd:%02hd:%02hd.%03hdZ %s status=Success\r\n",
                           TimeFields.Year,
                           TimeFields.Month,
                           TimeFields.Day,
                           TimeFields.Hour,
                           TimeFields.Minute,
                           TimeFields.Second,
                           TimeFields.Milliseconds,
                           Event) :
                 sprintf_s(Line,
                           sizeof(Line),
                           "%04hd-%02hd-%02hdT%02hd:%02hd:%02hd.%03hdZ %s status=%s:0x%08lX\r\n",
                           TimeFields.Year,
                           TimeFields.Month,
                           TimeFields.Day,
                           TimeFields.Hour,
                           TimeFields.Minute,
                           TimeFields.Second,
                           TimeFields.Milliseconds,
                           Event,
                           ZpClient_GetStatusTypeName(Status.Type),
                           Status.Code);
    if (Length < 0)
    {
        return;
    }
    RtlAcquireSRWLockExclusive(&ZpClientLogLock);
    for (Index = 0; Index < RTL_NUMBER_OF(ZpClientLogs); Index++)
    {
        if (ZpClientLogs[Index].Name == NULL ||
            _wcsicmp(ZpClientLogs[Index].Name, FileName) == 0)
        {
            Log = &ZpClientLogs[Index];
            break;
        }
    }
    if (Log != NULL && Log->Name == NULL &&
        _snwprintf_s(Path,
                     ARRAYSIZE(Path),
                     _TRUNCATE,
                     L"%slogs\\%s",
                     ZpClientDirectory,
                     FileName) >= 0 &&
        NT_SUCCESS(IO_CreateWin32File(&Log->Handle,
                                      Path,
                                      NULL,
                                      FILE_APPEND_DATA | SYNCHRONIZE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      FILE_OPEN_IF,
                                      FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT)))
    {
        Log->Name = FileName;
    }
    if (Log != NULL && Log->Handle != NULL)
    {
        Offset.QuadPart = -1;
        IO_WriteFile(Log->Handle, &Offset, Line, (ULONG)Length, NULL);
    }
    RtlReleaseSRWLockExclusive(&ZpClientLogLock);
}

static
VOID
ZpClient_CloseLogs(VOID)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(ZpClientLogs); Index++)
    {
        if (ZpClientLogs[Index].Handle != NULL)
        {
            NtClose(ZpClientLogs[Index].Handle);
        }
    }
}

static
VOID
NTAPI
ZpClient_StateCallback(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    CHAR Event[48];

    UNREFERENCED_PARAMETER(Client);
    UNREFERENCED_PARAMETER(Context);
    sprintf_s(Event,
              sizeof(Event),
              "state=%s",
              ZpClient_GetStateName(State));
    ZpClient_WriteLog(L"network.log", Event, Status);
    if (State == ZpClientStateStopped)
    {
        SetEvent(ZpClientStoppedEvent);
    }
}

static
VOID
NTAPI
ZpClient_OperationCallback(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ BYTE ModuleId,
    _In_ BYTE OperationId,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    CHAR Event[64];

    UNREFERENCED_PARAMETER(Client);
    UNREFERENCED_PARAMETER(Context);
    sprintf_s(Event,
              sizeof(Event),
              "operation module=%hu id=%hu",
              ModuleId,
              OperationId);
    ZpClient_WriteLog(ZpClient_GetModuleLogName(ModuleId),
                      Event,
                      Status);
}

static
BOOL
WINAPI
ZpClient_ConsoleHandler(
    _In_ DWORD ControlType)
{
    if (ControlType == CTRL_C_EVENT ||
        ControlType == CTRL_BREAK_EVENT ||
        ControlType == CTRL_CLOSE_EVENT ||
        ControlType == CTRL_SHUTDOWN_EVENT)
    {
        SetEvent(ZpClientStopEvent);
        return TRUE;
    }
    return FALSE;
}

static
NTSTATUS
ZpClient_InitializeDirectory(VOID)
{
    WCHAR LogDirectory[MAX_PATH];
    PWCHAR Separator;
    HANDLE DirectoryHandle;
    ULONG Length;
    NTSTATUS Status;

    Length = GetModuleFileNameW(NULL,
                                ZpClientDirectory,
                                ARRAYSIZE(ZpClientDirectory));
    if (Length == 0 || Length == ARRAYSIZE(ZpClientDirectory))
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    Separator = wcsrchr(ZpClientDirectory, L'\\');
    if (Separator == NULL)
    {
        return STATUS_OBJECT_PATH_INVALID;
    }
    Separator[1] = UNICODE_NULL;
    if (_snwprintf_s(LogDirectory,
                     ARRAYSIZE(LogDirectory),
                     _TRUNCATE,
                     L"%slogs",
                     ZpClientDirectory) < 0)
    {
        return STATUS_NAME_TOO_LONG;
    }
    Status = IO_CreateWin32File(&DirectoryHandle,
                                LogDirectory,
                                NULL,
                                FILE_LIST_DIRECTORY | SYNCHRONIZE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE |
                                    FILE_SHARE_DELETE,
                                FILE_OPEN_IF,
                                FILE_DIRECTORY_FILE |
                                    FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    return NtClose(DirectoryHandle);
}

static
NTSTATUS
ZpClient_LoadRootCertificate(
    _Outptr_result_bytebuffer_(*Length) PBYTE* Certificate,
    _Out_ PULONG Length)
{
    WCHAR Path[MAX_PATH];
    ULONGLONG FileSize;
    HANDLE FileHandle;
    PBYTE Buffer;
    NTSTATUS Status;

    if (_snwprintf_s(Path,
                     ARRAYSIZE(Path),
                     _TRUNCATE,
                     L"%szpigeon-root.cer",
                     ZpClientDirectory) < 0)
    {
        return STATUS_NAME_TOO_LONG;
    }
    Status = IO_OpenWin32File(&FileHandle,
                              Path,
                              NULL,
                              FILE_READ_DATA | SYNCHRONIZE,
                              FILE_SHARE_READ);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = IO_GetFileSize(FileHandle, &FileSize);
    if (!NT_SUCCESS(Status) || FileSize == 0 || FileSize > ZP_CERTIFICATE_MAX_SIZE)
    {
        NtClose(FileHandle);
        return NT_SUCCESS(Status) ? STATUS_FILE_TOO_LARGE : Status;
    }
    Buffer = Mem_Alloc((SIZE_T)FileSize);
    if (Buffer == NULL)
    {
        NtClose(FileHandle);
        return STATUS_NO_MEMORY;
    }
    Status = IO_ReadFile(FileHandle,
                         NULL,
                         Buffer,
                         (ULONG)FileSize,
                         Length);
    NtClose(FileHandle);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    *Certificate = Buffer;
    return STATUS_SUCCESS;
}

int
wmain(VOID)
{
    static const ZP_ENDPOINT Endpoint = {
        ZpTransportQuic,
        L"127.0.0.1",
        ZP_CLIENT_PORT,
        L"localhost"
    };
    ZP_CLIENT_CONFIG Config = { 0 };
    ZP_CLIENT_HANDLE Client;
    PBYTE RootCertificate;
    ULONG RootCertificateLength;
    NTSTATUS Status;

    Status = ZpClient_InitializeDirectory();
    if (!NT_SUCCESS(Status))
    {
        return (int)Status;
    }
    ZpClientStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ZpClientStoppedEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (ZpClientStopEvent == NULL || ZpClientStoppedEvent == NULL)
    {
        return (int)NTSTATUS_FROM_WIN32(GetLastError());
    }
    Status = ZpClient_LoadRootCertificate(&RootCertificate,
                                          &RootCertificateLength);
    if (!NT_SUCCESS(Status))
    {
        ZpClient_WriteLog(L"network.log",
                          "event=LoadRootCertificate",
                          ZpStatus_FromNtStatus(Status));
        return (int)Status;
    }
    Config.Endpoints = &Endpoint;
    Config.EndpointCount = 1;
    Config.DeploymentRootCertificate = RootCertificate;
    Config.DeploymentRootCertificateLength = RootCertificateLength;
    Config.StateCallback = ZpClient_StateCallback;
    Config.OperationCallback = ZpClient_OperationCallback;
    Config.ClientKeyScope = ZpClientKeyUser;
    Status = ZpClient_Create(&Config, &Client);
    Mem_Free(RootCertificate);
    if (!NT_SUCCESS(Status))
    {
        ZpClient_WriteLog(L"network.log",
                          "event=CreateClient",
                          ZpStatus_FromNtStatus(Status));
        return (int)Status;
    }
    SetConsoleCtrlHandler(ZpClient_ConsoleHandler, TRUE);
    Status = ZpClient_Start(Client);
    if (NT_SUCCESS(Status))
    {
        WaitForSingleObject(ZpClientStopEvent, INFINITE);
        Status = ZpClient_Stop(Client);
        if (NT_SUCCESS(Status))
        {
            WaitForSingleObject(ZpClientStoppedEvent, INFINITE);
        }
    }
    else
    {
        ZpClient_WriteLog(L"network.log",
                          "event=StartClient",
                          ZpStatus_FromNtStatus(Status));
    }
    ZpClient_Close(Client);
    ZpClient_CloseLogs();
    CloseHandle(ZpClientStoppedEvent);
    CloseHandle(ZpClientStopEvent);
    return (int)Status;
}
