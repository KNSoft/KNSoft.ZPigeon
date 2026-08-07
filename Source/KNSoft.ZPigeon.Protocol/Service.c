#include "Include/KNSoft/ZPigeon/Service.h"

static
NTSTATUS
ZpService_ReadRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_SERVICE_RECORD_VIEW Record)
{
    ZP_SERVICE_RECORD_VIEW LocalRecord;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt32(Reader, &LocalRecord.ServiceType);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(Reader, &LocalRecord.CurrentState);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(Reader, &LocalRecord.ProcessId);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(Reader, &LocalRecord.ServiceName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(Reader, &LocalRecord.DisplayName);
    }
    if (NT_SUCCESS(Status) && Record != NULL)
    {
        *Record = LocalRecord;
    }
    return Status;
}

NTSTATUS
ZpService_EncodeList(
    _In_reads_opt_(ServiceCount) PCZP_SERVICE_RECORD Services,
    _In_ ULONG ServiceCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize = sizeof(ULONG);
    NTSTATUS Status;
    ULONG Index;

    if (ServiceCount > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (ServiceCount != 0 && Services == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < ServiceCount; Index++)
    {
        if (Services[Index].ServiceNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            Services[Index].DisplayNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            (Services[Index].ServiceNameLength != 0 &&
             Services[Index].ServiceName == NULL) ||
            (Services[Index].DisplayNameLength != 0 &&
             Services[Index].DisplayName == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 5 * sizeof(ULONG) +
                        (ULONGLONG)(Services[Index].ServiceNameLength +
                                    Services[Index].DisplayNameLength) *
                            sizeof(WCHAR);
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
    Status = ZpCodec_WriteArrayCount(&Writer, ServiceCount);
    for (Index = 0; NT_SUCCESS(Status) && Index < ServiceCount; Index++)
    {
        Status = ZpCodec_WriteUInt32(&Writer, Services[Index].ServiceType);
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteUInt32(&Writer, Services[Index].CurrentState);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteUInt32(&Writer, Services[Index].ProcessId);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteString(&Writer,
                                         Services[Index].ServiceName,
                                         Services[Index].ServiceNameLength);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteString(&Writer,
                                         Services[Index].DisplayName,
                                         Services[Index].DisplayNameLength);
        }
    }
    return Status;
}

NTSTATUS
ZpService_DecodeList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERVICE_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;
    ULONG Count, Index;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpService_ReadRecord(&Reader, NULL);
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
ZpService_GetRecord(
    _In_ PCZP_SERVICE_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_SERVICE_RECORD_VIEW Record)
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
        Status = ZpService_ReadRecord(&Reader,
                                      CurrentIndex == Index ? Record : NULL);
    }
    return Status;
}

NTSTATUS
ZpService_EncodeQuery(
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;

    if (ServiceNameLength == 0 ||
        ServiceNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        ServiceName == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(ULONG) +
                   (ULONGLONG)ServiceNameLength * sizeof(WCHAR);
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
    return ZpCodec_WriteString(&Writer, ServiceName, ServiceNameLength);
}

NTSTATUS
ZpService_DecodeQuery(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW ServiceName)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, ServiceName);
    if (!NT_SUCCESS(Status) ||
        ServiceName->Length == 0 ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpService_EncodeInfo(
    _In_ PCZP_SERVICE_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Info->ServiceNameLength == 0 ||
        Info->ServiceNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->DisplayNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->BinaryPathNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->StartNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->ServiceName == NULL ||
        (Info->DisplayNameLength != 0 && Info->DisplayName == NULL) ||
        (Info->BinaryPathNameLength != 0 && Info->BinaryPathName == NULL) ||
        (Info->StartNameLength != 0 && Info->StartName == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 9 * sizeof(ULONG) +
                   (ULONGLONG)(Info->ServiceNameLength +
                               Info->DisplayNameLength +
                               Info->BinaryPathNameLength +
                               Info->StartNameLength) * sizeof(WCHAR);
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
    Status = ZpCodec_WriteUInt32(&Writer, Info->ServiceType);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->CurrentState);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->ProcessId);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->StartType);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->ErrorControl);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer,
                                     Info->ServiceName,
                                     Info->ServiceNameLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer,
                                     Info->DisplayName,
                                     Info->DisplayNameLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer,
                                     Info->BinaryPathName,
                                     Info->BinaryPathNameLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer,
                                     Info->StartName,
                                     Info->StartNameLength);
    }
    return Status;
}

NTSTATUS
ZpService_DecodeInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERVICE_INFO_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength < 9 * sizeof(ULONG))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &View->ServiceType);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->CurrentState);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->ProcessId);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->StartType);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->ErrorControl);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->ServiceName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->DisplayName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->BinaryPathName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->StartName);
    }
    if (!NT_SUCCESS(Status) ||
        View->ServiceName.Length == 0 ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}
