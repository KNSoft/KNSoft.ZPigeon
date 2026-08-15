#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_SERVICE_MODULE_ID 3
#define ZP_SERVICE_MODULE_VERSION 1
#define ZP_SERVICE_OPERATION_ENUMERATE 1
#define ZP_SERVICE_OPERATION_QUERY 2
#define ZP_SERVICE_OPERATION_CONTROL 3
#define ZP_SERVICE_OPERATION_CONFIGURE_GENERAL 4
#define ZP_SERVICE_OPERATION_CONFIGURE_RECOVERY 5
#define ZP_SERVICE_OPERATION_CONFIGURE_ACCOUNT 6

#define ZP_SERVICE_CONTROL_START 1
#define ZP_SERVICE_CONTROL_STOP 2
#define ZP_SERVICE_CONTROL_PAUSE 3
#define ZP_SERVICE_CONTROL_CONTINUE 4
#define ZP_SERVICE_CONTROL_RESTART 5

typedef struct _ZP_SERVICE_RECORD
{
    ULONG ServiceType;
    ULONG CurrentState;
    ULONG ControlsAccepted;
    ULONG ProcessId;
    ULONG StartType;
    PCWCH ServiceName;
    ULONG ServiceNameLength;
    PCWCH DisplayName;
    ULONG DisplayNameLength;
    PCWCH Description;
    ULONG DescriptionLength;
    PCWCH StartName;
    ULONG StartNameLength;
} ZP_SERVICE_RECORD, *PZP_SERVICE_RECORD;

typedef const ZP_SERVICE_RECORD* PCZP_SERVICE_RECORD;

typedef struct _ZP_SERVICE_RECORD_VIEW
{
    ULONG ServiceType;
    ULONG CurrentState;
    ULONG ControlsAccepted;
    ULONG ProcessId;
    ULONG StartType;
    ZP_STRING_VIEW ServiceName;
    ZP_STRING_VIEW DisplayName;
    ZP_STRING_VIEW Description;
    ZP_STRING_VIEW StartName;
} ZP_SERVICE_RECORD_VIEW, *PZP_SERVICE_RECORD_VIEW;

typedef struct _ZP_SERVICE_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_SERVICE_LIST_VIEW, *PZP_SERVICE_LIST_VIEW;

typedef const ZP_SERVICE_LIST_VIEW* PCZP_SERVICE_LIST_VIEW;

typedef struct _ZP_SERVICE_INFO
{
    ULONG ServiceType;
    ULONG CurrentState;
    ULONG ControlsAccepted;
    ULONG ProcessId;
    ULONG StartType;
    ULONG ErrorControl;
    ULONG DelayedAutoStart;
    ULONG ServiceFlags;
    ULONG RecoverySupported;
    ULONG FailureActionsOnNonCrashFailures;
    ULONG RecoveryActionCount;
    ULONG ResetPeriodSeconds;
    ULONG RestartDelayMilliseconds;
    ULONG RebootDelayMilliseconds;
    ULONG FirstFailureAction;
    ULONG SecondFailureAction;
    ULONG ThirdFailureAction;
    ULONG SubsequentFailureAction;
    PCWCH ServiceName;
    ULONG ServiceNameLength;
    PCWCH DisplayName;
    ULONG DisplayNameLength;
    PCWCH Description;
    ULONG DescriptionLength;
    PCWCH BinaryPathName;
    ULONG BinaryPathNameLength;
    PCWCH StartName;
    ULONG StartNameLength;
    PCWCH LoadOrderGroup;
    ULONG LoadOrderGroupLength;
    PCWCH Dependencies;
    ULONG DependenciesLength;
    PCWCH Dependents;
    ULONG DependentsLength;
    PCWCH ServiceDll;
    ULONG ServiceDllLength;
    PCWCH RebootMessage;
    ULONG RebootMessageLength;
    PCWCH RecoveryCommand;
    ULONG RecoveryCommandLength;
} ZP_SERVICE_INFO, *PZP_SERVICE_INFO;

typedef const ZP_SERVICE_INFO* PCZP_SERVICE_INFO;

typedef struct _ZP_SERVICE_INFO_VIEW
{
    ULONG ServiceType;
    ULONG CurrentState;
    ULONG ControlsAccepted;
    ULONG ProcessId;
    ULONG StartType;
    ULONG ErrorControl;
    ULONG DelayedAutoStart;
    ULONG ServiceFlags;
    ULONG RecoverySupported;
    ULONG FailureActionsOnNonCrashFailures;
    ULONG RecoveryActionCount;
    ULONG ResetPeriodSeconds;
    ULONG RestartDelayMilliseconds;
    ULONG RebootDelayMilliseconds;
    ULONG FirstFailureAction;
    ULONG SecondFailureAction;
    ULONG ThirdFailureAction;
    ULONG SubsequentFailureAction;
    ZP_STRING_VIEW ServiceName;
    ZP_STRING_VIEW DisplayName;
    ZP_STRING_VIEW Description;
    ZP_STRING_VIEW BinaryPathName;
    ZP_STRING_VIEW StartName;
    ZP_STRING_VIEW LoadOrderGroup;
    ZP_STRING_VIEW Dependencies;
    ZP_STRING_VIEW Dependents;
    ZP_STRING_VIEW ServiceDll;
    ZP_STRING_VIEW RebootMessage;
    ZP_STRING_VIEW RecoveryCommand;
} ZP_SERVICE_INFO_VIEW, *PZP_SERVICE_INFO_VIEW;

typedef struct _ZP_SERVICE_CONFIG
{
    ULONG StartType;
    ULONG DelayedAutoStart;
    PCWCH ServiceName;
    ULONG ServiceNameLength;
    PCWCH DisplayName;
    ULONG DisplayNameLength;
    PCWCH Description;
    ULONG DescriptionLength;
    PCWCH BinaryPathName;
    ULONG BinaryPathNameLength;
    PCWCH LoadOrderGroup;
    ULONG LoadOrderGroupLength;
} ZP_SERVICE_CONFIG, *PZP_SERVICE_CONFIG;

typedef const ZP_SERVICE_CONFIG* PCZP_SERVICE_CONFIG;

typedef struct _ZP_SERVICE_CONFIG_VIEW
{
    ULONG StartType;
    ULONG DelayedAutoStart;
    ZP_STRING_VIEW ServiceName;
    ZP_STRING_VIEW DisplayName;
    ZP_STRING_VIEW Description;
    ZP_STRING_VIEW BinaryPathName;
    ZP_STRING_VIEW LoadOrderGroup;
} ZP_SERVICE_CONFIG_VIEW, *PZP_SERVICE_CONFIG_VIEW;

typedef const ZP_SERVICE_CONFIG_VIEW* PCZP_SERVICE_CONFIG_VIEW;

typedef struct _ZP_SERVICE_RECOVERY_CONFIG
{
    ULONG ErrorControl;
    ULONG FailureActionsOnNonCrashFailures;
    ULONG ResetPeriodSeconds;
    ULONG RestartDelayMilliseconds;
    ULONG RebootDelayMilliseconds;
    ULONG FirstFailureAction;
    ULONG SecondFailureAction;
    ULONG ThirdFailureAction;
    ULONG SubsequentFailureAction;
    PCWCH ServiceName;
    ULONG ServiceNameLength;
    PCWCH RebootMessage;
    ULONG RebootMessageLength;
    PCWCH Command;
    ULONG CommandLength;
} ZP_SERVICE_RECOVERY_CONFIG, *PZP_SERVICE_RECOVERY_CONFIG;

typedef const ZP_SERVICE_RECOVERY_CONFIG* PCZP_SERVICE_RECOVERY_CONFIG;

typedef struct _ZP_SERVICE_RECOVERY_CONFIG_VIEW
{
    ULONG ErrorControl;
    ULONG FailureActionsOnNonCrashFailures;
    ULONG ResetPeriodSeconds;
    ULONG RestartDelayMilliseconds;
    ULONG RebootDelayMilliseconds;
    ULONG FirstFailureAction;
    ULONG SecondFailureAction;
    ULONG ThirdFailureAction;
    ULONG SubsequentFailureAction;
    ZP_STRING_VIEW ServiceName;
    ZP_STRING_VIEW RebootMessage;
    ZP_STRING_VIEW Command;
} ZP_SERVICE_RECOVERY_CONFIG_VIEW, *PZP_SERVICE_RECOVERY_CONFIG_VIEW;

typedef const ZP_SERVICE_RECOVERY_CONFIG_VIEW* PCZP_SERVICE_RECOVERY_CONFIG_VIEW;

typedef struct _ZP_SERVICE_ACCOUNT_CONFIG
{
    ULONG PasswordPresent;
    PCWCH ServiceName;
    ULONG ServiceNameLength;
    PCWCH StartName;
    ULONG StartNameLength;
    PCWCH Password;
    ULONG PasswordLength;
} ZP_SERVICE_ACCOUNT_CONFIG, *PZP_SERVICE_ACCOUNT_CONFIG;

typedef const ZP_SERVICE_ACCOUNT_CONFIG* PCZP_SERVICE_ACCOUNT_CONFIG;

typedef struct _ZP_SERVICE_ACCOUNT_CONFIG_VIEW
{
    ULONG PasswordPresent;
    ZP_STRING_VIEW ServiceName;
    ZP_STRING_VIEW StartName;
    ZP_STRING_VIEW Password;
} ZP_SERVICE_ACCOUNT_CONFIG_VIEW, *PZP_SERVICE_ACCOUNT_CONFIG_VIEW;

typedef const ZP_SERVICE_ACCOUNT_CONFIG_VIEW* PCZP_SERVICE_ACCOUNT_CONFIG_VIEW;

NTSTATUS
ZpService_EncodeList(
    _In_reads_opt_(ServiceCount) PCZP_SERVICE_RECORD Services,
    _In_ ULONG ServiceCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpService_DecodeList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERVICE_LIST_VIEW View);

NTSTATUS
ZpService_GetRecord(
    _In_ PCZP_SERVICE_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_SERVICE_RECORD_VIEW Record);

NTSTATUS
ZpService_EncodeQuery(
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpService_DecodeQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW ServiceName);

NTSTATUS
ZpService_EncodeControl(
    _In_ ULONG Control,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_reads_opt_(ArgumentLength) PCWCH Argument,
    _In_ ULONG ArgumentLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpService_DecodeControl(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG Control,
    _Out_ PZP_STRING_VIEW ServiceName,
    _Out_ PZP_STRING_VIEW Argument);

NTSTATUS
ZpService_EncodeInfo(
    _In_ PCZP_SERVICE_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpService_DecodeInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERVICE_INFO_VIEW View);

NTSTATUS
ZpService_EncodeConfig(
    _In_ PCZP_SERVICE_CONFIG Config,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpService_DecodeConfig(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERVICE_CONFIG_VIEW Config);

NTSTATUS
ZpService_EncodeRecoveryConfig(
    _In_ PCZP_SERVICE_RECOVERY_CONFIG Config,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpService_DecodeRecoveryConfig(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERVICE_RECOVERY_CONFIG_VIEW Config);

NTSTATUS
ZpService_EncodeAccountConfig(
    _In_ PCZP_SERVICE_ACCOUNT_CONFIG Config,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpService_DecodeAccountConfig(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERVICE_ACCOUNT_CONFIG_VIEW Config);

EXTERN_C_END
