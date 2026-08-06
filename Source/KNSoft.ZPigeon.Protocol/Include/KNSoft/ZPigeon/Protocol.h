#pragma once

#include <KNSoft/NDK/NDK.h>

EXTERN_C_START

#define ZP_PROTOCOL_REVISION 1
#define ZP_FRAME_MAX_BODY_SIZE 0x01000000UL
#define ZP_MESSAGE_MAX_BODY_SIZE (ZP_FRAME_MAX_BODY_SIZE - sizeof(BYTE))
#define ZP_CHANNEL_DATA_MAX_SIZE 0x00100000UL
#define ZP_CHANNEL_WINDOW_MAX_SIZE 0x01000000UL
#define ZP_CODEC_MAX_ELEMENT_COUNT 0x00100000UL
#define ZP_MESSAGE_COMPRESSION_MIN_SIZE 0x00001000UL
#define ZP_MESSAGE_FLAG_COMPRESSED 0x80
#define ZP_MESSAGE_TYPE_MASK 0x7F
#define ZP_CONNECTION_POLICY_WIRE_SIZE sizeof(BYTE)
#define ZP_CONNECTION_POLICY_CLASS_MASK 0x07
#define ZP_CONNECTION_POLICY_LATENCY_SHIFT 3
#define ZP_CONNECTION_POLICY_RESERVED_MASK 0xC0
#define ZP_PERFORMANCE_CLASS_COUNT 5
#define ZP_MODULE_MAX_ID 64
#define ZP_MODULE_BIT(ModuleId) (1ull << ((ModuleId) - 1))
#define ZP_CLIENT_PUBLIC_KEY_SIZE 65
#define ZP_SERVER_CHALLENGE_SIZE 32
#define ZP_CLIENT_SIGNATURE_SIZE 64
#define ZP_MODULE_VERSION_WIRE_SIZE (sizeof(BYTE) + sizeof(USHORT))
#define ZP_MODULE_MANIFEST_MAX_WIRE_SIZE \
    (sizeof(BYTE) + ZP_MODULE_MAX_ID * ZP_MODULE_VERSION_WIRE_SIZE)
#define ZP_CLIENT_HELLO_MAX_WIRE_SIZE \
    (sizeof(BYTE) + ZP_MODULE_MANIFEST_MAX_WIRE_SIZE + ZP_CLIENT_PUBLIC_KEY_SIZE)

typedef USHORT ZP_STATUS_TYPE, *PZP_STATUS_TYPE;

typedef BYTE ZP_PERFORMANCE_CLASS, *PZP_PERFORMANCE_CLASS;

#define ZpPerformanceClass1 ((ZP_PERFORMANCE_CLASS)0)
#define ZpPerformanceClass2 ((ZP_PERFORMANCE_CLASS)1)
#define ZpPerformanceClass3 ((ZP_PERFORMANCE_CLASS)2)
#define ZpPerformanceClass4 ((ZP_PERFORMANCE_CLASS)3)
#define ZpPerformanceClass5 ((ZP_PERFORMANCE_CLASS)4)

#define ZpStatusNone ((ZP_STATUS_TYPE)0)
#define ZpStatusNtStatus ((ZP_STATUS_TYPE)1)
#define ZpStatusWin32 ((ZP_STATUS_TYPE)2)
#define ZpStatusWinsock ((ZP_STATUS_TYPE)3)
#define ZpStatusHResult ((ZP_STATUS_TYPE)4)
#define ZpStatusSecurity ((ZP_STATUS_TYPE)5)
#define ZpStatusQuic ((ZP_STATUS_TYPE)6)
#define ZpStatusProcessExit ((ZP_STATUS_TYPE)7)
#define ZpStatusConfigurationManager ((ZP_STATUS_TYPE)8)
#define ZpStatusSqlite ((ZP_STATUS_TYPE)9)

#define ZP_STATUS_WIRE_SIZE (sizeof(USHORT) + sizeof(ULONG))
#define ZP_REQUEST_HEADER_WIRE_SIZE (2 * sizeof(ULONG) + 2 * sizeof(BYTE))
#define ZP_RESPONSE_HEADER_WIRE_SIZE (sizeof(ULONG) + ZP_STATUS_WIRE_SIZE)
#define ZP_REQUEST_MAX_PAYLOAD_SIZE (ZP_MESSAGE_MAX_BODY_SIZE - ZP_REQUEST_HEADER_WIRE_SIZE)
#define ZP_RESPONSE_MAX_PAYLOAD_SIZE (ZP_MESSAGE_MAX_BODY_SIZE - ZP_RESPONSE_HEADER_WIRE_SIZE)

typedef struct _ZP_STATUS
{
    ZP_STATUS_TYPE Type;
    ULONG Code;
} ZP_STATUS, *PZP_STATUS;

typedef const ZP_STATUS* PCZP_STATUS;

static FORCEINLINE
ZP_STATUS
ZpStatus_Make(
    _In_ ZP_STATUS_TYPE Type,
    _In_ ULONG Code)
{
    ZP_STATUS Status = { Type, Code };

    return Status;
}

static FORCEINLINE
ZP_STATUS
ZpStatus_FromNtStatus(
    _In_ NTSTATUS Status)
{
    return ZpStatus_Make(Status == STATUS_SUCCESS ?
                             ZpStatusNone :
                             ZpStatusNtStatus,
                         (ULONG)Status);
}

static FORCEINLINE
ZP_STATUS
ZpStatus_FromCode(
    _In_ ZP_STATUS_TYPE Type,
    _In_ ULONG Code)
{
    return ZpStatus_Make(Code == 0 ? ZpStatusNone : Type, Code);
}

static FORCEINLINE
ZP_STATUS
ZpStatus_FromProcessExit(
    _In_ ULONG ExitCode)
{
    return ZpStatus_Make(ZpStatusProcessExit, ExitCode);
}

static FORCEINLINE
LOGICAL
ZpStatus_IsSuccess(
    _In_ ZP_STATUS Status)
{
    switch (Status.Type)
    {
        case ZpStatusNone:
            return Status.Code == 0;

        case ZpStatusNtStatus:
            return NT_SUCCESS((NTSTATUS)Status.Code);

        case ZpStatusWin32:
        case ZpStatusWinsock:
        case ZpStatusConfigurationManager:
        case ZpStatusSqlite:
            return Status.Code == ERROR_SUCCESS;

        case ZpStatusHResult:
        case ZpStatusSecurity:
        case ZpStatusQuic:
            return SUCCEEDED((HRESULT)Status.Code);

        case ZpStatusProcessExit:
            return TRUE;
    }
    return FALSE;
}

static FORCEINLINE
LOGICAL
ZpStatus_IsValid(
    _In_ ZP_STATUS Status)
{
    return Status.Type <= ZpStatusSqlite &&
           ((Status.Type == ZpStatusNone && Status.Code == 0) ||
            (Status.Type != ZpStatusNone &&
             (Status.Code != 0 || Status.Type == ZpStatusProcessExit)));
}

typedef BYTE ZP_MESSAGE_TYPE, *PZP_MESSAGE_TYPE;

#define ZpMessageClientHello ((ZP_MESSAGE_TYPE)0x01)
#define ZpMessageServerChallenge ((ZP_MESSAGE_TYPE)0x02)
#define ZpMessageClientAuthenticate ((ZP_MESSAGE_TYPE)0x03)
#define ZpMessageReady ((ZP_MESSAGE_TYPE)0x04)
#define ZpMessageRequest ((ZP_MESSAGE_TYPE)0x10)
#define ZpMessageResponse ((ZP_MESSAGE_TYPE)0x11)
#define ZpMessageCancel ((ZP_MESSAGE_TYPE)0x12)
#define ZpMessageChannelData ((ZP_MESSAGE_TYPE)0x13)
#define ZpMessageChannelClose ((ZP_MESSAGE_TYPE)0x14)
#define ZpMessageChannelWindow ((ZP_MESSAGE_TYPE)0x15)
#define ZpMessageConnectionPolicy ((ZP_MESSAGE_TYPE)0x16)

static FORCEINLINE
ZP_MESSAGE_TYPE
ZpMessage_GetType(
    _In_ ZP_MESSAGE_TYPE MessageType)
{
    return MessageType & ZP_MESSAGE_TYPE_MASK;
}

static FORCEINLINE
LOGICAL
ZpMessage_IsCompressed(
    _In_ ZP_MESSAGE_TYPE MessageType)
{
    return (MessageType & ZP_MESSAGE_FLAG_COMPRESSED) != 0;
}

typedef struct _ZP_BUFFER_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
} ZP_BUFFER_VIEW, *PZP_BUFFER_VIEW;

typedef const ZP_BUFFER_VIEW* PCZP_BUFFER_VIEW;

typedef struct _ZP_STRING_VIEW
{
    PCWCH Buffer;
    ULONG Length;
} ZP_STRING_VIEW, *PZP_STRING_VIEW;

typedef const ZP_STRING_VIEW* PCZP_STRING_VIEW;

typedef struct _ZP_SECURITY_DESCRIPTOR_VIEW
{
    ZP_STRING_VIEW Sddl;
    BOOLEAN DaclProtected;
} ZP_SECURITY_DESCRIPTOR_VIEW, *PZP_SECURITY_DESCRIPTOR_VIEW;

typedef const ZP_SECURITY_DESCRIPTOR_VIEW* PCZP_SECURITY_DESCRIPTOR_VIEW;

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

typedef struct _ZP_MODULE_VERSION
{
    BYTE ModuleId;
    USHORT Version;
} ZP_MODULE_VERSION, *PZP_MODULE_VERSION;

typedef const ZP_MODULE_VERSION* PCZP_MODULE_VERSION;

typedef struct _ZP_CLIENT_HELLO
{
    BYTE ProtocolRevision;
    PCZP_MODULE_VERSION Modules;
    BYTE ModuleCount;
    const BYTE* ClientPublicKey;
} ZP_CLIENT_HELLO, *PZP_CLIENT_HELLO;

typedef const ZP_CLIENT_HELLO* PCZP_CLIENT_HELLO;

typedef struct _ZP_CLIENT_HELLO_VIEW
{
    BYTE ProtocolRevision;
    ZP_MODULE_VERSION Modules[ZP_MODULE_MAX_ID];
    BYTE ModuleCount;
    const BYTE* ClientPublicKey;
} ZP_CLIENT_HELLO_VIEW, *PZP_CLIENT_HELLO_VIEW;

typedef struct _ZP_READY
{
    PCZP_MODULE_VERSION Modules;
    BYTE ModuleCount;
} ZP_READY, *PZP_READY;

typedef const ZP_READY* PCZP_READY;

typedef struct _ZP_READY_VIEW
{
    ZP_MODULE_VERSION Modules[ZP_MODULE_MAX_ID];
    BYTE ModuleCount;
} ZP_READY_VIEW, *PZP_READY_VIEW;

typedef struct _ZP_CONNECTION_POLICY
{
    BYTE SpeedClass : 3;
    BYTE LatencyClass : 3;
    BYTE Reserved : 2;
} ZP_CONNECTION_POLICY, *PZP_CONNECTION_POLICY;

typedef const ZP_CONNECTION_POLICY* PCZP_CONNECTION_POLICY;

C_ASSERT(sizeof(ZP_CONNECTION_POLICY) == ZP_CONNECTION_POLICY_WIRE_SIZE);

typedef struct _ZP_REQUEST
{
    ULONG RequestId;
    BYTE ModuleId;
    BYTE OperationId;
    ULONG TimeoutMilliseconds;
    const VOID* Payload;
    ULONG PayloadLength;
} ZP_REQUEST, *PZP_REQUEST;

typedef const ZP_REQUEST* PCZP_REQUEST;

typedef struct _ZP_REQUEST_VIEW
{
    ULONG RequestId;
    BYTE ModuleId;
    BYTE OperationId;
    ULONG TimeoutMilliseconds;
    ZP_BUFFER_VIEW Payload;
} ZP_REQUEST_VIEW, *PZP_REQUEST_VIEW;

typedef const ZP_REQUEST_VIEW* PCZP_REQUEST_VIEW;

typedef struct _ZP_RESPONSE
{
    ULONG RequestId;
    ZP_STATUS Status;
    const VOID* Payload;
    ULONG PayloadLength;
} ZP_RESPONSE, *PZP_RESPONSE;

typedef const ZP_RESPONSE* PCZP_RESPONSE;

typedef struct _ZP_RESPONSE_VIEW
{
    ULONG RequestId;
    ZP_STATUS Status;
    ZP_BUFFER_VIEW Payload;
} ZP_RESPONSE_VIEW, *PZP_RESPONSE_VIEW;

typedef const ZP_RESPONSE_VIEW* PCZP_RESPONSE_VIEW;

typedef struct _ZP_CHANNEL_DATA_VIEW
{
    ULONG ChannelId;
    ZP_BUFFER_VIEW Data;
} ZP_CHANNEL_DATA_VIEW, *PZP_CHANNEL_DATA_VIEW;

typedef struct _ZP_CHANNEL_CLOSE
{
    ULONG ChannelId;
    ZP_STATUS Status;
} ZP_CHANNEL_CLOSE, *PZP_CHANNEL_CLOSE;

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
ZpMessage_EncodeClientHello(
    _In_ PCZP_CLIENT_HELLO Message,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpMessage_DecodeClientHello(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_CLIENT_HELLO_VIEW View);

NTSTATUS
ZpMessage_EncodeServerChallenge(
    _In_reads_bytes_(ZP_SERVER_CHALLENGE_SIZE) const BYTE* Challenge,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpMessage_DecodeServerChallenge(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_BUFFER_VIEW View);

NTSTATUS
ZpMessage_EncodeClientAuthenticate(
    _In_reads_bytes_(ZP_CLIENT_SIGNATURE_SIZE) const BYTE* Signature,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpMessage_DecodeClientAuthenticate(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_BUFFER_VIEW View);

NTSTATUS
ZpMessage_EncodeReady(
    _In_ PCZP_READY Message,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpMessage_DecodeReady(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_READY_VIEW View);

NTSTATUS
ZpMessage_EncodeConnectionPolicy(
    _In_ PCZP_CONNECTION_POLICY Policy,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpMessage_DecodeConnectionPolicy(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_CONNECTION_POLICY Policy);

NTSTATUS
ZpMessage_EncodeRequestHeader(
    _In_ PCZP_REQUEST Message,
    _Out_writes_bytes_(ZP_REQUEST_HEADER_WIRE_SIZE) PVOID Buffer);

NTSTATUS
ZpMessage_EncodeRequest(
    _In_ PCZP_REQUEST Message,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpMessage_DecodeRequest(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_REQUEST_VIEW View);

NTSTATUS
ZpMessage_EncodeResponseHeader(
    _In_ PCZP_RESPONSE Message,
    _Out_writes_bytes_(ZP_RESPONSE_HEADER_WIRE_SIZE) PVOID Buffer);

NTSTATUS
ZpMessage_EncodeResponse(
    _In_ PCZP_RESPONSE Message,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpMessage_DecodeResponse(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_RESPONSE_VIEW View);

NTSTATUS
ZpMessage_EncodeCancel(
    _In_ ULONG RequestId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpMessage_DecodeCancel(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PULONG RequestId);

NTSTATUS
ZpMessage_EncodeChannelDataHeader(
    _In_ ULONG ChannelId,
    _Out_writes_bytes_(sizeof(ULONG)) PVOID Buffer);

NTSTATUS
ZpMessage_EncodeChannelData(
    _In_ ULONG ChannelId,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpMessage_DecodeChannelData(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_CHANNEL_DATA_VIEW View);

NTSTATUS
ZpMessage_EncodeChannelClose(
    _In_ ULONG ChannelId,
    _In_ ZP_STATUS Status,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpMessage_DecodeChannelClose(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PZP_CHANNEL_CLOSE Message);

NTSTATUS
ZpMessage_EncodeChannelWindow(
    _In_ ULONG ChannelId,
    _In_ ULONG CreditBytes,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpMessage_DecodeChannelWindow(
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Out_ PULONG ChannelId,
    _Out_ PULONG CreditBytes);

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
