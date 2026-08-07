#include "UnitTest.h"

#include <KNSoft/ZPigeon/File.h>
#include <KNSoft/ZPigeon/Protocol.h>
#include <KNSoft/ZPigeon/Process.h>
#include <KNSoft/ZPigeon/Service.h>
#include <KNSoft/ZPigeon/System.h>
#include <KNSoft/ZPigeon/Terminal.h>

TEST_FUNC(ProtocolMessage)
{
    ZP_MODULE_RECORD Modules[] = {
        { 1, 1, 0x01020304 },
        { 3, 2, 0xA0B0C0D0 }
    };
    BYTE PublicKey[ZP_CLIENT_PUBLIC_KEY_SIZE] = { 0x04 };
    BYTE Challenge[ZP_SERVER_CHALLENGE_SIZE], Signature[ZP_CLIENT_SIGNATURE_SIZE], Buffer[256];
    ZP_CLIENT_HELLO ClientHello = { ZP_CORE_VERSION, Modules, ARRAYSIZE(Modules), PublicKey };
    ZP_CLIENT_HELLO_VIEW ClientHelloView;
    ZP_READY Ready = { Modules, ARRAYSIZE(Modules) };
    ZP_READY_VIEW ReadyView;
    ZP_DISCONNECT Disconnect = { STATUS_ACCESS_DENIED, L"Denied", 6 };
    ZP_DISCONNECT_VIEW DisconnectView;
    const BYTE RequestPayload[] = { 1, 2, 3 };
    const BYTE ResponsePayload[] = { 4, 5 };
    ZP_REQUEST Request = { 7, 1, 2, 5000, RequestPayload, sizeof(RequestPayload) };
    ZP_REQUEST_VIEW RequestView;
    ZP_RESPONSE Response = { 7, STATUS_SUCCESS, ResponsePayload, sizeof(ResponsePayload) };
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
        { 0, 0, L"", 0 },
        { 1234, 1, L"example.exe", 11 }
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
        11
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
    ZP_FILE_RECORD_VIEW FileRecord;
    ZP_TERMINAL_CREATE_VIEW TerminalCreate;
    ZP_MODULE_RECORD Module;
    ZP_BUFFER_VIEW BufferView;
    ULONGLONG Value, FileSize, FileOffset;
    ULONG Length, Index, ExitCode, CreditBytes, ProcessId;
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

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeClientHello(&ClientHello, NULL, 0, &Length)) && Length == 85);
    TEST_OK(ZpMessage_EncodeClientHello(&ClientHello, Buffer, Length - 1, &Index) == STATUS_BUFFER_TOO_SMALL &&
            Index == Length);
    TEST_OK(NT_SUCCESS(ZpMessage_EncodeClientHello(&ClientHello, Buffer, sizeof(Buffer), &Length)) && Length == 85);
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeClientHello(Buffer, Length, &ClientHelloView)) &&
            ClientHelloView.CoreVersion == ZP_CORE_VERSION &&
            ClientHelloView.Modules.Count == ARRAYSIZE(Modules) &&
            RtlCompareMemory(ClientHelloView.ClientPublicKey,
                             PublicKey,
                             sizeof(PublicKey)) == sizeof(PublicKey));
    TEST_OK(NT_SUCCESS(ZpMessage_GetModuleRecord(&ClientHelloView.Modules, 0, &Module)) &&
            Module.ModuleId == Modules[0].ModuleId &&
            Module.ModuleVersion == Modules[0].ModuleVersion &&
            Module.Capabilities == Modules[0].Capabilities);
    TEST_OK(NT_SUCCESS(ZpMessage_GetModuleRecord(&ClientHelloView.Modules, 1, &Module)) &&
            Module.ModuleId == Modules[1].ModuleId &&
            Module.ModuleVersion == Modules[1].ModuleVersion &&
            Module.Capabilities == Modules[1].Capabilities);
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

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeReady(&Ready, Buffer, sizeof(Buffer), &Length)) && Length == 18);
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeReady(Buffer, Length, &ReadyView)) &&
            ReadyView.Modules.Count == ARRAYSIZE(Modules));
    TEST_OK(NT_SUCCESS(ZpMessage_GetModuleRecord(&ReadyView.Modules, 1, &Module)) &&
            Module.ModuleId == Modules[1].ModuleId);

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeDisconnect(&Disconnect, Buffer, sizeof(Buffer), &Length)) && Length == 20);
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeDisconnect(Buffer, Length, &DisconnectView)) &&
            DisconnectView.Status == STATUS_ACCESS_DENIED &&
            DisconnectView.Reason.Length == Disconnect.ReasonLength &&
            RtlCompareMemory(DisconnectView.Reason.Buffer,
                             Disconnect.Reason,
                             Disconnect.ReasonLength * sizeof(WCHAR)) == Disconnect.ReasonLength * sizeof(WCHAR));
    TEST_OK(ZpMessage_DecodeDisconnect(Buffer, Length - 1, &DisconnectView) == STATUS_DATA_ERROR);
    Disconnect.ReasonLength = ZP_CODEC_MAX_ELEMENT_COUNT + 1;
    TEST_OK(ZpMessage_EncodeDisconnect(&Disconnect, NULL, 0, &Length) == STATUS_INVALID_PARAMETER);

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

    TEST_OK(NT_SUCCESS(ZpMessage_EncodeResponse(&Response, Buffer, sizeof(Buffer), &Length)) && Length == 14);
    TEST_OK(NT_SUCCESS(ZpMessage_DecodeResponse(Buffer, Length, &ResponseView)) &&
            ResponseView.RequestId == Response.RequestId &&
            ResponseView.Status == Response.Status &&
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
                                                    STATUS_CANCELLED,
                                                    Buffer,
                                                    sizeof(Buffer),
                                                    &Length)) &&
            NT_SUCCESS(ZpMessage_DecodeChannelClose(Buffer,
                                                    Length,
                                                    &ChannelClose)) &&
            ChannelClose.ChannelId == 9 &&
            ChannelClose.Status == STATUS_CANCELLED);
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
            Length == 50 &&
            NT_SUCCESS(ZpProcess_DecodeList(Buffer, Length, &ProcessList)) &&
            ProcessList.Count == ARRAYSIZE(Processes) &&
            NT_SUCCESS(ZpProcess_GetRecord(&ProcessList, 0, &ProcessRecord)) &&
            ProcessRecord.ProcessId == 0 &&
            ProcessRecord.ImageName.Length == 0 &&
            NT_SUCCESS(ZpProcess_GetRecord(&ProcessList, 1, &ProcessRecord)) &&
            ProcessRecord.ProcessId == Processes[1].ProcessId &&
            ProcessRecord.SessionId == Processes[1].SessionId &&
            ProcessRecord.ImageName.Length == Processes[1].ImageNameLength &&
            RtlCompareMemory(ProcessRecord.ImageName.Buffer,
                             Processes[1].ImageName,
                             Processes[1].ImageNameLength * sizeof(WCHAR)) ==
                Processes[1].ImageNameLength * sizeof(WCHAR));
    TEST_OK(ZpProcess_GetRecord(&ProcessList,
                                ProcessList.Count,
                                &ProcessRecord) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpProcess_EncodeQuery(ProcessInfo.ProcessId,
                                             Buffer,
                                             sizeof(Buffer),
                                             &Length)) &&
            NT_SUCCESS(ZpProcess_DecodeQuery(Buffer, Length, &Index)) &&
            Index == ProcessInfo.ProcessId);
    TEST_OK(NT_SUCCESS(ZpProcess_EncodeInfo(&ProcessInfo,
                                            Buffer,
                                            sizeof(Buffer),
                                            &Length)) &&
            Length == 86 &&
            NT_SUCCESS(ZpProcess_DecodeInfo(Buffer, Length, &ProcessInfoView)) &&
            ProcessInfoView.ProcessId == ProcessInfo.ProcessId &&
            ProcessInfoView.ParentProcessId == ProcessInfo.ParentProcessId &&
            ProcessInfoView.ThreadCount == ProcessInfo.ThreadCount &&
            ProcessInfoView.WorkingSetBytes == ProcessInfo.WorkingSetBytes &&
            ProcessInfoView.PrivateBytes == ProcessInfo.PrivateBytes &&
            ProcessInfoView.ImageName.Length == ProcessInfo.ImageNameLength);
    TEST_OK(NT_SUCCESS(ZpProcess_EncodeTerminate(ProcessInfo.ProcessId,
                                                 0x10203040,
                                                 Buffer,
                                                 sizeof(Buffer),
                                                 &Length)) &&
            Length == 2 * sizeof(ULONG) &&
            NT_SUCCESS(ZpProcess_DecodeTerminate(Buffer,
                                                 Length,
                                                 &Index,
                                                 &ExitCode)) &&
            Index == ProcessInfo.ProcessId &&
            ExitCode == 0x10203040);
    TEST_OK(ZpProcess_EncodeTerminate(0,
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
    TEST_OK(ZpFile_EncodeOpenReadResponse(3,
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
    TEST_OK(ZpTerminal_EncodeCreateResponse(3,
                                             4321,
                                             Buffer,
                                             sizeof(Buffer),
                                             &Length) == STATUS_INVALID_PARAMETER &&
            ZpTerminal_EncodeResize(2,
                                     0,
                                     25,
                                     Buffer,
                                     sizeof(Buffer),
                                     &Length) == STATUS_INVALID_PARAMETER);
}
