#include "UnitTest.h"

#include <KNSoft/ZPigeon/Protocol.h>
#include <KNSoft/ZPigeon/File.h>
#include <KNSoft/ZPigeon/Rtc.h>
#include <KNSoft/ZPigeon/Serial.h>
#include <KNSoft/ZPigeon/Recording.h>
#include <KNSoft/ZPigeon/PortableDevice.h>
#include <KNSoft/ZPigeon/Window.h>
#include <KNSoft/ZPigeon/Process.h>

TEST_FUNC(ProtocolProcess)
{
    const ZP_PROCESS_MEMORY_REGION Regions[] = {
        { 0x1000, 0x1000, 0x3000, 0x2000, 0x1800, 0x1000, 0x800, 0x1000, 0, 0x800,
          MEM_COMMIT, MEM_IMAGE, PAGE_EXECUTE_READ, PAGE_READONLY, 1, 5, STATUS_SUCCESS,
          STATUS_SUCCESS, STATUS_SUCCESS, L"\\Device\\Test", 12 },
        { 0x4000, 0, 0x1000, 0, 0, 0, 0, 0, 0, 0, MEM_FREE, 0, 0, 0, 0, 0,
          STATUS_NOT_FOUND, STATUS_NOT_FOUND, STATUS_SUCCESS, NULL, 0 }
    };
    ZP_PROCESS_MEMORY_MAP_VIEW Map;
    ZP_PROCESS_MEMORY_REGION_VIEW Region;
    BYTE Buffer[512];
    ULONG Length, Offset = 0;

    TEST_OK(NT_SUCCESS(ZpProcess_EncodeMemoryMap(Regions,
                                                ARRAYSIZE(Regions),
                                                Buffer,
                                                sizeof(Buffer),
                                                &Length)) &&
            NT_SUCCESS(ZpProcess_DecodeMemoryMap(Buffer, Length, &Map)) && Map.Count == 2 &&
            NT_SUCCESS(ZpProcess_ReadMemoryMapRegion(&Map, &Offset, &Region)) &&
            Region.BaseAddress == 0x1000 && Region.CommitSize == 0x2000 &&
            Region.WorkingSetBytes == 0x1800 && Region.Priority == 5 &&
            Region.MappedPath.Length == 12 &&
            NT_SUCCESS(ZpProcess_ReadMemoryMapRegion(&Map, &Offset, &Region)) && Region.State == MEM_FREE &&
            Offset == Map.Length);
}

TEST_FUNC(ProtocolWindow)
{
    const ZP_WINDOW_CAPTURE_OPTIONS Desktop = {
        0,
        0,
        0,
        ZP_WINDOW_CAPTURE_DESKTOP | ZP_WINDOW_CAPTURE_CURSOR,
        1280,
        12,
        85,
        7,
        ZP_WINDOW_CAPTURE_PRIMARY_MONITOR
    };
    const ZP_WINDOW_MONITOR Monitors[] = {
        { 0, MONITORINFOF_PRIMARY, -1920, 0, 0, 1080, -1920, 0, 0, 1040, L"DISPLAY1", 8 },
        { 1, 0, 0, 0, 1920, 1080, 0, 0, 1920, 1040, L"DISPLAY2", 8 }
    };
    ZP_WINDOW_MONITOR_LIST_VIEW MonitorList;
    ZP_WINDOW_MONITOR_VIEW Monitor;
    ZP_WINDOW_CAPTURE_OPTIONS Decoded, Invalid;
    BYTE Buffer[256];
    ULONG Length, Offset = 0;

    TEST_OK(NT_SUCCESS(ZpWindow_EncodeCaptureRequest(&Desktop, Buffer, sizeof(Buffer), &Length)) &&
            NT_SUCCESS(ZpWindow_DecodeCaptureRequest(Buffer, Length, &Decoded)) &&
            Decoded.Handle == 0 && Decoded.Flags == Desktop.Flags && Decoded.DirectStreamId == 7 &&
            Decoded.MonitorIndex == ZP_WINDOW_CAPTURE_PRIMARY_MONITOR);
    Invalid = Desktop;
    Invalid.Handle = 1;
    TEST_OK(ZpWindow_EncodeCaptureRequest(&Invalid, Buffer, sizeof(Buffer), &Length) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpWindow_EncodeMonitorList(Monitors,
                                                  ARRAYSIZE(Monitors),
                                                  Buffer,
                                                  sizeof(Buffer),
                                                  &Length)) &&
            NT_SUCCESS(ZpWindow_DecodeMonitorList(Buffer, Length, &MonitorList)) &&
            MonitorList.Count == ARRAYSIZE(Monitors) &&
            NT_SUCCESS(ZpWindow_GetNextMonitor(&MonitorList, &Offset, &Monitor)) &&
            NT_SUCCESS(ZpWindow_GetNextMonitor(&MonitorList, &Offset, &Monitor)) &&
            Monitor.Index == 1 && Monitor.Right == 1920 && Monitor.Device.Length == 8);
}

TEST_FUNC(ProtocolPortable)
{
    ZP_PORTABLE_DEVICE_RECORD Devices[] = {
        { L"id", 2, L"phone", 5, L"vendor", 6, L"model", 5 }
    };
    ZP_PORTABLE_OBJECT_RECORD Objects[] = {
        { 123, 456, 1000, 877, ZP_PORTABLE_OBJECT_STORAGE | ZP_PORTABLE_OBJECT_FOLDER,
          L"object", 6, L"persistent", 10, L"storage", 7 },
        { 789, 654, 0, 0, ZP_PORTABLE_OBJECT_CAN_DELETE, L"file", 4, NULL, 0, L"data.bin", 8 }
    };
    BYTE Buffer[256];
    ZP_PORTABLE_DEVICE_LIST_VIEW DeviceList;
    ZP_PORTABLE_DEVICE_RECORD_VIEW Device;
    ZP_PORTABLE_OBJECT_PAGE_VIEW ObjectPage;
    ZP_PORTABLE_OBJECT_RECORD_VIEW Object;
    ZP_PORTABLE_WRITE_REQUEST_VIEW Write;
    ULONG Length, Offset = 0;

    TEST_OK(NT_SUCCESS(ZpPortable_EncodeDeviceList(Devices, ARRAYSIZE(Devices), Buffer, sizeof(Buffer), &Length)) &&
            NT_SUCCESS(ZpPortable_DecodeDeviceList(Buffer, Length, &DeviceList)) && DeviceList.Count == 1 &&
            NT_SUCCESS(ZpPortable_GetNextDevice(&DeviceList, &Offset, &Device)) && Device.Id.Length == 2 &&
            Device.Name.Length == 5);
    Offset = 0;
    TEST_OK(NT_SUCCESS(ZpPortable_EncodeObjectPage(Objects,
                                                   ARRAYSIZE(Objects),
                                                   100,
                                                   Buffer,
                                                   sizeof(Buffer),
                                                   &Length)) &&
            NT_SUCCESS(ZpPortable_DecodeObjectPage(Buffer, Length, &ObjectPage)) && ObjectPage.Count == 2 &&
            ObjectPage.NextOffset == 100 &&
            NT_SUCCESS(ZpPortable_GetNextObject(&ObjectPage, &Offset, &Object)) && Object.Size == 0 &&
            Object.Capacity == 1000 && Object.Flags == Objects[0].Flags &&
            NT_SUCCESS(ZpPortable_GetNextObject(&ObjectPage, &Offset, &Object)) && Object.Size == 789 &&
            Object.Capacity == 0 && Object.Flags == Objects[1].Flags);
    TEST_OK(NT_SUCCESS(ZpPortable_EncodeWriteRequest(L"id", 2, L"DEVICE", 6, L"file", 4, 123,
                                                     Buffer, sizeof(Buffer), &Length)) &&
            NT_SUCCESS(ZpPortable_DecodeWriteRequest(Buffer, Length, &Write)) && Write.FileSize == 123 &&
            Write.DeviceId.Length == 2 && Write.ParentId.Length == 6 && Write.Name.Length == 4);
}

TEST_FUNC(ProtocolFileOwners)
{
    static const ZP_FILE_OWNER_RECORD Owners[] = {
        { 42, STATUS_SUCCESS, STATUS_ACCESS_DENIED, L"test.exe", 8, L"C:\\test.exe", 11,
          NULL, 0, L"SvcA\0SvcB", 9 }
    };
    static const ULONG ProcessIds[] = { 4, 42 };
    static const ZP_FILE_OWNER_CONTROL_RESULT Results[] = {
        { 4, STATUS_ACCESS_DENIED, 0 },
        { 42, STATUS_SUCCESS, 2 }
    };
    BYTE Buffer[256];
    ZP_FILE_OWNER_LIST_VIEW List;
    ZP_FILE_OWNER_RECORD_VIEW Owner;
    ZP_FILE_OWNER_CONTROL_REQUEST_VIEW Request;
    ZP_FILE_OWNER_CONTROL_RESULT_VIEW ResultView;
    ZP_FILE_OWNER_CONTROL_RESULT Result;
    ULONG Length, Offset = 0, ProcessId;

    TEST_OK(NT_SUCCESS(ZpFile_EncodeOwnerList(Owners, ARRAYSIZE(Owners), Buffer, sizeof(Buffer), &Length)) &&
            NT_SUCCESS(ZpFile_DecodeOwnerList(Buffer, Length, &List)) && List.Count == ARRAYSIZE(Owners) &&
            NT_SUCCESS(ZpFile_GetNextOwnerRecord(&List, &Offset, &Owner)) && Owner.ProcessId == 42 &&
            Owner.ImageName.Length == 8 && Owner.CommandLineStatus == STATUS_ACCESS_DENIED &&
            Owner.ServiceNames.Length == 9);
    TEST_OK(ZpFile_DecodeOwnerList(Buffer, Length - 1, &List) == STATUS_DATA_ERROR);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeOwnerControlRequest(L"C:\\test.exe",
                                                        11,
                                                        ZpFileOwnerCloseHandles,
                                                        ProcessIds,
                                                        ARRAYSIZE(ProcessIds),
                                                        Buffer,
                                                        sizeof(Buffer),
                                                        &Length)) &&
            NT_SUCCESS(ZpFile_DecodeOwnerControlRequest(Buffer, Length, &Request)) &&
            Request.Control == ZpFileOwnerCloseHandles && Request.ProcessCount == ARRAYSIZE(ProcessIds) &&
            NT_SUCCESS(ZpFile_GetOwnerControlProcessId(&Request, 1, &ProcessId)) && ProcessId == 42);
    TEST_OK(NT_SUCCESS(ZpFile_EncodeOwnerControlResults(Results,
                                                        ARRAYSIZE(Results),
                                                        Buffer,
                                                        sizeof(Buffer),
                                                        &Length)) &&
            NT_SUCCESS(ZpFile_DecodeOwnerControlResults(Buffer, Length, &ResultView)) &&
            ResultView.Count == ARRAYSIZE(Results) &&
            NT_SUCCESS(ZpFile_GetOwnerControlResult(&ResultView, 1, &Result)) && Result.ProcessId == 42 &&
            Result.Status == STATUS_SUCCESS && Result.AffectedHandleCount == 2);
}

TEST_FUNC(ProtocolCodec)
{
    static const BYTE ByteString[] = { 1, 2, 3 };
    BYTE Buffer[64], InvalidBoolean = 2, TruncatedByteString[] = { 3, 0, 0, 0, 1, 2 };
    BYTE ByteValue;
    USHORT UInt16Value;
    ULONG UInt32Value;
    ULONGLONG UInt64Value;
    BOOLEAN BooleanValue;
    ZP_BUFFER_VIEW BufferView;
    ZP_STRING_VIEW StringView;
    ZP_CODEC_WRITER Writer;
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeWriter(&Writer, NULL, 0);
    TEST_OK(NT_SUCCESS(ZpCodec_WriteByte(&Writer, 0x5A)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt16(&Writer, 0x1234)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt32(&Writer, 0x89ABCDEF)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt64(&Writer, 0x0123456789ABCDEFULL)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteBoolean(&Writer, TRUE)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteString(&Writer, L"AZ", 2)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteByteString(&Writer, ByteString, sizeof(ByteString))));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteArrayCount(&Writer, 4)));
    TEST_OK(Writer.Offset == 35);

    ZpCodec_InitializeWriter(&Writer, Buffer, sizeof(Buffer));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteByte(&Writer, 0x5A)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt16(&Writer, 0x1234)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt32(&Writer, 0x89ABCDEF)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt64(&Writer, 0x0123456789ABCDEFULL)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteBoolean(&Writer, TRUE)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteString(&Writer, L"AZ", 2)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteByteString(&Writer, ByteString, sizeof(ByteString))));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteArrayCount(&Writer, 4)));

    ZpCodec_InitializeReader(&Reader, Buffer, Writer.Offset);
    TEST_OK(NT_SUCCESS(ZpCodec_ReadByte(&Reader, &ByteValue)) && ByteValue == 0x5A);
    TEST_OK(NT_SUCCESS(ZpCodec_ReadUInt16(&Reader, &UInt16Value)) && UInt16Value == 0x1234);
    TEST_OK(NT_SUCCESS(ZpCodec_ReadUInt32(&Reader, &UInt32Value)) && UInt32Value == 0x89ABCDEF);
    TEST_OK(NT_SUCCESS(ZpCodec_ReadUInt64(&Reader, &UInt64Value)) && UInt64Value == 0x0123456789ABCDEFULL);
    TEST_OK(NT_SUCCESS(ZpCodec_ReadBoolean(&Reader, &BooleanValue)) && BooleanValue == TRUE);
    TEST_OK(NT_SUCCESS(ZpCodec_ReadString(&Reader, &StringView)) && StringView.Length == 2 &&
            StringView.Buffer[0] == L'A' && StringView.Buffer[1] == L'Z');
    TEST_OK(NT_SUCCESS(ZpCodec_ReadByteString(&Reader, &BufferView)) &&
            BufferView.Length == sizeof(ByteString) &&
            RtlCompareMemory(BufferView.Buffer, ByteString, sizeof(ByteString)) == sizeof(ByteString));
    TEST_OK(NT_SUCCESS(ZpCodec_ReadArrayCount(&Reader, &UInt32Value)) && UInt32Value == 4);
    TEST_OK(Reader.Offset == Reader.Size);

    ZpCodec_InitializeReader(&Reader, &InvalidBoolean, sizeof(InvalidBoolean));
    TEST_OK(ZpCodec_ReadBoolean(&Reader, &BooleanValue) == STATUS_DATA_ERROR);

    ZpCodec_InitializeReader(&Reader, TruncatedByteString, sizeof(TruncatedByteString));
    TEST_OK(ZpCodec_ReadByteString(&Reader, &BufferView) == STATUS_DATA_ERROR);

    ZpCodec_InitializeWriter(&Writer, Buffer, sizeof(ULONG));
    Status = ZpCodec_WriteByteString(&Writer, ByteString, sizeof(ByteString));
    TEST_OK(Status == STATUS_BUFFER_TOO_SMALL && Writer.Offset == 0);
    TEST_OK(ZpCodec_WriteBoolean(&Writer, 2) == STATUS_INVALID_PARAMETER);
    TEST_OK(ZpCodec_WriteArrayCount(&Writer, ZP_CODEC_MAX_ELEMENT_COUNT + 1) == STATUS_INVALID_BUFFER_SIZE);
}

TEST_FUNC(ProtocolFrame)
{
    BYTE PingBody[8], Frame[128], ClientHello[2 + ZP_CLIENT_PUBLIC_KEY_SIZE], InvalidFrame[5] = { 1, 0, 0, 0, 0xFF };
    ZP_CODEC_WRITER Writer;
    ZP_FRAME_VIEW View;
    ULONG FrameSize, BytesConsumed;
    NTSTATUS Status;

    ZpCodec_InitializeWriter(&Writer, PingBody, sizeof(PingBody));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteUInt64(&Writer, 0x0123456789ABCDEFULL)));

    TEST_OK(NT_SUCCESS(ZpFrame_GetSize(sizeof(PingBody), &FrameSize)) && FrameSize == 13);
    TEST_OK(ZpFrame_GetSize(ZP_FRAME_MAX_BODY_SIZE, &FrameSize) == STATUS_INVALID_BUFFER_SIZE);
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(ZpMessagePing, PingBody, sizeof(PingBody), NULL, 0, &FrameSize)) &&
            FrameSize == 13);
    TEST_OK(ZpFrame_Encode(ZpMessagePing,
                          PingBody,
                          sizeof(PingBody),
                          Frame,
                          FrameSize - 1,
                          &BytesConsumed) == STATUS_BUFFER_TOO_SMALL &&
            BytesConsumed == FrameSize);
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(ZpMessagePing,
                                     PingBody,
                                     sizeof(PingBody),
                                     Frame,
                                     sizeof(Frame),
                                     &BytesConsumed)) &&
            BytesConsumed == FrameSize);
    TEST_OK(ZpFrame_Decode(Frame, 3, &View, &BytesConsumed) == STATUS_MORE_PROCESSING_REQUIRED);
    TEST_OK(ZpFrame_Decode(Frame, FrameSize - 1, &View, &BytesConsumed) == STATUS_MORE_PROCESSING_REQUIRED);
    TEST_OK(NT_SUCCESS(ZpFrame_Decode(Frame, FrameSize, &View, &BytesConsumed)) &&
            BytesConsumed == FrameSize &&
            View.MessageType == ZpMessagePing &&
            View.BodyLength == sizeof(PingBody) &&
            RtlCompareMemory(View.Body, PingBody, sizeof(PingBody)) == sizeof(PingBody));

    TEST_OK(ZpFrame_Decode(InvalidFrame, sizeof(InvalidFrame), &View, &BytesConsumed) == STATUS_DATA_ERROR);
    InvalidFrame[0] = 0;
    TEST_OK(ZpFrame_Decode(InvalidFrame, sizeof(InvalidFrame), &View, &BytesConsumed) == STATUS_DATA_ERROR);

    ZpCodec_InitializeWriter(&Writer, ClientHello, sizeof(ClientHello));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteByte(&Writer, ZP_CORE_VERSION)));
    TEST_OK(NT_SUCCESS(ZpCodec_WriteByte(&Writer, 0)));
    ClientHello[Writer.Offset] = 0x04;
    RtlZeroMemory(ClientHello + Writer.Offset + 1, ZP_CLIENT_PUBLIC_KEY_SIZE - 1);
    TEST_OK(NT_SUCCESS(ZpFrame_Encode(ZpMessageClientHello,
                                     ClientHello,
                                     sizeof(ClientHello),
                                     Frame,
                                     sizeof(Frame),
                                     &FrameSize)));
    TEST_OK(NT_SUCCESS(ZpFrame_Decode(Frame, FrameSize, &View, &BytesConsumed)) &&
            View.MessageType == ZpMessageClientHello);

    ClientHello[0] = 2;
    Status = ZpFrame_Encode(ZpMessageClientHello,
                            ClientHello,
                            sizeof(ClientHello),
                            Frame,
                            sizeof(Frame),
                            &FrameSize);
    TEST_OK(Status == STATUS_REVISION_MISMATCH);
    ClientHello[0] = ZP_CORE_VERSION;
    ClientHello[2] = 0x03;
    TEST_OK(ZpFrame_Encode(ZpMessageClientHello,
                          ClientHello,
                          sizeof(ClientHello),
                          Frame,
                          sizeof(Frame),
                          &FrameSize) == STATUS_DATA_ERROR);
}

TEST_FUNC(ProtocolRtc)
{
    static const BYTE SessionId[ZP_RTC_SESSION_ID_SIZE] = { 1, 2, 3, 4 };
    static const ZP_RTC_ICE_SERVER Servers[] = { { L"stun:one", 8 }, { L"turn:two", 8 } };
    BYTE Buffer[256];
    ZP_RTC_OPEN_REQUEST_VIEW Request;
    ZP_STRING_VIEW String;
    const BYTE* DecodedSessionId;
    ULONG Length, Offset = 0;

    TEST_OK(NT_SUCCESS(ZpRtc_EncodeOpenRequest(SessionId,
                                               L"offer",
                                               5,
                                               Servers,
                                               ARRAYSIZE(Servers),
                                               Buffer,
                                               sizeof(Buffer),
                                               &Length)));
    TEST_OK(NT_SUCCESS(ZpRtc_DecodeOpenRequest(Buffer, Length, &Request)) &&
            Request.Offer.Length == 5 && Request.IceServerCount == ARRAYSIZE(Servers) &&
            RtlCompareMemory(Request.SessionId, SessionId, sizeof(SessionId)) == sizeof(SessionId));
    TEST_OK(NT_SUCCESS(ZpRtc_GetNextIceServer(&Request, &Offset, &String)) &&
            NT_SUCCESS(ZpRtc_GetNextIceServer(&Request, &Offset, &String)) &&
            String.Length == Servers[1].UrlLength &&
            RtlCompareMemory(String.Buffer, Servers[1].Url, String.Length * sizeof(WCHAR)) ==
                String.Length * sizeof(WCHAR));
    TEST_OK(ZpRtc_GetNextIceServer(&Request, &Offset, &String) == STATUS_INVALID_PARAMETER);
    TEST_OK(ZpRtc_DecodeOpenRequest(Buffer, Length - 1, &Request) == STATUS_DATA_ERROR);

    TEST_OK(NT_SUCCESS(ZpRtc_EncodeAnswer(L"answer", 6, Buffer, sizeof(Buffer), &Length)) &&
            NT_SUCCESS(ZpRtc_DecodeAnswer(Buffer, Length, &String)) && String.Length == 6);
    TEST_OK(NT_SUCCESS(ZpRtc_EncodeSessionId(SessionId, Buffer, sizeof(Buffer), &Length)) &&
            NT_SUCCESS(ZpRtc_DecodeSessionId(Buffer, Length, &DecodedSessionId)) &&
            RtlCompareMemory(DecodedSessionId, SessionId, sizeof(SessionId)) == sizeof(SessionId));
}

TEST_FUNC(ProtocolSerial)
{
    static const ZP_SERIAL_PORT Ports[] = {
        { L"COM3", 4, L"\\Device\\Serial0", 15 },
        { L"COM12", 5, L"\\Device\\VCP0", 12 }
    };
    BYTE Buffer[256];
    ZP_SERIAL_PORT_LIST_VIEW List;
    ZP_SERIAL_PORT_VIEW Port;
    ZP_SERIAL_OPEN_REQUEST_VIEW Request;
    ULONG Length, Offset = 0, ChannelId;

    TEST_OK(NT_SUCCESS(ZpSerial_EncodePortList(Ports,
                                               ARRAYSIZE(Ports),
                                               Buffer,
                                               sizeof(Buffer),
                                               &Length)));
    TEST_OK(NT_SUCCESS(ZpSerial_DecodePortList(Buffer, Length, &List)) &&
            List.Count == ARRAYSIZE(Ports));
    TEST_OK(NT_SUCCESS(ZpSerial_GetNextPort(&List, &Offset, &Port)) && Port.Name.Length == 4 &&
            RtlCompareMemory(Port.Name.Buffer, L"COM3", 4 * sizeof(WCHAR)) == 4 * sizeof(WCHAR));
    TEST_OK(NT_SUCCESS(ZpSerial_GetNextPort(&List, &Offset, &Port)) && Port.Device.Length == 12);
    TEST_OK(ZpSerial_DecodePortList(Buffer, Length - 1, &List) == STATUS_DATA_ERROR);

    TEST_OK(NT_SUCCESS(ZpSerial_EncodeOpenRequest(L"COM12",
                                                  5,
                                                  115200,
                                                  8,
                                                  ZP_SERIAL_PARITY_NONE,
                                                  ZP_SERIAL_STOP_BITS_ONE,
                                                  ZP_SERIAL_FLOW_RTS_CTS,
                                                  Buffer,
                                                  sizeof(Buffer),
                                                  &Length)));
    TEST_OK(NT_SUCCESS(ZpSerial_DecodeOpenRequest(Buffer, Length, &Request)) &&
            Request.BaudRate == 115200 && Request.DataBits == 8 &&
            Request.FlowControl == ZP_SERIAL_FLOW_RTS_CTS);
    TEST_OK(ZpSerial_EncodeOpenRequest(L"\\Device", 7, 115200, 8, 0, 0, 0,
                                      Buffer, sizeof(Buffer), &Length) == STATUS_INVALID_PARAMETER);
    TEST_OK(NT_SUCCESS(ZpSerial_EncodeChannel(7, Buffer, sizeof(Buffer), &Length)) &&
            NT_SUCCESS(ZpSerial_DecodeChannel(Buffer, Length, &ChannelId)) && ChannelId == 7);
}

TEST_FUNC(ProtocolRecording)
{
    const ZP_RECORDING_START Start = {
        ZpRecordingSourceCamera,
        ZpRecordingCodecH265,
        30,
        ZpRecordingAudioInput,
        0,
        1920,
        4000000,
        160000,
        0,
        L"camera-id",
        9,
        L"microphone-id",
        13
    };
    const ZP_RECORDING_RECORD Records[] = {
        { 7, ZpRecordingSourceCamera, ZpRecordingCodecH265, ZpRecordingStateRecording,
          { ZpStatusNone, STATUS_SUCCESS }, 123, 456, 789, L"C:\\recording.mp4", 16 }
    };
    ZP_RECORDING_START_VIEW StartView;
    ZP_RECORDING_LIST_VIEW List;
    ZP_RECORDING_RECORD_VIEW Record;
    BYTE Buffer[512];
    ULONG Length, Offset = 0, Value;

    TEST_OK(NT_SUCCESS(ZpRecording_EncodeStart(&Start, Buffer, sizeof(Buffer), &Length)) &&
            NT_SUCCESS(ZpRecording_DecodeStart(Buffer, Length, &StartView)) &&
            StartView.Source == Start.Source && StartView.Codec == Start.Codec &&
            StartView.AudioSource == Start.AudioSource && StartView.SourceId.Length == Start.SourceIdLength &&
            StartView.AudioDeviceId.Length == Start.AudioDeviceIdLength);
    TEST_OK(ZpRecording_DecodeStart(Buffer, Length - 1, &StartView) == STATUS_DATA_ERROR);
    TEST_OK(NT_SUCCESS(ZpRecording_EncodeRecords(Records, ARRAYSIZE(Records), Buffer, sizeof(Buffer), &Length)) &&
            NT_SUCCESS(ZpRecording_DecodeRecords(Buffer, Length, &List)) && List.Count == ARRAYSIZE(Records) &&
            NT_SUCCESS(ZpRecording_GetNextRecord(&List, &Offset, &Record)) && Record.RecordingId == 7 &&
            Record.Path.Length == Records[0].PathLength);
    TEST_OK(NT_SUCCESS(ZpRecording_EncodeCapabilities(0x1F, Buffer, sizeof(Buffer), &Length)) &&
            NT_SUCCESS(ZpRecording_DecodeCapabilities(Buffer, Length, &Value)) && Value == 0x1F);
    TEST_OK(NT_SUCCESS(ZpRecording_EncodeId(7, Buffer, sizeof(Buffer), &Length)) &&
            NT_SUCCESS(ZpRecording_DecodeId(Buffer, Length, &Value)) && Value == 7);
}
