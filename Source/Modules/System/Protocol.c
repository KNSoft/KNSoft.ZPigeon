#include "Include/KNSoft/ZPigeon/System.h"

#include "../../KNSoft.ZPigeon.Protocol/Core/Protocol.inl"

#define ZP_SYSTEM_INFO_FIXED_SIZE 17

NTSTATUS
ZpSystem_EncodeInfo(
    _In_ PCZP_SYSTEM_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONG RequiredSize;
    NTSTATUS Status;

    if (Info->Architecture < ZpSystemArchitectureX86 ||
        Info->Architecture > ZpSystemArchitectureArm64 ||
        Info->MajorVersion > MAXBYTE || Info->MinorVersion > MAXBYTE ||
        Info->ProcessorCount == 0 || Info->ProcessorCount > MAXUSHORT ||
        Info->ComputerNameLength == 0 ||
        Info->ComputerNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->ComputerName == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = ZP_SYSTEM_INFO_FIXED_SIZE +
                   Info->ComputerNameLength * sizeof(WCHAR);
    *BytesWritten = RequiredSize;
    if (Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }
    if (BufferSize < RequiredSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteByte(&Writer, Info->Architecture);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteByte(&Writer, (BYTE)Info->MajorVersion);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteByte(&Writer, (BYTE)Info->MinorVersion);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->BuildNumber);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt16(&Writer, (USHORT)Info->ProcessorCount);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt64(&Writer, Info->PhysicalMemoryBytes);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteTailString(&Writer,
                                         Info->ComputerName,
                                         Info->ComputerNameLength);
    }
    return Status;
}

NTSTATUS
ZpSystem_DecodeInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SYSTEM_INFO_VIEW View)
{
    ZP_CODEC_READER Reader;
    ZP_SYSTEM_ARCHITECTURE Architecture;
    USHORT ProcessorCount;
    BYTE MajorVersion, MinorVersion;
    NTSTATUS Status;

    if (PayloadLength < ZP_SYSTEM_INFO_FIXED_SIZE)
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &Architecture);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadByte(&Reader, &MajorVersion);
        if (NT_SUCCESS(Status)) View->MajorVersion = MajorVersion;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadByte(&Reader, &MinorVersion);
        if (NT_SUCCESS(Status)) View->MinorVersion = MinorVersion;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->BuildNumber);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt16(&Reader, &ProcessorCount);
        if (NT_SUCCESS(Status)) View->ProcessorCount = ProcessorCount;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt64(&Reader, &View->PhysicalMemoryBytes);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadTailString(&Reader, &View->ComputerName);
    }
    if (!NT_SUCCESS(Status) ||
        Reader.Offset != PayloadLength ||
        Architecture < ZpSystemArchitectureX86 ||
        Architecture > ZpSystemArchitectureArm64 ||
        View->ProcessorCount == 0 || View->ComputerName.Length == 0)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Architecture = Architecture;
    return STATUS_SUCCESS;
}
