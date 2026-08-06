#pragma once

#include <KNSoft/ZPigeon/Execution.h>

EXTERN_C_START

#define ZP_TERMINAL_MODULE_ID 5
#define ZP_TERMINAL_OPERATION_CREATE 1
#define ZP_TERMINAL_OPERATION_RESIZE 2
#define ZP_TERMINAL_OPERATION_QUERY_SHELLS 3

typedef BYTE ZP_TERMINAL_SHELL, *PZP_TERMINAL_SHELL;

#define ZpTerminalShellCommandPrompt ((ZP_TERMINAL_SHELL)0x01)
#define ZpTerminalShellWindowsPowerShell ((ZP_TERMINAL_SHELL)0x02)
#define ZpTerminalShellPowerShell ((ZP_TERMINAL_SHELL)0x04)

#define ZP_TERMINAL_SHELL_MASK 0x07

typedef struct _ZP_TERMINAL_CREATE_VIEW
{
    USHORT Columns;
    USHORT Rows;
    ZP_EXECUTION_START_VIEW Start;
} ZP_TERMINAL_CREATE_VIEW, *PZP_TERMINAL_CREATE_VIEW;

NTSTATUS
ZpTerminal_EncodeCreate(
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _In_ PCZP_EXECUTION_START Start,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpTerminal_DecodeCreate(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_TERMINAL_CREATE_VIEW View);

NTSTATUS
ZpTerminal_EncodeCreateResponse(
    _In_ ULONG ChannelId,
    _In_ ULONG ProcessId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpTerminal_DecodeCreateResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ChannelId,
    _Out_ PULONG ProcessId);

NTSTATUS
ZpTerminal_EncodeResize(
    _In_ ULONG ChannelId,
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpTerminal_DecodeResize(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ChannelId,
    _Out_ PUSHORT Columns,
    _Out_ PUSHORT Rows);

NTSTATUS
ZpTerminal_EncodeShells(
    _In_ BYTE Shells,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpTerminal_DecodeShells(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PBYTE Shells);

EXTERN_C_END
