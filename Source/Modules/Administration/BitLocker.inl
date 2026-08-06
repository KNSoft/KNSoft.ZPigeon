#define ZP_BITLOCKER_NAMESPACE L"ROOT\\CIMV2\\Security\\MicrosoftVolumeEncryption"
#define ZP_BITLOCKER_CLASS L"Win32_EncryptableVolume"

static
HRESULT
ZpBitLocker_GetUInt32(
    _In_ IWbemClassObject* Object,
    _In_ PCWSTR Name,
    _Out_ PULONG Value)
{
    VARIANT Variant;
    HRESULT Result;

    VariantInit(&Variant);
    Result = IWbemClassObject_Get(Object, Name, 0, &Variant, NULL, NULL);
    if (SUCCEEDED(Result))
    {
        if (V_VT(&Variant) == VT_UI4) *Value = V_UI4(&Variant);
        else if (V_VT(&Variant) == VT_I4) *Value = (ULONG)V_I4(&Variant);
        else if (V_VT(&Variant) == VT_BOOL) *Value = V_BOOL(&Variant) != VARIANT_FALSE;
        else Result = WBEM_E_TYPE_MISMATCH;
    }
    VariantClear(&Variant);
    return Result;
}

static
HRESULT
ZpBitLocker_Connect(
    _Outptr_ IWbemServices** Services)
{
    IWbemLocator* Locator;
    BSTR Namespace;
    HRESULT Result;

    Result = CoCreateInstance(&CLSID_WbemLocator,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_IWbemLocator,
                              (PVOID*)&Locator);
    if (FAILED(Result)) return Result;
    Namespace = SysAllocString(ZP_BITLOCKER_NAMESPACE);
    if (Namespace == NULL)
    {
        IWbemLocator_Release(Locator);
        return E_OUTOFMEMORY;
    }
    Result = IWbemLocator_ConnectServer(Locator, Namespace, NULL, NULL, NULL, 0, NULL, NULL, Services);
    SysFreeString(Namespace);
    IWbemLocator_Release(Locator);
    if (SUCCEEDED(Result))
    {
        Result = CoSetProxyBlanket((IUnknown*)*Services,
                                   RPC_C_AUTHN_WINNT,
                                   RPC_C_AUTHZ_NONE,
                                   NULL,
                                   RPC_C_AUTHN_LEVEL_CALL,
                                   RPC_C_IMP_LEVEL_IMPERSONATE,
                                   NULL,
                                   EOAC_NONE);
        if (FAILED(Result)) IWbemServices_Release(*Services);
    }
    return Result;
}

static
HRESULT
ZpBitLocker_CreateEnumerator(
    _In_ IWbemServices* Services,
    _Outptr_ IEnumWbemClassObject** Enumerator)
{
    BSTR Language = SysAllocString(L"WQL");
    BSTR Query = SysAllocString(L"SELECT * FROM " ZP_BITLOCKER_CLASS);
    HRESULT Result;

    if (Language == NULL || Query == NULL)
    {
        SysFreeString(Query);
        SysFreeString(Language);
        return E_OUTOFMEMORY;
    }
    Result = IWbemServices_ExecQuery(Services,
                                     Language,
                                     Query,
                                     WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                     NULL,
                                     Enumerator);
    SysFreeString(Query);
    SysFreeString(Language);
    return Result;
}

static
HRESULT
ZpBitLocker_CreateMethodInput(
    _In_ IWbemServices* Services,
    _In_ PCWSTR Method,
    _Outptr_ IWbemClassObject** Input)
{
    IWbemClassObject* Class = NULL;
    IWbemClassObject* Definition = NULL;
    BSTR ClassName = SysAllocString(ZP_BITLOCKER_CLASS);
    BSTR MethodName = SysAllocString(Method);
    HRESULT Result;

    if (ClassName == NULL || MethodName == NULL) Result = E_OUTOFMEMORY;
    else Result = IWbemServices_GetObject(Services, ClassName, 0, NULL, &Class, NULL);
    if (SUCCEEDED(Result)) Result = IWbemClassObject_GetMethod(Class, MethodName, 0, &Definition, NULL);
    if (SUCCEEDED(Result)) Result = IWbemClassObject_SpawnInstance(Definition, 0, Input);
    if (Definition != NULL) IWbemClassObject_Release(Definition);
    if (Class != NULL) IWbemClassObject_Release(Class);
    SysFreeString(MethodName);
    SysFreeString(ClassName);
    return Result;
}

static
HRESULT
ZpBitLocker_PutUInt32(
    _In_ IWbemClassObject* Input,
    _In_ PCWSTR Name,
    _In_ ULONG Value)
{
    VARIANT Variant;

    VariantInit(&Variant);
    V_VT(&Variant) = VT_I4;
    V_I4(&Variant) = (LONG)Value;
    return IWbemClassObject_Put(Input, Name, 0, &Variant, CIM_UINT32);
}

static
HRESULT
ZpBitLocker_PutBoolean(
    _In_ IWbemClassObject* Input,
    _In_ PCWSTR Name,
    _In_ LOGICAL Value)
{
    VARIANT Variant;

    VariantInit(&Variant);
    V_VT(&Variant) = VT_BOOL;
    V_BOOL(&Variant) = Value ? VARIANT_TRUE : VARIANT_FALSE;
    return IWbemClassObject_Put(Input, Name, 0, &Variant, CIM_BOOLEAN);
}

static
HRESULT
ZpBitLocker_PutString(
    _In_ IWbemClassObject* Input,
    _In_ PCWSTR Name,
    _In_reads_(Length) PCWCH Value,
    _In_ ULONG Length)
{
    VARIANT Variant;
    HRESULT Result;

    VariantInit(&Variant);
    V_VT(&Variant) = VT_BSTR;
    V_BSTR(&Variant) = SysAllocStringLen(Value, Length);
    if (V_BSTR(&Variant) == NULL) return E_OUTOFMEMORY;
    Result = IWbemClassObject_Put(Input, Name, 0, &Variant, CIM_STRING);
    VariantClear(&Variant);
    return Result;
}

static
HRESULT
ZpBitLocker_ExecuteMethod(
    _In_ IWbemServices* Services,
    _In_ BSTR ObjectPath,
    _In_ PCWSTR Method,
    _In_opt_ IWbemClassObject* Input,
    _Outptr_opt_result_maybenull_ IWbemClassObject** Output)
{
    IWbemClassObject* LocalOutput = NULL;
    BSTR MethodName = SysAllocString(Method);
    HRESULT Result;
    ULONG Code;

    if (MethodName == NULL) return E_OUTOFMEMORY;
    Result = IWbemServices_ExecMethod(Services,
                                      ObjectPath,
                                      MethodName,
                                      0,
                                      NULL,
                                      Input,
                                      &LocalOutput,
                                      NULL);
    SysFreeString(MethodName);
    if (SUCCEEDED(Result))
    {
        Result = ZpBitLocker_GetUInt32(LocalOutput, L"ReturnValue", &Code);
        if (SUCCEEDED(Result) && Code != ERROR_SUCCESS)
        {
            Result = (Code & 0x80000000) != 0 ? (HRESULT)Code : HRESULT_FROM_WIN32(Code);
        }
    }
    if (SUCCEEDED(Result) && Output != NULL)
    {
        *Output = LocalOutput;
        LocalOutput = NULL;
    }
    if (LocalOutput != NULL) IWbemClassObject_Release(LocalOutput);
    return Result;
}

static
HRESULT
ZpBitLocker_QueryStatus(
    _In_ IWbemServices* Services,
    _In_ BSTR ObjectPath,
    _Out_ PULONG ConversionStatus,
    _Out_ PULONG Percentage,
    _Out_ PULONG EncryptionFlags)
{
    IWbemClassObject* Input = NULL;
    IWbemClassObject* Output = NULL;
    HRESULT Result;

    Result = ZpBitLocker_CreateMethodInput(Services, L"GetConversionStatus", &Input);
    if (SUCCEEDED(Result)) Result = ZpBitLocker_PutUInt32(Input, L"PrecisionFactor", 0);
    if (SUCCEEDED(Result))
    {
        Result = ZpBitLocker_ExecuteMethod(Services, ObjectPath, L"GetConversionStatus", Input, &Output);
    }
    if (SUCCEEDED(Result)) Result = ZpBitLocker_GetUInt32(Output, L"ConversionStatus", ConversionStatus);
    if (SUCCEEDED(Result)) Result = ZpBitLocker_GetUInt32(Output, L"EncryptionPercentage", Percentage);
    if (SUCCEEDED(Result)) Result = ZpBitLocker_GetUInt32(Output, L"EncryptionFlags", EncryptionFlags);
    if (Output != NULL) IWbemClassObject_Release(Output);
    if (Input != NULL) IWbemClassObject_Release(Input);
    return Result;
}

static
HRESULT
ZpBitLocker_QueryMethodUInt32(
    _In_ IWbemServices* Services,
    _In_ BSTR ObjectPath,
    _In_ PCWSTR Method,
    _In_ PCWSTR Property,
    _Out_ PULONG Value)
{
    IWbemClassObject* Output = NULL;
    HRESULT Result = ZpBitLocker_ExecuteMethod(Services, ObjectPath, Method, NULL, &Output);

    if (SUCCEEDED(Result)) Result = ZpBitLocker_GetUInt32(Output, Property, Value);
    if (Output != NULL) IWbemClassObject_Release(Output);
    return Result;
}

static
HRESULT
ZpBitLocker_AddVolume(
    _In_ IWbemServices* Services,
    _In_ IWbemClassObject* Object,
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    VARIANT DeviceId, DriveLetter, PersistentId, ObjectPath, Initialized;
    ULONG ConversionStatus, Percentage = 0, EncryptionFlags = 0;
    ULONG ProtectionStatus, LockStatus = 2, EncryptionMethod, VolumeType;
    ULONG AutoUnlock = 0, Flags;
    HRESULT Result;
    NTSTATUS Status;

    VariantInit(&DeviceId);
    VariantInit(&DriveLetter);
    VariantInit(&PersistentId);
    VariantInit(&ObjectPath);
    VariantInit(&Initialized);
    Result = IWbemClassObject_Get(Object, L"DeviceID", 0, &DeviceId, NULL, NULL);
    if (SUCCEEDED(Result)) Result = IWbemClassObject_Get(Object, L"DriveLetter", 0, &DriveLetter, NULL, NULL);
    if (SUCCEEDED(Result)) Result = IWbemClassObject_Get(Object, L"PersistentVolumeID", 0, &PersistentId, NULL, NULL);
    if (SUCCEEDED(Result)) Result = IWbemClassObject_Get(Object, L"__PATH", 0, &ObjectPath, NULL, NULL);
    if (SUCCEEDED(Result)) Result = IWbemClassObject_Get(Object, L"IsVolumeInitializedForProtection", 0, &Initialized, NULL, NULL);
    if (SUCCEEDED(Result)) Result = ZpBitLocker_GetUInt32(Object, L"ConversionStatus", &ConversionStatus);
    if (SUCCEEDED(Result)) Result = ZpBitLocker_GetUInt32(Object, L"ProtectionStatus", &ProtectionStatus);
    if (SUCCEEDED(Result)) Result = ZpBitLocker_GetUInt32(Object, L"EncryptionMethod", &EncryptionMethod);
    if (SUCCEEDED(Result)) Result = ZpBitLocker_GetUInt32(Object, L"VolumeType", &VolumeType);
    if (SUCCEEDED(Result) &&
        (V_VT(&DeviceId) != VT_BSTR || V_VT(&ObjectPath) != VT_BSTR ||
         (V_VT(&Initialized) != VT_BOOL && V_VT(&Initialized) != VT_I4 && V_VT(&Initialized) != VT_UI4)))
    {
        Result = WBEM_E_TYPE_MISMATCH;
    }
    if (SUCCEEDED(Result))
    {
        ULONG LiveStatus, LivePercentage, LiveFlags;

        if (SUCCEEDED(ZpBitLocker_QueryStatus(Services,
                                              V_BSTR(&ObjectPath),
                                              &LiveStatus,
                                              &LivePercentage,
                                              &LiveFlags)))
        {
            ConversionStatus = LiveStatus;
            Percentage = LivePercentage;
            EncryptionFlags = LiveFlags;
        }
        ZpBitLocker_QueryMethodUInt32(Services, V_BSTR(&ObjectPath), L"GetLockStatus", L"LockStatus", &LockStatus);
        ZpBitLocker_QueryMethodUInt32(Services,
                                     V_BSTR(&ObjectPath),
                                     L"IsAutoUnlockEnabled",
                                     L"IsAutoUnlockEnabled",
                                     &AutoUnlock);
        Flags = VolumeType & ZP_ADMINISTRATION_BITLOCKER_VOLUME_TYPE_MASK;
        Flags |= (ProtectionStatus << ZP_ADMINISTRATION_BITLOCKER_PROTECTION_SHIFT) &
                 ZP_ADMINISTRATION_BITLOCKER_PROTECTION_MASK;
        Flags |= (LockStatus << ZP_ADMINISTRATION_BITLOCKER_LOCK_SHIFT) &
                 ZP_ADMINISTRATION_BITLOCKER_LOCK_MASK;
        Flags |= (EncryptionMethod << ZP_ADMINISTRATION_BITLOCKER_ENCRYPTION_METHOD_SHIFT) &
                 ZP_ADMINISTRATION_BITLOCKER_ENCRYPTION_METHOD_MASK;
        if ((V_VT(&Initialized) == VT_BOOL && V_BOOL(&Initialized) != VARIANT_FALSE) ||
            (V_VT(&Initialized) == VT_I4 && V_I4(&Initialized) != 0) ||
            (V_VT(&Initialized) == VT_UI4 && V_UI4(&Initialized) != 0))
        {
            Flags |= ZP_ADMINISTRATION_BITLOCKER_FLAG_INITIALIZED;
        }
        if (AutoUnlock != 0) Flags |= ZP_ADMINISTRATION_BITLOCKER_FLAG_AUTO_UNLOCK;
        if ((EncryptionFlags & 1) != 0) Flags |= ZP_ADMINISTRATION_BITLOCKER_FLAG_DATA_ONLY;
        Status = ZpAdministration_AddRecord(
            Builder,
            ZpAdministrationKindBitLockerVolume,
            ConversionStatus,
            Flags,
            Percentage,
            V_BSTR(&DeviceId),
            V_VT(&DriveLetter) == VT_BSTR && SysStringLen(V_BSTR(&DriveLetter)) != 0 ?
                V_BSTR(&DriveLetter) : V_BSTR(&DeviceId),
            V_VT(&PersistentId) == VT_BSTR ? V_BSTR(&PersistentId) : NULL,
            NULL);
        if (!NT_SUCCESS(Status)) Result = HRESULT_FROM_NT(Status);
    }
    VariantClear(&Initialized);
    VariantClear(&ObjectPath);
    VariantClear(&PersistentId);
    VariantClear(&DriveLetter);
    VariantClear(&DeviceId);
    return Result;
}

static
HRESULT
ZpBitLocker_QueryProtectorType(
    _In_ IWbemServices* Services,
    _In_ BSTR ObjectPath,
    _In_ BSTR ProtectorId,
    _Out_ PULONG Type)
{
    IWbemClassObject* Input = NULL;
    IWbemClassObject* Output = NULL;
    HRESULT Result;

    Result = ZpBitLocker_CreateMethodInput(Services, L"GetKeyProtectorType", &Input);
    if (SUCCEEDED(Result))
    {
        Result = ZpBitLocker_PutString(Input,
                                       L"VolumeKeyProtectorID",
                                       ProtectorId,
                                       SysStringLen(ProtectorId));
    }
    if (SUCCEEDED(Result))
    {
        Result = ZpBitLocker_ExecuteMethod(Services, ObjectPath, L"GetKeyProtectorType", Input, &Output);
    }
    if (SUCCEEDED(Result)) Result = ZpBitLocker_GetUInt32(Output, L"KeyProtectorType", Type);
    if (Output != NULL) IWbemClassObject_Release(Output);
    if (Input != NULL) IWbemClassObject_Release(Input);
    return Result;
}

static
HRESULT
ZpBitLocker_AddProtectors(
    _In_ IWbemServices* Services,
    _In_ IWbemClassObject* Object,
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    IWbemClassObject* Input = NULL;
    IWbemClassObject* Output = NULL;
    VARIANT DeviceId, DriveLetter, ObjectPath, Protectors;
    LONG Lower, Upper, Index;
    HRESULT Result;

    VariantInit(&DeviceId);
    VariantInit(&DriveLetter);
    VariantInit(&ObjectPath);
    VariantInit(&Protectors);
    Result = IWbemClassObject_Get(Object, L"DeviceID", 0, &DeviceId, NULL, NULL);
    if (SUCCEEDED(Result)) Result = IWbemClassObject_Get(Object, L"DriveLetter", 0, &DriveLetter, NULL, NULL);
    if (SUCCEEDED(Result)) Result = IWbemClassObject_Get(Object, L"__PATH", 0, &ObjectPath, NULL, NULL);
    if (SUCCEEDED(Result) && (V_VT(&DeviceId) != VT_BSTR || V_VT(&ObjectPath) != VT_BSTR))
    {
        Result = WBEM_E_TYPE_MISMATCH;
    }
    if (SUCCEEDED(Result)) Result = ZpBitLocker_CreateMethodInput(Services, L"GetKeyProtectors", &Input);
    if (SUCCEEDED(Result)) Result = ZpBitLocker_PutUInt32(Input, L"KeyProtectorType", 0);
    if (SUCCEEDED(Result))
    {
        Result = ZpBitLocker_ExecuteMethod(Services, V_BSTR(&ObjectPath), L"GetKeyProtectors", Input, &Output);
    }
    if (Result == FVE_E_NOT_ACTIVATED) Result = S_FALSE;
    if (SUCCEEDED(Result) && Result != S_FALSE)
    {
        Result = IWbemClassObject_Get(Output, L"VolumeKeyProtectorID", 0, &Protectors, NULL, NULL);
    }
    if (SUCCEEDED(Result) && Result != S_FALSE && V_VT(&Protectors) != (VT_ARRAY | VT_BSTR))
    {
        Result = WBEM_E_TYPE_MISMATCH;
    }
    if (SUCCEEDED(Result) && Result != S_FALSE)
    {
        Result = SafeArrayGetLBound(V_ARRAY(&Protectors), 1, &Lower);
        if (SUCCEEDED(Result)) Result = SafeArrayGetUBound(V_ARRAY(&Protectors), 1, &Upper);
        for (Index = Lower; SUCCEEDED(Result) && Index <= Upper; Index++)
        {
            BSTR ProtectorId = NULL;
            ULONG Type = 0;
            NTSTATUS Status;

            Result = SafeArrayGetElement(V_ARRAY(&Protectors), &Index, &ProtectorId);
            if (SUCCEEDED(Result))
            {
                Result = ZpBitLocker_QueryProtectorType(Services, V_BSTR(&ObjectPath), ProtectorId, &Type);
            }
            if (SUCCEEDED(Result))
            {
                Status = ZpAdministration_AddRecord(
                    Builder,
                    ZpAdministrationKindBitLockerProtector,
                    Type,
                    0,
                    0,
                    ProtectorId,
                    V_VT(&DriveLetter) == VT_BSTR && SysStringLen(V_BSTR(&DriveLetter)) != 0 ?
                        V_BSTR(&DriveLetter) : V_BSTR(&DeviceId),
                    NULL,
                    V_BSTR(&DeviceId));
                if (!NT_SUCCESS(Status)) Result = HRESULT_FROM_NT(Status);
            }
            SysFreeString(ProtectorId);
        }
    }
    VariantClear(&Protectors);
    if (Output != NULL) IWbemClassObject_Release(Output);
    if (Input != NULL) IWbemClassObject_Release(Input);
    VariantClear(&ObjectPath);
    VariantClear(&DriveLetter);
    VariantClear(&DeviceId);
    return Result == S_FALSE ? S_OK : Result;
}

static
ZP_STATUS
ZpBitLocker_Enumerate(
    _In_ LOGICAL Protectors,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    IWbemServices* Services = NULL;
    IEnumWbemClassObject* Enumerator = NULL;
    IWbemClassObject* Object = NULL;
    HRESULT Result, InitializeResult;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Returned;

    InitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Result = InitializeResult == RPC_E_CHANGED_MODE ? S_OK : InitializeResult;
    if (SUCCEEDED(Result)) Result = ZpBitLocker_Connect(&Services);
    if (SUCCEEDED(Result)) Result = ZpBitLocker_CreateEnumerator(Services, &Enumerator);
    while (SUCCEEDED(Result))
    {
        Result = IEnumWbemClassObject_Next(Enumerator, WBEM_INFINITE, 1, &Object, &Returned);
        if (Result == WBEM_S_FALSE || Returned == 0)
        {
            Result = S_OK;
            break;
        }
        if (FAILED(Result)) break;
        Result = Protectors ? ZpBitLocker_AddProtectors(Services, Object, &Builder) :
                              ZpBitLocker_AddVolume(Services, Object, &Builder);
        IWbemClassObject_Release(Object);
        Object = NULL;
    }
    if (Object != NULL) IWbemClassObject_Release(Object);
    if (Enumerator != NULL) IEnumWbemClassObject_Release(Enumerator);
    if (Services != NULL) IWbemServices_Release(Services);
    if (SUCCEEDED(InitializeResult)) CoUninitialize();
    if (FAILED(Result))
    {
        ZpAdministration_FreeBuilder(&Builder);
        return ZpStatus_FromCode(ZpStatusHResult, Result);
    }
    Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_EnumerateBitLockerVolumes(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    return ZpBitLocker_Enumerate(FALSE, Response, ResponseLength);
}

static
ZP_STATUS
ZpAdministration_EnumerateBitLockerProtectors(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    return ZpBitLocker_Enumerate(TRUE, Response, ResponseLength);
}

static
HRESULT
ZpBitLocker_FindObjectPath(
    _In_ IWbemServices* Services,
    _In_ PCZP_STRING_VIEW Identity,
    _Out_ BSTR* ObjectPath)
{
    IEnumWbemClassObject* Enumerator = NULL;
    IWbemClassObject* Object = NULL;
    VARIANT DeviceId, Path;
    HRESULT Result;
    ULONG Returned;

    Result = ZpBitLocker_CreateEnumerator(Services, &Enumerator);
    while (SUCCEEDED(Result))
    {
        Result = IEnumWbemClassObject_Next(Enumerator, WBEM_INFINITE, 1, &Object, &Returned);
        if (Result == WBEM_S_FALSE || Returned == 0)
        {
            Result = WBEM_E_NOT_FOUND;
            break;
        }
        if (FAILED(Result)) break;
        VariantInit(&DeviceId);
        VariantInit(&Path);
        Result = IWbemClassObject_Get(Object, L"DeviceID", 0, &DeviceId, NULL, NULL);
        if (SUCCEEDED(Result) && V_VT(&DeviceId) == VT_BSTR &&
            SysStringLen(V_BSTR(&DeviceId)) == Identity->Length &&
            _wcsnicmp(V_BSTR(&DeviceId), (PCWCH)Identity->Buffer, Identity->Length) == 0)
        {
            Result = IWbemClassObject_Get(Object, L"__PATH", 0, &Path, NULL, NULL);
            if (SUCCEEDED(Result) && V_VT(&Path) == VT_BSTR)
            {
                *ObjectPath = SysAllocString(V_BSTR(&Path));
                if (*ObjectPath == NULL) Result = E_OUTOFMEMORY;
            }
            else if (SUCCEEDED(Result)) Result = WBEM_E_TYPE_MISMATCH;
            VariantClear(&Path);
            VariantClear(&DeviceId);
            IWbemClassObject_Release(Object);
            Object = NULL;
            break;
        }
        if (SUCCEEDED(Result) && V_VT(&DeviceId) != VT_BSTR) Result = WBEM_E_TYPE_MISMATCH;
        VariantClear(&Path);
        VariantClear(&DeviceId);
        IWbemClassObject_Release(Object);
        Object = NULL;
    }
    if (Object != NULL) IWbemClassObject_Release(Object);
    if (Enumerator != NULL) IEnumWbemClassObject_Release(Enumerator);
    return Result;
}

static
HRESULT
ZpBitLocker_ParseUInt32(
    _In_ PCZP_STRING_VIEW View,
    _In_ ULONG Maximum,
    _Out_ PULONG Value)
{
    PWSTR Text, End;
    ULONGLONG Parsed;

    if (View->Length == 0) return E_INVALIDARG;
    Text = ZpAdministration_CopyView(View);
    if (Text == NULL) return E_OUTOFMEMORY;
    Parsed = wcstoull(Text, &End, 10);
    if (End != Text + View->Length || Parsed > Maximum)
    {
        Mem_Free(Text);
        return E_INVALIDARG;
    }
    *Value = (ULONG)Parsed;
    Mem_Free(Text);
    return S_OK;
}

static
HRESULT
ZpBitLocker_ControlVolumeMethod(
    _In_ IWbemServices* Services,
    _In_ BSTR ObjectPath,
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    IWbemClassObject* Input = NULL;
    PCWSTR Method = NULL;
    HRESULT Result = S_OK;

    switch (Control->Action)
    {
        case ZpAdministrationActionEncrypt:
        {
            ULONG EncryptionMethod;

            Result = ZpBitLocker_ParseUInt32(&Control->Argument, 7, &EncryptionMethod);
            if (SUCCEEDED(Result) && EncryptionMethod != 3 && EncryptionMethod != 4 &&
                EncryptionMethod != 6 && EncryptionMethod != 7)
            {
                Result = E_INVALIDARG;
            }
            if (SUCCEEDED(Result)) Result = ZpBitLocker_CreateMethodInput(Services, L"Encrypt", &Input);
            if (SUCCEEDED(Result)) Result = ZpBitLocker_PutUInt32(Input, L"EncryptionMethod", EncryptionMethod);
            if (SUCCEEDED(Result)) Result = ZpBitLocker_PutUInt32(Input, L"EncryptionFlags", 1);
            Method = L"Encrypt";
            break;
        }
        case ZpAdministrationActionDecrypt:
            Method = L"Decrypt";
            break;
        case ZpAdministrationActionPause:
            Method = L"PauseConversion";
            break;
        case ZpAdministrationActionResume:
            Method = L"ResumeConversion";
            break;
        case ZpAdministrationActionEnable:
            Method = L"EnableKeyProtectors";
            break;
        case ZpAdministrationActionDisable:
            Method = L"DisableKeyProtectors";
            if (Control->Argument.Length != 0)
            {
                ULONG DisableCount;

                Result = ZpBitLocker_ParseUInt32(&Control->Argument, 15, &DisableCount);
                if (SUCCEEDED(Result))
                {
                    Result = ZpBitLocker_CreateMethodInput(Services, Method, &Input);
                }
                if (SUCCEEDED(Result)) Result = ZpBitLocker_PutUInt32(Input, L"DisableCount", DisableCount);
            }
            break;
        case ZpAdministrationActionLock:
            Method = L"Lock";
            Result = ZpBitLocker_CreateMethodInput(Services, Method, &Input);
            if (SUCCEEDED(Result)) Result = ZpBitLocker_PutBoolean(Input, L"ForceDismount", FALSE);
            break;
        case ZpAdministrationActionUnlock:
            Method = L"UnlockWithNumericalPassword";
            if (Control->Secret.Length == 0 || Control->Secret.Length > 64) Result = E_INVALIDARG;
            if (SUCCEEDED(Result)) Result = ZpBitLocker_CreateMethodInput(Services, Method, &Input);
            if (SUCCEEDED(Result))
            {
                Result = ZpBitLocker_PutString(Input,
                                               L"NumericalPassword",
                                               (PCWCH)Control->Secret.Buffer,
                                               Control->Secret.Length);
            }
            break;
        default:
            return E_INVALIDARG;
    }
    if (SUCCEEDED(Result)) Result = ZpBitLocker_ExecuteMethod(Services, ObjectPath, Method, Input, NULL);
    if (Input != NULL) IWbemClassObject_Release(Input);
    return Result;
}

static
ZP_STATUS
ZpAdministration_ControlBitLockerVolume(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    IWbemServices* Services = NULL;
    BSTR ObjectPath = NULL;
    HRESULT Result, InitializeResult;

    if (Control->Identity.Length == 0 || Control->Identity.Length > MAX_PATH)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    InitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Result = InitializeResult == RPC_E_CHANGED_MODE ? S_OK : InitializeResult;
    if (SUCCEEDED(Result)) Result = ZpBitLocker_Connect(&Services);
    if (SUCCEEDED(Result)) Result = ZpBitLocker_FindObjectPath(Services, &Control->Identity, &ObjectPath);
    if (SUCCEEDED(Result)) Result = ZpBitLocker_ControlVolumeMethod(Services, ObjectPath, Control);
    SysFreeString(ObjectPath);
    if (Services != NULL) IWbemServices_Release(Services);
    if (SUCCEEDED(InitializeResult)) CoUninitialize();
    return ZpStatus_FromCode(ZpStatusHResult, Result);
}

static
ZP_STATUS
ZpAdministration_ControlBitLockerProtector(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    IWbemServices* Services = NULL;
    IWbemClassObject* Input = NULL;
    BSTR ObjectPath = NULL;
    PCWSTR Method = NULL;
    HRESULT Result, InitializeResult;

    if (Control->Identity.Length == 0 || Control->Identity.Length > MAX_PATH)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    InitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Result = InitializeResult == RPC_E_CHANGED_MODE ? S_OK : InitializeResult;
    if (SUCCEEDED(Result)) Result = ZpBitLocker_Connect(&Services);
    if (SUCCEEDED(Result)) Result = ZpBitLocker_FindObjectPath(Services, &Control->Identity, &ObjectPath);
    if (SUCCEEDED(Result) && Control->Action == ZpAdministrationActionCreate)
    {
        Method = L"ProtectKeyWithNumericalPassword";
        if (Control->Argument.Length > 256 || Control->Secret.Length == 0 || Control->Secret.Length > 64)
        {
            Result = E_INVALIDARG;
        }
        if (SUCCEEDED(Result)) Result = ZpBitLocker_CreateMethodInput(Services, Method, &Input);
        if (SUCCEEDED(Result) && Control->Argument.Length != 0)
        {
            Result = ZpBitLocker_PutString(Input,
                                           L"FriendlyName",
                                           (PCWCH)Control->Argument.Buffer,
                                           Control->Argument.Length);
        }
        if (SUCCEEDED(Result))
        {
            Result = ZpBitLocker_PutString(Input,
                                           L"NumericalPassword",
                                           (PCWCH)Control->Secret.Buffer,
                                           Control->Secret.Length);
        }
    }
    else if (SUCCEEDED(Result) && Control->Action == ZpAdministrationActionDelete)
    {
        Method = L"DeleteKeyProtector";
        if (Control->Argument.Length == 0 || Control->Argument.Length > 128) Result = E_INVALIDARG;
        if (SUCCEEDED(Result)) Result = ZpBitLocker_CreateMethodInput(Services, Method, &Input);
        if (SUCCEEDED(Result))
        {
            Result = ZpBitLocker_PutString(Input,
                                           L"VolumeKeyProtectorID",
                                           (PCWCH)Control->Argument.Buffer,
                                           Control->Argument.Length);
        }
    }
    else if (SUCCEEDED(Result))
    {
        Result = E_INVALIDARG;
    }
    if (SUCCEEDED(Result)) Result = ZpBitLocker_ExecuteMethod(Services, ObjectPath, Method, Input, NULL);
    if (Input != NULL) IWbemClassObject_Release(Input);
    SysFreeString(ObjectPath);
    if (Services != NULL) IWbemServices_Release(Services);
    if (SUCCEEDED(InitializeResult)) CoUninitialize();
    return ZpStatus_FromCode(ZpStatusHResult, Result);
}
