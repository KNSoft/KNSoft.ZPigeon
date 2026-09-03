#include "Connection.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#include <Ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

#define ZP_COMPRESSION_FORMAT COMPRESSION_FORMAT_XPRESS
#define ZP_COMPRESSION_MAXIMUM_FORMAT (COMPRESSION_FORMAT_XPRESS | COMPRESSION_ENGINE_MAXIMUM)
#define ZP_COMPRESSION_CHUNK_SIZE 4096
#define ZP_COMPRESSION_SAMPLE_SIZE 1024
#define ZP_COMPRESSION_SAMPLE_THRESHOLD (64 * 1024)
#define ZP_TRANSFER_SAMPLE_MIN_BYTES (256 * 1024)
#define ZP_TRANSFER_SAMPLE_MIN_MILLISECONDS 500
#define ZP_TRANSFER_SAMPLE_IDLE_MILLISECONDS 1000
#define ZP_CONNECTION_MAX_CACHED_COMPRESSION_BUFFER_SIZE (64 * 1024)

static volatile LONG64 ZpGlobalOutstandingSendBytes;

NTSTATUS
ZpMessage_ValidateBody(
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const BYTE* Body,
    _In_ ULONG BodyLength);

ZP_STATUS
ZpSocket_ResolveAddress(
    _In_opt_ PCWSTR Host,
    _In_ USHORT Port,
    _In_ LOGICAL Passive,
    _In_ INT SocketType,
    _In_ INT Protocol,
    _Out_ SOCKADDR_STORAGE* Address,
    _Out_ PINT AddressLength)
{
    ADDRINFOW Hints = { 0 }, *Result;
    WCHAR Service[6];
    INT Error;

    Hints.ai_family = AF_UNSPEC;
    Hints.ai_socktype = SocketType;
    Hints.ai_protocol = Protocol;
    Hints.ai_flags = Passive ? AI_PASSIVE : 0;
    _ultow_s(Port, Service, RTL_NUMBER_OF(Service), 10);
    Error = GetAddrInfoW(Host, Service, &Hints, &Result);
    if (Error != 0)
    {
        return ZpStatus_FromCode(ZpStatusWinsock, (ULONG)Error);
    }
    if (Result->ai_addrlen > sizeof(*Address))
    {
        FreeAddrInfoW(Result);
        return ZpStatus_FromNtStatus(STATUS_INVALID_ADDRESS);
    }
    RtlCopyMemory(Address, Result->ai_addr, Result->ai_addrlen);
    *AddressLength = (INT)Result->ai_addrlen;
    FreeAddrInfoW(Result);
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ULONG
ZpConnection_ReadUInt32(
    _In_reads_bytes_(sizeof(ULONG)) const BYTE* Buffer)
{
    return (ULONG)Buffer[0] |
           ((ULONG)Buffer[1] << 8) |
           ((ULONG)Buffer[2] << 16) |
           ((ULONG)Buffer[3] << 24);
}

static
VOID
ZpConnection_WriteUInt32(
    _Out_writes_bytes_(sizeof(ULONG)) BYTE* Buffer,
    _In_ ULONG Value)
{
    Buffer[0] = (BYTE)Value;
    Buffer[1] = (BYTE)(Value >> 8);
    Buffer[2] = (BYTE)(Value >> 16);
    Buffer[3] = (BYTE)(Value >> 24);
}

static
ULONG
ZpConnection_CompressionSavings(
    _In_ ULONG Length,
    _In_ PCZP_CONNECTION_POLICY Policy,
    _In_ ULONG Minimum)
{
    static const BYTE SpeedPercent[ZP_PERFORMANCE_CLASS_COUNT] = { 3, 6, 12, 25, 33 };
    static const LONG LatencyAdjustment[ZP_PERFORMANCE_CLASS_COUNT] = { -4, -2, 0, 4, 8 };
    LONG Percent = max(3, SpeedPercent[Policy->SpeedClass] +
                          LatencyAdjustment[Policy->LatencyClass]);

    return max(Minimum, (Length * Percent + 99) / 100);
}

static
VOID
ZpConnection_RecordTransfer(
    _Inout_ PZP_CONNECTION Connection,
    _Inout_ PZP_TRANSFER_STATISTICS Transfer,
    _In_ ULONG Length)
{
    ULONGLONG BitsPerSecond, Elapsed, Now = GetTickCount64();

    RtlAcquireSRWLockExclusive(&Connection->StatisticsLock);
    Transfer->TotalBytes += Length;
    if (Transfer->WindowStartTickCount == 0 ||
        Now - Transfer->LastTickCount > ZP_TRANSFER_SAMPLE_IDLE_MILLISECONDS)
    {
        Transfer->WindowStartTickCount = Now;
        Transfer->WindowBytes = 0;
    }
    Transfer->WindowBytes += Length;
    Transfer->LastTickCount = Now;
    Elapsed = Now - Transfer->WindowStartTickCount;
    if (Elapsed >= ZP_TRANSFER_SAMPLE_MIN_MILLISECONDS &&
        Transfer->WindowBytes >= ZP_TRANSFER_SAMPLE_MIN_BYTES)
    {
        BitsPerSecond = Transfer->WindowBytes * 8000 / Elapsed;
        Transfer->SmoothedBitsPerSecond = Transfer->SmoothedBitsPerSecond == 0 ?
                                              BitsPerSecond :
                                              (Transfer->SmoothedBitsPerSecond * 7 +
                                               BitsPerSecond * 3 + 5) / 10;
        Transfer->LastSampleTickCount = Now;
        Transfer->WindowStartTickCount = Now;
        Transfer->WindowBytes = 0;
    }
    RtlReleaseSRWLockExclusive(&Connection->StatisticsLock);
}

static
VOID
ZpConnection_Close(
    _Inout_ PZP_CONNECTION Connection)
{
    if (Connection->ReceiveBuffer != NULL)
    {
        Mem_Free(Connection->ReceiveBuffer);
        Connection->ReceiveBuffer = NULL;
    }
    Connection->ReceiveBufferLength = 0;
    Connection->ReceiveBufferSize = 0;
    Connection->ReceiveFrameSize = 0;
    Connection->State = ZpConnectionStateClosed;
}

NTSTATUS
ZpConnection_PrepareSend(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ZP_SEND_FLAGS SendFlags,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SEND_MESSAGE Message)
{
    BYTE Sample[ZP_COMPRESSION_SAMPLE_SIZE];
    ZP_CONNECTION_POLICY Policy;
    const BYTE* SampleInput;
    PBYTE Compressed, Input, InputAllocation = NULL, NewBuffer;
    ULONG MessageLength;
    ULONG CompressedLength, CompressedCapacity, FragmentWorkspaceSize;
    ULONG Savings, WorkspaceSize;
    USHORT CompressionFormat;
    NTSTATUS Status;

    if ((BodyLength != 0 && Body == NULL) || (PayloadLength != 0 && Payload == NULL) ||
        BodyLength > MAXULONG - PayloadLength ||
        (SendFlags & (ZP_SEND_FLAGS)~ZP_SEND_VALID_FLAGS) != 0 ||
        ZpMessage_IsCompressed(MessageType))
    {
        return STATUS_INVALID_PARAMETER;
    }
    MessageLength = BodyLength + PayloadLength;
    if (MessageLength > ZP_MESSAGE_MAX_BODY_SIZE) return STATUS_INVALID_PARAMETER;
    Message->MessageType = MessageType;
    Message->Body = Body;
    Message->BodyLength = BodyLength;
    Message->Payload = Payload;
    Message->PayloadLength = PayloadLength;
    Message->Buffer.Allocation = NULL;
    Message->CompressionLockHeld = FALSE;
    if (FlagOn(SendFlags, ZP_SEND_FLAG_SENSITIVE) ||
        !FlagOn(SendFlags, ZP_SEND_FLAG_COMPRESSIBLE) ||
        MessageLength < ZP_MESSAGE_COMPRESSION_MIN_SIZE ||
        (MessageType != ZpMessageResponse && MessageType != ZpMessageChannelData))
    {
        return STATUS_SUCCESS;
    }
    RtlAcquireSRWLockShared(&Connection->StatisticsLock);
    Policy = Connection->Policy;
    RtlReleaseSRWLockShared(&Connection->StatisticsLock);
    Savings = ZpConnection_CompressionSavings(MessageLength, &Policy, 64);
    if (MessageLength <= sizeof(ULONG) + Savings)
    {
        return STATUS_SUCCESS;
    }
    CompressedCapacity = MessageLength - sizeof(ULONG) - Savings;
    RtlAcquireSRWLockExclusive(&Connection->CompressionLock);
    if (Connection->CompressionWorkspace == NULL)
    {
        Status = RtlGetCompressionWorkSpaceSize(ZP_COMPRESSION_MAXIMUM_FORMAT,
                                                &WorkspaceSize,
                                                &FragmentWorkspaceSize);
        if (!NT_SUCCESS(Status))
        {
            RtlReleaseSRWLockExclusive(&Connection->CompressionLock);
            return Status;
        }
        Connection->CompressionWorkspace = Mem_Alloc(WorkspaceSize);
        if (Connection->CompressionWorkspace == NULL)
        {
            RtlReleaseSRWLockExclusive(&Connection->CompressionLock);
            return STATUS_SUCCESS;
        }
    }
    CompressionFormat = FlagOn(SendFlags, ZP_SEND_FLAG_BULK) &&
                        Policy.SpeedClass <= ZpPerformanceClass2 &&
                        Policy.LatencyClass <= ZpPerformanceClass3 ?
                            ZP_COMPRESSION_MAXIMUM_FORMAT : ZP_COMPRESSION_FORMAT;
    if (MessageLength >= ZP_COMPRESSION_SAMPLE_THRESHOLD)
    {
        ULONG SampleSavings = ZpConnection_CompressionSavings(sizeof(Sample),
                                                               &Policy,
                                                               16);

        if (BodyLength >= PayloadLength)
        {
            NT_ASSERT(BodyLength >= sizeof(Sample));
            SampleInput = Body;
        }
        else
        {
            NT_ASSERT(PayloadLength >= sizeof(Sample));
            SampleInput = Payload;
        }
        Status = RtlCompressBuffer(CompressionFormat,
                                   (PUCHAR)SampleInput,
                                   sizeof(Sample),
                                   Sample,
                                   sizeof(Sample) - SampleSavings,
                                   ZP_COMPRESSION_CHUNK_SIZE,
                                   &CompressedLength,
                                   Connection->CompressionWorkspace);
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            RtlReleaseSRWLockExclusive(&Connection->CompressionLock);
            return STATUS_SUCCESS;
        }
        if (!NT_SUCCESS(Status))
        {
            RtlReleaseSRWLockExclusive(&Connection->CompressionLock);
            return Status;
        }
    }
    if (PayloadLength == 0)
    {
        Input = (PBYTE)Body;
    }
    else
    {
        InputAllocation = Mem_Alloc(MessageLength);
        if (InputAllocation == NULL)
        {
            RtlReleaseSRWLockExclusive(&Connection->CompressionLock);
            return STATUS_SUCCESS;
        }
        Input = InputAllocation;
        if (BodyLength != 0) RtlCopyMemory(Input, Body, BodyLength);
        RtlCopyMemory(Input + BodyLength, Payload, PayloadLength);
    }
    if (sizeof(ULONG) + CompressedCapacity <= ZP_CONNECTION_MAX_CACHED_COMPRESSION_BUFFER_SIZE)
    {
        Compressed = Connection->CompressionBuffer;
        if (Connection->CompressionBufferSize < sizeof(ULONG) + CompressedCapacity)
        {
            NewBuffer = Mem_ReAlloc(Connection->CompressionBuffer,
                                    sizeof(ULONG) + CompressedCapacity);
            if (NewBuffer == NULL)
            {
                Mem_Free(InputAllocation);
                RtlReleaseSRWLockExclusive(&Connection->CompressionLock);
                return STATUS_SUCCESS;
            }
            Compressed = NewBuffer;
            Connection->CompressionBuffer = NewBuffer;
            Connection->CompressionBufferSize = sizeof(ULONG) + CompressedCapacity;
        }
    }
    else
    {
        Compressed = Mem_Alloc(sizeof(ULONG) + CompressedCapacity);
    }
    if (Compressed == NULL)
    {
        Mem_Free(InputAllocation);
        RtlReleaseSRWLockExclusive(&Connection->CompressionLock);
        return STATUS_SUCCESS;
    }
    Status = RtlCompressBuffer(CompressionFormat,
                               Input,
                               MessageLength,
                               Compressed + sizeof(ULONG),
                               CompressedCapacity,
                               ZP_COMPRESSION_CHUNK_SIZE,
                               &CompressedLength,
                               Connection->CompressionWorkspace);
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        if (Compressed != Connection->CompressionBuffer) Mem_Free(Compressed);
        Mem_Free(InputAllocation);
        RtlReleaseSRWLockExclusive(&Connection->CompressionLock);
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
    {
        if (Compressed != Connection->CompressionBuffer) Mem_Free(Compressed);
        Mem_Free(InputAllocation);
        RtlReleaseSRWLockExclusive(&Connection->CompressionLock);
        return Status;
    }
    Mem_Free(InputAllocation);
    ZpConnection_WriteUInt32(Compressed, MessageLength);
    Message->MessageType = (ZP_MESSAGE_TYPE)((BYTE)Message->MessageType |
                                            ZP_MESSAGE_FLAG_COMPRESSED);
    Message->Body = Compressed;
    Message->BodyLength = sizeof(ULONG) + CompressedLength;
    Message->Payload = NULL;
    Message->PayloadLength = 0;
    if (Compressed == Connection->CompressionBuffer)
    {
        Message->CompressionLockHeld = TRUE;
    }
    else
    {
        Message->Buffer.Allocation = Compressed;
        Message->Buffer.Offset = 0;
        Message->Buffer.Length = Message->BodyLength;
        Message->Buffer.Flags = SendFlags;
        RtlReleaseSRWLockExclusive(&Connection->CompressionLock);
    }
    return STATUS_SUCCESS;
}

VOID
ZpConnection_ReleaseSend(
    _Inout_ PZP_CONNECTION Connection,
    _Inout_ PZP_SEND_MESSAGE Message)
{
    ZpSendBuffer_Release(&Message->Buffer);
    if (Message->CompressionLockHeld)
    {
        RtlReleaseSRWLockExclusive(&Connection->CompressionLock);
    }
}

NTSTATUS
ZpConnection_ReserveSend(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ULONG Length)
{
    LONG64 Global, Updated;
    NTSTATUS Status = STATUS_SUCCESS;

    do
    {
        Global = InterlockedCompareExchange64(&ZpGlobalOutstandingSendBytes, 0, 0);
        if ((ULONGLONG)Length > ZP_GLOBAL_MAX_OUTSTANDING_SEND_BYTES - (ULONGLONG)Global)
        {
            RtlAcquireSRWLockExclusive(&Connection->StatisticsLock);
            Connection->RejectedSends++;
            RtlReleaseSRWLockExclusive(&Connection->StatisticsLock);
            return STATUS_QUOTA_EXCEEDED;
        }
        Updated = Global + Length;
    } while (InterlockedCompareExchange64(&ZpGlobalOutstandingSendBytes,
                                           Updated,
                                           Global) != Global);

    RtlAcquireSRWLockExclusive(&Connection->StatisticsLock);
    if ((ULONGLONG)Length > ZP_CONNECTION_MAX_OUTSTANDING_SEND_BYTES -
                            Connection->OutstandingSendBytes)
    {
        Connection->RejectedSends++;
        Status = STATUS_QUOTA_EXCEEDED;
    }
    else
    {
        Connection->OutstandingSendBytes += Length;
        Connection->MaximumOutstandingSendBytes =
            max(Connection->MaximumOutstandingSendBytes,
                Connection->OutstandingSendBytes);
    }
    RtlReleaseSRWLockExclusive(&Connection->StatisticsLock);
    if (!NT_SUCCESS(Status))
    {
        InterlockedAdd64(&ZpGlobalOutstandingSendBytes, -(LONG64)Length);
    }
    return Status;
}

VOID
ZpConnection_CompleteSend(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ULONG Length)
{
    RtlAcquireSRWLockExclusive(&Connection->StatisticsLock);
    NT_ASSERT(Connection->OutstandingSendBytes >= Length);
    Connection->OutstandingSendBytes -= Length;
    RtlReleaseSRWLockExclusive(&Connection->StatisticsLock);
    InterlockedAdd64(&ZpGlobalOutstandingSendBytes, -(LONG64)Length);
}

VOID
ZpConnection_RecordSendQueueDelay(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ULONGLONG EnqueuedTickCount)
{
    ULONGLONG Delay = GetTickCount64() - EnqueuedTickCount;

    RtlAcquireSRWLockExclusive(&Connection->StatisticsLock);
    Connection->MaximumSendQueueDelayMilliseconds =
        max(Connection->MaximumSendQueueDelayMilliseconds, Delay);
    RtlReleaseSRWLockExclusive(&Connection->StatisticsLock);
}

NTSTATUS
ZpConnection_EncodeFrame(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ZP_SEND_FLAGS SendFlags,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ USHORT Headroom,
    _Out_ PZP_SEND_BUFFER Buffer)
{
    ZP_SEND_MESSAGE Message;
    PBYTE Cursor;
    ULONG FrameSize;
    NTSTATUS Status;

    Status = ZpConnection_PrepareSend(Connection,
                                      SendFlags,
                                      MessageType,
                                      Body,
                                      BodyLength,
                                      Payload,
                                      PayloadLength,
                                      &Message);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpFrame_GetSize(Message.BodyLength + Message.PayloadLength, &FrameSize);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_ReleaseSend(Connection, &Message);
        return Status;
    }
    Status = ZpSendBuffer_Allocate(Buffer, Headroom, FrameSize, SendFlags);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_ReleaseSend(Connection, &Message);
        return Status;
    }
    Cursor = Buffer->Allocation + Buffer->Offset;
    ZpConnection_WriteUInt32(Cursor,
                             sizeof(BYTE) + Message.BodyLength + Message.PayloadLength);
    Cursor += sizeof(ULONG);
    *Cursor++ = Message.MessageType;
    if (Message.BodyLength != 0) RtlCopyMemory(Cursor, Message.Body, Message.BodyLength);
    Cursor += Message.BodyLength;
    if (Message.PayloadLength != 0) RtlCopyMemory(Cursor, Message.Payload, Message.PayloadLength);
    ZpConnection_ReleaseSend(Connection, &Message);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpSendBuffer_Allocate(
    _Out_ PZP_SEND_BUFFER Buffer,
    _In_ USHORT Headroom,
    _In_ ULONG Length,
    _In_ ZP_SEND_FLAGS Flags)
{
    ULONG Capacity;

    if ((Flags & (ZP_SEND_FLAGS)~ZP_SEND_VALID_FLAGS) != 0 || Headroom > MAXULONG - Length)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Capacity = Headroom + Length;
    Buffer->Allocation = Mem_Alloc(Capacity);
    if (Buffer->Allocation == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Buffer->Offset = Headroom;
    Buffer->Length = Length;
    Buffer->Flags = Flags;
    return STATUS_SUCCESS;
}

VOID
ZpSendBuffer_Release(
    _Inout_ PZP_SEND_BUFFER Buffer)
{
    if (Buffer->Allocation != NULL)
    {
        if (FlagOn(Buffer->Flags, ZP_SEND_FLAG_SENSITIVE))
        {
            RtlSecureZeroMemory(Buffer->Allocation + Buffer->Offset, Buffer->Length);
        }
        Mem_Free(Buffer->Allocation);
    }
}

NTSTATUS
ZpConnection_SetPolicy(
    _Inout_ PZP_CONNECTION Connection,
    _In_ PCZP_CONNECTION_POLICY Policy)
{
    if (Policy == NULL || Policy->Reserved != 0 ||
        Policy->SpeedClass >= ZP_PERFORMANCE_CLASS_COUNT ||
        Policy->LatencyClass >= ZP_PERFORMANCE_CLASS_COUNT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlAcquireSRWLockExclusive(&Connection->StatisticsLock);
    Connection->Policy = *Policy;
    RtlReleaseSRWLockExclusive(&Connection->StatisticsLock);
    return STATUS_SUCCESS;
}

VOID
ZpConnection_QueryStatistics(
    _Inout_ PZP_CONNECTION Connection,
    _Out_ PZP_NETWORK_STATISTICS Statistics)
{
    RtlAcquireSRWLockShared(&Connection->StatisticsLock);
    Statistics->SentBytes = Connection->Sent.TotalBytes;
    Statistics->ReceivedBytes = Connection->Received.TotalBytes;
    Statistics->SentBitsPerSecond = Connection->Sent.SmoothedBitsPerSecond;
    Statistics->ReceivedBitsPerSecond = Connection->Received.SmoothedBitsPerSecond;
    Statistics->SentSampleTickCount = Connection->Sent.LastSampleTickCount;
    Statistics->ReceivedSampleTickCount = Connection->Received.LastSampleTickCount;
    Statistics->OutstandingSendBytes = Connection->OutstandingSendBytes;
    Statistics->MaximumOutstandingSendBytes = Connection->MaximumOutstandingSendBytes;
    Statistics->MaximumSendQueueDelayMilliseconds =
        Connection->MaximumSendQueueDelayMilliseconds;
    Statistics->RejectedSends = Connection->RejectedSends;
    Statistics->Policy = Connection->Policy;
    RtlReleaseSRWLockShared(&Connection->StatisticsLock);
}

static
NTSTATUS
ZpConnection_GetFrameSize(
    _In_reads_bytes_(sizeof(ULONG)) const BYTE* Prefix,
    _Out_ PULONG FrameSize)
{
    ULONG BodyLength;

    BodyLength = ZpConnection_ReadUInt32(Prefix);
    if (BodyLength < sizeof(BYTE) || BodyLength > ZP_FRAME_MAX_BODY_SIZE)
    {
        return STATUS_DATA_ERROR;
    }
    *FrameSize = sizeof(ULONG) + BodyLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpConnection_ReserveReceiveBuffer(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ULONG RequiredSize)
{
    PBYTE Buffer;
    ULONG BufferSize;

    if (RequiredSize <= Connection->ReceiveBufferSize)
    {
        return STATUS_SUCCESS;
    }
    BufferSize = Connection->ReceiveBufferSize != 0 ?
                     Connection->ReceiveBufferSize :
                     min(ZP_CONNECTION_INITIAL_RECEIVE_BUFFER_SIZE, Connection->ReceiveFrameSize);
    while (BufferSize < RequiredSize)
    {
        BufferSize = min(BufferSize * 2, Connection->ReceiveFrameSize);
    }
    Buffer = Mem_ReAlloc(Connection->ReceiveBuffer, BufferSize);
    if (Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Connection->ReceiveBuffer = Buffer;
    Connection->ReceiveBufferSize = BufferSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpConnection_StartBufferedFrame(
    _Inout_ PZP_CONNECTION Connection,
    _In_reads_bytes_(DataLength) const BYTE* Data,
    _In_ ULONG DataLength,
    _In_ ULONG FrameSize)
{
    NTSTATUS Status;

    Connection->ReceiveFrameSize = FrameSize;
    Status = ZpConnection_ReserveReceiveBuffer(Connection, DataLength);
    if (!NT_SUCCESS(Status))
    {
        Connection->ReceiveFrameSize = 0;
        return Status;
    }
    RtlCopyMemory(Connection->ReceiveBuffer, Data, DataLength);
    Connection->ReceiveBufferLength = DataLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpConnection_GetReceiveState(
    _In_ const ZP_CONNECTION* Connection,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _Out_ ZP_CONNECTION_STATE* State)
{
    switch (Connection->State)
    {
        case ZpConnectionStateClientWaitChallenge:
            if (MessageType == ZpMessageServerChallenge)
            {
                *State = ZpConnectionStateClientSendAuthenticate;
                return STATUS_SUCCESS;
            }
            if (MessageType == ZpMessageServerReject)
            {
                *State = ZpConnectionStateClosed;
                return STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateClientWaitReady:
            if (MessageType == ZpMessageReady)
            {
                *State = ZpConnectionStateReady;
                return STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateServerWaitHello:
            if (MessageType == ZpMessageClientHello)
            {
                *State = ZpConnectionStateServerSendChallenge;
                return STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateServerWaitAuthenticate:
            if (MessageType == ZpMessageClientAuthenticate)
            {
                *State = ZpConnectionStateServerSendReady;
                return STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateReady:
            if (MessageType >= ZpMessageRequest &&
                MessageType <= ZpMessageConnectionPolicy)
            {
                *State = ZpConnectionStateReady;
                return STATUS_SUCCESS;
            }
            break;
    }
    return STATUS_PROTOCOL_UNREACHABLE;
}

static
NTSTATUS
ZpConnection_HandleReceiveFailure(
    _Inout_ PZP_CONNECTION Connection,
    _In_ NTSTATUS Status,
    _In_ LOGICAL Recoverable)
{
    ULONGLONG Now;

    if (!Recoverable)
    {
        ZpConnection_Close(Connection);
        return Status;
    }
    // A complete Ready-state frame has been consumed, so the next frame remains aligned.
    // Keep isolated peer or resource failures local; only invariants and sustained violations are fatal.
    if (Status == STATUS_INTERNAL_ERROR)
    {
        ZpConnection_Close(Connection);
        return Status;
    }
    if (Status == STATUS_NO_MEMORY ||
        Status == STATUS_INSUFFICIENT_RESOURCES ||
        Status == STATUS_QUOTA_EXCEEDED)
    {
        return STATUS_SUCCESS;
    }
    Now = GetTickCount64();
    if (Connection->ReceiveViolationWindowStart == 0 ||
        Now - Connection->ReceiveViolationWindowStart >
            ZP_CONNECTION_VIOLATION_WINDOW_MILLISECONDS)
    {
        Connection->ReceiveViolationWindowStart = Now;
        Connection->ReceiveViolationCount = 0;
    }
    Connection->ReceiveViolationCount++;
    if (Connection->ReceiveViolationCount < ZP_CONNECTION_VIOLATION_LIMIT)
    {
        return STATUS_SUCCESS;
    }
    ZpConnection_Close(Connection);
    return Status;
}

static
NTSTATUS
ZpConnection_DispatchFrame(
    _Inout_ PZP_CONNECTION Connection,
    _In_ const ZP_FRAME_VIEW* Frame)
{
    NTSTATUS Status;
    ZP_CONNECTION_STATE State;
    LOGICAL Recoverable;

    Recoverable = Connection->State == ZpConnectionStateReady;
    Status = ZpConnection_GetReceiveState(Connection, Frame->MessageType, &State);
    if (!NT_SUCCESS(Status))
    {
        return ZpConnection_HandleReceiveFailure(Connection, Status, Recoverable);
    }
    Connection->State = State;
    Status = Connection->MessageCallback(Connection, Frame, Connection->CallbackContext);
    return NT_SUCCESS(Status) ?
               STATUS_SUCCESS :
               ZpConnection_HandleReceiveFailure(Connection, Status, Recoverable);
}

static
NTSTATUS
ZpConnection_GetDecompressionBuffer(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ULONG Length,
    _Outptr_result_bytebuffer_(Length) PBYTE* Buffer,
    _Out_ PLOGICAL Cached)
{
    PBYTE NewBuffer;

    if (Length > ZP_CONNECTION_MAX_CACHED_COMPRESSION_BUFFER_SIZE)
    {
        *Buffer = Mem_Alloc(Length);
        *Cached = FALSE;
        return *Buffer != NULL ? STATUS_SUCCESS : STATUS_NO_MEMORY;
    }
    if (Connection->DecompressionBufferSize < Length)
    {
        NewBuffer = Mem_ReAlloc(Connection->DecompressionBuffer, Length);
        if (NewBuffer == NULL)
        {
            return STATUS_NO_MEMORY;
        }
        Connection->DecompressionBuffer = NewBuffer;
        Connection->DecompressionBufferSize = Length;
    }
    *Buffer = Connection->DecompressionBuffer;
    *Cached = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpConnection_DecodeAndDispatch(
    _Inout_ PZP_CONNECTION Connection,
    _In_reads_bytes_(FrameSize) const BYTE* Buffer,
    _In_ ULONG FrameSize)
{
    ZP_FRAME_VIEW DecodedFrame;
    PBYTE Decompressed;
    NTSTATUS Status;
    ZP_FRAME_VIEW Frame;
    ULONG BytesConsumed;
    LOGICAL Cached;

    Status = ZpFrame_Decode(Buffer, FrameSize, &Frame, &BytesConsumed);
    if (!NT_SUCCESS(Status) || BytesConsumed != FrameSize)
    {
        return ZpConnection_HandleReceiveFailure(
            Connection,
            NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status,
            Connection->State == ZpConnectionStateReady);
    }
    if (ZpMessage_IsCompressed(Frame.MessageType))
    {
        DecodedFrame.MessageType = ZpMessage_GetType(Frame.MessageType);
        DecodedFrame.BodyLength = ZpConnection_ReadUInt32(Frame.Body);
        Status = ZpConnection_GetDecompressionBuffer(Connection,
                                                     DecodedFrame.BodyLength,
                                                     &Decompressed,
                                                     &Cached);
        if (!NT_SUCCESS(Status))
        {
            return ZpConnection_HandleReceiveFailure(Connection,
                                                     Status,
                                                     Connection->State == ZpConnectionStateReady);
        }
        Status = RtlDecompressBuffer(ZP_COMPRESSION_FORMAT,
                                     Decompressed,
                                     DecodedFrame.BodyLength,
                                     (PUCHAR)Frame.Body + sizeof(ULONG),
                                     Frame.BodyLength - sizeof(ULONG),
                                     &BytesConsumed);
        if (!NT_SUCCESS(Status) || BytesConsumed != DecodedFrame.BodyLength ||
            !NT_SUCCESS(Status = ZpMessage_ValidateBody(DecodedFrame.MessageType,
                                                       Decompressed,
                                                       DecodedFrame.BodyLength)))
        {
            if (!Cached) Mem_Free(Decompressed);
            return ZpConnection_HandleReceiveFailure(
                Connection,
                NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status,
                Connection->State == ZpConnectionStateReady);
        }
        DecodedFrame.Body = Decompressed;
        Status = ZpConnection_DispatchFrame(Connection, &DecodedFrame);
        if (!Cached) Mem_Free(Decompressed);
        return Status;
    }
    return ZpConnection_DispatchFrame(Connection, &Frame);
}

NTSTATUS
ZpConnection_Initialize(
    _Out_ PZP_CONNECTION Connection,
    _In_ ZP_CONNECTION_ROLE Role,
    _In_ ZP_CONNECTION_MESSAGE_CALLBACK MessageCallback,
    _In_opt_ PVOID CallbackContext)
{
    if ((Role != ZpConnectionRoleClient && Role != ZpConnectionRoleServer) || MessageCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Connection, sizeof(*Connection));
    Connection->State = Role == ZpConnectionRoleClient ?
                            ZpConnectionStateClientSendHello :
                            ZpConnectionStateServerWaitHello;
    Connection->MessageCallback = MessageCallback;
    Connection->CallbackContext = CallbackContext;
    RtlInitializeSRWLock(&Connection->CompressionLock);
    RtlInitializeSRWLock(&Connection->StatisticsLock);
    Connection->Policy.SpeedClass = ZpPerformanceClass3;
    Connection->Policy.LatencyClass = ZpPerformanceClass3;
    return STATUS_SUCCESS;
}

VOID
ZpConnection_Uninitialize(
    _Inout_ PZP_CONNECTION Connection)
{
    ZpConnection_Close(Connection);
    if (Connection->CompressionWorkspace != NULL)
    {
        Mem_Free(Connection->CompressionWorkspace);
        Connection->CompressionWorkspace = NULL;
    }
    Mem_Free(Connection->CompressionBuffer);
    Mem_Free(Connection->DecompressionBuffer);
}

NTSTATUS
ZpConnection_NotifyMessageSent(
    _Inout_ PZP_CONNECTION Connection,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_ ULONG FrameSize)
{
    NTSTATUS Status = STATUS_INVALID_DEVICE_STATE;

    if (Connection->State == ZpConnectionStateClosed)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    switch (Connection->State)
    {
        case ZpConnectionStateClientSendHello:
            if (MessageType == ZpMessageClientHello)
            {
                Connection->State = ZpConnectionStateClientWaitChallenge;
                Status = STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateClientSendAuthenticate:
            if (MessageType == ZpMessageClientAuthenticate)
            {
                Connection->State = ZpConnectionStateClientWaitReady;
                Status = STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateServerSendChallenge:
            if (MessageType == ZpMessageServerChallenge)
            {
                Connection->State = ZpConnectionStateServerWaitAuthenticate;
                Status = STATUS_SUCCESS;
            }
            else if (MessageType == ZpMessageServerReject)
            {
                Connection->State = ZpConnectionStateClosed;
                Status = STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateServerSendReady:
            if (MessageType == ZpMessageReady)
            {
                Connection->State = ZpConnectionStateReady;
                Status = STATUS_SUCCESS;
            }
            break;

        case ZpConnectionStateReady:
            if (MessageType >= ZpMessageRequest &&
                MessageType <= ZpMessageConnectionPolicy)
            {
                Status = STATUS_SUCCESS;
            }
            break;
    }
    if (NT_SUCCESS(Status)) ZpConnection_RecordTransfer(Connection, &Connection->Sent, FrameSize);
    return Status;
}

NTSTATUS
ZpConnection_Receive(
    _Inout_ PZP_CONNECTION Connection,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    NTSTATUS Status;
    const BYTE* Input = Data;
    ULONG InputLength = DataLength;
    PBYTE FrameBuffer;
    ULONG CopyLength, FrameBufferSize, FrameSize;

    if ((DataLength != 0 && Data == NULL) || Connection->State == ZpConnectionStateClosed)
    {
        return DataLength != 0 && Data == NULL ? STATUS_INVALID_PARAMETER : STATUS_INVALID_DEVICE_STATE;
    }
    if (DataLength != 0) ZpConnection_RecordTransfer(Connection, &Connection->Received, InputLength);

    while (DataLength != 0)
    {
        if (Connection->ReceiveFrameSize != 0)
        {
            CopyLength = min(DataLength, Connection->ReceiveFrameSize - Connection->ReceiveBufferLength);
            Status = ZpConnection_ReserveReceiveBuffer(Connection,
                                                       Connection->ReceiveBufferLength + CopyLength);
            if (!NT_SUCCESS(Status))
            {
                ZpConnection_Close(Connection);
                return Status;
            }
            RtlCopyMemory(Connection->ReceiveBuffer + Connection->ReceiveBufferLength, Input, CopyLength);
            Connection->ReceiveBufferLength += CopyLength;
            Input += CopyLength;
            DataLength -= CopyLength;
            if (Connection->ReceiveBufferLength != Connection->ReceiveFrameSize)
            {
                return STATUS_SUCCESS;
            }

            FrameBuffer = Connection->ReceiveBuffer;
            FrameBufferSize = Connection->ReceiveBufferSize;
            FrameSize = Connection->ReceiveFrameSize;
            Connection->ReceiveBuffer = NULL;
            Connection->ReceiveBufferLength = 0;
            Connection->ReceiveBufferSize = 0;
            Connection->ReceiveFrameSize = 0;
            Status = ZpConnection_DecodeAndDispatch(Connection, FrameBuffer, FrameSize);
            if (Connection->State != ZpConnectionStateClosed &&
                Connection->ReceiveBuffer == NULL &&
                FrameBufferSize <= ZP_CONNECTION_MAX_CACHED_RECEIVE_BUFFER_SIZE)
            {
                Connection->ReceiveBuffer = FrameBuffer;
                Connection->ReceiveBufferSize = FrameBufferSize;
            }
            else
            {
                Mem_Free(FrameBuffer);
            }
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            if (Connection->State == ZpConnectionStateClosed)
            {
                return STATUS_SUCCESS;
            }
            continue;
        }

        if (Connection->ReceivePrefixLength != 0)
        {
            CopyLength = min(DataLength, sizeof(ULONG) - Connection->ReceivePrefixLength);
            RtlCopyMemory(Connection->ReceivePrefix + Connection->ReceivePrefixLength, Input, CopyLength);
            Connection->ReceivePrefixLength += CopyLength;
            Input += CopyLength;
            DataLength -= CopyLength;
            if (Connection->ReceivePrefixLength != sizeof(ULONG))
            {
                return STATUS_SUCCESS;
            }

            Status = ZpConnection_GetFrameSize(Connection->ReceivePrefix, &FrameSize);
            if (!NT_SUCCESS(Status))
            {
                ZpConnection_Close(Connection);
                return Status;
            }
            Status = ZpConnection_StartBufferedFrame(Connection,
                                                     Connection->ReceivePrefix,
                                                     sizeof(ULONG),
                                                     FrameSize);
            Connection->ReceivePrefixLength = 0;
            if (!NT_SUCCESS(Status))
            {
                ZpConnection_Close(Connection);
                return Status;
            }
            continue;
        }

        if (DataLength < sizeof(ULONG))
        {
            RtlCopyMemory(Connection->ReceivePrefix, Input, DataLength);
            Connection->ReceivePrefixLength = DataLength;
            return STATUS_SUCCESS;
        }

        Status = ZpConnection_GetFrameSize(Input, &FrameSize);
        if (!NT_SUCCESS(Status))
        {
            ZpConnection_Close(Connection);
            return Status;
        }
        if (DataLength < FrameSize)
        {
            Status = ZpConnection_StartBufferedFrame(Connection, Input, DataLength, FrameSize);
            if (!NT_SUCCESS(Status))
            {
                ZpConnection_Close(Connection);
            }
            return Status;
        }

        Status = ZpConnection_DecodeAndDispatch(Connection, Input, FrameSize);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if (Connection->State == ZpConnectionStateClosed)
        {
            return STATUS_SUCCESS;
        }
        Input += FrameSize;
        DataLength -= FrameSize;
    }
    return STATUS_SUCCESS;
}
