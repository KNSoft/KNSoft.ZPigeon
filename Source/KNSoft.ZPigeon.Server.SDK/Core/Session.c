#include "Session.h"

#include "../Server.inl"
#include "../../Network/Authentication.inl"

#include <Bcrypt.h>

#pragma comment(lib, "Bcrypt.lib")

static
NTSTATUS
NTAPI
ZpServerSession_MessageCallback(
    _Inout_ PZP_CONNECTION Connection,
    _In_ const ZP_FRAME_VIEW* Frame,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_SESSION Session = Context;
    ZP_CLIENT_HELLO_VIEW Hello;
    ZP_BUFFER_VIEW Signature;
    ZP_RESPONSE_VIEW Response;
    ZP_CHANNEL_DATA_VIEW ChannelData;
    ZP_CHANNEL_CLOSE ChannelClose;
    ZP_READY Ready;
    BYTE Body[ZP_MODULE_MANIFEST_MAX_WIRE_SIZE];
    ULONG BodyLength, ChannelId, CreditBytes;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Connection);
    switch (Frame->MessageType)
    {
        case ZpMessageClientHello:
            Status = ZpMessage_DecodeClientHello(Frame->Body, Frame->BodyLength, &Hello);
            if (NT_SUCCESS(Status))
            {
                BYTE ClientIndex = 0, ServerIndex = 0;

                RtlCopyMemory(Session->PublicKey,
                              Hello.ClientPublicKey,
                              sizeof(Session->PublicKey));
                while (ClientIndex < Hello.ModuleCount &&
                       ServerIndex < Session->Owner->Config.ModuleCount)
                {
                    PCZP_MODULE_VERSION ClientModule = &Hello.Modules[ClientIndex];
                    PCZP_MODULE_VERSION ServerModule =
                        &Session->Owner->Config.Modules[ServerIndex];

                    if (ClientModule->ModuleId < ServerModule->ModuleId)
                    {
                        ClientIndex++;
                    }
                    else if (ClientModule->ModuleId > ServerModule->ModuleId)
                    {
                        ServerIndex++;
                    }
                    else
                    {
                        if (ClientModule->Version == ServerModule->Version)
                        {
                            Session->Modules[Session->ModuleCount++] = *ClientModule;
                            Session->ModuleMask |= ZP_MODULE_BIT(ClientModule->ModuleId);
                        }
                        ClientIndex++;
                        ServerIndex++;
                    }
                }
                if (Session->ModuleCount == 0)
                {
                    Status = STATUS_NOT_SUPPORTED;
                }
            }
            if (NT_SUCCESS(Status))
            {
                Status = BCryptGenRandom(NULL,
                                         Session->Challenge,
                                         sizeof(Session->Challenge),
                                         BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            }
            if (NT_SUCCESS(Status))
            {
                Status = ZpMessage_EncodeServerChallenge(Session->Challenge,
                                                         Body,
                                                         sizeof(Body),
                                                         &BodyLength);
            }
            return NT_SUCCESS(Status) ?
                       Session->Public->Send(Session->Public,
                                             0,
                                             ZpMessageServerChallenge,
                                             Body,
                                             BodyLength,
                                             NULL,
                                             0) : Status;

        case ZpMessageClientAuthenticate:
            Status = ZpMessage_DecodeClientAuthenticate(Frame->Body,
                                                        Frame->BodyLength,
                                                        &Signature);
            if (NT_SUCCESS(Status))
            {
                Status = ZpAuthentication_Verify(Session->PublicKey,
                                                 Session->Challenge,
                                                 Signature.Buffer);
            }
            RtlSecureZeroMemory(Session->Challenge, sizeof(Session->Challenge));
            if (!NT_SUCCESS(Status))
            {
                return STATUS_ACCESS_DENIED;
            }
            Ready.Modules = Session->Modules;
            Ready.ModuleCount = Session->ModuleCount;
            Status = ZpMessage_EncodeReady(&Ready, Body, sizeof(Body), &BodyLength);
            if (NT_SUCCESS(Status))
            {
                Status = Session->Public->Send(Session->Public,
                                               0,
                                               ZpMessageReady,
                                               Body,
                                               BodyLength,
                                               NULL,
                                               0);
            }
            if (NT_SUCCESS(Status))
            {
                ZpServerConnection_SetModuleMask(Session->Public, Session->ModuleMask);
                ZpServerConnection_SetPhase(Session->Public, ZpConnectionPhaseReady);
                ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Session->Owner,
                                          (ZP_CONNECTION_HANDLE)Session->Public,
                                          ZpConnectionPhaseReady,
                                          ZpStatus_FromNtStatus(STATUS_SUCCESS));
            }
            return Status;

        case ZpMessageResponse:
            Status = ZpMessage_DecodeResponse(Frame->Body, Frame->BodyLength, &Response);
            return NT_SUCCESS(Status) ?
                       ZpServerConnection_ReceiveResponse(Session->Public, &Response) : Status;

        case ZpMessageChannelWindow:
            Status = ZpMessage_DecodeChannelWindow(Frame->Body,
                                                    Frame->BodyLength,
                                                    &ChannelId,
                                                    &CreditBytes);
            return NT_SUCCESS(Status) ?
                       ZpServerConnection_ReceiveChannelWindow(Session->Public,
                                                               ChannelId,
                                                               CreditBytes) : Status;

        case ZpMessageChannelData:
            Status = ZpMessage_DecodeChannelData(Frame->Body, Frame->BodyLength, &ChannelData);
            return NT_SUCCESS(Status) ?
                       ZpServerConnection_ReceiveChannelData(Session->Public, &ChannelData) : Status;

        case ZpMessageChannelClose:
            Status = ZpMessage_DecodeChannelClose(Frame->Body, Frame->BodyLength, &ChannelClose);
            return NT_SUCCESS(Status) ?
                       ZpServerConnection_ReceiveChannelClose(Session->Public, &ChannelClose) : Status;
    }
    return STATUS_PROTOCOL_UNREACHABLE;
}

NTSTATUS
ZpServerSession_Initialize(
    _Out_ PZP_SERVER_SESSION Session,
    _Inout_ PZP_SERVER_OBJECT Owner,
    _Inout_ PZP_CONNECTION_OBJECT Connection)
{
    NTSTATUS Status;

    RtlZeroMemory(Session, sizeof(*Session));
    Session->Owner = Owner;
    Session->Public = Connection;
    Status = ZpConnection_Initialize(&Session->Connection,
                                     ZpConnectionRoleServer,
                                     ZpServerSession_MessageCallback,
                                     Session);
    if (NT_SUCCESS(Status))
    {
        Session->ConnectionInitialized = TRUE;
        RtlAcquireSRWLockExclusive(&Connection->Lock);
        Connection->ProtocolConnection = &Session->Connection;
        RtlReleaseSRWLockExclusive(&Connection->Lock);
    }
    return Status;
}

NTSTATUS
ZpServerSession_Receive(
    _Inout_ PZP_SERVER_SESSION Session,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    return ZpConnection_Receive(&Session->Connection, Data, DataLength);
}

NTSTATUS
NTAPI
ZpServer_QueryConnectionClientPublicKey(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _Out_writes_bytes_(ZP_CLIENT_PUBLIC_KEY_SIZE) PBYTE PublicKey)
{
    PZP_CONNECTION_OBJECT ConnectionObject = Connection;
    PZP_SERVER_SESSION Session;
    NTSTATUS Status;

    if (ConnectionObject == NULL || PublicKey == NULL) return STATUS_INVALID_PARAMETER;
    RtlAcquireSRWLockShared(&ConnectionObject->Lock);
    if (ConnectionObject->Phase == ZpConnectionPhaseReady &&
        ConnectionObject->ProtocolConnection != NULL)
    {
        Session = CONTAINING_RECORD(ConnectionObject->ProtocolConnection,
                                    ZP_SERVER_SESSION,
                                    Connection);
        RtlCopyMemory(PublicKey, Session->PublicKey, ZP_CLIENT_PUBLIC_KEY_SIZE);
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
    RtlReleaseSRWLockShared(&ConnectionObject->Lock);
    return Status;
}

VOID
ZpServerSession_Uninitialize(
    _Inout_ PZP_SERVER_SESSION Session)
{
    if (Session->ConnectionInitialized)
    {
        RtlAcquireSRWLockExclusive(&Session->Public->Lock);
        Session->Public->ProtocolConnection = NULL;
        RtlReleaseSRWLockExclusive(&Session->Public->Lock);
        ZpConnection_Uninitialize(&Session->Connection);
    }
    RtlSecureZeroMemory(Session, sizeof(*Session));
}
