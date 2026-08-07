#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_TERMINAL_MODULE_ID 5
#define ZP_TERMINAL_MODULE_VERSION 1
#define ZP_TERMINAL_OPERATION_CREATE 1
#define ZP_TERMINAL_OPERATION_RESIZE 2

typedef struct _ZP_TERMINAL_CREATE_VIEW
{
    USHORT Columns;
    USHORT Rows;
    ZP_STRING_VIEW CommandLine;
    ZP_STRING_VIEW WorkingDirectory;
} ZP_TERMINAL_CREATE_VIEW, *PZP_TERMINAL_CREATE_VIEW;

NTSTATUS
ZpTerminal_EncodeCreate(
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _In_reads_(CommandLineLength) PCWCH CommandLine,
    _In_ ULONG CommandLineLength,
    _In_reads_opt_(WorkingDirectoryLength) PCWCH WorkingDirectory,
    _In_ ULONG WorkingDirectoryLength,
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
    _In_ ULONGLONG ChannelId,
    _In_ ULONG ProcessId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpTerminal_DecodeCreateResponse(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG ChannelId,
    _Out_ PULONG ProcessId);

NTSTATUS
ZpTerminal_EncodeResize(
    _In_ ULONGLONG ChannelId,
    _In_ USHORT Columns,
    _In_ USHORT Rows,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpTerminal_DecodeResize(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG ChannelId,
    _Out_ PUSHORT Columns,
    _Out_ PUSHORT Rows);

EXTERN_C_END
