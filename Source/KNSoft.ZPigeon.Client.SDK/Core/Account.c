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

DECLSPEC_IMPORT
BOOL
WINAPI
LookupAccountNameLocalW(
    _In_ PCWSTR AccountName,
    _Out_writes_bytes_to_opt_(*SidLength, *SidLength) PSID Sid,
    _Inout_ PULONG SidLength,
    _Out_writes_to_opt_(*DomainNameLength, *DomainNameLength) PWSTR DomainName,
    _Inout_ PULONG DomainNameLength,
    _Out_ PSID_NAME_USE Use);

DECLSPEC_IMPORT
BOOL
WINAPI
ConvertSidToStringSidW(
    _In_ PSID Sid,
    _Out_ PWSTR* StringSid);

DECLSPEC_IMPORT
BOOL
WINAPI
ConvertStringSidToSidW(
    _In_ PCWSTR StringSid,
    _Out_ PSID* Sid);

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
ZpAccount_QueryNameSid(
    _In_ PCWSTR AccountName,
    _Outptr_ PUNICODE_STRING* SidString)
{
    PUNICODE_STRING Value;
    SID_NAME_USE Use;
    PSID Sid;
    PWSTR String;
    ULONG SidLength = 0, DomainLength = 0, Length, Error;

    if (LookupAccountNameLocalW(AccountName, NULL, &SidLength, NULL, &DomainLength, &Use) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    Sid = Mem_Alloc(SidLength + (SIZE_T)DomainLength * sizeof(WCHAR));
    if (Sid == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    if (!LookupAccountNameLocalW(AccountName,
                                 Sid,
                                 &SidLength,
                                 (PWSTR)Add2Ptr(Sid, SidLength),
                                 &DomainLength,
                                 &Use))
    {
        Error = GetLastError();
        Mem_Free(Sid);
        return NTSTATUS_FROM_WIN32(Error);
    }
    if (!ConvertSidToStringSidW(Sid, &String))
    {
        Error = GetLastError();
        Mem_Free(Sid);
        return NTSTATUS_FROM_WIN32(Error);
    }
    Mem_Free(Sid);
    Length = (ULONG)wcslen(String);
    Value = Length <= (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR) ?
                NT_AllocStringW((USHORT)Length) : NULL;
    if (Value != NULL)
    {
        RtlCopyMemory(Value->Buffer, String, (Length + 1) * sizeof(WCHAR));
    }
    LocalFree(String);
    if (Value == NULL)
    {
        return Length > (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR) ?
                   STATUS_NAME_TOO_LONG : STATUS_NO_MEMORY;
    }
    *SidString = Value;
    return STATUS_SUCCESS;
}

NTSTATUS
ZpAccount_QueryStringSidName(
    _In_ PCWSTR SidString,
    _Outptr_ PUNICODE_STRING* AccountName)
{
    PSID Sid;
    NTSTATUS Status;

    if (!ConvertStringSidToSidW(SidString, &Sid))
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    Status = ZpAccount_QuerySidName(Sid, AccountName);
    LocalFree(Sid);
    return Status;
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
