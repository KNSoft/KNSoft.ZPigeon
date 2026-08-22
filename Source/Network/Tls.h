#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

#define SECURITY_WIN32
#define SCHANNEL_USE_BLACKLISTS
#include <Schannel.h>
#include <Sspi.h>

#define ZP_TLS_MAX_BUFFER_SIZE 0x00100000UL

typedef enum _ZP_TLS_ROLE
{
    ZpTlsClient,
    ZpTlsServer
} ZP_TLS_ROLE;

typedef
NTSTATUS
(NTAPI *ZP_TLS_PLAINTEXT_CALLBACK)(
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_opt_ PVOID Context);

typedef struct _ZP_TLS_CONTEXT
{
    ZP_TLS_ROLE Role;
    PCredHandle Credential;
    CtxtHandle Handle;
    LOGICAL HandleInitialized;
    LOGICAL HandshakeComplete;
    SecPkgContext_StreamSizes StreamSizes;
    PBYTE Input;
    ULONG InputOffset;
    ULONG InputLength;
    ULONG InputSize;
    PCWSTR ServerName;
} ZP_TLS_CONTEXT, *PZP_TLS_CONTEXT;

ZP_STATUS
ZpTls_AcquireClientCredentials(
    _Out_ PCredHandle Credential);

ZP_STATUS
ZpTls_AcquireServerCredentials(
    _Out_ PCredHandle Credential,
    _In_reads_(CertificateCount) PCCERT_CONTEXT* Certificates,
    _In_ ULONG CertificateCount);

VOID
ZpTls_FreeCredentials(
    _Inout_ PCredHandle Credential);

VOID
ZpTls_Initialize(
    _Out_ PZP_TLS_CONTEXT Context,
    _In_ ZP_TLS_ROLE Role,
    _In_ PCredHandle Credential,
    _In_opt_ PCWSTR ServerName);

ZP_STATUS
ZpTls_Handshake(
    _Inout_ PZP_TLS_CONTEXT Context,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Outptr_result_bytebuffer_maybenull_(*TokenLength) PBYTE* Token,
    _Out_ PULONG TokenLength,
    _Out_ PLOGICAL Complete);

ZP_STATUS
ZpTls_EncryptFrame(
    _Inout_ PZP_TLS_CONTEXT Context,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength,
    _Outptr_result_bytebuffer_(*EncryptedLength) PBYTE* Encrypted,
    _Out_ PULONG EncryptedLength);

ZP_STATUS
ZpTls_Decrypt(
    _Inout_ PZP_TLS_CONTEXT Context,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ZP_TLS_PLAINTEXT_CALLBACK Callback,
    _In_opt_ PVOID CallbackContext);

ZP_STATUS
ZpTls_GetRemoteCertificate(
    _In_ PZP_TLS_CONTEXT Context,
    _Outptr_ PCCERT_CONTEXT* Certificate);

VOID
ZpTls_Uninitialize(
    _Inout_ PZP_TLS_CONTEXT Context);
