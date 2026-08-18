#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Window.h"

static
NTSTATUS
ZpWindow_WriteRecord(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ PCZP_WINDOW_RECORD Record)
{
    NTSTATUS Status;

    Status = ZpCodec_WriteUInt64(Writer, Record->Handle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(Writer, Record->ParentHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->ThreadId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->Style);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->ExStyle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(Writer, Record->Flags);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(Writer, Record->Caption, Record->CaptionLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(Writer, Record->ClassName, Record->ClassNameLength);
    }
    return Status;
}

static
NTSTATUS
ZpWindow_ReadRecord(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_WINDOW_RECORD_VIEW Record)
{
    ZP_WINDOW_RECORD_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt64(Reader, &Local.Handle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.ParentHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.ThreadId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Style);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.ExStyle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Caption);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.ClassName);
    if (NT_SUCCESS(Status) && Record != NULL) *Record = Local;
    return Status;
}

static
LOGICAL
ZpWindow_IsControlValid(
    _In_ ZP_WINDOW_CONTROL Control)
{
    return Control >= ZpWindowControlShow && Control <= ZpWindowControlNotTopmost;
}

NTSTATUS
ZpWindow_EncodeList(
    _In_reads_opt_(WindowCount) PCZP_WINDOW_RECORD Windows,
    _In_ ULONG WindowCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize = sizeof(ULONG);
    NTSTATUS Status;
    ULONG Index;

    if (WindowCount > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (WindowCount != 0 && Windows == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < WindowCount; Index++)
    {
        if (Windows[Index].CaptionLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            Windows[Index].ClassNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            (Windows[Index].CaptionLength != 0 && Windows[Index].Caption == NULL) ||
            (Windows[Index].ClassNameLength != 0 && Windows[Index].ClassName == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 2 * sizeof(ULONGLONG) + 7 * sizeof(ULONG) +
                        ((ULONGLONG)Windows[Index].CaptionLength +
                         Windows[Index].ClassNameLength) * sizeof(WCHAR);
        if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12)
        {
            return STATUS_BUFFER_OVERFLOW;
        }
    }
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteArrayCount(&Writer, WindowCount);
    for (Index = 0; NT_SUCCESS(Status) && Index < WindowCount; Index++)
    {
        Status = ZpWindow_WriteRecord(&Writer, &Windows[Index]);
    }
    return Status;
}

NTSTATUS
ZpWindow_DecodeList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_WINDOW_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;
    ULONG Count, Index;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &Count);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Status = ZpWindow_ReadRecord(&Reader, NULL);
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
ZpWindow_GetRecord(
    _In_ PCZP_WINDOW_LIST_VIEW List,
    _In_ ULONG Index,
    _Out_ PZP_WINDOW_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Current;

    if (Index >= List->Count) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, List->Buffer, List->Length);
    for (Current = 0; NT_SUCCESS(Status) && Current <= Index; Current++)
    {
        Status = ZpWindow_ReadRecord(&Reader, Current == Index ? Record : NULL);
    }
    return Status;
}

NTSTATUS
ZpWindow_EncodeIdentity(
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    NTSTATUS Status;

    if (Handle == 0 || ProcessId == 0 || ThreadId == 0) return STATUS_INVALID_PARAMETER;
    *BytesWritten = sizeof(Handle) + sizeof(ProcessId) + sizeof(ThreadId);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, Handle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, ThreadId);
    return Status;
}

NTSTATUS
ZpWindow_DecodeIdentity(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG Handle,
    _Out_ PULONG ProcessId,
    _Out_ PULONG ThreadId)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != sizeof(*Handle) + sizeof(*ProcessId) + sizeof(*ThreadId))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, Handle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, ThreadId);
    if (NT_SUCCESS(Status) && (*Handle == 0 || *ProcessId == 0 || *ThreadId == 0))
    {
        return STATUS_DATA_ERROR;
    }
    return Status;
}

NTSTATUS
ZpWindow_EncodeControl(
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ZP_WINDOW_CONTROL Control,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONG IdentityLength;
    NTSTATUS Status;

    if (!ZpWindow_IsControlValid(Control)) return STATUS_INVALID_PARAMETER;
    Status = ZpWindow_EncodeIdentity(Handle, ProcessId, ThreadId, NULL, 0, &IdentityLength);
    if (!NT_SUCCESS(Status)) return Status;
    *BytesWritten = IdentityLength + sizeof(USHORT);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, Handle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, ThreadId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, Control);
    return Status;
}

NTSTATUS
ZpWindow_DecodeControl(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG Handle,
    _Out_ PULONG ProcessId,
    _Out_ PULONG ThreadId,
    _Out_ PZP_WINDOW_CONTROL Control)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (PayloadLength != sizeof(*Handle) + sizeof(*ProcessId) + sizeof(*ThreadId) + sizeof(USHORT))
    {
        return STATUS_DATA_ERROR;
    }
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, Handle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, ThreadId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, Control);
    if (NT_SUCCESS(Status) &&
        (*Handle == 0 || *ProcessId == 0 || *ThreadId == 0 || !ZpWindow_IsControlValid(*Control)))
    {
        return STATUS_DATA_ERROR;
    }
    return Status;
}

NTSTATUS
ZpWindow_EncodeInfo(
    _In_ PCZP_WINDOW_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Info->Record.CaptionLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->Record.ClassNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->MonitorDeviceLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (Info->Record.CaptionLength != 0 && Info->Record.Caption == NULL) ||
        (Info->Record.ClassNameLength != 0 && Info->Record.ClassName == NULL) ||
        (Info->MonitorDeviceLength != 0 && Info->MonitorDevice == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 8 * sizeof(ULONGLONG) + 23 * sizeof(ULONG) + 2 * sizeof(USHORT) +
                   ((ULONGLONG)Info->Record.CaptionLength + Info->Record.ClassNameLength +
                    Info->MonitorDeviceLength) * sizeof(WCHAR);
    if (RequiredSize > ZP_FRAME_MAX_BODY_SIZE - 12) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpWindow_WriteRecord(&Writer, &Info->Record);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->OwnerHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->WindowLeft);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->WindowTop);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->WindowRight);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->WindowBottom);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->ClientLeft);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->ClientTop);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->ClientRight);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->ClientBottom);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->WindowStatus);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->BorderWidth);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Info->BorderHeight);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, Info->ClassAtom);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, Info->CreatorVersion);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->PreviousHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->NextHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->FirstChildHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->FirstSiblingHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt64(&Writer, Info->LastSiblingHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->MonitorLeft);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->MonitorTop);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->MonitorRight);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Info->MonitorBottom);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Info->MonitorDevice, Info->MonitorDeviceLength);
    }
    return Status;
}

NTSTATUS
ZpWindow_DecodeInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_WINDOW_INFO_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpWindow_ReadRecord(&Reader, &View->Record);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->OwnerHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->WindowLeft);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->WindowTop);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->WindowRight);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->WindowBottom);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->ClientLeft);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->ClientTop);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->ClientRight);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->ClientBottom);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->WindowStatus);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->BorderWidth);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->BorderHeight);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &View->ClassAtom);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &View->CreatorVersion);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->PreviousHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->NextHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->FirstChildHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->FirstSiblingHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(&Reader, &View->LastSiblingHandle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->MonitorLeft);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->MonitorTop);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->MonitorRight);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->MonitorBottom);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &View->MonitorDevice);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpWindow_EncodeUpdate(
    _In_ PCZP_WINDOW_UPDATE Update,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Update->Handle == 0 || Update->ProcessId == 0 || Update->ThreadId == 0 ||
        Update->Fields == 0 || FlagOn(Update->Fields, ~ZP_WINDOW_UPDATE_MASK) ||
        Update->CaptionLength > ZP_WINDOW_CAPTION_MAX_CCH ||
        (Update->CaptionLength != 0 && Update->Caption == NULL) ||
        (!FlagOn(Update->Fields, ZP_WINDOW_UPDATE_CAPTION) && Update->CaptionLength != 0) ||
        (FlagOn(Update->Fields, ZP_WINDOW_UPDATE_RECT) &&
         (Update->Right <= Update->Left || Update->Bottom <= Update->Top)))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = sizeof(ULONGLONG) + 10 * sizeof(ULONG) +
                   (ULONGLONG)Update->CaptionLength * sizeof(WCHAR);
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteUInt64(&Writer, Update->Handle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Update->ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Update->ThreadId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Update->Fields);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Update->Caption, Update->CaptionLength);
    }
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Update->Left);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Update->Top);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Update->Right);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, (ULONG)Update->Bottom);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Update->Style);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Update->ExStyle);
    return Status;
}

NTSTATUS
ZpWindow_DecodeUpdate(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_WINDOW_UPDATE_VIEW View)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt64(&Reader, &View->Handle);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->ThreadId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->Fields);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &View->Caption);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->Left);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->Top);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->Right);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, (PULONG)&View->Bottom);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->Style);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &View->ExStyle);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    if (View->Handle == 0 || View->ProcessId == 0 || View->ThreadId == 0 ||
        View->Fields == 0 || FlagOn(View->Fields, ~ZP_WINDOW_UPDATE_MASK) ||
        View->Caption.Length > ZP_WINDOW_CAPTION_MAX_CCH ||
        (!FlagOn(View->Fields, ZP_WINDOW_UPDATE_CAPTION) && View->Caption.Length != 0) ||
        (FlagOn(View->Fields, ZP_WINDOW_UPDATE_RECT) &&
         (View->Right <= View->Left || View->Bottom <= View->Top)))
    {
        return STATUS_DATA_ERROR;
    }
    return STATUS_SUCCESS;
}
