#pragma once

#include <KNSoft/ZPigeon/Execution.h>

EXTERN_C_START

ZP_STATUS
ZpProcess_Launch(
    _In_ PCZP_EXECUTION_START_VIEW Start,
    _In_opt_ HPCON PseudoConsole,
    _In_opt_ HANDLE Job,
    _Out_ PPROCESS_INFORMATION ProcessInformation,
    _Out_ PULONG SessionId);

NTSTATUS
ZpProcess_TerminateTree(
    _In_ HANDLE Process,
    _In_ ULONG ProcessId,
    _In_ LONGLONG CreateTime,
    _In_ NTSTATUS ExitStatus);

EXTERN_C_END
