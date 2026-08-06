#include "Dtls.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#pragma comment(lib, "Secur32.lib")

#define ZP_DTLS_CLIENT_FLAGS (ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | \
                              ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | \
                              ISC_REQ_DATAGRAM)
#define ZP_DTLS_SERVER_FLAGS (ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY | \
                              ASC_REQ_EXTENDED_ERROR | ASC_REQ_ALLOCATE_MEMORY | \
                              ASC_REQ_DATAGRAM)

static
ZP_STATUS
ZpDtls_FromSecurityStatus(
    _In_ SECURITY_STATUS Status)
{
    return ZpStatus_FromCode(ZpStatusSecurity, (ULONG)Status);
}

static
ZP_STATUS
ZpDtls_AcquireCredentials(
    _Out_ PCredHandle Credential,
    _In_ ULONG Use,
    _Inout_ PSCHANNEL_CRED Credentials)
{
    TimeStamp Expiry;

    return ZpDtls_FromSecurityStatus(AcquireCredentialsHandleW(NULL,
                                                               UNISP_NAME_W,
                                                               Use,
                                                               NULL,
                                                               Credentials,
                                                               NULL,
                                                               NULL,
                                                               Credential,
                                                               &Expiry));
}

ZP_STATUS
ZpDtls_AcquireClientCredentials(
    _Out_ PCredHandle Credential)
{
    SCHANNEL_CRED Credentials = { SCHANNEL_CRED_VERSION };

    Credentials.grbitEnabledProtocols = SP_PROT_DTLS1_2_CLIENT;
    Credentials.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION |
                          SCH_CRED_NO_DEFAULT_CREDS |
                          SCH_USE_STRONG_CRYPTO;
    return ZpDtls_AcquireCredentials(Credential,
                                     SECPKG_CRED_OUTBOUND,
                                     &Credentials);
}

ZP_STATUS
ZpDtls_AcquireServerCredentials(
    _Out_ PCredHandle Credential,
    _In_reads_(CertificateCount) PCCERT_CONTEXT* Certificates,
    _In_ ULONG CertificateCount)
{
    SCHANNEL_CRED Credentials = { SCHANNEL_CRED_VERSION };

    Credentials.cCreds = CertificateCount;
    Credentials.paCred = Certificates;
    Credentials.grbitEnabledProtocols = SP_PROT_DTLS1_2_SERVER;
    Credentials.dwFlags = SCH_CRED_NO_SYSTEM_MAPPER | SCH_USE_STRONG_CRYPTO;
    return ZpDtls_AcquireCredentials(Credential,
                                     SECPKG_CRED_INBOUND,
                                     &Credentials);
}

VOID
ZpDtls_FreeCredentials(
    _Inout_ PCredHandle Credential)
{
    if (SecIsValidHandle(Credential))
    {
        FreeCredentialsHandle(Credential);
        SecInvalidateHandle(Credential);
    }
}

VOID
ZpDtls_Initialize(
    _Out_ PZP_DTLS_CONTEXT Context,
    _In_ ZP_DTLS_ROLE Role,
    _In_ PCredHandle Credential,
    _In_opt_ PCWSTR ServerName)
{
    RtlZeroMemory(Context, sizeof(*Context));
    SecInvalidateHandle(&Context->Handle);
    Context->Role = Role;
    Context->Credential = Credential;
    Context->ServerName = ServerName;
}

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
    _Out_ PLOGICAL Complete)
{
    SecBuffer InputBuffers[3] = { 0 };
    SecBuffer OutputBuffers[3] = { 0 };
    SecBufferDesc InputDescriptor = { SECBUFFER_VERSION, 2, InputBuffers };
    SecBufferDesc OutputDescriptor = { SECBUFFER_VERSION, RTL_NUMBER_OF(OutputBuffers), OutputBuffers };
    TimeStamp Expiry;
    SECURITY_STATUS SecurityStatus, CompletionStatus;
    ULONG Attributes, Mtu = ZP_DTLS_MTU;
    PBYTE OutputToken = NULL;
    ULONG OutputTokenLength = 0;
    LOGICAL HasMore = FALSE, HandshakeComplete = FALSE;
    ZP_STATUS Status;

    if ((DataLength != 0 && Data == NULL) ||
        (Context->Role == ZpDtlsServer && (Address == NULL || AddressLength <= 0)))
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    InputBuffers[0].BufferType = SECBUFFER_TOKEN;
    InputBuffers[0].pvBuffer = (PVOID)Data;
    InputBuffers[0].cbBuffer = DataLength;
    InputBuffers[1].BufferType = SECBUFFER_EMPTY;
    if (Context->Role == ZpDtlsServer)
    {
        InputBuffers[2].BufferType = SECBUFFER_EXTRA;
        InputBuffers[2].pvBuffer = (PVOID)Address;
        InputBuffers[2].cbBuffer = AddressLength;
        InputDescriptor.cBuffers++;
    }
    OutputBuffers[0].BufferType = SECBUFFER_TOKEN;
    OutputBuffers[1].BufferType = SECBUFFER_ALERT;
    OutputBuffers[2].BufferType = SECBUFFER_EMPTY;
    if (Context->Role == ZpDtlsClient)
    {
        SecurityStatus = InitializeSecurityContextW(
            Context->Credential,
            Context->HandleInitialized ? &Context->Handle : NULL,
            (PWSTR)Context->ServerName,
            ZP_DTLS_CLIENT_FLAGS,
            0,
            SECURITY_NATIVE_DREP,
            Context->HandleInitialized || DataLength != 0 ? &InputDescriptor : NULL,
            0,
            &Context->Handle,
            &OutputDescriptor,
            &Attributes,
            &Expiry);
    }
    else
    {
        SecurityStatus = AcceptSecurityContext(Context->Credential,
                                               Context->HandleInitialized ? &Context->Handle : NULL,
                                               &InputDescriptor,
                                               ZP_DTLS_SERVER_FLAGS,
                                               SECURITY_NATIVE_DREP,
                                               &Context->Handle,
                                               &OutputDescriptor,
                                               &Attributes,
                                               &Expiry);
    }
    if (SecurityStatus != SEC_E_INCOMPLETE_MESSAGE)
    {
        Context->HandleInitialized = TRUE;
    }
    CompletionStatus = SecurityStatus;
    if (SecurityStatus == SEC_I_COMPLETE_NEEDED ||
        SecurityStatus == SEC_I_COMPLETE_AND_CONTINUE)
    {
        SecurityStatus = CompleteAuthToken(&Context->Handle, &OutputDescriptor);
        if (SecurityStatus != SEC_E_OK)
        {
            CompletionStatus = SecurityStatus;
        }
    }
    if (OutputBuffers[0].cbBuffer != 0)
    {
        OutputToken = Mem_Alloc(OutputBuffers[0].cbBuffer);
        if (OutputToken == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            goto Cleanup;
        }
        RtlCopyMemory(OutputToken, OutputBuffers[0].pvBuffer, OutputBuffers[0].cbBuffer);
        OutputTokenLength = OutputBuffers[0].cbBuffer;
    }
    if (CompletionStatus == SEC_I_MESSAGE_FRAGMENT)
    {
        HasMore = TRUE;
        goto Succeeded;
    }
    if (CompletionStatus == SEC_E_INCOMPLETE_MESSAGE ||
        CompletionStatus == SEC_I_CONTINUE_NEEDED ||
        CompletionStatus == SEC_I_COMPLETE_AND_CONTINUE)
    {
        goto Succeeded;
    }
    if (CompletionStatus != SEC_E_OK && CompletionStatus != SEC_I_COMPLETE_NEEDED)
    {
        Status = ZpDtls_FromSecurityStatus(CompletionStatus);
        goto Cleanup;
    }
    SecurityStatus = SetContextAttributesW(&Context->Handle,
                                            SECPKG_ATTR_DTLS_MTU,
                                            &Mtu,
                                            sizeof(Mtu));
    if (SecurityStatus == SEC_E_OK)
    {
        SecurityStatus = QueryContextAttributesW(&Context->Handle,
                                                  SECPKG_ATTR_STREAM_SIZES,
                                                  &Context->StreamSizes);
    }
    if (SecurityStatus != SEC_E_OK)
    {
        Status = ZpDtls_FromSecurityStatus(SecurityStatus);
        goto Cleanup;
    }
    Context->HandshakeComplete = TRUE;
    HandshakeComplete = TRUE;

Succeeded:
    *Token = OutputToken;
    *TokenLength = OutputTokenLength;
    *More = HasMore;
    *Complete = HandshakeComplete;
    OutputToken = NULL;
    Status = ZpStatus_FromNtStatus(STATUS_SUCCESS);

Cleanup:
    if (OutputBuffers[0].pvBuffer != NULL)
    {
        FreeContextBuffer(OutputBuffers[0].pvBuffer);
    }
    if (OutputBuffers[1].pvBuffer != NULL)
    {
        FreeContextBuffer(OutputBuffers[1].pvBuffer);
    }
    if (OutputToken != NULL)
    {
        Mem_Free(OutputToken);
    }
    return Status;
}

ZP_STATUS
ZpDtls_Encrypt(
    _Inout_ PZP_DTLS_CONTEXT Context,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Out_writes_bytes_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG EncryptedLength)
{
    SecBuffer Buffers[4] = { 0 };
    SecBufferDesc Descriptor = { SECBUFFER_VERSION, RTL_NUMBER_OF(Buffers), Buffers };
    ULONG Size;
    SECURITY_STATUS Status;

    if (!Context->HandshakeComplete || Data == NULL || DataLength == 0 ||
        Context->StreamSizes.cbHeader > ZP_DTLS_MTU ||
        Context->StreamSizes.cbTrailer > ZP_DTLS_MTU - Context->StreamSizes.cbHeader ||
        DataLength > ZP_DTLS_MTU - Context->StreamSizes.cbHeader - Context->StreamSizes.cbTrailer)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_BUFFER_SIZE);
    }
    Size = Context->StreamSizes.cbHeader + DataLength + Context->StreamSizes.cbTrailer;
    if (Buffer == NULL || BufferSize < Size)
    {
        return ZpStatus_FromNtStatus(STATUS_BUFFER_TOO_SMALL);
    }
    Buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
    Buffers[0].pvBuffer = Buffer;
    Buffers[0].cbBuffer = Context->StreamSizes.cbHeader;
    Buffers[1].BufferType = SECBUFFER_DATA;
    Buffers[1].pvBuffer = Add2Ptr(Buffer, Context->StreamSizes.cbHeader);
    Buffers[1].cbBuffer = DataLength;
    Buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
    Buffers[2].pvBuffer = Add2Ptr(Buffers[1].pvBuffer, DataLength);
    Buffers[2].cbBuffer = Context->StreamSizes.cbTrailer;
    Buffers[3].BufferType = SECBUFFER_EMPTY;
    RtlCopyMemory(Buffers[1].pvBuffer, Data, DataLength);
    Status = EncryptMessage(&Context->Handle, 0, &Descriptor, 0);
    if (Status != SEC_E_OK)
    {
        return ZpDtls_FromSecurityStatus(Status);
    }
    *EncryptedLength = Buffers[0].cbBuffer + Buffers[1].cbBuffer + Buffers[2].cbBuffer;
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

ZP_STATUS
ZpDtls_Decrypt(
    _Inout_ PZP_DTLS_CONTEXT Context,
    _Inout_updates_bytes_(DataLength) PVOID Data,
    _In_ ULONG DataLength,
    _Outptr_result_bytebuffer_(*PlaintextLength) const BYTE** Plaintext,
    _Out_ PULONG PlaintextLength)
{
    SecBuffer Buffers[4] = { 0 };
    SecBufferDesc Descriptor = { SECBUFFER_VERSION, RTL_NUMBER_OF(Buffers), Buffers };
    SECURITY_STATUS SecurityStatus;
    ULONG Index;

    if (!Context->HandshakeComplete || Data == NULL || DataLength == 0)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Buffers[0].BufferType = SECBUFFER_DATA;
    Buffers[0].pvBuffer = Data;
    Buffers[0].cbBuffer = DataLength;
    SecurityStatus = DecryptMessage(&Context->Handle, &Descriptor, 0, NULL);
    if (SecurityStatus != SEC_E_OK)
    {
        return ZpDtls_FromSecurityStatus(SecurityStatus);
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Buffers); Index++)
    {
        if (Buffers[Index].BufferType == SECBUFFER_DATA && Buffers[Index].cbBuffer != 0)
        {
            *Plaintext = Buffers[Index].pvBuffer;
            *PlaintextLength = Buffers[Index].cbBuffer;
            return ZpStatus_FromNtStatus(STATUS_SUCCESS);
        }
    }
    return ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
}

ZP_STATUS
ZpDtls_GetRemoteCertificate(
    _In_ PZP_DTLS_CONTEXT Context,
    _Outptr_ PCCERT_CONTEXT* Certificate)
{
    return ZpDtls_FromSecurityStatus(QueryContextAttributesW(&Context->Handle,
                                                              SECPKG_ATTR_REMOTE_CERT_CONTEXT,
                                                              Certificate));
}

VOID
ZpDtls_Uninitialize(
    _Inout_ PZP_DTLS_CONTEXT Context)
{
    if (Context->HandleInitialized)
    {
        DeleteSecurityContext(&Context->Handle);
    }
    RtlZeroMemory(Context, sizeof(*Context));
}
