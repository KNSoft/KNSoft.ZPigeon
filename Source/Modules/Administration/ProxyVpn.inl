#include <ras.h>
#include <raserror.h>
#include <winhttp.h>
#include <wininet.h>

#pragma comment(lib, "Rasapi32.lib")
#pragma comment(lib, "Winhttp.lib")
#pragma comment(lib, "Wininet.lib")

static
NTSTATUS
ZpProxy_JoinDetails(
    _In_reads_(Count) PCWSTR const* Values,
    _In_ ULONG Count,
    _Outptr_ PWSTR* Detail)
{
    PCWSTR Value;
    PWSTR Buffer, Cursor;
    SIZE_T Length = Count - 1, ValueLength;
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        Value = Values[Index] == NULL ? L"" : Values[Index];
        ValueLength = wcslen(Value);
        if (ValueLength > (MAXULONG_PTR / sizeof(WCHAR)) - Length - 1)
        {
            return STATUS_INTEGER_OVERFLOW;
        }
        Length += ValueLength;
    }
    Buffer = Mem_Alloc((Length + 1) * sizeof(WCHAR));
    if (Buffer == NULL) return STATUS_NO_MEMORY;
    Cursor = Buffer;
    for (Index = 0; Index < Count; Index++)
    {
        Value = Values[Index] == NULL ? L"" : Values[Index];
        ValueLength = wcslen(Value);
        RtlCopyMemory(Cursor, Value, ValueLength * sizeof(WCHAR));
        Cursor += ValueLength;
        if (Index + 1 != Count) *Cursor++ = L'\n';
    }
    *Cursor = UNICODE_NULL;
    *Detail = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpProxy_AddWinInet(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG Config;
    PCWSTR Values[3];
    PWSTR Detail;
    ULONG Flags = 0;
    NTSTATUS Status;

    if (!WinHttpGetIEProxyConfigForCurrentUser(&Config)) return NTSTATUS_FROM_WIN32(GetLastError());
    if (Config.fAutoDetect) Flags |= PROXY_TYPE_AUTO_DETECT;
    if (Config.lpszAutoConfigUrl != NULL) Flags |= PROXY_TYPE_AUTO_PROXY_URL;
    if (Config.lpszProxy != NULL) Flags |= PROXY_TYPE_PROXY;
    if ((Flags & PROXY_TYPE_PROXY) == 0) Flags |= PROXY_TYPE_DIRECT;
    Values[0] = Config.lpszProxy;
    Values[1] = Config.lpszProxyBypass;
    Values[2] = Config.lpszAutoConfigUrl;
    Status = ZpProxy_JoinDetails(Values, ARRAYSIZE(Values), &Detail);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindProxy,
                                             (Flags & (PROXY_TYPE_PROXY | PROXY_TYPE_AUTO_PROXY_URL |
                                                       PROXY_TYPE_AUTO_DETECT)) != 0,
                                             Flags,
                                             0,
                                             L"wininet",
                                             NULL,
                                             NULL,
                                             Detail);
        Mem_Free(Detail);
    }
    GlobalFree(Config.lpszAutoConfigUrl);
    GlobalFree(Config.lpszProxy);
    GlobalFree(Config.lpszProxyBypass);
    return Status;
}

static
NTSTATUS
ZpProxy_AddWinHttp(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    WINHTTP_PROXY_INFO Config;
    PCWSTR Values[2];
    PWSTR Detail;
    NTSTATUS Status;

    if (!WinHttpGetDefaultProxyConfiguration(&Config)) return NTSTATUS_FROM_WIN32(GetLastError());
    Values[0] = Config.lpszProxy;
    Values[1] = Config.lpszProxyBypass;
    Status = ZpProxy_JoinDetails(Values, ARRAYSIZE(Values), &Detail);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindProxy,
                                             Config.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                                             Config.dwAccessType,
                                             0,
                                             L"winhttp",
                                             NULL,
                                             NULL,
                                             Detail);
        Mem_Free(Detail);
    }
    GlobalFree(Config.lpszProxy);
    GlobalFree(Config.lpszProxyBypass);
    return Status;
}

static
DWORD
ZpVpn_EnumerateConnections(
    _Outptr_result_buffer_(*Count) LPRASCONNW* Connections,
    _Out_ PULONG Count)
{
    LPRASCONNW Buffer = NULL;
    LPRASCONNW NewBuffer;
    DWORD Size = sizeof(RASCONNW), Result;

    Buffer = Mem_Alloc(Size);
    if (Buffer == NULL) return ERROR_NOT_ENOUGH_MEMORY;
    Buffer[0].dwSize = sizeof(RASCONNW);
    Result = RasEnumConnectionsW(Buffer, &Size, Count);
    if (Result == ERROR_BUFFER_TOO_SMALL)
    {
        NewBuffer = Mem_ReAlloc(Buffer, Size);
        if (NewBuffer == NULL)
        {
            Mem_Free(Buffer);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        Buffer = NewBuffer;
        Buffer[0].dwSize = sizeof(RASCONNW);
        Result = RasEnumConnectionsW(Buffer, &Size, Count);
    }
    if (Result == ERROR_SUCCESS) *Connections = Buffer;
    else Mem_Free(Buffer);
    return Result;
}

static
NTSTATUS
ZpVpn_AddEntries(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    LPRASENTRYNAMEW Entries = NULL;
    LPRASENTRYNAMEW NewEntries;
    LPRASCONNW Connections = NULL;
    DWORD Size = sizeof(RASENTRYNAMEW), Count = 0, ConnectionCount = 0, Result;
    ULONG Index, ConnectionIndex;
    BOOLEAN Active;
    NTSTATUS Status = STATUS_SUCCESS;

    Entries = Mem_Alloc(Size);
    if (Entries == NULL) return STATUS_NO_MEMORY;
    Entries[0].dwSize = sizeof(RASENTRYNAMEW);
    Result = RasEnumEntriesW(NULL, NULL, Entries, &Size, &Count);
    if (Result == ERROR_BUFFER_TOO_SMALL)
    {
        NewEntries = Mem_ReAlloc(Entries, Size);
        if (NewEntries == NULL)
        {
            Mem_Free(Entries);
            return STATUS_NO_MEMORY;
        }
        Entries = NewEntries;
        Entries[0].dwSize = sizeof(RASENTRYNAMEW);
        Result = RasEnumEntriesW(NULL, NULL, Entries, &Size, &Count);
    }
    if (Result != ERROR_SUCCESS)
    {
        Mem_Free(Entries);
        return NTSTATUS_FROM_WIN32(Result);
    }
    Result = ZpVpn_EnumerateConnections(&Connections, &ConnectionCount);
    if (Result != ERROR_SUCCESS)
    {
        Mem_Free(Entries);
        return NTSTATUS_FROM_WIN32(Result);
    }
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Active = FALSE;
        for (ConnectionIndex = 0; ConnectionIndex < ConnectionCount; ConnectionIndex++)
        {
            if (_wcsicmp(Entries[Index].szEntryName, Connections[ConnectionIndex].szEntryName) == 0)
            {
                Active = TRUE;
                break;
            }
        }
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindVpn,
                                             Active,
                                             0,
                                             0,
                                             Entries[Index].szEntryName,
                                             Entries[Index].szEntryName,
                                             NULL,
                                             NULL);
    }
    Mem_Free(Connections);
    Mem_Free(Entries);
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateProxyVpn(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    NTSTATUS Status = ZpProxy_AddWinInet(&Builder);

    if (NT_SUCCESS(Status)) Status = ZpProxy_AddWinHttp(&Builder);
    if (NT_SUCCESS(Status)) Status = ZpVpn_AddEntries(&Builder);
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
PWSTR
ZpProxy_NextValue(
    _Inout_ PWSTR* Cursor)
{
    PWSTR Value = *Cursor, End;

    if (Value == NULL) return NULL;
    End = wcschr(Value, L'\n');
    if (End == NULL) *Cursor = NULL;
    else
    {
        *End = UNICODE_NULL;
        *Cursor = End + 1;
    }
    return Value;
}

static
NTSTATUS
ZpProxy_ConfigureWinInet(
    _Inout_ PWSTR Argument)
{
    INTERNET_PER_CONN_OPTIONW Options[4];
    INTERNET_PER_CONN_OPTION_LISTW List = { sizeof(List) };
    PWSTR Cursor = Argument, FlagsText, Proxy, Bypass, AutoConfig;
    UNICODE_STRING Value;
    ULONG Flags;

    FlagsText = ZpProxy_NextValue(&Cursor);
    Proxy = ZpProxy_NextValue(&Cursor);
    Bypass = ZpProxy_NextValue(&Cursor);
    AutoConfig = ZpProxy_NextValue(&Cursor);
    if (FlagsText == NULL || Proxy == NULL || Bypass == NULL || AutoConfig == NULL || Cursor != NULL)
        return STATUS_INVALID_PARAMETER;
    RtlInitUnicodeString(&Value, FlagsText);
    if (!NT_SUCCESS(RtlUnicodeStringToInteger(&Value, 10, &Flags))) return STATUS_INVALID_PARAMETER;
    Options[0].dwOption = INTERNET_PER_CONN_FLAGS;
    Options[0].Value.dwValue = Flags;
    Options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
    Options[1].Value.pszValue = Proxy;
    Options[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
    Options[2].Value.pszValue = Bypass;
    Options[3].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
    Options[3].Value.pszValue = AutoConfig;
    List.dwOptionCount = ARRAYSIZE(Options);
    List.pOptions = Options;
    if (!InternetSetOptionW(NULL, INTERNET_OPTION_PER_CONNECTION_OPTION, &List, sizeof(List)) ||
        !InternetSetOptionW(NULL, INTERNET_OPTION_PROXY_SETTINGS_CHANGED, NULL, 0) ||
        !InternetSetOptionW(NULL, INTERNET_OPTION_REFRESH, NULL, 0))
    {
        return NTSTATUS_FROM_WIN32(GetLastError());
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpProxy_ConfigureWinHttp(
    _Inout_ PWSTR Argument)
{
    WINHTTP_PROXY_INFO Config;
    PWSTR Cursor = Argument, Proxy = ZpProxy_NextValue(&Cursor), Bypass = ZpProxy_NextValue(&Cursor);

    if (Proxy == NULL || Bypass == NULL || Cursor != NULL) return STATUS_INVALID_PARAMETER;
    Config.dwAccessType = *Proxy == UNICODE_NULL ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_NAMED_PROXY;
    Config.lpszProxy = Proxy;
    Config.lpszProxyBypass = Bypass;
    return WinHttpSetDefaultProxyConfiguration(&Config) ? STATUS_SUCCESS : NTSTATUS_FROM_WIN32(GetLastError());
}

static
NTSTATUS
ZpVpn_Control(
    _In_ PCWSTR Name,
    _In_ ZP_ADMINISTRATION_ACTION Action)
{
    LPRASCONNW Connections;
    ULONG Count, Index;
    DWORD Result;

    if (Action == ZpAdministrationActionDelete)
        return NTSTATUS_FROM_WIN32(RasDeleteEntryW(NULL, Name));
    if (Action != ZpAdministrationActionDisconnect) return STATUS_NOT_SUPPORTED;
    Result = ZpVpn_EnumerateConnections(&Connections, &Count);
    if (Result != ERROR_SUCCESS) return NTSTATUS_FROM_WIN32(Result);
    Result = ERROR_NOT_FOUND;
    for (Index = 0; Index < Count; Index++)
    {
        if (_wcsicmp(Name, Connections[Index].szEntryName) == 0)
        {
            Result = RasHangUpW(Connections[Index].hrasconn);
            break;
        }
    }
    Mem_Free(Connections);
    return NTSTATUS_FROM_WIN32(Result);
}

static
ZP_STATUS
ZpAdministration_ControlProxyVpn(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PWSTR Identity = ZpAdministration_CopyView(&Control->Identity), Argument = NULL;
    NTSTATUS Status;

    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    if (Control->Secret.Length != 0)
    {
        Mem_Free(Identity);
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    if (_wcsicmp(Identity, L"wininet") == 0 || _wcsicmp(Identity, L"winhttp") == 0)
    {
        if (Control->Action != ZpAdministrationActionConfigure)
        {
            Status = STATUS_NOT_SUPPORTED;
        }
        else
        {
            Argument = ZpAdministration_CopyView(&Control->Argument);
            if (Argument == NULL) Status = STATUS_NO_MEMORY;
            else Status = _wcsicmp(Identity, L"wininet") == 0 ?
                              ZpProxy_ConfigureWinInet(Argument) : ZpProxy_ConfigureWinHttp(Argument);
        }
    }
    else if (Control->Argument.Length != 0)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        Status = ZpVpn_Control(Identity, Control->Action);
    }
    Mem_Free(Argument);
    Mem_Free(Identity);
    return ZpStatus_FromNtStatus(Status);
}
