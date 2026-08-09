#include "Client.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"
#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#define ZP_TERMINAL_CHANNEL_CHUNK_SIZE 0x00010000UL
#define ZP_TERMINAL_INPUT_WINDOW_SIZE 0x00001000UL

struct _ZP_CLIENT_TERMINAL_CHANNEL
{
    ZP_CLIENT_LOCAL_CHANNEL Header;
    LOGICAL WorkerActive;
    RTL_SRWLOCK InputLock;
    RTL_SRWLOCK PseudoConsoleLock;
    ULONGLONG Credit;
    ULONGLONG ReceiveCredit;
    HPCON PseudoConsole;
    HANDLE Input;
    HANDLE Output;
    HANDLE Process;
    ULONG ProcessId;
};

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
    _In_ NTSTATUS Status);

static
VOID
ZpTerminal_ChannelAbort(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ NTSTATUS Status);

static
VOID
ZpTerminal_ChannelDestroy(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel);

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
    _In_ NTSTATUS CloseStatus)
{
    BYTE Body[sizeof(ULONGLONG) + sizeof(ULONG)];
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
    if (Channel->Process != NULL)
    {
        if (PS_WaitForObject(Channel->Process, 0) == STATUS_TIMEOUT)
        {
            NtTerminateProcess(Channel->Process, STATUS_CANCELLED);
        }
        NtClose(Channel->Process);
    }
    if (Channel->PseudoConsole != NULL)
    {
        ClosePseudoConsole(Channel->PseudoConsole);
    }
    if (Channel->Input != NULL)
    {
        NtClose(Channel->Input);
    }
    if (Channel->Output != NULL)
    {
        NtClose(Channel->Output);
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
    _In_ NTSTATUS Status)
{
    PZP_CLIENT_TERMINAL_CHANNEL Channel =
        (PZP_CLIENT_TERMINAL_CHANNEL)LocalChannel;

    UNREFERENCED_PARAMETER(Status);
    NtTerminateProcess(Channel->Process, STATUS_CANCELLED);
}

static
VOID
ZpTerminal_FinishWorker(
    _Inout_ PZP_CLIENT_TERMINAL_CHANNEL Channel,
    _In_ NTSTATUS Status,
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
    ZpClientLocalChannel_Release(&Channel->Header);
    return STATUS_NO_MEMORY;
}

static
VOID
CALLBACK
ZpTerminal_ChannelCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_TERMINAL_CHANNEL Channel = Context;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    FILE_PIPE_LOCAL_INFORMATION PipeInfo;
    PROCESS_BASIC_INFORMATION ProcessInfo;
    IO_STATUS_BLOCK IoStatusBlock;
    PBYTE Body;
    ULONG ReadLength, BytesRead, BodyLength;
    NTSTATUS Status;
    LOGICAL ProcessExited = FALSE;
    LOGICAL PseudoConsoleCloseQueued = FALSE;
    LOGICAL HasPseudoConsole;

    CallbackMayRunLong(Instance);
    Body = Mem_Alloc(sizeof(ULONGLONG) + ZP_TERMINAL_CHANNEL_CHUNK_SIZE);
    if (Body == NULL)
    {
        ZpTerminal_FinishWorker(Channel, STATUS_NO_MEMORY, TRUE);
        return;
    }
    for (;;)
    {
        RtlAcquireSRWLockExclusive(&Object->Lock);
        if (!Channel->Header.Pending)
        {
            Channel->WorkerActive = FALSE;
            Object->CallbackCount--;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            break;
        }
        RtlReleaseSRWLockExclusive(&Object->Lock);

        if (!ProcessExited &&
            PS_WaitForObject(Channel->Process, 0) == STATUS_SUCCESS)
        {
            ProcessExited = TRUE;
        }
        Status = NtQueryInformationFile(Channel->Output,
                                        &IoStatusBlock,
                                        &PipeInfo,
                                        sizeof(PipeInfo),
                                        FilePipeLocalInformation);
        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_PIPE_BROKEN && ProcessExited)
            {
                PipeInfo.ReadDataAvailable = 0;
            }
            else
            {
                Mem_Free(Body);
                ZpTerminal_FinishWorker(Channel, Status, TRUE);
                return;
            }
        }
        if (PipeInfo.ReadDataAvailable == 0)
        {
            if (ProcessExited)
            {
                RtlAcquireSRWLockShared(&Channel->PseudoConsoleLock);
                HasPseudoConsole = Channel->PseudoConsole != NULL;
                RtlReleaseSRWLockShared(&Channel->PseudoConsoleLock);
                if (HasPseudoConsole)
                {
                    if (!PseudoConsoleCloseQueued)
                    {
                        Status = ZpTerminal_QueuePseudoConsoleClose(Channel);
                        if (!NT_SUCCESS(Status))
                        {
                            Mem_Free(Body);
                            ZpTerminal_FinishWorker(Channel, Status, TRUE);
                            return;
                        }
                        PseudoConsoleCloseQueued = TRUE;
                    }
                    PS_DelayExec(1);
                    continue;
                }
                Status = NtQueryInformationProcess(
                    Channel->Process,
                    ProcessBasicInformation,
                    &ProcessInfo,
                    sizeof(ProcessInfo),
                    NULL);
                if (NT_SUCCESS(Status))
                {
                    Status = ProcessInfo.ExitStatus;
                }
                Mem_Free(Body);
                ZpTerminal_FinishWorker(Channel, Status, TRUE);
                return;
            }
            PS_DelayExec(10);
            continue;
        }

        RtlAcquireSRWLockExclusive(&Object->Lock);
        if (!Channel->Header.Pending)
        {
            Channel->WorkerActive = FALSE;
            Object->CallbackCount--;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            break;
        }
        ReadLength = (ULONG)min(min(Channel->Credit,
                                    (ULONGLONG)PipeInfo.ReadDataAvailable),
                                ZP_TERMINAL_CHANNEL_CHUNK_SIZE);
        if (ReadLength == 0)
        {
            RtlReleaseSRWLockExclusive(&Object->Lock);
            PS_DelayExec(10);
            continue;
        }
        Channel->Credit -= ReadLength;
        RtlReleaseSRWLockExclusive(&Object->Lock);

        Status = IO_ReadFile(Channel->Output,
                             NULL,
                             Add2Ptr(Body, sizeof(ULONGLONG)),
                             ReadLength,
                             &BytesRead);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(Body);
            ZpTerminal_FinishWorker(Channel, Status, TRUE);
            return;
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
            Mem_Free(Body);
            ZpTerminal_FinishWorker(Channel, Status, TRUE);
            return;
        }
        RtlAcquireSRWLockExclusive(&Object->Lock);
        if (!Channel->Header.Pending)
        {
            Channel->WorkerActive = FALSE;
            Object->CallbackCount--;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            break;
        }
        Channel->Credit += ReadLength - BytesRead;
        Status = ZpTerminal_SendLocked(Object,
                                       ZpMessageChannelData,
                                       Body,
                                       BodyLength);
        RtlReleaseSRWLockExclusive(&Object->Lock);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(Body);
            ZpTerminal_FinishWorker(Channel, Status, FALSE);
            return;
        }
    }
    Mem_Free(Body);
    ZpClientLocalChannel_Release(&Channel->Header);
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
    LOGICAL Queue = FALSE;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending ||
        MAXULONGLONG - Channel->Credit < CreditBytes)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->Credit += CreditBytes;
    if (!Channel->WorkerActive)
    {
        Channel->WorkerActive = TRUE;
        ZpClientLocalChannel_AddRef(&Channel->Header);
        Object->CallbackCount++;
        Queue = TRUE;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Queue && !TrySubmitThreadpoolCallback(ZpTerminal_ChannelCallback,
                                              Channel,
                                              NULL))
    {
        ZpTerminal_FinishWorker(Channel, STATUS_NO_MEMORY, TRUE);
    }
    return STATUS_SUCCESS;
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
    ULONG BytesWritten;
    NTSTATUS Status;
    LOGICAL Removed = FALSE;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending ||
        Message->Data.Length > Channel->ReceiveCredit)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->ReceiveCredit -= Message->Data.Length;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    RtlAcquireSRWLockExclusive(&Channel->InputLock);
    Status = IO_WriteFile(Channel->Input,
                          NULL,
                          (PVOID)Message->Data.Buffer,
                          Message->Data.Length,
                          &BytesWritten);
    RtlReleaseSRWLockExclusive(&Channel->InputLock);
    if (NT_SUCCESS(Status) && BytesWritten != Message->Data.Length)
    {
        Status = STATUS_UNSUCCESSFUL;
    }
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
        ZpTerminal_SendCloseLocked(Channel, Status);
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        NtTerminateProcess(Channel->Process, STATUS_CANCELLED);
        ZpClientLocalChannel_Release(&Channel->Header);
    }
    return NT_SUCCESS(Status) ? STATUS_SUCCESS : Status;
}

static
NTSTATUS
ZpTerminal_ChannelClose(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ NTSTATUS Status)
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
    ZpClientLocalChannel_Release(&Channel->Header);
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpTerminal_Create(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ const ZP_TERMINAL_CREATE_VIEW* Create,
    _Out_ PZP_CLIENT_TERMINAL_CHANNEL* Channel)
{
    STARTUPINFOEXW StartupInfo = { 0 };
    PROCESS_INFORMATION ProcessInfo = { 0 };
    PZP_CLIENT_TERMINAL_CHANNEL ChannelObject = NULL;
    PPROC_THREAD_ATTRIBUTE_LIST AttributeList = NULL;
    HANDLE InputRead = NULL, InputWrite = NULL;
    HANDLE OutputRead = NULL, OutputWrite = NULL;
    HPCON PseudoConsole = NULL;
    PWCHAR CommandLine = NULL, WorkingDirectory = NULL;
    SIZE_T AttributeListSize = 0;
    COORD Size;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL AttributeListInitialized = FALSE;

    if (Create->Columns > MAXSHORT || Create->Rows > MAXSHORT)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ChannelObject = Mem_Alloc(sizeof(*ChannelObject));
    CommandLine = Mem_Alloc(((SIZE_T)Create->CommandLine.Length + 1) *
                            sizeof(WCHAR));
    if (Create->WorkingDirectory.Length != 0)
    {
        WorkingDirectory = Mem_Alloc(
            ((SIZE_T)Create->WorkingDirectory.Length + 1) * sizeof(WCHAR));
    }
    if (ChannelObject == NULL || CommandLine == NULL ||
        (Create->WorkingDirectory.Length != 0 && WorkingDirectory == NULL))
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    RtlZeroMemory(ChannelObject, sizeof(*ChannelObject));
    RtlCopyMemory(CommandLine,
                  Create->CommandLine.Buffer,
                  (SIZE_T)Create->CommandLine.Length * sizeof(WCHAR));
    CommandLine[Create->CommandLine.Length] = UNICODE_NULL;
    if (WorkingDirectory != NULL)
    {
        RtlCopyMemory(WorkingDirectory,
                      Create->WorkingDirectory.Buffer,
                      (SIZE_T)Create->WorkingDirectory.Length * sizeof(WCHAR));
        WorkingDirectory[Create->WorkingDirectory.Length] = UNICODE_NULL;
    }
    if (!CreatePipe(&InputRead, &InputWrite, NULL, 0) ||
        !CreatePipe(&OutputRead, &OutputWrite, NULL, 0))
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    Size.X = (SHORT)Create->Columns;
    Size.Y = (SHORT)Create->Rows;
    Result = CreatePseudoConsole(Size,
                                 InputRead,
                                 OutputWrite,
                                 0,
                                 &PseudoConsole);
    if (FAILED(Result))
    {
        Status = NTSTATUS_FROM_WIN32(HRESULT_CODE(Result));
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
        Status = NTSTATUS_FROM_WIN32(GetLastError());
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
        Status = NTSTATUS_FROM_WIN32(GetLastError());
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
        Status = NTSTATUS_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    NtClose(InputRead);
    InputRead = NULL;
    NtClose(OutputWrite);
    OutputWrite = NULL;
    NtClose(ProcessInfo.hThread);
    ProcessInfo.hThread = NULL;
    ChannelObject->PseudoConsole = PseudoConsole;
    ChannelObject->Input = InputWrite;
    ChannelObject->Output = OutputRead;
    ChannelObject->Process = ProcessInfo.hProcess;
    ChannelObject->ProcessId = ProcessInfo.dwProcessId;
    PseudoConsole = NULL;
    InputWrite = NULL;
    OutputRead = NULL;
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
    if (InputRead != NULL)
    {
        NtClose(InputRead);
    }
    if (InputWrite != NULL)
    {
        NtClose(InputWrite);
    }
    if (OutputRead != NULL)
    {
        NtClose(OutputRead);
    }
    if (OutputWrite != NULL)
    {
        NtClose(OutputWrite);
    }
    Mem_Free(WorkingDirectory);
    Mem_Free(CommandLine);
    if (ChannelObject != NULL)
    {
        ZpTerminal_Destroy(ChannelObject);
    }
    return Status;
}

static
NTSTATUS
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
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpClientLocalChannel_ReferenceById(Object,
                                                ChannelId,
                                                ZP_TERMINAL_MODULE_ID,
                                                &LocalChannel);
    if (!NT_SUCCESS(Status))
    {
        return Status;
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
               STATUS_SUCCESS : NTSTATUS_FROM_WIN32(HRESULT_CODE(Result));
}

NTSTATUS
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

    *Channel = NULL;
    if (OperationId == ZP_TERMINAL_OPERATION_CREATE)
    {
        Status = ZpTerminal_DecodeCreate(Request, RequestLength, &Create);
        if (NT_SUCCESS(Status))
        {
            Status = ZpTerminal_Create(Client, &Create, &ChannelObject);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpTerminal_EncodeCreateResponse(
                ChannelObject->Header.ChannelId,
                ChannelObject->ProcessId,
                NULL,
                0,
                ResponseLength);
        }
        *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
        if (NT_SUCCESS(Status) && *Response == NULL)
        {
            Status = STATUS_NO_MEMORY;
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
        return Status;
    }
    if (OperationId != ZP_TERMINAL_OPERATION_RESIZE)
    {
        return STATUS_NOT_SUPPORTED;
    }
    Status = ZpTerminal_DecodeResize(Request,
                                     RequestLength,
                                     &ChannelId,
                                     &Columns,
                                     &Rows);
    return NT_SUCCESS(Status) ?
               ZpTerminal_Resize(Client, ChannelId, Columns, Rows) : Status;
}

VOID
ZpTerminal_CommitChannel(
    _Inout_ PZP_CLIENT_TERMINAL_CHANNEL Channel,
    _In_ LOGICAL ResponseSent)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL Removed = FALSE;

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
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        ZpClientLocalChannel_Release(&Channel->Header);
    }
}
