#include "Client.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"
#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#define ZP_TERMINAL_CHANNEL_CHUNK_SIZE 0x00020000UL
#define ZP_TERMINAL_INPUT_WINDOW_SIZE 0x00001000UL
#define ZP_TERMINAL_PATH_BUFFER_SIZE 0x00010000UL

struct _ZP_CLIENT_TERMINAL_CHANNEL
{
    ZP_CLIENT_LOCAL_CHANNEL Header;
    LOGICAL WorkerActive;
    LOGICAL InputPending;
    volatile LONG CloseQueued;
    RTL_SRWLOCK InputLock;
    RTL_SRWLOCK PseudoConsoleLock;
    ULONGLONG Credit;
    ULONGLONG ReceiveCredit;
    HPCON PseudoConsole;
    HANDLE Input;
    HANDLE Output;
    HANDLE Process;
    HANDLE OutputThread;
    HANDLE InputEvent;
    HANDLE CreditEvent;
    IO_STATUS_BLOCK InputIoStatus;
    ULONG InputLength;
    ULONG ProcessId;
    BYTE InputBuffer[ZP_TERMINAL_INPUT_WINDOW_SIZE];
};

static
NTSTATUS
ZpTerminal_QueryShells(
    _Out_writes_bytes_(ResponseSize) PVOID Response,
    _In_ ULONG ResponseSize,
    _Out_ PULONG ResponseLength)
{
    static UNICODE_STRING PathName = RTL_CONSTANT_STRING(L"PATH");
    static const PCWSTR Executables[] = {
        L"cmd.exe",
        L"powershell.exe",
        L"pwsh.exe"
    };
    UNICODE_STRING Path = { 0 };
    WCHAR FilePath[MAX_PATH];
    PVOID Buffer;
    ULONG Shells = 0, Index;
    NTSTATUS Status;

    Buffer = Mem_Alloc(ZP_TERMINAL_PATH_BUFFER_SIZE);
    if (Buffer == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Path.Buffer = Buffer;
    Path.MaximumLength = MAXUSHORT - 1;
    Status = RtlQueryEnvironmentVariable_U(NULL, &PathName, &Path);
    if (NT_SUCCESS(Status))
    {
        Path.Buffer[Path.Length / sizeof(WCHAR)] = UNICODE_NULL;
        for (Index = 0; Index < ARRAYSIZE(Executables); Index++)
        {
            if (RtlDosSearchPath_U(Path.Buffer,
                                   Executables[Index],
                                   NULL,
                                   sizeof(FilePath),
                                   FilePath,
                                   NULL) != 0)
            {
                Shells |= 1UL << Index;
            }
        }
        Status = ZpTerminal_EncodeShells(Shells,
                                         Response,
                                         ResponseSize,
                                         ResponseLength);
    }
    Mem_Free(Buffer);
    return Status;
}

static
NTSTATUS
ZpTerminal_ChannelData(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message);

static
NTSTATUS
ZpTerminal_ChannelWindow(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ ULONG CreditBytes);

static
NTSTATUS
ZpTerminal_ChannelClose(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ ZP_STATUS Status);

static
VOID
ZpTerminal_ChannelAbort(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ ZP_STATUS Status);

static
VOID
ZpTerminal_ChannelDestroy(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel);

static
NTSTATUS
ZpTerminal_QueuePseudoConsoleClose(
    _Inout_ PZP_CLIENT_TERMINAL_CHANNEL Channel);

static
NTSTATUS
ZpTerminal_SendLocked(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PCZP_TRANSPORT_OPERATIONS Operations =
        Object->TransportOperations[Object->ActiveTransport];

    return Object->State == ZpClientStateReady && Operations->Send != NULL ?
               Operations->Send(
                   Object->TransportContexts[Object->ActiveTransport],
                   MessageType,
                   Body,
                   BodyLength) :
               STATUS_CONNECTION_DISCONNECTED;
}

static
NTSTATUS
ZpTerminal_SendCloseLocked(
    _Inout_ PZP_CLIENT_TERMINAL_CHANNEL Channel,
    _In_ ZP_STATUS CloseStatus)
{
    BYTE Body[sizeof(ULONGLONG) + ZP_STATUS_WIRE_SIZE];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelClose(Channel->Header.ChannelId,
                                          CloseStatus,
                                          Body,
                                          sizeof(Body),
                                          &BodyLength);
    return NT_SUCCESS(Status) ?
               ZpTerminal_SendLocked(Channel->Header.Owner,
                                     ZpMessageChannelClose,
                                     Body,
                                     BodyLength) :
               Status;
}

static
NTSTATUS
ZpTerminal_SendWindowLocked(
    _Inout_ PZP_CLIENT_TERMINAL_CHANNEL Channel,
    _In_ ULONG CreditBytes)
{
    BYTE Body[sizeof(ULONGLONG) + sizeof(ULONG)];
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
        Status = ZpTerminal_SendLocked(Channel->Header.Owner,
                                       ZpMessageChannelWindow,
                                       Body,
                                       BodyLength);
        if (!NT_SUCCESS(Status))
        {
            Channel->ReceiveCredit -= CreditBytes;
        }
    }
    return Status;
}

static
VOID
ZpTerminal_Destroy(
    _Inout_ PZP_CLIENT_TERMINAL_CHANNEL Channel)
{
    IO_STATUS_BLOCK IoStatusBlock;

    if (Channel->Process != NULL &&
        PS_WaitForObject(Channel->Process, 0) == STATUS_TIMEOUT)
    {
        NtTerminateProcess(Channel->Process, STATUS_CANCELLED);
    }
    if (Channel->Input != NULL)
    {
        if (Channel->InputPending)
        {
            NtCancelIoFileEx(Channel->Input,
                             &Channel->InputIoStatus,
                             &IoStatusBlock);
            NtWaitForSingleObject(Channel->InputEvent, FALSE, NULL);
        }
        NtClose(Channel->Input);
    }
    if (Channel->Output != NULL)
    {
        NtClose(Channel->Output);
    }
    if (Channel->PseudoConsole != NULL)
    {
        ClosePseudoConsole(Channel->PseudoConsole);
    }
    if (Channel->Process != NULL)
    {
        NtClose(Channel->Process);
    }
    if (Channel->OutputThread != NULL)
    {
        NtClose(Channel->OutputThread);
    }
    if (Channel->InputEvent != NULL)
    {
        NtClose(Channel->InputEvent);
    }
    if (Channel->CreditEvent != NULL)
    {
        NtClose(Channel->CreditEvent);
    }
    Mem_Free(Channel);
}

static
VOID
ZpTerminal_ChannelDestroy(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel)
{
    ZpTerminal_Destroy((PZP_CLIENT_TERMINAL_CHANNEL)Channel);
}

static
VOID
ZpTerminal_ChannelAbort(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_TERMINAL_CHANNEL Channel =
        (PZP_CLIENT_TERMINAL_CHANNEL)LocalChannel;

    UNREFERENCED_PARAMETER(Status);
    NtTerminateProcess(Channel->Process, STATUS_CANCELLED);
    ZpTerminal_QueuePseudoConsoleClose(Channel);
    NtSetEvent(Channel->CreditEvent, NULL);
}

static
VOID
ZpTerminal_FinishWorker(
    _Inout_ PZP_CLIENT_TERMINAL_CHANNEL Channel,
    _In_ ZP_STATUS Status,
    _In_ LOGICAL Notify)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    if (Removed && Notify)
    {
        ZpTerminal_SendCloseLocked(Channel, Status);
    }
    Channel->WorkerActive = FALSE;
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        ZpClientLocalChannel_Release(&Channel->Header);
    }
    ZpClientLocalChannel_Release(&Channel->Header);
}

static
VOID
CALLBACK
ZpTerminal_ClosePseudoConsoleCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_TERMINAL_CHANNEL Channel = Context;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    HPCON PseudoConsole;

    UNREFERENCED_PARAMETER(Instance);
    RtlAcquireSRWLockExclusive(&Channel->PseudoConsoleLock);
    PseudoConsole = Channel->PseudoConsole;
    Channel->PseudoConsole = NULL;
    if (PseudoConsole != NULL)
    {
        ClosePseudoConsole(PseudoConsole);
    }
    RtlReleaseSRWLockExclusive(&Channel->PseudoConsoleLock);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    ZpClientLocalChannel_Release(&Channel->Header);
}

static
NTSTATUS
ZpTerminal_QueuePseudoConsoleClose(
    _Inout_ PZP_CLIENT_TERMINAL_CHANNEL Channel)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;

    if (InterlockedCompareExchange(&Channel->CloseQueued, TRUE, FALSE))
    {
        return STATUS_SUCCESS;
    }
    ZpClientLocalChannel_AddRef(&Channel->Header);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount++;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (TrySubmitThreadpoolCallback(ZpTerminal_ClosePseudoConsoleCallback,
                                    Channel,
                                    NULL))
    {
        return STATUS_SUCCESS;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    InterlockedExchange(&Channel->CloseQueued, FALSE);
    ZpClientLocalChannel_Release(&Channel->Header);
    return STATUS_NO_MEMORY;
}

static
ZP_STATUS
ZpTerminal_GetProcessStatus(
    _In_ PZP_CLIENT_TERMINAL_CHANNEL Channel)
{
    PROCESS_BASIC_INFORMATION ProcessInfo;
    NTSTATUS Status;

    Status = NtQueryInformationProcess(Channel->Process,
                                       ProcessBasicInformation,
                                       &ProcessInfo,
                                       sizeof(ProcessInfo),
                                       NULL);
    return NT_SUCCESS(Status) ?
               ZpStatus_FromProcessExit((ULONG)ProcessInfo.ExitStatus) :
               ZpStatus_FromNtStatus(Status);
}

static
NTSTATUS
NTAPI
ZpTerminal_OutputThread(
    _In_ PVOID Context)
{
    PZP_CLIENT_TERMINAL_CHANNEL Channel = Context;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE Event = NULL, Handles[2];
    PBYTE Body = NULL;
    ULONG ReadLength, ReservedLength, BytesRead, BodyLength;
    NTSTATUS Status, WaitStatus;
    ZP_STATUS CompletionStatus = { 0 };
    LOGICAL Pending, ProcessExited = FALSE, Notify = TRUE, Removed;

    Status = NtCreateEvent(&Event,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        goto Finish;
    }
    Body = Mem_Alloc(sizeof(ULONGLONG) + ZP_TERMINAL_CHANNEL_CHUNK_SIZE);
    if (Body == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Finish;
    }

    for (;;)
    {
        if (!ProcessExited &&
            PS_WaitForObject(Channel->Process, 0) == STATUS_SUCCESS)
        {
            ProcessExited = TRUE;
            Status = ZpTerminal_QueuePseudoConsoleClose(Channel);
            if (!NT_SUCCESS(Status))
            {
                break;
            }
        }

        RtlAcquireSRWLockExclusive(&Object->Lock);
        Pending = Channel->Header.Pending;
        if (Pending && Channel->Credit == 0)
        {
            RtlReleaseSRWLockExclusive(&Object->Lock);
            Handles[0] = Channel->CreditEvent;
            Handles[1] = Channel->Process;
            WaitStatus = NtWaitForMultipleObjects(ProcessExited ? 1 : 2,
                                                  Handles,
                                                  WaitAny,
                                                  FALSE,
                                                  NULL);
            if (WaitStatus == STATUS_WAIT_1)
            {
                ProcessExited = TRUE;
                Status = ZpTerminal_QueuePseudoConsoleClose(Channel);
                if (!NT_SUCCESS(Status))
                {
                    break;
                }
            }
            else if (WaitStatus != STATUS_WAIT_0)
            {
                Status = WaitStatus;
                break;
            }
            continue;
        }
        ReadLength = Pending ?
                         (ULONG)min(Channel->Credit,
                                    ZP_TERMINAL_CHANNEL_CHUNK_SIZE) :
                         ZP_TERMINAL_CHANNEL_CHUNK_SIZE;
        ReservedLength = Pending ? ReadLength : 0;
        Channel->Credit -= ReservedLength;
        RtlReleaseSRWLockExclusive(&Object->Lock);

        NtClearEvent(Event);
        Status = NtReadFile(Channel->Output,
                            Event,
                            NULL,
                            NULL,
                            &IoStatusBlock,
                            Add2Ptr(Body, sizeof(ULONGLONG)),
                            ReadLength,
                            NULL,
                            NULL);
        while (Status == STATUS_PENDING)
        {
            Handles[0] = Event;
            Handles[1] = Channel->Process;
            WaitStatus = NtWaitForMultipleObjects(ProcessExited ? 1 : 2,
                                                  Handles,
                                                  WaitAny,
                                                  FALSE,
                                                  NULL);
            if (WaitStatus == STATUS_WAIT_0)
            {
                Status = IoStatusBlock.Status;
            }
            else if (WaitStatus == STATUS_WAIT_1)
            {
                ProcessExited = TRUE;
                WaitStatus = ZpTerminal_QueuePseudoConsoleClose(Channel);
                if (!NT_SUCCESS(WaitStatus))
                {
                    Status = WaitStatus;
                }
            }
            else
            {
                Status = WaitStatus;
            }
        }
        if (!NT_SUCCESS(Status))
        {
            if (ProcessExited)
            {
                CompletionStatus = ZpTerminal_GetProcessStatus(Channel);
            }
            break;
        }
        BytesRead = (ULONG)IoStatusBlock.Information;
        if (BytesRead == 0)
        {
            if (ProcessExited)
            {
                CompletionStatus = ZpTerminal_GetProcessStatus(Channel);
            }
            else
            {
                Status = STATUS_PIPE_BROKEN;
            }
            break;
        }

        Status = ZpMessage_EncodeChannelData(
            Channel->Header.ChannelId,
            Add2Ptr(Body, sizeof(ULONGLONG)),
            BytesRead,
            Body,
            sizeof(ULONGLONG) + ZP_TERMINAL_CHANNEL_CHUNK_SIZE,
            &BodyLength);
        if (!NT_SUCCESS(Status))
        {
            break;
        }
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Pending = Channel->Header.Pending;
        if (ReservedLength != 0)
        {
            Channel->Credit += ReservedLength - BytesRead;
        }
        Status = Pending ?
                     ZpTerminal_SendLocked(Object,
                                           ZpMessageChannelData,
                                           Body,
                                           BodyLength) :
                     STATUS_SUCCESS;
        if (!NT_SUCCESS(Status))
        {
            Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        }
        else
        {
            Removed = FALSE;
        }
        RtlReleaseSRWLockExclusive(&Object->Lock);
        if (Removed)
        {
            Notify = FALSE;
            ZpClientLocalChannel_Release(&Channel->Header);
            NtTerminateProcess(Channel->Process, STATUS_CANCELLED);
            Status = ZpTerminal_QueuePseudoConsoleClose(Channel);
            NtSetEvent(Channel->CreditEvent, NULL);
            if (!NT_SUCCESS(Status))
            {
                break;
            }
        }
    }

Finish:
    if (Event != NULL)
    {
        NtClose(Event);
    }
    Mem_Free(Body);
    if (CompletionStatus.Type == ZpStatusNone)
    {
        CompletionStatus = ZpStatus_FromNtStatus(Status);
    }
    ZpTerminal_FinishWorker(Channel, CompletionStatus, Notify);
    return Status;
}

static
NTSTATUS
ZpTerminal_StartWorker(
    _Inout_ PZP_CLIENT_TERMINAL_CHANNEL Channel)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    HANDLE Thread;
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending || Channel->WorkerActive)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Channel->WorkerActive = TRUE;
    ZpClientLocalChannel_AddRef(&Channel->Header);
    Object->CallbackCount++;
    RtlReleaseSRWLockExclusive(&Object->Lock);

    Status = PS_CreateThread(NtCurrentProcess(),
                             TRUE,
                             ZpTerminal_OutputThread,
                             Channel,
                             &Thread,
                             NULL);
    if (NT_SUCCESS(Status))
    {
        Channel->OutputThread = Thread;
        Status = NtResumeThread(Thread, NULL);
    }
    if (!NT_SUCCESS(Status))
    {
        if (Channel->OutputThread != NULL)
        {
            NtTerminateThread(Channel->OutputThread, Status);
        }
        ZpTerminal_FinishWorker(Channel,
                                ZpStatus_FromNtStatus(Status),
                                TRUE);
    }
    return Status;
}

static
NTSTATUS
ZpTerminal_ChannelWindow(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ULONG CreditBytes)
{
    PZP_CLIENT_TERMINAL_CHANNEL Channel =
        (PZP_CLIENT_TERMINAL_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending ||
        MAXULONGLONG - Channel->Credit < CreditBytes)
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
ZpTerminal_ChannelData(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message)
{
    PZP_CLIENT_TERMINAL_CHANNEL Channel =
        (PZP_CLIENT_TERMINAL_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    NTSTATUS Status;
    LOGICAL Removed = FALSE;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending ||
        Message->Data.Length > Channel->ReceiveCredit ||
        Message->Data.Length > sizeof(Channel->InputBuffer))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->ReceiveCredit -= Message->Data.Length;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    RtlAcquireSRWLockExclusive(&Channel->InputLock);
    if (Channel->InputPending)
    {
        Status = NtWaitForSingleObject(Channel->InputEvent, FALSE, NULL);
        if (NT_SUCCESS(Status))
        {
            Status = Channel->InputIoStatus.Status;
        }
        if (NT_SUCCESS(Status) &&
            Channel->InputIoStatus.Information != Channel->InputLength)
        {
            Status = STATUS_UNSUCCESSFUL;
        }
        Channel->InputPending = FALSE;
    }
    else
    {
        Status = STATUS_SUCCESS;
    }
    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(Channel->InputBuffer,
                      Message->Data.Buffer,
                      Message->Data.Length);
        Channel->InputLength = Message->Data.Length;
        NtClearEvent(Channel->InputEvent);
        Status = NtWriteFile(Channel->Input,
                             Channel->InputEvent,
                             NULL,
                             NULL,
                             &Channel->InputIoStatus,
                             Channel->InputBuffer,
                             Channel->InputLength,
                             NULL,
                             NULL);
        if (Status == STATUS_PENDING)
        {
            Channel->InputPending = TRUE;
            Status = STATUS_SUCCESS;
        }
        else if (NT_SUCCESS(Status) &&
                 Channel->InputIoStatus.Information != Channel->InputLength)
        {
            Status = STATUS_UNSUCCESSFUL;
        }
    }
    RtlReleaseSRWLockExclusive(&Channel->InputLock);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_SUCCESS;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpTerminal_SendWindowLocked(Channel,
                                             Message->Data.Length);
    }
    if (!NT_SUCCESS(Status))
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        ZpTerminal_SendCloseLocked(Channel, ZpStatus_FromNtStatus(Status));
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        NtTerminateProcess(Channel->Process, STATUS_CANCELLED);
        ZpTerminal_QueuePseudoConsoleClose(Channel);
        NtSetEvent(Channel->CreditEvent, NULL);
        ZpClientLocalChannel_Release(&Channel->Header);
    }
    return NT_SUCCESS(Status) ? STATUS_SUCCESS : Status;
}

static
NTSTATUS
ZpTerminal_ChannelClose(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_TERMINAL_CHANNEL Channel =
        (PZP_CLIENT_TERMINAL_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed;

    UNREFERENCED_PARAMETER(Status);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (!Removed)
    {
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    NtTerminateProcess(Channel->Process, STATUS_CANCELLED);
    ZpTerminal_QueuePseudoConsoleClose(Channel);
    NtSetEvent(Channel->CreditEvent, NULL);
    ZpClientLocalChannel_Release(&Channel->Header);
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpTerminal_Create(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ const ZP_TERMINAL_CREATE_VIEW* Create,
    _Out_ PZP_CLIENT_TERMINAL_CHANNEL* Channel)
{
    STARTUPINFOEXW StartupInfo = { 0 };
    PROCESS_INFORMATION ProcessInfo = { 0 };
    PZP_CLIENT_TERMINAL_CHANNEL ChannelObject = NULL;
    PPROC_THREAD_ATTRIBUTE_LIST AttributeList = NULL;
    HANDLE PipeDirectory = NULL;
    HANDLE Input = NULL, InputPeer = NULL;
    HANDLE Output = NULL, OutputPeer = NULL;
    HPCON PseudoConsole = NULL;
    PWCHAR CommandLine = NULL, WorkingDirectory = NULL;
    SIZE_T AttributeListSize = 0;
    UNICODE_STRING PipeDirectoryPath = RTL_CONSTANT_STRING(DEVICE_NAMED_PIPE);
    COORD Size;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;
    ZP_STATUS ResultStatus = { 0 };
    LOGICAL AttributeListInitialized = FALSE;
    static UNICODE_STRING UserProfileName =
        RTL_CONSTANT_STRING(L"USERPROFILE");

    if (Create->Columns > MAXSHORT || Create->Rows > MAXSHORT)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    ChannelObject = Mem_Alloc(sizeof(*ChannelObject));
    CommandLine = Mem_Alloc(((SIZE_T)Create->CommandLine.Length + 1) *
                            sizeof(WCHAR));
    WorkingDirectory = Mem_Alloc(Create->WorkingDirectory.Length != 0 ?
        ((SIZE_T)Create->WorkingDirectory.Length + 1) * sizeof(WCHAR) :
        ZP_TERMINAL_PATH_BUFFER_SIZE);
    if (ChannelObject == NULL || CommandLine == NULL || WorkingDirectory == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    RtlZeroMemory(ChannelObject, sizeof(*ChannelObject));
    RtlCopyMemory(CommandLine,
                  Create->CommandLine.Buffer,
                  (SIZE_T)Create->CommandLine.Length * sizeof(WCHAR));
    CommandLine[Create->CommandLine.Length] = UNICODE_NULL;
    if (Create->WorkingDirectory.Length != 0)
    {
        RtlCopyMemory(WorkingDirectory,
                      Create->WorkingDirectory.Buffer,
                      (SIZE_T)Create->WorkingDirectory.Length * sizeof(WCHAR));
        WorkingDirectory[Create->WorkingDirectory.Length] = UNICODE_NULL;
    }
    else
    {
        UNICODE_STRING UserProfile = {
            .Buffer = WorkingDirectory,
            .MaximumLength = MAXUSHORT - 1
        };
        Status = RtlQueryEnvironmentVariable_U(NULL,
                                                &UserProfileName,
                                                &UserProfile);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
        WorkingDirectory[UserProfile.Length / sizeof(WCHAR)] = UNICODE_NULL;
    }
    Status = NtCreateEvent(&ChannelObject->InputEvent,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    Status = NtCreateEvent(&ChannelObject->CreditEvent,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           NULL,
                           SynchronizationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    Status = IO_CreateFile(&PipeDirectory,
                           &PipeDirectoryPath,
                           NULL,
                           SYNCHRONIZE | GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           FILE_OPEN,
                           FILE_SYNCHRONOUS_IO_NONALERT);
    if (NT_SUCCESS(Status))
    {
        Status = IO_CreatePipe(PipeDirectory,
                               &Input,
                               &InputPeer,
                               FILE_PIPE_OUTBOUND,
                               ZP_TERMINAL_CHANNEL_CHUNK_SIZE);
    }
    if (NT_SUCCESS(Status))
    {
        Status = IO_CreatePipe(PipeDirectory,
                               &Output,
                               &OutputPeer,
                               FILE_PIPE_INBOUND,
                               ZP_TERMINAL_CHANNEL_CHUNK_SIZE);
        NtClose(PipeDirectory);
        PipeDirectory = NULL;
    }
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    Size.X = (SHORT)Create->Columns;
    Size.Y = (SHORT)Create->Rows;
    Result = CreatePseudoConsole(Size,
                                 InputPeer,
                                 OutputPeer,
                                 0,
                                 &PseudoConsole);
    if (FAILED(Result))
    {
        ResultStatus = ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result);
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }
    InitializeProcThreadAttributeList(NULL, 1, 0, &AttributeListSize);
    AttributeList = Mem_Alloc(AttributeListSize);
    if (AttributeList == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    if (!InitializeProcThreadAttributeList(AttributeList,
                                           1,
                                           0,
                                           &AttributeListSize))
    {
        ResultStatus = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }
    AttributeListInitialized = TRUE;
    if (!UpdateProcThreadAttribute(AttributeList,
                                   0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   PseudoConsole,
                                   sizeof(PseudoConsole),
                                   NULL,
                                   NULL))
    {
        ResultStatus = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }
    StartupInfo.StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    StartupInfo.lpAttributeList = AttributeList;
    if (!CreateProcessW(NULL,
                        CommandLine,
                        NULL,
                        NULL,
                        FALSE,
                        EXTENDED_STARTUPINFO_PRESENT |
                            CREATE_UNICODE_ENVIRONMENT,
                        NULL,
                        WorkingDirectory,
                        &StartupInfo.StartupInfo,
                        &ProcessInfo))
    {
        ResultStatus = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }
    NtClose(InputPeer);
    InputPeer = NULL;
    NtClose(OutputPeer);
    OutputPeer = NULL;
    NtClose(ProcessInfo.hThread);
    ProcessInfo.hThread = NULL;
    ChannelObject->PseudoConsole = PseudoConsole;
    ChannelObject->Input = Input;
    ChannelObject->Output = Output;
    ChannelObject->Process = ProcessInfo.hProcess;
    ChannelObject->ProcessId = ProcessInfo.dwProcessId;
    PseudoConsole = NULL;
    Input = NULL;
    Output = NULL;
    ProcessInfo.hProcess = NULL;
    Status = ZpClientLocalChannel_Insert(Object,
                                         &ChannelObject->Header,
                                         ZP_TERMINAL_MODULE_ID,
                                         ZpTerminal_ChannelData,
                                         ZpTerminal_ChannelWindow,
                                         ZpTerminal_ChannelClose,
                                         ZpTerminal_ChannelAbort,
                                         ZpTerminal_ChannelDestroy);
    if (NT_SUCCESS(Status))
    {
        *Channel = ChannelObject;
        ChannelObject = NULL;
    }

Cleanup:
    if (AttributeListInitialized)
    {
        DeleteProcThreadAttributeList(AttributeList);
    }
    Mem_Free(AttributeList);
    if (ProcessInfo.hThread != NULL)
    {
        NtClose(ProcessInfo.hThread);
    }
    if (ProcessInfo.hProcess != NULL)
    {
        NtTerminateProcess(ProcessInfo.hProcess, STATUS_CANCELLED);
        NtClose(ProcessInfo.hProcess);
    }
    if (PseudoConsole != NULL)
    {
        ClosePseudoConsole(PseudoConsole);
    }
    if (PipeDirectory != NULL)
    {
        NtClose(PipeDirectory);
    }
    if (Input != NULL)
    {
        NtClose(Input);
    }
    if (InputPeer != NULL)
    {
        NtClose(InputPeer);
    }
    if (Output != NULL)
    {
        NtClose(Output);
    }
    if (OutputPeer != NULL)
    {
        NtClose(OutputPeer);
    }
    Mem_Free(WorkingDirectory);
    Mem_Free(CommandLine);
    if (ChannelObject != NULL)
    {
        ZpTerminal_Destroy(ChannelObject);
    }
    return ResultStatus.Type != ZpStatusNone ?
               ResultStatus : ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpTerminal_Resize(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ ULONGLONG ChannelId,
    _In_ USHORT Columns,
    _In_ USHORT Rows)
{
    PZP_CLIENT_LOCAL_CHANNEL LocalChannel;
    PZP_CLIENT_TERMINAL_CHANNEL Channel;
    COORD Size;
    HRESULT Result;
    NTSTATUS Status;

    if (Columns > MAXSHORT || Rows > MAXSHORT)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Status = ZpClientLocalChannel_ReferenceById(Object,
                                                ChannelId,
                                                ZP_TERMINAL_MODULE_ID,
                                                &LocalChannel);
    if (!NT_SUCCESS(Status))
    {
        return ZpStatus_FromNtStatus(Status);
    }
    Channel = (PZP_CLIENT_TERMINAL_CHANNEL)LocalChannel;
    Size.X = (SHORT)Columns;
    Size.Y = (SHORT)Rows;
    RtlAcquireSRWLockShared(&Channel->PseudoConsoleLock);
    Result = Channel->PseudoConsole != NULL ?
                 ResizePseudoConsole(Channel->PseudoConsole, Size) : E_HANDLE;
    RtlReleaseSRWLockShared(&Channel->PseudoConsoleLock);
    ZpClientLocalChannel_Release(LocalChannel);
    return SUCCEEDED(Result) ?
               ZpStatus_FromNtStatus(STATUS_SUCCESS) :
               ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result);
}

ZP_STATUS
ZpTerminal_Execute(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ USHORT OperationId,
    _In_reads_bytes_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_TERMINAL_CHANNEL* Channel)
{
    ZP_TERMINAL_CREATE_VIEW Create;
    PZP_CLIENT_TERMINAL_CHANNEL ChannelObject = NULL;
    ULONGLONG ChannelId;
    USHORT Columns, Rows;
    NTSTATUS Status;
    ZP_STATUS ResultStatus;

    *Channel = NULL;
    if (OperationId == ZP_TERMINAL_OPERATION_QUERY_SHELLS)
    {
        if (RequestLength != 0)
        {
            return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
        }
        *Response = Mem_Alloc(sizeof(ULONG));
        if (*Response == NULL)
        {
            return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
        Status = ZpTerminal_QueryShells(*Response,
                                        sizeof(ULONG),
                                        ResponseLength);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(*Response);
            *Response = NULL;
        }
        return ZpStatus_FromNtStatus(Status);
    }
    if (OperationId == ZP_TERMINAL_OPERATION_CREATE)
    {
        Status = ZpTerminal_DecodeCreate(Request, RequestLength, &Create);
        if (NT_SUCCESS(Status))
        {
            ResultStatus = ZpTerminal_Create(Client, &Create, &ChannelObject);
        }
        else
        {
            ResultStatus = ZpStatus_FromNtStatus(Status);
        }
        if (ZpStatus_IsSuccess(ResultStatus))
        {
            Status = ZpTerminal_EncodeCreateResponse(
                ChannelObject->Header.ChannelId,
                ChannelObject->ProcessId,
                NULL,
                0,
                ResponseLength);
        }
        else
        {
            Status = STATUS_UNSUCCESSFUL;
        }
        *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
        if (NT_SUCCESS(Status) && *Response == NULL)
        {
            Status = STATUS_NO_MEMORY;
            ResultStatus = ZpStatus_FromNtStatus(Status);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpTerminal_EncodeCreateResponse(
                ChannelObject->Header.ChannelId,
                ChannelObject->ProcessId,
                *Response,
                *ResponseLength,
                ResponseLength);
        }
        if (!NT_SUCCESS(Status) && ChannelObject != NULL)
        {
            ZpTerminal_CommitChannel(ChannelObject, FALSE);
        }
        else
        {
            *Channel = ChannelObject;
        }
        if (!NT_SUCCESS(Status) && ZpStatus_IsSuccess(ResultStatus))
        {
            ResultStatus = ZpStatus_FromNtStatus(Status);
        }
        return ResultStatus;
    }
    if (OperationId != ZP_TERMINAL_OPERATION_RESIZE)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Status = ZpTerminal_DecodeResize(Request,
                                     RequestLength,
                                     &ChannelId,
                                     &Columns,
                                     &Rows);
    return NT_SUCCESS(Status) ?
               ZpTerminal_Resize(Client, ChannelId, Columns, Rows) :
               ZpStatus_FromNtStatus(Status);
}

VOID
ZpTerminal_CommitChannel(
    _Inout_ PZP_CLIENT_TERMINAL_CHANNEL Channel,
    _In_ LOGICAL ResponseSent)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL Removed = FALSE;
    LOGICAL StartWorker = FALSE;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ResponseSent)
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    }
    else
    {
        Status = ZpTerminal_SendWindowLocked(Channel,
                                             ZP_TERMINAL_INPUT_WINDOW_SIZE);
        if (!NT_SUCCESS(Status))
        {
            Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        }
        else
        {
            StartWorker = TRUE;
        }
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        ZpClientLocalChannel_Release(&Channel->Header);
    }
    else if (StartWorker)
    {
        ZpTerminal_StartWorker(Channel);
    }
}
