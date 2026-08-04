/*
 * PROJECT:     ReactOS Resource Hub private interface
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Connection registration shared by firmware enumerators
 */

#pragma once

#include <reshub.h>

#define IOCTL_RH_REGISTER_CONNECTION CTL_CODE(FILE_DEVICE_RESOURCE_HUB, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define RH_REGISTER_CONNECTION_VERSION 1

typedef struct _RH_REGISTER_CONNECTION_INPUT
{
    ULONG Version;
    LARGE_INTEGER ConnectionId;
    UCHAR Class;
    UCHAR Type;
    USHORT Reserved;
    ULONG PropertiesLength;
    UCHAR Properties[ANYSIZE_ARRAY];
} RH_REGISTER_CONNECTION_INPUT, *PRH_REGISTER_CONNECTION_INPUT;
