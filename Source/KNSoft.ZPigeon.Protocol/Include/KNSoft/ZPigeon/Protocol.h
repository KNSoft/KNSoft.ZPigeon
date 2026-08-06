#pragma once

#include <KNSoft/NDK/NDK.h>

EXTERN_C_START

#define ZP_CORE_VERSION 1
#define ZP_FRAME_MAX_BODY_SIZE 0x01000000UL
#define ZP_CHANNEL_DATA_MAX_SIZE 0x00100000UL
#define ZP_CODEC_MAX_ELEMENT_COUNT 0x00100000UL
#define ZP_MODULE_MAX_COUNT 64
#define ZP_CLIENT_PUBLIC_KEY_SIZE 65
#define ZP_SERVER_CHALLENGE_SIZE 32
#define ZP_CLIENT_SIGNATURE_SIZE 64

typedef enum _ZP_MESSAGE_TYPE
{
    ZpMessageClientHello = 0x01,
    ZpMessageServerChallenge = 0x02,
    ZpMessageClientAuthenticate = 0x03,
    ZpMessageReady = 0x04,
    ZpMessageDisconnect = 0x05,
    ZpMessageRequest = 0x10,
    ZpMessageResponse = 0x11,
    ZpMessageCancel = 0x12,
    ZpMessageEvent = 0x13,
    ZpMessageChannelData = 0x14,
    ZpMessageChannelClose = 0x15,
    ZpMessagePing = 0x16,
    ZpMessagePong = 0x17
} ZP_MESSAGE_TYPE;

typedef struct _ZP_BUFFER_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
} ZP_BUFFER_VIEW, *PZP_BUFFER_VIEW;

typedef const ZP_BUFFER_VIEW* PCZP_BUFFER_VIEW;

typedef struct _ZP_STRING_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
} ZP_STRING_VIEW, *PZP_STRING_VIEW;

typedef const ZP_STRING_VIEW* PCZP_STRING_VIEW;

typedef struct _ZP_CODEC_WRITER
{
    PBYTE Buffer;
    ULONG Size;
    ULONG Offset;
} ZP_CODEC_WRITER, *PZP_CODEC_WRITER;

typedef struct _ZP_CODEC_READER
{
    const BYTE* Buffer;
    ULONG Size;
    ULONG Offset;
} ZP_CODEC_READER, *PZP_CODEC_READER;

typedef struct _ZP_FRAME_VIEW
{
    ZP_MESSAGE_TYPE MessageType;
    const BYTE* Body;
    ULONG BodyLength;
} ZP_FRAME_VIEW, *PZP_FRAME_VIEW;

VOID
ZpCodec_InitializeWriter(
    _Out_ PZP_CODEC_WRITER Writer,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize);

VOID
ZpCodec_InitializeReader(
    _Out_ PZP_CODEC_READER Reader,
    _In_reads_bytes_(BufferSize) const VOID* Buffer,
    _In_ ULONG BufferSize);

NTSTATUS
ZpCodec_WriteByte(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ BYTE Value);

NTSTATUS
ZpCodec_WriteBoolean(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ BOOLEAN Value);

NTSTATUS
ZpCodec_WriteUInt16(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ USHORT Value);

NTSTATUS
ZpCodec_WriteUInt32(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ ULONG Value);

NTSTATUS
ZpCodec_WriteUInt64(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ ULONGLONG Value);

NTSTATUS
ZpCodec_WriteData(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_reads_bytes_opt_(Length) const VOID* Data,
    _In_ ULONG Length);

NTSTATUS
ZpCodec_WriteByteString(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_reads_bytes_opt_(Length) const VOID* Data,
    _In_ ULONG Length);

NTSTATUS
ZpCodec_WriteString(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_reads_opt_(Length) PCWCH String,
    _In_ ULONG Length);

NTSTATUS
ZpCodec_WriteArrayCount(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ ULONG Count);

NTSTATUS
ZpCodec_ReadByte(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PBYTE Value);

NTSTATUS
ZpCodec_ReadBoolean(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PBOOLEAN Value);

NTSTATUS
ZpCodec_ReadUInt16(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PUSHORT Value);

NTSTATUS
ZpCodec_ReadUInt32(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PULONG Value);

NTSTATUS
ZpCodec_ReadUInt64(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PULONGLONG Value);

NTSTATUS
ZpCodec_ReadData(
    _Inout_ PZP_CODEC_READER Reader,
    _In_ ULONG Length,
    _Out_ PZP_BUFFER_VIEW View);

NTSTATUS
ZpCodec_ReadByteString(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PZP_BUFFER_VIEW View);

NTSTATUS
ZpCodec_ReadString(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PZP_STRING_VIEW View);

NTSTATUS
ZpCodec_ReadArrayCount(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PULONG Count);

NTSTATUS
ZpFrame_GetSize(
    _In_ ULONG MessageBodyLength,
    _Out_ PULONG FrameSize);

NTSTATUS
ZpFrame_Encode(
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(MessageBodyLength) const VOID* MessageBody,
    _In_ ULONG MessageBodyLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpFrame_Decode(
    _In_reads_bytes_(BufferSize) const VOID* Buffer,
    _In_ ULONG BufferSize,
    _Out_ PZP_FRAME_VIEW View,
    _Out_ PULONG BytesConsumed);

EXTERN_C_END
