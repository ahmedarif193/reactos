/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Display-driver escape bridge to the RPi5 video miniport
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi5_gdi.h"

#define RPI5VC4_OPENGL_ICD_VERSION 1
#define RPI5VC4_OPENGL_ICD_DRIVER_VERSION 1
#define OPENGL_GETINFO_DRVNAME 0

typedef struct _RPI5VC4_OPENGL_INFO
{
    ULONG Version;
    ULONG DriverVersion;
    WCHAR DriverName[MAX_PATH + 1];
} RPI5VC4_OPENGL_INFO, *PRPI5VC4_OPENGL_INFO;

static BOOL
Rpi5Vc4V3dExecutionSupported(
    _In_ PPDEV Device)
{
    RPI5VC4_V3D_INFO Info;
    ULONG Returned = 0;

    memset(&Info, 0, sizeof(Info));
    if (EngDeviceIoControl(Device->hDriver,
                           IOCTL_VIDEO_RPI5VC4_QUERY_V3D,
                           NULL,
                           0,
                           &Info,
                           sizeof(Info),
                           &Returned) != 0 ||
        Returned < sizeof(Info))
    {
        return FALSE;
    }

    return Info.AbiVersion == RPI5VC4_XPDM_ABI_VERSION &&
           Info.Version == 71 &&
           (Info.Flags & RPI5VC4_V3D_FLAG_IDENT_VALID) != 0;
}

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
        if (RequestedEscape == RPI5VC4_ESCAPE_RENDER_CLEAR ||
            RequestedEscape == RPI5VC4_ESCAPE_RENDER_TRIANGLE ||
            RequestedEscape == RPI5VC4_ESCAPE_RENDER_BATCH ||
            RequestedEscape == RPI5VC4_ESCAPE_UPLOAD_TEXTURE ||
            RequestedEscape == RPI5VC4_ESCAPE_RENDER_GRAPH ||
            RequestedEscape == RPI5VC4_ESCAPE_READ_GRAPH)
            return Rpi5Vc4V3dExecutionSupported(Device) ? 1 : 0;

        if (RequestedEscape == OPENGL_GETINFO)
            return Rpi5Vc4V3dExecutionSupported(Device) ? 1 : 0;

        return RequestedEscape == RPI5VC4_ESCAPE_QUERY_PLATFORM ||
               RequestedEscape == RPI5VC4_ESCAPE_QUERY_V3D ||
               RequestedEscape == RPI5VC4_ESCAPE_RUN_V3D_SELFTEST;
    }

    if (pvOut == NULL)
    {
        return 0;
    }

    Returned = 0;
    if (iEsc == OPENGL_GETINFO)
    {
        static const WCHAR IcdName[] = L"Rpi5Vc4";
        ULONG Query;
        PRPI5VC4_OPENGL_INFO Info;

        if (pvIn == NULL ||
            cjIn < sizeof(Query) ||
            cjOut < sizeof(*Info) ||
            !Rpi5Vc4V3dExecutionSupported(Device))
        {
            return 0;
        }

        Query = *(PULONG)pvIn;
        if (Query != OPENGL_GETINFO_DRVNAME)
            return 0;

        Info = pvOut;
        memset(Info, 0, sizeof(*Info));
        Info->Version = RPI5VC4_OPENGL_ICD_VERSION;
        Info->DriverVersion = RPI5VC4_OPENGL_ICD_DRIVER_VERSION;
        memcpy(Info->DriverName, IcdName, sizeof(IcdName));
        return sizeof(*Info);
    }
    else if (iEsc == RPI5VC4_ESCAPE_QUERY_PLATFORM &&
             cjOut >= sizeof(RPI5VC4_PLATFORM_INFO))
    {
        if (EngDeviceIoControl(Device->hDriver,
                               IOCTL_VIDEO_RPI5VC4_QUERY_PLATFORM,
                               NULL,
                               0,
                               pvOut,
                               cjOut,
                               &Returned) == 0)
        {
            return Returned;
        }
    }
    else if (iEsc == RPI5VC4_ESCAPE_QUERY_V3D &&
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
    else if (iEsc == RPI5VC4_ESCAPE_RENDER_CLEAR &&
             pvIn != NULL &&
             cjIn >= sizeof(RPI5VC4_V3D_CLEAR_REQUEST) &&
             cjOut >= FIELD_OFFSET(RPI5VC4_V3D_CLEAR_RESULT, Pixels))
    {
        if (EngDeviceIoControl(Device->hDriver,
                               IOCTL_VIDEO_RPI5VC4_RENDER_CLEAR,
                               pvIn,
                               cjIn,
                               pvOut,
                               cjOut,
                               &Returned) == 0)
        {
            return Returned;
        }
    }
    else if (iEsc == RPI5VC4_ESCAPE_RENDER_TRIANGLE &&
             pvIn != NULL &&
             cjIn >= sizeof(RPI5VC4_V3D_TRIANGLE_REQUEST) &&
             cjOut >= FIELD_OFFSET(RPI5VC4_V3D_TRIANGLE_RESULT, Pixels))
    {
        if (EngDeviceIoControl(Device->hDriver,
                               IOCTL_VIDEO_RPI5VC4_RENDER_TRIANGLE,
                               pvIn,
                               cjIn,
                               pvOut,
                               cjOut,
                               &Returned) == 0)
        {
            return Returned;
        }
    }
    else if (iEsc == RPI5VC4_ESCAPE_RENDER_BATCH &&
             pvIn != NULL &&
             cjIn >= FIELD_OFFSET(RPI5VC4_V3D_BATCH_REQUEST, Vertices) &&
             cjOut >= FIELD_OFFSET(RPI5VC4_V3D_BATCH_RESULT, Pixels))
    {
        if (EngDeviceIoControl(Device->hDriver,
                               IOCTL_VIDEO_RPI5VC4_RENDER_BATCH,
                               pvIn,
                               cjIn,
                               pvOut,
                               cjOut,
                               &Returned) == 0)
        {
            return Returned;
        }
    }
    else if (iEsc == RPI5VC4_ESCAPE_UPLOAD_TEXTURE &&
             pvIn != NULL &&
             cjIn >= FIELD_OFFSET(RPI5VC4_V3D_TEXTURE_UPLOAD_REQUEST,
                                  Pixels) &&
             cjOut >= sizeof(RPI5VC4_V3D_TEXTURE_UPLOAD_RESULT))
    {
        if (EngDeviceIoControl(Device->hDriver,
                               IOCTL_VIDEO_RPI5VC4_UPLOAD_TEXTURE,
                               pvIn,
                               cjIn,
                               pvOut,
                               cjOut,
                               &Returned) == 0)
        {
            return Returned;
        }
    }
    else if (iEsc == RPI5VC4_ESCAPE_RENDER_GRAPH &&
             pvIn != NULL &&
             cjIn >= FIELD_OFFSET(RPI5VC4_V3D_RENDER_GRAPH_REQUEST,
                                  Pixels) &&
             cjOut >= sizeof(RPI5VC4_V3D_RENDER_GRAPH_RESULT))
    {
        if (EngDeviceIoControl(Device->hDriver,
                               IOCTL_VIDEO_RPI5VC4_RENDER_GRAPH,
                               pvIn,
                               cjIn,
                               pvOut,
                               cjOut,
                               &Returned) == 0)
        {
            return Returned;
        }
    }
    else if (iEsc == RPI5VC4_ESCAPE_READ_GRAPH &&
             pvIn != NULL &&
             cjIn >= sizeof(RPI5VC4_V3D_READ_GRAPH_REQUEST) &&
             cjOut >= FIELD_OFFSET(RPI5VC4_V3D_READ_GRAPH_RESULT,
                                   Pixels))
    {
        if (EngDeviceIoControl(Device->hDriver,
                               IOCTL_VIDEO_RPI5VC4_READ_GRAPH,
                               pvIn,
                               cjIn,
                               pvOut,
                               cjOut,
                               &Returned) == 0)
        {
            return Returned;
        }
    }

    return 0;
}
