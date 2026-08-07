#include "Include/KNSoft/ZPigeon/Process.h"

static
NTSTATUS
ZpProcess_ReadRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_PROCESS_RECORD_VIEW Record)
{
    ZP_PROCESS_RECORD_VIEW LocalRecord;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt32(Reader, &LocalRecord.ProcessId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(Reader, &LocalRecord.SessionId);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(Reader, &LocalRecord.ImageName);
    }
    if (NT_SUCCESS(Status) && Record != NULL)
    {
        *Record = LocalRecord;
    }
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

    if (ProcessCount > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (ProcessCount != 0 && Processes == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < ProcessCount; Index++)
    {
        if (Processes[Index].ImageNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            (Processes[Index].ImageNameLength != 0 &&
             Processes[Index].ImageName == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 3 * sizeof(ULONG) +
                        (ULONGLONG)Processes[Index].ImageNameLength * sizeof(WCHAR);
        if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12)
        {
            return STATUS_BUFFER_OVERFLOW;
        }
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < RequiredSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteArrayCount(&Writer, ProcessCount);
    for (Index = 0; NT_SUCCESS(Status) && Index < ProcessCount; Index++)
    {
        Status = ZpCodec_WriteUInt32(&Writer, Processes[Index].ProcessId);
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteUInt32(&Writer, Processes[Index].SessionId);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteString(&Writer,
                                         Processes[Index].ImageName,
                                         Processes[Index].ImageNameLength);
        }
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
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpProcess_ReadRecord(&Reader, NULL);
    }
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

    if (Index >= List->Count)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ZpCodec_InitializeReader(&Reader, List->Buffer, List->Length);
    for (CurrentIndex = 0;
         NT_SUCCESS(Status) && CurrentIndex <= Index;
         CurrentIndex++)
    {
        Status = ZpProcess_ReadRecord(&Reader,
                                      CurrentIndex == Index ? Record : NULL);
    }
    return Status;
}

NTSTATUS
ZpProcess_EncodeQuery(
    _In_ ULONG ProcessId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;

    *BytesWritten = sizeof(ProcessId);
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < sizeof(ProcessId))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteUInt32(&Writer, ProcessId);
}

NTSTATUS
ZpProcess_DecodeQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ProcessId)
{
    ZP_CODEC_READER Reader;

    if (PayloadLength != sizeof(*ProcessId))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    return ZpCodec_ReadUInt32(&Reader, ProcessId);
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

    if (Info->ImageNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (Info->ImageNameLength != 0 && Info->ImageName == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 5 * sizeof(ULONG) +
                   5 * sizeof(ULONGLONG) +
                   sizeof(ULONG) +
                   (ULONGLONG)Info->ImageNameLength * sizeof(WCHAR);
    if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12)
    {
        return STATUS_BUFFER_OVERFLOW;
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < RequiredSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt32(&Writer, Info->ProcessId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->ParentProcessId);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->SessionId);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->ThreadCount);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->HandleCount);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, Info->CreateTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, Info->UserTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, Info->KernelTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, Info->WorkingSetBytes);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, Info->PrivateBytes);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer,
                                     Info->ImageName,
                                     Info->ImageNameLength);
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
    NTSTATUS Status;

    if (PayloadLength < 5 * sizeof(ULONG) +
                        5 * sizeof(ULONGLONG) +
                        sizeof(ULONG))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &View->ProcessId);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->ParentProcessId);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->SessionId);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->ThreadCount);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->HandleCount);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, &View->CreateTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, &View->UserTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, &View->KernelTime);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, &View->WorkingSetBytes);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, &View->PrivateBytes);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->ImageName);
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}
