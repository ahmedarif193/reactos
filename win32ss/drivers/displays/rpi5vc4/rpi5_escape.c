/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Display-driver escape bridge to the RPi5 video miniport
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "framebuf.h"
#include <reactos/rpi5vc4_xpdm.h>

ULONG
APIENTRY
DrvEscape(
    _In_ SURFOBJ *pso,
    _In_ ULONG iEsc,
    _In_ ULONG cjIn,
    _In_reads_bytes_(cjIn) PVOID pvIn,
    _In_ ULONG cjOut,
    _Out_writes_bytes_(cjOut) PVOID pvOut)
{
    PPDEV Device;
    ULONG RequestedEscape;
    ULONG Returned;

    if (pso == NULL || pso->dhpdev == NULL)
        return 0;

    Device = (PPDEV)pso->dhpdev;
    if (iEsc == QUERYESCSUPPORT)
    {
        if (pvIn == NULL || cjIn < sizeof(RequestedEscape))
            return 0;

        RequestedEscape = *(PULONG)pvIn;
        return RequestedEscape == RPI5VC4_ESCAPE_QUERY_V3D ||
               RequestedEscape == RPI5VC4_ESCAPE_RUN_V3D_SELFTEST;
    }

    if (pvOut == NULL)
    {
        return 0;
    }

    Returned = 0;
    if (iEsc == RPI5VC4_ESCAPE_QUERY_V3D &&
        cjOut >= sizeof(RPI5VC4_V3D_INFO))
    {
        if (EngDeviceIoControl(Device->hDriver,
                               IOCTL_VIDEO_RPI5VC4_QUERY_V3D,
                               NULL,
                               0,
                               pvOut,
                               cjOut,
                               &Returned) == 0)
        {
            return Returned;
        }
    }
    else if (iEsc == RPI5VC4_ESCAPE_RUN_V3D_SELFTEST &&
             cjOut >= sizeof(RPI5VC4_V3D_SELFTEST))
    {
        if (EngDeviceIoControl(Device->hDriver,
                               IOCTL_VIDEO_RPI5VC4_RUN_V3D_SELFTEST,
                               NULL,
                               0,
                               pvOut,
                               cjOut,
                               &Returned) == 0)
        {
            return Returned;
        }
    }

    return 0;
}
