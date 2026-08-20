#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_PORTABLE_DEVICE_MODULE_ID 19
#define ZP_PORTABLE_DEVICE_MODULE_VERSION 1
#define ZP_PORTABLE_DEVICE_OPERATION_ENUMERATE_DEVICES 1
#define ZP_PORTABLE_DEVICE_OPERATION_ENUMERATE_OBJECTS 2
#define ZP_PORTABLE_DEVICE_OPERATION_CREATE_FOLDER 3
#define ZP_PORTABLE_DEVICE_OPERATION_DELETE 4
#define ZP_PORTABLE_DEVICE_OPERATION_RENAME 5
#define ZP_PORTABLE_DEVICE_OPERATION_OPEN_READ 6
#define ZP_PORTABLE_DEVICE_OPERATION_OPEN_WRITE 7
#define ZP_PORTABLE_DEVICE_PAGE_COUNT 100
#define ZP_PORTABLE_DEVICE_MAX_DEVICES 64
#define ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH 1024
#define ZP_PORTABLE_OBJECT_FOLDER 0x00000001UL
#define ZP_PORTABLE_OBJECT_STORAGE 0x00000002UL
#define ZP_PORTABLE_OBJECT_CAN_DELETE 0x00000004UL

typedef struct _ZP_PORTABLE_DEVICE_RECORD
{
    PCWCH Id;
    ULONG IdLength;
    PCWCH Name;
    ULONG NameLength;
    PCWCH Manufacturer;
    ULONG ManufacturerLength;
    PCWCH Model;
    ULONG ModelLength;
} ZP_PORTABLE_DEVICE_RECORD, *PZP_PORTABLE_DEVICE_RECORD;

typedef const ZP_PORTABLE_DEVICE_RECORD* PCZP_PORTABLE_DEVICE_RECORD;

typedef struct _ZP_PORTABLE_DEVICE_RECORD_VIEW
{
    ZP_STRING_VIEW Id;
    ZP_STRING_VIEW Name;
    ZP_STRING_VIEW Manufacturer;
    ZP_STRING_VIEW Model;
} ZP_PORTABLE_DEVICE_RECORD_VIEW, *PZP_PORTABLE_DEVICE_RECORD_VIEW;

typedef struct _ZP_PORTABLE_DEVICE_LIST_VIEW
{
    const VOID* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_PORTABLE_DEVICE_LIST_VIEW, *PZP_PORTABLE_DEVICE_LIST_VIEW;

typedef const ZP_PORTABLE_DEVICE_LIST_VIEW* PCZP_PORTABLE_DEVICE_LIST_VIEW;

typedef struct _ZP_PORTABLE_OBJECT_RECORD
{
    ULONGLONG Size;
    ULONGLONG ModifiedTime;
    ULONGLONG Capacity;
    ULONGLONG FreeSpace;
    ULONG Flags;
    PCWCH Id;
    ULONG IdLength;
    PCWCH PersistentId;
    ULONG PersistentIdLength;
    PCWCH Name;
    ULONG NameLength;
} ZP_PORTABLE_OBJECT_RECORD, *PZP_PORTABLE_OBJECT_RECORD;

typedef const ZP_PORTABLE_OBJECT_RECORD* PCZP_PORTABLE_OBJECT_RECORD;

typedef struct _ZP_PORTABLE_OBJECT_RECORD_VIEW
{
    ULONGLONG Size;
    ULONGLONG ModifiedTime;
    ULONGLONG Capacity;
    ULONGLONG FreeSpace;
    ULONG Flags;
    ZP_STRING_VIEW Id;
    ZP_STRING_VIEW PersistentId;
    ZP_STRING_VIEW Name;
} ZP_PORTABLE_OBJECT_RECORD_VIEW, *PZP_PORTABLE_OBJECT_RECORD_VIEW;

typedef struct _ZP_PORTABLE_OBJECT_PAGE_VIEW
{
    const VOID* Buffer;
    ULONG Length;
    ULONG Count;
    ULONG NextOffset;
} ZP_PORTABLE_OBJECT_PAGE_VIEW, *PZP_PORTABLE_OBJECT_PAGE_VIEW;

typedef const ZP_PORTABLE_OBJECT_PAGE_VIEW* PCZP_PORTABLE_OBJECT_PAGE_VIEW;

typedef struct _ZP_PORTABLE_OBJECT_PAGE_REQUEST_VIEW
{
    ZP_STRING_VIEW DeviceId;
    ZP_STRING_VIEW ParentId;
    ULONG Offset;
} ZP_PORTABLE_OBJECT_PAGE_REQUEST_VIEW, *PZP_PORTABLE_OBJECT_PAGE_REQUEST_VIEW;

typedef const ZP_PORTABLE_OBJECT_PAGE_REQUEST_VIEW* PCZP_PORTABLE_OBJECT_PAGE_REQUEST_VIEW;

typedef struct _ZP_PORTABLE_OBJECT_REQUEST_VIEW
{
    ZP_STRING_VIEW DeviceId;
    ZP_STRING_VIEW ObjectId;
} ZP_PORTABLE_OBJECT_REQUEST_VIEW, *PZP_PORTABLE_OBJECT_REQUEST_VIEW;

typedef const ZP_PORTABLE_OBJECT_REQUEST_VIEW* PCZP_PORTABLE_OBJECT_REQUEST_VIEW;

typedef struct _ZP_PORTABLE_NAME_REQUEST_VIEW
{
    ZP_STRING_VIEW DeviceId;
    ZP_STRING_VIEW ObjectId;
    ZP_STRING_VIEW Name;
} ZP_PORTABLE_NAME_REQUEST_VIEW, *PZP_PORTABLE_NAME_REQUEST_VIEW;

typedef const ZP_PORTABLE_NAME_REQUEST_VIEW* PCZP_PORTABLE_NAME_REQUEST_VIEW;

typedef struct _ZP_PORTABLE_WRITE_REQUEST_VIEW
{
    ZP_STRING_VIEW DeviceId;
    ZP_STRING_VIEW ParentId;
    ZP_STRING_VIEW Name;
    ULONGLONG FileSize;
} ZP_PORTABLE_WRITE_REQUEST_VIEW, *PZP_PORTABLE_WRITE_REQUEST_VIEW;

typedef const ZP_PORTABLE_WRITE_REQUEST_VIEW* PCZP_PORTABLE_WRITE_REQUEST_VIEW;

NTSTATUS
ZpPortable_EncodeDeviceList(
    _In_reads_opt_(Count) PCZP_PORTABLE_DEVICE_RECORD Devices,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpPortable_DecodeDeviceList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PORTABLE_DEVICE_LIST_VIEW List);

NTSTATUS
ZpPortable_GetNextDevice(
    _In_ PCZP_PORTABLE_DEVICE_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_PORTABLE_DEVICE_RECORD_VIEW Device);

NTSTATUS
ZpPortable_EncodeObjectPage(
    _In_reads_opt_(Count) PCZP_PORTABLE_OBJECT_RECORD Objects,
    _In_ ULONG Count,
    _In_ ULONG NextOffset,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpPortable_DecodeObjectPage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PORTABLE_OBJECT_PAGE_VIEW Page);

NTSTATUS
ZpPortable_GetNextObject(
    _In_ PCZP_PORTABLE_OBJECT_PAGE_VIEW Page,
    _Inout_ PULONG Offset,
    _Out_ PZP_PORTABLE_OBJECT_RECORD_VIEW Object);

NTSTATUS
ZpPortable_EncodeObjectPageRequest(
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_opt_(ParentIdLength) PCWCH ParentId,
    _In_ ULONG ParentIdLength,
    _In_ ULONG Offset,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpPortable_DecodeObjectPageRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PORTABLE_OBJECT_PAGE_REQUEST_VIEW Request);

NTSTATUS
ZpPortable_EncodeObjectRequest(
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ObjectIdLength) PCWCH ObjectId,
    _In_ ULONG ObjectIdLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpPortable_DecodeObjectRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PORTABLE_OBJECT_REQUEST_VIEW Request);

NTSTATUS
ZpPortable_EncodeNameRequest(
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ObjectIdLength) PCWCH ObjectId,
    _In_ ULONG ObjectIdLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpPortable_DecodeNameRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PORTABLE_NAME_REQUEST_VIEW Request);

NTSTATUS
ZpPortable_EncodeWriteRequest(
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ParentIdLength) PCWCH ParentId,
    _In_ ULONG ParentIdLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONGLONG FileSize,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpPortable_DecodeWriteRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PORTABLE_WRITE_REQUEST_VIEW Request);

EXTERN_C_END
