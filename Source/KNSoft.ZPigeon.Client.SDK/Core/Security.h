#pragma once

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

NTSTATUS
ZpSecurity_QueryDacl(
    _In_ HANDLE Object,
    _Outptr_ PUNICODE_STRING* Sddl);

NTSTATUS
ZpSecurity_SetDacl(
    _In_ HANDLE Object,
    _In_ PCWSTR Sddl);
