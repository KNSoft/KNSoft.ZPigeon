#include <wincrypt.h>

#define ZP_CERTIFICATE_STORE_ERROR 0x00000001
#define ZP_CERTIFICATE_STORE_USER 0x00000100
#define ZP_CERTIFICATE_STORE_MACHINE 0x00000200
#define ZP_CERTIFICATE_PRIVATE_KEY 0x00000001
#define ZP_CERTIFICATE_ARCHIVED 0x00000002
#define ZP_CERTIFICATE_SELF_SIGNED 0x00000004
#define ZP_CERTIFICATE_MAX_ENCODED_LENGTH 0x000C0000

typedef struct _ZP_CERTIFICATE_ENUMERATION
{
    PZP_ADMINISTRATION_BUILDER Builder;
    DWORD Location;
    PCWSTR Scope;
    NTSTATUS Status;
    DWORD Error;
} ZP_CERTIFICATE_ENUMERATION, *PZP_CERTIFICATE_ENUMERATION;

static
ZP_STATUS
ZpCertificate_Win32Status(
    _In_ DWORD Error)
{
    return ZpStatus_FromCode(ZpStatusWin32, Error);
}

static
ULONGLONG
ZpCertificate_FileTime(
    _In_ const FILETIME* Time)
{
    return ((ULONGLONG)Time->dwHighDateTime << 32) | Time->dwLowDateTime;
}

static
ULONG
ZpCertificate_GetFlags(
    _In_ PCCERT_CONTEXT Certificate)
{
    DWORD Length = 0;
    ULONG Flags = 0;

    if (CertGetCertificateContextProperty(Certificate, CERT_KEY_PROV_INFO_PROP_ID, NULL, &Length))
    {
        Flags |= ZP_CERTIFICATE_PRIVATE_KEY;
    }
    Length = 0;
    if (CertGetCertificateContextProperty(Certificate, CERT_ARCHIVED_PROP_ID, NULL, &Length))
    {
        Flags |= ZP_CERTIFICATE_ARCHIVED;
    }
    if (CertCompareCertificateName(Certificate->dwCertEncodingType,
                                   &Certificate->pCertInfo->Subject,
                                   &Certificate->pCertInfo->Issuer))
    {
        Flags |= ZP_CERTIFICATE_SELF_SIGNED;
    }
    return Flags;
}

static
PWSTR
ZpCertificate_GetName(
    _In_ PCCERT_CONTEXT Certificate,
    _In_ DWORD Flags)
{
    DWORD Length = CertGetNameStringW(Certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, Flags, NULL, NULL, 0);
    PWSTR Name;

    if (Length == 0) return NULL;
    Name = Mem_Alloc((SIZE_T)Length * sizeof(WCHAR));
    if (Name == NULL) return NULL;
    if (CertGetNameStringW(Certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, Flags, NULL, Name, Length) == 0)
    {
        Mem_Free(Name);
        return NULL;
    }
    return Name;
}

static
PWSTR
ZpCertificate_GetFriendlyName(
    _In_ PCCERT_CONTEXT Certificate)
{
    DWORD Length = 0;
    PWSTR Name;

    if (!CertGetCertificateContextProperty(Certificate, CERT_FRIENDLY_NAME_PROP_ID, NULL, &Length) || Length == 0)
    {
        return NULL;
    }
    Name = Mem_Alloc(Length);
    if (Name == NULL) return NULL;
    if (!CertGetCertificateContextProperty(Certificate, CERT_FRIENDLY_NAME_PROP_ID, Name, &Length))
    {
        Mem_Free(Name);
        return NULL;
    }
    return Name;
}

static
PWSTR
ZpCertificate_GetPurposes(
    _In_ PCCERT_CONTEXT Certificate)
{
    DWORD Length = 0;
    PCERT_ENHKEY_USAGE Usage;
    PWSTR Purposes, Cursor;
    SIZE_T CharacterCount = 1;
    ULONG Index;

    if (!CertGetEnhancedKeyUsage(Certificate, 0, NULL, &Length) || Length == 0) return NULL;
    Usage = Mem_Alloc(Length);
    if (Usage == NULL) return NULL;
    if (!CertGetEnhancedKeyUsage(Certificate, 0, Usage, &Length))
    {
        Mem_Free(Usage);
        return NULL;
    }
    if (Usage->cUsageIdentifier == 0)
    {
        PWSTR AllPurposes = Mem_Alloc(sizeof(L"所有"));

        Mem_Free(Usage);
        if (AllPurposes != NULL) RtlCopyMemory(AllPurposes, L"所有", sizeof(L"所有"));
        return AllPurposes;
    }
    for (Index = 0; Index < Usage->cUsageIdentifier; Index++)
    {
        PCCRYPT_OID_INFO Info = CryptFindOIDInfo(CRYPT_OID_INFO_OID_KEY,
                                                  Usage->rgpszUsageIdentifier[Index],
                                                  CRYPT_ENHKEY_USAGE_OID_GROUP_ID);
        PCWSTR Name = Info == NULL ? NULL : Info->pwszName;

        CharacterCount += Name == NULL ?
                              strlen(Usage->rgpszUsageIdentifier[Index]) :
                              wcslen(Name);
        if (Index != 0) CharacterCount += 2;
    }
    Purposes = CharacterCount <= ZP_CODEC_MAX_ELEMENT_COUNT ?
                   Mem_Alloc(CharacterCount * sizeof(WCHAR)) : NULL;
    if (Purposes == NULL)
    {
        Mem_Free(Usage);
        return NULL;
    }
    Cursor = Purposes;
    for (Index = 0; Index < Usage->cUsageIdentifier; Index++)
    {
        PCCRYPT_OID_INFO Info = CryptFindOIDInfo(CRYPT_OID_INFO_OID_KEY,
                                                  Usage->rgpszUsageIdentifier[Index],
                                                  CRYPT_ENHKEY_USAGE_OID_GROUP_ID);
        PCWSTR Name = Info == NULL ? NULL : Info->pwszName;
        SIZE_T NameLength;

        if (Index != 0)
        {
            *Cursor++ = L',';
            *Cursor++ = L' ';
        }
        if (Name != NULL)
        {
            NameLength = wcslen(Name);
            RtlCopyMemory(Cursor, Name, NameLength * sizeof(WCHAR));
        }
        else
        {
            NameLength = strlen(Usage->rgpszUsageIdentifier[Index]);
            mbstowcs_s(NULL,
                       Cursor,
                       CharacterCount - (Cursor - Purposes),
                       Usage->rgpszUsageIdentifier[Index],
                       NameLength);
        }
        Cursor += NameLength;
    }
    *Cursor = UNICODE_NULL;
    Mem_Free(Usage);
    return Purposes;
}

static
NTSTATUS
ZpCertificate_GetIdentity(
    _In_ PCWSTR Scope,
    _In_ PCWSTR StoreName,
    _In_ PCCERT_CONTEXT Certificate,
    _Outptr_ PWSTR* Identity,
    _Out_ PDWORD Error)
{
    static const WCHAR Hex[] = L"0123456789ABCDEF";
    BYTE Hash[20];
    DWORD HashLength = sizeof(Hash);
    SIZE_T ScopeLength = wcslen(Scope), StoreLength = wcslen(StoreName), Index;
    PWSTR Value, Cursor;

    if (!CertGetCertificateContextProperty(Certificate, CERT_SHA1_HASH_PROP_ID, Hash, &HashLength) ||
        HashLength != sizeof(Hash))
    {
        *Error = GetLastError();
        return STATUS_DATA_ERROR;
    }
    Value = Mem_Alloc((ScopeLength + StoreLength + 44) * sizeof(WCHAR));
    if (Value == NULL) return STATUS_NO_MEMORY;
    Cursor = Value;
    RtlCopyMemory(Cursor, Scope, ScopeLength * sizeof(WCHAR));
    Cursor += ScopeLength;
    *Cursor++ = L'\n';
    RtlCopyMemory(Cursor, StoreName, StoreLength * sizeof(WCHAR));
    Cursor += StoreLength;
    *Cursor++ = L'\n';
    for (Index = 0; Index < sizeof(Hash); Index++)
    {
        *Cursor++ = Hex[Hash[Index] >> 4];
        *Cursor++ = Hex[Hash[Index] & 0x0f];
    }
    *Cursor = UNICODE_NULL;
    *Identity = Value;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpCertificate_AddCertificate(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PCWSTR Scope,
    _In_ PCWSTR StoreName,
    _In_ PCCERT_CONTEXT Certificate,
    _Out_ PDWORD Error)
{
    FILETIME Now;
    PWSTR Identity, Name, Issuer, Purposes, FriendlyName, Detail = NULL;
    ULONG State;
    NTSTATUS Status;

    Status = ZpCertificate_GetIdentity(Scope, StoreName, Certificate, &Identity, Error);
    if (!NT_SUCCESS(Status)) return Status;
    Name = ZpCertificate_GetName(Certificate, 0);
    Issuer = ZpCertificate_GetName(Certificate, CERT_NAME_ISSUER_FLAG);
    Purposes = ZpCertificate_GetPurposes(Certificate);
    FriendlyName = ZpCertificate_GetFriendlyName(Certificate);
    if (Purposes != NULL || FriendlyName != NULL)
    {
        SIZE_T PurposeLength = Purposes == NULL ? 0 : wcslen(Purposes);
        SIZE_T FriendlyLength = FriendlyName == NULL ? 0 : wcslen(FriendlyName);

        Detail = Mem_Alloc((PurposeLength + FriendlyLength + 2) * sizeof(WCHAR));
        if (Detail != NULL)
        {
            _snwprintf_s(Detail,
                         PurposeLength + FriendlyLength + 2,
                         _TRUNCATE,
                         L"%s\n%s",
                         Purposes == NULL ? L"" : Purposes,
                         FriendlyName == NULL ? L"" : FriendlyName);
        }
    }
    GetSystemTimeAsFileTime(&Now);
    State = CompareFileTime(&Now, &Certificate->pCertInfo->NotBefore) < 0 ? 1 :
            CompareFileTime(&Now, &Certificate->pCertInfo->NotAfter) > 0 ? 2 : 0;
    Status = ZpAdministration_AddRecord(Builder,
                                         ZpAdministrationKindCertificate,
                                         State,
                                         ZpCertificate_GetFlags(Certificate),
                                         ZpCertificate_FileTime(&Certificate->pCertInfo->NotAfter),
                                         Identity,
                                         Name,
                                         Issuer,
                                         Detail);
    Mem_Free(Detail);
    Mem_Free(FriendlyName);
    Mem_Free(Purposes);
    Mem_Free(Issuer);
    Mem_Free(Name);
    Mem_Free(Identity);
    return Status;
}

static
BOOL
WINAPI
ZpCertificate_EnumerateStore(
    _In_ const VOID* SystemStore,
    _In_ DWORD Flags,
    _In_ PCERT_SYSTEM_STORE_INFO StoreInfo,
    _Reserved_ VOID* Reserved,
    _Inout_ VOID* Context)
{
    PZP_CERTIFICATE_ENUMERATION Enumeration = Context;
    PCWSTR StoreName = SystemStore;
    PCCERT_CONTEXT Certificate = NULL;
    HCERTSTORE Store;
    PWSTR Identity;
    SIZE_T ScopeLength, StoreLength;
    ULONG RecordIndex, CertificateCount = 0, RecordFlags;
    DWORD Error;

    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(StoreInfo);
    UNREFERENCED_PARAMETER(Reserved);
    ScopeLength = wcslen(Enumeration->Scope);
    StoreLength = wcslen(StoreName);
    if (ScopeLength + StoreLength + 2 > ZP_CODEC_MAX_ELEMENT_COUNT) return TRUE;
    Identity = Mem_Alloc((ScopeLength + StoreLength + 2) * sizeof(WCHAR));
    if (Identity == NULL)
    {
        Enumeration->Status = STATUS_NO_MEMORY;
        return FALSE;
    }
    _snwprintf_s(Identity,
                 ScopeLength + StoreLength + 2,
                 _TRUNCATE,
                 L"%s\n%s",
                 Enumeration->Scope,
                 StoreName);
    RecordIndex = Enumeration->Builder->Count;
    RecordFlags = Enumeration->Location == CERT_SYSTEM_STORE_CURRENT_USER ?
                      ZP_CERTIFICATE_STORE_USER : ZP_CERTIFICATE_STORE_MACHINE;
    Store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W,
                          0,
                          0,
                          Enumeration->Location | CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG,
                          StoreName);
    Error = Store == NULL ? GetLastError() : ERROR_SUCCESS;
    if (Error != ERROR_SUCCESS) RecordFlags |= ZP_CERTIFICATE_STORE_ERROR;
    Enumeration->Status = ZpAdministration_AddRecord(Enumeration->Builder,
                                                       ZpAdministrationKindCertificateStore,
                                                       Error,
                                                       RecordFlags,
                                                       0,
                                                       Identity,
                                                       StoreName,
                                                       Enumeration->Scope,
                                                       NULL);
    Mem_Free(Identity);
    if (!NT_SUCCESS(Enumeration->Status) || Store == NULL) return NT_SUCCESS(Enumeration->Status);
    while ((Certificate = CertEnumCertificatesInStore(Store, Certificate)) != NULL)
    {
        Enumeration->Status = ZpCertificate_AddCertificate(Enumeration->Builder,
                                                            Enumeration->Scope,
                                                            StoreName,
                                                            Certificate,
                                                            &Enumeration->Error);
        if (!NT_SUCCESS(Enumeration->Status))
        {
            CertFreeCertificateContext(Certificate);
            break;
        }
        CertificateCount++;
    }
    Enumeration->Builder->Records[RecordIndex].Value = CertificateCount;
    CertCloseStore(Store, 0);
    return NT_SUCCESS(Enumeration->Status);
}

static
ZP_STATUS
ZpAdministration_EnumerateCertificates(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_CERTIFICATE_ENUMERATION Enumeration = { &Builder, CERT_SYSTEM_STORE_CURRENT_USER, L"user" };
    DWORD Error = ERROR_SUCCESS;
    NTSTATUS Status;

    if (!CertEnumSystemStore(Enumeration.Location, NULL, &Enumeration, ZpCertificate_EnumerateStore))
    {
        Error = GetLastError();
    }
    if (NT_SUCCESS(Enumeration.Status) && Error == ERROR_SUCCESS && Enumeration.Error == ERROR_SUCCESS)
    {
        Enumeration.Location = CERT_SYSTEM_STORE_LOCAL_MACHINE;
        Enumeration.Scope = L"machine";
        if (!CertEnumSystemStore(Enumeration.Location, NULL, &Enumeration, ZpCertificate_EnumerateStore))
        {
            Error = GetLastError();
        }
    }
    Status = Enumeration.Status;
    if (Error == ERROR_SUCCESS) Error = Enumeration.Error;
    if (NT_SUCCESS(Status) && Error == ERROR_SUCCESS)
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }
    ZpAdministration_FreeBuilder(&Builder);
    return !NT_SUCCESS(Status) ?
               ZpStatus_FromNtStatus(Status) :
               ZpCertificate_Win32Status(Error);
}

static
NTSTATUS
ZpCertificate_ParseIdentity(
    _In_ PCZP_STRING_VIEW View,
    _In_ LOGICAL CertificateRequired,
    _Outptr_ PWSTR* Buffer,
    _Out_ PDWORD Location,
    _Outptr_ PWSTR* StoreName,
    _Outptr_opt_ PWSTR* Thumbprint)
{
    PWSTR Value = ZpAdministration_CopyView(View);
    PWSTR Separator, Hash = NULL;

    if (Value == NULL) return STATUS_NO_MEMORY;
    Separator = wcschr(Value, L'\n');
    if (Separator == NULL || Separator == Value || Separator[1] == UNICODE_NULL)
    {
        Mem_Free(Value);
        return STATUS_INVALID_PARAMETER;
    }
    *Separator++ = UNICODE_NULL;
    if (wcscmp(Value, L"user") == 0) *Location = CERT_SYSTEM_STORE_CURRENT_USER;
    else if (wcscmp(Value, L"machine") == 0) *Location = CERT_SYSTEM_STORE_LOCAL_MACHINE;
    else
    {
        Mem_Free(Value);
        return STATUS_INVALID_PARAMETER;
    }
    if (CertificateRequired)
    {
        Hash = wcschr(Separator, L'\n');
        if (Hash == NULL || Hash == Separator || wcslen(Hash + 1) != 40)
        {
            Mem_Free(Value);
            return STATUS_INVALID_PARAMETER;
        }
        *Hash++ = UNICODE_NULL;
    }
    else if (wcschr(Separator, L'\n') != NULL)
    {
        Mem_Free(Value);
        return STATUS_INVALID_PARAMETER;
    }
    *Buffer = Value;
    *StoreName = Separator;
    if (Thumbprint != NULL) *Thumbprint = Hash;
    return STATUS_SUCCESS;
}

static
PCCERT_CONTEXT
ZpCertificate_Find(
    _In_ HCERTSTORE Store,
    _In_ PCWSTR Thumbprint)
{
    BYTE Hash[20];
    CRYPT_HASH_BLOB Blob = { sizeof(Hash), Hash };
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Hash); Index++)
    {
        WCHAR High = Thumbprint[Index * 2], Low = Thumbprint[Index * 2 + 1];

        if (!iswxdigit(High) || !iswxdigit(Low))
        {
            SetLastError(ERROR_INVALID_DATA);
            return NULL;
        }
        Hash[Index] = (BYTE)(((High <= L'9' ? High - L'0' : towupper(High) - L'A' + 10) << 4) |
                             (Low <= L'9' ? Low - L'0' : towupper(Low) - L'A' + 10));
    }
    return CertFindCertificateInStore(Store,
                                      X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                      0,
                                      CERT_FIND_SHA1_HASH,
                                      &Blob,
                                      NULL);
}

static
NTSTATUS
ZpCertificate_AddDetails(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PCWSTR Identity,
    _In_ DWORD Location,
    _In_ PCCERT_CONTEXT Certificate,
    _Out_ PDWORD Error)
{
    PCCERT_CHAIN_CONTEXT Chain = NULL;
    CERT_CHAIN_PARA Parameters = { sizeof(Parameters) };
    PWSTR Name, Issuer, Encoded = NULL;
    DWORD EncodedLength = 0;
    NTSTATUS Status;
    ULONG ChainIndex, DetailsIndex;

    if (!CryptBinaryToStringW(Certificate->pbCertEncoded,
                              Certificate->cbCertEncoded,
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              NULL,
                              &EncodedLength))
    {
        *Error = GetLastError();
        return STATUS_DATA_ERROR;
    }
    Encoded = EncodedLength <= ZP_CODEC_MAX_ELEMENT_COUNT ?
                  Mem_Alloc((SIZE_T)EncodedLength * sizeof(WCHAR)) : NULL;
    if (Encoded == NULL) return EncodedLength <= ZP_CODEC_MAX_ELEMENT_COUNT ? STATUS_NO_MEMORY : STATUS_QUOTA_EXCEEDED;
    if (!CryptBinaryToStringW(Certificate->pbCertEncoded,
                              Certificate->cbCertEncoded,
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              Encoded,
                              &EncodedLength))
    {
        *Error = GetLastError();
        Mem_Free(Encoded);
        return STATUS_DATA_ERROR;
    }
    Name = ZpCertificate_GetName(Certificate, 0);
    Issuer = ZpCertificate_GetName(Certificate, CERT_NAME_ISSUER_FLAG);
    DetailsIndex = Builder->Count;
    Status = ZpAdministration_AddRecord(Builder,
                                         ZpAdministrationKindCertificateDetails,
                                         0,
                                         ZpCertificate_GetFlags(Certificate),
                                         ZpCertificate_FileTime(&Certificate->pCertInfo->NotAfter),
                                         Identity,
                                         Name,
                                         Issuer,
                                         Encoded);
    Mem_Free(Issuer);
    Mem_Free(Name);
    Mem_Free(Encoded);
    if (!NT_SUCCESS(Status)) return Status;
    if (!CertGetCertificateChain(Location == CERT_SYSTEM_STORE_LOCAL_MACHINE ? HCCE_LOCAL_MACHINE : NULL,
                                 Certificate,
                                 NULL,
                                 Certificate->hCertStore,
                                 &Parameters,
                                 0,
                                 NULL,
                                 &Chain))
    {
        Builder->Records[DetailsIndex].State = GetLastError();
        return STATUS_SUCCESS;
    }
    if (Chain->cChain == 0)
    {
        Builder->Records[DetailsIndex].State = ERROR_NOT_FOUND;
        CertFreeCertificateChain(Chain);
        return STATUS_SUCCESS;
    }
    for (ChainIndex = 0;
         ChainIndex < Chain->rgpChain[0]->cElement && NT_SUCCESS(Status);
         ChainIndex++)
    {
        PCERT_CHAIN_ELEMENT Element = Chain->rgpChain[0]->rgpElement[ChainIndex];
        PWSTR ElementIdentity;

        Name = ZpCertificate_GetName(Element->pCertContext, 0);
        Issuer = ZpCertificate_GetName(Element->pCertContext, CERT_NAME_ISSUER_FLAG);
        Status = ZpCertificate_GetIdentity(L"chain",
                                           L"chain",
                                           Element->pCertContext,
                                           &ElementIdentity,
                                           Error);
        if (NT_SUCCESS(Status))
        {
            Status = ZpAdministration_AddRecord(Builder,
                                                 ZpAdministrationKindCertificateChain,
                                                 Element->TrustStatus.dwErrorStatus,
                                                 Element->TrustStatus.dwInfoStatus,
                                                 ZpCertificate_FileTime(&Element->pCertContext->pCertInfo->NotAfter),
                                                 ElementIdentity,
                                                 Name,
                                                 Issuer,
                                                 NULL);
            Mem_Free(ElementIdentity);
        }
        Mem_Free(Issuer);
        Mem_Free(Name);
    }
    CertFreeCertificateChain(Chain);
    return Status;
}

static
ZP_STATUS
ZpAdministration_QueryCertificate(
    _In_ PCZP_STRING_VIEW Identity,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PWSTR IdentityBuffer, StoreName, Thumbprint;
    PWSTR IdentityValue;
    DWORD Location, Error = ERROR_SUCCESS;
    HCERTSTORE Store;
    PCCERT_CONTEXT Certificate;
    NTSTATUS Status;

    IdentityValue = ZpAdministration_CopyView(Identity);
    if (IdentityValue == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Status = ZpCertificate_ParseIdentity(Identity,
                                         TRUE,
                                         &IdentityBuffer,
                                         &Location,
                                         &StoreName,
                                         &Thumbprint);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(IdentityValue);
        return ZpStatus_FromNtStatus(Status);
    }
    Store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W,
                          0,
                          0,
                          Location | CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG,
                          StoreName);
    if (Store == NULL)
    {
        Error = GetLastError();
        Status = STATUS_UNSUCCESSFUL;
    }
    else
    {
        Certificate = ZpCertificate_Find(Store, Thumbprint);
        if (Certificate == NULL)
        {
            Error = GetLastError();
            Status = STATUS_UNSUCCESSFUL;
        }
        else
        {
            Status = ZpCertificate_AddDetails(&Builder, IdentityValue, Location, Certificate, &Error);
            CertFreeCertificateContext(Certificate);
        }
        CertCloseStore(Store, 0);
    }
    Mem_Free(IdentityBuffer);
    Mem_Free(IdentityValue);
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return NT_SUCCESS(Status) ?
               ZpStatus_FromNtStatus(STATUS_SUCCESS) :
               Error == ERROR_SUCCESS ? ZpStatus_FromNtStatus(Status) : ZpCertificate_Win32Status(Error);
}

static
ZP_STATUS
ZpAdministration_ControlCertificate(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PWSTR Identity, StoreName, Thumbprint;
    DWORD Location, Error = ERROR_SUCCESS;
    HCERTSTORE Store;
    PCCERT_CONTEXT Certificate;
    PBYTE Encoded = NULL;
    DWORD EncodedLength = 0;
    NTSTATUS Status;

    if (Control->Action != ZpAdministrationActionInstall &&
        Control->Action != ZpAdministrationActionDelete)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Status = ZpCertificate_ParseIdentity(&Control->Identity,
                                         Control->Action == ZpAdministrationActionDelete,
                                         &Identity,
                                         &Location,
                                         &StoreName,
                                         &Thumbprint);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W,
                          0,
                          0,
                          Location | CERT_STORE_OPEN_EXISTING_FLAG,
                          StoreName);
    if (Store == NULL)
    {
        Error = GetLastError();
    }
    else if (Control->Action == ZpAdministrationActionDelete)
    {
        Certificate = ZpCertificate_Find(Store, Thumbprint);
        if (Certificate == NULL || !CertDeleteCertificateFromStore(Certificate)) Error = GetLastError();
    }
    else if (Control->Argument.Length == 0 || Control->Argument.Length > ZP_CERTIFICATE_MAX_ENCODED_LENGTH)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else if (!CryptStringToBinaryW((PCWCH)Control->Argument.Buffer,
                                   Control->Argument.Length,
                                   CRYPT_STRING_ANY,
                                   NULL,
                                   &EncodedLength,
                                   NULL,
                                   NULL))
    {
        Error = GetLastError();
    }
    else
    {
        Encoded = Mem_Alloc(EncodedLength);
        if (Encoded == NULL)
        {
            Status = STATUS_NO_MEMORY;
        }
        else if (!CryptStringToBinaryW((PCWCH)Control->Argument.Buffer,
                                       Control->Argument.Length,
                                       CRYPT_STRING_ANY,
                                       Encoded,
                                       &EncodedLength,
                                       NULL,
                                       NULL) ||
                 !CertAddEncodedCertificateToStore(Store,
                                                   X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                   Encoded,
                                                   EncodedLength,
                                                   CERT_STORE_ADD_NEW,
                                                   NULL))
        {
            Error = GetLastError();
        }
    }
    Mem_Free(Encoded);
    if (Store != NULL) CertCloseStore(Store, 0);
    Mem_Free(Identity);
    return Error != ERROR_SUCCESS ?
               ZpCertificate_Win32Status(Error) :
               ZpStatus_FromNtStatus(Status);
}
