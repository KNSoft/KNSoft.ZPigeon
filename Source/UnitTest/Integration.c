#include "UnitTest.h"

#include <KNSoft/ZPigeon/Client.h>
#include <KNSoft/ZPigeon/Server.h>
#include <KNSoft/ZPigeon/Terminal.h>

#include "../KNSoft.ZPigeon.Client.SDK/Client.inl"

#include <Bcrypt.h>
#include <Ncrypt.h>
#include <stdio.h>
#include <Ws2tcpip.h>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Ncrypt.lib")
#pragma comment(lib, "Ws2_32.lib")

#define SDK_INTEGRATION_TIMEOUT_MILLISECONDS 10000

typedef struct _SDK_INTEGRATION_CONTEXT
{
    HANDLE ServerRunningEvent;
    HANDLE ClientReadyEvent;
    HANDLE ClientRetryWaitEvent;
    HANDLE ClientStoppedEvent;
    HANDLE ServerReadyEvent;
    HANDLE ServerStoppedEvent;
    HANDLE ClientPongEvent;
    HANDLE SystemInfoEvent;
    HANDLE ProcessListEvent;
    HANDLE ProcessInfoEvent;
    HANDLE ServiceListEvent;
    HANDLE ServiceInfoEvent;
    HANDLE ProcessTerminateEvent;
    HANDLE ServiceControlEvent;
    HANDLE FileInfoEvent;
    HANDLE FileListEvent;
    HANDLE FilePageEvent;
    HANDLE EventLogPageEvent;
    HANDLE FileHashEvent;
    HANDLE FileReadEvent;
    HANDLE FileWriteEvent;
    HANDLE TerminalWritableEvent;
    HANDLE TerminalResizeEvent;
    HANDLE TerminalCloseEvent;
    HANDLE RegistryPageEvent;
    HANDLE RegistryValueEvent;
    HANDLE RegistryStatusEvent;
    ZP_CONNECTION_HANDLE Connection;
    ZP_STATUS ClientReadyStatus;
    ZP_STATUS ClientStoppedStatus;
    ZP_STATUS ServerReadyStatus;
    ZP_STATUS ServerStoppedStatus;
    ULONGLONG ClientPongToken;
    ZP_STATUS SystemInfoStatus;
    ZP_SYSTEM_ARCHITECTURE SystemArchitecture;
    ULONG SystemProcessorCount;
    ULONGLONG SystemPhysicalMemoryBytes;
    ULONG SystemComputerNameLength;
    ZP_STATUS ProcessListStatus;
    ULONG ProcessCount;
    LOGICAL FoundCurrentProcess;
    volatile LONG ProcessCompletionCount;
    LONG ExpectedProcessCompletions;
    LOGICAL CollectProcessDetails;
    ZP_STATUS ProcessInfoStatus;
    ULONG ProcessInfoId;
    ULONG ProcessInfoThreadCount;
    ULONGLONG ProcessInfoCreateTime;
    ULONG ProcessInfoImageNameLength;
    ZP_STATUS ServiceListStatus;
    ULONG ServiceCount;
    LOGICAL FoundNamedService;
    WCHAR ServiceName[256];
    ULONG ServiceNameLength;
    ZP_STATUS ServiceInfoStatus;
    ULONG ServiceInfoType;
    ULONG ServiceInfoStartType;
    ULONG ServiceInfoNameLength;
    ULONG ServiceInfoDisplayNameLength;
    ULONG ServiceInfoBinaryPathLength;
    ZP_STATUS ProcessTerminateStatus;
    ZP_STATUS ServiceControlStatus;
    ZP_STATUS FileInfoStatus;
    ULONG FileAttributes;
    ULONGLONG FileSize;
    ULONGLONG FileLastWriteTime;
    ZP_STATUS FileListStatus;
    ULONG FileCount;
    WCHAR ExpectedFileName[MAX_PATH];
    ULONG ExpectedFileNameLength;
    LOGICAL FoundExpectedFile;
    ZP_STATUS FilePageStatus;
    ULONG FilePageCount;
    WCHAR FilePageCursor[MAX_PATH];
    ULONG FilePageCursorLength;
    WCHAR FilePageName[MAX_PATH];
    ULONG FilePageNameLength;
    ZP_STATUS EventLogPageStatus;
    ULONG EventLogPageCount;
    BOOLEAN EventLogHasMore;
    WCHAR EventLogBookmark[4096];
    ULONG EventLogBookmarkLength;
    ULONG EventLogXmlLength;
    ZP_STATUS FileHashStatus;
    ZP_FILE_HASH_ALGORITHM FileHashAlgorithm;
    ULONGLONG FileHashSize;
    BYTE FileDigest[ZP_FILE_SHA256_SIZE];
    ZP_STATUS FileOpenReadStatus;
    ZP_STATUS FileReadCloseStatus;
    ZP_CHANNEL_HANDLE FileReadChannel;
    ULONGLONG FileReadSize;
    ULONGLONG FileReadOffset;
    ULONGLONG FileReadBytes;
    ULONGLONG FileReadHash;
    ZP_STATUS FileOpenWriteStatus;
    ZP_STATUS FileWriteStatus;
    ZP_CHANNEL_HANDLE FileWriteChannel;
    const BYTE* FileWriteData;
    ULONG FileWriteLength;
    ULONG FileWriteOffset;
    LOGICAL CancelFileWrite;
    ZP_STATUS TerminalCreateStatus;
    ZP_STATUS TerminalResizeStatus;
    ZP_STATUS TerminalCloseStatus;
    ZP_CHANNEL_HANDLE TerminalChannel;
    ULONG TerminalProcessId;
    ULONG TerminalWritableCredit;
    ULONGLONG TerminalDataBytes;
    ZP_STATUS RegistryPageStatus;
    ULONG RegistryRecordCount;
    LOGICAL RegistryPageValues;
    BOOLEAN RegistryHasMore;
    WCHAR RegistryRecordName[256];
    ULONG RegistryRecordNameLength;
    WCHAR RegistryCursor[256];
    ULONG RegistryCursorLength;
    ZP_STATUS RegistryValueStatus;
    ULONG RegistryValueType;
    BYTE RegistryValueData[64];
    ULONG RegistryValueDataLength;
    ZP_STATUS RegistryStatus;
} SDK_INTEGRATION_CONTEXT, *PSDK_INTEGRATION_CONTEXT;

static
VOID
NTAPI
SDKIntegration_ClientStateCallback(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ZP_CLIENT_STATE State,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Client);
    if (State == ZpClientStateReady)
    {
        TestContext->ClientReadyStatus = Status;
        SetEvent(TestContext->ClientReadyEvent);
    }
    else if (State == ZpClientStateRetryWait)
    {
        SetEvent(TestContext->ClientRetryWaitEvent);
    }
    else if (State == ZpClientStateStopped)
    {
        TestContext->ClientStoppedStatus = Status;
        SetEvent(TestContext->ClientStoppedEvent);
    }
}

static
VOID
NTAPI
SDKIntegration_ClientPongCallback(
    _In_ ZP_CLIENT_HANDLE Client,
    _In_ ULONGLONG Token,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Client);
    TestContext->ClientPongToken = Token;
    SetEvent(TestContext->ClientPongEvent);
}

static
VOID
NTAPI
SDKIntegration_SystemInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_SYSTEM_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->SystemArchitecture = Info->Architecture;
        TestContext->SystemProcessorCount = Info->ProcessorCount;
        TestContext->SystemPhysicalMemoryBytes = Info->PhysicalMemoryBytes;
        TestContext->SystemComputerNameLength = Info->ComputerName.Length;
    }
    TestContext->SystemInfoStatus = Status;
    SetEvent(TestContext->SystemInfoEvent);
}

static
VOID
NTAPI
SDKIntegration_ProcessListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_PROCESS_LIST_VIEW Processes,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;
    ZP_PROCESS_RECORD_VIEW Process;
    ULONG Index;

    UNREFERENCED_PARAMETER(Request);
    if (ZpStatus_IsSuccess(Status) && TestContext->CollectProcessDetails)
    {
        TestContext->ProcessCount = Processes->Count;
        for (Index = 0; Index < Processes->Count; Index++)
        {
            Status = ZpStatus_FromNtStatus(
                ZpProcess_GetRecord(Processes, Index, &Process));
            if (!ZpStatus_IsSuccess(Status))
            {
                break;
            }
            if (Process.ProcessId == GetCurrentProcessId())
            {
                TestContext->FoundCurrentProcess = TRUE;
                TestContext->ProcessInfoCreateTime = Process.CreateTime;
            }
        }
    }
    TestContext->ProcessListStatus = Status;
    if (InterlockedIncrement(&TestContext->ProcessCompletionCount) >=
        TestContext->ExpectedProcessCompletions)
    {
        SetEvent(TestContext->ProcessListEvent);
    }
}

static
VOID
NTAPI
SDKIntegration_ProcessInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_PROCESS_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->ProcessInfoId = Info->ProcessId;
        TestContext->ProcessInfoThreadCount = Info->ThreadCount;
        TestContext->ProcessInfoCreateTime = Info->CreateTime;
        TestContext->ProcessInfoImageNameLength = Info->ImageName.Length;
    }
    TestContext->ProcessInfoStatus = Status;
    SetEvent(TestContext->ProcessInfoEvent);
}

static
VOID
NTAPI
SDKIntegration_ServiceListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_SERVICE_LIST_VIEW Services,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;
    ZP_SERVICE_RECORD_VIEW Service;
    ULONG Index;

    UNREFERENCED_PARAMETER(Request);
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->ServiceCount = Services->Count;
        for (Index = 0; Index < Services->Count; Index++)
        {
            Status = ZpStatus_FromNtStatus(
                ZpService_GetRecord(Services, Index, &Service));
            if (!ZpStatus_IsSuccess(Status))
            {
                break;
            }
            if (Service.ServiceName.Length != 0 &&
                Service.DisplayName.Length != 0)
            {
                TestContext->FoundNamedService = TRUE;
                if (TestContext->ServiceNameLength == 0 &&
                    Service.ServiceName.Length <
                        ARRAYSIZE(TestContext->ServiceName))
                {
                    RtlCopyMemory(TestContext->ServiceName,
                                  Service.ServiceName.Buffer,
                                  (SIZE_T)Service.ServiceName.Length *
                                      sizeof(WCHAR));
                    TestContext->ServiceName[Service.ServiceName.Length] =
                        UNICODE_NULL;
                    TestContext->ServiceNameLength =
                        Service.ServiceName.Length;
                }
            }
        }
    }
    TestContext->ServiceListStatus = Status;
    SetEvent(TestContext->ServiceListEvent);
}

static
VOID
NTAPI
SDKIntegration_ServiceInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_SERVICE_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->ServiceInfoType = Info->ServiceType;
        TestContext->ServiceInfoStartType = Info->StartType;
        TestContext->ServiceInfoNameLength = Info->ServiceName.Length;
        TestContext->ServiceInfoDisplayNameLength = Info->DisplayName.Length;
        TestContext->ServiceInfoBinaryPathLength = Info->BinaryPathName.Length;
    }
    TestContext->ServiceInfoStatus = Status;
    SetEvent(TestContext->ServiceInfoEvent);
}

static
VOID
NTAPI
SDKIntegration_ProcessTerminateCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->ProcessTerminateStatus = Status;
    SetEvent(TestContext->ProcessTerminateEvent);
}

static
VOID
NTAPI
SDKIntegration_ServiceControlCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->ServiceControlStatus = Status;
    SetEvent(TestContext->ServiceControlEvent);
}

static
VOID
NTAPI
SDKIntegration_FileInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_INFO Info,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->FileAttributes = Info->Attributes;
        TestContext->FileSize = Info->Size;
        TestContext->FileLastWriteTime = Info->LastWriteTime;
    }
    TestContext->FileInfoStatus = Status;
    SetEvent(TestContext->FileInfoEvent);
}

static
VOID
NTAPI
SDKIntegration_FileListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_LIST_VIEW Files,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;
    ZP_FILE_RECORD_VIEW File;
    ULONG Index;

    UNREFERENCED_PARAMETER(Request);
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->FileCount = Files->Count;
        for (Index = 0; Index < Files->Count; Index++)
        {
            Status = ZpStatus_FromNtStatus(
                ZpFile_GetRecord(Files, Index, &File));
            if (!ZpStatus_IsSuccess(Status))
            {
                break;
            }
            if (File.Name.Length == TestContext->ExpectedFileNameLength &&
                RtlCompareMemory(File.Name.Buffer,
                                 TestContext->ExpectedFileName,
                                 (SIZE_T)File.Name.Length * sizeof(WCHAR)) ==
                    (SIZE_T)File.Name.Length * sizeof(WCHAR))
            {
                TestContext->FoundExpectedFile = TRUE;
            }
        }
    }
    TestContext->FileListStatus = Status;
    SetEvent(TestContext->FileListEvent);
}

static
VOID
NTAPI
SDKIntegration_FilePageCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;
    ZP_FILE_RECORD_VIEW File;

    UNREFERENCED_PARAMETER(Request);
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->FilePageCount = Page->Files.Count;
        TestContext->FilePageCursorLength = Page->NextCursor.Length;
        if (Page->NextCursor.Length >= ARRAYSIZE(TestContext->FilePageCursor))
        {
            Status = ZpStatus_FromNtStatus(STATUS_NAME_TOO_LONG);
        }
        else if (Page->NextCursor.Length != 0)
        {
            RtlCopyMemory(TestContext->FilePageCursor,
                          Page->NextCursor.Buffer,
                          (SIZE_T)Page->NextCursor.Length * sizeof(WCHAR));
        }
    }
    if (ZpStatus_IsSuccess(Status) && Page->Files.Count != 0)
    {
        Status = ZpStatus_FromNtStatus(
            ZpFile_GetRecord(&Page->Files, 0, &File));
        if (ZpStatus_IsSuccess(Status) &&
            File.Name.Length >= ARRAYSIZE(TestContext->FilePageName))
        {
            Status = ZpStatus_FromNtStatus(STATUS_NAME_TOO_LONG);
        }
        if (ZpStatus_IsSuccess(Status))
        {
            TestContext->FilePageNameLength = File.Name.Length;
            RtlCopyMemory(TestContext->FilePageName,
                          File.Name.Buffer,
                          (SIZE_T)File.Name.Length * sizeof(WCHAR));
        }
    }
    TestContext->FilePageStatus = Status;
    SetEvent(TestContext->FilePageEvent);
}

static
VOID
NTAPI
SDKIntegration_EventLogPageCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_EVENT_LOG_PAGE_VIEW* Page,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;
    ZP_EVENT_LOG_RECORD_VIEW Record;
    ULONG Index;

    UNREFERENCED_PARAMETER(Request);
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->EventLogPageCount = Page->Records.Count;
        TestContext->EventLogHasMore = Page->HasMore;
        TestContext->EventLogBookmarkLength = Page->NextBookmark.Length;
        if (Page->NextBookmark.Length >=
            ARRAYSIZE(TestContext->EventLogBookmark))
        {
            Status = ZpStatus_FromNtStatus(STATUS_NAME_TOO_LONG);
        }
        else if (Page->NextBookmark.Length != 0)
        {
            RtlCopyMemory(TestContext->EventLogBookmark,
                          Page->NextBookmark.Buffer,
                          (SIZE_T)Page->NextBookmark.Length * sizeof(WCHAR));
        }
    }
    for (Index = 0;
         ZpStatus_IsSuccess(Status) && Index < Page->Records.Count;
         Index++)
    {
        Status = ZpStatus_FromNtStatus(
            ZpEventLog_GetRecord(&Page->Records, Index, &Record));
        if (ZpStatus_IsSuccess(Status) &&
            (Record.Bookmark.Length == 0 || Record.Xml.Length == 0))
        {
            Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
        }
        if (ZpStatus_IsSuccess(Status))
        {
            TestContext->EventLogXmlLength = Record.Xml.Length;
        }
    }
    TestContext->EventLogPageStatus = Status;
    SetEvent(TestContext->EventLogPageEvent);
}

static
VOID
NTAPI
SDKIntegration_FileHashCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_HASH_VIEW Hash,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->FileHashAlgorithm = Hash->Algorithm;
        TestContext->FileHashSize = Hash->FileSize;
        RtlCopyMemory(TestContext->FileDigest,
                      Hash->Digest.Buffer,
                      Hash->Digest.Length);
    }
    TestContext->FileHashStatus = Status;
    SetEvent(TestContext->FileHashEvent);
}

static
VOID
NTAPI
SDKIntegration_FileOpenReadCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_ ULONGLONG Offset,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->FileReadChannel = Channel;
    TestContext->FileReadSize = FileSize;
    TestContext->FileReadOffset = Offset;
    TestContext->FileOpenReadStatus = Status;
    if (!ZpStatus_IsSuccess(Status))
    {
        SetEvent(TestContext->FileReadEvent);
    }
}

static
VOID
NTAPI
SDKIntegration_FileOpenWriteCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->FileWriteChannel = Channel;
    if (ZpStatus_IsSuccess(Status) && FileSize != TestContext->FileWriteLength)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    TestContext->FileOpenWriteStatus = Status;
}

static
VOID
NTAPI
SDKIntegration_FileWriteWritableCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;
    ULONG WriteLength;
    NTSTATUS Status = STATUS_SUCCESS;

    if (TestContext->CancelFileWrite)
    {
        ZpChannel_Cancel(Channel);
        return;
    }

    while (CreditBytes != 0 &&
           TestContext->FileWriteOffset < TestContext->FileWriteLength)
    {
        WriteLength = min(min(CreditBytes, 0x10000UL),
                          TestContext->FileWriteLength -
                              TestContext->FileWriteOffset);
        Status = ZpChannel_Send(Channel,
                                TestContext->FileWriteData +
                                    TestContext->FileWriteOffset,
                                WriteLength);
        if (!NT_SUCCESS(Status))
        {
            break;
        }
        TestContext->FileWriteOffset += WriteLength;
        CreditBytes -= WriteLength;
    }
    if (!NT_SUCCESS(Status))
    {
        TestContext->FileWriteStatus = ZpStatus_FromNtStatus(Status);
        ZpChannel_Cancel(Channel);
    }
}

static
VOID
NTAPI
SDKIntegration_FileWriteCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    TestContext->FileWriteStatus = Status;
    SetEvent(TestContext->FileWriteEvent);
}

static
VOID
NTAPI
SDKIntegration_ChannelDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;
    ULONG Index;

    UNREFERENCED_PARAMETER(Channel);
    for (Index = 0; Index < Data->Length; Index++)
    {
        TestContext->FileReadHash ^= Data->Buffer[Index];
        TestContext->FileReadHash *= 1099511628211ULL;
    }
    TestContext->FileReadBytes += Data->Length;
}

static
VOID
NTAPI
SDKIntegration_ChannelCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    TestContext->FileReadCloseStatus = Status;
    SetEvent(TestContext->FileReadEvent);
}

static
VOID
NTAPI
SDKIntegration_TerminalCreateCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG ProcessId,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->TerminalChannel = Channel;
    TestContext->TerminalProcessId = ProcessId;
    TestContext->TerminalCreateStatus = Status;
    if (!ZpStatus_IsSuccess(Status))
    {
        SetEvent(TestContext->TerminalCloseEvent);
    }
}

static
VOID
NTAPI
SDKIntegration_TerminalDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    TestContext->TerminalDataBytes += Data->Length;
}

static
VOID
NTAPI
SDKIntegration_TerminalWritableCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    TestContext->TerminalWritableCredit += CreditBytes;
    SetEvent(TestContext->TerminalWritableEvent);
}

static
VOID
NTAPI
SDKIntegration_TerminalResizeCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->TerminalResizeStatus = Status;
    SetEvent(TestContext->TerminalResizeEvent);
}

static
VOID
NTAPI
SDKIntegration_TerminalCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Channel);
    TestContext->TerminalCloseStatus = Status;
    SetEvent(TestContext->TerminalCloseEvent);
}

static
VOID
NTAPI
SDKIntegration_RegistryPageCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;
    ZP_REGISTRY_VALUE_RECORD_VIEW Record;

    UNREFERENCED_PARAMETER(Request);
    TestContext->RegistryRecordCount = 0;
    TestContext->RegistryRecordNameLength = 0;
    TestContext->RegistryCursorLength = 0;
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->RegistryRecordCount = Page->Records.Count;
        TestContext->RegistryHasMore = Page->HasMore;
        TestContext->RegistryCursorLength = Page->NextCursor.Length;
        if (Page->NextCursor.Length <= ARRAYSIZE(TestContext->RegistryCursor) &&
            Page->NextCursor.Length != 0)
        {
            RtlCopyMemory(TestContext->RegistryCursor,
                          Page->NextCursor.Buffer,
                          (SIZE_T)Page->NextCursor.Length * sizeof(WCHAR));
        }
        if (TestContext->RegistryPageValues &&
            Page->Records.Count != 0 &&
            NT_SUCCESS(ZpRegistry_GetValueRecord(&Page->Records, 0, &Record)))
        {
            TestContext->RegistryRecordNameLength = Record.Name.Length;
            if (Record.Name.Length <=
                    ARRAYSIZE(TestContext->RegistryRecordName) &&
                Record.Name.Length != 0)
            {
                RtlCopyMemory(TestContext->RegistryRecordName,
                              Record.Name.Buffer,
                              (SIZE_T)Record.Name.Length * sizeof(WCHAR));
            }
        }
    }
    TestContext->RegistryPageStatus = Status;
    SetEvent(TestContext->RegistryPageEvent);
}

static
VOID
NTAPI
SDKIntegration_RegistryValueCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_VALUE_VIEW Value,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->RegistryValueDataLength = 0;
    if (ZpStatus_IsSuccess(Status))
    {
        TestContext->RegistryValueType = Value->Type;
        TestContext->RegistryValueDataLength = Value->Data.Length;
        if (Value->Data.Length <= sizeof(TestContext->RegistryValueData) &&
            Value->Data.Length != 0)
        {
            RtlCopyMemory(TestContext->RegistryValueData,
                          Value->Data.Buffer,
                          Value->Data.Length);
        }
    }
    TestContext->RegistryValueStatus = Status;
    SetEvent(TestContext->RegistryValueEvent);
}

static
VOID
NTAPI
SDKIntegration_RegistryStatusCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->RegistryStatus = Status;
    SetEvent(TestContext->RegistryStatusEvent);
}

static
LOGICAL
SDKIntegration_HashFile(
    _In_ PCWSTR Path,
    _In_ ULONGLONG Offset,
    _Out_ PULONGLONG Bytes,
    _Out_ PULONGLONG Hash)
{
    BYTE Buffer[0x10000];
    LARGE_INTEGER Position;
    HANDLE File;
    DWORD BytesRead;
    ULONG Index;
    LOGICAL Result = FALSE;

    File = CreateFileW(Path,
                       GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                       NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    Position.QuadPart = Offset;
    if (!SetFilePointerEx(File, Position, NULL, FILE_BEGIN))
    {
        goto Cleanup;
    }
    *Bytes = 0;
    *Hash = 1469598103934665603ULL;
    do
    {
        if (!ReadFile(File, Buffer, sizeof(Buffer), &BytesRead, NULL))
        {
            goto Cleanup;
        }
        for (Index = 0; Index < BytesRead; Index++)
        {
            *Hash ^= Buffer[Index];
            *Hash *= 1099511628211ULL;
        }
        *Bytes += BytesRead;
    } while (BytesRead != 0);
    Result = TRUE;

Cleanup:
    CloseHandle(File);
    return Result;
}

static
LOGICAL
SDKIntegration_HashFileSha256(
    _In_ PCWSTR Path,
    _Out_writes_bytes_(ZP_FILE_SHA256_SIZE) BYTE* Digest)
{
    BCRYPT_ALG_HANDLE Algorithm = NULL;
    BCRYPT_HASH_HANDLE Hash = NULL;
    BYTE Buffer[0x10000];
    HANDLE File;
    DWORD BytesRead;
    NTSTATUS Status;
    LOGICAL Result = FALSE;

    File = CreateFileW(Path,
                       GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                       NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    Status = BCryptOpenAlgorithmProvider(&Algorithm,
                                         BCRYPT_SHA256_ALGORITHM,
                                         NULL,
                                         0);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    Status = BCryptCreateHash(Algorithm, &Hash, NULL, 0, NULL, 0, 0);
    while (NT_SUCCESS(Status))
    {
        if (!ReadFile(File, Buffer, sizeof(Buffer), &BytesRead, NULL))
        {
            goto Cleanup;
        }
        if (BytesRead == 0)
        {
            break;
        }
        Status = BCryptHashData(Hash, Buffer, BytesRead, 0);
    }
    if (NT_SUCCESS(Status))
    {
        Status = BCryptFinishHash(Hash, Digest, ZP_FILE_SHA256_SIZE, 0);
    }
    Result = NT_SUCCESS(Status);

Cleanup:
    if (Hash != NULL)
    {
        BCryptDestroyHash(Hash);
    }
    if (Algorithm != NULL)
    {
        BCryptCloseAlgorithmProvider(Algorithm, 0);
    }
    CloseHandle(File);
    return Result;
}

static
VOID
NTAPI
SDKIntegration_ServerStateCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_SERVER_STATE State,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Server);
    if (State == ZpServerStateRunning)
    {
        if (ZpStatus_IsSuccess(Status))
        {
            SetEvent(TestContext->ServerRunningEvent);
        }
    }
    else if (State == ZpServerStateStopped)
    {
        TestContext->ServerStoppedStatus = Status;
        SetEvent(TestContext->ServerStoppedEvent);
    }
}

static
VOID
NTAPI
SDKIntegration_ServerConnectionCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_CONNECTION_PHASE Phase,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PSDK_INTEGRATION_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Server);
    if (Phase == ZpConnectionPhaseReady)
    {
        ZP_CONNECTION_HANDLE Previous;

        ZpConnection_AddRef(Connection);
        Previous = InterlockedExchangePointer((PVOID volatile*)&TestContext->Connection,
                                              Connection);
        if (Previous != NULL)
        {
            ZpConnection_Release(Previous);
        }
        TestContext->ServerReadyStatus = Status;
        SetEvent(TestContext->ServerReadyEvent);
    }
    else if (Phase == ZpConnectionPhaseClosed)
    {
        Connection = InterlockedExchangePointer(
            (PVOID volatile*)&TestContext->Connection,
            NULL);
        if (Connection != NULL)
        {
            ZpConnection_Release(Connection);
        }
    }
}

static
PCCERT_CONTEXT
SDKIntegration_CreateCertificate(
    _Out_ HCERTSTORE* Store)
{
    static const WCHAR Subject[] = L"CN=localhost";
    static const WCHAR ServerName[] = L"localhost";
    static PSTR EnhancedKeyUsages[] = { szOID_PKIX_KP_SERVER_AUTH };
    CERT_ALT_NAME_ENTRY AlternateNameEntry = { CERT_ALT_NAME_DNS_NAME };
    CERT_ALT_NAME_INFO AlternateNames = { 1, &AlternateNameEntry };
    CERT_ENHKEY_USAGE EnhancedKeyUsage = { ARRAYSIZE(EnhancedKeyUsages), EnhancedKeyUsages };
    CERT_EXTENSION Extensions[3] = { 0 };
    CERT_EXTENSIONS CertificateExtensions = { ARRAYSIZE(Extensions), Extensions };
    CERT_NAME_BLOB SubjectName = { 0 };
    SYSTEMTIME StartTime = { 0 }, EndTime = { 0 };
    PCCERT_CONTEXT Certificate = NULL, StoredCertificate = NULL;
    PBYTE AlternateNamesEncoded = NULL, EnhancedKeyUsageEncoded = NULL;
    BYTE KeyUsage = CERT_DIGITAL_SIGNATURE_KEY_USAGE;
    CRYPT_BIT_BLOB KeyUsageBlob = { sizeof(KeyUsage), &KeyUsage, 0 };
    DWORD AlternateNamesSize = 0, EnhancedKeyUsageSize = 0;
    DWORD KeyUsageSize = sizeof(KeyUsage);

    *Store = NULL;
    if (!CertStrToNameW(X509_ASN_ENCODING,
                        Subject,
                        CERT_X500_NAME_STR,
                        NULL,
                        NULL,
                        &SubjectName.cbData,
                        NULL))
    {
        goto Cleanup;
    }
    SubjectName.pbData = HeapAlloc(GetProcessHeap(), 0, SubjectName.cbData);
    if (SubjectName.pbData == NULL ||
        !CertStrToNameW(X509_ASN_ENCODING,
                        Subject,
                        CERT_X500_NAME_STR,
                        NULL,
                        SubjectName.pbData,
                        &SubjectName.cbData,
                        NULL))
    {
        goto Cleanup;
    }

    AlternateNameEntry.pwszDNSName = (PWSTR)ServerName;
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING,
                             X509_ALTERNATE_NAME,
                             &AlternateNames,
                             CRYPT_ENCODE_ALLOC_FLAG,
                             NULL,
                             &AlternateNamesEncoded,
                             &AlternateNamesSize) ||
        !CryptEncodeObjectEx(X509_ASN_ENCODING,
                             X509_ENHANCED_KEY_USAGE,
                             &EnhancedKeyUsage,
                             CRYPT_ENCODE_ALLOC_FLAG,
                             NULL,
                             &EnhancedKeyUsageEncoded,
                             &EnhancedKeyUsageSize))
    {
        goto Cleanup;
    }
    Extensions[0].pszObjId = szOID_SUBJECT_ALT_NAME2;
    Extensions[0].Value.cbData = AlternateNamesSize;
    Extensions[0].Value.pbData = AlternateNamesEncoded;
    Extensions[1].pszObjId = szOID_ENHANCED_KEY_USAGE;
    Extensions[1].Value.cbData = EnhancedKeyUsageSize;
    Extensions[1].Value.pbData = EnhancedKeyUsageEncoded;
    Extensions[2].pszObjId = szOID_KEY_USAGE;
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING,
                             X509_KEY_USAGE,
                             &KeyUsageBlob,
                             CRYPT_ENCODE_ALLOC_FLAG,
                             NULL,
                             &Extensions[2].Value.pbData,
                             &KeyUsageSize))
    {
        goto Cleanup;
    }
    Extensions[2].Value.cbData = KeyUsageSize;

    GetSystemTime(&StartTime);
    EndTime = StartTime;
    if (StartTime.wDay > 1)
    {
        StartTime.wDay--;
    }
    EndTime.wYear += 1;
    Certificate = CertCreateSelfSignCertificate(
        0,
        &SubjectName,
        0,
        NULL,
        NULL,
        &StartTime,
        &EndTime,
        &CertificateExtensions);
    if (Certificate != NULL)
    {
        *Store = CertOpenStore(CERT_STORE_PROV_MEMORY,
                               X509_ASN_ENCODING,
                               0,
                               CERT_STORE_CREATE_NEW_FLAG,
                               NULL);
        if (*Store != NULL &&
            CertAddCertificateContextToStore(*Store,
                                             Certificate,
                                             CERT_STORE_ADD_ALWAYS,
                                             &StoredCertificate))
        {
            CertFreeCertificateContext(Certificate);
            Certificate = StoredCertificate;
        }
        else
        {
            CertFreeCertificateContext(Certificate);
            Certificate = NULL;
        }
    }

Cleanup:
    if (SubjectName.pbData != NULL)
    {
        HeapFree(GetProcessHeap(), 0, SubjectName.pbData);
    }
    LocalFree(AlternateNamesEncoded);
    LocalFree(EnhancedKeyUsageEncoded);
    LocalFree(Extensions[2].Value.pbData);
    if (Certificate == NULL)
    {
        if (*Store != NULL)
        {
            CertCloseStore(*Store, 0);
            *Store = NULL;
        }
    }
    return Certificate;
}

static
VOID
SDKIntegration_DeleteCertificateKey(
    _In_ PCCERT_CONTEXT Certificate)
{
    PCRYPT_KEY_PROV_INFO KeyProviderInfo;
    NCRYPT_PROV_HANDLE KeyProvider = 0;
    NCRYPT_KEY_HANDLE Key = 0;
    HCRYPTPROV Provider = 0;
    DWORD Size = 0;

    if (!CertGetCertificateContextProperty(Certificate,
                                           CERT_KEY_PROV_INFO_PROP_ID,
                                           NULL,
                                           &Size))
    {
        return;
    }
    KeyProviderInfo = HeapAlloc(GetProcessHeap(), 0, Size);
    if (KeyProviderInfo == NULL ||
        !CertGetCertificateContextProperty(Certificate,
                                           CERT_KEY_PROV_INFO_PROP_ID,
                                           KeyProviderInfo,
                                           &Size))
    {
        if (KeyProviderInfo != NULL)
        {
            HeapFree(GetProcessHeap(), 0, KeyProviderInfo);
        }
        return;
    }
    if (KeyProviderInfo->dwKeySpec == CERT_NCRYPT_KEY_SPEC)
    {
        if (NCryptOpenStorageProvider(&KeyProvider,
                                      KeyProviderInfo->pwszProvName,
                                      0) == ERROR_SUCCESS &&
            NCryptOpenKey(KeyProvider,
                          &Key,
                          KeyProviderInfo->pwszContainerName,
                          0,
                          KeyProviderInfo->dwFlags & CRYPT_MACHINE_KEYSET ?
                              NCRYPT_MACHINE_KEY_FLAG :
                              0) == ERROR_SUCCESS)
        {
            NCryptDeleteKey(Key, NCRYPT_SILENT_FLAG);
        }
        if (KeyProvider != 0)
        {
            NCryptFreeObject(KeyProvider);
        }
    }
    else
    {
        CryptAcquireContextW(&Provider,
                             KeyProviderInfo->pwszContainerName,
                             KeyProviderInfo->pwszProvName,
                             KeyProviderInfo->dwProvType,
                             CRYPT_DELETEKEYSET | CRYPT_SILENT |
                                 (KeyProviderInfo->dwFlags & CRYPT_MACHINE_KEYSET));
    }
    HeapFree(GetProcessHeap(), 0, KeyProviderInfo);
}

static
USHORT
SDKIntegration_GetFreePort(VOID)
{
    WSADATA WinsockData;
    SOCKADDR_IN Address = { 0 };
    int AddressLength = sizeof(Address);
    SOCKET Socket = INVALID_SOCKET;
    USHORT Port = 0;

    if (WSAStartup(MAKEWORD(2, 2), &WinsockData) != 0)
    {
        return 0;
    }
    Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (Socket == INVALID_SOCKET)
    {
        goto Cleanup;
    }
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(Socket, (PSOCKADDR)&Address, sizeof(Address)) == SOCKET_ERROR ||
        getsockname(Socket, (PSOCKADDR)&Address, &AddressLength) == SOCKET_ERROR)
    {
        goto Cleanup;
    }
    Port = ntohs(Address.sin_port);

Cleanup:
    if (Socket != INVALID_SOCKET)
    {
        closesocket(Socket);
    }
    WSACleanup();
    return Port;
}

TEST_FUNC(SDKQuicIntegration)
{
    static const WCHAR ServerName[] = L"localhost";
    static const WCHAR MissingServiceName[] =
        L"KNSoft.ZPigeon.UnitTest.DoesNotExist";
    static const WCHAR TerminalCommandLine[] = L"cmd.exe /D /Q";
    static const WCHAR TerminalCancelCommandLine[] =
        L"powershell.exe -NoLogo -NoProfile -Command \"Start-Sleep -Seconds 30\"";
    static const BYTE TerminalInput[] =
        "(for /L %i in (1,1,2048) do @echo "
        "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX) "
        "& exit /b 7\r\n";
    SDK_INTEGRATION_CONTEXT TestContext = { 0 };
    ZP_MODULE_RECORD ClientModules[] = {
        { ZP_SYSTEM_MODULE_ID, ZP_SYSTEM_MODULE_VERSION },
        { ZP_PROCESS_MODULE_ID, ZP_PROCESS_MODULE_VERSION },
        { ZP_SERVICE_MODULE_ID, ZP_SERVICE_MODULE_VERSION },
        { ZP_FILE_MODULE_ID, ZP_FILE_MODULE_VERSION },
        { ZP_TERMINAL_MODULE_ID, ZP_TERMINAL_MODULE_VERSION },
        { ZP_EVENT_LOG_MODULE_ID, ZP_EVENT_LOG_MODULE_VERSION },
        { ZP_REGISTRY_MODULE_ID, ZP_REGISTRY_MODULE_VERSION }
    };
    ZP_MODULE_RECORD ServerModules[] = {
        { ZP_SYSTEM_MODULE_ID, ZP_SYSTEM_MODULE_VERSION },
        { ZP_PROCESS_MODULE_ID, ZP_PROCESS_MODULE_VERSION },
        { ZP_SERVICE_MODULE_ID, ZP_SERVICE_MODULE_VERSION },
        { ZP_FILE_MODULE_ID, ZP_FILE_MODULE_VERSION },
        { ZP_TERMINAL_MODULE_ID, ZP_TERMINAL_MODULE_VERSION },
        { ZP_EVENT_LOG_MODULE_ID, ZP_EVENT_LOG_MODULE_VERSION },
        { ZP_REGISTRY_MODULE_ID, ZP_REGISTRY_MODULE_VERSION }
    };
    ZP_ENDPOINT Endpoint = { ZpTransportQuic, L"127.0.0.1", 0, ServerName, NULL };
    ZP_LISTENER_ENDPOINT Listener = { ZpTransportQuic, L"127.0.0.1", 0, NULL };
    ZP_SERVER_DEPLOYMENT Deployment = { ServerName, NULL };
    ZP_CLIENT_CONFIG ClientConfig = { 0 };
    ZP_SERVER_CONFIG ServerConfig = { 0 };
    ZP_CLIENT_HANDLE Client = NULL;
    ZP_SERVER_HANDLE Server = NULL;
    ZP_REQUEST_HANDLE Request = NULL;
    ZP_CHANNEL_HANDLE ActiveTerminalChannel;
    NCRYPT_PROV_HANDLE IdentityProvider = 0;
    NCRYPT_KEY_HANDLE IdentityKey = 0;
    HCERTSTORE CertificateStore = NULL;
    PCCERT_CONTEXT Certificate = NULL;
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION TemporaryProcess = { 0 };
    FILETIME TemporaryCreateTime, TemporaryExitTime, TemporaryKernelTime, TemporaryUserTime;
    WCHAR TemporaryCommand[] = L"ping.exe -n 30 127.0.0.1";
    WCHAR ModulePath[MAX_PATH];
    WCHAR UploadPath[MAX_PATH] = { 0 };
    WCHAR UploadSearchPath[MAX_PATH];
    WCHAR RegistryPath[256] = { 0 };
    WCHAR RegistryChildPath[256];
    static const WCHAR RegistryValueName[] = L"Named";
    static const ULONG RegistryDefaultData = 17;
    static const ULONG RegistryNamedData = 42;
    BYTE RegistryOversizedData[8192] = { 0 };
    WCHAR FirstPageName[MAX_PATH];
    ULONG FirstPageNameLength;
    WCHAR FirstEventBookmark[4096];
    ULONG FirstEventBookmarkLength;
    BYTE FileWriteData[0x20000 + 17];
    NTSTATUS Status;
    ZP_STATUS StartStatus;
    DWORD WaitStatus;
    DWORD ServerStopWait = MAXDWORD, RetryWait = MAXDWORD, ProcessWait = MAXDWORD;
    ULONG Index;
    ULONGLONG ExpectedFileReadBytes, ExpectedFileReadHash;
    ULONGLONG UploadedBytes, UploadedHash, ExpectedUploadHash;
    WIN32_FIND_DATAW UploadFindData;
    HANDLE UploadFindHandle;
    BYTE ExpectedFileDigest[ZP_FILE_SHA256_SIZE];
    LOGICAL Result = FALSE;
    HANDLE Events[] = {
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL),
        CreateEventW(NULL, TRUE, FALSE, NULL)
    };

    for (Index = 0; Index < ARRAYSIZE(Events); Index++)
    {
        if (Events[Index] == NULL)
        {
            goto Cleanup;
        }
    }
    TestContext.ServerRunningEvent = Events[0];
    TestContext.ClientReadyEvent = Events[1];
    TestContext.ClientRetryWaitEvent = Events[2];
    TestContext.ClientStoppedEvent = Events[3];
    TestContext.ServerReadyEvent = Events[4];
    TestContext.ServerStoppedEvent = Events[5];
    TestContext.ClientPongEvent = Events[6];
    TestContext.SystemInfoEvent = Events[7];
    TestContext.ProcessListEvent = Events[8];
    TestContext.ProcessInfoEvent = Events[9];
    TestContext.ServiceListEvent = Events[10];
    TestContext.ServiceInfoEvent = Events[11];
    TestContext.ProcessTerminateEvent = Events[12];
    TestContext.ServiceControlEvent = Events[13];
    TestContext.FileInfoEvent = Events[14];
    TestContext.FileListEvent = Events[15];
    TestContext.FilePageEvent = Events[16];
    TestContext.EventLogPageEvent = Events[17];
    TestContext.FileHashEvent = Events[18];
    TestContext.FileReadEvent = Events[19];
    TestContext.FileWriteEvent = Events[20];
    TestContext.TerminalWritableEvent = Events[21];
    TestContext.TerminalResizeEvent = Events[22];
    TestContext.TerminalCloseEvent = Events[23];
    TestContext.RegistryPageEvent = Events[24];
    TestContext.RegistryValueEvent = Events[25];
    TestContext.RegistryStatusEvent = Events[26];
    if (NCryptOpenStorageProvider(&IdentityProvider,
                                  MS_KEY_STORAGE_PROVIDER,
                                  0) != ERROR_SUCCESS ||
        NCryptCreatePersistedKey(IdentityProvider,
                                 &IdentityKey,
                                 NCRYPT_ECDSA_P256_ALGORITHM,
                                 NULL,
                                 0,
                                 0) != ERROR_SUCCESS ||
        NCryptFinalizeKey(IdentityKey, NCRYPT_SILENT_FLAG) != ERROR_SUCCESS)
    {
            goto Cleanup;
    }

    Certificate = SDKIntegration_CreateCertificate(&CertificateStore);
    Endpoint.Port = Listener.Port = SDKIntegration_GetFreePort();
    if (Certificate == NULL || Endpoint.Port == 0)
    {
        goto Cleanup;
    }
    Deployment.Certificate = Certificate;

    ClientConfig.Size = sizeof(ClientConfig);
    ClientConfig.Endpoints = &Endpoint;
    ClientConfig.EndpointCount = 1;
    ClientConfig.DeploymentRootCertificate = Certificate->pbCertEncoded;
    ClientConfig.DeploymentRootCertificateLength = Certificate->cbCertEncoded;
    ClientConfig.ClientKeyName = NULL;
    ClientConfig.Modules = ClientModules;
    ClientConfig.ModuleCount = ARRAYSIZE(ClientModules);
    ClientConfig.ConnectTimeoutMilliseconds = SDK_INTEGRATION_TIMEOUT_MILLISECONDS;
    ClientConfig.StateCallback = SDKIntegration_ClientStateCallback;
    ClientConfig.PongCallback = SDKIntegration_ClientPongCallback;
    ClientConfig.CallbackContext = &TestContext;
    ClientConfig.MaxRequestPayloadBytesPerConnection = 4096;

    ServerConfig.Size = sizeof(ServerConfig);
    ServerConfig.Listeners = &Listener;
    ServerConfig.ListenerCount = 1;
    ServerConfig.Deployments = &Deployment;
    ServerConfig.DeploymentCount = 1;
    ServerConfig.Modules = ServerModules;
    ServerConfig.ModuleCount = ARRAYSIZE(ServerModules);
    ServerConfig.MaxRequestsPerConnection = 4;
    ServerConfig.MaxChannelsPerConnection = 1;
    ServerConfig.StateCallback = SDKIntegration_ServerStateCallback;
    ServerConfig.ConnectionCallback = SDKIntegration_ServerConnectionCallback;
    ServerConfig.CallbackContext = &TestContext;

    Status = ZpServer_Create(&ServerConfig, &Server);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    StartStatus = ZpServer_Start(Server);
    if (!ZpStatus_IsSuccess(StartStatus))
    {
        goto Cleanup;
    }
    if (
        WaitForSingleObject(TestContext.ServerRunningEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0)
    {
        goto Cleanup;
    }
    Status = ZpClient_Create(&ClientConfig, &Client);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    ((PZP_CLIENT_OBJECT)Client)->QuicTransport.ExternalKey = IdentityKey;
    Status = ZpClient_Start(Client);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }
    if (
        WaitForSingleObject(TestContext.ClientReadyEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        WaitForSingleObject(TestContext.ServerReadyEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.ClientReadyStatus) ||
        !ZpStatus_IsSuccess(TestContext.ServerReadyStatus))
    {
        goto Cleanup;
    }

    Status = ZpClient_Ping(Client, 0x0102030405060708);
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ClientPongEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        TestContext.ClientPongToken != 0x0102030405060708)
    {
        goto Cleanup;
    }

    Status = ZpServer_QueryEventLogPage(
        TestContext.Connection,
        ZpEventLogStartOldest,
        1,
        L"System",
        ARRAYSIZE(L"System") - 1,
        NULL,
        0,
        NULL,
        0,
        SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
        SDKIntegration_EventLogPageCallback,
        &TestContext,
        &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.EventLogPageEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.EventLogPageStatus) ||
        TestContext.EventLogPageCount != 1 ||
        TestContext.EventLogBookmarkLength == 0 ||
        TestContext.EventLogXmlLength == 0)
    {
        goto Cleanup;
    }
    FirstEventBookmarkLength = TestContext.EventLogBookmarkLength;
    RtlCopyMemory(FirstEventBookmark,
                  TestContext.EventLogBookmark,
                  (SIZE_T)FirstEventBookmarkLength * sizeof(WCHAR));
    ResetEvent(TestContext.EventLogPageEvent);
    TestContext.EventLogPageStatus = ZpStatus_FromNtStatus(STATUS_PENDING);
    TestContext.EventLogPageCount = 0;
    TestContext.EventLogBookmarkLength = 0;
    TestContext.EventLogXmlLength = 0;
    Status = ZpServer_QueryEventLogPage(
        TestContext.Connection,
        ZpEventLogStartAfterBookmark,
        1,
        L"System",
        ARRAYSIZE(L"System") - 1,
        NULL,
        0,
        FirstEventBookmark,
        FirstEventBookmarkLength,
        SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
        SDKIntegration_EventLogPageCallback,
        &TestContext,
        &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.EventLogPageEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.EventLogPageStatus) ||
        TestContext.EventLogPageCount > 1 ||
        (TestContext.EventLogPageCount != 0 &&
         TestContext.EventLogBookmarkLength == 0))
    {
        goto Cleanup;
    }

    Status = ZpServer_GetSystemInfo(TestContext.Connection,
                                    SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                    SDKIntegration_SystemInfoCallback,
                                    &TestContext,
                                    &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.SystemInfoEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.SystemInfoStatus) ||
        TestContext.SystemArchitecture < ZpSystemArchitectureX86 ||
        TestContext.SystemArchitecture > ZpSystemArchitectureArm64 ||
        TestContext.SystemProcessorCount == 0 ||
        TestContext.SystemPhysicalMemoryBytes == 0 ||
        TestContext.SystemComputerNameLength == 0)
    {
        goto Cleanup;
    }

    TestContext.ExpectedProcessCompletions = 1;
    TestContext.CollectProcessDetails = TRUE;
    Status = ZpServer_EnumerateProcesses(TestContext.Connection,
                                         SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                         SDKIntegration_ProcessListCallback,
                                         &TestContext,
                                         &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ProcessListEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.ProcessListStatus) ||
        TestContext.ProcessCount == 0 ||
        !TestContext.FoundCurrentProcess)
    {
        goto Cleanup;
    }

    Status = ZpServer_QueryProcess(TestContext.Connection,
                                   GetCurrentProcessId(),
                                   TestContext.ProcessInfoCreateTime,
                                   SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                   SDKIntegration_ProcessInfoCallback,
                                   &TestContext,
                                   &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ProcessInfoEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.ProcessInfoStatus) ||
        TestContext.ProcessInfoId != GetCurrentProcessId() ||
        TestContext.ProcessInfoThreadCount == 0 ||
        TestContext.ProcessInfoCreateTime == 0 ||
        TestContext.ProcessInfoImageNameLength == 0)
    {
        goto Cleanup;
    }

    Status = ZpServer_EnumerateServices(TestContext.Connection,
                                        SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                        SDKIntegration_ServiceListCallback,
                                        &TestContext,
                                        &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ServiceListEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.ServiceListStatus) ||
        TestContext.ServiceCount == 0 ||
        !TestContext.FoundNamedService)
    {
        goto Cleanup;
    }

    Status = ZpServer_QueryService(TestContext.Connection,
                                   TestContext.ServiceName,
                                   TestContext.ServiceNameLength,
                                   SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                   SDKIntegration_ServiceInfoCallback,
                                   &TestContext,
                                   &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ServiceInfoEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.ServiceInfoStatus) ||
        TestContext.ServiceInfoType == 0 ||
        TestContext.ServiceInfoNameLength != TestContext.ServiceNameLength ||
        TestContext.ServiceInfoDisplayNameLength == 0 ||
        TestContext.ServiceInfoBinaryPathLength == 0)
    {
        goto Cleanup;
    }

    if (!CreateProcessW(NULL,
                        TemporaryCommand,
                        NULL,
                        NULL,
                        FALSE,
                        CREATE_NO_WINDOW,
                        NULL,
                        NULL,
                        &StartupInfo,
                        &TemporaryProcess))
    {
        goto Cleanup;
    }
    if (!GetProcessTimes(TemporaryProcess.hProcess,
                         &TemporaryCreateTime,
                         &TemporaryExitTime,
                         &TemporaryKernelTime,
                         &TemporaryUserTime))
    {
        goto Cleanup;
    }
    Status = ZpServer_TerminateProcess(TestContext.Connection,
                                       TemporaryProcess.dwProcessId,
                                       ((ULONGLONG)TemporaryCreateTime.dwHighDateTime << 32) |
                                           TemporaryCreateTime.dwLowDateTime,
                                       0x10203040,
                                       SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                       SDKIntegration_ProcessTerminateCallback,
                                       &TestContext,
                                       &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ProcessTerminateEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.ProcessTerminateStatus) ||
        WaitForSingleObject(TemporaryProcess.hProcess,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0)
    {
        goto Cleanup;
    }

    Status = ZpServer_ControlService(TestContext.Connection,
                                     ZP_SERVICE_CONTROL_START,
                                     MissingServiceName,
                                     ARRAYSIZE(MissingServiceName) - 1,
                                     NULL,
                                     0,
                                     SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                     SDKIntegration_ServiceControlCallback,
                                     &TestContext,
                                     &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.ServiceControlEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        ZpStatus_IsSuccess(TestContext.ServiceControlStatus) ||
        (TestContext.ServiceControlStatus.Type == ZpStatusWin32 &&
         TestContext.ServiceControlStatus.Code == ERROR_ACCESS_DENIED))
    {
        goto Cleanup;
    }

    Index = GetModuleFileNameW(NULL, ModulePath, ARRAYSIZE(ModulePath));
    if (Index == 0 || Index == ARRAYSIZE(ModulePath))
    {
        goto Cleanup;
    }
    Status = ZpServer_QueryFile(TestContext.Connection,
                                ModulePath,
                                Index,
                                SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                SDKIntegration_FileInfoCallback,
                                &TestContext,
                                &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.FileInfoEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.FileInfoStatus) ||
        TestContext.FileAttributes == INVALID_FILE_ATTRIBUTES ||
        TestContext.FileSize == 0 ||
        TestContext.FileLastWriteTime == 0)
    {
        goto Cleanup;
    }
    if (!SDKIntegration_HashFileSha256(ModulePath, ExpectedFileDigest))
    {
        goto Cleanup;
    }
    Status = ZpServer_HashFile(TestContext.Connection,
                               ModulePath,
                               Index,
                               ZpFileHashSha256,
                               SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                               SDKIntegration_FileHashCallback,
                               &TestContext,
                               &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.FileHashEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.FileHashStatus) ||
        TestContext.FileHashAlgorithm != ZpFileHashSha256 ||
        TestContext.FileHashSize != TestContext.FileSize ||
        RtlCompareMemory(TestContext.FileDigest,
                         ExpectedFileDigest,
                         sizeof(ExpectedFileDigest)) != sizeof(ExpectedFileDigest))
    {
        goto Cleanup;
    }
    TestContext.FileReadHash = 1469598103934665603ULL;
    if (!SDKIntegration_HashFile(ModulePath,
                                 17,
                                 &ExpectedFileReadBytes,
                                 &ExpectedFileReadHash))
    {
        goto Cleanup;
    }
    Status = ZpServer_OpenFileRead(TestContext.Connection,
                                  ModulePath,
                                  Index,
                                  17,
                                  SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                  SDKIntegration_FileOpenReadCallback,
                                  SDKIntegration_ChannelDataCallback,
                                  SDKIntegration_ChannelCloseCallback,
                                  &TestContext,
                                  &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.FileReadEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.FileOpenReadStatus) ||
        !ZpStatus_IsSuccess(TestContext.FileReadCloseStatus) ||
        TestContext.FileReadChannel == NULL ||
        TestContext.FileReadSize != TestContext.FileSize ||
        TestContext.FileReadOffset != 17 ||
        TestContext.FileReadBytes != ExpectedFileReadBytes ||
        TestContext.FileReadHash != ExpectedFileReadHash)
    {
        goto Cleanup;
    }
    ZpChannel_Close(TestContext.FileReadChannel);
    TestContext.FileReadChannel = NULL;
    for (; Index != 0; Index--)
    {
        if (ModulePath[Index - 1] == L'\\' || ModulePath[Index - 1] == L'/')
        {
            break;
        }
    }
    if (Index <= 1 ||
        ARRAYSIZE(ModulePath) - Index >=
            ARRAYSIZE(TestContext.ExpectedFileName))
    {
        goto Cleanup;
    }
    TestContext.ExpectedFileNameLength =
        (ULONG)wcslen(&ModulePath[Index]);
    RtlCopyMemory(TestContext.ExpectedFileName,
                  &ModulePath[Index],
                  ((SIZE_T)TestContext.ExpectedFileNameLength + 1) *
                      sizeof(WCHAR));
    ModulePath[Index - 1] = UNICODE_NULL;
    Status = ZpServer_EnumerateFiles(TestContext.Connection,
                                     ModulePath,
                                     Index - 1,
                                     SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                     SDKIntegration_FileListCallback,
                                     &TestContext,
                                     &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.FileListEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.FileListStatus) ||
        TestContext.FileCount == 0 ||
        !TestContext.FoundExpectedFile)
    {
        goto Cleanup;
    }

    Status = ZpServer_EnumerateFilesPage(
        TestContext.Connection,
        ModulePath,
        (ULONG)wcslen(ModulePath),
        NULL,
        0,
        1,
        SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
        SDKIntegration_FilePageCallback,
        &TestContext,
        &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.FilePageEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.FilePageStatus) ||
        TestContext.FilePageCount != 1 ||
        TestContext.FilePageCursorLength == 0 ||
        TestContext.FilePageCursorLength != TestContext.FilePageNameLength)
    {
        goto Cleanup;
    }
    FirstPageNameLength = TestContext.FilePageNameLength;
    RtlCopyMemory(FirstPageName,
                  TestContext.FilePageName,
                  (SIZE_T)FirstPageNameLength * sizeof(WCHAR));
    ResetEvent(TestContext.FilePageEvent);
    TestContext.FilePageStatus = ZpStatus_FromNtStatus(STATUS_PENDING);
    TestContext.FilePageCount = 0;
    TestContext.FilePageNameLength = 0;
    Status = ZpServer_EnumerateFilesPage(
        TestContext.Connection,
        ModulePath,
        (ULONG)wcslen(ModulePath),
        TestContext.FilePageCursor,
        TestContext.FilePageCursorLength,
        1,
        SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
        SDKIntegration_FilePageCallback,
        &TestContext,
        &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.FilePageEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.FilePageStatus) ||
        TestContext.FilePageCount != 1 ||
        CompareStringOrdinal(FirstPageName,
                             FirstPageNameLength,
                             TestContext.FilePageName,
                             TestContext.FilePageNameLength,
                             TRUE) != CSTR_LESS_THAN)
    {
        goto Cleanup;
    }

    if (swprintf_s(UploadPath,
                   ARRAYSIZE(UploadPath),
                   L"%s\\ZPigeon-Upload-%lu.tmp",
                   ModulePath,
                   GetCurrentProcessId()) < 0)
    {
        goto Cleanup;
    }
    DeleteFileW(UploadPath);
    ExpectedUploadHash = 1469598103934665603ULL;
    for (Index = 0; Index < ARRAYSIZE(FileWriteData); Index++)
    {
        FileWriteData[Index] = (BYTE)(Index * 31 + 7);
        ExpectedUploadHash ^= FileWriteData[Index];
        ExpectedUploadHash *= 1099511628211ULL;
    }
    TestContext.FileWriteData = FileWriteData;
    TestContext.FileWriteLength = sizeof(FileWriteData);
    Status = ZpServer_OpenFileWrite(TestContext.Connection,
                                    UploadPath,
                                    (ULONG)wcslen(UploadPath),
                                    sizeof(FileWriteData),
                                    ZpFileCreateNew,
                                    SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                    SDKIntegration_FileOpenWriteCallback,
                                    SDKIntegration_FileWriteWritableCallback,
                                    SDKIntegration_FileWriteCloseCallback,
                                    &TestContext,
                                    &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.FileWriteEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.FileOpenWriteStatus) ||
        !ZpStatus_IsSuccess(TestContext.FileWriteStatus) ||
        TestContext.FileWriteChannel == NULL ||
        TestContext.FileWriteOffset != sizeof(FileWriteData) ||
        !SDKIntegration_HashFile(UploadPath,
                                 0,
                                 &UploadedBytes,
                                 &UploadedHash) ||
        UploadedBytes != sizeof(FileWriteData) ||
        UploadedHash != ExpectedUploadHash)
    {
        goto Cleanup;
    }
    ZpChannel_Close(TestContext.FileWriteChannel);
    TestContext.FileWriteChannel = NULL;
    ResetEvent(TestContext.FileWriteEvent);
    TestContext.FileWriteLength = 0;
    TestContext.FileWriteOffset = 0;
    TestContext.FileOpenWriteStatus = ZpStatus_FromNtStatus(STATUS_PENDING);
    TestContext.FileWriteStatus = ZpStatus_FromNtStatus(STATUS_PENDING);
    Status = ZpServer_OpenFileWrite(TestContext.Connection,
                                    UploadPath,
                                    (ULONG)wcslen(UploadPath),
                                    0,
                                    ZpFileCreateAlways,
                                    SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                    SDKIntegration_FileOpenWriteCallback,
                                    SDKIntegration_FileWriteWritableCallback,
                                    SDKIntegration_FileWriteCloseCallback,
                                    &TestContext,
                                    &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.FileWriteEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.FileOpenWriteStatus) ||
        !ZpStatus_IsSuccess(TestContext.FileWriteStatus) ||
        TestContext.FileWriteChannel == NULL ||
        !SDKIntegration_HashFile(UploadPath,
                                 0,
                                 &UploadedBytes,
                                 &UploadedHash) ||
        UploadedBytes != 0)
    {
        goto Cleanup;
    }
    ZpChannel_Close(TestContext.FileWriteChannel);
    TestContext.FileWriteChannel = NULL;
    if (!DeleteFileW(UploadPath))
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.FileWriteEvent);
    TestContext.FileWriteLength = sizeof(FileWriteData);
    TestContext.FileWriteOffset = 0;
    TestContext.CancelFileWrite = TRUE;
    TestContext.FileOpenWriteStatus = ZpStatus_FromNtStatus(STATUS_PENDING);
    TestContext.FileWriteStatus = ZpStatus_FromNtStatus(STATUS_PENDING);
    Status = ZpServer_OpenFileWrite(TestContext.Connection,
                                    UploadPath,
                                    (ULONG)wcslen(UploadPath),
                                    sizeof(FileWriteData),
                                    ZpFileCreateNew,
                                    SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                    SDKIntegration_FileOpenWriteCallback,
                                    SDKIntegration_FileWriteWritableCallback,
                                    SDKIntegration_FileWriteCloseCallback,
                                    &TestContext,
                                    &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.FileWriteEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.FileOpenWriteStatus) ||
        TestContext.FileWriteStatus.Type != ZpStatusNtStatus ||
        TestContext.FileWriteStatus.Code != (ULONG)STATUS_CANCELLED ||
        TestContext.FileWriteChannel == NULL)
    {
        goto Cleanup;
    }
    ZpChannel_Close(TestContext.FileWriteChannel);
    TestContext.FileWriteChannel = NULL;
    if (swprintf_s(UploadSearchPath,
                   ARRAYSIZE(UploadSearchPath),
                   L"%s.*.zpigeon.tmp",
                   UploadPath) < 0)
    {
        goto Cleanup;
    }
    for (Index = 0; Index < 100; Index++)
    {
        UploadFindHandle = FindFirstFileW(UploadSearchPath, &UploadFindData);
        if (UploadFindHandle == INVALID_HANDLE_VALUE)
        {
            break;
        }
        FindClose(UploadFindHandle);
        Sleep(10);
    }
    UploadFindHandle = FindFirstFileW(UploadSearchPath, &UploadFindData);
    if (GetFileAttributesW(UploadPath) != INVALID_FILE_ATTRIBUTES ||
        UploadFindHandle != INVALID_HANDLE_VALUE)
    {
        if (UploadFindHandle != INVALID_HANDLE_VALUE)
        {
            FindClose(UploadFindHandle);
        }
        goto Cleanup;
    }
    UploadPath[0] = UNICODE_NULL;

    if (swprintf_s(RegistryPath,
                   ARRAYSIZE(RegistryPath),
                   L"Software\\KNSoft\\KNSoft.ZPigeon.UnitTest.%lu.%llu",
                   GetCurrentProcessId(),
                   GetTickCount64()) < 0 ||
        swprintf_s(RegistryChildPath,
                   ARRAYSIZE(RegistryChildPath),
                   L"%s\\Child",
                   RegistryPath) < 0)
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.RegistryStatusEvent);
    Status = ZpServer_CreateRegistryKey(
                 TestContext.Connection,
                 ZpRegistryCurrentUser,
                 RegistryPath,
                 (ULONG)wcslen(RegistryPath),
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryStatusCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryStatusEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.RegistryStatus))
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.RegistryStatusEvent);
    Status = ZpServer_SetRegistryValue(
                 TestContext.Connection,
                 ZpRegistryCurrentUser,
                 RegistryPath,
                 (ULONG)wcslen(RegistryPath),
                 L"Oversized",
                 ARRAYSIZE(L"Oversized") - 1,
                 REG_BINARY,
                 RegistryOversizedData,
                 sizeof(RegistryOversizedData),
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryStatusCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryStatusEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        TestContext.RegistryStatus.Type != ZpStatusNtStatus ||
        TestContext.RegistryStatus.Code != (ULONG)STATUS_QUOTA_EXCEEDED)
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.RegistryStatusEvent);
    Status = ZpServer_CreateRegistryKey(
                 TestContext.Connection,
                 ZpRegistryCurrentUser,
                 RegistryChildPath,
                 (ULONG)wcslen(RegistryChildPath),
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryStatusCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryStatusEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.RegistryStatus))
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.RegistryStatusEvent);
    Status = ZpServer_SetRegistryValue(
                 TestContext.Connection,
                 ZpRegistryCurrentUser,
                 RegistryPath,
                 (ULONG)wcslen(RegistryPath),
                 NULL,
                 0,
                 REG_DWORD,
                 &RegistryDefaultData,
                 sizeof(RegistryDefaultData),
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryStatusCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryStatusEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.RegistryStatus))
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.RegistryStatusEvent);
    Status = ZpServer_SetRegistryValue(
                 TestContext.Connection,
                 ZpRegistryCurrentUser,
                 RegistryPath,
                 (ULONG)wcslen(RegistryPath),
                 RegistryValueName,
                 ARRAYSIZE(RegistryValueName) - 1,
                 REG_DWORD,
                 &RegistryNamedData,
                 sizeof(RegistryNamedData),
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryStatusCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryStatusEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.RegistryStatus))
    {
        goto Cleanup;
    }

    ResetEvent(TestContext.RegistryPageEvent);
    TestContext.RegistryPageValues = FALSE;
    Status = ZpServer_EnumerateRegistryKeysPage(
                 TestContext.Connection,
                 ZpRegistryCurrentUser,
                 RegistryPath,
                 (ULONG)wcslen(RegistryPath),
                 NULL,
                 0,
                 8,
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryPageCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryPageEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.RegistryPageStatus) ||
        TestContext.RegistryRecordCount != 1)
    {
        goto Cleanup;
    }

    ResetEvent(TestContext.RegistryPageEvent);
    Status = ZpServer_EnumerateRegistryKeysPage(
                 TestContext.Connection,
                 ZpRegistryLocalMachine,
                 NULL,
                 0,
                 NULL,
                 0,
                 ZP_REGISTRY_PAGE_MAX_COUNT,
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryPageCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryPageEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.RegistryPageStatus) ||
        TestContext.RegistryRecordCount == 0)
    {
        goto Cleanup;
    }

    ResetEvent(TestContext.RegistryPageEvent);
    TestContext.RegistryPageValues = TRUE;
    Status = ZpServer_EnumerateRegistryValuesPage(
                 TestContext.Connection,
                 ZpRegistryCurrentUser,
                 RegistryPath,
                 (ULONG)wcslen(RegistryPath),
                 NULL,
                 0,
                 1,
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryPageCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryPageEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.RegistryPageStatus) ||
        TestContext.RegistryRecordCount != 1 ||
        !TestContext.RegistryHasMore ||
        TestContext.RegistryRecordNameLength != 0 ||
        TestContext.RegistryCursorLength != 0)
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.RegistryPageEvent);
    Status = ZpServer_EnumerateRegistryValuesPage(
                 TestContext.Connection,
                 ZpRegistryCurrentUser,
                 RegistryPath,
                 (ULONG)wcslen(RegistryPath),
                 L"",
                 0,
                 1,
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryPageCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryPageEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.RegistryPageStatus) ||
        TestContext.RegistryRecordCount != 1 ||
        TestContext.RegistryHasMore ||
        TestContext.RegistryRecordNameLength !=
            ARRAYSIZE(RegistryValueName) - 1 ||
        RtlCompareMemory(TestContext.RegistryRecordName,
                         RegistryValueName,
                         sizeof(RegistryValueName) - sizeof(WCHAR)) !=
            sizeof(RegistryValueName) - sizeof(WCHAR))
    {
        goto Cleanup;
    }

    ResetEvent(TestContext.RegistryValueEvent);
    Status = ZpServer_QueryRegistryValue(
                 TestContext.Connection,
                 ZpRegistryCurrentUser,
                 RegistryPath,
                 (ULONG)wcslen(RegistryPath),
                 RegistryValueName,
                 ARRAYSIZE(RegistryValueName) - 1,
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryValueCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryValueEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.RegistryValueStatus) ||
        TestContext.RegistryValueType != REG_DWORD ||
        TestContext.RegistryValueDataLength != sizeof(RegistryNamedData) ||
        RtlCompareMemory(TestContext.RegistryValueData,
                         &RegistryNamedData,
                         sizeof(RegistryNamedData)) !=
            sizeof(RegistryNamedData))
    {
        goto Cleanup;
    }

    ResetEvent(TestContext.RegistryStatusEvent);
    Status = ZpServer_DeleteRegistryValue(
                 TestContext.Connection,
                 ZpRegistryCurrentUser,
                 RegistryPath,
                 (ULONG)wcslen(RegistryPath),
                 RegistryValueName,
                 ARRAYSIZE(RegistryValueName) - 1,
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryStatusCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryStatusEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.RegistryStatus))
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.RegistryStatusEvent);
    Status = ZpServer_DeleteRegistryValue(
                 TestContext.Connection,
                 ZpRegistryCurrentUser,
                 RegistryPath,
                 (ULONG)wcslen(RegistryPath),
                 NULL,
                 0,
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryStatusCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryStatusEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.RegistryStatus))
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.RegistryStatusEvent);
    Status = ZpServer_DeleteRegistryKey(
                 TestContext.Connection,
                 ZpRegistryCurrentUser,
                 RegistryChildPath,
                 (ULONG)wcslen(RegistryChildPath),
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryStatusCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryStatusEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.RegistryStatus))
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.RegistryStatusEvent);
    Status = ZpServer_DeleteRegistryKey(
                 TestContext.Connection,
                 ZpRegistryCurrentUser,
                 RegistryPath,
                 (ULONG)wcslen(RegistryPath),
                 SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                 SDKIntegration_RegistryStatusCallback,
                 &TestContext,
                 &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.RegistryStatusEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) !=
            WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.RegistryStatus))
    {
        goto Cleanup;
    }
    RegistryPath[0] = UNICODE_NULL;

    Status = ZpServer_CreateTerminal(TestContext.Connection,
                                      80,
                                      25,
                                      TerminalCommandLine,
                                      ARRAYSIZE(TerminalCommandLine) - 1,
                                      NULL,
                                      0,
                                      SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                      SDKIntegration_TerminalCreateCallback,
                                      SDKIntegration_TerminalDataCallback,
                                      SDKIntegration_TerminalWritableCallback,
                                      SDKIntegration_TerminalCloseCallback,
                                      &TestContext,
                                      &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.TerminalWritableEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.TerminalCreateStatus) ||
        TestContext.TerminalChannel == NULL ||
        TestContext.TerminalProcessId == 0 ||
        TestContext.TerminalWritableCredit < sizeof(TerminalInput) - 1)
    {
        goto Cleanup;
    }
    Status = ZpServer_ResizeTerminal(TestContext.Connection,
                                     TestContext.TerminalChannel,
                                     100,
                                     30,
                                     SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                     SDKIntegration_TerminalResizeCallback,
                                     &TestContext,
                                     &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.TerminalResizeEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.TerminalResizeStatus))
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.TerminalWritableEvent);
    Status = ZpChannel_Send(TestContext.TerminalChannel,
                            TerminalInput,
                            sizeof(TerminalInput) - 1);
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.TerminalWritableEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0)
    {
        goto Cleanup;
    }
    if (
        WaitForSingleObject(TestContext.TerminalCloseEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        TestContext.TerminalCloseStatus.Type != ZpStatusProcessExit ||
        TestContext.TerminalCloseStatus.Code != 7 ||
        TestContext.TerminalDataBytes < 100000)
    {
        goto Cleanup;
    }
    ZpChannel_Close(TestContext.TerminalChannel);
    TestContext.TerminalChannel = NULL;

    ResetEvent(TestContext.TerminalWritableEvent);
    ResetEvent(TestContext.TerminalCloseEvent);
    TestContext.TerminalWritableCredit = 0;
    TestContext.TerminalDataBytes = 0;
    TestContext.TerminalProcessId = 0;
    TestContext.TerminalCreateStatus = ZpStatus_FromNtStatus(STATUS_PENDING);
    TestContext.TerminalCloseStatus = ZpStatus_FromNtStatus(STATUS_PENDING);
    Status = ZpServer_CreateTerminal(
        TestContext.Connection,
        80,
        25,
        TerminalCancelCommandLine,
        ARRAYSIZE(TerminalCancelCommandLine) - 1,
        NULL,
        0,
        SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
        SDKIntegration_TerminalCreateCallback,
        SDKIntegration_TerminalDataCallback,
        SDKIntegration_TerminalWritableCallback,
        SDKIntegration_TerminalCloseCallback,
        &TestContext,
        &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (!NT_SUCCESS(Status) ||
        WaitForSingleObject(TestContext.TerminalWritableEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.TerminalCreateStatus) ||
        TestContext.TerminalChannel == NULL ||
        TestContext.TerminalProcessId == 0)
    {
        goto Cleanup;
    }
    ActiveTerminalChannel = TestContext.TerminalChannel;
    TestContext.TerminalChannel = NULL;
    ResetEvent(TestContext.TerminalCloseEvent);
    TestContext.TerminalCreateStatus = ZpStatus_FromNtStatus(STATUS_PENDING);
    Status = ZpServer_CreateTerminal(
        TestContext.Connection,
        80,
        25,
        TerminalCancelCommandLine,
        ARRAYSIZE(TerminalCancelCommandLine) - 1,
        NULL,
        0,
        SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
        SDKIntegration_TerminalCreateCallback,
        SDKIntegration_TerminalDataCallback,
        SDKIntegration_TerminalWritableCallback,
        SDKIntegration_TerminalCloseCallback,
        &TestContext,
        &Request);
    if (NT_SUCCESS(Status))
    {
        ZpRequest_Close(Request);
        Request = NULL;
    }
    if (Status != STATUS_QUOTA_EXCEEDED ||
        WaitForSingleObject(TestContext.TerminalCloseEvent, 0) != WAIT_TIMEOUT ||
        TestContext.TerminalCreateStatus.Type != ZpStatusNtStatus ||
        TestContext.TerminalCreateStatus.Code != (ULONG)STATUS_PENDING ||
        TestContext.TerminalChannel != NULL)
    {
        ZpChannel_Cancel(ActiveTerminalChannel);
        ZpChannel_Close(ActiveTerminalChannel);
        goto Cleanup;
    }
    TestContext.TerminalChannel = ActiveTerminalChannel;
    ResetEvent(TestContext.TerminalCloseEvent);
    if (!NT_SUCCESS(ZpChannel_Cancel(TestContext.TerminalChannel)) ||
        WaitForSingleObject(TestContext.TerminalCloseEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        TestContext.TerminalCloseStatus.Type != ZpStatusNtStatus ||
        TestContext.TerminalCloseStatus.Code != (ULONG)STATUS_CANCELLED)
    {
        goto Cleanup;
    }
    ZpChannel_Close(TestContext.TerminalChannel);
    TestContext.TerminalChannel = NULL;

    ResetEvent(TestContext.ServerRunningEvent);
    ResetEvent(TestContext.ClientReadyEvent);
    ResetEvent(TestContext.ServerReadyEvent);
    ResetEvent(TestContext.ProcessListEvent);
    InterlockedExchange(&TestContext.ProcessCompletionCount, 0);
    TestContext.ExpectedProcessCompletions = 4;
    TestContext.CollectProcessDetails = FALSE;
    for (Index = 0; Index < 4; Index++)
    {
        Status = ZpServer_EnumerateProcesses(TestContext.Connection,
                                             SDK_INTEGRATION_TIMEOUT_MILLISECONDS,
                                             SDKIntegration_ProcessListCallback,
                                             &TestContext,
                                             &Request);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
        ZpRequest_Close(Request);
        Request = NULL;
    }
    Status = ZpServer_Stop(Server);
    ServerStopWait = WaitForSingleObject(TestContext.ServerStoppedEvent,
                                         SDK_INTEGRATION_TIMEOUT_MILLISECONDS);
    RetryWait = WaitForSingleObject(TestContext.ClientRetryWaitEvent,
                                    SDK_INTEGRATION_TIMEOUT_MILLISECONDS);
    ProcessWait = WaitForSingleObject(TestContext.ProcessListEvent,
                                      SDK_INTEGRATION_TIMEOUT_MILLISECONDS);
    if (!NT_SUCCESS(Status) ||
        ServerStopWait != WAIT_OBJECT_0 ||
        RetryWait != WAIT_OBJECT_0 ||
        ProcessWait != WAIT_OBJECT_0 ||
        TestContext.ProcessCompletionCount !=
            TestContext.ExpectedProcessCompletions)
    {
        goto Cleanup;
    }
    ResetEvent(TestContext.ServerStoppedEvent);
    StartStatus = ZpServer_Start(Server);
    if (!ZpStatus_IsSuccess(StartStatus) ||
        WaitForSingleObject(TestContext.ServerRunningEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        WaitForSingleObject(TestContext.ClientReadyEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        WaitForSingleObject(TestContext.ServerReadyEvent,
                            SDK_INTEGRATION_TIMEOUT_MILLISECONDS) != WAIT_OBJECT_0 ||
        !ZpStatus_IsSuccess(TestContext.ClientReadyStatus) ||
        !ZpStatus_IsSuccess(TestContext.ServerReadyStatus))
    {
        goto Cleanup;
    }
    Result = TRUE;

Cleanup:
    if (Request != NULL)
    {
        ZpRequest_Close(Request);
    }
    if (TestContext.FileReadChannel != NULL)
    {
        ZpChannel_Cancel(TestContext.FileReadChannel);
        ZpChannel_Close(TestContext.FileReadChannel);
        TestContext.FileReadChannel = NULL;
    }
    if (TestContext.FileWriteChannel != NULL)
    {
        ZpChannel_Cancel(TestContext.FileWriteChannel);
        ZpChannel_Close(TestContext.FileWriteChannel);
        TestContext.FileWriteChannel = NULL;
    }
    if (TestContext.TerminalChannel != NULL)
    {
        ZpChannel_Cancel(TestContext.TerminalChannel);
        ZpChannel_Close(TestContext.TerminalChannel);
        TestContext.TerminalChannel = NULL;
    }
    if (Client != NULL)
    {
        ZpClient_Stop(Client);
        WaitStatus = WaitForSingleObject(TestContext.ClientStoppedEvent,
                                         SDK_INTEGRATION_TIMEOUT_MILLISECONDS);
        Status = ZpClient_Close(Client);
        Result = Result && WaitStatus == WAIT_OBJECT_0 &&
                 ZpStatus_IsSuccess(TestContext.ClientStoppedStatus) && NT_SUCCESS(Status);
    }
    if (Server != NULL)
    {
        ZpServer_Stop(Server);
        WaitStatus = WaitForSingleObject(TestContext.ServerStoppedEvent,
                                         SDK_INTEGRATION_TIMEOUT_MILLISECONDS);
        Status = ZpServer_Close(Server);
        Result = Result && WaitStatus == WAIT_OBJECT_0 &&
                 ZpStatus_IsSuccess(TestContext.ServerStoppedStatus) && NT_SUCCESS(Status);
    }
    if (UploadPath[0] != UNICODE_NULL)
    {
        DeleteFileW(UploadPath);
    }
    if (RegistryPath[0] != UNICODE_NULL)
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, RegistryPath);
    }
    if (IdentityKey != 0)
    {
        NCryptFreeObject(IdentityKey);
    }
    if (IdentityProvider != 0)
    {
        NCryptFreeObject(IdentityProvider);
    }
    if (TemporaryProcess.hProcess != NULL)
    {
        if (WaitForSingleObject(TemporaryProcess.hProcess, 0) == WAIT_TIMEOUT)
        {
            TerminateProcess(TemporaryProcess.hProcess, STATUS_CANCELLED);
            WaitForSingleObject(TemporaryProcess.hProcess,
                                SDK_INTEGRATION_TIMEOUT_MILLISECONDS);
        }
        CloseHandle(TemporaryProcess.hProcess);
    }
    if (TemporaryProcess.hThread != NULL)
    {
        CloseHandle(TemporaryProcess.hThread);
    }
    if (Certificate != NULL)
    {
        SDKIntegration_DeleteCertificateKey(Certificate);
        CertFreeCertificateContext(Certificate);
    }
    if (CertificateStore != NULL)
    {
        CertCloseStore(CertificateStore, 0);
    }
    for (Index = 0; Index < ARRAYSIZE(Events); Index++)
    {
        if (Events[Index] != NULL)
        {
            CloseHandle(Events[Index]);
        }
    }
    TEST_OK(Result);
}
