#include <wincrypt.h>

#include <cryptuiapi.h>

#pragma comment(lib, "Cryptui.lib")

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
    BOOLEAN IncludeCertificates;
    ZP_STATUS Status;
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
NTSTATUS
ZpCertificate_EncodeMetadata(
    _In_ PCCERT_CONTEXT Certificate,
    _In_opt_ PCWSTR FriendlyName,
    _Outptr_result_bytebuffer_(*DataLength) PBYTE* Data,
    _Out_ PULONG DataLength)
{
    DWORD UsageLength = 0;
    PCERT_ENHKEY_USAGE Usage = NULL;
    ZP_CODEC_WRITER Writer;
    PBYTE Buffer;
    PWSTR Oid = NULL;
    SIZE_T FriendlyNameLength = FriendlyName == NULL ? 0 : wcslen(FriendlyName);
    SIZE_T MaximumOidLength = 0;
    ULONGLONG RequiredSize = sizeof(BYTE) + sizeof(USHORT) + sizeof(ULONG) +
                             FriendlyNameLength * sizeof(WCHAR);
    ULONG Count = 0, Index;
    NTSTATUS Status;

    if (CertGetEnhancedKeyUsage(Certificate, 0, NULL, &UsageLength) && UsageLength != 0)
    {
        Usage = Mem_Alloc(UsageLength);
        if (Usage == NULL) return STATUS_NO_MEMORY;
        if (!CertGetEnhancedKeyUsage(Certificate, 0, Usage, &UsageLength))
        {
            Status = NTSTATUS_FROM_WIN32(GetLastError());
            Mem_Free(Usage);
            return Status;
        }
        Count = Usage->cUsageIdentifier;
        if (Count > MAXUSHORT)
        {
            Mem_Free(Usage);
            return STATUS_BUFFER_OVERFLOW;
        }
        for (Index = 0; Index < Count; Index++)
        {
            SIZE_T Length = strlen(Usage->rgpszUsageIdentifier[Index]);

            MaximumOidLength = max(MaximumOidLength, Length);
            RequiredSize += sizeof(ULONG) + Length * sizeof(WCHAR);
        }
    }
    if (RequiredSize > ZP_CODEC_MAX_ELEMENT_COUNT)
    {
        Mem_Free(Usage);
        return STATUS_BUFFER_OVERFLOW;
    }
    Buffer = Mem_Alloc((SIZE_T)RequiredSize);
    if (Buffer == NULL)
    {
        Mem_Free(Usage);
        return STATUS_NO_MEMORY;
    }
    if (MaximumOidLength != 0)
    {
        Oid = Mem_Alloc((MaximumOidLength + 1) * sizeof(WCHAR));
        if (Oid == NULL)
        {
            Mem_Free(Buffer);
            Mem_Free(Usage);
            return STATUS_NO_MEMORY;
        }
    }
    ZpCodec_InitializeWriter(&Writer, Buffer, (ULONG)RequiredSize);
    Status = ZpCodec_WriteBoolean(&Writer, Usage != NULL && Count == 0);
    if (NT_SUCCESS(Status))
        Status = ZpCodec_WriteString(&Writer, FriendlyName, (ULONG)FriendlyNameLength);
    if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt16(&Writer, (USHORT)Count);
    for (Index = 0; NT_SUCCESS(Status) && Index < Count; Index++)
    {
        SIZE_T Length = strlen(Usage->rgpszUsageIdentifier[Index]);
        SIZE_T CharacterIndex;

        for (CharacterIndex = 0; CharacterIndex < Length; CharacterIndex++)
            Oid[CharacterIndex] = (WCHAR)(BYTE)Usage->rgpszUsageIdentifier[Index][CharacterIndex];
        Oid[Length] = UNICODE_NULL;
        Status = ZpCodec_WriteString(&Writer, Oid, (ULONG)Length);
    }
    Mem_Free(Oid);
    Mem_Free(Usage);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    *Data = Buffer;
    *DataLength = Writer.Offset;
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpCertificate_GetIdentity(
    _In_ PCWSTR Scope,
    _In_ PCWSTR StoreName,
    _In_ PCCERT_CONTEXT Certificate,
    _Outptr_ PWSTR* Identity)
{
    static const WCHAR Hex[] = L"0123456789ABCDEF";
    BYTE Hash[20];
    DWORD HashLength = sizeof(Hash);
    SIZE_T ScopeLength = wcslen(Scope), StoreLength = wcslen(StoreName), Index;
    PWSTR Value, Cursor;

    if (!CertGetCertificateContextProperty(Certificate, CERT_SHA1_HASH_PROP_ID, Hash, &HashLength))
    {
        return ZpCertificate_Win32Status(GetLastError());
    }
    if (HashLength != sizeof(Hash)) return ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    Value = Mem_Alloc((ScopeLength + StoreLength + sizeof(Hash) * 2 + 3) * sizeof(WCHAR));
    if (Value == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
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
    return ZpStatus_Make(ZpStatusNone, 0);
}

static
ZP_STATUS
ZpCertificate_AddCertificate(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PCWSTR Scope,
    _In_ PCWSTR StoreName,
    _In_ PCCERT_CONTEXT Certificate)
{
    FILETIME Now;
    PWSTR Identity, Name, Issuer, FriendlyName;
    PBYTE Metadata;
    ULONG State, MetadataLength;
    NTSTATUS NtStatus;
    ZP_STATUS Status;

    Status = ZpCertificate_GetIdentity(Scope, StoreName, Certificate, &Identity);
    if (!ZpStatus_IsSuccess(Status)) return Status;
    Name = ZpCertificate_GetName(Certificate, 0);
    Issuer = ZpCertificate_GetName(Certificate, CERT_NAME_ISSUER_FLAG);
    FriendlyName = ZpCertificate_GetFriendlyName(Certificate);
    NtStatus = ZpCertificate_EncodeMetadata(Certificate, FriendlyName, &Metadata, &MetadataLength);
    if (!NT_SUCCESS(NtStatus)) goto Cleanup;
    GetSystemTimeAsFileTime(&Now);
    State = CompareFileTime(&Now, &Certificate->pCertInfo->NotBefore) < 0 ? 1 :
            CompareFileTime(&Now, &Certificate->pCertInfo->NotAfter) > 0 ? 2 : 0;
    NtStatus = ZpAdministration_AddRecordData(Builder,
                                               ZpAdministrationKindCertificate,
                                               State,
                                               ZpCertificate_GetFlags(Certificate),
                                               ZpCertificate_FileTime(&Certificate->pCertInfo->NotAfter),
                                               Identity,
                                               Name,
                                               Issuer,
                                               NULL,
                                               Metadata,
                                               MetadataLength);
    Mem_Free(Metadata);
Cleanup:
    Mem_Free(FriendlyName);
    Mem_Free(Issuer);
    Mem_Free(Name);
    Mem_Free(Identity);
    return ZpStatus_FromNtStatus(NtStatus);
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
    ULONG CertificateCount = 0, RecordFlags;
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
        Enumeration->Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        return FALSE;
    }
    _snwprintf_s(Identity,
                 ScopeLength + StoreLength + 2,
                 _TRUNCATE,
                 L"%s\n%s",
                 Enumeration->Scope,
                 StoreName);
    RecordFlags = Enumeration->Location == CERT_SYSTEM_STORE_CURRENT_USER ?
                      ZP_CERTIFICATE_STORE_USER : ZP_CERTIFICATE_STORE_MACHINE;
    Store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W,
                          0,
                          0,
                          Enumeration->Location | CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG,
                          StoreName);
    Error = Store == NULL ? GetLastError() : ERROR_SUCCESS;
    if (Error != ERROR_SUCCESS) RecordFlags |= ZP_CERTIFICATE_STORE_ERROR;
    if (Store != NULL && Enumeration->IncludeCertificates)
    {
        while ((Certificate = CertEnumCertificatesInStore(Store, Certificate)) != NULL)
        {
            Enumeration->Status = ZpCertificate_AddCertificate(Enumeration->Builder,
                                                                Enumeration->Scope,
                                                                StoreName,
                                                                Certificate);
            if (!ZpStatus_IsSuccess(Enumeration->Status))
            {
                CertFreeCertificateContext(Certificate);
                break;
            }
            CertificateCount++;
        }
        CertCloseStore(Store, 0);
    }
    if (ZpStatus_IsSuccess(Enumeration->Status))
    {
        Enumeration->Status = ZpStatus_FromNtStatus(
            ZpAdministration_AddRecord(Enumeration->Builder,
                                       ZpAdministrationKindCertificateStore,
                                       Error,
                                       RecordFlags,
                                       CertificateCount,
                                       Identity,
                                       StoreName,
                                       Enumeration->Scope,
                                       NULL));
    }
    Mem_Free(Identity);
    return ZpStatus_IsSuccess(Enumeration->Status);
}

static
ZP_STATUS
ZpCertificate_Enumerate(
    _In_ BOOLEAN IncludeCertificates,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_CERTIFICATE_ENUMERATION Enumeration = {
        &Builder,
        CERT_SYSTEM_STORE_CURRENT_USER,
        L"user",
        IncludeCertificates
    };
    ZP_STATUS Status;

    if (!CertEnumSystemStore(Enumeration.Location, NULL, &Enumeration, ZpCertificate_EnumerateStore))
    {
        Status = ZpStatus_IsSuccess(Enumeration.Status) ?
                     ZpCertificate_Win32Status(GetLastError()) : Enumeration.Status;
        goto Cleanup;
    }
    if (!ZpStatus_IsSuccess(Enumeration.Status))
    {
        Status = Enumeration.Status;
        goto Cleanup;
    }
    Enumeration.Location = CERT_SYSTEM_STORE_LOCAL_MACHINE;
    Enumeration.Scope = L"machine";
    if (!CertEnumSystemStore(Enumeration.Location, NULL, &Enumeration, ZpCertificate_EnumerateStore))
    {
        Status = ZpStatus_IsSuccess(Enumeration.Status) ?
                     ZpCertificate_Win32Status(GetLastError()) : Enumeration.Status;
        goto Cleanup;
    }
    Status = ZpStatus_IsSuccess(Enumeration.Status) ?
                 ZpStatus_FromNtStatus(
                     ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength)) :
                 Enumeration.Status;

Cleanup:
    ZpAdministration_FreeBuilder(&Builder);
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateCertificates(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    return ZpCertificate_Enumerate(TRUE, Response, ResponseLength);
}

static
ZP_STATUS
ZpAdministration_EnumerateCertificateStores(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    return ZpCertificate_Enumerate(FALSE, Response, ResponseLength);
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
ZP_STATUS
ZpCertificate_AddDetails(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PCWSTR Identity,
    _In_ DWORD Location,
    _In_ PCCERT_CONTEXT Certificate)
{
    PCCERT_CHAIN_CONTEXT Chain = NULL;
    CERT_CHAIN_PARA Parameters = { sizeof(Parameters) };
    PWSTR Name, Issuer;
    NTSTATUS NtStatus;
    ULONG ChainIndex, DetailsState = ERROR_SUCCESS;
    ZP_STATUS Status;

    Name = ZpCertificate_GetName(Certificate, 0);
    Issuer = ZpCertificate_GetName(Certificate, CERT_NAME_ISSUER_FLAG);
    if (!CertGetCertificateChain(Location == CERT_SYSTEM_STORE_LOCAL_MACHINE ? HCCE_LOCAL_MACHINE : NULL,
                                 Certificate,
                                 NULL,
                                 Certificate->hCertStore,
                                 &Parameters,
                                 0,
                                 NULL,
                                 &Chain))
    {
        DetailsState = GetLastError();
    }
    else if (Chain->cChain == 0)
    {
        DetailsState = ERROR_NOT_FOUND;
    }
    NtStatus = ZpAdministration_AddRecord(Builder,
                                           ZpAdministrationKindCertificateDetails,
                                           DetailsState,
                                           ZpCertificate_GetFlags(Certificate),
                                           ZpCertificate_FileTime(&Certificate->pCertInfo->NotAfter),
                                           Identity,
                                           Name,
                                           Issuer,
                                           NULL);
    Status = ZpStatus_FromNtStatus(NtStatus);
    Mem_Free(Issuer);
    Mem_Free(Name);
    if (Chain != NULL && Chain->cChain != 0)
    {
        for (ChainIndex = 0;
             ChainIndex < Chain->rgpChain[0]->cElement && ZpStatus_IsSuccess(Status);
             ChainIndex++)
        {
            PCERT_CHAIN_ELEMENT Element = Chain->rgpChain[0]->rgpElement[ChainIndex];
            PWSTR ElementIdentity;

            Name = ZpCertificate_GetName(Element->pCertContext, 0);
            Issuer = ZpCertificate_GetName(Element->pCertContext, CERT_NAME_ISSUER_FLAG);
            Status = ZpCertificate_GetIdentity(L"chain",
                                               L"chain",
                                               Element->pCertContext,
                                               &ElementIdentity);
            if (ZpStatus_IsSuccess(Status))
            {
                Status = ZpStatus_FromNtStatus(
                    ZpAdministration_AddRecord(Builder,
                                               ZpAdministrationKindCertificateChain,
                                               Element->TrustStatus.dwErrorStatus,
                                               Element->TrustStatus.dwInfoStatus,
                                               ZpCertificate_FileTime(
                                                   &Element->pCertContext->pCertInfo->NotAfter),
                                               ElementIdentity,
                                               Name,
                                               Issuer,
                                               NULL));
                Mem_Free(ElementIdentity);
            }
            Mem_Free(Issuer);
            Mem_Free(Name);
        }
    }
    if (Chain != NULL) CertFreeCertificateChain(Chain);
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
    DWORD Location;
    HCERTSTORE Store;
    PCCERT_CONTEXT Certificate;
    NTSTATUS NtStatus;
    ZP_STATUS Status;

    IdentityValue = ZpAdministration_CopyView(Identity);
    if (IdentityValue == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    NtStatus = ZpCertificate_ParseIdentity(Identity,
                                           TRUE,
                                           &IdentityBuffer,
                                           &Location,
                                           &StoreName,
                                           &Thumbprint);
    if (!NT_SUCCESS(NtStatus))
    {
        Mem_Free(IdentityValue);
        return ZpStatus_FromNtStatus(NtStatus);
    }
    Store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W,
                          0,
                          0,
                          Location | CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG,
                          StoreName);
    if (Store == NULL)
    {
        Status = ZpCertificate_Win32Status(GetLastError());
    }
    else
    {
        Certificate = ZpCertificate_Find(Store, Thumbprint);
        if (Certificate == NULL)
        {
            Status = ZpCertificate_Win32Status(GetLastError());
        }
        else
        {
            Status = ZpCertificate_AddDetails(&Builder, IdentityValue, Location, Certificate);
            CertFreeCertificateContext(Certificate);
        }
        CertCloseStore(Store, 0);
    }
    Mem_Free(IdentityBuffer);
    Mem_Free(IdentityValue);
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength));
    }
    ZpAdministration_FreeBuilder(&Builder);
    return Status;
}

static
ZP_STATUS
ZpAdministration_QueryCertificateData(
    _In_ PCZP_STRING_VIEW Identity,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PWSTR IdentityBuffer, StoreName, Thumbprint;
    DWORD Location, Error = ERROR_SUCCESS;
    HCERTSTORE Store;
    PCCERT_CONTEXT Certificate;
    PBYTE Data = NULL;
    NTSTATUS Status;

    Status = ZpCertificate_ParseIdentity(Identity,
                                         TRUE,
                                         &IdentityBuffer,
                                         &Location,
                                         &StoreName,
                                         &Thumbprint);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W,
                          0,
                          0,
                          Location | CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG,
                          StoreName);
    if (Store == NULL)
    {
        Error = GetLastError();
    }
    else
    {
        Certificate = ZpCertificate_Find(Store, Thumbprint);
        if (Certificate == NULL)
        {
            Error = GetLastError();
        }
        else if (Certificate->cbCertEncoded == 0 ||
                 Certificate->cbCertEncoded > ZP_CERTIFICATE_MAX_ENCODED_LENGTH)
        {
            Status = STATUS_INVALID_BUFFER_SIZE;
        }
        else
        {
            Data = Mem_Alloc(Certificate->cbCertEncoded);
            if (Data == NULL)
            {
                Status = STATUS_NO_MEMORY;
            }
            else
            {
                RtlCopyMemory(Data, Certificate->pbCertEncoded, Certificate->cbCertEncoded);
                *Response = Data;
                *ResponseLength = Certificate->cbCertEncoded;
            }
        }
        if (Certificate != NULL) CertFreeCertificateContext(Certificate);
        CertCloseStore(Store, 0);
    }
    Mem_Free(IdentityBuffer);
    return Error != ERROR_SUCCESS ?
               ZpCertificate_Win32Status(Error) :
               ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpCertificate_ImportSubject(
    _In_ HCERTSTORE Store,
    _In_ DWORD Location,
    _In_ PCCRYPTUI_WIZ_IMPORT_SRC_INFO Source)
{
    DWORD Flags = CRYPTUI_WIZ_NO_UI | CRYPTUI_WIZ_IMPORT_ALLOW_CERT |
                  (Location == CERT_SYSTEM_STORE_CURRENT_USER ?
                       CRYPTUI_WIZ_IMPORT_TO_CURRENTUSER :
                       CRYPTUI_WIZ_IMPORT_TO_LOCALMACHINE);

    return CryptUIWizImport(Flags, NULL, NULL, Source, Store) ?
               ZpStatus_Make(ZpStatusNone, 0) :
               ZpCertificate_Win32Status(GetLastError());
}

static
ZP_STATUS
ZpCertificate_InstallFile(
    _In_ HCERTSTORE Store,
    _In_ DWORD Location,
    _In_ PCWSTR Path,
    _In_ PCWSTR Password,
    _In_ BOOLEAN Exportable)
{
    CRYPTUI_WIZ_IMPORT_SRC_INFO Source = { sizeof(Source) };

    Source.dwSubjectChoice = CRYPTUI_WIZ_IMPORT_SUBJECT_FILE;
    Source.pwszFileName = Path;
    Source.dwFlags = (Location == CERT_SYSTEM_STORE_CURRENT_USER ?
                          CRYPT_USER_KEYSET :
                          CRYPT_MACHINE_KEYSET) |
                     (Exportable ? CRYPT_EXPORTABLE : 0);
    Source.pwszPassword = Password;
    return ZpCertificate_ImportSubject(Store, Location, &Source);
}

static
ZP_STATUS
ZpCertificate_InstallData(
    _In_ HCERTSTORE Store,
    _In_ DWORD Location,
    _In_reads_bytes_(DataLength) const BYTE* Data,
    _In_ ULONG DataLength,
    _In_ PCWSTR Password,
    _In_ BOOLEAN Exportable)
{
    CRYPTUI_WIZ_IMPORT_SRC_INFO Source = { sizeof(Source) };
    CRYPT_DATA_BLOB Blob = { DataLength, (PBYTE)Data };
    PCCERT_CONTEXT Certificate = NULL;
    HCERTSTORE SourceStore = NULL;
    ZP_STATUS Status;

    Source.pwszPassword = Password;
    if (PFXIsPFXBlob(&Blob))
    {
        DWORD Flags = PKCS12_INCLUDE_EXTENDED_PROPERTIES |
                      (Location == CERT_SYSTEM_STORE_CURRENT_USER ?
                           CRYPT_USER_KEYSET :
                           CRYPT_MACHINE_KEYSET) |
                      (Exportable ? CRYPT_EXPORTABLE : 0);

        SourceStore = PFXImportCertStore(&Blob, Password, Flags);
        if (SourceStore == NULL) return ZpCertificate_Win32Status(GetLastError());
        Source.dwSubjectChoice = CRYPTUI_WIZ_IMPORT_SUBJECT_CERT_STORE;
        Source.hCertStore = SourceStore;
    }
    else
    {
        if (!CryptQueryObject(CERT_QUERY_OBJECT_BLOB,
                              &Blob,
                              CERT_QUERY_CONTENT_FLAG_CERT,
                              CERT_QUERY_FORMAT_FLAG_ALL,
                              0,
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              (const VOID**)&Certificate))
        {
            return ZpCertificate_Win32Status(GetLastError());
        }
        Source.dwSubjectChoice = CRYPTUI_WIZ_IMPORT_SUBJECT_CERT_CONTEXT;
        Source.pCertContext = Certificate;
    }
    Status = ZpCertificate_ImportSubject(Store, Location, &Source);
    if (Certificate != NULL) CertFreeCertificateContext(Certificate);
    if (SourceStore != NULL) CertCloseStore(SourceStore, 0);
    return Status;
}

static
ZP_STATUS
ZpAdministration_ControlCertificateData(
    _In_ PCZP_ADMINISTRATION_DATA_CONTROL_VIEW Control)
{
    ZP_STRING_VIEW IdentityView;
    PWSTR Identity, StoreName, Thumbprint;
    PWSTR Password = NULL, Path = NULL;
    const BYTE* Source = NULL;
    ULONG PasswordLength = 0, SourceLength = 0, Index;
    SIZE_T PasswordBytes;
    DWORD Location, Error = ERROR_SUCCESS;
    HCERTSTORE Store;
    PCCERT_CONTEXT Certificate;
    NTSTATUS NtStatus;
    ZP_STATUS Status = ZpStatus_Make(ZpStatusNone, 0);

    if (Control->Action != ZpAdministrationActionInstall &&
        Control->Action != ZpAdministrationActionDelete ||
        FlagOn(Control->Flags,
               ~(ZP_CERTIFICATE_INSTALL_FLAG_SOURCE_PATH |
                 ZP_CERTIFICATE_INSTALL_FLAG_EXPORTABLE)) ||
        (Control->Action == ZpAdministrationActionInstall &&
         (Control->Data.Length <= sizeof(ULONG) ||
          Control->Data.Length > ZP_CERTIFICATE_MAX_ENCODED_LENGTH)) ||
        (Control->Action == ZpAdministrationActionDelete &&
         (Control->Flags != 0 || Control->Data.Length != 0)))
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    NtStatus = ZpAdministration_GetDataControlIdentityString(Control, &IdentityView);
    if (!NT_SUCCESS(NtStatus)) return ZpStatus_FromNtStatus(NtStatus);
    NtStatus = ZpCertificate_ParseIdentity(&IdentityView,
                                           Control->Action == ZpAdministrationActionDelete,
                                           &Identity,
                                           &Location,
                                           &StoreName,
                                           &Thumbprint);
    if (!NT_SUCCESS(NtStatus)) return ZpStatus_FromNtStatus(NtStatus);
    if (Control->Action == ZpAdministrationActionInstall)
    {
        RtlCopyMemory(&PasswordLength, Control->Data.Buffer, sizeof(PasswordLength));
        PasswordBytes = (SIZE_T)PasswordLength * sizeof(WCHAR);
        if (PasswordLength > 32767 ||
            PasswordBytes > Control->Data.Length - sizeof(PasswordLength))
        {
            NtStatus = STATUS_INVALID_BUFFER_SIZE;
            goto Cleanup;
        }
        Source = Add2Ptr(Control->Data.Buffer, sizeof(PasswordLength) + PasswordBytes);
        SourceLength = Control->Data.Length - sizeof(PasswordLength) - (ULONG)PasswordBytes;
        if (SourceLength == 0)
        {
            NtStatus = STATUS_INVALID_BUFFER_SIZE;
            goto Cleanup;
        }
        Password = Mem_Alloc(PasswordBytes + sizeof(WCHAR));
        if (Password == NULL)
        {
            NtStatus = STATUS_NO_MEMORY;
            goto Cleanup;
        }
        RtlCopyMemory(Password, Add2Ptr(Control->Data.Buffer, sizeof(PasswordLength)), PasswordBytes);
        Password[PasswordLength] = UNICODE_NULL;
        for (Index = 0; Index < PasswordLength; Index++)
        {
            if (Password[Index] == UNICODE_NULL)
            {
                NtStatus = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }
        }
        if (FlagOn(Control->Flags, ZP_CERTIFICATE_INSTALL_FLAG_SOURCE_PATH))
        {
            ULONG CharacterCount;

            if (FlagOn(SourceLength, sizeof(WCHAR) - 1) ||
                (CharacterCount = SourceLength / sizeof(WCHAR)) > 32767)
            {
                NtStatus = STATUS_INVALID_BUFFER_SIZE;
                goto Cleanup;
            }
            Path = Mem_Alloc((SIZE_T)SourceLength + sizeof(WCHAR));
            if (Path == NULL)
            {
                NtStatus = STATUS_NO_MEMORY;
                goto Cleanup;
            }
            RtlCopyMemory(Path, Source, SourceLength);
            Path[CharacterCount] = UNICODE_NULL;
            for (Index = 0; Index < CharacterCount; Index++)
            {
                if (Path[Index] == UNICODE_NULL)
                {
                    NtStatus = STATUS_INVALID_PARAMETER;
                    goto Cleanup;
                }
            }
        }
    }
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
    else if (Path != NULL)
    {
        Status = ZpCertificate_InstallFile(Store,
                                           Location,
                                           Path,
                                           Password,
                                           FlagOn(Control->Flags,
                                                  ZP_CERTIFICATE_INSTALL_FLAG_EXPORTABLE));
    }
    else Status = ZpCertificate_InstallData(Store,
                                            Location,
                                            Source,
                                            SourceLength,
                                            Password,
                                            FlagOn(Control->Flags,
                                                   ZP_CERTIFICATE_INSTALL_FLAG_EXPORTABLE));
    if (Store != NULL) CertCloseStore(Store, 0);

Cleanup:
    Mem_Free(Path);
    if (Password != NULL)
    {
        RtlSecureZeroMemory(Password, PasswordBytes + sizeof(WCHAR));
        Mem_Free(Password);
    }
    Mem_Free(Identity);
    return Error != ERROR_SUCCESS ?
               ZpCertificate_Win32Status(Error) :
           !NT_SUCCESS(NtStatus) ?
               ZpStatus_FromNtStatus(NtStatus) : Status;
}
