#include "UnitTest.h"

#include <KNSoft/ZPigeon/Protocol.h>
#include <KNSoft/ZPigeon/Process.h>
#include <KNSoft/ZPigeon/System.h>

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
    ZP_MODULE_RECORD Module;
    ZP_BUFFER_VIEW BufferView;
    ULONGLONG Value;
    ULONG Length, Index;

    for (Index = 0; Index < ARRAYSIZE(Challenge); Index++)
    {
        Challenge[Index] = (BYTE)Index;
    }
    for (Index = 0; Index < ARRAYSIZE(Signature); Index++)
    {
        Signature[Index] = (BYTE)(ARRAYSIZE(Signature) - Index);
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
}
