#include "UnitTest.h"

#include <KNSoft/ZPigeon/EventLog.h>
#include <KNSoft/ZPigeon/File.h>
#include <KNSoft/ZPigeon/Protocol.h>
#include <KNSoft/ZPigeon/Process.h>
#include <KNSoft/ZPigeon/Registry.h>
#include <KNSoft/ZPigeon/Service.h>
#include <KNSoft/ZPigeon/System.h>
#include <KNSoft/ZPigeon/Terminal.h>

TEST_FUNC(ProtocolMessage)
{
    static const WCHAR EventChannel[] = L"System";
    static const WCHAR EventQuery[] = L"*[System/Level<=3]";
    static const WCHAR EventBookmark[] = L"<Bookmark>1</Bookmark>";
    static const WCHAR EventXml[] = L"<Event/>";
    static const WCHAR RegistryPath[] = L"Software\\KNSoft";
    static const WCHAR RegistryValueName[] = L"Enabled";
    static const WCHAR RegistryNewName[] = L"Active";
    static const BYTE RegistryData[] = { 1, 2, 3, 4 };
    ZP_MODULE_RECORD Modules[] = {
        { 1, 1 },
        { 3, 2 }
    };
    BYTE PublicKey[ZP_CLIENT_PUBLIC_KEY_SIZE] = { 0x04 };
    BYTE Challenge[ZP_SERVER_CHALLENGE_SIZE], Signature[ZP_CLIENT_SIGNATURE_SIZE], Buffer[256];
    ZP_CLIENT_HELLO ClientHello = { ZP_CORE_VERSION, Modules, ARRAYSIZE(Modules), PublicKey };
    ZP_CLIENT_HELLO_VIEW ClientHelloView;
    ZP_READY Ready = { Modules, ARRAYSIZE(Modules) };
    ZP_READY_VIEW ReadyView;
    const BYTE RequestPayload[] = { 1, 2, 3 };
    const BYTE ResponsePayload[] = { 4, 5 };
    ZP_REQUEST Request = { 7, 1, 2, 5000, RequestPayload, sizeof(RequestPayload) };
    ZP_REQUEST_VIEW RequestView;
    ZP_RESPONSE Response = {
        7,
        { ZpStatusQuic, 0x80410006UL },
        ResponsePayload,
        sizeof(ResponsePayload)
    };
    ZP_RESPONSE_VIEW ResponseView;
    ZP_CHANNEL_DATA_VIEW ChannelDataView;
    ZP_CHANNEL_CLOSE ChannelClose;
    ZP_SYSTEM_INFO SystemInfo = {
        ZpSystemArchitectureX64,
        10,
        0,
        26100,
        16,
        32ULL * 1024 * 1024 * 1024,
        L"TEST-HOST",
        9
    };
    ZP_SYSTEM_INFO_VIEW SystemInfoView;
    ZP_PROCESS_RECORD Processes[] = {
        { 0, 0, 0, 4, 0, 10, 20, 30, 40, 50, L"", 0 },
        { 1234, 1000, 1, 8, 64, 100, 200, 300, 400, 500, L"example.exe", 11 }
    };
    ZP_PROCESS_LIST_VIEW ProcessList;
    ZP_PROCESS_RECORD_VIEW ProcessRecord;
    ZP_PROCESS_INFO ProcessInfo = {
        1234,
        1000,
        1,
        8,
        64,
        100,
        200,
        300,
        400,
        500,
        L"example.exe",
        11,
        STATUS_SUCCESS,
        L"C:\\example.exe",
        14,
        STATUS_SUCCESS,
        L"example.exe -test",
        17
    };
    ZP_PROCESS_INFO_VIEW ProcessInfoView;
    ZP_SERVICE_RECORD Services[] = {
        { 0x10, 4, 4321, L"SvcA", 4, L"Service A", 9 }
    };
    ZP_SERVICE_LIST_VIEW ServiceList;
    ZP_SERVICE_RECORD_VIEW ServiceRecord;
    ZP_SERVICE_INFO ServiceInfo = {
        0x10,
        4,
        4321,
        2,
        1,
        L"SvcA",
        4,
        L"Service A",
        9,
        L"C:\\SvcA.exe",
        11,
        L"LocalSystem",
        11
    };
    ZP_SERVICE_INFO_VIEW ServiceInfoView;
    ZP_FILE_INFO FileInfo = {
        FILE_ATTRIBUTE_ARCHIVE,
        123456789,
        100,
        200,
        300
    };
    ZP_FILE_INFO FileInfoView;
    ZP_STRING_VIEW FilePathView;
    ZP_FILE_HASH_ALGORITHM FileHashAlgorithm;
    ZP_FILE_CREATE_DISPOSITION FileDisposition;
    ZP_FILE_HASH_VIEW FileHashView;
    BYTE FileDigest[ZP_FILE_SHA256_SIZE];
    ZP_FILE_RECORD FileRecords[] = {
        {
            { FILE_ATTRIBUTE_ARCHIVE, 123456789, 100, 200, 300 },
            L"Test.bin",
            8
        }
    };
    ZP_FILE_LIST_VIEW FileList;
    ZP_FILE_PAGE_VIEW FilePage;
    ZP_FILE_RECORD_VIEW FileRecord;
    ZP_EVENT_LOG_RECORD EventRecords[] = {
        {
            EventBookmark,
            ARRAYSIZE(EventBookmark) - 1,
            EventXml,
            ARRAYSIZE(EventXml) - 1
        }
    };
    ZP_EVENT_LOG_QUERY_VIEW EventLogQuery;
    ZP_EVENT_LOG_PAGE_VIEW EventLogPage;
    ZP_EVENT_LOG_RECORD_VIEW EventLogRecord;
    ZP_STRING_VIEW EventLogChannel;
    BOOLEAN EventLogEnabled;
    ZP_REGISTRY_ENUMERATE_VIEW RegistryEnumerate;
    ZP_REGISTRY_VALUE_REQUEST_VIEW RegistryValueRequest;
    ZP_REGISTRY_SET_VALUE_VIEW RegistrySetValue;
    ZP_REGISTRY_KEY_REQUEST_VIEW RegistryKeyRequest;
    ZP_REGISTRY_RENAME_REQUEST_VIEW RegistryRenameRequest;
    ZP_REGISTRY_KEY_RECORD RegistryKeys[] = {
        { L"Alpha", 5, 100, FALSE },
        { L"Beta", 4, 200, TRUE }
    };
    ZP_REGISTRY_VALUE_RECORD RegistryValues[] = {
        { L"", 0, 1, 8, RegistryData, sizeof(RegistryData) }
    };
    ZP_REGISTRY_PAGE_VIEW RegistryPage;
    ZP_REGISTRY_KEY_RECORD_VIEW RegistryKey;
    ZP_REGISTRY_VALUE_RECORD_VIEW RegistryValueRecord;
    ZP_REGISTRY_VALUE_VIEW RegistryValue;
    ZP_TERMINAL_CREATE_VIEW TerminalCreate;
    ZP_MODULE_RECORD Module;
    ZP_BUFFER_VIEW BufferView;
    ULONGLONG Value, FileSize, FileOffset;
    ULONG Length, Index, ExitCode, CreditBytes, ProcessId, MaxEntries, Shells;
    USHORT Columns, Rows;

    for (Index = 0; Index < ARRAYSIZE(Challenge); Index++)
    {
        Challenge[Index] = (BYTE)Index;
    }
    for (Index = 0; Index < ARRAYSIZE(Signature); Index++)
    {
        Signature[Index] = (BYTE)(ARRAYSIZE(Signature) - Index);
    }
    for (Index = 0; Index < ARRAYSIZE(FileDigest); Index++)
    {
        FileDigest[Index] = (BYTE)(Index + 1);
    }

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeClientHello(&ClientHello, NULL, 0, &Length)) && Length == 77);
    TEST_OK(ZpMessage_EncodeClientHello(&ClientHello, Buffer, Length - 1, &Index) == STATUS_BUFFER_TOO_SMALL &&
            Index == Length);
    TEST_OK(NT_SUCCESS(ZpMessage_EncodeClientHello(&ClientHello, Buffer, sizeof(Buffer), &Length)) && Length == 77);
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeClientHello(Buffer, Length, &ClientHelloView)) &&
            ClientHelloView.CoreVersion == ZP_CORE_VERSION &&
            ClientHelloView.Modules.Count == ARRAYSIZE(Modules) &&
            RtlCompareMemory(ClientHelloView.ClientPublicKey,
                             PublicKey,
                             sizeof(PublicKey)) == sizeof(PublicKey));
    TEST_OK(NT_SUCCESS(ZpMessage_GetModuleRecord(&ClientHelloView.Modules, 0, &Module)) &&
            Module.ModuleId == Modules[0].ModuleId &&
            Module.ModuleVersion == Modules[0].ModuleVersion);
    TEST_OK(NT_SUCCESS(ZpMessage_GetModuleRecord(&ClientHelloView.Modules, 1, &Module)) &&
            Module.ModuleId == Modules[1].ModuleId &&
            Module.ModuleVersion == Modules[1].ModuleVersion);
    TEST_OK(ZpMessage_GetModuleRecord(&ClientHelloView.Modules,
                                      ClientHelloView.Modules.Count,
                                      &Module) == STATUS_INVALID_PARAMETER);

    Modules[1].ModuleId = Modules[0].ModuleId;
    TEST_OK(ZpMessage_EncodeClientHello(&ClientHello, NULL, 0, &Length) == STATUS_INVALID_PARAMETER);
    Modules[1].ModuleId = 3;
    PublicKey[0] = 0x03;
    TEST_OK(ZpMessage_EncodeClientHello(&ClientHello, NULL, 0, &Length) == STATUS_INVALID_PARAMETER);
    PublicKey[0] = 0x04;
    ClientHello.CoreVersion++;
    TEST_OK(ZpMessage_EncodeClientHello(&ClientHello, NULL, 0, &Length) == STATUS_REVISION_MISMATCH);
    ClientHello.CoreVersion = ZP_CORE_VERSION;

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeServerChallenge(Challenge, Buffer, sizeof(Buffer), &Length)) &&
            Length == sizeof(Challenge));
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeServerChallenge(Buffer, Length, &BufferView)) &&
            BufferView.Length == sizeof(Challenge) &&
            RtlCompareMemory(BufferView.Buffer, Challenge, sizeof(Challenge)) == sizeof(Challenge));
    TEST_OK(ZpMessage_DecodeServerChallenge(Buffer, Length - 1, &BufferView) == STATUS_DATA_ERROR);

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeClientAuthenticate(Signature, Buffer, sizeof(Buffer), &Length)) &&
            Length == sizeof(Signature));
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeClientAuthenticate(Buffer, Length, &BufferView)) &&
            BufferView.Length == sizeof(Signature) &&
            RtlCompareMemory(BufferView.Buffer, Signature, sizeof(Signature)) == sizeof(Signature));

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeReady(&Ready, Buffer, sizeof(Buffer), &Length)) && Length == 10);
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeReady(Buffer, Length, &ReadyView)) &&
            ReadyView.Modules.Count == ARRAYSIZE(Modules));
    TEST_OK(NT_SUCCESS(ZpMessage_GetModuleRecord(&ReadyView.Modules, 1, &Module)) &&
            Module.ModuleId == Modules[1].ModuleId);

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeRequest(&Request, Buffer, sizeof(Buffer), &Length)) && Length == 19);
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeRequest(Buffer, Length, &RequestView)) &&
            RequestView.RequestId == Request.RequestId &&
            RequestView.ModuleId == Request.ModuleId &&
            RequestView.OperationId == Request.OperationId &&
            RequestView.TimeoutMilliseconds == Request.TimeoutMilliseconds &&
            RequestView.Payload.Length == sizeof(RequestPayload) &&
            RtlCompareMemory(RequestView.Payload.Buffer,
                             RequestPayload,
                             sizeof(RequestPayload)) == sizeof(RequestPayload));
    Request.RequestId = 0;
    TEST_OK(ZpMessage_EncodeRequest(&Request, NULL, 0, &Length) == STATUS_INVALID_PARAMETER);
    Request.RequestId = 7;

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeResponse(&Response, Buffer, sizeof(Buffer), &Length)) && Length == 16);
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeResponse(Buffer, Length, &ResponseView)) &&
            ResponseView.RequestId == Response.RequestId &&
            ResponseView.Status.Type == Response.Status.Type &&
            ResponseView.Status.Code == Response.Status.Code &&
            ResponseView.Payload.Length == sizeof(ResponsePayload) &&
            RtlCompareMemory(ResponseView.Payload.Buffer,
                             ResponsePayload,
                             sizeof(ResponsePayload)) == sizeof(ResponsePayload));

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeCancel(7, Buffer, sizeof(Buffer), &Length)) &&
            Length == sizeof(ULONGLONG) &&
            NT_SUCCESS(ZpMessage_DecodeCancel(Buffer, Length, &Value)) &&
            Value == 7);
    TEST_OK(ZpMessage_EncodeCancel(0, NULL, 0, &Length) == STATUS_INVALID_PARAMETER);

    TEST_OK(NT_SUCCESS(ZpMessage_EncodePing(MAXULONGLONG, Buffer, sizeof(Buffer), &Length)) &&
            Length == sizeof(ULONGLONG) &&
            NT_SUCCESS(ZpMessage_DecodePing(ZpMessagePing, Buffer, Length, &Value)) &&
            Value == MAXULONGLONG &&
            NT_SUCCESS(ZpMessage_DecodePing(ZpMessagePong, Buffer, Length, &Value)) &&
            Value == MAXULONGLONG);
    TEST_OK(ZpMessage_DecodePing(ZpMessageRequest, Buffer, Length, &Value) == STATUS_INVALID_PARAMETER);

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeChannelData(9,
                                                   RequestPayload,
                                                   sizeof(RequestPayload),
                                                   Buffer,
                                                   sizeof(Buffer),
                                                   &Length)) &&
            Length == sizeof(ULONGLONG) + sizeof(RequestPayload) &&
            NT_SUCCESS(ZpMessage_DecodeChannelData(Buffer,
                                                   Length,
                                                   &ChannelDataView)) &&
            ChannelDataView.ChannelId == 9 &&
            ChannelDataView.Data.Length == sizeof(RequestPayload) &&
            RtlCompareMemory(ChannelDataView.Data.Buffer,
                             RequestPayload,
                             sizeof(RequestPayload)) == sizeof(RequestPayload));
    TEST_OK(NT_SUCCESS(ZpMessage_EncodeChannelClose(9,
                                                    ZpStatus_Make(
                                                        ZpStatusProcessExit,
                                                        7),
                                                    Buffer,
                                                    sizeof(Buffer),
                                                    &Length)) &&
            NT_SUCCESS(ZpMessage_DecodeChannelClose(Buffer,
                                                    Length,
                                                    &ChannelClose)) &&
            Length == sizeof(ULONGLONG) + ZP_STATUS_WIRE_SIZE &&
            ChannelClose.ChannelId == 9 &&
            ChannelClose.Status.Type == ZpStatusProcessExit &&
            ChannelClose.Status.Code == 7);
    TEST_OK(NT_SUCCESS(ZpMessage_EncodeChannelWindow(9,
                                                     ZP_CHANNEL_DATA_MAX_SIZE,
                                                     Buffer,
                                                     sizeof(Buffer),
                                                     &Length)) &&
            NT_SUCCESS(ZpMessage_DecodeChannelWindow(Buffer,
                                                     Length,
                                                     &Value,
                                                     &CreditBytes)) &&
            Value == 9 &&
            CreditBytes == ZP_CHANNEL_DATA_MAX_SIZE);
    TEST_OK(ZpMessage_EncodeChannelData(9,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        &Length) == STATUS_INVALID_PARAMETER &&
            ZpMessage_EncodeChannelWindow(9,
                                          ZP_CHANNEL_WINDOW_MAX_SIZE + 1,
                                          NULL,
                                          0,
                                          &Length) == STATUS_INVALID_PARAMETER);

    TEST_OK(NT_SUCCESS(ZpSystem_EncodeInfo(&SystemInfo, Buffer, sizeof(Buffer), &Length)) &&
            Length == 48 &&
            NT_SUCCESS(ZpSystem_DecodeInfo(Buffer, Length, &SystemInfoView)) &&
            SystemInfoView.Architecture == SystemInfo.Architecture &&
            SystemInfoView.BuildNumber == SystemInfo.BuildNumber &&
            SystemInfoView.ProcessorCount == SystemInfo.ProcessorCount &&
            SystemInfoView.PhysicalMemoryBytes == SystemInfo.PhysicalMemoryBytes &&
            SystemInfoView.ComputerName.Length == SystemInfo.ComputerNameLength &&
            RtlCompareMemory(SystemInfoView.ComputerName.Buffer,
                             SystemInfo.ComputerName,
                             SystemInfo.ComputerNameLength * sizeof(WCHAR)) ==
                SystemInfo.ComputerNameLength * sizeof(WCHAR));
    TEST_OK(ZpSystem_DecodeInfo(Buffer, Length - 1, &SystemInfoView) == STATUS_DATA_ERROR);

    TEST_OK(NT_SUCCESS(ZpProcess_EncodeList(Processes,
                                           ARRAYSIZE(Processes),
                                           Buffer,
                                           sizeof(Buffer),
                                           &Length)) &&
            Length == 154 &&
            NT_SUCCESS(ZpProcess_DecodeList(Buffer, Length, &ProcessList)) &&
            ProcessList.Count == ARRAYSIZE(Processes) &&
            NT_SUCCESS(ZpProcess_GetRecord(&ProcessList, 0, &ProcessRecord)) &&
            ProcessRecord.ProcessId == 0 &&
            ProcessRecord.ImageName.Length == 0 &&
            NT_SUCCESS(ZpProcess_GetRecord(&ProcessList, 1, &ProcessRecord)) &&
            ProcessRecord.ProcessId == Processes[1].ProcessId &&
            ProcessRecord.ParentProcessId == Processes[1].ParentProcessId &&
            ProcessRecord.SessionId == Processes[1].SessionId &&
            ProcessRecord.ThreadCount == Processes[1].ThreadCount &&
            ProcessRecord.HandleCount == Processes[1].HandleCount &&
            ProcessRecord.CreateTime == Processes[1].CreateTime &&
            ProcessRecord.UserTime == Processes[1].UserTime &&
            ProcessRecord.KernelTime == Processes[1].KernelTime &&
            ProcessRecord.WorkingSetBytes == Processes[1].WorkingSetBytes &&
            ProcessRecord.PrivateBytes == Processes[1].PrivateBytes &&
            ProcessRecord.ImageName.Length == Processes[1].ImageNameLength &&
            RtlCompareMemory(ProcessRecord.ImageName.Buffer,
                             Processes[1].ImageName,
                             Processes[1].ImageNameLength * sizeof(WCHAR)) ==
                Processes[1].ImageNameLength * sizeof(WCHAR));
    TEST_OK(ZpProcess_GetRecord(&ProcessList,
                                ProcessList.Count,
                                &ProcessRecord) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpProcess_EncodeQuery(ProcessInfo.ProcessId,
                                             ProcessInfo.CreateTime,
                                             Buffer,
                                             sizeof(Buffer),
                                             &Length)) &&
            NT_SUCCESS(ZpProcess_DecodeQuery(Buffer, Length, &Index, &Value)) &&
            Index == ProcessInfo.ProcessId &&
            Value == ProcessInfo.CreateTime);
    TEST_OK(NT_SUCCESS(ZpProcess_EncodeInfo(&ProcessInfo,
                                            Buffer,
                                            sizeof(Buffer),
                                            &Length)) &&
            Length == 164 &&
            NT_SUCCESS(ZpProcess_DecodeInfo(Buffer, Length, &ProcessInfoView)) &&
            ProcessInfoView.ProcessId == ProcessInfo.ProcessId &&
            ProcessInfoView.ParentProcessId == ProcessInfo.ParentProcessId &&
            ProcessInfoView.ThreadCount == ProcessInfo.ThreadCount &&
            ProcessInfoView.WorkingSetBytes == ProcessInfo.WorkingSetBytes &&
            ProcessInfoView.PrivateBytes == ProcessInfo.PrivateBytes &&
            ProcessInfoView.ImageName.Length == ProcessInfo.ImageNameLength &&
            ProcessInfoView.ImagePathStatus == STATUS_SUCCESS &&
            ProcessInfoView.ImagePath.Length == ProcessInfo.ImagePathLength &&
            ProcessInfoView.CommandLineStatus == STATUS_SUCCESS &&
            ProcessInfoView.CommandLine.Length == ProcessInfo.CommandLineLength);
    TEST_OK(NT_SUCCESS(ZpProcess_EncodeTerminate(ProcessInfo.ProcessId,
                                                 ProcessInfo.CreateTime,
                                                 0x10203040,
                                                 Buffer,
                                                 sizeof(Buffer),
                                                 &Length)) &&
            Length == 2 * sizeof(ULONG) + sizeof(ULONGLONG) &&
            NT_SUCCESS(ZpProcess_DecodeTerminate(Buffer,
                                                 Length,
                                                 &Index,
                                                 &Value,
                                                 &ExitCode)) &&
            Index == ProcessInfo.ProcessId &&
            Value == ProcessInfo.CreateTime &&
            ExitCode == 0x10203040);
    TEST_OK(ZpProcess_EncodeTerminate(0,
                                      ProcessInfo.CreateTime,
                                      0,
                                      NULL,
                                      0,
                                      &Length) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpService_EncodeList(Services,
                                           ARRAYSIZE(Services),
                                           Buffer,
                                           sizeof(Buffer),
                                           &Length)) &&
            Length == 50 &&
            NT_SUCCESS(ZpService_DecodeList(Buffer, Length, &ServiceList)) &&
            ServiceList.Count == ARRAYSIZE(Services) &&
            NT_SUCCESS(ZpService_GetRecord(&ServiceList, 0, &ServiceRecord)) &&
            ServiceRecord.ServiceType == Services[0].ServiceType &&
            ServiceRecord.CurrentState == Services[0].CurrentState &&
            ServiceRecord.ProcessId == Services[0].ProcessId &&
            ServiceRecord.ServiceName.Length == Services[0].ServiceNameLength &&
            ServiceRecord.DisplayName.Length == Services[0].DisplayNameLength);
    TEST_OK(ZpService_GetRecord(&ServiceList,
                                ServiceList.Count,
                                &ServiceRecord) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpService_EncodeQuery(ServiceInfo.ServiceName,
                                             ServiceInfo.ServiceNameLength,
                                             Buffer,
                                             sizeof(Buffer),
                                             &Length)) &&
            NT_SUCCESS(ZpService_DecodeQuery(Buffer,
                                             Length,
                                             &ServiceInfoView.ServiceName)) &&
            ServiceInfoView.ServiceName.Length == ServiceInfo.ServiceNameLength);
    TEST_OK(NT_SUCCESS(ZpService_EncodeInfo(&ServiceInfo,
                                            Buffer,
                                            sizeof(Buffer),
                                            &Length)) &&
            NT_SUCCESS(ZpService_DecodeInfo(Buffer,
                                            Length,
                                            &ServiceInfoView)) &&
            ServiceInfoView.ServiceType == ServiceInfo.ServiceType &&
            ServiceInfoView.CurrentState == ServiceInfo.CurrentState &&
            ServiceInfoView.ProcessId == ServiceInfo.ProcessId &&
            ServiceInfoView.StartType == ServiceInfo.StartType &&
            ServiceInfoView.ErrorControl == ServiceInfo.ErrorControl &&
            ServiceInfoView.ServiceName.Length == ServiceInfo.ServiceNameLength &&
            ServiceInfoView.DisplayName.Length == ServiceInfo.DisplayNameLength &&
            ServiceInfoView.BinaryPathName.Length == ServiceInfo.BinaryPathNameLength &&
            ServiceInfoView.StartName.Length == ServiceInfo.StartNameLength);
    TEST_OK(NT_SUCCESS(ZpFile_EncodePath(L"C:\\Test.bin",
                                         11,
                                         Buffer,
                                         sizeof(Buffer),
                                         &Length)) &&
            NT_SUCCESS(ZpFile_DecodePath(Buffer, Length, &FilePathView)) &&
            FilePathView.Length == 11);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeRenameRequest(L"C:\\Test.bin",
                                                  11,
                                                  L"C:\\Renamed.bin",
                                                  14,
                                                  Buffer,
                                                  sizeof(Buffer),
                                                  &Length)) &&
            NT_SUCCESS(ZpFile_DecodeRenameRequest(Buffer,
                                                  Length,
                                                  &FilePathView,
                                                  &FilePage.NextCursor)) &&
            FilePathView.Length == 11 &&
            FilePage.NextCursor.Length == 14);
    TEST_OK(ZpFile_EncodeRenameRequest(NULL,
                                       0,
                                       L"C:\\Renamed.bin",
                                       14,
                                       Buffer,
                                       sizeof(Buffer),
                                       &Length) == STATUS_INVALID_PARAMETER &&
            ZpFile_DecodeRenameRequest(Buffer,
                                       Length - 1,
                                       &FilePathView,
                                       &FilePage.NextCursor) == STATUS_DATA_ERROR);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeEnumeratePageRequest(L"C:\\Test",
                                                         7,
                                                         NULL,
                                                         0,
                                                         128,
                                                         Buffer,
                                                         sizeof(Buffer),
                                                         &Length)) &&
            NT_SUCCESS(ZpFile_DecodeEnumeratePageRequest(Buffer,
                                                         Length,
                                                         &FilePathView,
                                                         &FilePage.NextCursor,
                                                         &MaxEntries)) &&
            FilePathView.Length == 7 &&
            FilePage.NextCursor.Length == 0 &&
            MaxEntries == 128);
    TEST_OK(ZpFile_EncodeEnumeratePageRequest(L"C:\\Test",
                                              7,
                                              NULL,
                                              0,
                                              0,
                                              Buffer,
                                              sizeof(Buffer),
                                              &Length) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeOpenReadRequest(L"C:\\Test.bin",
                                                    11,
                                                    4096,
                                                    Buffer,
                                                    sizeof(Buffer),
                                                    &Length)) &&
            NT_SUCCESS(ZpFile_DecodeOpenReadRequest(Buffer,
                                                    Length,
                                                    &FilePathView,
                                                    &FileOffset)) &&
            FilePathView.Length == 11 &&
            FileOffset == 4096);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeOpenReadResponse(2,
                                                     8192,
                                                     4096,
                                                     Buffer,
                                                     sizeof(Buffer),
                                                     &Length)) &&
            Length == 3 * sizeof(ULONGLONG) &&
            NT_SUCCESS(ZpFile_DecodeOpenReadResponse(Buffer,
                                                     Length,
                                                     &Value,
                                                     &FileSize,
                                                     &FileOffset)) &&
            Value == 2 &&
            FileSize == 8192 &&
            FileOffset == 4096);
    TEST_OK(ZpFile_EncodeOpenReadResponse(0,
                                          8192,
                                          4096,
                                          Buffer,
                                          sizeof(Buffer),
                                          &Length) == STATUS_INVALID_PARAMETER &&
            ZpFile_EncodeOpenReadResponse(2,
                                          4095,
                                          4096,
                                          Buffer,
                                          sizeof(Buffer),
                                          &Length) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeOpenWriteRequest(L"C:\\Test.bin",
                                                     11,
                                                     12345,
                                                     ZpFileCreateAlways,
                                                     Buffer,
                                                     sizeof(Buffer),
                                                     &Length)) &&
            NT_SUCCESS(ZpFile_DecodeOpenWriteRequest(Buffer,
                                                     Length,
                                                     &FilePathView,
                                                     &FileSize,
                                                     &FileDisposition)) &&
            FilePathView.Length == 11 &&
            FileSize == 12345 &&
            FileDisposition == ZpFileCreateAlways);
    TEST_OK(ZpFile_EncodeOpenWriteRequest(L"C:\\Test.bin",
                                          11,
                                          12345,
                                          (ZP_FILE_CREATE_DISPOSITION)0,
                                          Buffer,
                                          sizeof(Buffer),
                                          &Length) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeOpenWriteResponse(4,
                                                      12345,
                                                      Buffer,
                                                      sizeof(Buffer),
                                                      &Length)) &&
            NT_SUCCESS(ZpFile_DecodeOpenWriteResponse(Buffer,
                                                      Length,
                                                      &Value,
                                                      &FileSize)) &&
            Value == 4 &&
            FileSize == 12345);
    TEST_OK(ZpFile_EncodeOpenWriteResponse(0,
                                           12345,
                                           Buffer,
                                           sizeof(Buffer),
                                           &Length) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeHashRequest(ZpFileHashSha256,
                                                L"C:\\Test.bin",
                                                11,
                                                Buffer,
                                                sizeof(Buffer),
                                                &Length)) &&
            NT_SUCCESS(ZpFile_DecodeHashRequest(Buffer,
                                                Length,
                                                &FileHashAlgorithm,
                                                &FilePathView)) &&
            FileHashAlgorithm == ZpFileHashSha256 &&
            FilePathView.Length == 11);
    TEST_OK(ZpFile_EncodeHashRequest((ZP_FILE_HASH_ALGORITHM)0,
                                     L"C:\\Test.bin",
                                     11,
                                     Buffer,
                                     sizeof(Buffer),
                                     &Length) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeHashResponse(ZpFileHashSha256,
                                                 8192,
                                                 FileDigest,
                                                 sizeof(FileDigest),
                                                 Buffer,
                                                 sizeof(Buffer),
                                                 &Length)) &&
            NT_SUCCESS(ZpFile_DecodeHashResponse(Buffer,
                                                 Length,
                                                 &FileHashView)) &&
            FileHashView.Algorithm == ZpFileHashSha256 &&
            FileHashView.FileSize == 8192 &&
            FileHashView.Digest.Length == sizeof(FileDigest) &&
            RtlCompareMemory(FileHashView.Digest.Buffer,
                             FileDigest,
                             sizeof(FileDigest)) == sizeof(FileDigest));
    TEST_OK(ZpFile_EncodeHashResponse(ZpFileHashSha256,
                                      8192,
                                      FileDigest,
                                      sizeof(FileDigest) - 1,
                                      Buffer,
                                      sizeof(Buffer),
                                      &Length) == STATUS_INVALID_PARAMETER &&
            ZpFile_DecodeHashResponse(Buffer,
                                      sizeof(USHORT) + sizeof(ULONGLONG),
                                      &FileHashView) == STATUS_DATA_ERROR);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeInfo(&FileInfo,
                                         Buffer,
                                         sizeof(Buffer),
                                         &Length)) &&
            Length == sizeof(ULONG) + 4 * sizeof(ULONGLONG) &&
            NT_SUCCESS(ZpFile_DecodeInfo(Buffer, Length, &FileInfoView)) &&
            FileInfoView.Attributes == FileInfo.Attributes &&
            FileInfoView.Size == FileInfo.Size &&
            FileInfoView.CreationTime == FileInfo.CreationTime &&
            FileInfoView.LastAccessTime == FileInfo.LastAccessTime &&
            FileInfoView.LastWriteTime == FileInfo.LastWriteTime);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeList(FileRecords,
                                         ARRAYSIZE(FileRecords),
                                         Buffer,
                                         sizeof(Buffer),
                                         &Length)) &&
            NT_SUCCESS(ZpFile_DecodeList(Buffer, Length, &FileList)) &&
            FileList.Count == ARRAYSIZE(FileRecords) &&
            NT_SUCCESS(ZpFile_GetRecord(&FileList, 0, &FileRecord)) &&
            FileRecord.Info.Attributes == FileRecords[0].Info.Attributes &&
            FileRecord.Info.Size == FileRecords[0].Info.Size &&
            FileRecord.Name.Length == FileRecords[0].NameLength);
    TEST_OK(ZpFile_GetRecord(&FileList,
                              FileList.Count,
                              &FileRecord) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpFile_EncodePage(FileRecords,
                                         ARRAYSIZE(FileRecords),
                                         FileRecords[0].Name,
                                         FileRecords[0].NameLength,
                                         Buffer,
                                         sizeof(Buffer),
                                         &Length)) &&
            NT_SUCCESS(ZpFile_DecodePage(Buffer, Length, &FilePage)) &&
            FilePage.NextCursor.Length == FileRecords[0].NameLength &&
            FilePage.Files.Count == ARRAYSIZE(FileRecords) &&
            NT_SUCCESS(ZpFile_GetRecord(&FilePage.Files, 0, &FileRecord)) &&
            FileRecord.Name.Length == FileRecords[0].NameLength);
    TEST_OK(NT_SUCCESS(ZpFile_EncodePage(FileRecords,
                                         ARRAYSIZE(FileRecords),
                                         NULL,
                                         0,
                                         Buffer,
                                         sizeof(Buffer),
                                         &Length)) &&
            NT_SUCCESS(ZpFile_DecodePage(Buffer, Length, &FilePage)) &&
            FilePage.NextCursor.Length == 0 &&
            FilePage.Files.Count == ARRAYSIZE(FileRecords));
    TEST_OK(NT_SUCCESS(ZpEventLog_EncodeQueryPageRequest(
                           ZpEventLogStartOldest,
                           16,
                           EventChannel,
                           ARRAYSIZE(EventChannel) - 1,
                           EventQuery,
                           ARRAYSIZE(EventQuery) - 1,
                           NULL,
                           0,
                           Buffer,
                           sizeof(Buffer),
                           &Length)) &&
            NT_SUCCESS(ZpEventLog_DecodeQueryPageRequest(Buffer,
                                                         Length,
                                                         &EventLogQuery)) &&
            EventLogQuery.StartMode == ZpEventLogStartOldest &&
            EventLogQuery.MaxEvents == 16 &&
            EventLogQuery.ChannelPath.Length == ARRAYSIZE(EventChannel) - 1 &&
            EventLogQuery.Query.Length == ARRAYSIZE(EventQuery) - 1 &&
            EventLogQuery.Bookmark.Length == 0);
    TEST_OK(NT_SUCCESS(ZpEventLog_EncodeQueryPageRequest(
                           ZpEventLogStartAfterBookmark,
                           ZP_EVENT_LOG_PAGE_MAX_COUNT,
                           EventChannel,
                           ARRAYSIZE(EventChannel) - 1,
                           NULL,
                           0,
                           EventBookmark,
                           ARRAYSIZE(EventBookmark) - 1,
                           Buffer,
                           sizeof(Buffer),
                           &Length)) &&
            NT_SUCCESS(ZpEventLog_DecodeQueryPageRequest(Buffer,
                                                         Length,
                                                         &EventLogQuery)) &&
            EventLogQuery.StartMode == ZpEventLogStartAfterBookmark &&
            EventLogQuery.Bookmark.Length == ARRAYSIZE(EventBookmark) - 1);
    TEST_OK(ZpEventLog_EncodeQueryPageRequest((ZP_EVENT_LOG_START_MODE)0,
                                              16,
                                              EventChannel,
                                              ARRAYSIZE(EventChannel) - 1,
                                              NULL,
                                              0,
                                              NULL,
                                              0,
                                              Buffer,
                                              sizeof(Buffer),
                                              &Length) ==
                STATUS_INVALID_PARAMETER &&
            ZpEventLog_EncodeQueryPageRequest(ZpEventLogStartAfterBookmark,
                                              16,
                                              EventChannel,
                                              ARRAYSIZE(EventChannel) - 1,
                                              NULL,
                                              0,
                                              NULL,
                                              0,
                                              Buffer,
                                              sizeof(Buffer),
                                              &Length) ==
                STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpEventLog_EncodeSetChannelEnabledRequest(
                           EventChannel,
                           ARRAYSIZE(EventChannel) - 1,
                           TRUE,
                           Buffer,
                           sizeof(Buffer),
                           &Length)) &&
            NT_SUCCESS(ZpEventLog_DecodeSetChannelEnabledRequest(
                Buffer,
                Length,
                &EventLogChannel,
                &EventLogEnabled)) &&
            EventLogEnabled &&
            EventLogChannel.Length == ARRAYSIZE(EventChannel) - 1);
    TEST_OK(NT_SUCCESS(ZpEventLog_EncodePage(
                           TRUE,
                           EventRecords,
                           ARRAYSIZE(EventRecords),
                           EventBookmark,
                           ARRAYSIZE(EventBookmark) - 1,
                           Buffer,
                           sizeof(Buffer),
                           &Length)) &&
            NT_SUCCESS(ZpEventLog_DecodePage(Buffer, Length, &EventLogPage)) &&
            EventLogPage.HasMore &&
            EventLogPage.NextBookmark.Length == ARRAYSIZE(EventBookmark) - 1 &&
            EventLogPage.Records.Count == ARRAYSIZE(EventRecords) &&
            NT_SUCCESS(ZpEventLog_GetRecord(&EventLogPage.Records,
                                            0,
                                            &EventLogRecord)) &&
            EventLogRecord.Bookmark.Length == ARRAYSIZE(EventBookmark) - 1 &&
            EventLogRecord.Xml.Length == ARRAYSIZE(EventXml) - 1);
    TEST_OK(ZpEventLog_EncodePage(TRUE,
                                 EventRecords,
                                 ARRAYSIZE(EventRecords),
                                 L"different",
                                 9,
                                 Buffer,
                                 sizeof(Buffer),
                                 &Length) == STATUS_INVALID_PARAMETER &&
            ZpEventLog_GetRecord(&EventLogPage.Records,
                                 EventLogPage.Records.Count,
                                 &EventLogRecord) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpEventLog_EncodeClearRequest(
                           EventChannel,
                           ARRAYSIZE(EventChannel) - 1,
                           Buffer,
                           sizeof(Buffer),
                           &Length)) &&
            NT_SUCCESS(ZpEventLog_DecodeClearRequest(Buffer,
                                                     Length,
                                                     &EventLogChannel)) &&
            EventLogChannel.Length == ARRAYSIZE(EventChannel) - 1);
    TEST_OK(NT_SUCCESS(ZpRegistry_EncodeEnumerateRequest(
                           ZpRegistryLocalMachine,
                           32,
                           TRUE,
                           RegistryPath,
                           ARRAYSIZE(RegistryPath) - 1,
                           L"",
                           0,
                           Buffer,
                           sizeof(Buffer),
                           &Length)) &&
            NT_SUCCESS(ZpRegistry_DecodeEnumerateRequest(Buffer,
                                                         Length,
                                                         &RegistryEnumerate)) &&
            RegistryEnumerate.Root == ZpRegistryLocalMachine &&
            RegistryEnumerate.MaxEntries == 32 &&
            RegistryEnumerate.CursorPresent &&
            RegistryEnumerate.Path.Length == ARRAYSIZE(RegistryPath) - 1 &&
            RegistryEnumerate.Cursor.Length == 0);
    TEST_OK(ZpRegistry_EncodeEnumerateRequest(
                0,
                1,
                FALSE,
                NULL,
                0,
                L"cursor",
                6,
                Buffer,
                sizeof(Buffer),
                &Length) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpRegistry_EncodeKeyPage(
                           TRUE,
                           RegistryKeys,
                           ARRAYSIZE(RegistryKeys),
                           L"Beta",
                           4,
                           Buffer,
                           sizeof(Buffer),
                           &Length)) &&
            NT_SUCCESS(ZpRegistry_DecodeKeyPage(Buffer,
                                                Length,
                                                &RegistryPage)) &&
            RegistryPage.HasMore &&
            RegistryPage.Records.Count == ARRAYSIZE(RegistryKeys) &&
            NT_SUCCESS(ZpRegistry_GetKeyRecord(&RegistryPage.Records,
                                               1,
                                               &RegistryKey)) &&
            RegistryKey.Name.Length == 4 &&
            RegistryKey.LastWriteTime == 200 &&
            RegistryKey.HasChildren);
    TEST_OK(NT_SUCCESS(ZpRegistry_EncodeValuePage(
                           TRUE,
                           RegistryValues,
                           ARRAYSIZE(RegistryValues),
                           L"",
                           0,
                           Buffer,
                           sizeof(Buffer),
                           &Length)) &&
            NT_SUCCESS(ZpRegistry_DecodeValuePage(Buffer,
                                                  Length,
                                                  &RegistryPage)) &&
            RegistryPage.HasMore &&
            RegistryPage.NextCursor.Length == 0 &&
            NT_SUCCESS(ZpRegistry_GetValueRecord(&RegistryPage.Records,
                                                 0,
                                                 &RegistryValueRecord)) &&
            RegistryValueRecord.Name.Length == 0 &&
            RegistryValueRecord.Type == 1 &&
            RegistryValueRecord.DataLength == 8 &&
            RegistryValueRecord.Preview.Length == sizeof(RegistryData));
    TEST_OK(NT_SUCCESS(ZpRegistry_EncodeValueRequest(
                           ZpRegistryCurrentUser,
                           RegistryPath,
                           ARRAYSIZE(RegistryPath) - 1,
                           RegistryValueName,
                           ARRAYSIZE(RegistryValueName) - 1,
                           Buffer,
                           sizeof(Buffer),
                           &Length)) &&
            NT_SUCCESS(ZpRegistry_DecodeValueRequest(Buffer,
                                                     Length,
                                                     &RegistryValueRequest)) &&
            RegistryValueRequest.Root == ZpRegistryCurrentUser &&
            RegistryValueRequest.ValueName.Length ==
                ARRAYSIZE(RegistryValueName) - 1);
    TEST_OK(NT_SUCCESS(ZpRegistry_EncodeValue(4,
                                              RegistryData,
                                              sizeof(RegistryData),
                                              Buffer,
                                              sizeof(Buffer),
                                              &Length)) &&
            NT_SUCCESS(ZpRegistry_DecodeValue(Buffer,
                                              Length,
                                              &RegistryValue)) &&
            RegistryValue.Type == 4 &&
            RegistryValue.Data.Length == sizeof(RegistryData) &&
            RtlCompareMemory(RegistryValue.Data.Buffer,
                             RegistryData,
                             sizeof(RegistryData)) == sizeof(RegistryData));
    TEST_OK(NT_SUCCESS(ZpRegistry_EncodeSetValueRequest(
                           ZpRegistryCurrentUser,
                           3,
                           RegistryPath,
                           ARRAYSIZE(RegistryPath) - 1,
                           RegistryValueName,
                           ARRAYSIZE(RegistryValueName) - 1,
                           RegistryData,
                           sizeof(RegistryData),
                           Buffer,
                           sizeof(Buffer),
                           &Length)) &&
            NT_SUCCESS(ZpRegistry_DecodeSetValueRequest(Buffer,
                                                        Length,
                                                        &RegistrySetValue)) &&
            RegistrySetValue.Type == 3 &&
            RegistrySetValue.Data.Length == sizeof(RegistryData));
    TEST_OK(NT_SUCCESS(ZpRegistry_EncodeKeyRequest(
                           ZpRegistryLocalMachine,
                           RegistryPath,
                           ARRAYSIZE(RegistryPath) - 1,
                           Buffer,
                           sizeof(Buffer),
                           &Length)) &&
            NT_SUCCESS(ZpRegistry_DecodeKeyRequest(Buffer,
                                                   Length,
                                                   &RegistryKeyRequest)) &&
            RegistryKeyRequest.Path.Length == ARRAYSIZE(RegistryPath) - 1 &&
            ZpRegistry_EncodeKeyRequest(ZpRegistryLocalMachine,
                                        NULL,
                                        0,
                                        Buffer,
                                        sizeof(Buffer),
                                        &Length) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpRegistry_EncodeRenameRequest(
                           ZpRegistryCurrentUser,
                           RegistryPath,
                           ARRAYSIZE(RegistryPath) - 1,
                           RegistryValueName,
                           ARRAYSIZE(RegistryValueName) - 1,
                           RegistryNewName,
                           ARRAYSIZE(RegistryNewName) - 1,
                           Buffer,
                           sizeof(Buffer),
                           &Length)) &&
            NT_SUCCESS(ZpRegistry_DecodeRenameRequest(
                Buffer,
                Length,
                &RegistryRenameRequest)) &&
            RegistryRenameRequest.Name.Length ==
                ARRAYSIZE(RegistryValueName) - 1 &&
            RegistryRenameRequest.NewName.Length ==
                ARRAYSIZE(RegistryNewName) - 1);
    TEST_OK(NT_SUCCESS(ZpTerminal_EncodeCreate(120,
                                               30,
                                               L"cmd.exe /Q",
                                               10,
                                               L"C:\\",
                                               3,
                                               Buffer,
                                               sizeof(Buffer),
                                               &Length)) &&
            NT_SUCCESS(ZpTerminal_DecodeCreate(Buffer,
                                               Length,
                                               &TerminalCreate)) &&
            TerminalCreate.Columns == 120 &&
            TerminalCreate.Rows == 30 &&
            TerminalCreate.CommandLine.Length == 10 &&
            TerminalCreate.WorkingDirectory.Length == 3);
    TEST_OK(NT_SUCCESS(ZpTerminal_EncodeCreateResponse(2,
                                                       4321,
                                                       Buffer,
                                                       sizeof(Buffer),
                                                       &Length)) &&
            NT_SUCCESS(ZpTerminal_DecodeCreateResponse(Buffer,
                                                       Length,
                                                       &Value,
                                                       &ProcessId)) &&
            Value == 2 &&
            ProcessId == 4321);
    TEST_OK(NT_SUCCESS(ZpTerminal_EncodeResize(2,
                                               80,
                                               25,
                                               Buffer,
                                               sizeof(Buffer),
                                               &Length)) &&
            NT_SUCCESS(ZpTerminal_DecodeResize(Buffer,
                                               Length,
                                               &Value,
                                               &Columns,
                                               &Rows)) &&
            Value == 2 &&
            Columns == 80 &&
            Rows == 25);
    TEST_OK(NT_SUCCESS(ZpTerminal_EncodeShells(
                           ZpTerminalShellCommandPrompt |
                               ZpTerminalShellPowerShell,
                           Buffer,
                           sizeof(Buffer),
                           &Length)) &&
            NT_SUCCESS(ZpTerminal_DecodeShells(Buffer,
                                               Length,
                                               &Shells)) &&
            Shells == (ZpTerminalShellCommandPrompt |
                       ZpTerminalShellPowerShell));
    TEST_OK(ZpTerminal_EncodeCreateResponse(0,
                                             4321,
                                             Buffer,
                                             sizeof(Buffer),
                                             &Length) == STATUS_INVALID_PARAMETER &&
            ZpTerminal_EncodeResize(2,
                                     0,
                                     25,
                                     Buffer,
                                     sizeof(Buffer),
                                     &Length) == STATUS_INVALID_PARAMETER &&
            ZpTerminal_DecodeShells(Buffer,
                                    sizeof(ULONG) - 1,
                                    &Shells) == STATUS_DATA_ERROR);
}
