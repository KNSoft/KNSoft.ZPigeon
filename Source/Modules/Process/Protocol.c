#include "Include/KNSoft/ZPigeon/Process.h"

#include "../../KNSoft.ZPigeon.Protocol/Core/Protocol.inl"

static
NTSTATUS
ZpProcess_ReadRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_PROCESS_RECORD_VIEW Record)
{
    ZP_PROCESS_RECORD_VIEW Local;
    BYTE Flags;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt32(Reader, &Local.ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.ParentProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.SessionId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.ThreadCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.HandleCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(Reader, &Flags);
    if (NT_SUCCESS(Status)) Local.Flags = Flags;
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(Reader, &Local.MachineType);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(Reader, &Local.PriorityClass);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.UserTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.KernelTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.WorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.PrivateBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.ImageName);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.UserName);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.ImagePath);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.ServiceNames);
    if (NT_SUCCESS(Status) && (Local.Flags & ~ZP_PROCESS_FLAGS_MASK) != 0)
    {
        Status = STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Record != NULL) *Record = Local;
    return Status;
}

static
VOID
ZpProcess_WriteRecord(
    _Inout_ PBYTE* Cursor,
    _In_ PCZP_PROCESS_RECORD Record)
{
    ZpWire_WriteUInt32(Cursor, Record->ProcessId);
    ZpWire_WriteUInt32(Cursor, Record->ParentProcessId);
    ZpWire_WriteUInt32(Cursor, Record->SessionId);
    ZpWire_WriteUInt32(Cursor, Record->ThreadCount);
    ZpWire_WriteUInt32(Cursor, Record->HandleCount);
    ZpWire_WriteByte(Cursor, (BYTE)Record->Flags);
    ZpWire_WriteUInt16(Cursor, Record->MachineType);
    ZpWire_WriteByte(Cursor, Record->PriorityClass);
    ZpWire_WriteUInt64(Cursor, Record->CreateTime);
    ZpWire_WriteUInt64(Cursor, Record->UserTime);
    ZpWire_WriteUInt64(Cursor, Record->KernelTime);
    ZpWire_WriteUInt64(Cursor, Record->WorkingSetBytes);
    ZpWire_WriteUInt64(Cursor, Record->PrivateBytes);
    ZpWire_WriteString(Cursor, Record->ImageName, Record->ImageNameLength);
    ZpWire_WriteString(Cursor, Record->UserName, Record->UserNameLength);
    ZpWire_WriteString(Cursor, Record->ImagePath, Record->ImagePathLength);
    ZpWire_WriteString(Cursor, Record->ServiceNames, Record->ServiceNamesLength);
}

NTSTATUS
ZpProcess_EncodeList(
    _In_reads_opt_(ProcessCount) PCZP_PROCESS_RECORD Processes,
    _In_ ULONG ProcessCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = sizeof(ULONG);
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
            (Processes[Index].Flags & ~ZP_PROCESS_FLAGS_MASK) != 0 ||
            (Processes[Index].ImageNameLength != 0 && Processes[Index].ImageName == NULL) ||
            (Processes[Index].UserNameLength != 0 && Processes[Index].UserName == NULL) ||
            (Processes[Index].ImagePathLength != 0 && Processes[Index].ImagePath == NULL) ||
            (Processes[Index].ServiceNamesLength != 0 && Processes[Index].ServiceNames == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 8 * sizeof(ULONG) + sizeof(USHORT) + 2 * sizeof(BYTE) + 5 * sizeof(ULONGLONG) +
                        ((ULONGLONG)Processes[Index].ImageNameLength + Processes[Index].UserNameLength +
                         Processes[Index].ImagePathLength) * sizeof(WCHAR);
        RequiredSize += sizeof(ULONG) + (ULONGLONG)Processes[Index].ServiceNamesLength * sizeof(WCHAR);
        if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, ProcessCount);
    for (Index = 0; Index < ProcessCount; Index++)
    {
        ZpProcess_WriteRecord(&Cursor, &Processes[Index]);
    }
    return STATUS_SUCCESS;
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
ZpProcess_GetNextRecord(
    _In_ PCZP_PROCESS_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_PROCESS_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpProcess_ReadRecord(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
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
        (Info->CommandLineLength != 0 && Info->CommandLine == NULL) ||
        (Info->Flags & ~ZP_PROCESS_FLAGS_MASK) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 11 * sizeof(ULONG) + sizeof(USHORT) + 2 * sizeof(BYTE) + 6 * sizeof(ULONGLONG) +
                   ((ULONGLONG)Info->ImageNameLength + Info->UserNameLength + Info->ImagePathLength +
                    Info->CommandLineLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, Info->ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->ParentProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->SessionId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->ThreadCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->HandleCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, (BYTE)Info->Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, Info->MachineType);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, Info->PriorityClass);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->UserTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->KernelTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->WorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->PrivateBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->ImageBaseStatus);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->ImageBase);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Info->ImageName, Info->ImageNameLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Info->UserName, Info->UserNameLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->ImagePathStatus);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Info->ImagePath, Info->ImagePathLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->CommandLineStatus);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteTailString(&Writer, Info->CommandLine, Info->CommandLineLength);
    }
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
    BYTE Flags;
    NTSTATUS Status;

    if (PayloadLength < 11 * sizeof(ULONG) + sizeof(USHORT) + 2 * sizeof(BYTE) + 6 * sizeof(ULONGLONG))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &View->ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->ParentProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->SessionId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->ThreadCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->HandleCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Flags);
    if (NT_SUCCESS(Status)) View->Flags = Flags;
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &View->MachineType);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &View->PriorityClass);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->UserTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->KernelTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->WorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->PrivateBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Value);
    if (NT_SUCCESS(Status)) View->ImageBaseStatus = (NTSTATUS)Value;
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->ImageBase);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &View->ImageName);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &View->UserName);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Value);
    if (NT_SUCCESS(Status)) View->ImagePathStatus = (NTSTATUS)Value;
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &View->ImagePath);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Value);
    if (NT_SUCCESS(Status)) View->CommandLineStatus = (NTSTATUS)Value;
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadTailString(&Reader, &View->CommandLine);
    if (NT_SUCCESS(Status) && (View->Flags & ~ZP_PROCESS_FLAGS_MASK) != 0)
    {
        Status = STATUS_DATA_ERROR;
    }
    return NT_SUCCESS(Status) && Reader.Offset != PayloadLength ? STATUS_DATA_ERROR : Status;
}

static
NTSTATUS
ZpProcess_ReadModule(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_PROCESS_MODULE_RECORD_VIEW Module)
{
    ZP_PROCESS_MODULE_RECORD_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt64(Reader, &Local.BaseAddress);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.EntryPoint);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.LoadTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.SizeOfImage);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.LoadReason);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Path);
    if (NT_SUCCESS(Status) && Module != NULL) *Module = Local;
    return Status;
}

NTSTATUS
ZpProcess_EncodeModuleList(
    _In_ USHORT MachineType,
    _In_ BYTE MachineBits,
    _In_reads_opt_(ModuleCount) PCZP_PROCESS_MODULE_RECORD Modules,
    _In_ ULONG ModuleCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = sizeof(BYTE) + sizeof(USHORT) + sizeof(ULONG);
    ULONG Index;

    if (MachineType == IMAGE_FILE_MACHINE_UNKNOWN || (MachineBits != 32 && MachineBits != 64) ||
        ModuleCount > ZP_CODEC_MAX_ELEMENT_COUNT || (ModuleCount != 0 && Modules == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < ModuleCount; Index++)
    {
        if (Modules[Index].PathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            (Modules[Index].PathLength != 0 && Modules[Index].Path == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 3 * sizeof(ULONGLONG) + 3 * sizeof(ULONG) +
                        (ULONGLONG)Modules[Index].PathLength * sizeof(WCHAR);
        if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteByte(&Cursor, MachineBits);
    ZpWire_WriteUInt16(&Cursor, MachineType);
    ZpWire_WriteUInt32(&Cursor, ModuleCount);
    for (Index = 0; Index < ModuleCount; Index++)
    {
        ZpWire_WriteUInt64(&Cursor, Modules[Index].BaseAddress);
        ZpWire_WriteUInt64(&Cursor, Modules[Index].EntryPoint);
        ZpWire_WriteUInt64(&Cursor, Modules[Index].LoadTime);
        ZpWire_WriteUInt32(&Cursor, Modules[Index].SizeOfImage);
        ZpWire_WriteUInt32(&Cursor, Modules[Index].LoadReason);
        ZpWire_WriteString(&Cursor, Modules[Index].Path, Modules[Index].PathLength);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpProcess_DecodeModuleList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PROCESS_MODULE_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;
    ULONG Count, Index;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &View->MachineBits);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &View->MachineType);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    View->Buffer = Add2Ptr(Payload, Reader.Offset);
    View->Length = PayloadLength - Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpProcess_ReadModule(&Reader, NULL);
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength ||
        View->MachineType == IMAGE_FILE_MACHINE_UNKNOWN ||
        (View->MachineBits != 32 && View->MachineBits != 64))
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpProcess_GetNextModule(
    _In_ PCZP_PROCESS_MODULE_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_PROCESS_MODULE_RECORD_VIEW Module)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpProcess_ReadModule(&Reader, Module);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

static
NTSTATUS
ZpProcess_ReadHandle(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_PROCESS_HANDLE_RECORD_VIEW Handle)
{
    ZP_PROCESS_HANDLE_RECORD_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt64(Reader, &Local.HandleValue);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.TypeName);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.ObjectName);
    if (NT_SUCCESS(Status) && Handle != NULL) *Handle = Local;
    return Status;
}

NTSTATUS
ZpProcess_EncodeHandleList(
    _In_reads_opt_(HandleCount) PCZP_PROCESS_HANDLE_RECORD Handles,
    _In_ ULONG HandleCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = sizeof(ULONG);
    ULONG Index;

    if (HandleCount > ZP_CODEC_MAX_ELEMENT_COUNT || (HandleCount != 0 && Handles == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < HandleCount; Index++)
    {
        if (Handles[Index].TypeNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            Handles[Index].ObjectNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            (Handles[Index].TypeNameLength != 0 && Handles[Index].TypeName == NULL) ||
            (Handles[Index].ObjectNameLength != 0 && Handles[Index].ObjectName == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += sizeof(ULONGLONG) + 2 * sizeof(ULONG) +
                        ((ULONGLONG)Handles[Index].TypeNameLength + Handles[Index].ObjectNameLength) * sizeof(WCHAR);
        if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, HandleCount);
    for (Index = 0; Index < HandleCount; Index++)
    {
        ZpWire_WriteUInt64(&Cursor, Handles[Index].HandleValue);
        ZpWire_WriteString(&Cursor, Handles[Index].TypeName, Handles[Index].TypeNameLength);
        ZpWire_WriteString(&Cursor, Handles[Index].ObjectName, Handles[Index].ObjectNameLength);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpProcess_DecodeHandleList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PROCESS_HANDLE_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;
    ULONG Count, Index;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    View->Buffer = Add2Ptr(Payload, Reader.Offset);
    View->Length = PayloadLength - Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++) Status = ZpProcess_ReadHandle(&Reader, NULL);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpProcess_GetNextHandle(
    _In_ PCZP_PROCESS_HANDLE_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_PROCESS_HANDLE_RECORD_VIEW Handle)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpProcess_ReadHandle(&Reader, Handle);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
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
    *BytesWritten = 2 * sizeof(ULONG) + sizeof(ULONGLONG) + sizeof(BYTE);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, Control);
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

    if (PayloadLength != 2 * sizeof(ULONG) + sizeof(ULONGLONG) + sizeof(BYTE)) return STATUS_DATA_ERROR;
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, Control);
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
    *BytesWritten = PathLength * sizeof(WCHAR);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteTailString(&Writer, Path, PathLength);
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
    Status = ZpCodec_ReadTailString(&Reader, Path);
    return NT_SUCCESS(Status) && (Path->Length == 0 || Reader.Offset != PayloadLength) ? STATUS_DATA_ERROR : Status;
}

NTSTATUS
ZpProcess_EncodeMemoryRead(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONGLONG Address,
    _In_ ULONG Length,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (ProcessId == 0 || CreateTime == 0 || Length == 0 || Length > ZP_PROCESS_MEMORY_MAX_LENGTH)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = 2 * sizeof(ULONGLONG) + sizeof(ULONG) + sizeof(USHORT);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Address);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, (USHORT)(Length - 1));
    return Status;
}

NTSTATUS
ZpProcess_DecodeMemoryRead(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ProcessId,
    _Out_ PULONGLONG CreateTime,
    _Out_ PULONGLONG Address,
    _Out_ PULONG Length)
{
    ZP_CODEC_READER Reader;
    USHORT EncodedLength;
    NTSTATUS Status;

    if (PayloadLength != 2 * sizeof(ULONGLONG) + sizeof(ULONG) + sizeof(USHORT)) return STATUS_DATA_ERROR;
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, Address);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &EncodedLength);
    if (NT_SUCCESS(Status)) *Length = (ULONG)EncodedLength + 1;
    if (NT_SUCCESS(Status) && (*ProcessId == 0 || *CreateTime == 0))
    {
        Status = STATUS_DATA_ERROR;
    }
    return Status;
}

NTSTATUS
ZpProcess_EncodeMemoryWrite(
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONGLONG Address,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (ProcessId == 0 || CreateTime == 0 || Data == NULL || DataLength == 0 ||
        DataLength > ZP_PROCESS_MEMORY_MAX_LENGTH)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = 2 * sizeof(ULONGLONG) + sizeof(ULONG) + DataLength;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Address);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteData(&Writer, Data, DataLength);
    return Status;
}

NTSTATUS
ZpProcess_DecodeMemoryWrite(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PROCESS_MEMORY_VIEW Memory)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &Memory->ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Memory->CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Memory->Address);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadData(&Reader, Reader.Size - Reader.Offset, &Memory->Data);
    }
    if (!NT_SUCCESS(Status) || Memory->ProcessId == 0 || Memory->CreateTime == 0 ||
        Memory->Data.Length == 0 || Memory->Data.Length > ZP_PROCESS_MEMORY_MAX_LENGTH ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpProcess_EncodeMemoryData(
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;

    if (Data == NULL || DataLength == 0 || DataLength > ZP_PROCESS_MEMORY_MAX_LENGTH)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *BytesWritten = DataLength;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteData(&Writer, Data, DataLength);
}

NTSTATUS
ZpProcess_DecodeMemoryData(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_BUFFER_VIEW Data)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadData(&Reader, Reader.Size - Reader.Offset, Data);
    return NT_SUCCESS(Status) &&
           (Data->Length == 0 || Data->Length > ZP_PROCESS_MEMORY_MAX_LENGTH || Reader.Offset != PayloadLength) ?
               STATUS_DATA_ERROR : Status;
}

NTSTATUS
ZpProcess_EncodeMemoryMapQuery(
    _In_ ULONG SnapshotId,
    _In_ ULONG AllocationIndex,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor = Buffer;

    if (SnapshotId == 0) return STATUS_INVALID_PARAMETER;
    *BytesWritten = 2 * sizeof(ULONG);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpWire_WriteUInt32(&Cursor, SnapshotId);
    ZpWire_WriteUInt32(&Cursor, AllocationIndex);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpProcess_DecodeMemoryMapQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG SnapshotId,
    _Out_ PULONG AllocationIndex)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, SnapshotId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, AllocationIndex);
    return NT_SUCCESS(Status) && (Reader.Offset != PayloadLength || *SnapshotId == 0) ?
               STATUS_DATA_ERROR : Status;
}

NTSTATUS
ZpProcess_EncodeMemoryMapClose(
    _In_ ULONG SnapshotId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor = Buffer;

    if (SnapshotId == 0) return STATUS_INVALID_PARAMETER;
    *BytesWritten = sizeof(ULONG);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpWire_WriteUInt32(&Cursor, SnapshotId);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpProcess_DecodeMemoryMapClose(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG SnapshotId)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, SnapshotId);
    return NT_SUCCESS(Status) && (Reader.Offset != PayloadLength || *SnapshotId == 0) ?
               STATUS_DATA_ERROR : Status;
}

static
VOID
ZpProcess_WriteMemoryAllocation(
    _Inout_ PBYTE* Cursor,
    _In_ PCZP_PROCESS_MEMORY_ALLOCATION Allocation)
{
    ZpWire_WriteUInt64(Cursor, Allocation->AllocationBase);
    ZpWire_WriteUInt64(Cursor, Allocation->RegionSize);
    ZpWire_WriteUInt64(Cursor, Allocation->CommitSize);
    ZpWire_WriteUInt64(Cursor, Allocation->WorkingSetBytes);
    ZpWire_WriteUInt64(Cursor, Allocation->PrivateWorkingSetBytes);
    ZpWire_WriteUInt64(Cursor, Allocation->SharedWorkingSetBytes);
    ZpWire_WriteUInt64(Cursor, Allocation->ShareableWorkingSetBytes);
    ZpWire_WriteUInt64(Cursor, Allocation->LockedWorkingSetBytes);
    ZpWire_WriteUInt64(Cursor, Allocation->SharedOriginalBytes);
    ZpWire_WriteUInt32(Cursor, Allocation->Type);
    ZpWire_WriteUInt32(Cursor, Allocation->AllocationProtect);
    ZpWire_WriteUInt32(Cursor, Allocation->RegionType);
    ZpWire_WriteUInt32(Cursor, Allocation->Priority);
    ZpWire_WriteUInt32(Cursor, Allocation->RegionCount);
    ZpWire_WriteUInt32(Cursor, (ULONG)Allocation->RegionStatus);
    ZpWire_WriteUInt32(Cursor, (ULONG)Allocation->WorkingSetStatus);
    ZpWire_WriteUInt32(Cursor, (ULONG)Allocation->MappedPathStatus);
    ZpWire_WriteString(Cursor, Allocation->MappedPath, Allocation->MappedPathLength);
}

static
NTSTATUS
ZpProcess_ReadMemoryAllocationInternal(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_PROCESS_MEMORY_ALLOCATION_VIEW Allocation)
{
    ZP_PROCESS_MEMORY_ALLOCATION_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt64(Reader, &Local.AllocationBase);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.RegionSize);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.CommitSize);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.WorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.PrivateWorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.SharedWorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.ShareableWorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.LockedWorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.SharedOriginalBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Type);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.AllocationProtect);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.RegionType);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Priority);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.RegionCount);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, (PULONG)&Local.RegionStatus);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, (PULONG)&Local.WorkingSetStatus);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, (PULONG)&Local.MappedPathStatus);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.MappedPath);
    if (NT_SUCCESS(Status) && Allocation != NULL) *Allocation = Local;
    return Status;
}

NTSTATUS
ZpProcess_EncodeMemoryAllocations(
    _In_ ULONG SnapshotId,
    _In_reads_opt_(AllocationCount) PCZP_PROCESS_MEMORY_ALLOCATION Allocations,
    _In_ ULONG AllocationCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = 2 * sizeof(ULONG);
    ULONG Index;

    if (SnapshotId == 0 || AllocationCount > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (AllocationCount != 0 && Allocations == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < AllocationCount; Index++)
    {
        if (Allocations[Index].MappedPathLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            (Allocations[Index].MappedPathLength != 0 && Allocations[Index].MappedPath == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 9 * sizeof(ULONGLONG) + 9 * sizeof(ULONG) +
                        (ULONGLONG)Allocations[Index].MappedPathLength * sizeof(WCHAR);
        if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, SnapshotId);
    ZpWire_WriteUInt32(&Cursor, AllocationCount);
    for (Index = 0; Index < AllocationCount; Index++)
    {
        ZpProcess_WriteMemoryAllocation(&Cursor, &Allocations[Index]);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpProcess_DecodeMemoryAllocations(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PROCESS_MEMORY_ALLOCATION_MAP_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;
    ULONG Count, Index;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &View->SnapshotId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    View->Buffer = Add2Ptr(Payload, Reader.Offset);
    View->Length = PayloadLength - Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpProcess_ReadMemoryAllocationInternal(&Reader, NULL);
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength || View->SnapshotId == 0)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpProcess_ReadMemoryAllocation(
    _In_ PCZP_PROCESS_MEMORY_ALLOCATION_MAP_VIEW Map,
    _Inout_ PULONG Offset,
    _Out_ PZP_PROCESS_MEMORY_ALLOCATION_VIEW Allocation)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= Map->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(Map->Buffer, *Offset), Map->Length - *Offset);
    Status = ZpProcess_ReadMemoryAllocationInternal(&Reader, Allocation);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

static
VOID
ZpProcess_WriteMemoryRegion(
    _Inout_ PBYTE* Cursor,
    _In_ PCZP_PROCESS_MEMORY_REGION Region)
{
    ZpWire_WriteUInt64(Cursor, Region->BaseAddress);
    ZpWire_WriteUInt64(Cursor, Region->RegionSize);
    ZpWire_WriteUInt64(Cursor, Region->CommitSize);
    ZpWire_WriteUInt64(Cursor, Region->WorkingSetBytes);
    ZpWire_WriteUInt64(Cursor, Region->PrivateWorkingSetBytes);
    ZpWire_WriteUInt64(Cursor, Region->SharedWorkingSetBytes);
    ZpWire_WriteUInt64(Cursor, Region->ShareableWorkingSetBytes);
    ZpWire_WriteUInt64(Cursor, Region->LockedWorkingSetBytes);
    ZpWire_WriteUInt64(Cursor, Region->SharedOriginalBytes);
    ZpWire_WriteUInt32(Cursor, Region->State);
    ZpWire_WriteUInt32(Cursor, Region->Protect);
    ZpWire_WriteUInt32(Cursor, Region->Priority);
    ZpWire_WriteUInt32(Cursor, (ULONG)Region->WorkingSetStatus);
}

static
NTSTATUS
ZpProcess_ReadMemoryRegion(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_PROCESS_MEMORY_REGION_VIEW Region)
{
    ZP_PROCESS_MEMORY_REGION_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt64(Reader, &Local.BaseAddress);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.RegionSize);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.CommitSize);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.WorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.PrivateWorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.SharedWorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.ShareableWorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.LockedWorkingSetBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.SharedOriginalBytes);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.State);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Protect);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Priority);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, (PULONG)&Local.WorkingSetStatus);
    if (NT_SUCCESS(Status) && Region != NULL) *Region = Local;
    return Status;
}

NTSTATUS
ZpProcess_EncodeMemoryMap(
    _In_reads_opt_(RegionCount) PCZP_PROCESS_MEMORY_REGION Regions,
    _In_ ULONG RegionCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = 0;
    ULONG Index;

    if (RegionCount > ZP_CODEC_MAX_ELEMENT_COUNT || (RegionCount != 0 && Regions == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < RegionCount; Index++)
    {
        RequiredSize += 9 * sizeof(ULONGLONG) + 4 * sizeof(ULONG);
        if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    for (Index = 0; Index < RegionCount; Index++)
    {
        ZpProcess_WriteMemoryRegion(&Cursor, &Regions[Index]);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpProcess_DecodeMemoryMap(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PROCESS_MEMORY_MAP_VIEW View)
{
    const ULONG RecordSize = 9 * sizeof(ULONGLONG) + 4 * sizeof(ULONG);
    ULONG Count;

    if (PayloadLength % RecordSize != 0)
    {
        return STATUS_DATA_ERROR;
    }
    Count = PayloadLength / RecordSize;
    if (Count > ZP_CODEC_MAX_ELEMENT_COUNT) return STATUS_DATA_ERROR;
    View->Buffer = Payload;
    View->Length = PayloadLength;
    View->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpProcess_ReadMemoryMapRegion(
    _In_ PCZP_PROCESS_MEMORY_MAP_VIEW Map,
    _Inout_ PULONG Offset,
    _Out_ PZP_PROCESS_MEMORY_REGION_VIEW Region)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= Map->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(Map->Buffer, *Offset), Map->Length - *Offset);
    Status = ZpProcess_ReadMemoryRegion(&Reader, Region);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}
