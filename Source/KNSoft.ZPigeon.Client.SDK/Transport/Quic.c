#include "../Client.inl"
#include "../Core/Channel.h"
#include "../../Modules/File/Client.h"
#include "../../Network/Authentication.inl"
#include "../../Network/Quic.inl"

#include <Bcrypt.h>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Ncrypt.lib")

#define ZP_CLIENT_QUIC_KEEP_ALIVE_INTERVAL_MILLISECONDS 20000

static const QUIC_REGISTRATION_CONFIG ZpClientQuicRegistrationConfig = {
    "KNSoft.ZPigeon.Client",
    QUIC_EXECUTION_PROFILE_LOW_LATENCY
};

static
VOID
ZpClientQuic_UninitializeAttempt(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport);

static
NTSTATUS
NTAPI
ZpClientQuic_Send(
    _In_opt_ PVOID Context,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength);

static
VOID
ZpClientQuic_SetShutdownStatus(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport,
    _In_ ZP_STATUS Status)
{
    RtlAcquireSRWLockExclusive(&Transport->Owner->Lock);
    if (Transport->ShutdownStatus.Type == ZpStatusNone)
    {
        Transport->ShutdownStatus = Status;
    }
    RtlReleaseSRWLockExclusive(&Transport->Owner->Lock);
}

static
ZP_STATUS
ZpClientQuic_GetShutdownStatus(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport)
{
    ZP_STATUS Status;

    RtlAcquireSRWLockShared(&Transport->Owner->Lock);
    Status = Transport->ShutdownStatus;
    RtlReleaseSRWLockShared(&Transport->Owner->Lock);
    return Status;
}

static
ZP_STATUS
ZpClientQuic_CreateIdentity(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport)
{
    SECURITY_STATUS SecurityStatus;
    PZP_CLIENT_OBJECT Object = Transport->Owner;
    PCWSTR KeyName = Object->Config.ClientKeyName != NULL ?
                         Object->Config.ClientKeyName :
                         ZP_CLIENT_DEFAULT_KEY_NAME;
    BYTE BlobBuffer[sizeof(BCRYPT_ECCKEY_BLOB) + 64];
    BCRYPT_ECCKEY_BLOB* Blob = (BCRYPT_ECCKEY_BLOB*)BlobBuffer;
    ULONG KeyFlags = Object->Config.ClientKeyScope == ZpClientKeyMachine ?
                         NCRYPT_MACHINE_KEY_FLAG : 0;
    ULONG BlobSize;

    if (Transport->ExternalKey != 0)
    {
        Transport->Key = Transport->ExternalKey;
        Transport->KeyOwned = FALSE;
        goto ExportKey;
    }
    SecurityStatus = NCryptOpenStorageProvider(&Transport->KeyProvider,
                                               MS_KEY_STORAGE_PROVIDER,
                                               0);
    if (SecurityStatus != ERROR_SUCCESS)
    {
        return ZpStatus_FromCode(ZpStatusSecurity, (ULONG)SecurityStatus);
    }
    SecurityStatus = NCryptOpenKey(Transport->KeyProvider,
                                   &Transport->Key,
                                   KeyName,
                                   0,
                                   KeyFlags | NCRYPT_SILENT_FLAG);
    if (SecurityStatus == NTE_BAD_KEYSET || SecurityStatus == NTE_NOT_FOUND)
    {
        SecurityStatus = NCryptCreatePersistedKey(Transport->KeyProvider,
                                                  &Transport->Key,
                                                   NCRYPT_ECDSA_P256_ALGORITHM,
                                                   KeyName,
                                                   0,
                                                   KeyFlags);
        if (SecurityStatus == ERROR_SUCCESS)
        {
            SecurityStatus = NCryptFinalizeKey(Transport->Key, NCRYPT_SILENT_FLAG);
        }
    }
    if (SecurityStatus != ERROR_SUCCESS)
    {
        return ZpStatus_FromCode(ZpStatusSecurity, (ULONG)SecurityStatus);
    }
    Transport->KeyOwned = TRUE;

ExportKey:
    SecurityStatus = NCryptExportKey(Transport->Key,
                                     0,
                                     BCRYPT_ECCPUBLIC_BLOB,
                                     NULL,
                                     BlobBuffer,
                                     sizeof(BlobBuffer),
                                     &BlobSize,
                                     0);
    if (SecurityStatus != ERROR_SUCCESS ||
        BlobSize != sizeof(BlobBuffer) ||
        Blob->dwMagic != BCRYPT_ECDSA_PUBLIC_P256_MAGIC ||
        Blob->cbKey != 32)
    {
        return SecurityStatus == ERROR_SUCCESS ?
                   ZpStatus_FromNtStatus(STATUS_DATA_ERROR) :
                   ZpStatus_FromCode(ZpStatusSecurity, (ULONG)SecurityStatus);
    }
    Transport->PublicKey[0] = 0x04;
    RtlCopyMemory(Transport->PublicKey + 1,
                  BlobBuffer + sizeof(*Blob),
                  sizeof(Transport->PublicKey) - 1);
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ZP_STATUS
ZpClientQuic_SignChallenge(
    _In_ PZP_CLIENT_QUIC_TRANSPORT Transport,
    _In_reads_bytes_(ZP_SERVER_CHALLENGE_SIZE) const BYTE* Challenge,
    _Out_writes_bytes_(ZP_CLIENT_SIGNATURE_SIZE) BYTE* Signature)
{
    SECURITY_STATUS SecurityStatus;
    BYTE Hash[32];
    ULONG SignatureSize;
    NTSTATUS Status;

    Status = ZpAuthentication_Hash(Challenge,
                                   Transport->PublicKey,
                                   Hash);
    if (!NT_SUCCESS(Status))
    {
        return ZpStatus_FromNtStatus(Status);
    }
    SecurityStatus = NCryptSignHash(Transport->Key,
                                    NULL,
                                    Hash,
                                    sizeof(Hash),
                                    Signature,
                                    ZP_CLIENT_SIGNATURE_SIZE,
                                    &SignatureSize,
                                    0);
    RtlSecureZeroMemory(Hash, sizeof(Hash));
    if (SecurityStatus != ERROR_SUCCESS || SignatureSize != ZP_CLIENT_SIGNATURE_SIZE)
    {
        return SecurityStatus == ERROR_SUCCESS ?
                   ZpStatus_FromNtStatus(STATUS_DATA_ERROR) :
                   ZpStatus_FromCode(ZpStatusSecurity, (ULONG)SecurityStatus);
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
NTSTATUS
ZpClientQuic_SendHello(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport)
{
    PZP_CLIENT_OBJECT Object = Transport->Owner;
    ZP_CLIENT_HELLO Message = {
        ZP_CORE_VERSION,
        Object->Config.Modules,
        Object->Config.ModuleCount,
        Transport->PublicKey
    };
    BYTE Body[4 + ZP_MODULE_MAX_COUNT * sizeof(ZP_MODULE_RECORD) +
              ZP_CLIENT_PUBLIC_KEY_SIZE];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeClientHello(&Message,
                                         Body,
                                         sizeof(Body),
                                         &BodyLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    return ZpClientQuic_Send(Transport,
                            ZpMessageClientHello,
                            Body,
                            BodyLength);
}

static
NTSTATUS
ZpClientQuic_ValidateReady(
    _In_ PZP_CLIENT_QUIC_TRANSPORT Transport,
    _In_ const ZP_READY_VIEW* Ready)
{
    PZP_CLIENT_OBJECT Object = Transport->Owner;
    ZP_MODULE_RECORD Selected;
    ULONG OfferedIndex = 0;
    USHORT Index;
    NTSTATUS Status;

    for (Index = 0; Index < Ready->Modules.Count; Index++)
    {
        Status = ZpMessage_GetModuleRecord(&Ready->Modules, Index, &Selected);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        while (OfferedIndex < Object->Config.ModuleCount &&
               Object->Config.Modules[OfferedIndex].ModuleId < Selected.ModuleId)
        {
            OfferedIndex++;
        }
        if (OfferedIndex == Object->Config.ModuleCount ||
            Object->Config.Modules[OfferedIndex].ModuleId != Selected.ModuleId ||
            Selected.ModuleVersion != Object->Config.Modules[OfferedIndex].ModuleVersion)
        {
            return STATUS_PROTOCOL_UNREACHABLE;
        }
    }
    Transport->ModuleCount = Ready->Modules.Count;
    for (Index = 0; Index < Ready->Modules.Count; Index++)
    {
        Status = ZpMessage_GetModuleRecord(&Ready->Modules,
                                           Index,
                                           &Transport->Modules[Index]);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
ZpClientQuic_MessageCallback(
    _Inout_ PZP_CONNECTION Connection,
    _In_ const ZP_FRAME_VIEW* Frame,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;
    ZP_BUFFER_VIEW Data;
    ZP_READY_VIEW Ready;
    ZP_REQUEST_VIEW Request;
    ZP_CHANNEL_DATA_VIEW ChannelData;
    ZP_CHANNEL_CLOSE ChannelClose;
    BYTE Signature[ZP_CLIENT_SIGNATURE_SIZE];
    BYTE Body[ZP_CLIENT_SIGNATURE_SIZE];
    ULONGLONG Token;
    ULONGLONG RequestId;
    ULONG BodyLength;
    NTSTATUS Status;
    ZP_STATUS SignStatus;

    switch (Frame->MessageType)
    {
        case ZpMessageServerChallenge:
            Status = ZpMessage_DecodeServerChallenge(Frame->Body,
                                                      Frame->BodyLength,
                                                      &Data);
            if (NT_SUCCESS(Status))
            {
                SignStatus = ZpClientQuic_SignChallenge(Transport,
                                                        Data.Buffer,
                                                        Signature);
                if (!ZpStatus_IsSuccess(SignStatus))
                {
                    ZpClientQuic_SetShutdownStatus(Transport, SignStatus);
                    Status = STATUS_UNSUCCESSFUL;
                }
            }
            if (NT_SUCCESS(Status))
            {
                Status = ZpMessage_EncodeClientAuthenticate(Signature,
                                                            Body,
                                                            sizeof(Body),
                                                            &BodyLength);
            }
            RtlSecureZeroMemory(Signature, sizeof(Signature));
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            return ZpClientQuic_Send(Transport,
                                    ZpMessageClientAuthenticate,
                                    Body,
                                    BodyLength);

        case ZpMessageReady:
            Status = ZpMessage_DecodeReady(Frame->Body,
                                           Frame->BodyLength,
                                           &Ready);
            if (NT_SUCCESS(Status))
            {
                Status = ZpClientQuic_ValidateReady(Transport, &Ready);
            }
            if (NT_SUCCESS(Status))
            {
                Status = ZpClient_NotifyState((ZP_CLIENT_HANDLE)Transport->Owner,
                                              ZpClientStateReady,
                                              ZpStatus_FromNtStatus(
                                                  STATUS_SUCCESS));
            }
            return Status;

        case ZpMessagePong:
            Status = ZpMessage_DecodePing(ZpMessagePong,
                                          Frame->Body,
                                          Frame->BodyLength,
                                          &Token);
            if (NT_SUCCESS(Status))
            {
                Status = ZpClient_NotifyPong((ZP_CLIENT_HANDLE)Transport->Owner,
                                            Token);
            }
            return Status;

        case ZpMessageRequest:
            Status = ZpMessage_DecodeRequest(Frame->Body,
                                             Frame->BodyLength,
                                             &Request);
            return NT_SUCCESS(Status) ?
                       ZpClient_QueueRequest((ZP_CLIENT_HANDLE)Transport->Owner,
                                             &Request) :
                       Status;

        case ZpMessageCancel:
            Status = ZpMessage_DecodeCancel(Frame->Body,
                                            Frame->BodyLength,
                                            &RequestId);
            return NT_SUCCESS(Status) ?
                       ZpClient_CancelInboundRequest(
                           (ZP_CLIENT_HANDLE)Transport->Owner,
                           RequestId) :
                       Status;

        case ZpMessageChannelData:
            Status = ZpMessage_DecodeChannelData(Frame->Body,
                                                  Frame->BodyLength,
                                                  &ChannelData);
            if (NT_SUCCESS(Status))
            {
                Status = ZpClientLocalChannel_ReceiveData(
                    Transport->Owner,
                    &ChannelData);
            }
            return Status;

        case ZpMessageChannelClose:
            Status = ZpMessage_DecodeChannelClose(Frame->Body,
                                                   Frame->BodyLength,
                                                   &ChannelClose);
            if (NT_SUCCESS(Status))
            {
                Status = ZpClientLocalChannel_ReceiveClose(
                    Transport->Owner,
                    &ChannelClose);
            }
            return Status;

        case ZpMessageChannelWindow:
        {
            ULONGLONG ChannelId;
            ULONG CreditBytes;

            Status = ZpMessage_DecodeChannelWindow(Frame->Body,
                                                    Frame->BodyLength,
                                                    &ChannelId,
                                                    &CreditBytes);
            if (NT_SUCCESS(Status))
            {
                Status = ZpClientLocalChannel_ReceiveWindow(
                    Transport->Owner,
                    ChannelId,
                    CreditBytes);
            }
            return Status;
        }
    }
    return STATUS_PROTOCOL_UNREACHABLE;
}

static
NTSTATUS
NTAPI
ZpClientQuic_Send(
    _In_opt_ PVOID Context,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;
    QUIC_STATUS QuicStatus = QUIC_STATUS_SUCCESS;
    NTSTATUS Status;

    if (Transport->Stream == NULL)
    {
        return STATUS_CONNECTION_DISCONNECTED;
    }
    Status = ZpQuic_SendFrame(Transport->Stream,
                              &Transport->ProtocolConnection,
                              MessageType,
                              Body,
                              BodyLength,
                              &QuicStatus);
    if (!NT_SUCCESS(Status))
    {
        ZpClientQuic_SetShutdownStatus(
            Transport,
            QUIC_FAILED(QuicStatus) ?
                ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus) :
                ZpStatus_FromNtStatus(Status));
        MsQuicConnectionShutdown(Transport->Connection,
                                 QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                 0);
    }
    return Status;
}

static
ZP_STATUS
ZpClientQuic_ValidateCertificate(
    _In_ PZP_CLIENT_QUIC_TRANSPORT Transport,
    _In_ PCCERT_CONTEXT Certificate)
{
    CERT_CHAIN_PARA ChainParameters = { sizeof(ChainParameters) };
    CERT_CHAIN_POLICY_PARA PolicyParameters = { sizeof(PolicyParameters) };
    CERT_CHAIN_POLICY_STATUS PolicyStatus = { sizeof(PolicyStatus) };
    SSL_EXTRA_CERT_CHAIN_POLICY_PARA SslParameters = { sizeof(SslParameters) };
    PCCERT_CHAIN_CONTEXT Chain;
    ZP_STATUS Status;

    if (!CertGetCertificateChain(Transport->ChainEngine,
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
    SslParameters.pwszServerName = (PWSTR)Transport->Owner->Config.Endpoints[
        Transport->EndpointIndex].ServerName;
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

static
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS
QUIC_API
ZpClientQuic_StreamCallback(
    _In_ HQUIC Stream,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_STREAM_EVENT* Event)
{
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;
    PZP_CLIENT_OBJECT Object;
    NTSTATUS Status;
    ULONG Index;

    if (Transport == NULL)
    {
        return QUIC_STATUS_INVALID_PARAMETER;
    }
    Object = Transport->Owner;
    switch (Event->Type)
    {
        case QUIC_STREAM_EVENT_START_COMPLETE:
            if (QUIC_SUCCEEDED(Event->START_COMPLETE.Status))
            {
                Status = ZpClient_NotifyState((ZP_CLIENT_HANDLE)Object,
                                              ZpClientStateAuthenticating,
                                              ZpStatus_FromNtStatus(
                                                  STATUS_SUCCESS));
                if (NT_SUCCESS(Status))
                {
                    Status = ZpClientQuic_SendHello(Transport);
                }
                if (!NT_SUCCESS(Status))
                {
                    ZpClientQuic_SetShutdownStatus(
                        Transport,
                        ZpStatus_FromNtStatus(Status));
                    MsQuicConnectionShutdown(Transport->Connection,
                                             QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                             0);
                }
            }
            else
            {
                ZpClientQuic_SetShutdownStatus(
                    Transport,
                    ZpStatus_FromCode(ZpStatusQuic,
                                      (ULONG)Event->START_COMPLETE.Status));
                MsQuicConnectionShutdown(Transport->Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
            }
            break;

        case QUIC_STREAM_EVENT_SEND_COMPLETE:
            ZpQuic_CompleteSend(Event->SEND_COMPLETE.ClientContext);
            break;

        case QUIC_STREAM_EVENT_RECEIVE:
            Status = STATUS_SUCCESS;
            for (Index = 0;
                 NT_SUCCESS(Status) && Index < Event->RECEIVE.BufferCount;
                 Index++)
            {
                Status = ZpConnection_Receive(&Transport->ProtocolConnection,
                                              Event->RECEIVE.Buffers[Index].Buffer,
                                              Event->RECEIVE.Buffers[Index].Length);
            }
            if (!NT_SUCCESS(Status))
            {
                ZpClientQuic_SetShutdownStatus(
                    Transport,
                    ZpStatus_FromNtStatus(Status));
                MsQuicConnectionShutdown(Transport->Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
            }
            break;

        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
            ZpClientQuic_SetShutdownStatus(
                Transport,
                ZpStatus_FromNtStatus(STATUS_CONNECTION_DISCONNECTED));
            MsQuicConnectionShutdown(Transport->Connection,
                                     QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                     0);
            break;

        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            RtlAcquireSRWLockExclusive(&Object->Lock);
            if (Transport->Stream == Stream)
            {
                Transport->Stream = NULL;
            }
            RtlReleaseSRWLockExclusive(&Object->Lock);
            if (!Event->SHUTDOWN_COMPLETE.AppCloseInProgress)
            {
                MsQuicStreamClose(Stream);
            }
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

static
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS
QUIC_API
ZpClientQuic_ConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ PVOID Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event)
{
    QUIC_STATUS QuicStatus;
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;
    PZP_CLIENT_OBJECT Object;
    HQUIC Stream;
    NTSTATUS Status;
    ZP_STATUS ShutdownStatus;
    ZP_STATUS CertificateStatus;
    BOOLEAN Valid;

    if (Transport == NULL)
    {
        return QUIC_STATUS_INVALID_PARAMETER;
    }
    Object = Transport->Owner;
    switch (Event->Type)
    {
        case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:
            CertificateStatus = ZpClientQuic_ValidateCertificate(
                Transport,
                (PCCERT_CONTEXT)Event->PEER_CERTIFICATE_RECEIVED.Certificate);
            if (!ZpStatus_IsSuccess(CertificateStatus))
            {
                ZpClientQuic_SetShutdownStatus(Transport,
                                               CertificateStatus);
            }
            Valid = ZpStatus_IsSuccess(CertificateStatus) ? TRUE : FALSE;
            MsQuicConnectionCertificateValidationComplete(
                Connection,
                Valid,
                Valid ?
                    QUIC_TLS_ALERT_CODE_SUCCESS :
                    QUIC_TLS_ALERT_CODE_BAD_CERTIFICATE);
            return QUIC_STATUS_PENDING;

        case QUIC_CONNECTION_EVENT_CONNECTED:
            Status = ZpConnection_Initialize(&Transport->ProtocolConnection,
                                             ZpConnectionRoleClient,
                                             ZpClientQuic_MessageCallback,
                                             Transport);
            if (!NT_SUCCESS(Status))
            {
                ZpClientQuic_SetShutdownStatus(
                    Transport,
                    ZpStatus_FromNtStatus(Status));
                MsQuicConnectionShutdown(Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
                break;
            }
            Transport->ProtocolConnectionInitialized = TRUE;
            QuicStatus = MsQuicStreamOpen(Connection,
                                          QUIC_STREAM_OPEN_FLAG_NONE,
                                          ZpClientQuic_StreamCallback,
                                          Transport,
                                          &Stream);
            if (QUIC_SUCCEEDED(QuicStatus))
            {
                RtlAcquireSRWLockExclusive(&Object->Lock);
                Transport->Stream = Stream;
                RtlReleaseSRWLockExclusive(&Object->Lock);
                QuicStatus = MsQuicStreamStart(Stream,
                                               QUIC_STREAM_START_FLAG_IMMEDIATE |
                                                   QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL);
            }
            if (QUIC_FAILED(QuicStatus))
            {
                ZpClientQuic_SetShutdownStatus(
                    Transport,
                    ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus));
                MsQuicConnectionShutdown(Connection,
                                         QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                         0);
            }
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            ZpClientQuic_SetShutdownStatus(
                Transport,
                ZpStatus_FromCode(
                    ZpStatusQuic,
                    (ULONG)Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status));
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
            ZpClientQuic_SetShutdownStatus(
                Transport,
                ZpStatus_FromNtStatus(STATUS_CONNECTION_DISCONNECTED));
            break;

        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            RtlAcquireSRWLockExclusive(&Object->Lock);
            if (Transport->Connection == Connection)
            {
                Transport->Connection = NULL;
            }
            RtlReleaseSRWLockExclusive(&Object->Lock);
            ShutdownStatus = ZpClientQuic_GetShutdownStatus(Transport);
            MsQuicConnectionClose(Connection);
            ZpClient_TransportShutdown((ZP_CLIENT_HANDLE)Object,
                                       ShutdownStatus);
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

static
ZP_STATUS
ZpClientQuic_CreateRootChainEngine(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport)
{
    CERT_CHAIN_ENGINE_CONFIG EngineConfig = { sizeof(EngineConfig) };
    PZP_CLIENT_OBJECT Object = Transport->Owner;
    PCCERT_CONTEXT RootCertificate;
    ULONG Error;

    RootCertificate = CertCreateCertificateContext(
        X509_ASN_ENCODING,
        Object->Config.DeploymentRootCertificate,
        Object->Config.DeploymentRootCertificateLength);
    if (RootCertificate == NULL)
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    Transport->RootStore = CertOpenStore(CERT_STORE_PROV_MEMORY,
                                         X509_ASN_ENCODING,
                                         0,
                                         CERT_STORE_CREATE_NEW_FLAG,
                                         NULL);
    if (Transport->RootStore == NULL)
    {
        Error = GetLastError();
        CertFreeCertificateContext(RootCertificate);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    if (!CertAddCertificateContextToStore(Transport->RootStore,
                                          RootCertificate,
                                          CERT_STORE_ADD_ALWAYS,
                                          NULL))
    {
        Error = GetLastError();
        CertFreeCertificateContext(RootCertificate);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    CertFreeCertificateContext(RootCertificate);
    EngineConfig.hExclusiveRoot = Transport->RootStore;
    if (!CertCreateCertificateChainEngine(&EngineConfig, &Transport->ChainEngine))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ZP_STATUS
ZpClientQuic_StartEndpoint(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport)
{
    QUIC_STATUS QuicStatus;
    PZP_CLIENT_OBJECT Object = Transport->Owner;
    const ZP_ENDPOINT* Endpoint = &Object->Config.Endpoints[Transport->EndpointIndex];
    QUIC_SETTINGS Settings = { 0 };
    QUIC_CREDENTIAL_CONFIG Credentials = { 0 };
    QUIC_ADDR Address;
    ZP_STATUS Status;
    PSTR ServerName;
    ULONG ServerNameSize;

    ZpClientQuic_UninitializeAttempt(Transport);
    Transport->Owner = Object;
    Transport->ShutdownStatus = ZpStatus_FromNtStatus(STATUS_SUCCESS);

    QuicStatus = KNSoftQuicInitialize();
    if (QUIC_FAILED(QuicStatus))
    {
        return ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
    }
    Transport->Initialized = TRUE;
    QuicStatus = MsQuicRegistrationOpen(&ZpClientQuicRegistrationConfig,
                                        &Transport->Registration);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
        goto Cleanup;
    }
    Status = ZpClientQuic_CreateRootChainEngine(Transport);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Cleanup;
    }
    Status = ZpClientQuic_CreateIdentity(Transport);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Cleanup;
    }

    Settings.HandshakeIdleTimeoutMs = Object->Config.ConnectTimeoutMilliseconds;
    Settings.IsSet.HandshakeIdleTimeoutMs = TRUE;
    Settings.KeepAliveIntervalMs =
        ZP_CLIENT_QUIC_KEEP_ALIVE_INTERVAL_MILLISECONDS;
    Settings.IsSet.KeepAliveIntervalMs = TRUE;
    Credentials.Type = QUIC_CREDENTIAL_TYPE_NONE;
    Credentials.Flags = QUIC_CREDENTIAL_FLAG_CLIENT |
                        QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED |
                        QUIC_CREDENTIAL_FLAG_DEFER_CERTIFICATE_VALIDATION;
    QuicStatus = MsQuicConfigurationOpen(Transport->Registration,
                                         &ZpQuicAlpn,
                                         1,
                                         &Settings,
                                         sizeof(Settings),
                                         NULL,
                                         &Transport->Configuration);
    if (QUIC_FAILED(QuicStatus) ||
        QUIC_FAILED(QuicStatus = MsQuicConfigurationLoadCredential(
                        Transport->Configuration,
                        &Credentials)))
    {
        Status = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
        goto Cleanup;
    }
    QuicStatus = MsQuicConnectionOpen(Transport->Registration,
                                      ZpClientQuic_ConnectionCallback,
                                      Transport,
                                      &Transport->Connection);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
        goto Cleanup;
    }
    Status = ZpQuic_ResolveAddress(Endpoint->Host, Endpoint->Port, &Address);
    if (!ZpStatus_IsSuccess(Status))
    {
        goto Cleanup;
    }
    QuicStatus = MsQuicSetParam(Transport->Connection,
                                QUIC_PARAM_CONN_REMOTE_ADDRESS,
                                sizeof(Address),
                                &Address);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
        goto Cleanup;
    }
    ServerNameSize = Str_UnicodeToUtf8(NULL, 0, Endpoint->ServerName);
    ServerName = Mem_Alloc(ServerNameSize);
    if (ServerName == NULL ||
        Str_UnicodeToUtf8(ServerName, ServerNameSize, Endpoint->ServerName) == 0)
    {
        Mem_Free(ServerName);
        Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        goto Cleanup;
    }
    QuicStatus = MsQuicConnectionStart(Transport->Connection,
                                       Transport->Configuration,
                                       Address.si_family,
                                       ServerName,
                                       Endpoint->Port);
    Mem_Free(ServerName);
    if (QUIC_FAILED(QuicStatus))
    {
        Status = ZpStatus_FromCode(ZpStatusQuic, (ULONG)QuicStatus);
        goto Cleanup;
    }
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);

Cleanup:
    ZpClientQuic_UninitializeAttempt(Transport);
    Transport->Owner = Object;
    return Status;
}

static
ZP_STATUS
NTAPI
ZpClientQuic_Start(
    _In_opt_ PVOID Context,
    _In_ ULONG EndpointIndex)
{
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;

    Transport->EndpointIndex = EndpointIndex;
    Transport->ModuleCount = 0;
    return ZpClientQuic_StartEndpoint(Transport);
}

static
VOID
NTAPI
ZpClientQuic_Stop(
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_QUIC_TRANSPORT Transport = Context;
    PZP_CLIENT_OBJECT Object = Transport->Owner;
    HQUIC Connection;
    LOGICAL StartPending;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Connection = Transport->Connection;
    StartPending = Object->StartPending;
    if (Connection != NULL)
    {
        Transport->ShutdownStatus = ZpStatus_FromNtStatus(STATUS_SUCCESS);
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Connection != NULL)
    {
        MsQuicConnectionShutdown(Connection,
                                 QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                 0);
        return;
    }
    if (!StartPending)
    {
        ZpClient_NotifyState((ZP_CLIENT_HANDLE)Object,
                             ZpClientStateStopped,
                             ZpStatus_FromNtStatus(STATUS_SUCCESS));
    }
}

static const ZP_TRANSPORT_OPERATIONS ZpClientQuicOperations = {
    ZpClientQuic_Start,
    ZpClientQuic_Stop,
    ZpClientQuic_Send
};

VOID
ZpClientQuic_Configure(
    _Inout_ PZP_CLIENT_OBJECT Object)
{
    Object->QuicTransport.Owner = Object;
    ZpClient_SetTransport((ZP_CLIENT_HANDLE)Object,
                          ZpTransportQuic,
                          &ZpClientQuicOperations,
                          &Object->QuicTransport);
}

static
VOID
ZpClientQuic_UninitializeAttempt(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport)
{
    if (Transport->Stream != NULL)
    {
        MsQuicStreamClose(Transport->Stream);
        Transport->Stream = NULL;
    }
    if (Transport->Connection != NULL)
    {
        MsQuicConnectionClose(Transport->Connection);
        Transport->Connection = NULL;
    }
    if (Transport->Configuration != NULL)
    {
        MsQuicConfigurationClose(Transport->Configuration);
        Transport->Configuration = NULL;
    }
    if (Transport->Registration != NULL)
    {
        MsQuicRegistrationClose(Transport->Registration);
        Transport->Registration = NULL;
    }
    if (Transport->ChainEngine != NULL)
    {
        CertFreeCertificateChainEngine(Transport->ChainEngine);
        Transport->ChainEngine = NULL;
    }
    if (Transport->Key != 0)
    {
        if (Transport->KeyOwned)
        {
            NCryptFreeObject(Transport->Key);
        }
        Transport->Key = 0;
        Transport->KeyOwned = FALSE;
    }
    if (Transport->KeyProvider != 0)
    {
        NCryptFreeObject(Transport->KeyProvider);
        Transport->KeyProvider = 0;
    }
    RtlSecureZeroMemory(Transport->PublicKey, sizeof(Transport->PublicKey));
    if (Transport->ProtocolConnectionInitialized)
    {
        ZpConnection_Uninitialize(&Transport->ProtocolConnection);
        Transport->ProtocolConnectionInitialized = FALSE;
    }
    if (Transport->RootStore != NULL)
    {
        CertCloseStore(Transport->RootStore, 0);
        Transport->RootStore = NULL;
    }
    if (Transport->Initialized)
    {
        KNSoftQuicUninitialize();
        Transport->Initialized = FALSE;
    }
}

VOID
ZpClientQuic_Uninitialize(
    _Inout_ PZP_CLIENT_QUIC_TRANSPORT Transport)
{
    ZpClientQuic_UninitializeAttempt(Transport);
}
