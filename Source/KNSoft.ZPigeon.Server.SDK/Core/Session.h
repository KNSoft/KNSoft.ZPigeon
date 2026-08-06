#pragma once

#include "Connection.h"
#include "../../Network/Connection.h"

struct _ZP_SERVER_OBJECT;

typedef struct _ZP_SERVER_SESSION
{
    struct _ZP_SERVER_OBJECT* Owner;
    PZP_CONNECTION_OBJECT Public;
    BYTE PublicKey[ZP_CLIENT_PUBLIC_KEY_SIZE];
    BYTE Challenge[ZP_SERVER_CHALLENGE_SIZE];
    ZP_MODULE_VERSION Modules[ZP_MODULE_MAX_ID];
    BYTE ModuleCount;
    ULONGLONG ModuleMask;
    ZP_CONNECTION Connection;
    BOOLEAN ConnectionInitialized;
} ZP_SERVER_SESSION, *PZP_SERVER_SESSION;

NTSTATUS
ZpServerSession_Initialize(
    _Out_ PZP_SERVER_SESSION Session,
    _Inout_ struct _ZP_SERVER_OBJECT* Owner,
    _Inout_ PZP_CONNECTION_OBJECT Connection);

NTSTATUS
ZpServerSession_Receive(
    _Inout_ PZP_SERVER_SESSION Session,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
    _In_ ULONG DataLength);

VOID
ZpServerSession_Uninitialize(
    _Inout_ PZP_SERVER_SESSION Session);
