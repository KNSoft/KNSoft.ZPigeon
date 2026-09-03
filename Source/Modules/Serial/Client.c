#include "Client.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"

#define ZP_SERIAL_CHUNK_SIZE 4096
#define ZP_SERIAL_WINDOW_SIZE 0x00010000UL

struct _ZP_CLIENT_SERIAL_CHANNEL
{
    ZP_CLIENT_LOCAL_CHANNEL Header;
    RTL_SRWLOCK SendLock;
    volatile LONG Closed;
    BOOLEAN WorkerActive;
    ULONGLONG Credit;
    ULONGLONG ReceiveCredit;
    HANDLE Port;
    HANDLE WorkerThread;
    HANDLE CreditEvent;
};

static
NTSTATUS
ZpSerial_SendLocked(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    return ZpClient_SendLocked(Object,
                               ZP_SEND_FLAG_INTERACTIVE,
                               MessageType,
                               Body,
                               BodyLength,
                               NULL,
                               0);
}

static
NTSTATUS
ZpSerial_SendCloseLocked(
    _Inout_ PZP_CLIENT_SERIAL_CHANNEL Channel,
    _In_ ZP_STATUS CloseStatus)
{
    BYTE Body[sizeof(ULONG) + ZP_STATUS_MAX_WIRE_SIZE];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelClose(Channel->Header.ChannelId,
                                          CloseStatus,
                                          Body,
                                          sizeof(Body),
                                          &BodyLength);
    return NT_SUCCESS(Status) ?
               ZpSerial_SendLocked(Channel->Header.Owner, ZpMessageChannelClose, Body, BodyLength) : Status;
}

static
NTSTATUS
ZpSerial_SendWindowLocked(
    _Inout_ PZP_CLIENT_SERIAL_CHANNEL Channel,
    _In_ ULONG CreditBytes)
{
    BYTE Body[2 * sizeof(ULONG)];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelWindow(Channel->Header.ChannelId,
                                           CreditBytes,
                                           Body,
                                           sizeof(Body),
                                           &BodyLength);
    if (NT_SUCCESS(Status))
    {
        Channel->ReceiveCredit += CreditBytes;
        Status = ZpSerial_SendLocked(Channel->Header.Owner, ZpMessageChannelWindow, Body, BodyLength);
        if (!NT_SUCCESS(Status)) Channel->ReceiveCredit -= CreditBytes;
    }
    return Status;
}

static
VOID
ZpSerial_Stop(
    _Inout_ PZP_CLIENT_SERIAL_CHANNEL Channel)
{
    IO_STATUS_BLOCK IoStatusBlock;

    if (InterlockedExchange(&Channel->Closed, TRUE)) return;
    if (Channel->WorkerThread != NULL) NtCancelSynchronousIoFile(Channel->WorkerThread, NULL, &IoStatusBlock);
    if (Channel->CreditEvent != NULL) NtSetEvent(Channel->CreditEvent, NULL);
}

static
VOID
ZpSerial_ChannelDestroy(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel)
{
    PZP_CLIENT_SERIAL_CHANNEL Channel = (PZP_CLIENT_SERIAL_CHANNEL)LocalChannel;

    ZpSerial_Stop(Channel);
    if (Channel->Port != NULL) NtClose(Channel->Port);
    if (Channel->WorkerThread != NULL) NtClose(Channel->WorkerThread);
    if (Channel->CreditEvent != NULL) NtClose(Channel->CreditEvent);
    Mem_Free(Channel);
}

static
VOID
ZpSerial_ChannelAbort(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    UNREFERENCED_PARAMETER(Status);
    ZpSerial_Stop((PZP_CLIENT_SERIAL_CHANNEL)LocalChannel);
}

static
VOID
ZpSerial_FinishWorker(
    _Inout_ PZP_CLIENT_SERIAL_CHANNEL Channel,
    _In_ ZP_STATUS Status,
    _In_ LOGICAL Notify)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    if (Removed && Notify) ZpSerial_SendCloseLocked(Channel, Status);
    Channel->WorkerActive = FALSE;
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    ZpSerial_Stop(Channel);
    if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
    ZpClientLocalChannel_Release(&Channel->Header);
}

static
_Function_class_(USER_THREAD_START_ROUTINE)
NTSTATUS
NTAPI
ZpSerial_ReceiveThread(
    _In_ PVOID Context)
{
    PZP_CLIENT_SERIAL_CHANNEL Channel = Context;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    PBYTE Body = Mem_Alloc(sizeof(ULONG) + ZP_SERIAL_CHUNK_SIZE);
    IO_STATUS_BLOCK IoStatusBlock;
    ZP_STATUS Completion = ZpStatus_Make(ZpStatusNone, 0);
    ULONG ReadLength, BodyLength;
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL Pending, Removed, Notify = TRUE;

    if (Body == NULL)
    {
        ZpSerial_FinishWorker(Channel, ZpStatus_FromNtStatus(STATUS_NO_MEMORY), TRUE);
        return STATUS_NO_MEMORY;
    }
    Status = ZpMessage_EncodeChannelDataHeader(Channel->Header.ChannelId, Body);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Body);
        ZpSerial_FinishWorker(Channel, ZpStatus_FromNtStatus(Status), TRUE);
        return Status;
    }
    for (;;)
    {
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Pending = Channel->Header.Pending;
        if (Pending && Channel->Credit == 0)
        {
            RtlReleaseSRWLockExclusive(&Object->Lock);
            Status = NtWaitForSingleObject(Channel->CreditEvent, FALSE, NULL);
            if (!NT_SUCCESS(Status)) break;
            continue;
        }
        if (!Pending)
        {
            RtlReleaseSRWLockExclusive(&Object->Lock);
            Notify = FALSE;
            break;
        }
        ReadLength = (ULONG)min(Channel->Credit, ZP_SERIAL_CHUNK_SIZE);
        Channel->Credit -= ReadLength;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Status = NtReadFile(Channel->Port,
                            NULL,
                            NULL,
                            NULL,
                            &IoStatusBlock,
                            Add2Ptr(Body, sizeof(ULONG)),
                            ReadLength,
                            NULL,
                            NULL);
        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_CANCELLED && Channel->Closed) Notify = FALSE;
            else Completion = ZpStatus_FromNtStatus(Status);
            break;
        }
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Channel->Credit += ReadLength - (ULONG)IoStatusBlock.Information;
        if (IoStatusBlock.Information == 0)
        {
            RtlReleaseSRWLockExclusive(&Object->Lock);
            continue;
        }
        BodyLength = sizeof(ULONG) + (ULONG)IoStatusBlock.Information;
        if (Channel->Header.Pending)
        {
            Status = ZpSerial_SendLocked(Object, ZpMessageChannelData, Body, BodyLength);
        }
        Removed = !NT_SUCCESS(Status) && ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        RtlReleaseSRWLockExclusive(&Object->Lock);
        if (Removed)
        {
            Notify = FALSE;
            ZpClientLocalChannel_Release(&Channel->Header);
            break;
        }
    }
    Mem_Free(Body);
    if (Completion.Type == ZpStatusNone && !NT_SUCCESS(Status)) Completion = ZpStatus_FromNtStatus(Status);
    ZpSerial_FinishWorker(Channel, Completion, Notify);
    return Status;
}

static
NTSTATUS
ZpSerial_ChannelWindow(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ULONG CreditBytes)
{
    PZP_CLIENT_SERIAL_CHANNEL Channel = (PZP_CLIENT_SERIAL_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending || MAXULONGLONG - Channel->Credit < CreditBytes)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->Credit += CreditBytes;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return NtSetEvent(Channel->CreditEvent, NULL);
}

static
NTSTATUS
ZpSerial_ChannelData(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message)
{
    PZP_CLIENT_SERIAL_CHANNEL Channel = (PZP_CLIENT_SERIAL_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    IO_STATUS_BLOCK IoStatusBlock;
    ZP_STATUS Completion = ZpStatus_Make(ZpStatusNone, 0);
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL Removed = FALSE;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending || Message->Data.Length > Channel->ReceiveCredit)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->ReceiveCredit -= Message->Data.Length;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    RtlAcquireSRWLockExclusive(&Channel->SendLock);
    Status = NtWriteFile(Channel->Port,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         (PVOID)Message->Data.Buffer,
                         Message->Data.Length,
                         NULL,
                         NULL);
    RtlReleaseSRWLockExclusive(&Channel->SendLock);
    if (!NT_SUCCESS(Status) || IoStatusBlock.Information != Message->Data.Length)
    {
        Completion = ZpStatus_FromNtStatus(NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status);
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (Channel->Header.Pending && Completion.Type == ZpStatusNone)
    {
        Status = ZpSerial_SendWindowLocked(Channel, Message->Data.Length);
        if (!NT_SUCCESS(Status)) Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    }
    else if (Channel->Header.Pending)
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        Status = ZpSerial_SendCloseLocked(Channel, Completion);
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        ZpSerial_Stop(Channel);
        ZpClientLocalChannel_Release(&Channel->Header);
    }
    return Status;
}

static
NTSTATUS
ZpSerial_ChannelClose(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_SERIAL_CHANNEL Channel = (PZP_CLIENT_SERIAL_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed;

    UNREFERENCED_PARAMETER(Status);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (!Removed) return STATUS_PROTOCOL_UNREACHABLE;
    ZpSerial_Stop(Channel);
    ZpClientLocalChannel_Release(&Channel->Header);
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpSerial_Enumerate(
    _Outptr_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    static const UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\HARDWARE\\DEVICEMAP\\SERIALCOMM");
    ZP_SERIAL_PORT Ports[ZP_SERIAL_MAX_PORTS];
    PKEY_VALUE_FULL_INFORMATION Values[ZP_SERIAL_MAX_PORTS];
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Key;
    ULONG Index, Length, Count = 0;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)&KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        Status = STATUS_SUCCESS;
        goto Encode;
    }
    if (!NT_SUCCESS(Status)) return Status;
    for (Index = 0; Count < ARRAYSIZE(Ports); Index++)
    {
        Length = 0;
        Status = NtEnumerateValueKey(Key, Index, KeyValueFullInformation, NULL, 0, &Length);
        if (Status == STATUS_NO_MORE_ENTRIES) break;
        if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW) break;
        Values[Count] = Mem_Alloc(Length);
        if (Values[Count] == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        Status = NtEnumerateValueKey(Key,
                                     Index,
                                     KeyValueFullInformation,
                                     Values[Count],
                                     Length,
                                     &Length);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(Values[Count]);
            break;
        }
        if (Values[Count]->Type != REG_SZ || Values[Count]->NameLength == 0 ||
            Values[Count]->NameLength > ZP_SERIAL_MAX_NAME_LENGTH * sizeof(WCHAR) ||
            Values[Count]->DataLength < sizeof(WCHAR) ||
            Values[Count]->DataLength > (ZP_SERIAL_MAX_NAME_LENGTH + 1) * sizeof(WCHAR) ||
            Values[Count]->DataOffset > Length || Values[Count]->DataLength > Length - Values[Count]->DataOffset)
        {
            Mem_Free(Values[Count]);
            continue;
        }
        Ports[Count].Device = Values[Count]->Name;
        Ports[Count].DeviceLength = Values[Count]->NameLength / sizeof(WCHAR);
        Ports[Count].Name = Add2Ptr(Values[Count], Values[Count]->DataOffset);
        Ports[Count].NameLength = Values[Count]->DataLength / sizeof(WCHAR);
        if (Ports[Count].Name[Ports[Count].NameLength - 1] == UNICODE_NULL) Ports[Count].NameLength--;
        if (!ZpSerial_IsPortNameValid(Ports[Count].Name, Ports[Count].NameLength))
        {
            Mem_Free(Values[Count]);
            continue;
        }
        Count++;
    }
    NtClose(Key);
    if (Status == STATUS_NO_MORE_ENTRIES) Status = STATUS_SUCCESS;

Encode:
    if (NT_SUCCESS(Status)) Status = ZpSerial_EncodePortList(Ports, Count, NULL, 0, ResponseLength);
    if (NT_SUCCESS(Status))
    {
        *Response = Mem_Alloc(*ResponseLength);
        if (*Response == NULL) Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpSerial_EncodePortList(Ports, Count, *Response, *ResponseLength, ResponseLength);
    }
    while (Count != 0) Mem_Free(Values[--Count]);
    return Status;
}

static
ZP_STATUS
ZpSerial_Open(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PCZP_SERIAL_OPEN_REQUEST_VIEW Request,
    _Outptr_ PZP_CLIENT_SERIAL_CHANNEL* OpenedChannel)
{
    WCHAR Path[13] = L"\\??\\";
    UNICODE_STRING PortName;
    PZP_CLIENT_SERIAL_CHANNEL Channel;
    COMMTIMEOUTS Timeouts = { MAXDWORD, 0, 100, 0, 5000 };
    DCB State = { sizeof(State) };
    NTSTATUS Status;
    ZP_STATUS Result = ZpStatus_Make(ZpStatusNone, 0);

    RtlCopyMemory(Path + 4, Request->Port.Buffer, (SIZE_T)Request->Port.Length * sizeof(WCHAR));
    RtlInitUnicodeString(&PortName, Path);
    Channel = Mem_Alloc(sizeof(*Channel));
    if (Channel == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    RtlZeroMemory(Channel, sizeof(*Channel));
    Status = IO_CreateFile(&Channel->Port,
                           &PortName,
                           NULL,
                           FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE,
                           0,
                           FILE_OPEN,
                           FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    if (!GetCommState(Channel->Port, &State))
    {
        Result = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    State.BaudRate = Request->BaudRate;
    State.ByteSize = Request->DataBits;
    State.Parity = Request->Parity;
    State.StopBits = Request->StopBits;
    State.fBinary = TRUE;
    State.fParity = Request->Parity != ZP_SERIAL_PARITY_NONE;
    State.fOutxCtsFlow = Request->FlowControl == ZP_SERIAL_FLOW_RTS_CTS;
    State.fOutxDsrFlow = Request->FlowControl == ZP_SERIAL_FLOW_DSR_DTR;
    State.fDtrControl = Request->FlowControl == ZP_SERIAL_FLOW_DSR_DTR ? DTR_CONTROL_HANDSHAKE : DTR_CONTROL_ENABLE;
    State.fOutX = State.fInX = Request->FlowControl == ZP_SERIAL_FLOW_XON_XOFF;
    State.fRtsControl = Request->FlowControl == ZP_SERIAL_FLOW_RTS_CTS ? RTS_CONTROL_HANDSHAKE : RTS_CONTROL_ENABLE;
    if (!SetCommState(Channel->Port, &State) || !SetCommTimeouts(Channel->Port, &Timeouts) ||
        !SetupComm(Channel->Port, ZP_SERIAL_WINDOW_SIZE, ZP_SERIAL_WINDOW_SIZE) ||
        !PurgeComm(Channel->Port, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR))
    {
        Result = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        goto Cleanup;
    }
    Status = NtCreateEvent(&Channel->CreditEvent,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           NULL,
                           SynchronizationEvent,
                           FALSE);
    if (NT_SUCCESS(Status))
    {
        Status = ZpClientLocalChannel_Insert(Client,
                                             &Channel->Header,
                                             ZP_SERIAL_MODULE_ID,
                                             ZpSerial_ChannelData,
                                             ZpSerial_ChannelWindow,
                                             ZpSerial_ChannelClose,
                                             ZpSerial_CommitChannel,
                                             ZpSerial_ChannelAbort,
                                             ZpSerial_ChannelDestroy);
    }
    if (NT_SUCCESS(Status))
    {
        *OpenedChannel = Channel;
        return Result;
    }

Cleanup:
    if (Channel->CreditEvent != NULL) NtClose(Channel->CreditEvent);
    if (Channel->Port != NULL) NtClose(Channel->Port);
    Mem_Free(Channel);
    return ZpStatus_IsSuccess(Result) ? ZpStatus_FromNtStatus(Status) : Result;
}

ZP_STATUS
ZpSerial_Execute(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_LOCAL_CHANNEL* Channel)
{
    ZP_SERIAL_OPEN_REQUEST_VIEW OpenRequest;
    PZP_CLIENT_SERIAL_CHANNEL OpenedChannel;
    NTSTATUS Status;
    ZP_STATUS Result;

    if (OperationId == ZP_SERIAL_OPERATION_ENUMERATE)
    {
        return RequestLength == 0 ? ZpStatus_FromNtStatus(ZpSerial_Enumerate(Response, ResponseLength)) :
                                    ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    if (OperationId != ZP_SERIAL_OPERATION_OPEN) return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    Status = ZpSerial_DecodeOpenRequest(Request, RequestLength, &OpenRequest);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Result = ZpSerial_Open(Client, &OpenRequest, &OpenedChannel);
    if (!ZpStatus_IsSuccess(Result)) return Result;
    *ResponseLength = sizeof(ULONG);
    *Response = Mem_Alloc(*ResponseLength);
    Status = *Response == NULL ? STATUS_NO_MEMORY :
                 ZpSerial_EncodeChannel(OpenedChannel->Header.ChannelId,
                                        *Response,
                                        *ResponseLength,
                                        ResponseLength);
    if (!NT_SUCCESS(Status))
    {
        ZpSerial_CommitChannel(&OpenedChannel->Header, FALSE);
        return ZpStatus_FromNtStatus(Status);
    }
    *Channel = &OpenedChannel->Header;
    return Result;
}

VOID
ZpSerial_CommitChannel(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ LOGICAL ResponseSent)
{
    PZP_CLIENT_SERIAL_CHANNEL Channel = (PZP_CLIENT_SERIAL_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed = FALSE, StartWorker = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ResponseSent)
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    }
    else
    {
        Status = ZpSerial_SendWindowLocked(Channel, ZP_SERIAL_WINDOW_SIZE);
        if (!NT_SUCCESS(Status)) Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        else
        {
            Channel->WorkerActive = TRUE;
            ZpClientLocalChannel_AddRef(&Channel->Header);
            Object->CallbackCount++;
            StartWorker = TRUE;
        }
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
    if (!StartWorker) return;
    Status = PS_CreateThread(NtCurrentProcess(), TRUE, ZpSerial_ReceiveThread, Channel, &Channel->WorkerThread, NULL);
    if (NT_SUCCESS(Status)) Status = NtResumeThread(Channel->WorkerThread, NULL);
    if (!NT_SUCCESS(Status)) ZpSerial_FinishWorker(Channel, ZpStatus_FromNtStatus(Status), TRUE);
}
