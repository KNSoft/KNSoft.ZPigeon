#pragma once

#include "../../Network/Connection.h"

#include <Ncrypt.h>

struct _ZP_CLIENT_OBJECT;

typedef
NTSTATUS
(NTAPI *ZP_CLIENT_SESSION_SEND_ROUTINE)(
    _In_opt_ PVOID Context,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_opt_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength);

typedef
VOID
(NTAPI *ZP_CLIENT_SESSION_FAILURE_ROUTINE)(
    _In_opt_ PVOID Context,
    _In_ ZP_STATUS Status);

typedef struct _ZP_CLIENT_SESSION
{
    struct _ZP_CLIENT_OBJECT* Owner;
    ZP_CLIENT_SESSION_SEND_ROUTINE Send;
    ZP_CLIENT_SESSION_FAILURE_ROUTINE Failure;
    PVOID Context;
    NCRYPT_PROV_HANDLE KeyProvider;
    NCRYPT_KEY_HANDLE Key;
    LOGICAL KeyOwned;
    BYTE PublicKey[ZP_CLIENT_PUBLIC_KEY_SIZE];
    ZP_CONNECTION Connection;
    LOGICAL ConnectionInitialized;
} ZP_CLIENT_SESSION, *PZP_CLIENT_SESSION;

ZP_STATUS
ZpClientSession_Prepare(
    _Out_ PZP_CLIENT_SESSION Session,
    _Inout_ struct _ZP_CLIENT_OBJECT* Owner,
    _In_ ZP_CLIENT_SESSION_SEND_ROUTINE Send,
    _In_ ZP_CLIENT_SESSION_FAILURE_ROUTINE Failure,
    _In_opt_ PVOID Context,
    _In_opt_ NCRYPT_KEY_HANDLE ExternalKey);

NTSTATUS
ZpClientSession_Start(
    _Inout_ PZP_CLIENT_SESSION Session);

NTSTATUS
ZpClientSession_Receive(
    _Inout_ PZP_CLIENT_SESSION Session,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength);

VOID
ZpClientSession_Uninitialize(
    _Inout_ PZP_CLIENT_SESSION Session);
