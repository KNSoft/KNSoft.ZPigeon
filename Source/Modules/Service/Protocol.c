#include "Include/KNSoft/ZPigeon/Service.h"

#include "../../KNSoft.ZPigeon.Protocol/Core/Protocol.inl"

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
        Status = ZpCodec_ReadUInt32(Reader, &LocalRecord.ControlsAccepted);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(Reader, &LocalRecord.ProcessId);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(Reader, &LocalRecord.StartType);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(Reader, &LocalRecord.ServiceName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(Reader, &LocalRecord.DisplayName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(Reader, &LocalRecord.Description);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(Reader, &LocalRecord.StartName);
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
    PBYTE Cursor;
    ULONGLONG RequiredSize = sizeof(ULONG);
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
            Services[Index].DescriptionLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            Services[Index].StartNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            (Services[Index].ServiceNameLength != 0 &&
             Services[Index].ServiceName == NULL) ||
            (Services[Index].DisplayNameLength != 0 &&
             Services[Index].DisplayName == NULL) ||
            (Services[Index].DescriptionLength != 0 &&
             Services[Index].Description == NULL) ||
            (Services[Index].StartNameLength != 0 &&
             Services[Index].StartName == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 9 * sizeof(ULONG) +
                        (ULONGLONG)(Services[Index].ServiceNameLength +
                                    Services[Index].DisplayNameLength +
                                    Services[Index].DescriptionLength +
                                    Services[Index].StartNameLength) *
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

    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, ServiceCount);
    for (Index = 0; Index < ServiceCount; Index++)
    {
        ZpWire_WriteUInt32(&Cursor, Services[Index].ServiceType);
        ZpWire_WriteUInt32(&Cursor, Services[Index].CurrentState);
        ZpWire_WriteUInt32(&Cursor, Services[Index].ControlsAccepted);
        ZpWire_WriteUInt32(&Cursor, Services[Index].ProcessId);
        ZpWire_WriteUInt32(&Cursor, Services[Index].StartType);
        ZpWire_WriteString(&Cursor,
                           Services[Index].ServiceName,
                           Services[Index].ServiceNameLength);
        ZpWire_WriteString(&Cursor,
                           Services[Index].DisplayName,
                           Services[Index].DisplayNameLength);
        ZpWire_WriteString(&Cursor,
                           Services[Index].Description,
                           Services[Index].DescriptionLength);
        ZpWire_WriteString(&Cursor,
                           Services[Index].StartName,
                           Services[Index].StartNameLength);
    }
    return STATUS_SUCCESS;
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
ZpService_GetNextRecord(
    _In_ PCZP_SERVICE_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_SERVICE_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpService_ReadRecord(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
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
ZpService_EncodeControl(
    _In_ ULONG Control,
    _In_reads_(ServiceNameLength) PCWCH ServiceName,
    _In_ ULONG ServiceNameLength,
    _In_reads_opt_(ArgumentLength) PCWCH Argument,
    _In_ ULONG ArgumentLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Control < ZP_SERVICE_CONTROL_START || Control > ZP_SERVICE_CONTROL_RESTART ||
        ServiceNameLength == 0 || ServiceNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        ArgumentLength > ZP_CODEC_MAX_ELEMENT_COUNT || ServiceName == NULL ||
        (ArgumentLength != 0 && Argument == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 3 * sizeof(ULONG) +
                   (ULONGLONG)(ServiceNameLength + ArgumentLength) * sizeof(WCHAR);
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
    Status = ZpCodec_WriteUInt32(&Writer, Control);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, ServiceName, ServiceNameLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Argument, ArgumentLength);
    }
    return Status;
}

NTSTATUS
ZpService_DecodeControl(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG Control,
    _Out_ PZP_STRING_VIEW ServiceName,
    _Out_ PZP_STRING_VIEW Argument)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, Control);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, ServiceName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, Argument);
    }
    if (!NT_SUCCESS(Status) || *Control < ZP_SERVICE_CONTROL_START ||
        *Control > ZP_SERVICE_CONTROL_RESTART || ServiceName->Length == 0 ||
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
        Info->DescriptionLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->BinaryPathNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->StartNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->LoadOrderGroupLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->DependenciesLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->DependentsLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->ServiceDllLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->RebootMessageLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->RecoveryCommandLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Info->RecoverySupported > TRUE ||
        Info->FailureActionsOnNonCrashFailures > TRUE ||
        Info->FirstFailureAction > SC_ACTION_OWN_RESTART ||
        Info->SecondFailureAction > SC_ACTION_OWN_RESTART ||
        Info->ThirdFailureAction > SC_ACTION_OWN_RESTART ||
        Info->SubsequentFailureAction > SC_ACTION_OWN_RESTART ||
        Info->ServiceName == NULL ||
        (Info->DisplayNameLength != 0 && Info->DisplayName == NULL) ||
        (Info->DescriptionLength != 0 && Info->Description == NULL) ||
        (Info->BinaryPathNameLength != 0 && Info->BinaryPathName == NULL) ||
        (Info->StartNameLength != 0 && Info->StartName == NULL) ||
        (Info->LoadOrderGroupLength != 0 && Info->LoadOrderGroup == NULL) ||
        (Info->DependenciesLength != 0 && Info->Dependencies == NULL) ||
        (Info->DependentsLength != 0 && Info->Dependents == NULL) ||
        (Info->ServiceDllLength != 0 && Info->ServiceDll == NULL) ||
        (Info->RebootMessageLength != 0 && Info->RebootMessage == NULL) ||
        (Info->RecoveryCommandLength != 0 && Info->RecoveryCommand == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 29 * sizeof(ULONG) +
                   (ULONGLONG)(Info->ServiceNameLength +
                               Info->DisplayNameLength +
                               Info->DescriptionLength +
                               Info->BinaryPathNameLength +
                               Info->StartNameLength +
                               Info->LoadOrderGroupLength +
                               Info->DependenciesLength +
                               Info->DependentsLength + Info->ServiceDllLength +
                               Info->RebootMessageLength + Info->RecoveryCommandLength) * sizeof(WCHAR);
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
        Status = ZpCodec_WriteUInt32(&Writer, Info->ControlsAccepted);
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
        Status = ZpCodec_WriteUInt32(&Writer, Info->DelayedAutoStart);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->ServiceFlags);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->RecoverySupported);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->FailureActionsOnNonCrashFailures);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->RecoveryActionCount);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->ResetPeriodSeconds);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->RestartDelayMilliseconds);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->RebootDelayMilliseconds);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->FirstFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->SecondFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->ThirdFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Info->SubsequentFailureAction);
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
                                     Info->Description,
                                     Info->DescriptionLength);
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
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Info->LoadOrderGroup, Info->LoadOrderGroupLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Info->Dependencies, Info->DependenciesLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Info->Dependents, Info->DependentsLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Info->ServiceDll, Info->ServiceDllLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Info->RebootMessage, Info->RebootMessageLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Info->RecoveryCommand, Info->RecoveryCommandLength);
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

    if (PayloadLength < 29 * sizeof(ULONG))
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
        Status = ZpCodec_ReadUInt32(&Reader, &View->ControlsAccepted);
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
        Status = ZpCodec_ReadUInt32(&Reader, &View->DelayedAutoStart);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->ServiceFlags);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->RecoverySupported);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->FailureActionsOnNonCrashFailures);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->RecoveryActionCount);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->ResetPeriodSeconds);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->RestartDelayMilliseconds);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->RebootDelayMilliseconds);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->FirstFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->SecondFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->ThirdFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &View->SubsequentFailureAction);
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
        Status = ZpCodec_ReadString(&Reader, &View->Description);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->BinaryPathName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->StartName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->LoadOrderGroup);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->Dependencies);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->Dependents);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->ServiceDll);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->RebootMessage);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &View->RecoveryCommand);
    }
    if (!NT_SUCCESS(Status) ||
        View->ServiceName.Length == 0 ||
        View->RecoverySupported > TRUE ||
        View->FailureActionsOnNonCrashFailures > TRUE ||
        View->FirstFailureAction > SC_ACTION_OWN_RESTART ||
        View->SecondFailureAction > SC_ACTION_OWN_RESTART ||
        View->ThirdFailureAction > SC_ACTION_OWN_RESTART ||
        View->SubsequentFailureAction > SC_ACTION_OWN_RESTART ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpService_EncodeConfig(
    _In_ PCZP_SERVICE_CONFIG Config,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Config->StartType > SERVICE_DISABLED ||
        Config->DelayedAutoStart > TRUE ||
        (Config->DelayedAutoStart && Config->StartType != SERVICE_AUTO_START) ||
        Config->ServiceNameLength == 0 || Config->ServiceNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Config->DisplayNameLength == 0 ||
        Config->DisplayNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Config->DescriptionLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Config->BinaryPathNameLength == 0 || Config->BinaryPathNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Config->LoadOrderGroupLength > ZP_CODEC_MAX_ELEMENT_COUNT || Config->ServiceName == NULL ||
        Config->DisplayName == NULL || Config->BinaryPathName == NULL ||
        (Config->DescriptionLength != 0 && Config->Description == NULL) ||
        (Config->LoadOrderGroupLength != 0 && Config->LoadOrderGroup == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 7 * sizeof(ULONG) +
                   (ULONGLONG)(Config->ServiceNameLength + Config->DisplayNameLength +
                               Config->DescriptionLength + Config->BinaryPathNameLength +
                               Config->LoadOrderGroupLength) * sizeof(WCHAR);
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
    Status = ZpCodec_WriteUInt32(&Writer, Config->StartType);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Config->DelayedAutoStart);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Config->ServiceName, Config->ServiceNameLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Config->DisplayName, Config->DisplayNameLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Config->Description, Config->DescriptionLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Config->BinaryPathName, Config->BinaryPathNameLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Config->LoadOrderGroup, Config->LoadOrderGroupLength);
    }
    return Status;
}

NTSTATUS
ZpService_DecodeConfig(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERVICE_CONFIG_VIEW Config)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &Config->StartType);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &Config->DelayedAutoStart);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Config->ServiceName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Config->DisplayName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Config->Description);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Config->BinaryPathName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Config->LoadOrderGroup);
    }
    if (!NT_SUCCESS(Status) || Config->StartType > SERVICE_DISABLED ||
        Config->DelayedAutoStart > TRUE ||
        (Config->DelayedAutoStart && Config->StartType != SERVICE_AUTO_START) ||
        Config->ServiceName.Length == 0 || Config->DisplayName.Length == 0 ||
        Config->BinaryPathName.Length == 0 ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpService_EncodeRecoveryConfig(
    _In_ PCZP_SERVICE_RECOVERY_CONFIG Config,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Config->ErrorControl > SERVICE_ERROR_CRITICAL ||
        Config->FailureActionsOnNonCrashFailures > TRUE ||
        Config->FirstFailureAction > SC_ACTION_OWN_RESTART ||
        Config->SecondFailureAction > SC_ACTION_OWN_RESTART ||
        Config->ThirdFailureAction > SC_ACTION_OWN_RESTART ||
        Config->SubsequentFailureAction > SC_ACTION_OWN_RESTART ||
        Config->ServiceNameLength == 0 || Config->ServiceNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Config->RebootMessageLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Config->CommandLength > ZP_CODEC_MAX_ELEMENT_COUNT || Config->ServiceName == NULL ||
        (Config->RebootMessageLength != 0 && Config->RebootMessage == NULL) ||
        (Config->CommandLength != 0 && Config->Command == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 12 * sizeof(ULONG) +
                   (ULONGLONG)(Config->ServiceNameLength + Config->RebootMessageLength +
                               Config->CommandLength) * sizeof(WCHAR);
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
    Status = ZpCodec_WriteUInt32(&Writer, Config->ErrorControl);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Config->FailureActionsOnNonCrashFailures);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Config->ResetPeriodSeconds);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Config->RestartDelayMilliseconds);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Config->RebootDelayMilliseconds);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Config->FirstFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Config->SecondFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Config->ThirdFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteUInt32(&Writer, Config->SubsequentFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Config->ServiceName, Config->ServiceNameLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Config->RebootMessage, Config->RebootMessageLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Config->Command, Config->CommandLength);
    }
    return Status;
}

NTSTATUS
ZpService_DecodeRecoveryConfig(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERVICE_RECOVERY_CONFIG_VIEW Config)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &Config->ErrorControl);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &Config->FailureActionsOnNonCrashFailures);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &Config->ResetPeriodSeconds);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &Config->RestartDelayMilliseconds);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &Config->RebootDelayMilliseconds);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &Config->FirstFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &Config->SecondFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &Config->ThirdFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadUInt32(&Reader, &Config->SubsequentFailureAction);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Config->ServiceName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Config->RebootMessage);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Config->Command);
    }
    if (!NT_SUCCESS(Status) || Config->ErrorControl > SERVICE_ERROR_CRITICAL ||
        Config->FailureActionsOnNonCrashFailures > TRUE ||
        Config->FirstFailureAction > SC_ACTION_OWN_RESTART ||
        Config->SecondFailureAction > SC_ACTION_OWN_RESTART ||
        Config->ThirdFailureAction > SC_ACTION_OWN_RESTART ||
        Config->SubsequentFailureAction > SC_ACTION_OWN_RESTART ||
        Config->ServiceName.Length == 0 || Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpService_EncodeAccountConfig(
    _In_ PCZP_SERVICE_ACCOUNT_CONFIG Config,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if (Config->PasswordPresent > TRUE ||
        Config->ServiceNameLength == 0 || Config->ServiceNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Config->StartNameLength == 0 || Config->StartNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
        Config->PasswordLength > ZP_CODEC_MAX_ELEMENT_COUNT || Config->ServiceName == NULL ||
        Config->StartName == NULL || (!Config->PasswordPresent && Config->PasswordLength != 0) ||
        (Config->PasswordLength != 0 && Config->Password == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 4 * sizeof(ULONG) +
                   (ULONGLONG)(Config->ServiceNameLength + Config->StartNameLength +
                               Config->PasswordLength) * sizeof(WCHAR);
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
    Status = ZpCodec_WriteUInt32(&Writer, Config->PasswordPresent);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Config->ServiceName, Config->ServiceNameLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Config->StartName, Config->StartNameLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteString(&Writer, Config->Password, Config->PasswordLength);
    }
    return Status;
}

NTSTATUS
ZpService_DecodeAccountConfig(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_SERVICE_ACCOUNT_CONFIG_VIEW Config)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &Config->PasswordPresent);
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Config->ServiceName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Config->StartName);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_ReadString(&Reader, &Config->Password);
    }
    if (!NT_SUCCESS(Status) || Config->PasswordPresent > TRUE ||
        Config->ServiceName.Length == 0 || Config->StartName.Length == 0 ||
        (!Config->PasswordPresent && Config->Password.Length != 0) ||
        Reader.Offset != PayloadLength)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}
