static
HRESULT
ZpAdministration_OpenFirewall(
    _Out_ INetFwPolicy2** Policy,
    _Out_ PLOGICAL Uninitialize)
{
    HRESULT Result;

    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(Result) && Result != RPC_E_CHANGED_MODE) return Result;
    *Uninitialize = SUCCEEDED(Result);
    Result = CoCreateInstance(&CLSID_NetFwPolicy2,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_INetFwPolicy2,
                              (PVOID*)Policy);
    if (FAILED(Result) && *Uninitialize) CoUninitialize();
    return Result;
}

static
ZP_STATUS
ZpAdministration_AddFirewallProfile(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ INetFwPolicy2* Policy,
    _In_ NET_FW_PROFILE_TYPE2 Profile,
    _In_ PCWSTR Identity,
    _In_ PCWSTR Name,
    _In_ LONG CurrentProfiles)
{
    VARIANT_BOOL Enabled;
    HRESULT Result;

    Result = INetFwPolicy2_get_FirewallEnabled(Policy, Profile, &Enabled);
    if (FAILED(Result)) return ZpStatus_FromCode(ZpStatusHResult, Result);
    return ZpStatus_FromNtStatus(
        ZpAdministration_AddRecord(Builder,
                                   ZpAdministrationKindFirewallProfile,
                                   Enabled != VARIANT_FALSE,
                                   FlagOn(CurrentProfiles, Profile),
                                   Profile,
                                   Identity,
                                   Name,
                                   NULL,
                                   NULL));
}

static
ZP_STATUS
ZpAdministration_AddFirewallRule(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ INetFwRule* Rule,
    _In_ ULONG Index)
{
    BSTR Name = NULL, Grouping = NULL, Description = NULL, Application = NULL;
    PWSTR Identity = NULL;
    VARIANT_BOOL Enabled, EdgeTraversal = VARIANT_FALSE;
    NET_FW_RULE_DIRECTION Direction;
    NET_FW_ACTION Action;
    LONG Profiles, Protocol;
    ULONG Flags;
    HRESULT Result;
    ZP_STATUS Status;

    Result = INetFwRule_get_Name(Rule, &Name);
    if (FAILED(Result)) return ZpStatus_FromCode(ZpStatusHResult, Result);
    INetFwRule_get_Grouping(Rule, &Grouping);
    INetFwRule_get_Description(Rule, &Description);
    INetFwRule_get_ApplicationName(Rule, &Application);
    Result = INetFwRule_get_Enabled(Rule, &Enabled);
    if (SUCCEEDED(Result)) Result = INetFwRule_get_Direction(Rule, &Direction);
    if (SUCCEEDED(Result)) Result = INetFwRule_get_Action(Rule, &Action);
    if (SUCCEEDED(Result)) Result = INetFwRule_get_Profiles(Rule, &Profiles);
    if (SUCCEEDED(Result)) Result = INetFwRule_get_Protocol(Rule, &Protocol);
    if (SUCCEEDED(Result)) INetFwRule_get_EdgeTraversal(Rule, &EdgeTraversal);
    if (SUCCEEDED(Result))
    {
        SIZE_T IdentityCount = wcslen(Name) + 32;

        Identity = Mem_Alloc(IdentityCount * sizeof(WCHAR));
        if (Identity == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            goto Exit;
        }
        _snwprintf_s(Identity, IdentityCount, _TRUNCATE, L"%lu\n%s", Index, Name);
        Flags = Direction == NET_FW_RULE_DIR_IN ? 1 : 2;
        if (Action == NET_FW_ACTION_ALLOW) Flags |= 4;
        if (FlagOn(Profiles, NET_FW_PROFILE2_PRIVATE)) Flags |= 8;
        if (FlagOn(Profiles, NET_FW_PROFILE2_PUBLIC)) Flags |= 16;
        if (FlagOn(Profiles, NET_FW_PROFILE2_DOMAIN)) Flags |= 32;
        if (EdgeTraversal != VARIANT_FALSE) Flags |= 64;
        Status = ZpStatus_FromNtStatus(
            ZpAdministration_AddRecord(Builder,
                                       ZpAdministrationKindFirewallRule,
                                       Enabled != VARIANT_FALSE,
                                       Flags,
                                       (ULONG)Protocol,
                                       Identity,
                                       Grouping,
                                       Description,
                                       Application));
    }
    else
    {
        Status = ZpStatus_FromCode(ZpStatusHResult, Result);
    }
Exit:
    Mem_Free(Identity);
    SysFreeString(Application);
    SysFreeString(Description);
    SysFreeString(Grouping);
    SysFreeString(Name);
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateFirewall(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    INetFwPolicy2* Policy;
    INetFwRules* Rules = NULL;
    IUnknown* Unknown = NULL;
    IEnumVARIANT* Enumerator = NULL;
    LOGICAL Uninitialize;
    LONG CurrentProfiles;
    HRESULT Result;
    ULONG RuleIndex = 0;
    ZP_STATUS Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);

    Result = ZpAdministration_OpenFirewall(&Policy, &Uninitialize);
    if (FAILED(Result)) return ZpStatus_FromCode(ZpStatusHResult, Result);
    Result = INetFwPolicy2_get_CurrentProfileTypes(Policy, &CurrentProfiles);
    if (SUCCEEDED(Result))
    {
        Status = ZpAdministration_AddFirewallProfile(&Builder,
                                                      Policy,
                                                      NET_FW_PROFILE2_PRIVATE,
                                                      L"Private",
                                                      L"专用网络",
                                                      CurrentProfiles);
    }
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpAdministration_AddFirewallProfile(&Builder,
                                                      Policy,
                                                      NET_FW_PROFILE2_PUBLIC,
                                                      L"Public",
                                                      L"来宾或公用网络",
                                                      CurrentProfiles);
    }
    if (ZpStatus_IsSuccess(Status) && SUCCEEDED(Result)) Result = INetFwPolicy2_get_Rules(Policy, &Rules);
    if (ZpStatus_IsSuccess(Status) && SUCCEEDED(Result)) Result = INetFwRules_get__NewEnum(Rules, &Unknown);
    if (ZpStatus_IsSuccess(Status) && SUCCEEDED(Result))
    {
        Result = Unknown->lpVtbl->QueryInterface(Unknown, &IID_IEnumVARIANT, (PVOID*)&Enumerator);
    }
    while (ZpStatus_IsSuccess(Status) && SUCCEEDED(Result))
    {
        VARIANT Value;
        ULONG Fetched;

        VariantInit(&Value);
        Result = Enumerator->lpVtbl->Next(Enumerator, 1, &Value, &Fetched);
        if (Result != S_OK) break;
        if (V_VT(&Value) == VT_DISPATCH)
        {
            INetFwRule* Rule;

            Result = V_DISPATCH(&Value)->lpVtbl->QueryInterface(
                V_DISPATCH(&Value),
                &IID_INetFwRule,
                (PVOID*)&Rule);
            if (SUCCEEDED(Result))
            {
                Status = ZpAdministration_AddFirewallRule(&Builder, Rule, RuleIndex++);
                INetFwRule_Release(Rule);
            }
        }
        VariantClear(&Value);
    }
    if (Result == S_FALSE) Result = S_OK;
    if (ZpStatus_IsSuccess(Status) && SUCCEEDED(Result))
    {
        Status = ZpStatus_FromNtStatus(
            ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength));
    }
    if (Enumerator != NULL) Enumerator->lpVtbl->Release(Enumerator);
    if (Unknown != NULL) Unknown->lpVtbl->Release(Unknown);
    if (Rules != NULL) INetFwRules_Release(Rules);
    INetFwPolicy2_Release(Policy);
    if (Uninitialize) CoUninitialize();
    ZpAdministration_FreeBuilder(&Builder);
    return FAILED(Result) ? ZpStatus_FromCode(ZpStatusHResult, Result) : Status;
}

static
HRESULT
ZpAdministration_FindFirewallRule(
    _In_ INetFwRules* Rules,
    _In_ PCWSTR Identity,
    _Out_ INetFwRule** Rule)
{
    IUnknown* Unknown;
    IEnumVARIANT* Enumerator;
    PWSTR End;
    ULONG TargetIndex, Index = 0;
    HRESULT Result;

    TargetIndex = wcstoul(Identity, &End, 10);
    if (End == Identity || *End != L'\n' || End[1] == UNICODE_NULL) return E_INVALIDARG;
    Result = INetFwRules_get__NewEnum(Rules, &Unknown);
    if (FAILED(Result)) return Result;
    Result = Unknown->lpVtbl->QueryInterface(Unknown, &IID_IEnumVARIANT, (PVOID*)&Enumerator);
    Unknown->lpVtbl->Release(Unknown);
    if (FAILED(Result)) return Result;
    for (;;)
    {
        VARIANT Value;
        ULONG Fetched;

        VariantInit(&Value);
        Result = Enumerator->lpVtbl->Next(Enumerator, 1, &Value, &Fetched);
        if (Result != S_OK) break;
        if (V_VT(&Value) == VT_DISPATCH)
        {
            INetFwRule* Current;

            Result = V_DISPATCH(&Value)->lpVtbl->QueryInterface(
                V_DISPATCH(&Value),
                &IID_INetFwRule,
                (PVOID*)&Current);
            if (SUCCEEDED(Result))
            {
                if (Index++ == TargetIndex)
                {
                    BSTR Name = NULL;

                    Result = INetFwRule_get_Name(Current, &Name);
                    if (SUCCEEDED(Result) && wcscmp(Name, End + 1) != 0)
                    {
                        Result = HRESULT_FROM_WIN32(ERROR_RETRY);
                    }
                    SysFreeString(Name);
                    if (SUCCEEDED(Result)) *Rule = Current;
                    else INetFwRule_Release(Current);
                    VariantClear(&Value);
                    break;
                }
                INetFwRule_Release(Current);
            }
        }
        VariantClear(&Value);
    }
    Enumerator->lpVtbl->Release(Enumerator);
    return Result == S_FALSE ? HRESULT_FROM_WIN32(ERROR_NOT_FOUND) : Result;
}

static
ZP_STATUS
ZpAdministration_ControlFirewall(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    INetFwPolicy2* Policy;
    INetFwRules* Rules = NULL;
    INetFwRule* Rule = NULL;
    PWSTR Identity;
    LOGICAL Uninitialize;
    HRESULT Result;

    if (Control->Action != ZpAdministrationActionEnable &&
        Control->Action != ZpAdministrationActionDisable &&
        Control->Action != ZpAdministrationActionAllow &&
        Control->Action != ZpAdministrationActionBlock)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Result = ZpAdministration_OpenFirewall(&Policy, &Uninitialize);
    if (FAILED(Result)) goto Exit;
    if (wcscmp(Identity, L"Private") == 0 || wcscmp(Identity, L"Public") == 0)
    {
        NET_FW_PROFILE_TYPE2 Profile = wcscmp(Identity, L"Private") == 0 ?
                                           NET_FW_PROFILE2_PRIVATE :
                                           NET_FW_PROFILE2_PUBLIC;

        Result = Control->Action == ZpAdministrationActionEnable ||
                 Control->Action == ZpAdministrationActionDisable ?
                     INetFwPolicy2_put_FirewallEnabled(
                         Policy,
                         Profile,
                         Control->Action == ZpAdministrationActionEnable ? VARIANT_TRUE : VARIANT_FALSE) :
                     E_INVALIDARG;
        goto ClosePolicy;
    }
    Result = INetFwPolicy2_get_Rules(Policy, &Rules);
    if (FAILED(Result)) goto ClosePolicy;
    Result = ZpAdministration_FindFirewallRule(Rules, Identity, &Rule);
    if (FAILED(Result)) goto ClosePolicy;
    switch (Control->Action)
    {
        case ZpAdministrationActionEnable:
        case ZpAdministrationActionDisable:
            Result = INetFwRule_put_Enabled(
                Rule,
                Control->Action == ZpAdministrationActionEnable ? VARIANT_TRUE : VARIANT_FALSE);
            break;

        case ZpAdministrationActionAllow:
        case ZpAdministrationActionBlock:
            Result = INetFwRule_put_Action(
                Rule,
                Control->Action == ZpAdministrationActionAllow ? NET_FW_ACTION_ALLOW : NET_FW_ACTION_BLOCK);
            break;
    }
ClosePolicy:
    if (Rule != NULL) INetFwRule_Release(Rule);
    if (Rules != NULL) INetFwRules_Release(Rules);
    INetFwPolicy2_Release(Policy);
    if (Uninitialize) CoUninitialize();
Exit:
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusHResult, Result);
}
