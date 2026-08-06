#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "RuntimeObject.lib")

typedef __x_ABI_CWindows_CSecurity_CCredentials_CICredentialFactory ZP_CREDENTIAL_FACTORY;
typedef __x_ABI_CWindows_CSecurity_CCredentials_CIPasswordCredential ZP_PASSWORD_CREDENTIAL;
typedef __x_ABI_CWindows_CSecurity_CCredentials_CIPasswordVault ZP_PASSWORD_VAULT;
typedef __FIVectorView_1_Windows__CSecurity__CCredentials__CPasswordCredential ZP_PASSWORD_CREDENTIALS;

typedef struct _ZP_CREDENTIAL_IDENTITY
{
    WCHAR Store;
    ULONG Number;
    ZP_STRING_VIEW Primary;
    ZP_STRING_VIEW Secondary;
} ZP_CREDENTIAL_IDENTITY, *PZP_CREDENTIAL_IDENTITY;

typedef const ZP_CREDENTIAL_IDENTITY* PCZP_CREDENTIAL_IDENTITY;

static const IID ZpCredentialFactoryIid = {
    0x54ef13a1, 0xbf26, 0x47b5, { 0x97, 0xdd, 0xde, 0x77, 0x9b, 0x7c, 0xad, 0x58 }
};
static const IID ZpPasswordVaultIid = {
    0x61fd2c0b, 0xc8d4, 0x48c1, { 0xa5, 0x4f, 0xbc, 0x5a, 0x64, 0x20, 0x5a, 0xf2 }
};

static
ZP_STATUS
ZpCredential_NtStatus(
    _In_ NTSTATUS Status)
{
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpCredential_Win32Status(
    _In_ DWORD Error)
{
    return ZpStatus_FromCode(ZpStatusWin32, Error);
}

static
ZP_STATUS
ZpCredential_HResultStatus(
    _In_ HRESULT Result)
{
    return ZpStatus_FromCode(ZpStatusHResult, Result);
}

static
NTSTATUS
ZpCredential_CreateIdentity(
    _In_ WCHAR Store,
    _In_ ULONG Number,
    _In_reads_(PrimaryLength) PCWCH Primary,
    _In_ ULONG PrimaryLength,
    _In_reads_opt_(SecondaryLength) PCWCH Secondary,
    _In_ ULONG SecondaryLength,
    _Outptr_ PWSTR* Identity)
{
    WCHAR NumberText[11];
    ULONG NumberLength;
    SIZE_T Length;
    PWSTR Buffer;

    if (_ultow_s(Number, NumberText, ARRAYSIZE(NumberText), 10) != 0) return STATUS_INVALID_PARAMETER;
    NumberLength = (ULONG)wcslen(NumberText);
    Length = (SIZE_T)NumberLength + PrimaryLength + SecondaryLength + 3;
    if (Length > MAXULONG) return STATUS_QUOTA_EXCEEDED;
    Buffer = Mem_Alloc(Length * sizeof(WCHAR));
    if (Buffer == NULL) return STATUS_NO_MEMORY;
    Buffer[0] = Store;
    RtlCopyMemory(Buffer + 1, NumberText, (SIZE_T)NumberLength * sizeof(WCHAR));
    Buffer[NumberLength + 1] = L':';
    RtlCopyMemory(Buffer + NumberLength + 2, Primary, (SIZE_T)PrimaryLength * sizeof(WCHAR));
    if (SecondaryLength != 0)
    {
        RtlCopyMemory(Buffer + NumberLength + PrimaryLength + 2,
                      Secondary,
                      (SIZE_T)SecondaryLength * sizeof(WCHAR));
    }
    Buffer[Length - 1] = UNICODE_NULL;
    *Identity = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpCredential_ParseIdentity(
    _In_ PCZP_STRING_VIEW Identity,
    _Out_ PZP_CREDENTIAL_IDENTITY Parsed)
{
    UNICODE_STRING NumberString;
    PCWCH Buffer = Identity->Buffer;
    ULONG Separator;
    NTSTATUS Status;

    if (Identity->Length < 4 || (Buffer[0] != L'W' && Buffer[0] != L'V'))
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Separator = 2; Separator < Identity->Length && Buffer[Separator] != L':'; Separator++);
    if (Separator == Identity->Length || Separator - 1 > MAXUSHORT / sizeof(WCHAR))
    {
        return STATUS_INVALID_PARAMETER;
    }
    NumberString.Length = (USHORT)((Separator - 1) * sizeof(WCHAR));
    NumberString.MaximumLength = NumberString.Length;
    NumberString.Buffer = (PWCHAR)Buffer + 1;
    Status = RtlUnicodeStringToInteger(&NumberString, 10, &Parsed->Number);
    if (!NT_SUCCESS(Status)) return Status;
    Parsed->Store = Buffer[0];
    Parsed->Primary.Buffer = Buffer + Separator + 1;
    Parsed->Primary.Length = Identity->Length - Separator - 1;
    Parsed->Secondary.Buffer = NULL;
    Parsed->Secondary.Length = 0;
    if (Parsed->Store == L'V')
    {
        if (Parsed->Number == 0 || Parsed->Number >= Parsed->Primary.Length) return STATUS_INVALID_PARAMETER;
        Parsed->Secondary.Buffer = Parsed->Primary.Buffer + Parsed->Number;
        Parsed->Secondary.Length = Parsed->Primary.Length - Parsed->Number;
        Parsed->Primary.Length = Parsed->Number;
    }
    else if (Parsed->Number < CRED_TYPE_GENERIC || Parsed->Number >= CRED_TYPE_MAXIMUM || Parsed->Primary.Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

static
HRESULT
ZpCredential_OpenVault(
    _Out_ ZP_PASSWORD_VAULT** Vault,
    _Out_ PLOGICAL Uninitialize)
{
    HSTRING ClassName;
    IInspectable* Inspectable;
    HRESULT Result;

    Result = RoInitialize(RO_INIT_MULTITHREADED);
    *Uninitialize = SUCCEEDED(Result);
    if (FAILED(Result) && Result != RPC_E_CHANGED_MODE) return Result;
    Result = WindowsCreateString(RuntimeClass_Windows_Security_Credentials_PasswordVault,
                                 RTL_NUMBER_OF(RuntimeClass_Windows_Security_Credentials_PasswordVault) - 1,
                                 &ClassName);
    if (FAILED(Result)) goto Cleanup;
    Result = RoActivateInstance(ClassName, &Inspectable);
    WindowsDeleteString(ClassName);
    if (FAILED(Result)) goto Cleanup;
    Result = IInspectable_QueryInterface(Inspectable, &ZpPasswordVaultIid, (PVOID*)Vault);
    IInspectable_Release(Inspectable);

Cleanup:
    if (FAILED(Result) && *Uninitialize) RoUninitialize();
    return Result;
}

static
HRESULT
ZpCredential_GetFactory(
    _Out_ ZP_CREDENTIAL_FACTORY** Factory)
{
    HSTRING ClassName;
    HRESULT Result;

    Result = WindowsCreateString(RuntimeClass_Windows_Security_Credentials_PasswordCredential,
                                 RTL_NUMBER_OF(RuntimeClass_Windows_Security_Credentials_PasswordCredential) - 1,
                                 &ClassName);
    if (FAILED(Result)) return Result;
    Result = RoGetActivationFactory(ClassName, &ZpCredentialFactoryIid, (PVOID*)Factory);
    WindowsDeleteString(ClassName);
    return Result;
}

static
ZP_STATUS
ZpCredential_EnumerateWindows(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    PCREDENTIALW* Credentials;
    DWORD Count, Index;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!CredEnumerateW(NULL, 0, &Count, &Credentials))
    {
        DWORD Error = GetLastError();

        return Error == ERROR_NOT_FOUND ? ZpCredential_NtStatus(STATUS_SUCCESS) : ZpCredential_Win32Status(Error);
    }
    for (Index = 0; Index < Count && NT_SUCCESS(Status); Index++)
    {
        PCREDENTIALW Credential = Credentials[Index];
        ULONG Flags = Credential->Persist << ZP_ADMINISTRATION_CREDENTIAL_PERSIST_SHIFT;
        PWSTR Identity;

        if ((Credential->Flags & CRED_FLAGS_PROMPT_NOW) != 0)
        {
            Flags |= ZP_ADMINISTRATION_CREDENTIAL_FLAG_PROMPT;
        }
        Status = ZpCredential_CreateIdentity(L'W',
                                             Credential->Type,
                                             Credential->TargetName,
                                             (ULONG)wcslen(Credential->TargetName),
                                             NULL,
                                             0,
                                             &Identity);
        if (NT_SUCCESS(Status))
        {
            Status = ZpAdministration_AddRecord(Builder,
                                                 ZpAdministrationKindWindowsCredential,
                                                 Credential->Type,
                                                 Flags,
                                                 ((ULONGLONG)Credential->LastWritten.dwHighDateTime << 32) |
                                                     Credential->LastWritten.dwLowDateTime,
                                                 Identity,
                                                 Credential->TargetName,
                                                 Credential->UserName,
                                                 Credential->Comment);
            Mem_Free(Identity);
        }
    }
    CredFree(Credentials);
    return ZpCredential_NtStatus(Status);
}

static
ZP_STATUS
ZpCredential_EnumerateWeb(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    ZP_PASSWORD_CREDENTIALS* Credentials = NULL;
    ZP_PASSWORD_CREDENTIAL* Credential;
    ZP_PASSWORD_VAULT* Vault;
    LOGICAL Uninitialize;
    HSTRING Resource, UserName;
    PCWSTR ResourceValue, UserNameValue;
    UINT32 Count, Index, ResourceLength, UserNameLength;
    PWSTR Identity = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    HRESULT Result;

    Result = ZpCredential_OpenVault(&Vault, &Uninitialize);
    if (FAILED(Result)) return ZpCredential_HResultStatus(Result);
    Result = Vault->lpVtbl->RetrieveAll(Vault, &Credentials);
    if (SUCCEEDED(Result)) Result = Credentials->lpVtbl->get_Size(Credentials, &Count);
    for (Index = 0; SUCCEEDED(Result) && NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Resource = NULL;
        UserName = NULL;
        Result = Credentials->lpVtbl->GetAt(Credentials, Index, &Credential);
        if (FAILED(Result)) break;
        Result = Credential->lpVtbl->get_Resource(Credential, &Resource);
        if (SUCCEEDED(Result)) Result = Credential->lpVtbl->get_UserName(Credential, &UserName);
        if (SUCCEEDED(Result))
        {
            UINT32 Character;

            ResourceValue = WindowsGetStringRawBuffer(Resource, &ResourceLength);
            UserNameValue = WindowsGetStringRawBuffer(UserName, &UserNameLength);
            for (Character = 0;
                 Character < ResourceLength && ResourceValue[Character] != UNICODE_NULL;
                 Character++);
            Status = Character == ResourceLength ? STATUS_SUCCESS : STATUS_DATA_ERROR;
            for (Character = 0;
                 NT_SUCCESS(Status) && Character < UserNameLength && UserNameValue[Character] != UNICODE_NULL;
                 Character++);
            if (NT_SUCCESS(Status) && Character != UserNameLength) Status = STATUS_DATA_ERROR;
            if (NT_SUCCESS(Status))
            {
                Status = ZpCredential_CreateIdentity(L'V',
                                                     ResourceLength,
                                                     ResourceValue,
                                                     ResourceLength,
                                                     UserNameValue,
                                                     UserNameLength,
                                                     &Identity);
            }
            if (NT_SUCCESS(Status))
            {
                Status = ZpAdministration_AddRecord(Builder,
                                                     ZpAdministrationKindWebCredential,
                                                     0,
                                                     ZP_ADMINISTRATION_CREDENTIAL_FLAG_SECRET_AVAILABLE,
                                                     0,
                                                     Identity,
                                                     ResourceValue,
                                                     UserNameValue,
                                                     NULL);
                Mem_Free(Identity);
            }
        }
        if (UserName != NULL) WindowsDeleteString(UserName);
        if (Resource != NULL) WindowsDeleteString(Resource);
        Credential->lpVtbl->Release(Credential);
    }
    if (Credentials != NULL) Credentials->lpVtbl->Release(Credentials);
    Vault->lpVtbl->Release(Vault);
    if (Uninitialize) RoUninitialize();
    return FAILED(Result) ? ZpCredential_HResultStatus(Result) : ZpCredential_NtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_EnumerateCredentials(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_STATUS Result;
    NTSTATUS Status;

    Result = ZpCredential_EnumerateWindows(&Builder);
    if (ZpStatus_IsSuccess(Result)) Result = ZpCredential_EnumerateWeb(&Builder);
    if (ZpStatus_IsSuccess(Result))
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
        Result = ZpCredential_NtStatus(Status);
    }
    ZpAdministration_FreeBuilder(&Builder);
    return Result;
}

static
ZP_STATUS
ZpAdministration_QueryCredential(
    _In_ PCZP_STRING_VIEW Identity,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_CREDENTIAL_IDENTITY Parsed;
    ZP_PASSWORD_CREDENTIAL* Credential = NULL;
    ZP_PASSWORD_VAULT* Vault;
    LOGICAL Uninitialize;
    HSTRING Resource = NULL, UserName = NULL, Password = NULL;
    PCWSTR PasswordValue;
    UINT32 PasswordLength;
    NTSTATUS Status;
    HRESULT Result;

    Status = ZpCredential_ParseIdentity(Identity, &Parsed);
    if (!NT_SUCCESS(Status)) return ZpCredential_NtStatus(Status);
    if (Parsed.Store != L'V') return ZpCredential_NtStatus(STATUS_NOT_SUPPORTED);
    Result = ZpCredential_OpenVault(&Vault, &Uninitialize);
    if (FAILED(Result)) return ZpCredential_HResultStatus(Result);
    Result = WindowsCreateString((PCWCH)Parsed.Primary.Buffer, Parsed.Primary.Length, &Resource);
    if (SUCCEEDED(Result))
    {
        Result = WindowsCreateString((PCWCH)Parsed.Secondary.Buffer, Parsed.Secondary.Length, &UserName);
    }
    if (SUCCEEDED(Result)) Result = Vault->lpVtbl->Retrieve(Vault, Resource, UserName, &Credential);
    if (SUCCEEDED(Result)) Result = Credential->lpVtbl->RetrievePassword(Credential);
    if (SUCCEEDED(Result)) Result = Credential->lpVtbl->get_Password(Credential, &Password);
    if (SUCCEEDED(Result))
    {
        UINT32 Index;

        PasswordValue = WindowsGetStringRawBuffer(Password, &PasswordLength);
        for (Index = 0; Index < PasswordLength && PasswordValue[Index] != UNICODE_NULL; Index++);
        Status = Index == PasswordLength ?
                     ZpAdministration_AddRecord(&Builder,
                                                ZpAdministrationKindWebCredential,
                                                0,
                                                ZP_ADMINISTRATION_CREDENTIAL_FLAG_SECRET_AVAILABLE,
                                                0,
                                                L"secret",
                                                NULL,
                                                NULL,
                                                PasswordValue) :
                     STATUS_DATA_ERROR;
        if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
        WindowsDeleteString(Password);
    }
    if (Credential != NULL) Credential->lpVtbl->Release(Credential);
    if (UserName != NULL) WindowsDeleteString(UserName);
    if (Resource != NULL) WindowsDeleteString(Resource);
    Vault->lpVtbl->Release(Vault);
    if (Uninitialize) RoUninitialize();
    ZpAdministration_FreeBuilder(&Builder);
    return FAILED(Result) ? ZpCredential_HResultStatus(Result) : ZpCredential_NtStatus(Status);
}

static
ZP_STATUS
ZpCredential_ControlWindows(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control,
    _In_ PCZP_CREDENTIAL_IDENTITY Parsed)
{
    CREDENTIALW Credential = { 0 };
    PCREDENTIALW Existing = NULL;
    PWSTR Target, UserName;
    DWORD Error = ERROR_SUCCESS;

    Target = ZpAdministration_CopyView(&Parsed->Primary);
    if (Target == NULL) return ZpCredential_NtStatus(STATUS_NO_MEMORY);
    if (Control->Action == ZpAdministrationActionDelete)
    {
        if (!CredDeleteW(Target, Parsed->Number, 0)) Error = GetLastError();
        Mem_Free(Target);
        return ZpCredential_Win32Status(Error);
    }
    if ((Control->Action != ZpAdministrationActionCreate && Control->Action != ZpAdministrationActionConfigure) ||
        (Parsed->Number != CRED_TYPE_GENERIC && Parsed->Number != CRED_TYPE_DOMAIN_PASSWORD) ||
        Control->Secret.Length > CRED_MAX_CREDENTIAL_BLOB_SIZE / sizeof(WCHAR))
    {
        Mem_Free(Target);
        return ZpCredential_NtStatus(STATUS_INVALID_PARAMETER);
    }
    if (Control->Action == ZpAdministrationActionCreate)
    {
        if (CredReadW(Target, Parsed->Number, 0, &Existing))
        {
            CredFree(Existing);
            Mem_Free(Target);
            return ZpCredential_Win32Status(ERROR_ALREADY_EXISTS);
        }
        Error = GetLastError();
        if (Error != ERROR_NOT_FOUND)
        {
            Mem_Free(Target);
            return ZpCredential_Win32Status(Error);
        }
        Error = ERROR_SUCCESS;
    }
    if (Control->Action == ZpAdministrationActionConfigure &&
        !CredReadW(Target, Parsed->Number, 0, &Existing))
    {
        Error = GetLastError();
        Mem_Free(Target);
        return ZpCredential_Win32Status(Error);
    }
    UserName = ZpAdministration_CopyView(&Control->Argument);
    if (UserName == NULL)
    {
        if (Existing != NULL) CredFree(Existing);
        Mem_Free(Target);
        return ZpCredential_NtStatus(STATUS_NO_MEMORY);
    }
    if (Existing != NULL) Credential = *Existing;
    Credential.Type = Parsed->Number;
    Credential.TargetName = Target;
    Credential.UserName = UserName;
    Credential.CredentialBlob = (PBYTE)Control->Secret.Buffer;
    Credential.CredentialBlobSize = Control->Secret.Length * sizeof(WCHAR);
    if (Existing == NULL) Credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&Credential, 0)) Error = GetLastError();
    Mem_Free(UserName);
    if (Existing != NULL) CredFree(Existing);
    Mem_Free(Target);
    return ZpCredential_Win32Status(Error);
}

static
ZP_STATUS
ZpCredential_ControlWeb(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control,
    _In_ PCZP_CREDENTIAL_IDENTITY Parsed)
{
    ZP_CREDENTIAL_FACTORY* Factory = NULL;
    ZP_PASSWORD_CREDENTIAL* Existing = NULL;
    ZP_PASSWORD_CREDENTIAL* Credential = NULL;
    ZP_PASSWORD_VAULT* Vault;
    LOGICAL Uninitialize;
    HSTRING Resource = NULL, UserName = NULL, Password = NULL;
    HRESULT Result;

    if (Control->Action != ZpAdministrationActionCreate &&
        Control->Action != ZpAdministrationActionConfigure &&
        Control->Action != ZpAdministrationActionDelete)
    {
        return ZpCredential_NtStatus(STATUS_INVALID_PARAMETER);
    }
    Result = ZpCredential_OpenVault(&Vault, &Uninitialize);
    if (FAILED(Result)) return ZpCredential_HResultStatus(Result);
    Result = WindowsCreateString((PCWCH)Parsed->Primary.Buffer, Parsed->Primary.Length, &Resource);
    if (SUCCEEDED(Result))
    {
        Result = WindowsCreateString((PCWCH)Parsed->Secondary.Buffer, Parsed->Secondary.Length, &UserName);
    }
    if (SUCCEEDED(Result) && Control->Action == ZpAdministrationActionCreate)
    {
        Result = Vault->lpVtbl->Retrieve(Vault, Resource, UserName, &Existing);
        if (SUCCEEDED(Result))
        {
            Existing->lpVtbl->Release(Existing);
            Result = HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        }
        else if (Result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
        {
            Result = S_OK;
        }
    }
    else if (SUCCEEDED(Result))
    {
        Result = Vault->lpVtbl->Retrieve(Vault, Resource, UserName, &Existing);
        if (SUCCEEDED(Result) && Control->Action == ZpAdministrationActionDelete)
        {
            Result = Vault->lpVtbl->Remove(Vault, Existing);
        }
        if (Existing != NULL) Existing->lpVtbl->Release(Existing);
    }
    if (SUCCEEDED(Result) && Control->Action != ZpAdministrationActionDelete)
    {
        Result = WindowsCreateString((PCWCH)Control->Secret.Buffer, Control->Secret.Length, &Password);
        if (SUCCEEDED(Result)) Result = ZpCredential_GetFactory(&Factory);
        if (SUCCEEDED(Result))
        {
            Result = Factory->lpVtbl->CreatePasswordCredential(Factory,
                                                               Resource,
                                                               UserName,
                                                               Password,
                                                               &Credential);
            Factory->lpVtbl->Release(Factory);
        }
        if (SUCCEEDED(Result))
        {
            Result = Vault->lpVtbl->Add(Vault, Credential);
            Credential->lpVtbl->Release(Credential);
        }
    }
    if (Password != NULL) WindowsDeleteString(Password);
    if (UserName != NULL) WindowsDeleteString(UserName);
    if (Resource != NULL) WindowsDeleteString(Resource);
    Vault->lpVtbl->Release(Vault);
    if (Uninitialize) RoUninitialize();
    return ZpCredential_HResultStatus(Result);
}

static
ZP_STATUS
ZpAdministration_ControlCredential(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    ZP_CREDENTIAL_IDENTITY Parsed;
    NTSTATUS Status;

    Status = ZpCredential_ParseIdentity(&Control->Identity, &Parsed);
    if (!NT_SUCCESS(Status)) return ZpCredential_NtStatus(Status);
    return Parsed.Store == L'W' ?
               ZpCredential_ControlWindows(Control, &Parsed) :
               ZpCredential_ControlWeb(Control, &Parsed);
}
