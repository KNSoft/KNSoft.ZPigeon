#pragma once

#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/Audio.h"

typedef struct _ZP_CLIENT_AUDIO_CHANNEL ZP_CLIENT_AUDIO_CHANNEL, *PZP_CLIENT_AUDIO_CHANNEL;

NTSTATUS
ZpAudio_CreateStreamChannel(
    _Inout_ struct _ZP_CLIENT_OBJECT* Client,
    _In_ PZP_AUDIO_STREAM_REQUEST_VIEW Request,
    _Out_ PZP_CLIENT_AUDIO_CHANNEL* Channel);

ZP_STATUS
ZpAudio_Execute(
    _Inout_ struct _ZP_CLIENT_OBJECT* Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_AUDIO_CHANNEL* Channel);

VOID
ZpAudio_CommitChannel(
    _Inout_ PZP_CLIENT_AUDIO_CHANNEL Channel,
    _In_ LOGICAL ResponseSent);
