#pragma once

#include "Include/KNSoft/ZPigeon/Protocol.h"

FORCEINLINE
USHORT
ZpReadUInt16(
    _In_reads_bytes_(sizeof(USHORT)) const BYTE* Buffer)
{
    return (USHORT)(Buffer[0] | ((USHORT)Buffer[1] << 8));
}

FORCEINLINE
ULONG
ZpReadUInt32(
    _In_reads_bytes_(sizeof(ULONG)) const BYTE* Buffer)
{
    return (ULONG)Buffer[0] |
           ((ULONG)Buffer[1] << 8) |
           ((ULONG)Buffer[2] << 16) |
           ((ULONG)Buffer[3] << 24);
}

FORCEINLINE
ULONGLONG
ZpReadUInt64(
    _In_reads_bytes_(sizeof(ULONGLONG)) const BYTE* Buffer)
{
    return (ULONGLONG)ZpReadUInt32(Buffer) | ((ULONGLONG)ZpReadUInt32(Buffer + sizeof(ULONG)) << 32);
}

FORCEINLINE
VOID
ZpWriteUInt16(
    _Out_writes_bytes_(sizeof(USHORT)) PBYTE Buffer,
    _In_ USHORT Value)
{
    Buffer[0] = (BYTE)Value;
    Buffer[1] = (BYTE)(Value >> 8);
}

FORCEINLINE
VOID
ZpWriteUInt32(
    _Out_writes_bytes_(sizeof(ULONG)) PBYTE Buffer,
    _In_ ULONG Value)
{
    Buffer[0] = (BYTE)Value;
    Buffer[1] = (BYTE)(Value >> 8);
    Buffer[2] = (BYTE)(Value >> 16);
    Buffer[3] = (BYTE)(Value >> 24);
}

FORCEINLINE
VOID
ZpWriteUInt64(
    _Out_writes_bytes_(sizeof(ULONGLONG)) PBYTE Buffer,
    _In_ ULONGLONG Value)
{
    ZpWriteUInt32(Buffer, (ULONG)Value);
    ZpWriteUInt32(Buffer + sizeof(ULONG), (ULONG)(Value >> 32));
}

FORCEINLINE
VOID
ZpWire_WriteByte(
    _Inout_ PBYTE* Cursor,
    _In_ BYTE Value)
{
    **Cursor = Value;
    *Cursor += sizeof(Value);
}

FORCEINLINE
VOID
ZpWire_WriteUInt16(
    _Inout_ PBYTE* Cursor,
    _In_ USHORT Value)
{
    ZpWriteUInt16(*Cursor, Value);
    *Cursor += sizeof(Value);
}

FORCEINLINE
VOID
ZpWire_WriteUInt32(
    _Inout_ PBYTE* Cursor,
    _In_ ULONG Value)
{
    ZpWriteUInt32(*Cursor, Value);
    *Cursor += sizeof(Value);
}

FORCEINLINE
VOID
ZpWire_WriteUInt64(
    _Inout_ PBYTE* Cursor,
    _In_ ULONGLONG Value)
{
    ZpWriteUInt64(*Cursor, Value);
    *Cursor += sizeof(Value);
}

FORCEINLINE
VOID
ZpWire_WriteData(
    _Inout_ PBYTE* Cursor,
    _In_reads_bytes_opt_(Length) const VOID* Data,
    _In_ ULONG Length)
{
    if (Length != 0)
    {
        RtlCopyMemory(*Cursor, Data, Length);
        *Cursor += Length;
    }
}

FORCEINLINE
VOID
ZpWire_WriteByteString(
    _Inout_ PBYTE* Cursor,
    _In_reads_bytes_opt_(Length) const VOID* Data,
    _In_ ULONG Length)
{
    ZpWire_WriteUInt32(Cursor, Length);
    ZpWire_WriteData(Cursor, Data, Length);
}

FORCEINLINE
VOID
ZpWire_WriteString(
    _Inout_ PBYTE* Cursor,
    _In_reads_opt_(Length) PCWCH String,
    _In_ ULONG Length)
{
    ZpWire_WriteUInt32(Cursor, Length);
    ZpWire_WriteData(Cursor, String, Length * sizeof(WCHAR));
}

NTSTATUS
ZpMessage_ValidateBody(
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const BYTE* Body,
    _In_ ULONG BodyLength);
