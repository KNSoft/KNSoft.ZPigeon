#include "Include/KNSoft/ZPigeon/Process.h"

static
NTSTATUS
ZpProcess_ReadRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_PROCESS_RECORD_VIEW Record)
{
    ZP_PROCESS_RECORD_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt32(Reader, &Local.ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.ParentProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.SessionId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.ThreadCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.HandleCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(Reader, &Local.MachineType);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(Reader, &Local.PriorityClass);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.UserTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.KernelTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.WorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.PrivateBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.ImageName);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.UserName);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.ImagePath);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.ServiceNames);
    if (NT_SUCCESS(Status) && Record != NULL) *Record = Local;
    return Status;
}

static
NTSTATUS
ZpProcess_WriteRecord(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ PCZP_PROCESS_RECORD Record)
{
    NTSTATUS Status;

    Status = ZpCodec_WriteUInt32(Writer, Record->ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->ParentProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->SessionId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->ThreadCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->HandleCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(Writer, Record->MachineType);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(Writer, Record->PriorityClass);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(Writer, Record->CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(Writer, Record->UserTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(Writer, Record->KernelTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(Writer, Record->WorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(Writer, Record->PrivateBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(Writer, Record->ImageName, Record->ImageNameLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(Writer, Record->UserName, Record->UserNameLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(Writer, Record->ImagePath, Record->ImagePathLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(Writer, Record->ServiceNames, Record->ServiceNamesLength);
    return Status;
}

NTSTATUS
ZpProcess_EncodeList(
    _In_reads_opt_(ProcessCount) PCZP_PROCESS_RECORD Processes,
    _In_ ULONG ProcessCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize = sizeof(ULONG);
    NTSTATUS Status;
    ULONG Index;

    if (ProcessCount > ZP_CODEC_MAX_ELEMENT_COUNT || (ProcessCount != 0 && Processes == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < ProcessCount; Index++)
    {
        if (Processes[Index].ImageNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            Processes[Index].UserNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            Processes[Index].ImagePathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            Processes[Index].ServiceNamesLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            (Processes[Index].ImageNameLength != 0 && Processes[Index].ImageName == NULL) ||
            (Processes[Index].UserNameLength != 0 && Processes[Index].UserName == NULL) ||
            (Processes[Index].ImagePathLength != 0 && Processes[Index].ImagePath == NULL) ||
            (Processes[Index].ServiceNamesLength != 0 && Processes[Index].ServiceNames == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 9 * sizeof(ULONG) + 2 * sizeof(USHORT) + 5 * sizeof(ULONGLONG) +
                        ((ULONGLONG)Processes[Index].ImageNameLength + Processes[Index].UserNameLength +
                         Processes[Index].ImagePathLength) * sizeof(WCHAR);
        RequiredSize += sizeof(ULONG) + (ULONGLONG)Processes[Index].ServiceNamesLength * sizeof(WCHAR);
        if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12) return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteArrayCount(&Writer, ProcessCount);
    for (Index = 0; NT_SUCCESS(Status) && Index < ProcessCount; Index++)
    {
        Status = ZpProcess_WriteRecord(&Writer, &Processes[Index]);
    }
    return Status;
}

NTSTATUS
ZpProcess_DecodeList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PROCESS_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;
    ULONG Count, Index;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++) Status = ZpProcess_ReadRecord(&Reader, NULL);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Buffer = Add2Ptr(Payload, sizeof(ULONG));
    View->Length = PayloadLength - sizeof(ULONG);
    View->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpProcess_GetRecord(
    _In_ PCZP_PROCESS_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_PROCESS_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG CurrentIndex;

    if (Index >= List->Count) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, List->Buffer, List->Length);
    for (CurrentIndex = 0; NT_SUCCESS(Status) && CurrentIndex <= Index; CurrentIndex++)
    {
        Status = ZpProcess_ReadRecord(&Reader, CurrentIndex == Index ? Record : NULL);
    }
    return Status;
}

NTSTATUS
ZpProcess_EncodeQuery(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (ProcessId == 0 || CreateTime == 0) return STATUS_INVALID_PARAMETER;
    *BytesWritten = sizeof(ProcessId) + sizeof(CreateTime);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, ProcessId);
    return NT_SUCCESS(Status) ? ZpCodec_WriteUInt64(&Writer, CreateTime) : Status;
}

NTSTATUS
ZpProcess_DecodeQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ProcessId,
    _Out_ PULONGLONG CreateTime)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != sizeof(*ProcessId) + sizeof(*CreateTime)) return STATUS_DATA_ERROR;
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, CreateTime);
    return NT_SUCCESS(Status) && (*ProcessId == 0 || *CreateTime == 0) ? STATUS_DATA_ERROR : Status;
}

NTSTATUS
ZpProcess_EncodeInfo(
    _In_ PCZP_PROCESS_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Info->ImageNameLength > ZP_CODEC_MAX_ELEMENT_COUNT || Info->UserNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->ImagePathLength > ZP_CODEC_MAX_ELEMENT_COUNT || Info->CommandLineLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (Info->ImageNameLength != 0 && Info->ImageName == NULL) ||
        (Info->UserNameLength != 0 && Info->UserName == NULL) ||
        (Info->ImagePathLength != 0 && Info->ImagePath == NULL) ||
        (Info->CommandLineLength != 0 && Info->CommandLine == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 12 * sizeof(ULONG) + 2 * sizeof(USHORT) + 5 * sizeof(ULONGLONG) +
                   ((ULONGLONG)Info->ImageNameLength + Info->UserNameLength + Info->ImagePathLength +
                    Info->CommandLineLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, Info->ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->ParentProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->SessionId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->ThreadCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->HandleCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, Info->MachineType);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, Info->PriorityClass);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->UserTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->KernelTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->WorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->PrivateBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Info->ImageName, Info->ImageNameLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Info->UserName, Info->UserNameLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->ImagePathStatus);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Info->ImagePath, Info->ImagePathLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->CommandLineStatus);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Info->CommandLine, Info->CommandLineLength);
    return Status;
}

NTSTATUS
ZpProcess_DecodeInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PROCESS_INFO_VIEW View)
{
    ZP_CODEC_READER Reader;
    ULONG Value;
    NTSTATUS Status;

    if (PayloadLength < 12 * sizeof(ULONG) + 2 * sizeof(USHORT) + 5 * sizeof(ULONGLONG))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &View->ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->ParentProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->SessionId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->ThreadCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->HandleCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &View->MachineType);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &View->PriorityClass);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->UserTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->KernelTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->WorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->PrivateBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &View->ImageName);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &View->UserName);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Value);
    if (NT_SUCCESS(Status)) View->ImagePathStatus = (NTSTATUS)Value;
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &View->ImagePath);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Value);
    if (NT_SUCCESS(Status)) View->CommandLineStatus = (NTSTATUS)Value;
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &View->CommandLine);
    return NT_SUCCESS(Status) && Reader.Offset != PayloadLength ? STATUS_DATA_ERROR : Status;
}

NTSTATUS
ZpProcess_EncodeControl(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ZP_PROCESS_CONTROL Control,
    _In_ ULONG Value,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (ProcessId == 0 || CreateTime == 0 || Control < ZpProcessControlTerminate ||
        Control > ZpProcessControlUacVirtualization)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = 2 * sizeof(ULONG) + sizeof(ULONGLONG) + sizeof(USHORT);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, Control);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Value);
    return Status;
}

NTSTATUS
ZpProcess_DecodeControl(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ProcessId,
    _Out_ PULONGLONG CreateTime,
    _Out_ ZP_PROCESS_CONTROL* Control,
    _Out_ PULONG Value)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != 2 * sizeof(ULONG) + sizeof(ULONGLONG) + sizeof(USHORT)) return STATUS_DATA_ERROR;
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, Control);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, Value);
    if (NT_SUCCESS(Status) && (*ProcessId == 0 || *CreateTime == 0 || *Control < ZpProcessControlTerminate ||
                              *Control > ZpProcessControlUacVirtualization))
    {
        Status = STATUS_DATA_ERROR;
    }
    return Status;
}

NTSTATUS
ZpProcess_EncodeDump(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONG DumpType,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (ProcessId == 0 || CreateTime == 0) return STATUS_INVALID_PARAMETER;
    *BytesWritten = 2 * sizeof(ULONG) + sizeof(ULONGLONG);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, DumpType);
    return Status;
}

NTSTATUS
ZpProcess_DecodeDump(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ProcessId,
    _Out_ PULONGLONG CreateTime,
    _Out_ PULONG DumpType)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != 2 * sizeof(ULONG) + sizeof(ULONGLONG)) return STATUS_DATA_ERROR;
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, DumpType);
    return NT_SUCCESS(Status) && (*ProcessId == 0 || *CreateTime == 0) ? STATUS_DATA_ERROR : Status;
}

NTSTATUS
ZpProcess_EncodeDumpPath(
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;

    if (PathLength == 0 || PathLength > ZP_CODEC_MAX_ELEMENT_COUNT || Path == NULL) return STATUS_INVALID_PARAMETER;
    *BytesWritten = sizeof(ULONG) + PathLength * sizeof(WCHAR);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteString(&Writer, Path, PathLength);
}

NTSTATUS
ZpProcess_DecodeDumpPath(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Path)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, Path);
    return NT_SUCCESS(Status) && (Path->Length == 0 || Reader.Offset != PayloadLength) ? STATUS_DATA_ERROR : Status;
}
