#pragma once

#include <KNSoft/ZPigeon/SDK.h>
#include <KNSoft/ZPigeon/Operations.h>

#include <Wincrypt.h>

#pragma comment(lib, "Crypt32.lib")

EXTERN_C_START

#define ZP_SERVER_DEFAULT_MAX_REQUESTS_PER_CONNECTION 64
#define ZP_SERVER_MAX_REQUESTS_PER_CONNECTION 4096
#define ZP_SERVER_DEFAULT_MAX_CHANNELS_PER_CONNECTION 16
#define ZP_SERVER_MAX_CHANNELS_PER_CONNECTION 1024
#define ZP_SERVER_DEFAULT_CHANNEL_WINDOW_SIZE ZP_CHANNEL_DATA_MAX_SIZE

typedef enum _ZP_SERVER_STATE
{
    ZpServerStateStopped,
    ZpServerStateStarting,
    ZpServerStateRunning,
    ZpServerStateStopping
} ZP_SERVER_STATE, *PZP_SERVER_STATE;

typedef
VOID
(NTAPI *ZP_SERVER_STATE_CALLBACK)(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_SERVER_STATE State,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context);

typedef
VOID
(NTAPI *ZP_SERVER_CONNECTION_CALLBACK)(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_CONNECTION_PHASE Phase,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context);

typedef struct _ZP_SERVER_DEPLOYMENT
{
    PCWSTR ServerName;
    PCCERT_CONTEXT Certificate;
} ZP_SERVER_DEPLOYMENT, *PZP_SERVER_DEPLOYMENT;

typedef const ZP_SERVER_DEPLOYMENT* PCZP_SERVER_DEPLOYMENT;

typedef struct _ZP_SERVER_CONFIG
{
    PCZP_LISTENER_ENDPOINT Listeners;
    ULONG ListenerCount;
    PCZP_SERVER_DEPLOYMENT Deployments;
    ULONG DeploymentCount;
    PCZP_MODULE_RECORD Modules;
    USHORT ModuleCount;
    ULONG MaxRequestsPerConnection;
    ULONG MaxChannelsPerConnection;
    ZP_SERVER_STATE_CALLBACK StateCallback;
    ZP_SERVER_CONNECTION_CALLBACK ConnectionCallback;
    PVOID CallbackContext;
} ZP_SERVER_CONFIG, *PZP_SERVER_CONFIG;

typedef const ZP_SERVER_CONFIG* PCZP_SERVER_CONFIG;

NTSTATUS
NTAPI
ZpServer_Create(
    _In_ PCZP_SERVER_CONFIG Config,
    _Out_ ZP_SERVER_HANDLE* Server);

ZP_STATUS
NTAPI
ZpServer_Start(
    _In_ ZP_SERVER_HANDLE Server);

NTSTATUS
NTAPI
ZpServer_Stop(
    _In_ ZP_SERVER_HANDLE Server);

NTSTATUS
NTAPI
ZpServer_Close(
    _In_ ZP_SERVER_HANDLE Server);

NTSTATUS
NTAPI
ZpServer_OpenTunnel(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(HostLength) PCWCH Host,
    _In_ ULONG HostLength,
    _In_ USHORT Port,
    _In_ USHORT Protocol,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_TUNNEL_OPEN_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateBrowsers(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_BROWSER_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryBrowser(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_BROWSER_TYPE Browser,
    _In_ ZP_BROWSER_KIND Kind,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_ ULONGLONG Cursor,
    _In_ ULONG Limit,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_BROWSER_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateWmiNamespaces(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WMI_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateWmiClasses(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WMI_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryWmi(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_reads_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_ ULONG Limit,
    _In_ ULONG Flags,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WMI_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_SendRequest(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE ModuleId,
    _In_ BYTE OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryFile(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_QUERY_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryFileSecurity(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

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
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ResolveAccountName(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ResolveAccountSid(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(SidLength) PCWCH Sid,
    _In_ ULONG SidLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

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
    _Out_ ZP_REQUEST_HANDLE* Request);

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
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_DeleteFile(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

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
    _Out_ ZP_REQUEST_HANDLE* Request);

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
    _Out_ ZP_REQUEST_HANDLE* Request);

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
    _Out_ ZP_REQUEST_HANDLE* Request);

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
    _Out_ ZP_REQUEST_HANDLE* Request);

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
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryEventLogPage(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_EVENT_LOG_START_MODE StartMode,
    _In_ ULONG MaxEvents,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_reads_opt_(BookmarkLength) PCWCH Bookmark,
    _In_ ULONG BookmarkLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EVENT_LOG_QUERY_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateEventLogChannels(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EVENT_LOG_CHANNELS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryEventLogChannelInfo(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EVENT_LOG_CHANNEL_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_SetEventLogChannelEnabled(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ BOOLEAN Enabled,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ClearEventLog(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ConfigureEventLogChannel(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ChannelPathLength) PCWCH ChannelPath,
    _In_ ULONG ChannelPathLength,
    _In_ BOOLEAN Enabled,
    _In_ ZP_EVENT_LOG_RETENTION_MODE RetentionMode,
    _In_ ULONGLONG MaximumSize,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

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
    _Out_ ZP_REQUEST_HANDLE* Request);

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
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryTerminalShells(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_TERMINAL_SHELLS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_GetSystemInfo(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_SYSTEM_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateProcesses(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_PROCESS_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryProcess(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_PROCESS_QUERY_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ControlProcess(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ZP_PROCESS_CONTROL Control,
    _In_ ULONG Value,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_CreateProcessDump(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONG DumpType,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_PROCESS_DUMP_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ReadProcessMemory(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONGLONG Address,
    _In_ ULONG Length,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_PROCESS_MEMORY_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_WriteProcessMemory(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONGLONG Address,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateWindows(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WINDOW_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryWindow(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WINDOW_QUERY_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ControlWindow(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ZP_WINDOW_CONTROL Control,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_UpdateWindow(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ PCZP_WINDOW_UPDATE Update,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_CaptureWindow(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WINDOW_CAPTURE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_OpenWindowCapture(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_WINDOW_CAPTURE_OPEN_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateAudioDevices(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_AUDIO_DEVICES_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateAudioSessions(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_AUDIO_SESSIONS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ControlAudioEndpoint(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_AUDIO_FLOW Flow,
    _In_ ZP_AUDIO_ENDPOINT_CONTROL Control,
    _In_ ULONG Value,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ControlAudioSession(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_AUDIO_SESSION_CONTROL Control,
    _In_ ULONG Value,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(SessionIdLength) PCWCH SessionId,
    _In_ ULONG SessionIdLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_OpenAudioStream(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_AUDIO_FLOW Flow,
    _In_ ULONG DirectStreamId,
    _In_reads_opt_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_AUDIO_STREAM_OPEN_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateVideoDevices(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_VIDEO_DEVICES_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_OpenVideoStream(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_ ULONG MaxDimension,
    _In_ USHORT FrameRate,
    _In_ USHORT Quality,
    _In_ ULONG DirectStreamId,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_VIDEO_STREAM_OPEN_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateAdministration(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_ADMINISTRATION_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryAdministration(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_reads_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_ADMINISTRATION_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ControlAdministration(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_ ZP_ADMINISTRATION_ACTION Action,
    _In_reads_opt_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_reads_opt_(ArgumentLength) PCWCH Argument,
    _In_ ULONG ArgumentLength,
    _In_reads_opt_(SecretLength) PCWCH Secret,
    _In_ ULONG SecretLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateServices(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_SERVICE_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryFileVolume(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_VOLUME_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

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
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateExecutionSessions(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EXECUTION_SESSIONS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_StartExecution(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ PCZP_EXECUTION_START Start,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EXECUTION_JOBS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateExecutionJobs(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EXECUTION_JOBS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_TerminateExecution(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG JobId,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_CreateExecutionStaging(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_EXECUTION_STAGING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryService(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_SERVICE_QUERY_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ControlService(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG Control,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_reads_opt_(ArgumentLength) PCWCH Argument,
    _In_ ULONG ArgumentLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ConfigureService(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ PCZP_SERVICE_CONFIG Config,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ConfigureServiceRecovery(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ PCZP_SERVICE_RECOVERY_CONFIG Config,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_ConfigureServiceAccount(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ PCZP_SERVICE_ACCOUNT_CONFIG Config,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateRegistryKeysPage(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _In_ ULONG MaxEntries,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REGISTRY_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_EnumerateRegistryValuesPage(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(CursorLength) PCWCH Cursor,
    _In_ ULONG CursorLength,
    _In_ ULONG MaxEntries,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REGISTRY_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryRegistryValue(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REGISTRY_VALUE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryRegistryValueRange(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REGISTRY_RANGE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_QueryRegistrySecurity(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REGISTRY_VALUE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_SetRegistrySecurity(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(SddlLength) PCWCH Sddl,
    _In_ ULONG SddlLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_SetRegistryValue(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_WriteRegistryValueRange(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _In_ ULONG Offset,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_DeleteRegistryValue(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(ValueNameLength) PCWCH ValueName,
    _In_ ULONG ValueNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_CreateRegistryKey(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_DeleteRegistryKey(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_RenameRegistryKey(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_reads_(NewNameLength) PCWCH NewName,
    _In_ ULONG NewNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_RenameRegistryValue(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_REGISTRY_ROOT Root,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_reads_(NewNameLength) PCWCH NewName,
    _In_ ULONG NewNameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_OpenRtc(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ZP_RTC_SESSION_ID_SIZE) const BYTE* SessionId,
    _In_reads_(OfferLength) PCWCH Offer,
    _In_ ULONG OfferLength,
    _In_reads_opt_(IceServerCount) PCZP_RTC_ICE_SERVER IceServers,
    _In_ ULONG IceServerCount,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

NTSTATUS
NTAPI
ZpServer_CloseRtc(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(ZP_RTC_SESSION_ID_SIZE) const BYTE* SessionId,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request);

VOID
NTAPI
ZpConnection_AddRef(
    _Inout_ ZP_CONNECTION_HANDLE Connection);

VOID
NTAPI
ZpConnection_Release(
    _Inout_ ZP_CONNECTION_HANDLE Connection);

EXTERN_C_END
