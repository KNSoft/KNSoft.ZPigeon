#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_SYSTEM_MODULE_ID 1
#define ZP_SYSTEM_OPERATION_INFO 1
#define ZP_SYSTEM_OPERATION_PROBE 2

typedef BYTE ZP_SYSTEM_ARCHITECTURE, *PZP_SYSTEM_ARCHITECTURE;

#define ZpSystemArchitectureX86 ((ZP_SYSTEM_ARCHITECTURE)1)
#define ZpSystemArchitectureX64 ((ZP_SYSTEM_ARCHITECTURE)2)
#define ZpSystemArchitectureArm64 ((ZP_SYSTEM_ARCHITECTURE)3)

typedef struct _ZP_SYSTEM_INFO
{
    ZP_SYSTEM_ARCHITECTURE Architecture;
    ULONG MajorVersion;
    ULONG MinorVersion;
    ULONG BuildNumber;
    ULONG ProcessorCount;
    ULONGLONG PhysicalMemoryBytes;
    PCWCH ComputerName;
    ULONG ComputerNameLength;
} ZP_SYSTEM_INFO, *PZP_SYSTEM_INFO;

typedef const ZP_SYSTEM_INFO* PCZP_SYSTEM_INFO;

typedef struct _ZP_SYSTEM_INFO_VIEW
{
    ZP_SYSTEM_ARCHITECTURE Architecture;
    ULONG MajorVersion;
    ULONG MinorVersion;
    ULONG BuildNumber;
    ULONG ProcessorCount;
    ULONGLONG PhysicalMemoryBytes;
    ZP_STRING_VIEW ComputerName;
} ZP_SYSTEM_INFO_VIEW, *PZP_SYSTEM_INFO_VIEW;

NTSTATUS
ZpSystem_EncodeInfo(
    _In_ PCZP_SYSTEM_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpSystem_DecodeInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SYSTEM_INFO_VIEW View);

EXTERN_C_END
