#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/PortableDevice.h"

static
LOGICAL
ZpPortable_IsStringValid(
    _In_ PCZP_STRING_VIEW String,
    _In_ LOGICAL Empty)
{
    return String->Length <= ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH &&
           (Empty || String->Length != 0);
}

static
NTSTATUS
ZpPortable_ReadDevice(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_PORTABLE_DEVICE_RECORD_VIEW Device)
{
    ZP_PORTABLE_DEVICE_RECORD_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadString(Reader, &Local.Id);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Name);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Manufacturer);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Model);
    if (NT_SUCCESS(Status) &&
        (!ZpPortable_IsStringValid(&Local.Id, FALSE) || !ZpPortable_IsStringValid(&Local.Name, TRUE) ||
         !ZpPortable_IsStringValid(&Local.Manufacturer, TRUE) || !ZpPortable_IsStringValid(&Local.Model, TRUE)))
    {
        return STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Device != NULL) *Device = Local;
    return Status;
}

NTSTATUS
ZpPortable_EncodeDeviceList(
    _In_reads_opt_(Count) PCZP_PORTABLE_DEVICE_RECORD Devices,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONG Index;
    NTSTATUS Status;

    if (Count > ZP_PORTABLE_DEVICE_MAX_DEVICES || (Count != 0 && Devices == NULL)) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteArrayCount(&Writer, Count);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        PCZP_PORTABLE_DEVICE_RECORD Device = &Devices[Index];

        if (Device->Id == NULL || Device->IdLength == 0 ||
            Device->IdLength > ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH ||
            Device->NameLength > ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH ||
            Device->ManufacturerLength > ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH ||
            Device->ModelLength > ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpCodec_WriteString(&Writer, Device->Id, Device->IdLength);
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Device->Name, Device->NameLength);
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteString(&Writer, Device->Manufacturer, Device->ManufacturerLength);
        }
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Device->Model, Device->ModelLength);
    }
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpPortable_DecodeDeviceList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PORTABLE_DEVICE_LIST_VIEW List)
{
    ZP_CODEC_READER Reader;
    ULONG Count, Index, Offset;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    if (!NT_SUCCESS(Status) || Count > ZP_PORTABLE_DEVICE_MAX_DEVICES)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    Offset = Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++) Status = ZpPortable_ReadDevice(&Reader, NULL);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength) return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    List->Buffer = Add2Ptr(Payload, Offset);
    List->Length = PayloadLength - Offset;
    List->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpPortable_GetNextDevice(
    _In_ PCZP_PORTABLE_DEVICE_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_PORTABLE_DEVICE_RECORD_VIEW Device)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpPortable_ReadDevice(&Reader, Device);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

static
NTSTATUS
ZpPortable_ReadObject(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_PORTABLE_OBJECT_RECORD_VIEW Object)
{
    ZP_PORTABLE_OBJECT_RECORD_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt32(Reader, &Local.Flags);
    if (NT_SUCCESS(Status) &&
        (Local.Flags & ~(ZP_PORTABLE_OBJECT_FOLDER | ZP_PORTABLE_OBJECT_STORAGE |
                         ZP_PORTABLE_OBJECT_CAN_DELETE)) != 0)
    {
        return STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && !FlagOn(Local.Flags, ZP_PORTABLE_OBJECT_FOLDER))
    {
        Status = ZpCodec_ReadUInt64(Reader, &Local.Size);
    }
    else Local.Size = 0;
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.ModifiedTime);
    if (NT_SUCCESS(Status) && FlagOn(Local.Flags, ZP_PORTABLE_OBJECT_STORAGE))
    {
        Status = ZpCodec_ReadUInt64(Reader, &Local.Capacity);
        if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.FreeSpace);
    }
    else
    {
        Local.Capacity = 0;
        Local.FreeSpace = 0;
    }
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Id);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.PersistentId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Name);
    if (NT_SUCCESS(Status) &&
        (!ZpPortable_IsStringValid(&Local.Id, FALSE) || !ZpPortable_IsStringValid(&Local.PersistentId, TRUE) ||
         !ZpPortable_IsStringValid(&Local.Name, TRUE)))
    {
        return STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Object != NULL) *Object = Local;
    return Status;
}

NTSTATUS
ZpPortable_EncodeObjectPage(
    _In_reads_opt_(Count) PCZP_PORTABLE_OBJECT_RECORD Objects,
    _In_ ULONG Count,
    _In_ ULONG NextOffset,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONG Index;
    NTSTATUS Status;

    if (Count > ZP_PORTABLE_DEVICE_PAGE_COUNT || (Count != 0 && Objects == NULL)) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteArrayCount(&Writer, Count);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, NextOffset);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        PCZP_PORTABLE_OBJECT_RECORD Object = &Objects[Index];

        if ((Object->Flags & ~(ZP_PORTABLE_OBJECT_FOLDER | ZP_PORTABLE_OBJECT_STORAGE |
                               ZP_PORTABLE_OBJECT_CAN_DELETE)) != 0 ||
            Object->Id == NULL || Object->IdLength == 0 ||
            Object->IdLength > ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH ||
            Object->PersistentIdLength > ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH ||
            Object->NameLength > ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpCodec_WriteUInt32(&Writer, Object->Flags);
        if (NT_SUCCESS(Status) && !FlagOn(Object->Flags, ZP_PORTABLE_OBJECT_FOLDER))
        {
            Status = ZpCodec_WriteUInt64(&Writer, Object->Size);
        }
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Object->ModifiedTime);
        if (NT_SUCCESS(Status) && FlagOn(Object->Flags, ZP_PORTABLE_OBJECT_STORAGE))
        {
            Status = ZpCodec_WriteUInt64(&Writer, Object->Capacity);
            if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Object->FreeSpace);
        }
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Object->Id, Object->IdLength);
        if (NT_SUCCESS(Status))
        {
            Status = ZpCodec_WriteString(&Writer, Object->PersistentId, Object->PersistentIdLength);
        }
        if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Object->Name, Object->NameLength);
    }
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpPortable_DecodeObjectPage(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PORTABLE_OBJECT_PAGE_VIEW Page)
{
    ZP_CODEC_READER Reader;
    ULONG Count, Index, Offset;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Page->NextOffset);
    if (!NT_SUCCESS(Status) || Count > ZP_PORTABLE_DEVICE_PAGE_COUNT)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    Offset = Reader.Offset;
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++) Status = ZpPortable_ReadObject(&Reader, NULL);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength) return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    Page->Buffer = Add2Ptr(Payload, Offset);
    Page->Length = PayloadLength - Offset;
    Page->Count = Count;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpPortable_GetNextObject(
    _In_ PCZP_PORTABLE_OBJECT_PAGE_VIEW Page,
    _Inout_ PULONG Offset,
    _Out_ PZP_PORTABLE_OBJECT_RECORD_VIEW Object)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= Page->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(Page->Buffer, *Offset), Page->Length - *Offset);
    Status = ZpPortable_ReadObject(&Reader, Object);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

static
NTSTATUS
ZpPortable_WriteString(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_reads_opt_(Length) PCWCH String,
    _In_ ULONG Length,
    _In_ LOGICAL Empty)
{
    if (Length > ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH || (!Empty && Length == 0) ||
        (Length != 0 && String == NULL)) return STATUS_INVALID_PARAMETER;
    return ZpCodec_WriteString(Writer, String, Length);
}

NTSTATUS
ZpPortable_EncodeObjectPageRequest(
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_opt_(ParentIdLength) PCWCH ParentId,
    _In_ ULONG ParentIdLength,
    _In_ ULONG Offset,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpPortable_WriteString(&Writer, DeviceId, DeviceIdLength, FALSE);
    if (NT_SUCCESS(Status)) Status = ZpPortable_WriteString(&Writer, ParentId, ParentIdLength, TRUE);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Offset);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpPortable_DecodeObjectPageRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PORTABLE_OBJECT_PAGE_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, &Request->DeviceId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->ParentId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Request->Offset);
    return NT_SUCCESS(Status) &&
           (!ZpPortable_IsStringValid(&Request->DeviceId, FALSE) ||
            !ZpPortable_IsStringValid(&Request->ParentId, TRUE) ||
            Request->Offset > MAXULONG - ZP_PORTABLE_DEVICE_PAGE_COUNT || Reader.Offset != PayloadLength) ?
               STATUS_DATA_ERROR : Status;
}

NTSTATUS
ZpPortable_EncodeObjectRequest(
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ObjectIdLength) PCWCH ObjectId,
    _In_ ULONG ObjectIdLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpPortable_WriteString(&Writer, DeviceId, DeviceIdLength, FALSE);
    if (NT_SUCCESS(Status)) Status = ZpPortable_WriteString(&Writer, ObjectId, ObjectIdLength, FALSE);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpPortable_DecodeObjectRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PORTABLE_OBJECT_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, &Request->DeviceId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->ObjectId);
    return NT_SUCCESS(Status) &&
           (!ZpPortable_IsStringValid(&Request->DeviceId, FALSE) ||
            !ZpPortable_IsStringValid(&Request->ObjectId, FALSE) || Reader.Offset != PayloadLength) ?
               STATUS_DATA_ERROR : Status;
}

NTSTATUS
ZpPortable_EncodeNameRequest(
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ObjectIdLength) PCWCH ObjectId,
    _In_ ULONG ObjectIdLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpPortable_WriteString(&Writer, DeviceId, DeviceIdLength, FALSE);
    if (NT_SUCCESS(Status)) Status = ZpPortable_WriteString(&Writer, ObjectId, ObjectIdLength, FALSE);
    if (NT_SUCCESS(Status)) Status = ZpPortable_WriteString(&Writer, Name, NameLength, FALSE);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpPortable_DecodeNameRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PORTABLE_NAME_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, &Request->DeviceId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->ObjectId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->Name);
    return NT_SUCCESS(Status) &&
           (!ZpPortable_IsStringValid(&Request->DeviceId, FALSE) ||
            !ZpPortable_IsStringValid(&Request->ObjectId, FALSE) ||
            !ZpPortable_IsStringValid(&Request->Name, FALSE) || Reader.Offset != PayloadLength) ?
               STATUS_DATA_ERROR : Status;
}

NTSTATUS
ZpPortable_EncodeWriteRequest(
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ParentIdLength) PCWCH ParentId,
    _In_ ULONG ParentIdLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONGLONG FileSize,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpPortable_WriteString(&Writer, DeviceId, DeviceIdLength, FALSE);
    if (NT_SUCCESS(Status)) Status = ZpPortable_WriteString(&Writer, ParentId, ParentIdLength, FALSE);
    if (NT_SUCCESS(Status)) Status = ZpPortable_WriteString(&Writer, Name, NameLength, FALSE);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, FileSize);
    *BytesWritten = Writer.Offset;
    return Status;
}

NTSTATUS
ZpPortable_DecodeWriteRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_PORTABLE_WRITE_REQUEST_VIEW Request)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, &Request->DeviceId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->ParentId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Request->Name);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &Request->FileSize);
    return NT_SUCCESS(Status) &&
           (!ZpPortable_IsStringValid(&Request->DeviceId, FALSE) ||
            !ZpPortable_IsStringValid(&Request->ParentId, FALSE) ||
            !ZpPortable_IsStringValid(&Request->Name, FALSE) || Reader.Offset != PayloadLength) ?
               STATUS_DATA_ERROR : Status;
}
