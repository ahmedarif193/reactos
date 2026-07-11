/*
 * PROJECT:     ReactOS Canonical Display Driver (cdd.dll)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Present seam + dirty-rectangle tracking + GDI draw delegation.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * cdd implements NO raster ops of its own. GDI's engine paints into the cached
 * shadow surface; the BitBlt/CopyBits/SynchronizeSurface hooks below exist only
 * to learn which rectangles changed, then call RcddPresent to push those
 * rectangles to the WDDM scan-out. RcddPresent is the single seam an Option-B
 * upgrade (D3DKMTPresent / SetVidPnSourceAddress) replaces; see surface.c.
 */

#include "cdd.h"

/*
 * RcddPresent
 *
 * Present the rectangle prcl (or the whole screen when prcl is NULL) from the
 * shadow to the mapped scan-out. Spans are rounded out to whole 64-byte lines
 * so the copy into the write-combined scan-out runs as co-aligned full-line
 * bursts.
 */
VOID
RcddPresent(
   PRCDD_PDEV ppdev,
   const RECTL *prcl)
{
   RECTL Dirty;
   ULONG Ret;

   /* GDI's engine drew straight into the mapped DOD primary (ppdev->ScreenPtr),
    * so there is nothing to copy — we only tell dxgkrnl which rectangle changed.
    * dxgkrnl scans it out through the miniport's DxgkDdiPresentDisplayOnly (the
    * WDDM display-only present path), driven by cdd rather than the fallback
    * present timer. */
   if (ppdev->ScreenPtr == NULL)
      return;

   Dirty.left   = 0;
   Dirty.top    = 0;
   Dirty.right  = ppdev->ScreenWidth;
   Dirty.bottom = ppdev->ScreenHeight;

   if (prcl != NULL)
   {
      Dirty.left   = max(Dirty.left, prcl->left);
      Dirty.top    = max(Dirty.top, prcl->top);
      Dirty.right  = min(Dirty.right, prcl->right);
      Dirty.bottom = min(Dirty.bottom, prcl->bottom);
   }

   if (Dirty.left >= Dirty.right || Dirty.top >= Dirty.bottom)
      return;

   EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT,
                      &Dirty, sizeof(Dirty), NULL, 0, &Ret);
}

/* Present the target rectangle, narrowed by the clip bounding box if any. */
static VOID
RcddPresentTarget(
   SURFOBJ *psoTrg,
   const RECTL *prclTrg,
   CLIPOBJ *pco)
{
   PRCDD_PDEV ppdev;
   RECTL rcl;

   if (psoTrg == NULL || psoTrg->dhpdev == NULL)
      return;

   ppdev = (PRCDD_PDEV)psoTrg->dhpdev;
   if (psoTrg->pvScan0 != ppdev->ScreenPtr)
      return;

   if (prclTrg == NULL)
   {
      /* No target bounds — fall back to the clip bounds; failing that,
       * present the whole screen. */
      if (pco != NULL && pco->iDComplexity != DC_TRIVIAL)
      {
         prclTrg = &pco->rclBounds;
      }
      else
      {
         RcddPresent(ppdev, NULL);
         return;
      }
   }

   rcl = *prclTrg;
   if (rcl.right < rcl.left)
   {
      rcl.left = prclTrg->right;
      rcl.right = prclTrg->left;
   }
   if (rcl.bottom < rcl.top)
   {
      rcl.top = prclTrg->bottom;
      rcl.bottom = prclTrg->top;
   }

   if (pco != NULL && pco->iDComplexity != DC_TRIVIAL)
   {
      rcl.left = max(rcl.left, pco->rclBounds.left);
      rcl.top = max(rcl.top, pco->rclBounds.top);
      rcl.right = min(rcl.right, pco->rclBounds.right);
      rcl.bottom = min(rcl.bottom, pco->rclBounds.bottom);
   }

   RcddPresent(ppdev, &rcl);
}

/*
 * RcddBitBlt
 *
 * Punt the blit to the GDI engine, then present the touched rectangle.
 */
BOOL APIENTRY
RcddBitBlt(
   IN SURFOBJ *psoTrg,
   IN SURFOBJ *psoSrc,
   IN SURFOBJ *psoMask,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclTrg,
   IN POINTL *pptlSrc,
   IN POINTL *pptlMask,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrush,
   IN ROP4 rop4)
{
   BOOL Result;

   Result = EngBitBlt(psoTrg, psoSrc, psoMask, pco, pxlo, prclTrg, pptlSrc, pptlMask, pbo, pptlBrush, rop4);
   if (Result)
      RcddPresentTarget(psoTrg, prclTrg, pco);

   return Result;
}

/*
 * RcddCopyBits
 *
 * Punt the copy to the GDI engine, then present the touched rectangle.
 */
BOOL APIENTRY
RcddCopyBits(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclDest,
   IN POINTL *pptlSrc)
{
   BOOL Result;

   Result = EngCopyBits(psoDest, psoSrc, pco, pxlo, prclDest, pptlSrc);
   if (Result)
      RcddPresentTarget(psoDest, prclDest, pco);

   return Result;
}

/*
 * RcddSynchronizeSurface
 *
 * GDI signals direct engine writes to the shadow with DSS_FLUSH_EVENT; present
 * the affected rectangle.
 */
VOID APIENTRY
RcddSynchronizeSurface(
   IN SURFOBJ *pso,
   IN RECTL *prcl,
   IN FLONG fl)
{
   if (!(fl & DSS_FLUSH_EVENT) || pso == NULL || pso->dhpdev == NULL)
      return;

   if (pso->pvScan0 != ((PRCDD_PDEV)pso->dhpdev)->ScreenPtr)
      return;

   RcddPresent((PRCDD_PDEV)pso->dhpdev, prcl);
}

/* Bounding box of a path in pixels (PATHOBJ bounds are 28.4 fixed point);
 * padded a pixel for pen width rounding. */
static VOID
RcddPathBounds(
   PATHOBJ *ppo,
   RECTL *prcl)
{
   RECTFX rcfx;

   PATHOBJ_vGetBounds(ppo, &rcfx);
   prcl->left   = (rcfx.xLeft >> 4) - 1;
   prcl->top    = (rcfx.yTop >> 4) - 1;
   prcl->right  = ((rcfx.xRight + 15) >> 4) + 1;
   prcl->bottom = ((rcfx.yBottom + 15) >> 4) + 1;
}

/*
 * The remaining draw DDIs. cdd implements no raster ops: every hook punts to
 * the GDI engine and then presents the touched rectangle. They are hooked
 * ONLY so no drawing primitive can reach the primary without an explicit
 * dirty-rect present — otherwise text/line/path/gradient output would sit
 * unpresented until the fallback timer, which is suppressed for tens of
 * milliseconds after any other dirty/present activity.
 */
BOOL APIENTRY
RcddTextOut(
   IN SURFOBJ *pso,
   IN STROBJ *pstro,
   IN FONTOBJ *pfo,
   IN CLIPOBJ *pco,
   IN RECTL *prclExtra,
   IN RECTL *prclOpaque,
   IN BRUSHOBJ *pboFore,
   IN BRUSHOBJ *pboOpaque,
   IN POINTL *pptlOrg,
   IN MIX mix)
{
   BOOL Result;

   Result = EngTextOut(pso, pstro, pfo, pco, prclExtra, prclOpaque,
                       pboFore, pboOpaque, pptlOrg, mix);
   if (Result)
   {
      RECTL rcl;
      const RECTL *prcl = NULL;

      if (pstro != NULL)
      {
         rcl = pstro->rclBkGround;
         if (prclOpaque != NULL)
         {
            rcl.left   = min(rcl.left, prclOpaque->left);
            rcl.top    = min(rcl.top, prclOpaque->top);
            rcl.right  = max(rcl.right, prclOpaque->right);
            rcl.bottom = max(rcl.bottom, prclOpaque->bottom);
         }
         prcl = &rcl;
      }
      else if (prclOpaque != NULL)
      {
         prcl = prclOpaque;
      }

      RcddPresentTarget(pso, prcl, pco);
   }

   return Result;
}

BOOL APIENTRY
RcddLineTo(
   IN SURFOBJ *pso,
   IN CLIPOBJ *pco,
   IN BRUSHOBJ *pbo,
   IN LONG x1,
   IN LONG y1,
   IN LONG x2,
   IN LONG y2,
   IN RECTL *prclBounds,
   IN MIX mix)
{
   BOOL Result;

   Result = EngLineTo(pso, pco, pbo, x1, y1, x2, y2, prclBounds, mix);
   if (Result)
      RcddPresentTarget(pso, prclBounds, pco);

   return Result;
}

BOOL APIENTRY
RcddStrokePath(
   IN SURFOBJ *pso,
   IN PATHOBJ *ppo,
   IN CLIPOBJ *pco,
   IN XFORMOBJ *pxo,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrushOrg,
   IN LINEATTRS *plineattrs,
   IN MIX mix)
{
   BOOL Result;

   Result = EngStrokePath(pso, ppo, pco, pxo, pbo, pptlBrushOrg, plineattrs, mix);
   if (Result)
   {
      RECTL rcl;
      RcddPathBounds(ppo, &rcl);
      RcddPresentTarget(pso, &rcl, pco);
   }

   return Result;
}

BOOL APIENTRY
RcddFillPath(
   IN SURFOBJ *pso,
   IN PATHOBJ *ppo,
   IN CLIPOBJ *pco,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrushOrg,
   IN MIX mix,
   IN FLONG flOptions)
{
   BOOL Result;

   Result = EngFillPath(pso, ppo, pco, pbo, pptlBrushOrg, mix, flOptions);
   if (Result)
   {
      RECTL rcl;
      RcddPathBounds(ppo, &rcl);
      RcddPresentTarget(pso, &rcl, pco);
   }

   return Result;
}

BOOL APIENTRY
RcddStrokeAndFillPath(
   IN SURFOBJ *pso,
   IN PATHOBJ *ppo,
   IN CLIPOBJ *pco,
   IN XFORMOBJ *pxo,
   IN BRUSHOBJ *pboStroke,
   IN LINEATTRS *plineattrs,
   IN BRUSHOBJ *pboFill,
   IN POINTL *pptlBrushOrg,
   IN MIX mixFill,
   IN FLONG flOptions)
{
   BOOL Result;

   Result = EngStrokeAndFillPath(pso, ppo, pco, pxo, pboStroke, plineattrs,
                                 pboFill, pptlBrushOrg, mixFill, flOptions);
   if (Result)
   {
      RECTL rcl;
      RcddPathBounds(ppo, &rcl);
      RcddPresentTarget(pso, &rcl, pco);
   }

   return Result;
}

BOOL APIENTRY
RcddStretchBlt(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN SURFOBJ *psoMask,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN COLORADJUSTMENT *pca,
   IN POINTL *pptlHTOrg,
   IN RECTL *prclDest,
   IN RECTL *prclSrc,
   IN POINTL *pptlMask,
   IN ULONG iMode)
{
   BOOL Result;

   Result = EngStretchBlt(psoDest, psoSrc, psoMask, pco, pxlo, pca, pptlHTOrg,
                          prclDest, prclSrc, pptlMask, iMode);
   if (Result)
      RcddPresentTarget(psoDest, prclDest, pco);

   return Result;
}

BOOL APIENTRY
RcddAlphaBlend(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclDest,
   IN RECTL *prclSrc,
   IN BLENDOBJ *pBlendObj)
{
   BOOL Result;

   Result = EngAlphaBlend(psoDest, psoSrc, pco, pxlo, prclDest, prclSrc, pBlendObj);
   if (Result)
      RcddPresentTarget(psoDest, prclDest, pco);

   return Result;
}

BOOL APIENTRY
RcddTransparentBlt(
   IN SURFOBJ *psoDst,
   IN SURFOBJ *psoSrc,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN RECTL *prclDst,
   IN RECTL *prclSrc,
   IN ULONG iTransColor,
   IN ULONG ulReserved)
{
   BOOL Result;

   Result = EngTransparentBlt(psoDst, psoSrc, pco, pxlo, prclDst, prclSrc,
                              iTransColor, ulReserved);
   if (Result)
      RcddPresentTarget(psoDst, prclDst, pco);

   return Result;
}

BOOL APIENTRY
RcddGradientFill(
   IN SURFOBJ *psoDest,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN TRIVERTEX *pVertex,
   IN ULONG nVertex,
   IN PVOID pMesh,
   IN ULONG nMesh,
   IN RECTL *prclExtents,
   IN POINTL *pptlDitherOrg,
   IN ULONG ulMode)
{
   BOOL Result;

   Result = EngGradientFill(psoDest, pco, pxlo, pVertex, nVertex, pMesh, nMesh,
                            prclExtents, pptlDitherOrg, ulMode);
   if (Result)
      RcddPresentTarget(psoDest, prclExtents, pco);

   return Result;
}
