#include "Tls.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

#define ZP_TLS_CONTEXT_FLAGS (ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | \
                              ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR | \
                              ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM)

static
ZP_STATUS
ZpTls_FromSecurityStatus(
    _In_ SECURITY_STATUS Status)
{
    return ZpStatus_FromCode(ZpStatusSecurity, (ULONG)Status);
}

static
ZP_STATUS
ZpTls_AcquireCredentials(
    _Out_ PCredHandle Credential,
    _In_ ULONG Use,
    _Inout_ PSCH_CREDENTIALS Credentials)
{
    TimeStamp Expiry;
    SECURITY_STATUS Status;

    Status = AcquireCredentialsHandleW(NULL,
                                       UNISP_NAME_W,
                                       Use,
                                       NULL,
                                       Credentials,
                                       NULL,
                                       NULL,
                                       Credential,
                                       &Expiry);
    return ZpTls_FromSecurityStatus(Status);
}

ZP_STATUS
ZpTls_AcquireClientCredentials(
    _Out_ PCredHandle Credential)
{
    TLS_PARAMETERS Parameters = { 0 };
    SCH_CREDENTIALS Credentials = { 0 };

    Parameters.grbitDisabledProtocols = SP_PROT_SSL2 | SP_PROT_SSL3 |
                                        SP_PROT_TLS1_0 | SP_PROT_TLS1_1;
    Credentials.dwVersion = SCH_CREDENTIALS_VERSION;
    Credentials.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION |
                          SCH_CRED_NO_DEFAULT_CREDS |
                          SCH_CRED_DISABLE_RECONNECTS |
                          SCH_USE_STRONG_CRYPTO;
    Credentials.cTlsParameters = 1;
    Credentials.pTlsParameters = &Parameters;
    return ZpTls_AcquireCredentials(Credential,
                                    SECPKG_CRED_OUTBOUND,
                                    &Credentials);
}

ZP_STATUS
ZpTls_AcquireServerCredentials(
    _Out_ PCredHandle Credential,
    _In_reads_(CertificateCount) PCCERT_CONTEXT* Certificates,
    _In_ ULONG CertificateCount)
{
    TLS_PARAMETERS Parameters = { 0 };
    SCH_CREDENTIALS Credentials = { 0 };

    Parameters.grbitDisabledProtocols = SP_PROT_SSL2 | SP_PROT_SSL3 |
                                        SP_PROT_TLS1_0 | SP_PROT_TLS1_1;
    Credentials.dwVersion = SCH_CREDENTIALS_VERSION;
    Credentials.cCreds = CertificateCount;
    Credentials.paCred = Certificates;
    Credentials.dwFlags = SCH_CRED_NO_SYSTEM_MAPPER |
                          SCH_CRED_DISABLE_RECONNECTS |
                          SCH_USE_STRONG_CRYPTO;
    Credentials.cTlsParameters = 1;
    Credentials.pTlsParameters = &Parameters;
    return ZpTls_AcquireCredentials(Credential,
                                    SECPKG_CRED_INBOUND,
                                    &Credentials);
}

VOID
ZpTls_FreeCredentials(
    _Inout_ PCredHandle Credential)
{
    if (SecIsValidHandle(Credential))
    {
        FreeCredentialsHandle(Credential);
        SecInvalidateHandle(Credential);
    }
}

VOID
ZpTls_Initialize(
    _Out_ PZP_TLS_CONTEXT Context,
    _In_ ZP_TLS_ROLE Role,
    _In_ PCredHandle Credential,
    _In_opt_ PCWSTR ServerName)
{
    RtlZeroMemory(Context, sizeof(*Context));
    SecInvalidateHandle(&Context->Handle);
    Context->Role = Role;
    Context->Credential = Credential;
    Context->ServerName = ServerName;
}

static
ZP_STATUS
ZpTls_AppendInput(
    _Inout_ PZP_TLS_CONTEXT Context,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    PBYTE Buffer;
    ULONG Size;

    if (DataLength == 0)
    {
        return ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }
    if (Data == NULL ||
        Context->InputLength > ZP_TLS_MAX_BUFFER_SIZE - DataLength)
    {
        return ZpStatus_FromNtStatus(Data == NULL ?
                                        STATUS_INVALID_PARAMETER :
                                        STATUS_BUFFER_OVERFLOW);
    }
    if (Context->InputLength + DataLength > Context->InputSize)
    {
        Size = max(4096, Context->InputSize);
        while (Size < Context->InputLength + DataLength)
        {
            Size = min(Size * 2, ZP_TLS_MAX_BUFFER_SIZE);
        }
        Buffer = Mem_ReAlloc(Context->Input, Size);
        if (Buffer == NULL)
        {
            return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
        Context->Input = Buffer;
        Context->InputSize = Size;
    }
    RtlCopyMemory(Context->Input + Context->InputLength, Data, DataLength);
    Context->InputLength += DataLength;
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
VOID
ZpTls_ConsumeInput(
    _Inout_ PZP_TLS_CONTEXT Context,
    _In_ const SecBuffer* Extra)
{
    if (Extra != NULL && Extra->BufferType == SECBUFFER_EXTRA)
    {
        RtlMoveMemory(Context->Input,
                      Context->Input + Context->InputLength - Extra->cbBuffer,
                      Extra->cbBuffer);
        Context->InputLength = Extra->cbBuffer;
    }
    else
    {
        Context->InputLength = 0;
    }
}

ZP_STATUS
ZpTls_Handshake(
    _Inout_ PZP_TLS_CONTEXT Context,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Outptr_result_bytebuffer_maybenull_(*TokenLength) PBYTE* Token,
    _Out_ PULONG TokenLength,
    _Out_ PLOGICAL Complete)
{
    SecBuffer InputBuffers[2] = { 0 };
    SecBuffer OutputBuffer = { 0 };
    SecBufferDesc InputDescriptor = { SECBUFFER_VERSION, RTL_NUMBER_OF(InputBuffers), InputBuffers };
    SecBufferDesc OutputDescriptor = { SECBUFFER_VERSION, 1, &OutputBuffer };
    ULONG Attributes;
    TimeStamp Expiry;
    SECURITY_STATUS SecurityStatus, HandshakeStatus;
    ZP_STATUS Status;

    *Token = NULL;
    *TokenLength = 0;
    *Complete = FALSE;
    Status = ZpTls_AppendInput(Context, Data, DataLength);
    if (!ZpStatus_IsSuccess(Status))
    {
        return Status;
    }
    if (Context->Role == ZpTlsServer && Context->InputLength == 0)
    {
        return ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }
    InputBuffers[0].BufferType = SECBUFFER_TOKEN;
    InputBuffers[0].pvBuffer = Context->Input;
    InputBuffers[0].cbBuffer = Context->InputLength;
    InputBuffers[1].BufferType = SECBUFFER_EMPTY;
    OutputBuffer.BufferType = SECBUFFER_TOKEN;
    if (Context->Role == ZpTlsClient)
    {
        SecurityStatus = InitializeSecurityContextW(
            Context->Credential,
            Context->HandleInitialized ? &Context->Handle : NULL,
            (PWSTR)Context->ServerName,
            ZP_TLS_CONTEXT_FLAGS,
            0,
            SECURITY_NATIVE_DREP,
            Context->HandleInitialized || Context->InputLength != 0 ? &InputDescriptor : NULL,
            0,
            &Context->Handle,
            &OutputDescriptor,
            &Attributes,
            &Expiry);
    }
    else
    {
        SecurityStatus = AcceptSecurityContext(
            Context->Credential,
            Context->HandleInitialized ? &Context->Handle : NULL,
            &InputDescriptor,
            ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT |
                ASC_REQ_CONFIDENTIALITY | ASC_REQ_EXTENDED_ERROR |
                ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM,
            SECURITY_NATIVE_DREP,
            &Context->Handle,
            &OutputDescriptor,
            &Attributes,
            &Expiry);
    }
    HandshakeStatus = SecurityStatus;
    if (SecurityStatus != SEC_E_INCOMPLETE_MESSAGE)
    {
        Context->HandleInitialized = TRUE;
        ZpTls_ConsumeInput(Context, &InputBuffers[1]);
    }
    if (SecurityStatus == SEC_I_COMPLETE_NEEDED ||
        SecurityStatus == SEC_I_COMPLETE_AND_CONTINUE)
    {
        SecurityStatus = CompleteAuthToken(&Context->Handle, &OutputDescriptor);
        if (SecurityStatus != SEC_E_OK)
        {
            if (OutputBuffer.pvBuffer != NULL)
            {
                FreeContextBuffer(OutputBuffer.pvBuffer);
            }
            return ZpTls_FromSecurityStatus(SecurityStatus);
        }
    }
    if (OutputBuffer.cbBuffer != 0)
    {
        *Token = Mem_Alloc(OutputBuffer.cbBuffer);
        if (*Token == NULL)
        {
            FreeContextBuffer(OutputBuffer.pvBuffer);
            return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
        RtlCopyMemory(*Token, OutputBuffer.pvBuffer, OutputBuffer.cbBuffer);
        *TokenLength = OutputBuffer.cbBuffer;
        FreeContextBuffer(OutputBuffer.pvBuffer);
    }
    if (HandshakeStatus == SEC_E_INCOMPLETE_MESSAGE ||
        HandshakeStatus == SEC_I_CONTINUE_NEEDED ||
        HandshakeStatus == SEC_I_COMPLETE_AND_CONTINUE)
    {
        return ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }
    if (HandshakeStatus != SEC_E_OK && HandshakeStatus != SEC_I_COMPLETE_NEEDED)
    {
        return ZpTls_FromSecurityStatus(HandshakeStatus);
    }
    SecurityStatus = QueryContextAttributesW(&Context->Handle,
                                              SECPKG_ATTR_STREAM_SIZES,
                                              &Context->StreamSizes);
    if (SecurityStatus != SEC_E_OK)
    {
        return ZpTls_FromSecurityStatus(SecurityStatus);
    }
    Context->HandshakeComplete = TRUE;
    *Complete = TRUE;
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

ZP_STATUS
ZpTls_Encrypt(
    _Inout_ PZP_TLS_CONTEXT Context,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _Outptr_result_bytebuffer_(*EncryptedLength) PBYTE* Encrypted,
    _Out_ PULONG EncryptedLength)
{
    const BYTE* Input = Data;
    PBYTE Buffer, Cursor;
    ULONG Chunk, RecordCount, Size = 0;
    SECURITY_STATUS SecurityStatus;
    SecBuffer Buffers[4];
    SecBufferDesc Descriptor = { SECBUFFER_VERSION, RTL_NUMBER_OF(Buffers), Buffers };

    if (!Context->HandshakeComplete || Data == NULL || DataLength == 0)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    RecordCount = (DataLength + Context->StreamSizes.cbMaximumMessage - 1) /
                  Context->StreamSizes.cbMaximumMessage;
    if (DataLength > MAXULONG - RecordCount *
                         (Context->StreamSizes.cbHeader + Context->StreamSizes.cbTrailer))
    {
        return ZpStatus_FromNtStatus(STATUS_INTEGER_OVERFLOW);
    }
    Buffer = Mem_Alloc(DataLength + RecordCount *
                           (Context->StreamSizes.cbHeader + Context->StreamSizes.cbTrailer));
    if (Buffer == NULL)
    {
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    Cursor = Buffer;
    while (DataLength != 0)
    {
        Chunk = min(DataLength, Context->StreamSizes.cbMaximumMessage);
        RtlZeroMemory(Buffers, sizeof(Buffers));
        Buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        Buffers[0].pvBuffer = Cursor;
        Buffers[0].cbBuffer = Context->StreamSizes.cbHeader;
        Buffers[1].BufferType = SECBUFFER_DATA;
        Buffers[1].pvBuffer = Cursor + Context->StreamSizes.cbHeader;
        Buffers[1].cbBuffer = Chunk;
        RtlCopyMemory(Buffers[1].pvBuffer, Input, Chunk);
        Buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        Buffers[2].pvBuffer = Add2Ptr(Buffers[1].pvBuffer, Chunk);
        Buffers[2].cbBuffer = Context->StreamSizes.cbTrailer;
        Buffers[3].BufferType = SECBUFFER_EMPTY;
        SecurityStatus = EncryptMessage(&Context->Handle, 0, &Descriptor, 0);
        if (SecurityStatus != SEC_E_OK)
        {
            Mem_Free(Buffer);
            return ZpTls_FromSecurityStatus(SecurityStatus);
        }
        Chunk = Buffers[0].cbBuffer + Buffers[1].cbBuffer + Buffers[2].cbBuffer;
        Cursor += Chunk;
        Size += Chunk;
        Input += Buffers[1].cbBuffer;
        DataLength -= Buffers[1].cbBuffer;
    }
    *Encrypted = Buffer;
    *EncryptedLength = Size;
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

ZP_STATUS
ZpTls_Decrypt(
    _Inout_ PZP_TLS_CONTEXT Context,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ZP_TLS_PLAINTEXT_CALLBACK Callback,
    _In_opt_ PVOID CallbackContext)
{
    SecBuffer Buffers[4];
    SecBufferDesc Descriptor = { SECBUFFER_VERSION, RTL_NUMBER_OF(Buffers), Buffers };
    SECURITY_STATUS SecurityStatus;
    ZP_STATUS Status;
    NTSTATUS CallbackStatus;
    ULONG Index;

    if (!Context->HandshakeComplete || Callback == NULL)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Status = ZpTls_AppendInput(Context, Data, DataLength);
    if (!ZpStatus_IsSuccess(Status))
    {
        return Status;
    }
    while (Context->InputLength != 0)
    {
        RtlZeroMemory(Buffers, sizeof(Buffers));
        Buffers[0].BufferType = SECBUFFER_DATA;
        Buffers[0].pvBuffer = Context->Input;
        Buffers[0].cbBuffer = Context->InputLength;
        Buffers[1].BufferType = SECBUFFER_EMPTY;
        Buffers[2].BufferType = SECBUFFER_EMPTY;
        Buffers[3].BufferType = SECBUFFER_EMPTY;
        SecurityStatus = DecryptMessage(&Context->Handle, &Descriptor, 0, NULL);
        if (SecurityStatus == SEC_E_INCOMPLETE_MESSAGE)
        {
            return ZpStatus_FromNtStatus(STATUS_SUCCESS);
        }
        if (SecurityStatus == SEC_I_CONTEXT_EXPIRED)
        {
            return ZpStatus_FromNtStatus(STATUS_CONNECTION_DISCONNECTED);
        }
        if (SecurityStatus != SEC_E_OK)
        {
            return ZpTls_FromSecurityStatus(SecurityStatus);
        }
        for (Index = 0; Index < RTL_NUMBER_OF(Buffers); Index++)
        {
            if (Buffers[Index].BufferType == SECBUFFER_DATA && Buffers[Index].cbBuffer != 0)
            {
                CallbackStatus = Callback(Buffers[Index].pvBuffer,
                                          Buffers[Index].cbBuffer,
                                          CallbackContext);
                if (!NT_SUCCESS(CallbackStatus))
                {
                    return ZpStatus_FromNtStatus(CallbackStatus);
                }
            }
        }
        for (Index = 0; Index < RTL_NUMBER_OF(Buffers); Index++)
        {
            if (Buffers[Index].BufferType == SECBUFFER_EXTRA)
            {
                break;
            }
        }
        ZpTls_ConsumeInput(Context,
                           Index < RTL_NUMBER_OF(Buffers) ? &Buffers[Index] : NULL);
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

ZP_STATUS
ZpTls_GetRemoteCertificate(
    _In_ PZP_TLS_CONTEXT Context,
    _Outptr_ PCCERT_CONTEXT* Certificate)
{
    SECURITY_STATUS Status;

    Status = QueryContextAttributesW(&Context->Handle,
                                      SECPKG_ATTR_REMOTE_CERT_CONTEXT,
                                      Certificate);
    return ZpTls_FromSecurityStatus(Status);
}

VOID
ZpTls_Uninitialize(
    _Inout_ PZP_TLS_CONTEXT Context)
{
    if (Context->HandleInitialized)
    {
        DeleteSecurityContext(&Context->Handle);
    }
    Mem_Free(Context->Input);
    RtlZeroMemory(Context, sizeof(*Context));
}
