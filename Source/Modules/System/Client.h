#pragma once

#include <KNSoft/ZPigeon/System.h>

ZP_STATUS
ZpSystem_ExecuteInfo(
    _Out_writes_bytes_to_(BufferSize, *PayloadLength) PBYTE Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG PayloadLength);
