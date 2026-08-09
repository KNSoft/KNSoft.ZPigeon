#include "Request.h"

NTSTATUS
NTAPI
ZpRequest_Cancel(
    _In_ ZP_REQUEST_HANDLE Request)
{
    return ((PZP_REQUEST_HEADER)Request)->Cancel(Request);
}

VOID
NTAPI
ZpRequest_Close(
    _In_ ZP_REQUEST_HANDLE Request)
{
    ZpRequest_Release((PZP_REQUEST_HEADER)Request);
}
