#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Execution.h"

#include "../../KNSoft.ZPigeon.Protocol/Core/Protocol.inl"

static
NTSTATUS
ZpExecution_ReadSession(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_EXECUTION_SESSION_RECORD_VIEW Record)
{
    ZP_EXECUTION_SESSION_RECORD_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt32(Reader, &Local.SessionId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.State);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.StationName);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.UserName);
    if (NT_SUCCESS(Status) && Record != NULL) *Record = Local;
    return Status;
}

NTSTATUS
ZpExecution_EncodeSessions(
    _In_reads_opt_(Count) PCZP_EXECUTION_SESSION_RECORD Records,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = sizeof(ULONG);
    ULONG Index;

    if (Count > ZP_CODEC_MAX_ELEMENT_COUNT || (Count != 0 && Records == NULL)) return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < Count; Index++)
    {
        if ((Records[Index].StationNameLength != 0 && Records[Index].StationName == NULL) ||
            (Records[Index].UserNameLength != 0 && Records[Index].UserName == NULL) ||
            Records[Index].StationNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            Records[Index].UserNameLength > ZP_CODEC_MAX_ELEMENT_COUNT)
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 5 * sizeof(ULONG) +
                        ((ULONGLONG)Records[Index].StationNameLength + Records[Index].UserNameLength) * sizeof(WCHAR);
    }
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, Count);
    for (Index = 0; Index < Count; Index++)
    {
        ZpWire_WriteUInt32(&Cursor, Records[Index].SessionId);
        ZpWire_WriteUInt32(&Cursor, Records[Index].State);
        ZpWire_WriteUInt32(&Cursor, Records[Index].Flags);
        ZpWire_WriteString(&Cursor, Records[Index].StationName, Records[Index].StationNameLength);
        ZpWire_WriteString(&Cursor, Records[Index].UserName, Records[Index].UserNameLength);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpExecution_DecodeSessions(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EXECUTION_SESSION_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    ULONG Index;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &View->Count);
    if (!NT_SUCCESS(Status)) return Status;
    View->Buffer = Add2Ptr(Payload, Reader.Offset);
    for (Index = 0; NT_SUCCESS(Status) && Index < View->Count; Index++)
    {
        Status = ZpExecution_ReadSession(&Reader, NULL);
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength) return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    View->Length = PayloadLength - sizeof(ULONG);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpExecution_GetNextSession(
    _In_ PCZP_EXECUTION_SESSION_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_EXECUTION_SESSION_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpExecution_ReadSession(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

static
NTSTATUS
ZpExecution_ReadRuntime(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_EXECUTION_RUNTIME_RECORD_VIEW Record)
{
    ZP_EXECUTION_RUNTIME_RECORD_VIEW Local;
    ULONG Index;
    NTSTATUS Status;

    Status = ZpCodec_ReadByte(Reader, &Local.Kind);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(Reader, &Local.Image.Machine);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(Reader, &Local.Image.Subsystem);
    for (Index = 0; NT_SUCCESS(Status) && Index < ARRAYSIZE(Local.Image.Version); Index++)
    {
        Status = ZpCodec_ReadUInt16(Reader, &Local.Image.Version[Index]);
    }
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.Path);
    if (NT_SUCCESS(Status) &&
        (Local.Kind < ZpExecutionRuntimeCommandPrompt || Local.Kind > ZpExecutionRuntimeGo ||
         Local.Path.Length == 0))
    {
        Status = STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Record != NULL) *Record = Local;
    return Status;
}

NTSTATUS
ZpExecution_EncodeEnvironment(
    _In_ ULONG Flags,
    _In_reads_opt_(Count) PCZP_EXECUTION_RUNTIME_RECORD Records,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = 2 * sizeof(ULONG);
    ULONG Index, VersionIndex;

    if ((Flags & ~ZP_EXECUTION_ENVIRONMENT_FLAG_ADMINISTRATOR) != 0 || Count > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (Count != 0 && Records == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < Count; Index++)
    {
        if (Records[Index].Kind < ZpExecutionRuntimeCommandPrompt || Records[Index].Kind > ZpExecutionRuntimeGo ||
            Records[Index].PathLength == 0 || Records[Index].Path == NULL ||
            Records[Index].PathLength > ZP_CODEC_MAX_ELEMENT_COUNT)
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += sizeof(BYTE) + 6 * sizeof(USHORT) + sizeof(ULONG) +
                        (ULONGLONG)Records[Index].PathLength * sizeof(WCHAR);
    }
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, Flags);
    ZpWire_WriteUInt32(&Cursor, Count);
    for (Index = 0; Index < Count; Index++)
    {
        ZpWire_WriteByte(&Cursor, Records[Index].Kind);
        ZpWire_WriteUInt16(&Cursor, Records[Index].Image.Machine);
        ZpWire_WriteUInt16(&Cursor, Records[Index].Image.Subsystem);
        for (VersionIndex = 0; VersionIndex < ARRAYSIZE(Records[Index].Image.Version); VersionIndex++)
        {
            ZpWire_WriteUInt16(&Cursor, Records[Index].Image.Version[VersionIndex]);
        }
        ZpWire_WriteString(&Cursor, Records[Index].Path, Records[Index].PathLength);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpExecution_DecodeEnvironment(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EXECUTION_ENVIRONMENT_VIEW View)
{
    ZP_CODEC_READER Reader;
    ULONG Index;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt32(&Reader, &View->Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadArrayCount(&Reader, &View->Count);
    if (!NT_SUCCESS(Status) || (View->Flags & ~ZP_EXECUTION_ENVIRONMENT_FLAG_ADMINISTRATOR) != 0)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    View->Buffer = Add2Ptr(Payload, Reader.Offset);
    for (Index = 0; NT_SUCCESS(Status) && Index < View->Count; Index++)
    {
        Status = ZpExecution_ReadRuntime(&Reader, NULL);
    }
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength) return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    View->Length = PayloadLength - 2 * sizeof(ULONG);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpExecution_GetNextRuntime(
    _In_ PCZP_EXECUTION_ENVIRONMENT_VIEW Environment,
    _Inout_ PULONG Offset,
    _Out_ PZP_EXECUTION_RUNTIME_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= Environment->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader,
                             Add2Ptr(Environment->Buffer, *Offset),
                             Environment->Length - *Offset);
    Status = ZpExecution_ReadRuntime(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

NTSTATUS
ZpExecution_EncodeImageInfo(
    _In_ PCZP_EXECUTION_IMAGE_INFO Image,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONG Index;

    *BytesWritten = 6 * sizeof(USHORT);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < *BytesWritten) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteUInt16(&Cursor, Image->Machine);
    ZpWire_WriteUInt16(&Cursor, Image->Subsystem);
    for (Index = 0; Index < ARRAYSIZE(Image->Version); Index++)
    {
        ZpWire_WriteUInt16(&Cursor, Image->Version[Index]);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
ZpExecution_DecodeImageInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EXECUTION_IMAGE_INFO Image)
{
    ZP_CODEC_READER Reader;
    ULONG Index;
    NTSTATUS Status;

    if (PayloadLength != 6 * sizeof(USHORT)) return STATUS_DATA_ERROR;
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadUInt16(&Reader, &Image->Machine);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt16(&Reader, &Image->Subsystem);
    for (Index = 0; NT_SUCCESS(Status) && Index < ARRAYSIZE(Image->Version); Index++)
    {
        Status = ZpCodec_ReadUInt16(&Reader, &Image->Version[Index]);
    }
    return Status;
}

NTSTATUS
ZpExecution_EncodeStart(
    _In_ PCZP_EXECUTION_START Start,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;
    NTSTATUS Status;

    if ((Start->Engine != ZpExecutionEngineCreateProcess && Start->Engine != ZpExecutionEngineShellExecute) ||
        Start->Identity < ZpExecutionIdentityCurrent || Start->Identity > ZpExecutionIdentityCustomToken ||
        (Start->Flags & ~(ZP_EXECUTION_FLAG_HIDDEN | ZP_EXECUTION_FLAG_DELETE_FILE |
                          ZP_EXECUTION_FLAG_JOB_OBJECT)) != 0 ||
        (FlagOn(Start->Flags, ZP_EXECUTION_FLAG_JOB_OBJECT) &&
         Start->Engine != ZpExecutionEngineCreateProcess) ||
        Start->FileNameLength == 0 || Start->FileName == NULL ||
        (Start->ArgumentsLength != 0 && Start->Arguments == NULL) ||
        (Start->WorkingDirectoryLength != 0 && Start->WorkingDirectory == NULL) ||
        (Start->VerbLength != 0 && Start->Verb == NULL) ||
        (Start->UserNameLength != 0 && Start->UserName == NULL) ||
        (Start->PasswordLength != 0 && Start->Password == NULL) ||
        (Start->AppContainerSidLength != 0 && Start->AppContainerSid == NULL) ||
        (Start->CustomTokenLength != 0 && Start->CustomToken == NULL) ||
        (Start->Identity == ZpExecutionIdentityOtherUser ?
             Start->UserNameLength == 0 || Start->PasswordLength == 0 :
             Start->UserNameLength != 0 || Start->PasswordLength != 0) ||
        (Start->Identity == ZpExecutionIdentityAppContainer ?
             Start->Engine != ZpExecutionEngineCreateProcess ||
                 Start->SessionId != ZP_EXECUTION_SESSION_CURRENT || Start->AppContainerSidLength == 0 ||
                 Start->VerbLength != 0 :
             Start->AppContainerSidLength != 0) ||
        (Start->Identity == ZpExecutionIdentityCustomToken ?
             Start->Engine != ZpExecutionEngineCreateProcess || Start->CustomTokenLength == 0 ||
                 Start->VerbLength != 0 :
             Start->CustomTokenLength != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequiredSize = 2 * sizeof(BYTE) + 2 * sizeof(ULONG) + 8 * sizeof(ULONG) +
                   ((ULONGLONG)Start->FileNameLength + Start->ArgumentsLength +
                    Start->WorkingDirectoryLength + Start->VerbLength +
                     Start->UserNameLength + Start->PasswordLength +
                     Start->AppContainerSidLength) * sizeof(WCHAR) + Start->CustomTokenLength;
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    Status = ZpCodec_WriteByte(&Writer, Start->Engine);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteByte(&Writer, Start->Identity);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Start->SessionId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Start->Flags);
#define ZP_EXECUTION_WRITE_START_STRING(Field) \
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteString(&Writer, Start->Field, Start->Field##Length)
    ZP_EXECUTION_WRITE_START_STRING(FileName);
    ZP_EXECUTION_WRITE_START_STRING(Arguments);
    ZP_EXECUTION_WRITE_START_STRING(WorkingDirectory);
    ZP_EXECUTION_WRITE_START_STRING(Verb);
    ZP_EXECUTION_WRITE_START_STRING(UserName);
    ZP_EXECUTION_WRITE_START_STRING(Password);
    ZP_EXECUTION_WRITE_START_STRING(AppContainerSid);
#undef ZP_EXECUTION_WRITE_START_STRING
    if (NT_SUCCESS(Status))
    {
        Status = ZpCodec_WriteByteString(&Writer, Start->CustomToken, Start->CustomTokenLength);
    }
    return Status;
}

NTSTATUS
ZpExecution_DecodeStart(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EXECUTION_START_VIEW Start)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadByte(&Reader, &Start->Engine);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(&Reader, &Start->Identity);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Start->SessionId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(&Reader, &Start->Flags);
#define ZP_EXECUTION_READ_START_STRING(Field) \
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(&Reader, &Start->Field)
    ZP_EXECUTION_READ_START_STRING(FileName);
    ZP_EXECUTION_READ_START_STRING(Arguments);
    ZP_EXECUTION_READ_START_STRING(WorkingDirectory);
    ZP_EXECUTION_READ_START_STRING(Verb);
    ZP_EXECUTION_READ_START_STRING(UserName);
    ZP_EXECUTION_READ_START_STRING(Password);
    ZP_EXECUTION_READ_START_STRING(AppContainerSid);
#undef ZP_EXECUTION_READ_START_STRING
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByteString(&Reader, &Start->CustomToken);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength || Start->FileName.Length == 0 ||
        (Start->Engine != ZpExecutionEngineCreateProcess && Start->Engine != ZpExecutionEngineShellExecute) ||
        Start->Identity < ZpExecutionIdentityCurrent || Start->Identity > ZpExecutionIdentityCustomToken ||
        (Start->Flags & ~(ZP_EXECUTION_FLAG_HIDDEN | ZP_EXECUTION_FLAG_DELETE_FILE |
                          ZP_EXECUTION_FLAG_JOB_OBJECT)) != 0 ||
        (FlagOn(Start->Flags, ZP_EXECUTION_FLAG_JOB_OBJECT) &&
         Start->Engine != ZpExecutionEngineCreateProcess) ||
        (Start->Identity == ZpExecutionIdentityOtherUser ?
             Start->UserName.Length == 0 || Start->Password.Length == 0 :
             Start->UserName.Length != 0 || Start->Password.Length != 0) ||
        (Start->Identity == ZpExecutionIdentityAppContainer ?
             Start->Engine != ZpExecutionEngineCreateProcess ||
                 Start->SessionId != ZP_EXECUTION_SESSION_CURRENT || Start->AppContainerSid.Length == 0 ||
                 Start->Verb.Length != 0 :
             Start->AppContainerSid.Length != 0) ||
        (Start->Identity == ZpExecutionIdentityCustomToken ?
             Start->Engine != ZpExecutionEngineCreateProcess || Start->CustomToken.Length == 0 ||
                 Start->Verb.Length != 0 :
             Start->CustomToken.Length != 0))
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}

static
VOID
ZpExecution_WriteJob(
    _Inout_ PBYTE* Cursor,
    _In_ PCZP_EXECUTION_JOB_RECORD Record)
{
    ZpWire_WriteUInt32(Cursor, Record->JobId);
    ZpWire_WriteUInt64(Cursor, Record->CreateTime);
    ZpWire_WriteUInt64(Cursor, Record->ExitTime);
    ZpWire_WriteUInt32(Cursor, Record->ProcessId);
    ZpWire_WriteUInt32(Cursor, Record->SessionId);
    ZpWire_WriteUInt32(Cursor, Record->ExitCode);
    ZpWire_WriteUInt32(Cursor, Record->Flags);
    ZpWire_WriteByte(Cursor, Record->Engine);
    ZpWire_WriteByte(Cursor, Record->Identity);
    ZpWire_WriteByte(Cursor, Record->State);
    ZpWire_WriteString(Cursor, Record->FileName, Record->FileNameLength);
}

static
NTSTATUS
ZpExecution_ReadJob(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_opt_ PZP_EXECUTION_JOB_RECORD_VIEW Record)
{
    ZP_EXECUTION_JOB_RECORD_VIEW Local;
    NTSTATUS Status;

    Status = ZpCodec_ReadUInt32(Reader, &Local.JobId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.CreateTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt64(Reader, &Local.ExitTime);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.ProcessId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.SessionId);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.ExitCode);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadUInt32(Reader, &Local.Flags);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(Reader, &Local.Engine);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(Reader, &Local.Identity);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadByte(Reader, &Local.State);
    if (NT_SUCCESS(Status)) Status = ZpCodec_ReadString(Reader, &Local.FileName);
    if (NT_SUCCESS(Status) && (Local.JobId == 0 || Local.State < ZpExecutionJobRunning ||
                               Local.State > ZpExecutionJobExited))
    {
        Status = STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status) && Record != NULL) *Record = Local;
    return Status;
}

NTSTATUS
ZpExecution_EncodeJobs(
    _In_reads_opt_(Count) PCZP_EXECUTION_JOB_RECORD Records,
    _In_ ULONG Count,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    PBYTE Cursor;
    ULONGLONG RequiredSize = sizeof(ULONG);
    ULONG Index;

    if (Count > ZP_CODEC_MAX_ELEMENT_COUNT || (Count != 0 && Records == NULL)) return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < Count; Index++)
    {
        if (Records[Index].JobId == 0 || Records[Index].FileNameLength > ZP_CODEC_MAX_ELEMENT_COUNT ||
            (Records[Index].FileNameLength != 0 && Records[Index].FileName == NULL))
        {
            return STATUS_INVALID_PARAMETER;
        }
        RequiredSize += 2 * sizeof(ULONGLONG) + 6 * sizeof(ULONG) + 3 * sizeof(BYTE) +
                        (ULONGLONG)Records[Index].FileNameLength * sizeof(WCHAR);
    }
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    Cursor = Buffer;
    ZpWire_WriteUInt32(&Cursor, Count);
    for (Index = 0; Index < Count; Index++) ZpExecution_WriteJob(&Cursor, &Records[Index]);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpExecution_DecodeJobs(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_EXECUTION_JOB_LIST_VIEW View)
{
    ZP_CODEC_READER Reader;
    ULONG Index;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadArrayCount(&Reader, &View->Count);
    if (!NT_SUCCESS(Status)) return Status;
    View->Buffer = Add2Ptr(Payload, Reader.Offset);
    for (Index = 0; NT_SUCCESS(Status) && Index < View->Count; Index++) Status = ZpExecution_ReadJob(&Reader, NULL);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength) return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    View->Length = PayloadLength - sizeof(ULONG);
    return STATUS_SUCCESS;
}

NTSTATUS
ZpExecution_GetNextJob(
    _In_ PCZP_EXECUTION_JOB_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_EXECUTION_JOB_RECORD_VIEW Record)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    if (*Offset >= List->Length) return STATUS_INVALID_PARAMETER;
    ZpCodec_InitializeReader(&Reader, Add2Ptr(List->Buffer, *Offset), List->Length - *Offset);
    Status = ZpExecution_ReadJob(&Reader, Record);
    if (NT_SUCCESS(Status)) *Offset += Reader.Offset;
    return Status;
}

NTSTATUS
ZpExecution_EncodeJobId(
    _In_ ULONG JobId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;

    if (JobId == 0) return STATUS_INVALID_PARAMETER;
    *BytesWritten = sizeof(ULONG);
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < sizeof(ULONG)) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteUInt32(&Writer, JobId);
}

NTSTATUS
ZpExecution_DecodeJobId(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG JobId)
{
    ZP_CODEC_READER Reader;

    if (PayloadLength != sizeof(ULONG)) return STATUS_DATA_ERROR;
    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    if (!NT_SUCCESS(ZpCodec_ReadUInt32(&Reader, JobId)) || *JobId == 0) return STATUS_DATA_ERROR;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpExecution_EncodeStaging(
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten)
{
    ZP_CODEC_WRITER Writer;
    ULONGLONG RequiredSize;

    if (NameLength == 0 || Name == NULL || NameLength > ZP_CODEC_MAX_ELEMENT_COUNT) return STATUS_INVALID_PARAMETER;
    RequiredSize = sizeof(ULONG) + (ULONGLONG)NameLength * sizeof(WCHAR);
    if (RequiredSize > ZP_RESPONSE_MAX_PAYLOAD_SIZE) return STATUS_BUFFER_OVERFLOW;
    *BytesWritten = (ULONG)RequiredSize;
    if (Buffer == NULL) return STATUS_SUCCESS;
    if (BufferSize < RequiredSize) return STATUS_BUFFER_TOO_SMALL;
    ZpCodec_InitializeWriter(&Writer, Buffer, BufferSize);
    return ZpCodec_WriteString(&Writer, Name, NameLength);
}

NTSTATUS
ZpExecution_DecodeStaging(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_STRING_VIEW Name)
{
    ZP_CODEC_READER Reader;
    NTSTATUS Status;

    ZpCodec_InitializeReader(&Reader, Payload, PayloadLength);
    Status = ZpCodec_ReadString(&Reader, Name);
    if (!NT_SUCCESS(Status) || Reader.Offset != PayloadLength || Name->Length == 0)
    {
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }
    return STATUS_SUCCESS;
}
