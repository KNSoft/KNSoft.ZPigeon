#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_RDP_CHILD_SESSION_STATE_STOPPED 0
#define ZP_RDP_CHILD_SESSION_STATE_STARTING 1
#define ZP_RDP_CHILD_SESSION_STATE_CONNECTING 2
#define ZP_RDP_CHILD_SESSION_STATE_ACTIVE 3
#define ZP_RDP_CHILD_SESSION_STATE_DISCONNECTED 4

ZP_STATUS
ZpRdpChildSession_Query(
    _Out_ PBOOLEAN Enabled,
    _Out_ PULONG State,
    _Out_ PULONG SessionId,
    _Out_ PULONG Error);

ZP_STATUS
ZpRdpChildSession_Start(VOID);

ZP_STATUS
ZpRdpChildSession_Stop(VOID);

VOID
ZpRdpChildSession_Close(VOID);

EXTERN_C_END
