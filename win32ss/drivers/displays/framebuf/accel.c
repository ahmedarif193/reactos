/*
 * ReactOS Generic Framebuffer VMware acceleration helpers
 *
 * Copyright (C) 2024 ReactOS Team
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

#ifndef ROP3_TO_ROP4
/* Local copy of the Win32 helper that mirrors an 8-bit ROP3 into a 16-bit ROP4 value. */
#define ROP3_TO_ROP4(Rop3) ((((Rop3) >> 8) & 0xff00) | (((Rop3) >> 16) & 0x00ff))
#endif

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

    if (!EngDeviceIoControl(ppdev->hDriver,
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

    if (!EngDeviceIoControl(ppdev->hDriver,
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

BOOL APIENTRY
DrvCopyBits(SURFOBJ *psoDst,
            SURFOBJ *psoSrc,
            CLIPOBJ *pco,
            XLATEOBJ *pxlo,
            RECTL *prclDst,
            POINTL *pptlSrc)
{
    PPDEV ppdev = (PPDEV)psoDst->dhpdev;

    if (ppdev && ppdev->VmwareFifo &&
        psoDst->iType == STYPE_DEVICE &&
        psoSrc && psoSrc->iType == STYPE_DEVICE &&
        pptlSrc != NULL &&
        (!pco || pco->iDComplexity == DC_TRIVIAL) &&
        (!pxlo || (pxlo->flXlate & XO_TRIVIAL)))
    {
        if (FbVmwareRectCopy(ppdev, prclDst, pptlSrc))
            return TRUE;
    }

    return EngCopyBits(psoDst, psoSrc, pco, pxlo, prclDst, pptlSrc);
}

BOOL APIENTRY
DrvBitBlt(SURFOBJ *psoDst,
          SURFOBJ *psoSrc,
          SURFOBJ *psoMask,
          CLIPOBJ *pco,
          XLATEOBJ *pxlo,
          RECTL *prclDst,
          POINTL *pptlSrc,
          POINTL *pptlMask,
          BRUSHOBJ *pbo,
          POINTL *pptlBrush,
          ROP4 rop4)
{
    PPDEV ppdev = (PPDEV)psoDst->dhpdev;

    if (ppdev && ppdev->VmwareFifo && (!pco || pco->iDComplexity == DC_TRIVIAL))
    {
        BOOL trivialXlate = (!pxlo) || (pxlo->flXlate & XO_TRIVIAL);

        if (rop4 == ROP3_TO_ROP4(SRCCOPY) &&
            psoSrc && psoSrc->iType == STYPE_DEVICE &&
            psoDst->iType == STYPE_DEVICE &&
            psoMask == NULL &&
            pptlSrc != NULL &&
            trivialXlate)
        {
            if (FbVmwareRectCopy(ppdev, prclDst, pptlSrc))
                return TRUE;
        }
        else if (rop4 == ROP3_TO_ROP4(PATCOPY) &&
                 psoDst->iType == STYPE_DEVICE &&
                 psoSrc == NULL &&
                 psoMask == NULL &&
                 pbo != NULL &&
                 pbo->iSolidColor != 0xFFFFFFFF &&
                 ppdev->BitsPerPixel >= 15 &&
                 trivialXlate)
        {
            if (FbVmwareRectFill(ppdev, prclDst, pbo->iSolidColor))
                return TRUE;
        }
    }

    return EngBitBlt(psoDst,
                     psoSrc,
                     psoMask,
                     pco,
                     pxlo,
                     prclDst,
                     pptlSrc,
                     pptlMask,
                     pbo,
                     pptlBrush,
                     rop4);
}
