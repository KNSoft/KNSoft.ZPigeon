#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

#define SECURITY_WIN32
#define SCHANNEL_USE_BLACKLISTS
#include <Schannel.h>
#include <Sspi.h>
#include <WinSock2.h>

#define ZP_DTLS_MTU 1200UL

typedef enum _ZP_DTLS_ROLE
{
    ZpDtlsClient,
    ZpDtlsServer
} ZP_DTLS_ROLE;

typedef struct _ZP_DTLS_CONTEXT
{
    ZP_DTLS_ROLE Role;
    PCredHandle Credential;
    CtxtHandle Handle;
    LOGICAL HandleInitialized;
    LOGICAL HandshakeComplete;
    SecPkgContext_StreamSizes StreamSizes;
    PCWSTR ServerName;
} ZP_DTLS_CONTEXT, *PZP_DTLS_CONTEXT;

ZP_STATUS
ZpDtls_AcquireClientCredentials(
    _Out_ PCredHandle Credential);

ZP_STATUS
ZpDtls_AcquireServerCredentials(
    _Out_ PCredHandle Credential,
    _In_reads_(CertificateCount) PCCERT_CONTEXT* Certificates,
    _In_ ULONG CertificateCount);

VOID
ZpDtls_FreeCredentials(
    _Inout_ PCredHandle Credential);

VOID
ZpDtls_Initialize(
    _Out_ PZP_DTLS_CONTEXT Context,
    _In_ ZP_DTLS_ROLE Role,
    _In_ PCredHandle Credential,
    _In_opt_ PCWSTR ServerName);

ZP_STATUS
ZpDtls_Handshake(
    _Inout_ PZP_DTLS_CONTEXT Context,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_reads_bytes_opt_(AddressLength) const SOCKADDR* Address,
    _In_ INT AddressLength,
    _Outptr_result_bytebuffer_maybenull_(*TokenLength) PBYTE* Token,
    _Out_ PULONG TokenLength,
    _Out_ PLOGICAL More,
    _Out_ PLOGICAL Complete);

ZP_STATUS
ZpDtls_Encrypt(
    _Inout_ PZP_DTLS_CONTEXT Context,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG EncryptedLength);

ZP_STATUS
ZpDtls_Decrypt(
    _Inout_ PZP_DTLS_CONTEXT Context,
    _Inout_updates_bytes_(DataLength) PVOID Data,
    _In_ ULONG DataLength,
    _Outptr_result_bytebuffer_(*PlaintextLength) const BYTE** Plaintext,
    _Out_ PULONG PlaintextLength);

ZP_STATUS
ZpDtls_GetRemoteCertificate(
    _In_ PZP_DTLS_CONTEXT Context,
    _Outptr_ PCCERT_CONTEXT* Certificate);

VOID
ZpDtls_Uninitialize(
    _Inout_ PZP_DTLS_CONTEXT Context);
