#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_WMI_MODULE_ID 13

#define ZP_WMI_OPERATION_ENUMERATE_NAMESPACES 1
#define ZP_WMI_OPERATION_ENUMERATE_CLASSES 2
#define ZP_WMI_OPERATION_QUERY 3

#define ZP_WMI_FLAG_SYSTEM_PROPERTIES 0x00000001UL
#define ZP_WMI_MAX_NAMESPACE_LENGTH 512
#define ZP_WMI_MAX_QUERY_LENGTH 32768
#define ZP_WMI_MAX_ROWS 4096
#define ZP_WMI_MAX_QUERY_ROWS 1000
#define ZP_WMI_MAX_CELLS 1024
#define ZP_WMI_MAX_CELL_LENGTH 65536

typedef struct _ZP_WMI_CELL
{
    USHORT Type;
    PCWCH Name;
    ULONG NameLength;
    PCWCH Value;
    ULONG ValueLength;
} ZP_WMI_CELL, *PZP_WMI_CELL;

typedef const ZP_WMI_CELL* PCZP_WMI_CELL;

typedef struct _ZP_WMI_ROW
{
    PCZP_WMI_CELL Cells;
    ULONG CellCount;
} ZP_WMI_ROW, *PZP_WMI_ROW;

typedef const ZP_WMI_ROW* PCZP_WMI_ROW;

typedef struct _ZP_WMI_ROW_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG CellCount;
} ZP_WMI_ROW_VIEW, *PZP_WMI_ROW_VIEW;

typedef const ZP_WMI_ROW_VIEW* PCZP_WMI_ROW_VIEW;

typedef struct _ZP_WMI_PAGE_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG RowCount;
} ZP_WMI_PAGE_VIEW, *PZP_WMI_PAGE_VIEW;

typedef const ZP_WMI_PAGE_VIEW* PCZP_WMI_PAGE_VIEW;

typedef struct _ZP_WMI_REQUEST_VIEW
{
    ULONG Limit;
    ULONG Flags;
    ZP_STRING_VIEW Namespace;
    ZP_STRING_VIEW Query;
} ZP_WMI_REQUEST_VIEW, *PZP_WMI_REQUEST_VIEW;

typedef const ZP_WMI_REQUEST_VIEW* PCZP_WMI_REQUEST_VIEW;

NTSTATUS
ZpWmi_EncodeCell(
    _In_ USHORT Type,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_reads_opt_(ValueLength) PCWCH Value,
    _In_ ULONG ValueLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpWmi_EncodePageHeader(
    _In_ ULONG RowCount,
    _Out_writes_bytes_(sizeof(ULONG)) PVOID Buffer);

NTSTATUS
ZpWmi_EncodeRowHeader(
    _In_ ULONG CellCount,
    _Out_writes_bytes_(sizeof(ULONG)) PVOID Buffer);

NTSTATUS
ZpWmi_EncodePage(
    _In_reads_opt_(RowCount) PCZP_WMI_ROW Rows,
    _In_ ULONG RowCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpWmi_DecodePage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_WMI_PAGE_VIEW Page);

NTSTATUS
ZpWmi_GetNextRow(
    _In_ PCZP_WMI_PAGE_VIEW Page,
    _Inout_ PULONG Offset,
    _Out_ PZP_WMI_ROW_VIEW Row);

NTSTATUS
ZpWmi_GetNextCell(
    _In_ PCZP_WMI_ROW_VIEW Row,
    _Inout_ PULONG Offset,
    _Out_ PZP_WMI_CELL Cell);

NTSTATUS
ZpWmi_EncodeRequest(
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_ ULONG Limit,
    _In_ ULONG Flags,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpWmi_DecodeRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_WMI_REQUEST_VIEW Request);

EXTERN_C_END
