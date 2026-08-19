#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

#include <Wincrypt.h>

typedef struct _ZP_CERTIFICATE_VALIDATOR
{
    HCERTSTORE RootStore;
    HCERTCHAINENGINE ChainEngine;
} ZP_CERTIFICATE_VALIDATOR, *PZP_CERTIFICATE_VALIDATOR;

ZP_STATUS
ZpCertificateValidator_Initialize(
    _Out_ PZP_CERTIFICATE_VALIDATOR Validator,
    _In_reads_bytes_(RootCertificateLength) const BYTE* RootCertificate,
    _In_ ULONG RootCertificateLength);

ZP_STATUS
ZpCertificateValidator_ValidateServer(
    _In_ PZP_CERTIFICATE_VALIDATOR Validator,
    _In_ PCCERT_CONTEXT Certificate,
    _In_ PCWSTR ServerName);

VOID
ZpCertificateValidator_Uninitialize(
    _Inout_ PZP_CERTIFICATE_VALIDATOR Validator);
