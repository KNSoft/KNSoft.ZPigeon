#include "Channel.h"

NTSTATUS
NTAPI
ZpChannel_Cancel(
    _In_ ZP_CHANNEL_HANDLE Channel)
{
    return ((PZP_CHANNEL_HEADER)Channel)->Cancel(Channel);
}

NTSTATUS
NTAPI
ZpChannel_Send(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    return ((PZP_CHANNEL_HEADER)Channel)->Send(Channel, Data, DataLength);
}

VOID
NTAPI
ZpChannel_Close(
    _In_ ZP_CHANNEL_HANDLE Channel)
{
    ((PZP_CHANNEL_HEADER)Channel)->Close(Channel);
}
