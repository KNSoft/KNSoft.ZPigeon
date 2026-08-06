#include <iphlpapi.h>

#pragma comment(lib, "Iphlpapi.lib")

#define ZP_NETWORK_ADAPTER_HIDDEN 0x80000000

static
NTSTATUS
ZpNetwork_FormatAddress(
    _In_ const SOCKADDR_INET* Address,
    _In_ USHORT Port,
    _Out_writes_(CharacterCount) PWSTR Buffer,
    _In_ ULONG CharacterCount)
{
    switch (Address->si_family)
    {
        case AF_INET:
            return RtlIpv4AddressToStringExW(&Address->Ipv4.sin_addr,
                                             Port,
                                             Buffer,
                                             &CharacterCount);

        case AF_INET6:
            return RtlIpv6AddressToStringExW(&Address->Ipv6.sin6_addr,
                                             Address->Ipv6.sin6_scope_id,
                                             Port,
                                             Buffer,
                                             &CharacterCount);

        default:
            return STATUS_INVALID_ADDRESS_COMPONENT;
    }
}

static
NTSTATUS
ZpNetwork_FormatPrefix(
    _In_ const IP_ADDRESS_PREFIX* Prefix,
    _Out_writes_(CharacterCount) PWSTR Buffer,
    _In_ ULONG CharacterCount)
{
    NTSTATUS Status = ZpNetwork_FormatAddress(&Prefix->Prefix, 0, Buffer, CharacterCount);
    SIZE_T Length;

    if (!NT_SUCCESS(Status)) return Status;
    Length = wcslen(Buffer);
    return _snwprintf_s(Buffer + Length,
                        CharacterCount - Length,
                        _TRUNCATE,
                        L"/%u",
                        Prefix->PrefixLength) < 0 ?
               STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}

static
VOID
ZpNetwork_FormatPhysicalAddress(
    _In_ const MIB_IF_ROW2* Row,
    _Out_writes_(CharacterCount) PWSTR Buffer,
    _In_ ULONG CharacterCount)
{
    ULONG Index;
    PWSTR Cursor = Buffer;

    for (Index = 0; Index < Row->PhysicalAddressLength && CharacterCount >= 3; Index++)
    {
        if (Index != 0)
        {
            *Cursor++ = L'-';
            CharacterCount--;
        }
        _snwprintf_s(Cursor, CharacterCount, _TRUNCATE, L"%02X", Row->PhysicalAddress[Index]);
        Cursor += 2;
        CharacterCount -= 2;
    }
    *Cursor = UNICODE_NULL;
}

static
NTSTATUS
ZpNetwork_AddAdapter(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ const MIB_IF_ROW2* Row,
    _In_ LONG Category)
{
    WCHAR Identity[16], Physical[IF_MAX_PHYS_ADDRESS_LENGTH * 3], Detail[384];

    _ultow_s(Row->InterfaceIndex, Identity, ARRAYSIZE(Identity), 10);
    ZpNetwork_FormatPhysicalAddress(Row, Physical, ARRAYSIZE(Physical));
    _snwprintf_s(Detail,
                 ARRAYSIZE(Detail),
                 _TRUNCATE,
                 L"%s\n%lu\n%llu\n%llu\n%llu\n%llu\n%llu\n%llu\n%u\n%u\n%ld",
                 Physical,
                 Row->Mtu,
                 Row->ReceiveLinkSpeed,
                 Row->InOctets,
                 Row->OutOctets,
                 Row->InErrors,
                 Row->OutErrors,
                 Row->OutQLen,
                 Row->MediaType,
                 Row->PhysicalMediumType,
                 Category);
    return ZpAdministration_AddRecord(Builder,
                                      ZpAdministrationKindNetworkAdapter,
                                      Row->OperStatus | ((ULONG)Row->AdminStatus << 16) |
                                          (Category == -2 ? ZP_NETWORK_ADAPTER_HIDDEN : 0),
                                      Row->Type,
                                      Row->TransmitLinkSpeed,
                                      Identity,
                                      Row->Alias,
                                      Row->Description,
                                      Detail);
}

static
NTSTATUS
ZpNetwork_AddAdapterAddress(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ const MIB_UNICASTIPADDRESS_ROW* Row)
{
    WCHAR Identity[16], Address[80];
    NTSTATUS Status;

    Status = ZpNetwork_FormatAddress(&Row->Address, 0, Address, ARRAYSIZE(Address));
    if (!NT_SUCCESS(Status)) return Status;
    _ultow_s(Row->InterfaceIndex, Identity, ARRAYSIZE(Identity), 10);
    return ZpAdministration_AddRecord(Builder,
                                      ZpAdministrationKindNetworkAdapterAddress,
                                      Row->Address.si_family,
                                      Row->PrefixOrigin | ((ULONG)Row->SuffixOrigin << 8) |
                                          ((ULONG)Row->DadState << 16),
                                      Row->OnLinkPrefixLength,
                                      Identity,
                                      Address,
                                      NULL,
                                      NULL);
}

static
VOID
ZpNetwork_GetAdapterCategories(
    _In_ const MIB_IF_TABLE2* Interfaces,
    _Out_writes_(Interfaces->NumEntries) PLONG Categories)
{
    INetworkListManager* Manager = NULL;
    IEnumNetworkConnections* Connections = NULL;
    INetworkConnection* Connection = NULL;
    INetwork* Network = NULL;
    INetConnectionManager* ConnectionManager = NULL;
    IEnumNetConnection* NetConnections = NULL;
    INetConnection* NetConnection = NULL;
    NETCON_PROPERTIES* Properties = NULL;
    NLM_NETWORK_CATEGORY Category;
    GUID Adapter;
    HRESULT Result, ItemResult, InitializeResult;
    ULONG Index, Fetched;

    for (Index = 0; Index < Interfaces->NumEntries; Index++) Categories[Index] = -1;
    InitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(InitializeResult)) return;
    Result = CoCreateInstance(&CLSID_ConnectionManager,
                              NULL,
                              CLSCTX_LOCAL_SERVER,
                              &IID_INetConnectionManager,
                              (PVOID*)&ConnectionManager);
    if (SUCCEEDED(Result))
    {
        Result = INetConnectionManager_EnumConnections(ConnectionManager,
                                                        NCME_DEFAULT,
                                                        &NetConnections);
    }
    if (SUCCEEDED(Result))
    {
        for (Index = 0; Index < Interfaces->NumEntries; Index++) Categories[Index] = -2;
        while (IEnumNetConnection_Next(NetConnections, 1, &NetConnection, &Fetched) == S_OK)
        {
            ItemResult = INetConnection_GetProperties(NetConnection, &Properties);
            if (SUCCEEDED(ItemResult))
            {
                for (Index = 0; Index < Interfaces->NumEntries; Index++)
                {
                    if (IsEqualGUID(&Properties->guidId, &Interfaces->Table[Index].InterfaceGuid))
                    {
                        Categories[Index] = -1;
                        break;
                    }
                }
                CoTaskMemFree(Properties->pszwName);
                CoTaskMemFree(Properties->pszwDeviceName);
                CoTaskMemFree(Properties);
                Properties = NULL;
            }
            INetConnection_Release(NetConnection);
            NetConnection = NULL;
        }
    }
    if (Properties != NULL)
    {
        CoTaskMemFree(Properties->pszwName);
        CoTaskMemFree(Properties->pszwDeviceName);
        CoTaskMemFree(Properties);
    }
    if (NetConnection != NULL) INetConnection_Release(NetConnection);
    if (NetConnections != NULL) IEnumNetConnection_Release(NetConnections);
    if (ConnectionManager != NULL) INetConnectionManager_Release(ConnectionManager);
    Result = CoCreateInstance(&CLSID_NetworkListManager,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_INetworkListManager,
                              (PVOID*)&Manager);
    if (SUCCEEDED(Result)) Result = INetworkListManager_GetNetworkConnections(Manager, &Connections);
    while (SUCCEEDED(Result) && IEnumNetworkConnections_Next(Connections, 1, &Connection, &Fetched) == S_OK)
    {
        ItemResult = INetworkConnection_GetAdapterId(Connection, &Adapter);
        if (SUCCEEDED(ItemResult)) ItemResult = INetworkConnection_GetNetwork(Connection, &Network);
        if (SUCCEEDED(ItemResult)) ItemResult = INetwork_GetCategory(Network, &Category);
        if (SUCCEEDED(ItemResult))
        {
            for (Index = 0; Index < Interfaces->NumEntries; Index++)
            {
                if (IsEqualGUID(&Adapter, &Interfaces->Table[Index].InterfaceGuid))
                {
                    Categories[Index] = Category;
                    break;
                }
            }
        }
        if (Network != NULL) INetwork_Release(Network);
        Network = NULL;
        INetworkConnection_Release(Connection);
        Connection = NULL;
    }
    if (Connection != NULL) INetworkConnection_Release(Connection);
    if (Network != NULL) INetwork_Release(Network);
    if (Connections != NULL) IEnumNetworkConnections_Release(Connections);
    if (Manager != NULL) INetworkListManager_Release(Manager);
    CoUninitialize();
}

static
ZP_STATUS
ZpAdministration_EnumerateNetworkAdapters(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PMIB_IF_TABLE2 Interfaces = NULL;
    PMIB_UNICASTIPADDRESS_TABLE Addresses = NULL;
    NETIO_STATUS Result;
    NTSTATUS Status = STATUS_SUCCESS;
    PLONG Categories = NULL;
    ULONG Index;

    Result = GetIfTable2(&Interfaces);
    if (Result == NO_ERROR)
    {
        Result = GetUnicastIpAddressTable(AF_UNSPEC, &Addresses);
    }
    if (Result == NO_ERROR)
    {
        Categories = Mem_Alloc((SIZE_T)Interfaces->NumEntries * sizeof(*Categories));
        if (Categories == NULL) Result = ERROR_NOT_ENOUGH_MEMORY;
        else ZpNetwork_GetAdapterCategories(Interfaces, Categories);
    }
    if (Result == NO_ERROR)
    {
        for (Index = 0; NT_SUCCESS(Status) && Index < Interfaces->NumEntries; Index++)
        {
            Status = ZpNetwork_AddAdapter(&Builder, &Interfaces->Table[Index], Categories[Index]);
        }
        for (Index = 0; NT_SUCCESS(Status) && Index < Addresses->NumEntries; Index++)
        {
            Status = ZpNetwork_AddAdapterAddress(&Builder, &Addresses->Table[Index]);
        }
    }
    if (Result == NO_ERROR && NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }
    if (Addresses != NULL) FreeMibTable(Addresses);
    if (Interfaces != NULL) FreeMibTable(Interfaces);
    Mem_Free(Categories);
    ZpAdministration_FreeBuilder(&Builder);
    return Result == NO_ERROR ?
               ZpStatus_FromNtStatus(Status) :
               ZpStatus_FromCode(ZpStatusWin32, Result);
}

static
ZP_STATUS
ZpNetwork_SetAdapterCategory(
    _In_ NET_IFINDEX InterfaceIndex,
    _In_ NLM_NETWORK_CATEGORY Category)
{
    INetworkListManager* Manager = NULL;
    IEnumNetworkConnections* Connections = NULL;
    INetworkConnection* Connection = NULL;
    INetwork* Network = NULL;
    MIB_IF_ROW2 Row = { 0 };
    GUID Adapter;
    HRESULT Result, ItemResult, InitializeResult;
    ULONG Fetched;
    NETIO_STATUS NetResult;

    Row.InterfaceIndex = InterfaceIndex;
    NetResult = GetIfEntry2(&Row);
    if (NetResult != NO_ERROR) return ZpStatus_FromCode(ZpStatusWin32, NetResult);
    InitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(InitializeResult)) return ZpStatus_FromCode(ZpStatusHResult, InitializeResult);
    Result = CoCreateInstance(&CLSID_NetworkListManager,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_INetworkListManager,
                              (PVOID*)&Manager);
    if (SUCCEEDED(Result)) Result = INetworkListManager_GetNetworkConnections(Manager, &Connections);
    while (SUCCEEDED(Result) && IEnumNetworkConnections_Next(Connections, 1, &Connection, &Fetched) == S_OK)
    {
        ItemResult = INetworkConnection_GetAdapterId(Connection, &Adapter);
        if (SUCCEEDED(ItemResult) && IsEqualGUID(&Adapter, &Row.InterfaceGuid))
        {
            Result = INetworkConnection_GetNetwork(Connection, &Network);
            if (SUCCEEDED(Result)) Result = INetwork_SetCategory(Network, Category);
            break;
        }
        INetworkConnection_Release(Connection);
        Connection = NULL;
    }
    if (Network == NULL && SUCCEEDED(Result)) Result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (Connection != NULL) INetworkConnection_Release(Connection);
    if (Network != NULL) INetwork_Release(Network);
    if (Connections != NULL) IEnumNetworkConnections_Release(Connections);
    if (Manager != NULL) INetworkListManager_Release(Manager);
    CoUninitialize();
    return ZpStatus_FromCode(ZpStatusHResult, Result);
}

static
ZP_STATUS
ZpAdministration_ControlNetworkAdapter(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    MIB_IFROW Row = { 0 };
    PWSTR Identity = ZpAdministration_CopyView(&Control->Identity), End;
    PWSTR Argument;
    DWORD Result, Category;

    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Row.dwIndex = wcstoul(Identity, &End, 10);
    if (End == Identity || *End != UNICODE_NULL || Row.dwIndex == 0 ||
        (Control->Action != ZpAdministrationActionEnable &&
         Control->Action != ZpAdministrationActionDisable &&
         Control->Action != ZpAdministrationActionConfigure))
    {
        Result = ERROR_INVALID_PARAMETER;
    }
    else if (Control->Action == ZpAdministrationActionConfigure)
    {
        Argument = ZpAdministration_CopyView(&Control->Argument);
        if (Argument == NULL)
        {
            Mem_Free(Identity);
            return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
        Category = wcstoul(Argument, &End, 10);
        if (*End != UNICODE_NULL || Category > NLM_NETWORK_CATEGORY_PRIVATE)
        {
            Mem_Free(Argument);
            Mem_Free(Identity);
            return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
        }
        Mem_Free(Argument);
        Mem_Free(Identity);
        return ZpNetwork_SetAdapterCategory(Row.dwIndex, (NLM_NETWORK_CATEGORY)Category);
    }
    else
    {
        Row.dwAdminStatus = Control->Action == ZpAdministrationActionEnable ?
                                MIB_IF_ADMIN_STATUS_UP : MIB_IF_ADMIN_STATUS_DOWN;
        Result = SetIfEntry(&Row);
    }
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusWin32, Result);
}

static
PCWSTR
ZpNetwork_FindInterfaceAlias(
    _In_ const MIB_IF_TABLE2* Interfaces,
    _In_ NET_IFINDEX InterfaceIndex)
{
    ULONG Index;

    for (Index = 0; Index < Interfaces->NumEntries; Index++)
    {
        if (Interfaces->Table[Index].InterfaceIndex == InterfaceIndex)
        {
            return Interfaces->Table[Index].Alias;
        }
    }
    return NULL;
}

static
NTSTATUS
ZpNetwork_AddRoute(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ const MIB_IPFORWARD_ROW2* Row,
    _In_ const MIB_IF_TABLE2* Interfaces)
{
    WCHAR Identity[256], Destination[96], NextHop[80];
    NTSTATUS Status;

    Status = ZpNetwork_FormatPrefix(&Row->DestinationPrefix, Destination, ARRAYSIZE(Destination));
    if (!NT_SUCCESS(Status)) return Status;
    Status = ZpNetwork_FormatAddress(&Row->NextHop, 0, NextHop, ARRAYSIZE(NextHop));
    if (!NT_SUCCESS(Status)) return Status;
    _snwprintf_s(Identity,
                 ARRAYSIZE(Identity),
                 _TRUNCATE,
                 L"%u|%lu|%s|%s",
                 Row->DestinationPrefix.Prefix.si_family,
                 Row->InterfaceIndex,
                 Destination,
                 NextHop);
    return ZpAdministration_AddRecord(Builder,
                                      ZpAdministrationKindNetworkRoute,
                                      Row->DestinationPrefix.Prefix.si_family,
                                      Row->Protocol | ((ULONG)Row->Origin << 16),
                                      Row->Metric,
                                      Identity,
                                      Destination,
                                      NextHop,
                                      ZpNetwork_FindInterfaceAlias(Interfaces, Row->InterfaceIndex));
}

static
NTSTATUS
ZpNetwork_ParseRoute(
    _Inout_ PWSTR Identity,
    _Out_ PMIB_IPFORWARD_ROW2 Row)
{
    PWSTR Context, FamilyText, InterfaceText, Destination, NextHop, PrefixText;
    ULONG Family, PrefixLength;
    USHORT Port;
    NTSTATUS Status;

    FamilyText = wcstok_s(Identity, L"|", &Context);
    InterfaceText = wcstok_s(NULL, L"|", &Context);
    Destination = wcstok_s(NULL, L"|", &Context);
    NextHop = wcstok_s(NULL, L"|", &Context);
    if (FamilyText == NULL || InterfaceText == NULL || Destination == NULL || NextHop == NULL ||
        wcstok_s(NULL, L"|", &Context) != NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Family = wcstoul(FamilyText, &Context, 10);
    if (*Context != UNICODE_NULL || (Family != AF_INET && Family != AF_INET6))
    {
        return STATUS_INVALID_PARAMETER;
    }
    InitializeIpForwardEntry(Row);
    Row->InterfaceIndex = wcstoul(InterfaceText, &Context, 10);
    if (*Context != UNICODE_NULL || Row->InterfaceIndex == 0) return STATUS_INVALID_PARAMETER;
    PrefixText = wcsrchr(Destination, L'/');
    if (PrefixText == NULL) return STATUS_INVALID_PARAMETER;
    *PrefixText++ = UNICODE_NULL;
    PrefixLength = wcstoul(PrefixText, &Context, 10);
    if (*Context != UNICODE_NULL || PrefixLength > (Family == AF_INET ? 32UL : 128UL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Row->DestinationPrefix.Prefix.si_family = Row->NextHop.si_family = (ADDRESS_FAMILY)Family;
    Row->DestinationPrefix.PrefixLength = (UINT8)PrefixLength;
    if (Family == AF_INET)
    {
        Status = RtlIpv4StringToAddressExW(Destination,
                                           TRUE,
                                           &Row->DestinationPrefix.Prefix.Ipv4.sin_addr,
                                           &Port);
        if (NT_SUCCESS(Status))
        {
            Status = RtlIpv4StringToAddressExW(NextHop, TRUE, &Row->NextHop.Ipv4.sin_addr, &Port);
        }
    }
    else
    {
        Status = RtlIpv6StringToAddressExW(Destination,
                                           &Row->DestinationPrefix.Prefix.Ipv6.sin6_addr,
                                           &Row->DestinationPrefix.Prefix.Ipv6.sin6_scope_id,
                                           &Port);
        if (NT_SUCCESS(Status))
        {
            Status = RtlIpv6StringToAddressExW(NextHop,
                                               &Row->NextHop.Ipv6.sin6_addr,
                                               &Row->NextHop.Ipv6.sin6_scope_id,
                                               &Port);
        }
    }
    return Status;
}

static
ZP_STATUS
ZpAdministration_ControlNetworkRoute(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    MIB_IPFORWARD_ROW2 Row;
    UNICODE_STRING MetricText;
    PWSTR Identity, Argument;
    ULONG Metric;
    NTSTATUS Status;
    NETIO_STATUS Result;

    if (Control->Action != ZpAdministrationActionCreate &&
        Control->Action != ZpAdministrationActionConfigure &&
        Control->Action != ZpAdministrationActionDelete)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Status = ZpNetwork_ParseRoute(Identity, &Row);
    if (NT_SUCCESS(Status) && Control->Action != ZpAdministrationActionDelete)
    {
        Argument = ZpAdministration_CopyView(&Control->Argument);
        if (Argument == NULL)
        {
            Mem_Free(Identity);
            return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
        RtlInitUnicodeString(&MetricText, Argument);
        Status = RtlUnicodeStringToInteger(&MetricText, 10, &Metric);
        Mem_Free(Argument);
        if (NT_SUCCESS(Status)) Row.Metric = Metric;
    }
    if (NT_SUCCESS(Status))
    {
        Result = Control->Action == ZpAdministrationActionCreate ? CreateIpForwardEntry2(&Row) :
                     Control->Action == ZpAdministrationActionConfigure ? SetIpForwardEntry2(&Row) :
                                                                          DeleteIpForwardEntry2(&Row);
    }
    Mem_Free(Identity);
    return NT_SUCCESS(Status) ? ZpStatus_FromCode(ZpStatusWin32, Result) : ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_EnumerateNetworkRoutes(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PMIB_IPFORWARD_TABLE2 Routes = NULL;
    PMIB_IF_TABLE2 Interfaces = NULL;
    NETIO_STATUS Result;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    Result = GetIpForwardTable2(AF_UNSPEC, &Routes);
    if (Result == NO_ERROR)
    {
        Result = GetIfTable2Ex(MibIfTableNormalWithoutStatistics, &Interfaces);
    }
    if (Result == NO_ERROR)
    {
        for (Index = 0; NT_SUCCESS(Status) && Index < Routes->NumEntries; Index++)
        {
            Status = ZpNetwork_AddRoute(&Builder, &Routes->Table[Index], Interfaces);
        }
    }
    if (Result == NO_ERROR && NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }
    if (Interfaces != NULL) FreeMibTable(Interfaces);
    if (Routes != NULL) FreeMibTable(Routes);
    ZpAdministration_FreeBuilder(&Builder);
    return Result == NO_ERROR ?
               ZpStatus_FromNtStatus(Status) :
               ZpStatus_FromCode(ZpStatusWin32, Result);
}

static
DWORD
ZpNetwork_QueryOwnerTable(
    _In_ BOOLEAN Tcp,
    _In_ ADDRESS_FAMILY Family,
    _Outptr_ PVOID* Table)
{
    PVOID Buffer = NULL, NewBuffer;
    DWORD Size = 0, Result;
    ULONG Attempt;

    Result = Tcp ?
                 GetExtendedTcpTable(NULL, &Size, TRUE, Family, TCP_TABLE_OWNER_PID_ALL, 0) :
                 GetExtendedUdpTable(NULL, &Size, TRUE, Family, UDP_TABLE_OWNER_PID, 0);
    for (Attempt = 0; Result == ERROR_INSUFFICIENT_BUFFER && Attempt < 3; Attempt++)
    {
        if (Size > ZP_FRAME_MAX_BODY_SIZE)
        {
            Result = ERROR_BUFFER_OVERFLOW;
            break;
        }
        NewBuffer = Mem_ReAlloc(Buffer, Size);
        if (NewBuffer == NULL)
        {
            Result = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        Buffer = NewBuffer;
        Result = Tcp ?
                     GetExtendedTcpTable(Buffer, &Size, TRUE, Family, TCP_TABLE_OWNER_PID_ALL, 0) :
                     GetExtendedUdpTable(Buffer, &Size, TRUE, Family, UDP_TABLE_OWNER_PID, 0);
    }
    if (Result == NO_ERROR)
    {
        *Table = Buffer;
    }
    else
    {
        Mem_Free(Buffer);
    }
    return Result;
}

static
NTSTATUS
ZpNetwork_AddEndpoint(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ ZP_ADMINISTRATION_KIND Kind,
    _In_ ULONG State,
    _In_ ULONG ProcessId,
    _In_ const SOCKADDR_INET* LocalAddress,
    _In_ USHORT LocalPort,
    _In_opt_ const SOCKADDR_INET* RemoteAddress,
    _In_ USHORT RemotePort)
{
    WCHAR Identity[16], Local[80], Remote[80];
    NTSTATUS Status;

    Status = ZpNetwork_FormatAddress(LocalAddress, LocalPort, Local, ARRAYSIZE(Local));
    if (!NT_SUCCESS(Status)) return Status;
    if (RemoteAddress != NULL)
    {
        Status = ZpNetwork_FormatAddress(RemoteAddress, RemotePort, Remote, ARRAYSIZE(Remote));
        if (!NT_SUCCESS(Status)) return Status;
    }
    _ultow_s(ProcessId, Identity, ARRAYSIZE(Identity), 10);
    return ZpAdministration_AddRecord(Builder,
                                      Kind,
                                      State,
                                      LocalAddress->si_family,
                                      ProcessId,
                                      Identity,
                                      Local,
                                      RemoteAddress != NULL ? Remote : NULL,
                                      NULL);
}

static
NTSTATUS
ZpNetwork_AddTcp4Table(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ const MIB_TCPTABLE_OWNER_PID* Table)
{
    SOCKADDR_INET Local = { 0 }, Remote = { 0 };
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    Local.si_family = Remote.si_family = AF_INET;
    for (Index = 0; NT_SUCCESS(Status) && Index < Table->dwNumEntries; Index++)
    {
        const MIB_TCPROW_OWNER_PID* Row = &Table->table[Index];

        Local.Ipv4.sin_addr.S_un.S_addr = Row->dwLocalAddr;
        Remote.Ipv4.sin_addr.S_un.S_addr = Row->dwRemoteAddr;
        Status = ZpNetwork_AddEndpoint(Builder,
                                       ZpAdministrationKindTcpEndpoint,
                                       Row->dwState,
                                       Row->dwOwningPid,
                                       &Local,
                                       (USHORT)Row->dwLocalPort,
                                       &Remote,
                                       (USHORT)Row->dwRemotePort);
    }
    return Status;
}

static
NTSTATUS
ZpNetwork_AddTcp6Table(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ const MIB_TCP6TABLE_OWNER_PID* Table)
{
    SOCKADDR_INET Local = { 0 }, Remote = { 0 };
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    Local.si_family = Remote.si_family = AF_INET6;
    for (Index = 0; NT_SUCCESS(Status) && Index < Table->dwNumEntries; Index++)
    {
        const MIB_TCP6ROW_OWNER_PID* Row = &Table->table[Index];

        RtlCopyMemory(&Local.Ipv6.sin6_addr, Row->ucLocalAddr, sizeof(Row->ucLocalAddr));
        RtlCopyMemory(&Remote.Ipv6.sin6_addr, Row->ucRemoteAddr, sizeof(Row->ucRemoteAddr));
        Local.Ipv6.sin6_scope_id = Row->dwLocalScopeId;
        Remote.Ipv6.sin6_scope_id = Row->dwRemoteScopeId;
        Status = ZpNetwork_AddEndpoint(Builder,
                                       ZpAdministrationKindTcpEndpoint,
                                       Row->dwState,
                                       Row->dwOwningPid,
                                       &Local,
                                       (USHORT)Row->dwLocalPort,
                                       &Remote,
                                       (USHORT)Row->dwRemotePort);
    }
    return Status;
}

static
NTSTATUS
ZpNetwork_AddUdp4Table(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ const MIB_UDPTABLE_OWNER_PID* Table)
{
    SOCKADDR_INET Local = { 0 };
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    Local.si_family = AF_INET;
    for (Index = 0; NT_SUCCESS(Status) && Index < Table->dwNumEntries; Index++)
    {
        const MIB_UDPROW_OWNER_PID* Row = &Table->table[Index];

        Local.Ipv4.sin_addr.S_un.S_addr = Row->dwLocalAddr;
        Status = ZpNetwork_AddEndpoint(Builder,
                                       ZpAdministrationKindUdpEndpoint,
                                       0,
                                       Row->dwOwningPid,
                                       &Local,
                                       (USHORT)Row->dwLocalPort,
                                       NULL,
                                       0);
    }
    return Status;
}

static
NTSTATUS
ZpNetwork_AddUdp6Table(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ const MIB_UDP6TABLE_OWNER_PID* Table)
{
    SOCKADDR_INET Local = { 0 };
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    Local.si_family = AF_INET6;
    for (Index = 0; NT_SUCCESS(Status) && Index < Table->dwNumEntries; Index++)
    {
        const MIB_UDP6ROW_OWNER_PID* Row = &Table->table[Index];

        RtlCopyMemory(&Local.Ipv6.sin6_addr, Row->ucLocalAddr, sizeof(Row->ucLocalAddr));
        Local.Ipv6.sin6_scope_id = Row->dwLocalScopeId;
        Status = ZpNetwork_AddEndpoint(Builder,
                                       ZpAdministrationKindUdpEndpoint,
                                       0,
                                       Row->dwOwningPid,
                                       &Local,
                                       (USHORT)Row->dwLocalPort,
                                       NULL,
                                       0);
    }
    return Status;
}

static
ZP_STATUS
ZpAdministration_ControlNetworkEndpoint(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    MIB_TCPROW Row = { 0 };
    PWSTR Identity, Remote;
    USHORT Port;
    NTSTATUS Status;

    if (Control->Action != ZpAdministrationActionStop ||
        Control->Argument.Length != 0 || Control->Secret.Length != 0)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Remote = wcschr(Identity, L'\n');
    if (Remote == NULL)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        *Remote++ = UNICODE_NULL;
        Status = RtlIpv4StringToAddressExW(Identity, TRUE, (PIN_ADDR)&Row.dwLocalAddr, &Port);
        Row.dwLocalPort = Port;
        if (NT_SUCCESS(Status))
        {
            Status = RtlIpv4StringToAddressExW(Remote, TRUE, (PIN_ADDR)&Row.dwRemoteAddr, &Port);
            Row.dwRemotePort = Port;
        }
    }
    if (NT_SUCCESS(Status))
    {
        Row.dwState = MIB_TCP_STATE_DELETE_TCB;
        Status = NTSTATUS_FROM_WIN32(SetTcpEntry(&Row));
    }
    Mem_Free(Identity);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_EnumerateNetworkEndpoints(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PVOID Table;
    DWORD Result;
    NTSTATUS Status;

    Result = ZpNetwork_QueryOwnerTable(TRUE, AF_INET, &Table);
    if (Result != NO_ERROR) goto Cleanup;
    Status = ZpNetwork_AddTcp4Table(&Builder, Table);
    Mem_Free(Table);
    if (!NT_SUCCESS(Status)) goto Encode;
    Result = ZpNetwork_QueryOwnerTable(TRUE, AF_INET6, &Table);
    if (Result != NO_ERROR) goto Cleanup;
    Status = ZpNetwork_AddTcp6Table(&Builder, Table);
    Mem_Free(Table);
    if (!NT_SUCCESS(Status)) goto Encode;
    Result = ZpNetwork_QueryOwnerTable(FALSE, AF_INET, &Table);
    if (Result != NO_ERROR) goto Cleanup;
    Status = ZpNetwork_AddUdp4Table(&Builder, Table);
    Mem_Free(Table);
    if (!NT_SUCCESS(Status)) goto Encode;
    Result = ZpNetwork_QueryOwnerTable(FALSE, AF_INET6, &Table);
    if (Result != NO_ERROR) goto Cleanup;
    Status = ZpNetwork_AddUdp6Table(&Builder, Table);
    Mem_Free(Table);
Encode:
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
Cleanup:
    ZpAdministration_FreeBuilder(&Builder);
    return Result == NO_ERROR ?
               ZpStatus_FromNtStatus(Status) :
               ZpStatus_FromCode(ZpStatusWin32, Result);
}
