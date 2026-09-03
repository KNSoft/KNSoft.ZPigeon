#include "Session.h"

#include "../Client.inl"
#include "Channel.h"
#include "../../Network/Authentication.inl"

#include <Bcrypt.h>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Ncrypt.lib")

static
ZP_STATUS
ZpClientSession_CreateIdentity(
    _Inout_ PZP_CLIENT_SESSION Session,
    _In_opt_ NCRYPT_KEY_HANDLE ExternalKey)
{
    SECURITY_STATUS SecurityStatus;
    PCWSTR KeyName = Session->Owner->Config.ClientKeyName != NULL ?
                         Session->Owner->Config.ClientKeyName :
                         ZP_CLIENT_DEFAULT_KEY_NAME;
    BYTE BlobBuffer[sizeof(BCRYPT_ECCKEY_BLOB) + 64];
    BCRYPT_ECCKEY_BLOB* Blob = (BCRYPT_ECCKEY_BLOB*)BlobBuffer;
    ULONG KeyFlags = Session->Owner->Config.ClientKeyScope == ZpClientKeyMachine ?
                         NCRYPT_MACHINE_KEY_FLAG : 0;
    ULONG BlobSize;

    if (ExternalKey != 0)
    {
        Session->Key = ExternalKey;
        goto ExportKey;
    }
    SecurityStatus = NCryptOpenStorageProvider(&Session->KeyProvider,
                                               MS_KEY_STORAGE_PROVIDER,
                                               0);
    if (SecurityStatus != ERROR_SUCCESS)
    {
        return ZpStatus_FromCode(ZpStatusSecurity, (ULONG)SecurityStatus);
    }
    SecurityStatus = NCryptOpenKey(Session->KeyProvider,
                                   &Session->Key,
                                   KeyName,
                                   0,
                                   KeyFlags | NCRYPT_SILENT_FLAG);
    if (SecurityStatus == NTE_BAD_KEYSET || SecurityStatus == NTE_NOT_FOUND)
    {
        SecurityStatus = NCryptCreatePersistedKey(Session->KeyProvider,
                                                  &Session->Key,
                                                  NCRYPT_ECDSA_P256_ALGORITHM,
                                                  KeyName,
                                                  0,
                                                  KeyFlags);
        if (SecurityStatus == ERROR_SUCCESS)
        {
            SecurityStatus = NCryptFinalizeKey(Session->Key, NCRYPT_SILENT_FLAG);
        }
    }
    if (SecurityStatus != ERROR_SUCCESS)
    {
        return ZpStatus_FromCode(ZpStatusSecurity, (ULONG)SecurityStatus);
    }
    Session->KeyOwned = TRUE;

ExportKey:
    SecurityStatus = NCryptExportKey(Session->Key,
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
    Session->PublicKey[0] = 0x04;
    RtlCopyMemory(Session->PublicKey + 1,
                  BlobBuffer + sizeof(*Blob),
                  sizeof(Session->PublicKey) - 1);
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

static
ZP_STATUS
ZpClientSession_SignChallenge(
    _In_ PZP_CLIENT_SESSION Session,
    _In_reads_bytes_(ZP_SERVER_CHALLENGE_SIZE) const BYTE* Challenge,
    _Out_writes_bytes_(ZP_CLIENT_SIGNATURE_SIZE) BYTE* Signature)
{
    SECURITY_STATUS SecurityStatus;
    BYTE Hash[32];
    ULONG SignatureSize;
    NTSTATUS Status;

    Status = ZpAuthentication_Hash(Challenge, Session->PublicKey, Hash);
    if (!NT_SUCCESS(Status))
    {
        return ZpStatus_FromNtStatus(Status);
    }
    SecurityStatus = NCryptSignHash(Session->Key,
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
NTAPI
ZpClientSession_MessageCallback(
    _Inout_ PZP_CONNECTION Connection,
    _In_ const ZP_FRAME_VIEW* Frame,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_SESSION Session = Context;
    ZP_BUFFER_VIEW Data;
    ZP_CONNECTION_POLICY Policy;
    ZP_REQUEST_VIEW Request;
    ZP_CHANNEL_DATA_VIEW ChannelData;
    ZP_CHANNEL_CLOSE ChannelClose;
    BYTE Signature[ZP_CLIENT_SIGNATURE_SIZE];
    BYTE Body[ZP_CLIENT_SIGNATURE_SIZE];
    ULONG BodyLength, ChannelId, CreditBytes, RequestId;
    NTSTATUS Status;
    ZP_STATUS SignStatus;

    switch (Frame->MessageType)
    {
        case ZpMessageServerChallenge:
            Status = ZpMessage_DecodeServerChallenge(Frame->Body, Frame->BodyLength, &Data);
            if (NT_SUCCESS(Status))
            {
                SignStatus = ZpClientSession_SignChallenge(Session, Data.Buffer, Signature);
                if (!ZpStatus_IsSuccess(SignStatus))
                {
                    Session->Failure(Session->Context, SignStatus);
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
            return NT_SUCCESS(Status) ?
                       Session->Send(Session->Context,
                                     0,
                                     ZpMessageClientAuthenticate,
                                     Body,
                                     BodyLength,
                                     NULL,
                                     0) : Status;

        case ZpMessageReady:
            return ZpClient_NotifyState((ZP_CLIENT_HANDLE)Session->Owner,
                                        ZpClientStateReady,
                                        ZpStatus_FromNtStatus(STATUS_SUCCESS));

        case ZpMessageServerReject:
            Status = STATUS_REVISION_MISMATCH;
            Session->Failure(Session->Context, ZpStatus_FromNtStatus(Status));
            return Status;

        case ZpMessageConnectionPolicy:
            Status = ZpMessage_DecodeConnectionPolicy(Frame->Body,
                                                       Frame->BodyLength,
                                                       &Policy);
            if (NT_SUCCESS(Status)) Status = ZpConnection_SetPolicy(Connection, &Policy);
            if (NT_SUCCESS(Status))
            {
                RtlAcquireSRWLockExclusive(&Session->Owner->Lock);
                Session->Owner->ConnectionPolicy = Policy;
                RtlReleaseSRWLockExclusive(&Session->Owner->Lock);
            }
            return Status;

        case ZpMessageRequest:
            Status = ZpMessage_DecodeRequest(Frame->Body, Frame->BodyLength, &Request);
            return NT_SUCCESS(Status) ?
                       ZpClient_QueueRequest((ZP_CLIENT_HANDLE)Session->Owner, &Request) : Status;

        case ZpMessageCancel:
            Status = ZpMessage_DecodeCancel(Frame->Body, Frame->BodyLength, &RequestId);
            return NT_SUCCESS(Status) ?
                       ZpClient_CancelInboundRequest((ZP_CLIENT_HANDLE)Session->Owner, RequestId) : Status;

        case ZpMessageChannelData:
            Status = ZpMessage_DecodeChannelData(Frame->Body, Frame->BodyLength, &ChannelData);
            return NT_SUCCESS(Status) ?
                       ZpClientLocalChannel_ReceiveData(Session->Owner, &ChannelData) : Status;

        case ZpMessageChannelClose:
            Status = ZpMessage_DecodeChannelClose(Frame->Body, Frame->BodyLength, &ChannelClose);
            return NT_SUCCESS(Status) ?
                       ZpClientLocalChannel_ReceiveClose(Session->Owner, &ChannelClose) : Status;

        case ZpMessageChannelWindow:
            Status = ZpMessage_DecodeChannelWindow(Frame->Body,
                                                    Frame->BodyLength,
                                                    &ChannelId,
                                                    &CreditBytes);
            return NT_SUCCESS(Status) ?
                       ZpClientLocalChannel_ReceiveWindow(Session->Owner,
                                                         ChannelId,
                                                         CreditBytes) : Status;
    }
    return STATUS_PROTOCOL_UNREACHABLE;
}

ZP_STATUS
ZpClientSession_Prepare(
    _Out_ PZP_CLIENT_SESSION Session,
    _Inout_ PZP_CLIENT_OBJECT Owner,
    _In_ ZP_CLIENT_SESSION_SEND_ROUTINE Send,
    _In_ ZP_CLIENT_SESSION_FAILURE_ROUTINE Failure,
    _In_opt_ PVOID Context,
    _In_opt_ NCRYPT_KEY_HANDLE ExternalKey)
{
    RtlZeroMemory(Session, sizeof(*Session));
    Session->Owner = Owner;
    Session->Send = Send;
    Session->Failure = Failure;
    Session->Context = Context;
    return ZpClientSession_CreateIdentity(Session, ExternalKey);
}

NTSTATUS
ZpClientSession_Start(
    _Inout_ PZP_CLIENT_SESSION Session)
{
    BYTE Body[ZP_CLIENT_HELLO_WIRE_SIZE];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpConnection_Initialize(&Session->Connection,
                                     ZpConnectionRoleClient,
                                     ZpClientSession_MessageCallback,
                                     Session);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Session->ConnectionInitialized = TRUE;
    Status = ZpClient_NotifyState((ZP_CLIENT_HANDLE)Session->Owner,
                                  ZpClientStateAuthenticating,
                                  ZpStatus_FromNtStatus(STATUS_SUCCESS));
    if (NT_SUCCESS(Status))
    {
        Status = ZpMessage_EncodeClientHello(Session->PublicKey,
                                             Body,
                                             sizeof(Body),
                                             &BodyLength);
    }
    return NT_SUCCESS(Status) ?
               Session->Send(Session->Context,
                             0,
                             ZpMessageClientHello,
                             Body,
                             BodyLength,
                             NULL,
                             0) : Status;
}

NTSTATUS
ZpClientSession_Receive(
    _Inout_ PZP_CLIENT_SESSION Session,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    return ZpConnection_Receive(&Session->Connection, Data, DataLength);
}

VOID
ZpClientSession_Uninitialize(
    _Inout_ PZP_CLIENT_SESSION Session)
{
    if (Session->ConnectionInitialized)
    {
        ZpConnection_Uninitialize(&Session->Connection);
    }
    if (Session->Key != 0 && Session->KeyOwned)
    {
        NCryptFreeObject(Session->Key);
    }
    if (Session->KeyProvider != 0)
    {
        NCryptFreeObject(Session->KeyProvider);
    }
    RtlSecureZeroMemory(Session, sizeof(*Session));
}
