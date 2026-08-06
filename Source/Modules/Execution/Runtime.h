#pragma once

#include <KNSoft/ZPigeon/Execution.h>

#ifdef __cplusplus

#include <string>

namespace ZpRuntime
{
    bool IsExistingFile(const std::wstring& path);
    std::wstring FindPathExecutable(PCWSTR fileName, bool rejectPythonAlias = false);
    std::wstring FindPython();
}

#endif

EXTERN_C_START

typedef BOOL (NTAPI *ZP_RUNTIME_CALLBACK)(
    _In_ BYTE Kind,
    _In_ PCWSTR Path,
    _In_ PCZP_EXECUTION_IMAGE_INFO Image,
    _In_opt_ PVOID Context);

HRESULT
ZpRuntime_Enumerate(
    _In_ ZP_RUNTIME_CALLBACK Callback,
    _In_opt_ PVOID Context);

HRESULT
ZpRuntime_QueryImage(
    _In_ PCWSTR Path,
    _Out_ PZP_EXECUTION_IMAGE_INFO Image);

EXTERN_C_END
