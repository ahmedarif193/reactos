/*
 * ReactOS UEFIFB shared ioctl definitions
 */

#pragma once

#include <devioctl.h>

#define IOCTL_VIDEO_UEFIFB_QUERY_CAPS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x910, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define UEFIFB_CAPS_VERSION      1

#define UEFIFB_CAP_LINEAR_ONLY   0x00000001

typedef struct _UEFIFB_CAPS
{
    ULONG Version;
    ULONG Caps;
} UEFIFB_CAPS, *PUEFIFB_CAPS;

