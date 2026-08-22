#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_PROCESS_MODULE_ID 2
#define ZP_PROCESS_MODULE_VERSION 1
#define ZP_PROCESS_OPERATION_ENUMERATE 1
#define ZP_PROCESS_OPERATION_QUERY 2
#define ZP_PROCESS_OPERATION_CONTROL 3
#define ZP_PROCESS_OPERATION_DUMP 4
#define ZP_PROCESS_OPERATION_READ_MEMORY 5
#define ZP_PROCESS_OPERATION_WRITE_MEMORY 6
#define ZP_PROCESS_OPERATION_QUERY_MEMORY_MAP 7
#define ZP_PROCESS_MEMORY_MAX_LENGTH 0x00010000UL

typedef struct _ZP_PROCESS_MEMORY_VIEW
{
    ULONG ProcessId;
    ULONGLONG CreateTime;
    ULONGLONG Address;
    ZP_BUFFER_VIEW Data;
} ZP_PROCESS_MEMORY_VIEW, *PZP_PROCESS_MEMORY_VIEW;

typedef const ZP_PROCESS_MEMORY_VIEW* PCZP_PROCESS_MEMORY_VIEW;

typedef struct _ZP_PROCESS_MEMORY_REGION
{
    ULONGLONG BaseAddress;
    ULONGLONG AllocationBase;
    ULONGLONG RegionSize;
    ULONGLONG CommitSize;
    ULONGLONG WorkingSetBytes;
    ULONGLONG PrivateWorkingSetBytes;
    ULONGLONG SharedWorkingSetBytes;
    ULONGLONG ShareableWorkingSetBytes;
    ULONGLONG LockedWorkingSetBytes;
    ULONGLONG SharedOriginalBytes;
    ULONG State;
    ULONG Type;
    ULONG Protect;
    ULONG AllocationProtect;
    ULONG RegionType;
    ULONG Priority;
    NTSTATUS RegionStatus;
    NTSTATUS WorkingSetStatus;
    NTSTATUS MappedPathStatus;
    PCWCH MappedPath;
    ULONG MappedPathLength;
} ZP_PROCESS_MEMORY_REGION, *PZP_PROCESS_MEMORY_REGION;

typedef const ZP_PROCESS_MEMORY_REGION* PCZP_PROCESS_MEMORY_REGION;

typedef struct _ZP_PROCESS_MEMORY_REGION_VIEW
{
    ULONGLONG BaseAddress;
    ULONGLONG AllocationBase;
    ULONGLONG RegionSize;
    ULONGLONG CommitSize;
    ULONGLONG WorkingSetBytes;
    ULONGLONG PrivateWorkingSetBytes;
    ULONGLONG SharedWorkingSetBytes;
    ULONGLONG ShareableWorkingSetBytes;
    ULONGLONG LockedWorkingSetBytes;
    ULONGLONG SharedOriginalBytes;
    ULONG State;
    ULONG Type;
    ULONG Protect;
    ULONG AllocationProtect;
    ULONG RegionType;
    ULONG Priority;
    NTSTATUS RegionStatus;
    NTSTATUS WorkingSetStatus;
    NTSTATUS MappedPathStatus;
    ZP_STRING_VIEW MappedPath;
} ZP_PROCESS_MEMORY_REGION_VIEW, *PZP_PROCESS_MEMORY_REGION_VIEW;

typedef struct _ZP_PROCESS_MEMORY_MAP_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_PROCESS_MEMORY_MAP_VIEW, *PZP_PROCESS_MEMORY_MAP_VIEW;

typedef const ZP_PROCESS_MEMORY_MAP_VIEW* PCZP_PROCESS_MEMORY_MAP_VIEW;

typedef USHORT ZP_PROCESS_CONTROL;
#define ZpProcessControlTerminate ((ZP_PROCESS_CONTROL)1)
#define ZpProcessControlTerminateTree ((ZP_PROCESS_CONTROL)2)
#define ZpProcessControlSuspend ((ZP_PROCESS_CONTROL)3)
#define ZpProcessControlResume ((ZP_PROCESS_CONTROL)4)
#define ZpProcessControlEfficiencyMode ((ZP_PROCESS_CONTROL)5)
#define ZpProcessControlPriority ((ZP_PROCESS_CONTROL)6)
#define ZpProcessControlUacVirtualization ((ZP_PROCESS_CONTROL)7)

#define ZP_PROCESS_FLAG_SUSPENDED 0x00000001UL
#define ZP_PROCESS_FLAG_EFFICIENCY_MODE 0x00000002UL
#define ZP_PROCESS_FLAG_VIRTUALIZATION_ALLOWED 0x00000004UL
#define ZP_PROCESS_FLAG_VIRTUALIZATION_ENABLED 0x00000008UL

typedef struct _ZP_PROCESS_RECORD
{
    ULONG ProcessId;
    ULONG ParentProcessId;
    ULONG SessionId;
    ULONG ThreadCount;
    ULONG HandleCount;
    ULONG Flags;
    USHORT MachineType;
    USHORT PriorityClass;
    ULONGLONG CreateTime;
    ULONGLONG UserTime;
    ULONGLONG KernelTime;
    ULONGLONG WorkingSetBytes;
    ULONGLONG PrivateBytes;
    PCWCH ImageName;
    ULONG ImageNameLength;
    PCWCH UserName;
    ULONG UserNameLength;
    PCWCH ImagePath;
    ULONG ImagePathLength;
    PCWCH ServiceNames;
    ULONG ServiceNamesLength;
} ZP_PROCESS_RECORD, *PZP_PROCESS_RECORD;

typedef const ZP_PROCESS_RECORD* PCZP_PROCESS_RECORD;

typedef struct _ZP_PROCESS_RECORD_VIEW
{
    ULONG ProcessId;
    ULONG ParentProcessId;
    ULONG SessionId;
    ULONG ThreadCount;
    ULONG HandleCount;
    ULONG Flags;
    USHORT MachineType;
    USHORT PriorityClass;
    ULONGLONG CreateTime;
    ULONGLONG UserTime;
    ULONGLONG KernelTime;
    ULONGLONG WorkingSetBytes;
    ULONGLONG PrivateBytes;
    ZP_STRING_VIEW ImageName;
    ZP_STRING_VIEW UserName;
    ZP_STRING_VIEW ImagePath;
    ZP_STRING_VIEW ServiceNames;
} ZP_PROCESS_RECORD_VIEW, *PZP_PROCESS_RECORD_VIEW;

typedef struct _ZP_PROCESS_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_PROCESS_LIST_VIEW, *PZP_PROCESS_LIST_VIEW;

typedef const ZP_PROCESS_LIST_VIEW* PCZP_PROCESS_LIST_VIEW;

typedef struct _ZP_PROCESS_INFO
{
    ULONG ProcessId;
    ULONG ParentProcessId;
    ULONG SessionId;
    ULONG ThreadCount;
    ULONG HandleCount;
    ULONG Flags;
    USHORT MachineType;
    USHORT PriorityClass;
    ULONGLONG CreateTime;
    ULONGLONG UserTime;
    ULONGLONG KernelTime;
    ULONGLONG WorkingSetBytes;
    ULONGLONG PrivateBytes;
    NTSTATUS ImageBaseStatus;
    ULONGLONG ImageBase;
    PCWCH ImageName;
    ULONG ImageNameLength;
    PCWCH UserName;
    ULONG UserNameLength;
    NTSTATUS ImagePathStatus;
    PCWCH ImagePath;
    ULONG ImagePathLength;
    NTSTATUS CommandLineStatus;
    PCWCH CommandLine;
    ULONG CommandLineLength;
} ZP_PROCESS_INFO, *PZP_PROCESS_INFO;

typedef const ZP_PROCESS_INFO* PCZP_PROCESS_INFO;

typedef struct _ZP_PROCESS_INFO_VIEW
{
    ULONG ProcessId;
    ULONG ParentProcessId;
    ULONG SessionId;
    ULONG ThreadCount;
    ULONG HandleCount;
    ULONG Flags;
    USHORT MachineType;
    USHORT PriorityClass;
    ULONGLONG CreateTime;
    ULONGLONG UserTime;
    ULONGLONG KernelTime;
    ULONGLONG WorkingSetBytes;
    ULONGLONG PrivateBytes;
    NTSTATUS ImageBaseStatus;
    ULONGLONG ImageBase;
    ZP_STRING_VIEW ImageName;
    ZP_STRING_VIEW UserName;
    NTSTATUS ImagePathStatus;
    ZP_STRING_VIEW ImagePath;
    NTSTATUS CommandLineStatus;
    ZP_STRING_VIEW CommandLine;
} ZP_PROCESS_INFO_VIEW, *PZP_PROCESS_INFO_VIEW;

NTSTATUS
ZpProcess_EncodeList(
    _In_reads_opt_(ProcessCount) PCZP_PROCESS_RECORD Processes,
    _In_ ULONG ProcessCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpProcess_DecodeList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PROCESS_LIST_VIEW View);

NTSTATUS
ZpProcess_GetNextRecord(
    _In_ PCZP_PROCESS_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_PROCESS_RECORD_VIEW Record);

NTSTATUS
ZpProcess_EncodeQuery(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpProcess_DecodeQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ProcessId,
    _Out_ PULONGLONG CreateTime);

NTSTATUS
ZpProcess_EncodeInfo(
    _In_ PCZP_PROCESS_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpProcess_DecodeInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PROCESS_INFO_VIEW View);

NTSTATUS
ZpProcess_EncodeControl(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ZP_PROCESS_CONTROL Control,
    _In_ ULONG Value,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpProcess_DecodeControl(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ProcessId,
    _Out_ PULONGLONG CreateTime,
    _Out_ ZP_PROCESS_CONTROL* Control,
    _Out_ PULONG Value);

NTSTATUS
ZpProcess_EncodeDump(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONG DumpType,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpProcess_DecodeDump(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ProcessId,
    _Out_ PULONGLONG CreateTime,
    _Out_ PULONG DumpType);

NTSTATUS
ZpProcess_EncodeDumpPath(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpProcess_DecodeDumpPath(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path);

NTSTATUS
ZpProcess_EncodeMemoryRead(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONGLONG Address,
    _In_ ULONG Length,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpProcess_DecodeMemoryRead(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ProcessId,
    _Out_ PULONGLONG CreateTime,
    _Out_ PULONGLONG Address,
    _Out_ PULONG Length);

NTSTATUS
ZpProcess_EncodeMemoryWrite(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONGLONG Address,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpProcess_DecodeMemoryWrite(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PROCESS_MEMORY_VIEW Memory);

NTSTATUS
ZpProcess_EncodeMemoryData(
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpProcess_DecodeMemoryData(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_BUFFER_VIEW Data);

NTSTATUS
ZpProcess_EncodeMemoryMap(
    _In_reads_opt_(RegionCount) PCZP_PROCESS_MEMORY_REGION Regions,
    _In_ ULONG RegionCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpProcess_DecodeMemoryMap(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PROCESS_MEMORY_MAP_VIEW View);

NTSTATUS
ZpProcess_ReadMemoryMapRegion(
    _In_ PCZP_PROCESS_MEMORY_MAP_VIEW Map,
    _Inout_ PULONG Offset,
    _Out_ PZP_PROCESS_MEMORY_REGION_VIEW Region);

EXTERN_C_END
