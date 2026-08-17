#include "Account.h"

#pragma comment(lib, "KNSoft.NDK.WinAPI.lib")

DECLSPEC_IMPORT
BOOL
WINAPI
LookupAccountSidLocalW(
    _In_ PSID Sid,
    _Out_writes_to_opt_(*NameLength, *NameLength) PWSTR Name,
    _Inout_ PULONG NameLength,
    _Out_writes_to_opt_(*DomainNameLength, *DomainNameLength) PWSTR DomainName,
    _Inout_ PULONG DomainNameLength,
    _Out_ PSID_NAME_USE Use);

NTSTATUS
ZpAccount_QuerySidName(
    _In_ PSID Sid,
    _Outptr_ PUNICODE_STRING* AccountName)
{
    PUNICODE_STRING Value;
    SID_NAME_USE Use;
    ULONG NameLength = 0, DomainLength = 0, Length;
    PWCHAR Name, Domain;

    if (LookupAccountSidLocalW(Sid, NULL, &NameLength, NULL, &DomainLength, &Use) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || NameLength == 0)
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    Length = NameLength - 1 + (DomainLength > 1 ? DomainLength : 0);
    if (Length > (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR))
    {
        return STATUS_NAME_TOO_LONG;
    }
    Value = NT_AllocStringW((USHORT)Length);
    if (Value == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    if (DomainLength > 1)
    {
        Domain = Value->Buffer;
        Name = Domain + DomainLength;
    }
    else
    {
        Domain = NULL;
        Name = Value->Buffer;
        DomainLength = 0;
    }
    if (!LookupAccountSidLocalW(Sid, Name, &NameLength, Domain, &DomainLength, &Use))
    {
        Length = GetLastError();
        NT_FreeStringW(Value);
        return NTSTATUS_FROM_WIN32(Length);
    }
    if (Domain != NULL)
    {
        Name[-1] = L'\\';
    }
    *AccountName = Value;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpAccount_QueryTokenName(
    _In_ HANDLE Token,
    _Outptr_ PUNICODE_STRING* AccountName)
{
    PTOKEN_USER User;
    NTSTATUS Status;

    Status = PS_GetTokenInfo(Token, TokenUser, (PVOID*)&User);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAccount_QuerySidName(User->User.Sid, AccountName);
        Mem_Free(User);
    }
    return Status;
}
