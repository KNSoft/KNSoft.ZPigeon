#pragma once

#include <KNSoft/ZPigeon/Server.h>

typedef struct _ZP_SERVER_OBJECT
{
    RTL_SRWLOCK Lock;
    ZP_SERVER_STATE State;
    ZP_SERVER_CONFIG Config;
} ZP_SERVER_OBJECT, *PZP_SERVER_OBJECT;
