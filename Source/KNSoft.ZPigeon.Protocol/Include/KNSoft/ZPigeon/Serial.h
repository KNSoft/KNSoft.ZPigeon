#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_SERIAL_MODULE_ID 17
#define ZP_SERIAL_MODULE_VERSION 1
#define ZP_SERIAL_OPERATION_ENUMERATE 1
#define ZP_SERIAL_OPERATION_OPEN 2
#define ZP_SERIAL_MAX_PORTS 256
#define ZP_SERIAL_MAX_NAME_LENGTH 255
#define ZP_SERIAL_MAX_BAUD_RATE 4000000
#define ZP_SERIAL_PARITY_NONE 0
#define ZP_SERIAL_PARITY_ODD 1
#define ZP_SERIAL_PARITY_EVEN 2
#define ZP_SERIAL_PARITY_MARK 3
#define ZP_SERIAL_PARITY_SPACE 4
#define ZP_SERIAL_STOP_BITS_ONE 0
#define ZP_SERIAL_STOP_BITS_ONE_AND_HALF 1
#define ZP_SERIAL_STOP_BITS_TWO 2
#define ZP_SERIAL_FLOW_NONE 0
#define ZP_SERIAL_FLOW_XON_XOFF 1
#define ZP_SERIAL_FLOW_RTS_CTS 2
#define ZP_SERIAL_FLOW_DSR_DTR 3

typedef struct _ZP_SERIAL_PORT
{
    PCWCH Name;
    ULONG NameLength;
    PCWCH Device;
    ULONG DeviceLength;
} ZP_SERIAL_PORT, *PZP_SERIAL_PORT;

typedef const ZP_SERIAL_PORT* PCZP_SERIAL_PORT;

typedef struct _ZP_SERIAL_PORT_VIEW
{
    ZP_STRING_VIEW Name;
    ZP_STRING_VIEW Device;
} ZP_SERIAL_PORT_VIEW, *PZP_SERIAL_PORT_VIEW;

typedef const ZP_SERIAL_PORT_VIEW* PCZP_SERIAL_PORT_VIEW;

typedef struct _ZP_SERIAL_PORT_LIST_VIEW
{
    const VOID* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_SERIAL_PORT_LIST_VIEW, *PZP_SERIAL_PORT_LIST_VIEW;

typedef const ZP_SERIAL_PORT_LIST_VIEW* PCZP_SERIAL_PORT_LIST_VIEW;

typedef struct _ZP_SERIAL_OPEN_REQUEST_VIEW
{
    ZP_STRING_VIEW Port;
    ULONG BaudRate;
    BYTE DataBits;
    BYTE Parity;
    BYTE StopBits;
    BYTE FlowControl;
} ZP_SERIAL_OPEN_REQUEST_VIEW, *PZP_SERIAL_OPEN_REQUEST_VIEW;

typedef const ZP_SERIAL_OPEN_REQUEST_VIEW* PCZP_SERIAL_OPEN_REQUEST_VIEW;

LOGICAL
ZpSerial_IsPortNameValid(
    _In_reads_(Length) PCWCH Name,
    _In_ ULONG Length);

NTSTATUS
ZpSerial_EncodePortList(
    _In_reads_opt_(Count) PCZP_SERIAL_PORT Ports,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpSerial_DecodePortList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERIAL_PORT_LIST_VIEW List);

NTSTATUS
ZpSerial_GetNextPort(
    _In_ PCZP_SERIAL_PORT_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_SERIAL_PORT_VIEW Port);

NTSTATUS
ZpSerial_EncodeOpenRequest(
    _In_reads_(PortLength) PCWCH Port,
    _In_ ULONG PortLength,
    _In_ ULONG BaudRate,
    _In_ BYTE DataBits,
    _In_ BYTE Parity,
    _In_ BYTE StopBits,
    _In_ BYTE FlowControl,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpSerial_DecodeOpenRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERIAL_OPEN_REQUEST_VIEW Request);

NTSTATUS
ZpSerial_EncodeChannel(
    _In_ ULONG ChannelId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpSerial_DecodeChannel(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ChannelId);

EXTERN_C_END
