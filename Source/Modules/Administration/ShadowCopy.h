#pragma once

#include <KNSoft/ZPigeon/Administration.h>

EXTERN_C_START

ZP_STATUS
ZpAdministration_EnumerateSystemProtection(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength);

ZP_STATUS
ZpAdministration_EnumerateRestorePoints(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength);

ZP_STATUS
ZpAdministration_EnumerateShadowCopies(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength);

ZP_STATUS
ZpAdministration_ControlSystemProtection(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control);

ZP_STATUS
ZpAdministration_ControlRestorePoint(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control);

ZP_STATUS
ZpAdministration_ControlShadowCopy(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control);

EXTERN_C_END
