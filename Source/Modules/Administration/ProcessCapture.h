#pragma once

#include <Windows.h>

EXTERN_C_START

NTSTATUS
ZpAdministration_RunProcess(
    _In_ PCWSTR Application,
    _In_reads_(ArgumentCount) PCWSTR const* Arguments,
    _In_ ULONG ArgumentCount,
    _In_ ULONG MaximumOutputLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ BOOLEAN CaptureStandardError,
    _Outptr_result_bytebuffer_maybenull_(*OutputLength) PBYTE* Output,
    _Out_ PULONG OutputLength,
    _Out_ PULONG ExitCode);

EXTERN_C_END
