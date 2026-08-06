#pragma once

#include <KNSoft/ZPigeon/Client.h>

typedef struct _ZP_CLIENT_OBJECT
{
    RTL_SRWLOCK Lock;
    ZP_CLIENT_STATE State;
    ZP_CLIENT_CONFIG Config;
} ZP_CLIENT_OBJECT, *PZP_CLIENT_OBJECT;
