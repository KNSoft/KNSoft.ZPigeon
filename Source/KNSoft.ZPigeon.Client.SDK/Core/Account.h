#pragma once

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

NTSTATUS
ZpAccount_QuerySidName(
    _In_ PSID Sid,
    _Outptr_ PUNICODE_STRING* AccountName);

NTSTATUS
ZpAccount_QueryTokenName(
    _In_ HANDLE Token,
    _Outptr_ PUNICODE_STRING* AccountName);
