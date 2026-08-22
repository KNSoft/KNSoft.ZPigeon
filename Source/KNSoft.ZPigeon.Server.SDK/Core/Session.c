#include "Session.h"

#include "../Server.inl"
#include "../../Network/Authentication.inl"

#include <Bcrypt.h>

#pragma comment(lib, "Bcrypt.lib")

static
NTSTATUS
ZpServerSession_SelectModules(
    _Inout_ PZP_SERVER_SESSION Session,
    _In_ const ZP_CLIENT_HELLO_VIEW* Hello)
{
    ZP_MODULE_RECORD ClientModule;
    ULONG ClientIndex = 0, ServerIndex = 0;
    NTSTATUS Status;

    Session->ModuleCount = 0;
    while (ClientIndex < Hello->Modules.Count &&
           ServerIndex < Session->Owner->Config.ModuleCount)
    {
        Status = ZpMessage_GetModuleRecord(&Hello->Modules,
                                           (USHORT)ClientIndex,
                                           &ClientModule);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if (ClientModule.ModuleId < Session->Owner->Config.Modules[ServerIndex].ModuleId)
        {
            ClientIndex++;
        }
        else if (ClientModule.ModuleId > Session->Owner->Config.Modules[ServerIndex].ModuleId)
        {
            ServerIndex++;
        }
        else
        {
            if (ClientModule.ModuleVersion ==
                Session->Owner->Config.Modules[ServerIndex].ModuleVersion)
            {
                Session->Modules[Session->ModuleCount++] = ClientModule;
            }
            ClientIndex++;
            ServerIndex++;
        }
    }
    return STATUS_SUCCESS;
}

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
    BYTE Body[sizeof(BYTE) + ZP_MODULE_MAX_COUNT * ZP_MODULE_RECORD_WIRE_SIZE];
    ULONG BodyLength, ChannelId, CreditBytes;
    ULONGLONG Token;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Connection);
    switch (Frame->MessageType)
    {
        case ZpMessageClientHello:
            Status = ZpMessage_DecodeClientHello(Frame->Body, Frame->BodyLength, &Hello);
            if (NT_SUCCESS(Status))
            {
                RtlCopyMemory(Session->PublicKey,
                              Hello.ClientPublicKey,
                              sizeof(Session->PublicKey));
                Status = ZpServerSession_SelectModules(Session, &Hello);
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
                                             ZpMessageServerChallenge,
                                             Body,
                                             BodyLength) : Status;

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
                                               ZpMessageReady,
                                               Body,
                                               BodyLength);
            }
            if (NT_SUCCESS(Status))
            {
                ZpServerConnection_SetModules(Session->Public,
                                              Session->Modules,
                                              Session->ModuleCount);
                ZpServerConnection_SetPhase(Session->Public, ZpConnectionPhaseReady);
                ZpServer_NotifyConnection((ZP_SERVER_HANDLE)Session->Owner,
                                          (ZP_CONNECTION_HANDLE)Session->Public,
                                          ZpConnectionPhaseReady,
                                          ZpStatus_FromNtStatus(STATUS_SUCCESS));
            }
            return Status;

        case ZpMessagePing:
            Status = ZpMessage_DecodePing(ZpMessagePing, Frame->Body, Frame->BodyLength, &Token);
            if (NT_SUCCESS(Status))
            {
                Status = ZpMessage_EncodePing(Token, Body, sizeof(Body), &BodyLength);
            }
            return NT_SUCCESS(Status) ?
                       Session->Public->Send(Session->Public,
                                             ZpMessagePong,
                                             Body,
                                             BodyLength) : Status;

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

VOID
ZpServerSession_Uninitialize(
    _Inout_ PZP_SERVER_SESSION Session)
{
    if (Session->ConnectionInitialized)
    {
        ZpConnection_Uninitialize(&Session->Connection);
    }
    RtlSecureZeroMemory(Session, sizeof(*Session));
}
