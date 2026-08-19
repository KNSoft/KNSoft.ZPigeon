#include "Certificate.h"

ZP_STATUS
ZpCertificateValidator_Initialize(
    _Out_ PZP_CERTIFICATE_VALIDATOR Validator,
    _In_reads_bytes_(RootCertificateLength) const BYTE* RootCertificateBuffer,
    _In_ ULONG RootCertificateLength)
{
    CERT_CHAIN_ENGINE_CONFIG EngineConfig = { sizeof(EngineConfig) };
    PCCERT_CONTEXT RootCertificate;
    ULONG Error;

    RtlZeroMemory(Validator, sizeof(*Validator));
    RootCertificate = CertCreateCertificateContext(X509_ASN_ENCODING,
                                                   RootCertificateBuffer,
                                                   RootCertificateLength);
    if (RootCertificate == NULL)
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    Validator->RootStore = CertOpenStore(CERT_STORE_PROV_MEMORY,
                                         X509_ASN_ENCODING,
                                         0,
                                         CERT_STORE_CREATE_NEW_FLAG,
                                         NULL);
    if (Validator->RootStore == NULL)
    {
        Error = GetLastError();
        CertFreeCertificateContext(RootCertificate);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    if (!CertAddCertificateContextToStore(Validator->RootStore,
                                          RootCertificate,
                                          CERT_STORE_ADD_ALWAYS,
                                          NULL))
    {
        Error = GetLastError();
        CertFreeCertificateContext(RootCertificate);
        ZpCertificateValidator_Uninitialize(Validator);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    CertFreeCertificateContext(RootCertificate);
    EngineConfig.hExclusiveRoot = Validator->RootStore;
    if (!CertCreateCertificateChainEngine(&EngineConfig, &Validator->ChainEngine))
    {
        Error = GetLastError();
        ZpCertificateValidator_Uninitialize(Validator);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

ZP_STATUS
ZpCertificateValidator_ValidateServer(
    _In_ PZP_CERTIFICATE_VALIDATOR Validator,
    _In_ PCCERT_CONTEXT Certificate,
    _In_ PCWSTR ServerName)
{
    CERT_CHAIN_PARA ChainParameters = { sizeof(ChainParameters) };
    CERT_CHAIN_POLICY_PARA PolicyParameters = { sizeof(PolicyParameters) };
    CERT_CHAIN_POLICY_STATUS PolicyStatus = { sizeof(PolicyStatus) };
    SSL_EXTRA_CERT_CHAIN_POLICY_PARA SslParameters = { sizeof(SslParameters) };
    PCCERT_CHAIN_CONTEXT Chain;
    ZP_STATUS Status;

    if (!CertGetCertificateChain(Validator->ChainEngine,
                                 Certificate,
                                 NULL,
                                 Certificate->hCertStore,
                                 &ChainParameters,
                                 CERT_CHAIN_CACHE_END_CERT,
                                 NULL,
                                 &Chain))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    SslParameters.dwAuthType = AUTHTYPE_SERVER;
    SslParameters.pwszServerName = (PWSTR)ServerName;
    PolicyParameters.pvExtraPolicyPara = &SslParameters;
    if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL,
                                          Chain,
                                          &PolicyParameters,
                                          &PolicyStatus))
    {
        Status = ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    else
    {
        Status = ZpStatus_FromCode(ZpStatusHResult, PolicyStatus.dwError);
    }
    CertFreeCertificateChain(Chain);
    return Status;
}

VOID
ZpCertificateValidator_Uninitialize(
    _Inout_ PZP_CERTIFICATE_VALIDATOR Validator)
{
    if (Validator->ChainEngine != NULL)
    {
        CertFreeCertificateChainEngine(Validator->ChainEngine);
    }
    if (Validator->RootStore != NULL)
    {
        CertCloseStore(Validator->RootStore, 0);
    }
    RtlZeroMemory(Validator, sizeof(*Validator));
}
