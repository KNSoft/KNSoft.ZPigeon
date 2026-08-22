#include "Protocol.inl"

static
NTSTATUS
ZpCodec_WriteRaw(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_reads_bytes_opt_(Length) const VOID* Data,
    _In_ ULONG Length)
{
    ULONG EndOffset;

    if (Length != 0 && Data == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Writer->Offset > MAXULONG - Length)
    {
        return STATUS_INTEGER_OVERFLOW;
    }
    EndOffset = Writer->Offset + Length;
    if (Writer->Buffer != NULL)
    {
        if (EndOffset > Writer->Size)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (Length != 0)
        {
            RtlCopyMemory(Writer->Buffer + Writer->Offset, Data, Length);
        }
    }
    Writer->Offset = EndOffset;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpCodec_ReadRaw(
    _Inout_ PZP_CODEC_READER Reader,
    _In_ ULONG Length,
    _Out_ const BYTE** Data)
{
    ULONG EndOffset;

    if (Reader->Offset > MAXULONG - Length)
    {
        return STATUS_DATA_ERROR;
    }
    EndOffset = Reader->Offset + Length;
    if (EndOffset > Reader->Size)
    {
        return STATUS_DATA_ERROR;
    }
    *Data = Reader->Buffer + Reader->Offset;
    Reader->Offset = EndOffset;
    return STATUS_SUCCESS;
}

VOID
ZpCodec_InitializeWriter(
    _Out_ PZP_CODEC_WRITER Writer,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize)
{
    Writer->Buffer = Buffer;
    Writer->Size = BufferSize;
    Writer->Offset = 0;
}

VOID
ZpCodec_InitializeReader(
    _Out_ PZP_CODEC_READER Reader,
    _In_reads_bytes_(BufferSize) const VOID* Buffer,
    _In_ ULONG BufferSize)
{
    Reader->Buffer = Buffer;
    Reader->Size = BufferSize;
    Reader->Offset = 0;
}

NTSTATUS
ZpCodec_WriteByte(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ BYTE Value)
{
    return ZpCodec_WriteRaw(Writer, &Value, sizeof(Value));
}

NTSTATUS
ZpCodec_WriteBoolean(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ BOOLEAN Value)
{
    if (Value > TRUE)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return ZpCodec_WriteByte(Writer, Value);
}

NTSTATUS
ZpCodec_WriteUInt16(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ USHORT Value)
{
    BYTE Buffer[sizeof(Value)];

    ZpWriteUInt16(Buffer, Value);
    return ZpCodec_WriteRaw(Writer, Buffer, sizeof(Buffer));
}

NTSTATUS
ZpCodec_WriteUInt32(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ ULONG Value)
{
    BYTE Buffer[sizeof(Value)];

    ZpWriteUInt32(Buffer, Value);
    return ZpCodec_WriteRaw(Writer, Buffer, sizeof(Buffer));
}

NTSTATUS
ZpCodec_WriteUInt64(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ ULONGLONG Value)
{
    BYTE Buffer[sizeof(Value)];

    ZpWriteUInt64(Buffer, Value);
    return ZpCodec_WriteRaw(Writer, Buffer, sizeof(Buffer));
}

NTSTATUS
ZpCodec_WriteData(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_reads_bytes_opt_(Length) const VOID* Data,
    _In_ ULONG Length)
{
    return ZpCodec_WriteRaw(Writer, Data, Length);
}

NTSTATUS
ZpCodec_WriteByteString(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_reads_bytes_opt_(Length) const VOID* Data,
    _In_ ULONG Length)
{
    NTSTATUS Status;
    ULONG Offset;

    if (Length > ZP_CODEC_MAX_ELEMENT_COUNT)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    Offset = Writer->Offset;
    Status = ZpCodec_WriteUInt32(Writer, Length);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpCodec_WriteRaw(Writer, Data, Length);
    if (!NT_SUCCESS(Status))
    {
        Writer->Offset = Offset;
    }
    return Status;
}

NTSTATUS
ZpCodec_WriteString(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_reads_opt_(Length) PCWCH String,
    _In_ ULONG Length)
{
    NTSTATUS Status;
    ULONG ByteLength, Offset;

    if (Length > ZP_CODEC_MAX_ELEMENT_COUNT)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    ByteLength = Length * sizeof(WCHAR);
    Offset = Writer->Offset;
    Status = ZpCodec_WriteUInt32(Writer, Length);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpCodec_WriteRaw(Writer, String, ByteLength);
    if (!NT_SUCCESS(Status))
    {
        Writer->Offset = Offset;
    }
    return Status;
}

NTSTATUS
ZpCodec_WriteArrayCount(
    _Inout_ PZP_CODEC_WRITER Writer,
    _In_ ULONG Count)
{
    if (Count > ZP_CODEC_MAX_ELEMENT_COUNT)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    return ZpCodec_WriteUInt32(Writer, Count);
}

NTSTATUS
ZpCodec_ReadByte(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PBYTE Value)
{
    NTSTATUS Status;
    const BYTE* Data;

    Status = ZpCodec_ReadRaw(Reader, sizeof(*Value), &Data);
    if (NT_SUCCESS(Status))
    {
        *Value = *Data;
    }
    return Status;
}

NTSTATUS
ZpCodec_ReadBoolean(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PBOOLEAN Value)
{
    NTSTATUS Status;
    BYTE EncodedValue;

    Status = ZpCodec_ReadByte(Reader, &EncodedValue);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (EncodedValue > TRUE)
    {
        return STATUS_DATA_ERROR;
    }
    *Value = EncodedValue;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpCodec_ReadUInt16(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PUSHORT Value)
{
    NTSTATUS Status;
    const BYTE* Data;

    Status = ZpCodec_ReadRaw(Reader, sizeof(*Value), &Data);
    if (NT_SUCCESS(Status))
    {
        *Value = ZpReadUInt16(Data);
    }
    return Status;
}

NTSTATUS
ZpCodec_ReadUInt32(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PULONG Value)
{
    NTSTATUS Status;
    const BYTE* Data;

    Status = ZpCodec_ReadRaw(Reader, sizeof(*Value), &Data);
    if (NT_SUCCESS(Status))
    {
        *Value = ZpReadUInt32(Data);
    }
    return Status;
}

NTSTATUS
ZpCodec_ReadUInt64(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PULONGLONG Value)
{
    NTSTATUS Status;
    const BYTE* Data;

    Status = ZpCodec_ReadRaw(Reader, sizeof(*Value), &Data);
    if (NT_SUCCESS(Status))
    {
        *Value = ZpReadUInt64(Data);
    }
    return Status;
}

NTSTATUS
ZpCodec_ReadData(
    _Inout_ PZP_CODEC_READER Reader,
    _In_ ULONG Length,
    _Out_ PZP_BUFFER_VIEW View)
{
    NTSTATUS Status;
    const BYTE* Data;

    Status = ZpCodec_ReadRaw(Reader, Length, &Data);
    if (NT_SUCCESS(Status))
    {
        View->Buffer = Data;
        View->Length = Length;
    }
    return Status;
}

NTSTATUS
ZpCodec_ReadByteString(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PZP_BUFFER_VIEW View)
{
    NTSTATUS Status;
    ULONG Length;

    Status = ZpCodec_ReadUInt32(Reader, &Length);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Length > ZP_CODEC_MAX_ELEMENT_COUNT)
    {
        return STATUS_DATA_ERROR;
    }
    return ZpCodec_ReadData(Reader, Length, View);
}

NTSTATUS
ZpCodec_ReadString(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PZP_STRING_VIEW View)
{
    NTSTATUS Status;
    ULONG Length;
    ZP_BUFFER_VIEW BufferView;

    Status = ZpCodec_ReadUInt32(Reader, &Length);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Length > ZP_CODEC_MAX_ELEMENT_COUNT)
    {
        return STATUS_DATA_ERROR;
    }
    Status = ZpCodec_ReadData(Reader, Length * sizeof(WCHAR), &BufferView);
    if (NT_SUCCESS(Status))
    {
        View->Buffer = (PCWCH)BufferView.Buffer;
        View->Length = Length;
    }
    return Status;
}

NTSTATUS
ZpCodec_ReadArrayCount(
    _Inout_ PZP_CODEC_READER Reader,
    _Out_ PULONG Count)
{
    NTSTATUS Status;
    ULONG Value;

    Status = ZpCodec_ReadUInt32(Reader, &Value);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Value > ZP_CODEC_MAX_ELEMENT_COUNT)
    {
        return STATUS_DATA_ERROR;
    }
    *Count = Value;
    return STATUS_SUCCESS;
}
