#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_EXECUTION_MODULE_ID 10

#define ZP_EXECUTION_OPERATION_ENUMERATE_SESSIONS 1
#define ZP_EXECUTION_OPERATION_START 2
#define ZP_EXECUTION_OPERATION_ENUMERATE_JOBS 3
#define ZP_EXECUTION_OPERATION_TERMINATE 4
#define ZP_EXECUTION_OPERATION_CREATE_STAGING 5
#define ZP_EXECUTION_OPERATION_QUERY_ENVIRONMENT 6
#define ZP_EXECUTION_OPERATION_QUERY_IMAGE 7

typedef BYTE ZP_EXECUTION_ENGINE, *PZP_EXECUTION_ENGINE;

#define ZpExecutionEngineCreateProcess ((ZP_EXECUTION_ENGINE)1)
#define ZpExecutionEngineShellExecute ((ZP_EXECUTION_ENGINE)2)

typedef BYTE ZP_EXECUTION_IDENTITY, *PZP_EXECUTION_IDENTITY;

#define ZpExecutionIdentityCurrent ((ZP_EXECUTION_IDENTITY)1)
#define ZpExecutionIdentityInteractive ((ZP_EXECUTION_IDENTITY)2)
#define ZpExecutionIdentityAdministrator ((ZP_EXECUTION_IDENTITY)3)
#define ZpExecutionIdentitySystem ((ZP_EXECUTION_IDENTITY)4)
#define ZpExecutionIdentityTrustedInstaller ((ZP_EXECUTION_IDENTITY)5)
#define ZpExecutionIdentityOtherUser ((ZP_EXECUTION_IDENTITY)6)
#define ZpExecutionIdentityAppContainer ((ZP_EXECUTION_IDENTITY)7)
#define ZpExecutionIdentityCustomToken ((ZP_EXECUTION_IDENTITY)8)

typedef BYTE ZP_EXECUTION_RUNTIME, *PZP_EXECUTION_RUNTIME;

#define ZpExecutionRuntimeCommandPrompt ((ZP_EXECUTION_RUNTIME)1)
#define ZpExecutionRuntimeWindowsPowerShell ((ZP_EXECUTION_RUNTIME)2)
#define ZpExecutionRuntimePowerShell ((ZP_EXECUTION_RUNTIME)3)
#define ZpExecutionRuntimeConsoleScriptHost ((ZP_EXECUTION_RUNTIME)4)
#define ZpExecutionRuntimeWindowsScriptHost ((ZP_EXECUTION_RUNTIME)5)
#define ZpExecutionRuntimeHtmlApplication ((ZP_EXECUTION_RUNTIME)6)
#define ZpExecutionRuntimeNode ((ZP_EXECUTION_RUNTIME)7)
#define ZpExecutionRuntimePython ((ZP_EXECUTION_RUNTIME)8)
#define ZpExecutionRuntimePythonWindow ((ZP_EXECUTION_RUNTIME)9)
#define ZpExecutionRuntimeGo ((ZP_EXECUTION_RUNTIME)10)

#define ZP_EXECUTION_ENVIRONMENT_FLAG_ADMINISTRATOR 0x00000001UL

#define ZP_EXECUTION_CUSTOM_TOKEN_VERSION 1
#define ZP_EXECUTION_CUSTOM_TOKEN_FLAG_UI_ACCESS 0x00000001UL
#define ZP_EXECUTION_CUSTOM_TOKEN_FLAG_ADD_LOGON_SID 0x00000002UL

#define ZP_EXECUTION_SESSION_CURRENT MAXULONG
#define ZP_EXECUTION_SESSION_FLAG_CLIENT 0x00000001UL
#define ZP_EXECUTION_SESSION_FLAG_ACTIVE 0x00000002UL
#define ZP_EXECUTION_FLAG_HIDDEN 0x00000001UL
#define ZP_EXECUTION_FLAG_DELETE_FILE 0x00000002UL
#define ZP_EXECUTION_FLAG_JOB_OBJECT 0x00000004UL

typedef BYTE ZP_EXECUTION_JOB_STATE, *PZP_EXECUTION_JOB_STATE;

#define ZpExecutionJobRunning ((ZP_EXECUTION_JOB_STATE)1)
#define ZpExecutionJobExited ((ZP_EXECUTION_JOB_STATE)2)

typedef struct _ZP_EXECUTION_SESSION_RECORD
{
    ULONG SessionId;
    ULONG State;
    ULONG Flags;
    PCWCH StationName;
    ULONG StationNameLength;
    PCWCH UserName;
    ULONG UserNameLength;
} ZP_EXECUTION_SESSION_RECORD, *PZP_EXECUTION_SESSION_RECORD;

typedef const ZP_EXECUTION_SESSION_RECORD* PCZP_EXECUTION_SESSION_RECORD;

typedef struct _ZP_EXECUTION_SESSION_RECORD_VIEW
{
    ULONG SessionId;
    ULONG State;
    ULONG Flags;
    ZP_STRING_VIEW StationName;
    ZP_STRING_VIEW UserName;
} ZP_EXECUTION_SESSION_RECORD_VIEW, *PZP_EXECUTION_SESSION_RECORD_VIEW;

typedef struct _ZP_EXECUTION_SESSION_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_EXECUTION_SESSION_LIST_VIEW, *PZP_EXECUTION_SESSION_LIST_VIEW;

typedef const ZP_EXECUTION_SESSION_LIST_VIEW* PCZP_EXECUTION_SESSION_LIST_VIEW;

typedef struct _ZP_EXECUTION_IMAGE_INFO
{
    USHORT Machine;
    USHORT Subsystem;
    USHORT Version[4];
} ZP_EXECUTION_IMAGE_INFO, *PZP_EXECUTION_IMAGE_INFO;

typedef const ZP_EXECUTION_IMAGE_INFO* PCZP_EXECUTION_IMAGE_INFO;

typedef struct _ZP_EXECUTION_RUNTIME_RECORD
{
    ZP_EXECUTION_RUNTIME Kind;
    ZP_EXECUTION_IMAGE_INFO Image;
    PCWCH Path;
    ULONG PathLength;
} ZP_EXECUTION_RUNTIME_RECORD, *PZP_EXECUTION_RUNTIME_RECORD;

typedef const ZP_EXECUTION_RUNTIME_RECORD* PCZP_EXECUTION_RUNTIME_RECORD;

typedef struct _ZP_EXECUTION_RUNTIME_RECORD_VIEW
{
    ZP_EXECUTION_RUNTIME Kind;
    ZP_EXECUTION_IMAGE_INFO Image;
    ZP_STRING_VIEW Path;
} ZP_EXECUTION_RUNTIME_RECORD_VIEW, *PZP_EXECUTION_RUNTIME_RECORD_VIEW;

typedef struct _ZP_EXECUTION_ENVIRONMENT_VIEW
{
    ULONG Flags;
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_EXECUTION_ENVIRONMENT_VIEW, *PZP_EXECUTION_ENVIRONMENT_VIEW;

typedef const ZP_EXECUTION_ENVIRONMENT_VIEW* PCZP_EXECUTION_ENVIRONMENT_VIEW;

typedef struct _ZP_EXECUTION_START
{
    ZP_EXECUTION_ENGINE Engine;
    ZP_EXECUTION_IDENTITY Identity;
    ULONG SessionId;
    ULONG Flags;
    PCWCH FileName;
    ULONG FileNameLength;
    PCWCH Arguments;
    ULONG ArgumentsLength;
    PCWCH WorkingDirectory;
    ULONG WorkingDirectoryLength;
    PCWCH Verb;
    ULONG VerbLength;
    PCWCH UserName;
    ULONG UserNameLength;
    PCWCH Password;
    ULONG PasswordLength;
    PCWCH AppContainerSid;
    ULONG AppContainerSidLength;
    const BYTE* CustomToken;
    ULONG CustomTokenLength;
} ZP_EXECUTION_START, *PZP_EXECUTION_START;

typedef const ZP_EXECUTION_START* PCZP_EXECUTION_START;

typedef struct _ZP_EXECUTION_START_VIEW
{
    ZP_EXECUTION_ENGINE Engine;
    ZP_EXECUTION_IDENTITY Identity;
    ULONG SessionId;
    ULONG Flags;
    ZP_STRING_VIEW FileName;
    ZP_STRING_VIEW Arguments;
    ZP_STRING_VIEW WorkingDirectory;
    ZP_STRING_VIEW Verb;
    ZP_STRING_VIEW UserName;
    ZP_STRING_VIEW Password;
    ZP_STRING_VIEW AppContainerSid;
    ZP_BUFFER_VIEW CustomToken;
} ZP_EXECUTION_START_VIEW, *PZP_EXECUTION_START_VIEW;

typedef const ZP_EXECUTION_START_VIEW* PCZP_EXECUTION_START_VIEW;

typedef struct _ZP_EXECUTION_JOB_RECORD
{
    ULONG JobId;
    ULONGLONG CreateTime;
    ULONGLONG ExitTime;
    ULONG ProcessId;
    ULONG SessionId;
    ULONG ExitCode;
    ULONG Flags;
    ZP_EXECUTION_ENGINE Engine;
    ZP_EXECUTION_IDENTITY Identity;
    ZP_EXECUTION_JOB_STATE State;
    PCWCH FileName;
    ULONG FileNameLength;
} ZP_EXECUTION_JOB_RECORD, *PZP_EXECUTION_JOB_RECORD;

typedef const ZP_EXECUTION_JOB_RECORD* PCZP_EXECUTION_JOB_RECORD;

typedef struct _ZP_EXECUTION_JOB_RECORD_VIEW
{
    ULONG JobId;
    ULONGLONG CreateTime;
    ULONGLONG ExitTime;
    ULONG ProcessId;
    ULONG SessionId;
    ULONG ExitCode;
    ULONG Flags;
    ZP_EXECUTION_ENGINE Engine;
    ZP_EXECUTION_IDENTITY Identity;
    ZP_EXECUTION_JOB_STATE State;
    ZP_STRING_VIEW FileName;
} ZP_EXECUTION_JOB_RECORD_VIEW, *PZP_EXECUTION_JOB_RECORD_VIEW;

typedef struct _ZP_EXECUTION_JOB_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_EXECUTION_JOB_LIST_VIEW, *PZP_EXECUTION_JOB_LIST_VIEW;

typedef const ZP_EXECUTION_JOB_LIST_VIEW* PCZP_EXECUTION_JOB_LIST_VIEW;

NTSTATUS
ZpExecution_EncodeSessions(
    _In_reads_opt_(Count) PCZP_EXECUTION_SESSION_RECORD Records,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpExecution_DecodeSessions(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EXECUTION_SESSION_LIST_VIEW View);

NTSTATUS
ZpExecution_GetNextSession(
    _In_ PCZP_EXECUTION_SESSION_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_EXECUTION_SESSION_RECORD_VIEW Record);

NTSTATUS
ZpExecution_EncodeEnvironment(
    _In_ ULONG Flags,
    _In_reads_opt_(Count) PCZP_EXECUTION_RUNTIME_RECORD Records,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpExecution_DecodeEnvironment(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EXECUTION_ENVIRONMENT_VIEW View);

NTSTATUS
ZpExecution_GetNextRuntime(
    _In_ PCZP_EXECUTION_ENVIRONMENT_VIEW Environment,
    _Inout_ PULONG Offset,
    _Out_ PZP_EXECUTION_RUNTIME_RECORD_VIEW Record);

NTSTATUS
ZpExecution_EncodeImageInfo(
    _In_ PCZP_EXECUTION_IMAGE_INFO Image,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpExecution_DecodeImageInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EXECUTION_IMAGE_INFO Image);

NTSTATUS
ZpExecution_EncodeStart(
    _In_ PCZP_EXECUTION_START Start,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpExecution_DecodeStart(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EXECUTION_START_VIEW Start);

NTSTATUS
ZpExecution_EncodeJobs(
    _In_reads_opt_(Count) PCZP_EXECUTION_JOB_RECORD Records,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpExecution_DecodeJobs(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EXECUTION_JOB_LIST_VIEW View);

NTSTATUS
ZpExecution_GetNextJob(
    _In_ PCZP_EXECUTION_JOB_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_EXECUTION_JOB_RECORD_VIEW Record);

NTSTATUS
ZpExecution_EncodeJobId(
    _In_ ULONG JobId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpExecution_DecodeJobId(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG JobId);

NTSTATUS
ZpExecution_EncodeStaging(
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpExecution_DecodeStaging(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Name);

EXTERN_C_END
