#include <KNSoft/MakeLifeEasier/IO/File.h>
#include <KNSoft/ZPigeon/Client.h>
#include <KNSoft/ZPigeon/EventLog.h>
#include <KNSoft/ZPigeon/File.h>
#include <KNSoft/ZPigeon/Process.h>
#include <KNSoft/ZPigeon/Registry.h>
#include <KNSoft/ZPigeon/Service.h>
#include <KNSoft/ZPigeon/System.h>
#include <KNSoft/ZPigeon/Terminal.h>

#include <stdio.h>

#define ZP_CLIENT_PORT 4433

static WCHAR ZpClientDirectory[MAX_PATH];
static HANDLE ZpClientStopEvent;
static HANDLE ZpClientStoppedEvent;

static
PCWSTR
ZpClient_GetModuleLogName(
    _In_ USHORT ModuleId)
{
    switch (ModuleId)
    {
        case ZP_SYSTEM_MODULE_ID: return L"system.log";
        case ZP_PROCESS_MODULE_ID: return L"process.log";
        case ZP_SERVICE_MODULE_ID: return L"service.log";
        case ZP_FILE_MODULE_ID: return L"file.log";
        case ZP_TERMINAL_MODULE_ID: return L"terminal.log";
        case ZP_EVENT_LOG_MODULE_ID: return L"eventlog.log";
        case ZP_REGISTRY_MODULE_ID: return L"registry.log";
        default: return L"modules.log";
    }
}

static
VOID
ZpClient_WriteLog(
    _In_ PCWSTR FileName,
    _In_ ULONG Value1,
    _In_ ULONG Value2,
    _In_ NTSTATUS Status)
{
    WCHAR Path[MAX_PATH];
    CHAR Line[128];
    LARGE_INTEGER Offset;
    HANDLE FileHandle;
    int Length;

    if (_snwprintf_s(Path,
                     ARRAYSIZE(Path),
                     _TRUNCATE,
                     L"%slogs\\%s",
                     ZpClientDirectory,
                     FileName) < 0)
    {
        return;
    }
    Length = sprintf_s(Line,
                       sizeof(Line),
                       "%llu %lu %lu 0x%08lX\r\n",
                       GetTickCount64(),
                       Value1,
                       Value2,
                       Status);
    if (Length < 0)
    {
        return;
    }
    if (NT_SUCCESS(IO_CreateWin32File(&FileHandle,
                                      Path,
                                      NULL,
                                      FILE_APPEND_DATA | SYNCHRONIZE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE |
                                          FILE_SHARE_DELETE,
                                      FILE_OPEN_IF,
                                      FILE_NON_DIRECTORY_FILE |
                                          FILE_SYNCHRONOUS_IO_NONALERT)))
    {
        Offset.QuadPart = -1;
        IO_WriteFile(FileHandle, &Offset, Line, (ULONG)Length, NULL);
        NtClose(FileHandle);
    }
}

static
VOID
NTAPI
ZpClient_StateCallback(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Client);
    UNREFERENCED_PARAMETER(Context);
    ZpClient_WriteLog(L"network.log", State, 0, Status);
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
    _In_ USHORT ModuleId,
    _In_ USHORT OperationId,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Client);
    UNREFERENCED_PARAMETER(Context);
    ZpClient_WriteLog(ZpClient_GetModuleLogName(ModuleId),
                      ModuleId,
                      OperationId,
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
    static const ZP_MODULE_RECORD Modules[] = {
        { ZP_SYSTEM_MODULE_ID, ZP_SYSTEM_MODULE_VERSION },
        { ZP_PROCESS_MODULE_ID, ZP_PROCESS_MODULE_VERSION },
        { ZP_SERVICE_MODULE_ID, ZP_SERVICE_MODULE_VERSION },
        { ZP_FILE_MODULE_ID, ZP_FILE_MODULE_VERSION },
        { ZP_TERMINAL_MODULE_ID, ZP_TERMINAL_MODULE_VERSION },
        { ZP_EVENT_LOG_MODULE_ID, ZP_EVENT_LOG_MODULE_VERSION },
        { ZP_REGISTRY_MODULE_ID, ZP_REGISTRY_MODULE_VERSION }
    };
    static const ZP_ENDPOINT Endpoint = {
        ZpTransportQuic,
        L"127.0.0.1",
        ZP_CLIENT_PORT,
        L"localhost",
        NULL
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
        ZpClient_WriteLog(L"network.log", 0, 0, Status);
        return (int)Status;
    }
    Config.Size = sizeof(Config);
    Config.Endpoints = &Endpoint;
    Config.EndpointCount = 1;
    Config.DeploymentRootCertificate = RootCertificate;
    Config.DeploymentRootCertificateLength = RootCertificateLength;
    Config.Modules = Modules;
    Config.ModuleCount = ARRAYSIZE(Modules);
    Config.StateCallback = ZpClient_StateCallback;
    Config.OperationCallback = ZpClient_OperationCallback;
    Config.ClientKeyScope = ZpClientKeyUser;
    Status = ZpClient_Create(&Config, &Client);
    Mem_Free(RootCertificate);
    if (!NT_SUCCESS(Status))
    {
        ZpClient_WriteLog(L"network.log", 0, 0, Status);
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
    ZpClient_Close(Client);
    CloseHandle(ZpClientStoppedEvent);
    CloseHandle(ZpClientStopEvent);
    return (int)Status;
}
