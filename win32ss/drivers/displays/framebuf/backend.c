/*
 * ReactOS Generic Framebuffer acceleration backend dispatch
 *
 * Copyright (C) 2026 ReactOS Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "framebuf.h"
#include <vmware/vmx_ioctl.h>

/* -----------------------------------------------------------------------
 * Generic (no-op) backend -- returns FALSE so callers fall through to Eng*
 * ----------------------------------------------------------------------- */

static BOOL
FbGenericRectCopy(_In_ PPDEV ppdev,
                  _In_ const RECTL *DestRect,
                  _In_ const POINTL *SrcPoint)
{
    UNREFERENCED_PARAMETER(ppdev);
    UNREFERENCED_PARAMETER(DestRect);
    UNREFERENCED_PARAMETER(SrcPoint);
    return FALSE;
}

static BOOL
FbGenericRectFill(_In_ PPDEV ppdev,
                  _In_ const RECTL *Rect,
                  _In_ ULONG Color)
{
    UNREFERENCED_PARAMETER(ppdev);
    UNREFERENCED_PARAMETER(Rect);
    UNREFERENCED_PARAMETER(Color);
    return FALSE;
}

/* -----------------------------------------------------------------------
 * VMware SVGA FIFO backend
 * ----------------------------------------------------------------------- */

static BOOL
FbVmwareRectCopy(_In_ PPDEV ppdev,
                 _In_ const RECTL *DestRect,
                 _In_ const POINTL *SrcPoint)
{
    VMWARE_VIDEO_BLIT cmd = {0};
    DWORD returned = 0;

    cmd.DestX = DestRect->left;
    cmd.DestY = DestRect->top;
    cmd.Width = DestRect->right - DestRect->left;
    cmd.Height = DestRect->bottom - DestRect->top;

    if (cmd.Width == 0 || cmd.Height == 0)
        return TRUE;

    cmd.SrcX = SrcPoint->x;
    cmd.SrcY = SrcPoint->y;

    if (EngDeviceIoControl(ppdev->hDriver,
                            IOCTL_VIDEO_VMWARE_FIFO_BLIT,
                            &cmd,
                            sizeof(cmd),
                            NULL,
                            0,
                            &returned))
    {
        return FALSE;
    }

    return TRUE;
}

static BOOL
FbVmwareRectFill(_In_ PPDEV ppdev,
                 _In_ const RECTL *Rect,
                 _In_ ULONG Color)
{
    VMWARE_VIDEO_FILL cmd = {0};
    DWORD returned = 0;

    cmd.X = Rect->left;
    cmd.Y = Rect->top;
    cmd.Width = Rect->right - Rect->left;
    cmd.Height = Rect->bottom - Rect->top;
    cmd.Color = Color;

    if (cmd.Width == 0 || cmd.Height == 0)
        return TRUE;

    if (EngDeviceIoControl(ppdev->hDriver,
                            IOCTL_VIDEO_VMWARE_FIFO_FILL,
                            &cmd,
                            sizeof(cmd),
                            NULL,
                            0,
                            &returned))
    {
        return FALSE;
    }

    return TRUE;
}

/* -----------------------------------------------------------------------
 * Backend tables
 * ----------------------------------------------------------------------- */

static const FB_ACCEL_BACKEND FbGenericBackend =
{
    FbGenericRectCopy,
    FbGenericRectFill
};

static const FB_ACCEL_BACKEND FbVmwareBackend =
{
    FbVmwareRectCopy,
    FbVmwareRectFill
};

/* -----------------------------------------------------------------------
 * Backend selection / teardown
 * ----------------------------------------------------------------------- */

VOID
FbSelectAccelerationBackend(_Inout_ PPDEV ppdev)
{
    VMWARE_VIDEO_CAPS caps = {0};
    DWORD returned = 0;

    ppdev->AccelBackend = &FbGenericBackend;
    ppdev->BackendContext = NULL;

    if (ppdev->UsingFallbackSurface || ppdev->BitsPerPixel < 15)
        return;

    if (!EngDeviceIoControl(ppdev->hDriver,
                            IOCTL_VIDEO_VMWARE_QUERY_CAPS,
                            NULL,
                            0,
                            &caps,
                            sizeof(caps),
                            &returned) &&
        returned >= sizeof(caps) &&
        caps.Version == VMWARE_VIDEO_CAPS_VERSION &&
        (caps.Caps & VMWARE_VIDEO_CAP_FIFO))
    {
        ppdev->AccelBackend = &FbVmwareBackend;
        FB_DBG("VMware SVGA FIFO backend selected (caps 0x%lx)\n",
               (unsigned long)caps.Caps);
    }
}

VOID
FbCleanupAccelerationBackend(_Inout_ PPDEV ppdev)
{
    ppdev->AccelBackend = NULL;
    ppdev->BackendContext = NULL;
}

/* -----------------------------------------------------------------------
 * Backend dispatch helpers (called from accel.c)
 * ----------------------------------------------------------------------- */

BOOL
FbBackendRectCopy(_In_ PPDEV ppdev,
                  _In_ const RECTL *DestRect,
                  _In_ const POINTL *SrcPoint)
{
    if (!ppdev->AccelBackend || !ppdev->AccelBackend->RectCopy)
        return FALSE;

    return ppdev->AccelBackend->RectCopy(ppdev, DestRect, SrcPoint);
}

BOOL
FbBackendRectFill(_In_ PPDEV ppdev,
                  _In_ const RECTL *Rect,
                  _In_ ULONG Color)
{
    if (!ppdev->AccelBackend || !ppdev->AccelBackend->RectFill)
        return FALSE;

    return ppdev->AccelBackend->RectFill(ppdev, Rect, Color);
}
