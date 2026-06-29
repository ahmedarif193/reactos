/*
 * ReactOS Canonical Display Driver (CDD) - Drawing DDI Hooks
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
 *
 * DESCRIPTION
 * -----------
 * Drawing DDI hooks and shadow buffer flush logic.
 *
 * Every hooked Drv* function follows the same pattern:
 *   1. If the target surface is our STYPE_DEVICE, substitute the shadow
 *      STYPE_BITMAP so that Eng* has a pvScan0 to draw on.
 *   2. Call the corresponding Eng* function on the shadow.
 *   3. Flush the dirty rectangle from the shadow to VRAM.
 *
 * This is the same architecture that Windows CDD uses: GDI draws to a
 * system-memory surface, and dirty rects are presented to the GPU.
 */

#include "cdd.h"

#ifndef ROP3_TO_ROP4
#define ROP3_TO_ROP4(Rop3) ((((Rop3) >> 8) & 0xff00) | (((Rop3) >> 16) & 0x00ff))
#endif

static volatile LONG g_CddDirtyPresentFailureLogCount = 0;

static BOOLEAN
CddNormalizeAndClipRect(
    _In_ PCDD_PDEV ppdev,
    _In_ const RECTL *InputRect,
    _Out_ PRECTL OutputRect);

static VOID
CddShadowFlushPathResult(
    _In_ PCDD_PDEV ppdev,
    _In_opt_ const CLIPOBJ *pco)
{
    RECTL rclFlush;

    if (pco != NULL)
    {
        CddShadowFlushRect(ppdev, &pco->rclBounds);
        return;
    }

    rclFlush.left = 0;
    rclFlush.top = 0;
    rclFlush.right = ppdev->ScreenWidth;
    rclFlush.bottom = ppdev->ScreenHeight;
    CddShadowFlushRect(ppdev, &rclFlush);
}

static VOID
CddPresentDirtyRect(
    _In_ PCDD_PDEV ppdev,
    _In_ const RECTL *Rect)
{
    RECTL ClippedRect;
    DWORD Returned = 0;

    if (!ppdev || !Rect)
        return;

    if (!CddNormalizeAndClipRect(ppdev, Rect, &ClippedRect))
        return;

    if (ppdev->D3DKMTConnected)
    {
        CddPresentPrimaryRect(ppdev, &ClippedRect);
        return;
    }

    if (!ppdev->SysmemFramebuffer)
        return;

    if (EngDeviceIoControl(ppdev->hDriver,
                           IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT,
                           &ClippedRect,
                           sizeof(ClippedRect),
                           NULL,
                           0,
                           &Returned))
    {
        LONG FailSeq = InterlockedIncrement(&g_CddDirtyPresentFailureLogCount);
        if (FailSeq <= 8)
        {
            CDD_DBG("dirty-rect present failed #%ld for (%ld,%ld)-(%ld,%ld)\n",
                    FailSeq,
                    ClippedRect.left,
                    ClippedRect.top,
                    ClippedRect.right,
                    ClippedRect.bottom);
        }
    }
}

static BOOLEAN
CddNormalizeRect(
    _In_ const RECTL *InputRect,
    _Out_ PRECTL OutputRect)
{
    RECTL rcl;

    if (!InputRect)
        return FALSE;

    rcl = *InputRect;

    if (rcl.left > rcl.right)
    {
        LONG t = rcl.left;
        rcl.left = rcl.right;
        rcl.right = t;
    }
    if (rcl.top > rcl.bottom)
    {
        LONG t = rcl.top;
        rcl.top = rcl.bottom;
        rcl.bottom = t;
    }

    if (rcl.left >= rcl.right || rcl.top >= rcl.bottom)
        return FALSE;

    *OutputRect = rcl;
    return TRUE;
}


/* ========================================================================
 * Shadow buffer flush routines
 * ======================================================================== */

/*
 * CddClipRectToScreen
 *
 * Clip a rectangle to the screen bounds.  Returns TRUE if the result
 * is non-empty.
 */
static BOOLEAN
CddClipRectToScreen(
    _In_ PCDD_PDEV ppdev,
    _Inout_ PRECTL Rect)
{
    if (Rect->left < 0)
        Rect->left = 0;
    if (Rect->top < 0)
        Rect->top = 0;
    if (Rect->right > (LONG)ppdev->ScreenWidth)
        Rect->right = ppdev->ScreenWidth;
    if (Rect->bottom > (LONG)ppdev->ScreenHeight)
        Rect->bottom = ppdev->ScreenHeight;

    return (Rect->left < Rect->right) && (Rect->top < Rect->bottom);
}

/*
 * CddNormalizeAndClipRect
 *
 * Normalize (swap if inverted) and clip to screen bounds.
 */
static BOOLEAN
CddNormalizeAndClipRect(
    _In_ PCDD_PDEV ppdev,
    _In_ const RECTL *InputRect,
    _Out_ PRECTL OutputRect)
{
    RECTL rcl;

    if (!ppdev || !CddNormalizeRect(InputRect, &rcl))
        return FALSE;

    if (!CddClipRectToScreen(ppdev, &rcl))
        return FALSE;

    *OutputRect = rcl;
    return TRUE;
}

static VOID
CddClearCursorRect(
    _In_ PCDD_PDEV ppdev)
{
    ppdev->OldCursorRect.left = 0;
    ppdev->OldCursorRect.top = 0;
    ppdev->OldCursorRect.right = 0;
    ppdev->OldCursorRect.bottom = 0;
}

static BOOLEAN
CddGetStoredCursorRect(
    _In_ PCDD_PDEV ppdev,
    _Out_ PRECTL Rect)
{
    return CddNormalizeAndClipRect(ppdev, &ppdev->OldCursorRect, Rect);
}

static VOID
CddSetStoredCursorRect(
    _In_ PCDD_PDEV ppdev,
    _In_opt_ const RECTL *Rect)
{
    RECTL clippedRect;

    if (Rect && CddNormalizeAndClipRect(ppdev, Rect, &clippedRect))
        ppdev->OldCursorRect = clippedRect;
    else
        CddClearCursorRect(ppdev);
}

static BOOLEAN
CddPointerSurfaceIsSane(
    _In_ const SURFOBJ *Surface,
    _In_ BOOLEAN Mask)
{
    if (!Surface ||
        Surface->iType != STYPE_BITMAP ||
        Surface->sizlBitmap.cx <= 0 ||
        Surface->sizlBitmap.cy <= 0 ||
        Surface->lDelta == 0)
    {
        return FALSE;
    }

    if (Mask && Surface->iBitmapFormat != BMF_1BPP)
        return FALSE;

    if (!Mask &&
        (Surface->iBitmapFormat < BMF_1BPP ||
         Surface->iBitmapFormat > BMF_32BPP))
    {
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN
CddValidatePointerShape(
    _In_ const SURFOBJ *pso,
    _In_opt_ const SURFOBJ *psoMask,
    _In_opt_ const SURFOBJ *psoColor,
    _In_ LONG xHot,
    _In_ LONG yHot,
    _In_ FLONG fl,
    _Out_ PLONG Width,
    _Out_ PLONG Height)
{
    LONG width, height;

    *Width = 0;
    *Height = 0;

    if (!pso)
        return FALSE;

    if (!psoMask && !psoColor)
        return TRUE;

    if (psoColor)
    {
        if (!CddPointerSurfaceIsSane(psoColor, FALSE))
            return FALSE;

        if ((fl & SPS_ALPHA) && psoColor->iBitmapFormat != BMF_32BPP)
            return FALSE;

        width = psoColor->sizlBitmap.cx;
        height = psoColor->sizlBitmap.cy;

        if (psoMask)
        {
            if (!CddPointerSurfaceIsSane(psoMask, TRUE) ||
                psoMask->sizlBitmap.cx != width ||
                psoMask->sizlBitmap.cy < height)
            {
                return FALSE;
            }

            if (psoMask->sizlBitmap.cy != height)
            {
                if (height > MAXLONG / 2 ||
                    psoMask->sizlBitmap.cy != height * 2)
                {
                    return FALSE;
                }
            }
        }
    }
    else
    {
        if (!CddPointerSurfaceIsSane(psoMask, TRUE) ||
            (psoMask->sizlBitmap.cy & 1))
        {
            return FALSE;
        }

        width = psoMask->sizlBitmap.cx;
        height = psoMask->sizlBitmap.cy / 2;
    }

    if (width <= 0 || height <= 0 ||
        xHot < 0 || yHot < 0 ||
        xHot >= width || yHot >= height)
    {
        return FALSE;
    }

    *Width = width;
    *Height = height;
    return TRUE;
}

static BOOLEAN
CddBuildCursorRect(
    _In_ PCDD_PDEV ppdev,
    _In_ LONG x,
    _In_ LONG y,
    _Out_ PRECTL Rect)
{
    RECTL rawRect;
    LONGLONG left, top, right, bottom;

    if (!ppdev || x == -1 || ppdev->CursorWidth <= 0 || ppdev->CursorHeight <= 0)
        return FALSE;

    left = (LONGLONG)x - ppdev->CursorHotX;
    top = (LONGLONG)y - ppdev->CursorHotY;
    right = left + ppdev->CursorWidth;
    bottom = top + ppdev->CursorHeight;

    if (left < -((LONGLONG)MAXLONG) - 1 ||
        top < -((LONGLONG)MAXLONG) - 1 ||
        right > (LONGLONG)MAXLONG ||
        bottom > (LONGLONG)MAXLONG)
    {
        return FALSE;
    }

    rawRect.left = (LONG)left;
    rawRect.top = (LONG)top;
    rawRect.right = (LONG)right;
    rawRect.bottom = (LONG)bottom;

    return CddNormalizeAndClipRect(ppdev, &rawRect, Rect);
}

/*
 * CddShadowFlushNormalizedRect
 *
 * Copy a normalized, screen-clipped rectangle from shadow to VRAM.
 */
static VOID
CddShadowFlushNormalizedRect(
    _In_ PCDD_PDEV ppdev,
    _In_ const RECTL *Rect)
{
    LONG y;
    LONG bytesPerPixel = (ppdev->BitsPerPixel + 7) / 8;
    LONG height = Rect->bottom - Rect->top;
    SIZE_T rowBytes = (SIZE_T)(Rect->right - Rect->left) * bytesPerPixel;
    PUCHAR shadowRow = (PUCHAR)ppdev->ShadowBuffer +
                       (SIZE_T)Rect->top * ppdev->ScreenDelta +
                       (SIZE_T)Rect->left * bytesPerPixel;
    PUCHAR vramRow = (PUCHAR)ppdev->VramPtr +
                     (SIZE_T)Rect->top * ppdev->ScreenDelta +
                     (SIZE_T)Rect->left * bytesPerPixel;

    for (y = 0; y < height; y++)
    {
        RtlCopyMemory(vramRow, shadowRow, rowBytes);
        shadowRow += ppdev->ScreenDelta;
        vramRow += ppdev->ScreenDelta;
    }
}

/*
 * CddShadowFlushRect
 *
 * Copy a dirty rectangle from the shadow buffer to VRAM.
 * Clips to screen bounds to avoid overflows.
 */
VOID
CddShadowFlushRect(
    _In_ PCDD_PDEV ppdev,
    _In_ const RECTL *prcl)
{
    RECTL rcl;

    if (!ppdev->ShadowBuffer || !prcl)
        return;

    if (ppdev->D3DKMTConnected)
    {
        CddPresentDirtyRect(ppdev, prcl);
        return;
    }

    if (ppdev->SysmemFramebuffer)
    {
        CddPresentDirtyRect(ppdev, prcl);
        return;
    }

    if (!ppdev->VramPtr)
        return;

    if (!CddNormalizeAndClipRect(ppdev, prcl, &rcl))
        return;

    CddShadowFlushNormalizedRect(ppdev, &rcl);
}

/*
 * CddShadowFlushRects
 *
 * Flush one or two dirty rectangles, coalescing when beneficial.
 */
VOID
CddShadowFlushRects(
    _In_ PCDD_PDEV ppdev,
    _In_opt_ const RECTL *prcl1,
    _In_opt_ const RECTL *prcl2)
{
    RECTL rcl1, rcl2, rclUnion;
    BOOLEAN valid1, valid2;

    if (!ppdev->ShadowBuffer)
        return;

    valid1 = CddNormalizeAndClipRect(ppdev, prcl1, &rcl1);
    valid2 = CddNormalizeAndClipRect(ppdev, prcl2, &rcl2);

    if (!valid1 && !valid2)
        return;

    if (valid1 && valid2)
    {
        rclUnion.left = min(rcl1.left, rcl2.left);
        rclUnion.top = min(rcl1.top, rcl2.top);
        rclUnion.right = max(rcl1.right, rcl2.right);
        rclUnion.bottom = max(rcl1.bottom, rcl2.bottom);
    }

    if (ppdev->D3DKMTConnected)
    {
        if (valid1 && valid2)
            CddPresentDirtyRect(ppdev, &rclUnion);
        else
            CddPresentDirtyRect(ppdev, valid1 ? &rcl1 : &rcl2);
        return;
    }

    if (ppdev->SysmemFramebuffer)
    {
        if (valid1 && valid2)
            CddPresentDirtyRect(ppdev, &rclUnion);
        else
            CddPresentDirtyRect(ppdev, valid1 ? &rcl1 : &rcl2);
        return;
    }

    if (!ppdev->VramPtr)
        return;

    if (valid1 && !valid2)
    {
        CddShadowFlushNormalizedRect(ppdev, &rcl1);
        return;
    }
    if (!valid1 && valid2)
    {
        CddShadowFlushNormalizedRect(ppdev, &rcl2);
        return;
    }

    /* Overlapping or touching: single flush */
    if (!((rcl1.right < rcl2.left) ||
          (rcl2.right < rcl1.left) ||
          (rcl1.bottom < rcl2.top) ||
          (rcl2.bottom < rcl1.top)))
    {
        CddShadowFlushNormalizedRect(ppdev, &rclUnion);
        return;
    }

    /* Coalesce nearby disjoint rects when wasted area <= 50% */
    {
        LONGLONG unionArea = (LONGLONG)(rclUnion.right - rclUnion.left) *
                             (rclUnion.bottom - rclUnion.top);
        LONGLONG area1 = (LONGLONG)(rcl1.right - rcl1.left) *
                         (rcl1.bottom - rcl1.top);
        LONGLONG area2 = (LONGLONG)(rcl2.right - rcl2.left) *
                         (rcl2.bottom - rcl2.top);
        LONGLONG sumArea = area1 + area2;

        if (unionArea <= sumArea + sumArea / 2)
        {
            CddShadowFlushNormalizedRect(ppdev, &rclUnion);
            return;
        }
    }

    CddShadowFlushNormalizedRect(ppdev, &rcl1);
    CddShadowFlushNormalizedRect(ppdev, &rcl2);
}


/* ========================================================================
 * Surface substitution helpers
 * ======================================================================== */

/*
 * CddPuntSurface
 *
 * If the given SURFOBJ is our STYPE_DEVICE surface, return the shadow
 * bitmap SURFOBJ instead.  Eng* functions need a STYPE_BITMAP surface
 * with pvScan0 to operate on.
 */
FORCEINLINE SURFOBJ *
CddPuntSurface(_In_ PCDD_PDEV ppdev, _In_opt_ SURFOBJ *pso)
{
    if (pso && pso->dhpdev == (DHPDEV)ppdev && pso->iType == STYPE_DEVICE)
        return ppdev->psoShadow;
    return pso;
}

/*
 * CddShadowPpdevFromSurfaces
 *
 * Extract ppdev from whichever surface is our STYPE_DEVICE surface in
 * shadow mode.  Returns NULL if neither surface belongs to us.
 */
FORCEINLINE PCDD_PDEV
CddShadowPpdevFromSurfaces(_In_opt_ SURFOBJ *pso1, _In_opt_ SURFOBJ *pso2)
{
    if (pso1 && pso1->iType == STYPE_DEVICE && pso1->dhpdev)
    {
        PCDD_PDEV ppdev = (PCDD_PDEV)pso1->dhpdev;
        if (ppdev->ShadowBuffer && ppdev->psoShadow)
            return ppdev;
    }
    if (pso2 && pso2->iType == STYPE_DEVICE && pso2->dhpdev)
    {
        PCDD_PDEV ppdev = (PCDD_PDEV)pso2->dhpdev;
        if (ppdev->ShadowBuffer && ppdev->psoShadow)
            return ppdev;
    }
    return NULL;
}

/*
 * CddIntersectRects
 *
 * Intersect two rectangles.  Returns TRUE if the result is non-empty.
 */
static BOOLEAN
CddIntersectRects(
    _Out_ PRECTL Target,
    _In_ const RECTL *A,
    _In_ const RECTL *B)
{
    Target->left = max(A->left, B->left);
    Target->top = max(A->top, B->top);
    Target->right = min(A->right, B->right);
    Target->bottom = min(A->bottom, B->bottom);
    return (Target->left < Target->right) && (Target->top < Target->bottom);
}

static VOID
CddShadowFlushClippedRect(
    _In_ PCDD_PDEV ppdev,
    _In_opt_ const RECTL *Rect,
    _In_opt_ const CLIPOBJ *Clip)
{
    RECTL clippedRect;

    if (!Rect)
        return;

    if (Clip && Clip->iDComplexity != DC_TRIVIAL)
    {
        if (!CddIntersectRects(&clippedRect, Rect, &Clip->rclBounds))
            return;

        CddShadowFlushRect(ppdev, &clippedRect);
        return;
    }

    CddShadowFlushRect(ppdev, Rect);
}

/*
 * CddClipStretchRectsToScreen
 *
 * Clip a stretch/blend destination rectangle to the screen and adjust the
 * source rectangle proportionally so Eng* never sees an out-of-bounds target
 * on the shadow bitmap.
 */
static BOOLEAN
CddClipStretchRectsToScreen(
    _In_ PCDD_PDEV ppdev,
    _In_ const RECTL *DestRect,
    _In_ const RECTL *SrcRect,
    _Out_ PRECTL ClippedDestRect,
    _Out_ PRECTL AdjustedSrcRect)
{
    RECTL dst, src, clippedDst;
    LONG dstWidth, dstHeight;
    LONGLONG srcWidth, srcHeight;

    if (!CddNormalizeAndClipRect(ppdev, DestRect, &clippedDst) ||
        !CddNormalizeRect(SrcRect, &src))
    {
        return FALSE;
    }

    dst = *DestRect;
    if (dst.left > dst.right)
    {
        LONG t = dst.left;
        dst.left = dst.right;
        dst.right = t;
    }
    if (dst.top > dst.bottom)
    {
        LONG t = dst.top;
        dst.top = dst.bottom;
        dst.bottom = t;
    }

    dstWidth = dst.right - dst.left;
    dstHeight = dst.bottom - dst.top;
    if (dstWidth <= 0 || dstHeight <= 0)
        return FALSE;

    srcWidth = (LONGLONG)src.right - src.left;
    srcHeight = (LONGLONG)src.bottom - src.top;
    if (srcWidth <= 0 || srcHeight <= 0)
        return FALSE;

    *ClippedDestRect = clippedDst;

    AdjustedSrcRect->left = src.left +
        (LONG)(((LONGLONG)(clippedDst.left - dst.left) * srcWidth) / dstWidth);
    AdjustedSrcRect->top = src.top +
        (LONG)(((LONGLONG)(clippedDst.top - dst.top) * srcHeight) / dstHeight);
    AdjustedSrcRect->right = src.left +
        (LONG)((((LONGLONG)(clippedDst.right - dst.left) * srcWidth) + dstWidth - 1) / dstWidth);
    AdjustedSrcRect->bottom = src.top +
        (LONG)((((LONGLONG)(clippedDst.bottom - dst.top) * srcHeight) + dstHeight - 1) / dstHeight);

    return (AdjustedSrcRect->left < AdjustedSrcRect->right) &&
           (AdjustedSrcRect->top < AdjustedSrcRect->bottom);
}


/* ========================================================================
 * Cursor hooks
 * ======================================================================== */

ULONG APIENTRY
DrvSetPointerShape(
    IN SURFOBJ *pso,
    IN SURFOBJ *psoMask,
    IN SURFOBJ *psoColor,
    IN XLATEOBJ *pxlo,
    IN LONG xHot,
    IN LONG yHot,
    IN LONG x,
    IN LONG y,
    IN RECTL *prcl,
    IN FLONG fl)
{
    PCDD_PDEV ppdev = pso ? (PCDD_PDEV)pso->dhpdev : NULL;
    ULONG ret;
    LONG cursorWidth = 0;
    LONG cursorHeight = 0;
    RECTL oldRect;
    RECTL newRect;
    BOOLEAN haveOld = FALSE;
    BOOLEAN haveNew = FALSE;
    BOOLEAN hidePointer;

    if (!CddValidatePointerShape(pso, psoMask, psoColor, xHot, yHot, fl,
                                 &cursorWidth, &cursorHeight))
    {
        return SPS_ERROR;
    }

    if (ppdev && ppdev->ShadowBuffer)
        haveOld = CddGetStoredCursorRect(ppdev, &oldRect);

    hidePointer = (!psoMask && !psoColor) || (x == -1);

    if (ppdev && !hidePointer)
    {
        ppdev->CursorWidth = cursorWidth;
        ppdev->CursorHeight = cursorHeight;
        ppdev->CursorHotX = xHot;
        ppdev->CursorHotY = yHot;
    }

    /* Pass original device surface for engine cursor handling */
    ret = EngSetPointerShape(pso, psoMask, psoColor, pxlo,
                             xHot, yHot, x, y, prcl, fl);

    if (ret == SPS_ERROR || !ppdev || !ppdev->ShadowBuffer)
        return ret;

    if (!hidePointer)
    {
        haveNew = CddNormalizeAndClipRect(ppdev, prcl, &newRect) ||
                  CddBuildCursorRect(ppdev, x, y, &newRect);
    }

    if (haveOld || haveNew)
        CddShadowFlushRects(ppdev, haveOld ? &oldRect : NULL, haveNew ? &newRect : NULL);

    CddSetStoredCursorRect(ppdev, haveNew ? &newRect : NULL);

    return ret;
}

VOID APIENTRY
DrvMovePointer(
    IN SURFOBJ *pso,
    IN LONG x,
    IN LONG y,
    IN RECTL *prcl)
{
    PCDD_PDEV ppdev = pso ? (PCDD_PDEV)pso->dhpdev : NULL;
    RECTL oldRect;
    RECTL newRect;
    BOOLEAN haveOld = FALSE;
    BOOLEAN haveNew = FALSE;

    if (ppdev && ppdev->ShadowBuffer)
        haveOld = CddGetStoredCursorRect(ppdev, &oldRect);

    EngMovePointer(pso, x, y, prcl);

    if (ppdev && ppdev->ShadowBuffer)
    {
        if (x == -1)
        {
            /* Cursor hidden */
            if (haveOld)
                CddShadowFlushRect(ppdev, &oldRect);

            CddClearCursorRect(ppdev);
        }
        else
        {
            haveNew = CddNormalizeAndClipRect(ppdev, prcl, &newRect) ||
                      CddBuildCursorRect(ppdev, x, y, &newRect);

            if (haveOld || haveNew)
            {
                CddShadowFlushRects(ppdev,
                                     haveOld ? &oldRect : NULL,
                                     haveNew ? &newRect : NULL);
            }

            CddSetStoredCursorRect(ppdev, haveNew ? &newRect : NULL);
        }
    }
}


/* ========================================================================
 * Drawing DDI hooks
 *
 * Each hook follows the pattern:
 *   - Detect if either surface is our device surface
 *   - Punt to Eng* on the shadow bitmap
 *   - Flush the dirty area
 * ======================================================================== */

BOOL APIENTRY
DrvCopyBits(
    SURFOBJ *psoDst,
    SURFOBJ *psoSrc,
    CLIPOBJ *pco,
    XLATEOBJ *pxlo,
    RECTL *prclDst,
    POINTL *pptlSrc)
{
    PCDD_PDEV ppdev;

    ppdev = CddShadowPpdevFromSurfaces(psoDst, psoSrc);
    if (ppdev)
    {
        BOOLEAN dstIsOurs = (psoDst->iType == STYPE_DEVICE &&
                             psoDst->dhpdev == (DHPDEV)ppdev);
        BOOL result = EngBitBlt(CddPuntSurface(ppdev, psoDst),
                                CddPuntSurface(ppdev, psoSrc),
                                NULL, pco, pxlo,
                                prclDst, pptlSrc, NULL, NULL, NULL,
                                ROP3_TO_ROP4(SRCCOPY));
        if (result && dstIsOurs && prclDst)
            CddShadowFlushClippedRect(ppdev, prclDst, pco);
        return result;
    }

    return EngCopyBits(psoDst, psoSrc, pco, pxlo, prclDst, pptlSrc);
}

BOOL APIENTRY
DrvBitBlt(
    SURFOBJ *psoDst,
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
    PCDD_PDEV ppdev;

    ppdev = CddShadowPpdevFromSurfaces(psoDst, psoSrc);
    if (ppdev)
    {
        BOOLEAN dstIsOurs = (psoDst->iType == STYPE_DEVICE &&
                             psoDst->dhpdev == (DHPDEV)ppdev);
        BOOL result = EngBitBlt(CddPuntSurface(ppdev, psoDst),
                                CddPuntSurface(ppdev, psoSrc),
                                psoMask, pco, pxlo,
                                prclDst, pptlSrc, pptlMask,
                                pbo, pptlBrush, rop4);
        if (result && dstIsOurs && prclDst)
            CddShadowFlushClippedRect(ppdev, prclDst, pco);
        return result;
    }

    return EngBitBlt(psoDst, psoSrc, psoMask, pco, pxlo,
                     prclDst, pptlSrc, pptlMask,
                     pbo, pptlBrush, rop4);
}

BOOL APIENTRY
DrvTextOut(
    SURFOBJ *pso,
    STROBJ *pstro,
    FONTOBJ *pfo,
    CLIPOBJ *pco,
    RECTL *prclExtra,
    RECTL *prclOpaque,
    BRUSHOBJ *pboFore,
    BRUSHOBJ *pboOpaque,
    POINTL *pptlOrg,
    MIX mix)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)pso->dhpdev;
    BOOL result;

    if (ppdev && ppdev->ShadowBuffer && pso->iType == STYPE_DEVICE)
    {
        result = EngTextOut(ppdev->psoShadow, pstro, pfo, pco, prclExtra,
                            prclOpaque, pboFore, pboOpaque, pptlOrg, mix);
        if (result)
        {
            if (pco && pco->iDComplexity != DC_TRIVIAL)
            {
                CddShadowFlushClippedRect(ppdev,
                                          pstro ? &pstro->rclBkGround : NULL,
                                          pco);
                CddShadowFlushClippedRect(ppdev, prclOpaque, pco);
            }
            else
            {
                CddShadowFlushRects(ppdev,
                                    pstro ? &pstro->rclBkGround : NULL,
                                    prclOpaque);
            }
        }
        return result;
    }

    return EngTextOut(pso, pstro, pfo, pco, prclExtra, prclOpaque,
                      pboFore, pboOpaque, pptlOrg, mix);
}

BOOL APIENTRY
DrvLineTo(
    SURFOBJ *pso,
    CLIPOBJ *pco,
    BRUSHOBJ *pbo,
    LONG x1,
    LONG y1,
    LONG x2,
    LONG y2,
    RECTL *prclBounds,
    MIX mix)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)pso->dhpdev;
    BOOL result;

    if (ppdev && ppdev->ShadowBuffer && pso->iType == STYPE_DEVICE)
    {
        result = EngLineTo(ppdev->psoShadow, pco, pbo,
                           x1, y1, x2, y2, prclBounds, mix);
        if (result && prclBounds)
            CddShadowFlushClippedRect(ppdev, prclBounds, pco);
        return result;
    }

    return EngLineTo(pso, pco, pbo, x1, y1, x2, y2, prclBounds, mix);
}

BOOL APIENTRY
DrvStrokePath(
    SURFOBJ *pso,
    PATHOBJ *ppo,
    CLIPOBJ *pco,
    XFORMOBJ *pxo,
    BRUSHOBJ *pbo,
    POINTL *pptlBrushOrg,
    LINEATTRS *plineattrs,
    MIX mix)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)pso->dhpdev;
    BOOL result;

    if (ppdev && ppdev->ShadowBuffer && pso->iType == STYPE_DEVICE)
    {
        result = EngStrokePath(ppdev->psoShadow, ppo, pco, pxo, pbo,
                               pptlBrushOrg, plineattrs, mix);
        if (result)
        {
            /* Keep path draws correct even while PATHOBJ_vGetBounds import resolution is unstable. */
            CddShadowFlushPathResult(ppdev, pco);
        }
        return result;
    }

    return EngStrokePath(pso, ppo, pco, pxo, pbo, pptlBrushOrg,
                         plineattrs, mix);
}

BOOL APIENTRY
DrvFillPath(
    SURFOBJ *pso,
    PATHOBJ *ppo,
    CLIPOBJ *pco,
    BRUSHOBJ *pbo,
    POINTL *pptlBrushOrg,
    MIX mix,
    FLONG flOptions)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)pso->dhpdev;
    BOOL result;

    if (ppdev && ppdev->ShadowBuffer && pso->iType == STYPE_DEVICE)
    {
        result = EngFillPath(ppdev->psoShadow, ppo, pco, pbo, pptlBrushOrg,
                             mix, flOptions);
        if (result)
        {
            CddShadowFlushPathResult(ppdev, pco);
        }
        return result;
    }

    return EngFillPath(pso, ppo, pco, pbo, pptlBrushOrg, mix, flOptions);
}

BOOL APIENTRY
DrvStrokeAndFillPath(
    SURFOBJ *pso,
    PATHOBJ *ppo,
    CLIPOBJ *pco,
    XFORMOBJ *pxo,
    BRUSHOBJ *pboStroke,
    LINEATTRS *plineattrs,
    BRUSHOBJ *pboFill,
    POINTL *pptlBrushOrg,
    MIX mixFill,
    FLONG flOptions)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)pso->dhpdev;
    BOOL result;

    if (ppdev && ppdev->ShadowBuffer && pso->iType == STYPE_DEVICE)
    {
        result = EngStrokeAndFillPath(ppdev->psoShadow, ppo, pco, pxo,
                                      pboStroke, plineattrs, pboFill,
                                      pptlBrushOrg, mixFill, flOptions);
        if (result)
        {
            CddShadowFlushPathResult(ppdev, pco);
        }
        return result;
    }

    return EngStrokeAndFillPath(pso, ppo, pco, pxo, pboStroke, plineattrs,
                                pboFill, pptlBrushOrg, mixFill, flOptions);
}

BOOL APIENTRY
DrvPaint(
    SURFOBJ *pso,
    CLIPOBJ *pco,
    BRUSHOBJ *pbo,
    POINTL *pptlBrushOrg,
    MIX mix)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)pso->dhpdev;
    BOOL result;

    if (ppdev && ppdev->ShadowBuffer && pso->iType == STYPE_DEVICE)
    {
        result = EngPaint(ppdev->psoShadow, pco, pbo, pptlBrushOrg, mix);
        if (result)
        {
            if (pco)
                CddShadowFlushClippedRect(ppdev, &pco->rclBounds, pco);
            else
            {
                RECTL rclFull = { 0, 0, ppdev->ScreenWidth, ppdev->ScreenHeight };
                CddShadowFlushRect(ppdev, &rclFull);
            }
        }
        return result;
    }

    return EngPaint(pso, pco, pbo, pptlBrushOrg, mix);
}

BOOL APIENTRY
DrvStretchBlt(
    SURFOBJ *psoDest,
    SURFOBJ *psoSrc,
    SURFOBJ *psoMask,
    CLIPOBJ *pco,
    XLATEOBJ *pxlo,
    COLORADJUSTMENT *pca,
    POINTL *pptlHTOrg,
    RECTL *prclDest,
    RECTL *prclSrc,
    POINTL *pptlMask,
    ULONG iMode)
{
    PCDD_PDEV ppdev = CddShadowPpdevFromSurfaces(psoDest, psoSrc);
    BOOL result;
    RECTL clippedDest;
    RECTL adjustedSrc;
    PRECTL pDstCall = prclDest;
    PRECTL pSrcCall = prclSrc;

    if (ppdev)
    {
        BOOLEAN dstIsOurs = (psoDest->iType == STYPE_DEVICE &&
                             psoDest->dhpdev == (DHPDEV)ppdev);

        if (dstIsOurs && prclDest && prclSrc)
        {
            if (!CddClipStretchRectsToScreen(ppdev,
                                             prclDest,
                                             prclSrc,
                                             &clippedDest,
                                             &adjustedSrc))
            {
                return TRUE;
            }

            pDstCall = &clippedDest;
            pSrcCall = &adjustedSrc;
        }

        result = EngStretchBlt(CddPuntSurface(ppdev, psoDest),
                               CddPuntSurface(ppdev, psoSrc),
                               psoMask, pco, pxlo, pca,
                               pptlHTOrg, pDstCall, pSrcCall, pptlMask, iMode);
        if (result && dstIsOurs && pDstCall)
            CddShadowFlushClippedRect(ppdev, pDstCall, pco);
        return result;
    }

    return EngStretchBlt(psoDest, psoSrc, psoMask, pco, pxlo, pca,
                         pptlHTOrg, prclDest, prclSrc, pptlMask, iMode);
}

BOOL APIENTRY
DrvStretchBltROP(
    SURFOBJ *psoDest,
    SURFOBJ *psoSrc,
    SURFOBJ *psoMask,
    CLIPOBJ *pco,
    XLATEOBJ *pxlo,
    COLORADJUSTMENT *pca,
    POINTL *pptlHTOrg,
    RECTL *prclDest,
    RECTL *prclSrc,
    POINTL *pptlMask,
    ULONG iMode,
    BRUSHOBJ *pbo,
    ROP4 rop4)
{
    PCDD_PDEV ppdev = CddShadowPpdevFromSurfaces(psoDest, psoSrc);
    BOOL result;
    RECTL clippedDest;
    RECTL adjustedSrc;
    PRECTL pDstCall = prclDest;
    PRECTL pSrcCall = prclSrc;

    if (ppdev)
    {
        BOOLEAN dstIsOurs = (psoDest->iType == STYPE_DEVICE &&
                             psoDest->dhpdev == (DHPDEV)ppdev);

        if (dstIsOurs && prclDest && prclSrc)
        {
            if (!CddClipStretchRectsToScreen(ppdev,
                                             prclDest,
                                             prclSrc,
                                             &clippedDest,
                                             &adjustedSrc))
            {
                return TRUE;
            }

            pDstCall = &clippedDest;
            pSrcCall = &adjustedSrc;
        }

        result = EngStretchBltROP(CddPuntSurface(ppdev, psoDest),
                                  CddPuntSurface(ppdev, psoSrc),
                                  psoMask, pco, pxlo, pca,
                                  pptlHTOrg, pDstCall, pSrcCall, pptlMask,
                                  iMode, pbo, rop4);
        if (result && dstIsOurs && pDstCall)
            CddShadowFlushClippedRect(ppdev, pDstCall, pco);
        return result;
    }

    return EngStretchBltROP(psoDest, psoSrc, psoMask, pco, pxlo, pca,
                            pptlHTOrg, prclDest, prclSrc, pptlMask,
                            iMode, pbo, rop4);
}

BOOL APIENTRY
DrvAlphaBlend(
    SURFOBJ *psoDest,
    SURFOBJ *psoSrc,
    CLIPOBJ *pco,
    XLATEOBJ *pxlo,
    RECTL *prclDest,
    RECTL *prclSrc,
    BLENDOBJ *pBlendObj)
{
    PCDD_PDEV ppdev = CddShadowPpdevFromSurfaces(psoDest, psoSrc);
    BOOL result;

    if (ppdev)
    {
        BOOLEAN dstIsOurs = (psoDest->iType == STYPE_DEVICE &&
                             psoDest->dhpdev == (DHPDEV)ppdev);
        result = EngAlphaBlend(CddPuntSurface(ppdev, psoDest),
                               CddPuntSurface(ppdev, psoSrc),
                               pco, pxlo, prclDest, prclSrc, pBlendObj);
        if (result && dstIsOurs && prclDest)
            CddShadowFlushClippedRect(ppdev, prclDest, pco);
        return result;
    }

    return EngAlphaBlend(psoDest, psoSrc, pco, pxlo,
                         prclDest, prclSrc, pBlendObj);
}

BOOL APIENTRY
DrvTransparentBlt(
    SURFOBJ *psoDst,
    SURFOBJ *psoSrc,
    CLIPOBJ *pco,
    XLATEOBJ *pxlo,
    RECTL *prclDst,
    RECTL *prclSrc,
    ULONG iTransColor,
    ULONG ulReserved)
{
    PCDD_PDEV ppdev = CddShadowPpdevFromSurfaces(psoDst, psoSrc);
    BOOL result;

    if (ppdev)
    {
        BOOLEAN dstIsOurs = (psoDst->iType == STYPE_DEVICE &&
                             psoDst->dhpdev == (DHPDEV)ppdev);
        result = EngTransparentBlt(CddPuntSurface(ppdev, psoDst),
                                   CddPuntSurface(ppdev, psoSrc),
                                   pco, pxlo, prclDst, prclSrc,
                                   iTransColor, ulReserved);
        if (result && dstIsOurs && prclDst)
            CddShadowFlushClippedRect(ppdev, prclDst, pco);
        return result;
    }

    return EngTransparentBlt(psoDst, psoSrc, pco, pxlo,
                             prclDst, prclSrc, iTransColor, ulReserved);
}

BOOL APIENTRY
DrvGradientFill(
    SURFOBJ *pso,
    CLIPOBJ *pco,
    XLATEOBJ *pxlo,
    TRIVERTEX *pVertex,
    ULONG nVertex,
    PVOID pMesh,
    ULONG nMesh,
    RECTL *prclExtents,
    POINTL *pptlDitherOrg,
    ULONG ulMode)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)pso->dhpdev;
    BOOL result;

    if (ppdev && ppdev->ShadowBuffer && pso->iType == STYPE_DEVICE)
    {
        result = EngGradientFill(ppdev->psoShadow, pco, pxlo, pVertex,
                                 nVertex, pMesh, nMesh, prclExtents,
                                 pptlDitherOrg, ulMode);
        if (result && prclExtents)
            CddShadowFlushClippedRect(ppdev, prclExtents, pco);
        return result;
    }

    return EngGradientFill(pso, pco, pxlo, pVertex, nVertex,
                           pMesh, nMesh, prclExtents, pptlDitherOrg, ulMode);
}

BOOL APIENTRY
DrvPlgBlt(
    SURFOBJ *psoTrg,
    SURFOBJ *psoSrc,
    SURFOBJ *psoMsk,
    CLIPOBJ *pco,
    XLATEOBJ *pxlo,
    COLORADJUSTMENT *pca,
    POINTL *pptlBrushOrg,
    POINTFIX *pptfx,
    RECTL *prclSrc,
    POINTL *pptlMask,
    ULONG iMode)
{
    PCDD_PDEV ppdev = CddShadowPpdevFromSurfaces(psoTrg, psoSrc);
    BOOL result;

    if (ppdev)
    {
        BOOLEAN dstIsOurs = (psoTrg->iType == STYPE_DEVICE &&
                             psoTrg->dhpdev == (DHPDEV)ppdev);
        result = EngPlgBlt(CddPuntSurface(ppdev, psoTrg),
                           CddPuntSurface(ppdev, psoSrc),
                           psoMsk, pco, pxlo, pca,
                           pptlBrushOrg, pptfx, prclSrc, pptlMask, iMode);
        if (result && dstIsOurs && pptfx)
        {
            /* Derive 4th parallelogram vertex: D = B + C - A */
            FIX fx4x = pptfx[1].x + pptfx[2].x - pptfx[0].x;
            FIX fx4y = pptfx[1].y + pptfx[2].y - pptfx[0].y;
            FIX fxLeft   = min(min(pptfx[0].x, pptfx[1].x), min(pptfx[2].x, fx4x));
            FIX fxTop    = min(min(pptfx[0].y, pptfx[1].y), min(pptfx[2].y, fx4y));
            FIX fxRight  = max(max(pptfx[0].x, pptfx[1].x), max(pptfx[2].x, fx4x));
            FIX fxBottom = max(max(pptfx[0].y, pptfx[1].y), max(pptfx[2].y, fx4y));
            RECTL rclPlg;
            rclPlg.left   = fxLeft >> 4;
            rclPlg.top    = fxTop >> 4;
            rclPlg.right  = (fxRight + 15) >> 4;
            rclPlg.bottom = (fxBottom + 15) >> 4;
            CddShadowFlushClippedRect(ppdev, &rclPlg, pco);
        }
        return result;
    }

    return EngPlgBlt(psoTrg, psoSrc, psoMsk, pco, pxlo, pca,
                     pptlBrushOrg, pptfx, prclSrc, pptlMask, iMode);
}
