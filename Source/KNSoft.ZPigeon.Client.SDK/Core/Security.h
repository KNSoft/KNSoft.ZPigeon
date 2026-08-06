#pragma once

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <aclapi.h>

NTSTATUS
ZpSecurity_QueryDacl(
    _In_ HANDLE Object,
    _Outptr_ PUNICODE_STRING* Sddl,
    _Out_ PBOOLEAN DaclProtected);

NTSTATUS
ZpSecurity_SetDacl(
    _In_ HANDLE Object,
    _In_ SE_OBJECT_TYPE ObjectType,
    _In_ PCWSTR Sddl,
    _In_ BOOLEAN DaclProtected);
