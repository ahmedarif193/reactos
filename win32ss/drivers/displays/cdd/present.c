/*
 * PROJECT:     ReactOS Canonical Display Driver (cdd.dll)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Present seam + dirty-rectangle tracking + GDI draw delegation.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * cdd implements NO raster ops of its own. GDI's engine paints into the cached
 * shadow surface; the BitBlt/CopyBits/SynchronizeSurface hooks below exist only
 * to learn which rectangles changed. Individual primitives accumulate damage;
 * a synchronized GDI flush publishes the completed batch to the WDDM scan-out.
 * RcddPresent is the single seam an Option-B upgrade (D3DKMTPresent /
 * SetVidPnSourceAddress) replaces; see surface.c.
 */

#include "cdd.h"

static BOOL
RcddRectContains(
   const RECTL *prclOuter,
   const RECTL *prclInner)
{
   return prclInner->left >= prclOuter->left &&
          prclInner->top >= prclOuter->top &&
          prclInner->right <= prclOuter->right &&
          prclInner->bottom <= prclOuter->bottom;
}

/*
 * RcddPresent
 *
 * Notify dxgkrnl that the rectangle prcl (or the whole screen when prcl is
 * NULL) of the mapped primary changed. GDI's engine drew straight into
 * ppdev->ScreenPtr, so there is nothing to copy here; dxgkrnl records the
 * rectangle. A later synchronized flush scans the accumulated batch out
 * through the WDDM display-only present path.
 *
 * While GDI has hidden the software cursor for a drawing operation
 * (DSS_RESERVED bracket from win32k's mouse safety, see RcddSynchronizeSurface)
 * the rectangle is only accumulated: the scan-out must never show the
 * cursor-less intermediate state. The bracket's closing flush sends the union.
 */
static BOOL
RcddNotifyDirty(
   PRCDD_PDEV ppdev,
   const RECTL *prcl,
   ULONG Flags)
{
   DXGK_PRESENT_DIRTY_RECT_INPUT Input;
   ULONG Ret;

   if (prcl != NULL)
   {
      Input.Rect = *prcl;
   }
   else
   {
      Input.Rect.left = Input.Rect.top = Input.Rect.right = Input.Rect.bottom = 0;
   }
   Input.Flags = Flags;

   return EngDeviceIoControl(ppdev->hDriver,
                             IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT,
                             &Input,
                             sizeof(Input),
                             NULL,
                             0,
                             &Ret) == 0;
}

/* Publish the completed GDI batch. GDI calls this path with the device lock
 * held for both programmatic flushes and its periodic synchronization timer. */
static VOID
RcddFlushOutstanding(
   PRCDD_PDEV ppdev)
{
   if (ppdev->ScreenPtr == NULL ||
       ppdev->SafetyHidden ||
       !ppdev->DirtyOutstanding)
   {
      return;
   }

   if (RcddNotifyDirty(ppdev, NULL, DXGK_PRESENT_DIRTY_FLUSH))
      ppdev->DirtyOutstanding = FALSE;
}

static VOID
RcddPresentEx(
   PRCDD_PDEV ppdev,
   const RECTL *prcl,
   ULONG Flags)
{
   RECTL Dirty;

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

   if (Dirty.left < Dirty.right && Dirty.top < Dirty.bottom)
   {
      if (ppdev->PendingValid)
      {
         ppdev->PendingRect.left   = min(ppdev->PendingRect.left, Dirty.left);
         ppdev->PendingRect.top    = min(ppdev->PendingRect.top, Dirty.top);
         ppdev->PendingRect.right  = max(ppdev->PendingRect.right, Dirty.right);
         ppdev->PendingRect.bottom = max(ppdev->PendingRect.bottom, Dirty.bottom);
      }
      else
      {
         ppdev->PendingRect = Dirty;
         ppdev->PendingValid = TRUE;
      }
   }

   if (ppdev->SafetyHidden)
      return;

   if (!ppdev->PendingValid)
   {
      if (Flags != 0)
         RcddNotifyDirty(ppdev, NULL, Flags);
      return;
   }

   Dirty = ppdev->PendingRect;
   ppdev->PendingValid = FALSE;
   ppdev->SentSeq = ppdev->DrawSeq;
   ppdev->SentRect = Dirty;

   if (RcddNotifyDirty(ppdev, &Dirty, Flags))
      ppdev->DirtyOutstanding = TRUE;
}

VOID
RcddPresent(
   PRCDD_PDEV ppdev,
   const RECTL *prcl)
{
   RcddPresentEx(ppdev, prcl, 0);
}

/*
 * Every draw DDI opens a sequence before punting to the engine. The engine's
 * own post-write flush (DrvSynchronizeSurface with DSS_FLUSH_EVENT) usually
 * notifies the touched rectangle first; the DDI then only notifies what that
 * flush did not already cover, so one operation costs one notification.
 */
static ULONG
RcddBeginDraw(
   SURFOBJ *pso)
{
   PRCDD_PDEV ppdev;

   if (pso == NULL || pso->dhpdev == NULL)
      return 0;

   ppdev = (PRCDD_PDEV)pso->dhpdev;
   if (pso->pvScan0 != ppdev->ScreenPtr)
      return 0;

   if (++ppdev->DrawSeq == 0)
      ppdev->DrawSeq = 1;

   return ppdev->DrawSeq;
}

/* Notify the target rectangle, narrowed by the clip bounding box if any. */
static VOID
RcddPresentTarget(
   SURFOBJ *psoTrg,
   ULONG seq,
   const RECTL *prclTrg,
   CLIPOBJ *pco)
{
   PRCDD_PDEV ppdev;
   RECTL rcl;

   if (seq == 0 || psoTrg == NULL || psoTrg->dhpdev == NULL)
      return;

   ppdev = (PRCDD_PDEV)psoTrg->dhpdev;
   if (psoTrg->pvScan0 != ppdev->ScreenPtr)
      return;

   if (prclTrg == NULL)
   {
      /* No target bounds — fall back to the clip bounds; failing that,
       * notify the whole screen. */
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

   if (ppdev->SentSeq == seq && !ppdev->PendingValid &&
       RcddRectContains(&ppdev->SentRect, &rcl))
   {
      return;
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

   ULONG seq = RcddBeginDraw(psoTrg);

   Result = EngBitBlt(psoTrg, psoSrc, psoMask, pco, pxlo, prclTrg, pptlSrc, pptlMask, pbo, pptlBrush, rop4);
   if (Result)
      RcddPresentTarget(psoTrg, seq, prclTrg, pco);

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

   ULONG seq = RcddBeginDraw(psoDest);

   Result = EngCopyBits(psoDest, psoSrc, pco, pxlo, prclDest, pptlSrc);
   if (Result)
      RcddPresentTarget(psoDest, seq, prclDest, pco);

   return Result;
}

/*
 * RcddSynchronizeSurface
 *
 * GDI signals direct engine writes to the shadow with DSS_FLUSH_EVENT; notify
 * the affected rectangle. DSS_RESERVED brackets a cursor-hidden drawing op.
 */
VOID APIENTRY
RcddSynchronizeSurface(
   IN SURFOBJ *pso,
   IN RECTL *prcl,
   IN FLONG fl)
{
   PRCDD_PDEV ppdev;

   if (pso == NULL || pso->dhpdev == NULL)
      return;

   ppdev = (PRCDD_PDEV)pso->dhpdev;
   if (pso->pvScan0 != ppdev->ScreenPtr)
      return;

   /* These are GDI's native completed-batch boundaries. Unlike a per-primitive
    * flush with a rectangle, publishing here cannot expose a menu highlight
    * after its background paint but before its text paint. */
   if ((fl & DSS_TIMER_EVENT) ||
       ((fl & DSS_FLUSH_EVENT) && prcl == NULL))
   {
      RcddFlushOutstanding(ppdev);
      return;
   }

   /*
    * win32k's mouse safety brackets a drawing op that overlaps the software
    * cursor: DSS_RESERVED alone when it hides the cursor before the op,
    * DSS_RESERVED | DSS_FLUSH_EVENT after it has redrawn the cursor. Nothing
    * is notified in between, so the scan-out never shows the cursor-less
    * intermediate state; the closing flush sends the whole accumulated area.
    */
   if (fl & DSS_RESERVED)
   {
      if (!(fl & DSS_FLUSH_EVENT))
      {
         if (!ppdev->SafetyHidden)
         {
            ppdev->SafetyHidden = TRUE;
            RcddNotifyDirty(ppdev, NULL, DXGK_PRESENT_DIRTY_HOLD);
         }
         return;
      }
      if (ppdev->SafetyHidden)
      {
         ppdev->SafetyHidden = FALSE;
         RcddPresentEx(ppdev, prcl, DXGK_PRESENT_DIRTY_RELEASE);
         return;
      }
   }

   if (!(fl & DSS_FLUSH_EVENT))
      return;

   RcddPresent(ppdev, prcl);
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
 * the GDI engine and then notifies the touched rectangle. They are hooked
 * ONLY so no drawing primitive can reach the primary without a dirty-rect
 * notification — otherwise text/line/path/gradient output would sit
 * unpresented until the fallback timer.
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

   ULONG seq = RcddBeginDraw(pso);

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

      RcddPresentTarget(pso, seq, prcl, pco);
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

   ULONG seq = RcddBeginDraw(pso);

   Result = EngLineTo(pso, pco, pbo, x1, y1, x2, y2, prclBounds, mix);
   if (Result)
      RcddPresentTarget(pso, seq, prclBounds, pco);

   return Result;
}

BOOL APIENTRY
RcddPaint(
   IN SURFOBJ *pso,
   IN CLIPOBJ *pco,
   IN BRUSHOBJ *pbo,
   IN POINTL *pptlBrushOrg,
   IN MIX mix)
{
   BOOL Result;

   ULONG seq = RcddBeginDraw(pso);

   Result = EngPaint(pso, pco, pbo, pptlBrushOrg, mix);
   if (Result)
      RcddPresentTarget(pso, seq, pco ? &pco->rclBounds : NULL, pco);
   return Result;
}

BOOL APIENTRY
RcddPlgBlt(
   IN SURFOBJ *psoDest,
   IN SURFOBJ *psoSrc,
   IN SURFOBJ *psoMask,
   IN CLIPOBJ *pco,
   IN XLATEOBJ *pxlo,
   IN COLORADJUSTMENT *pca,
   IN POINTL *pptlBrushOrg,
   IN POINTFIX *pptfx,
   IN RECTL *prclSrc,
   IN POINTL *pptlMask,
   IN ULONG iMode)
{
   BOOL Result;

   ULONG seq = RcddBeginDraw(psoDest);

   Result = EngPlgBlt(psoDest, psoSrc, psoMask, pco, pxlo, pca, pptlBrushOrg, pptfx, prclSrc, pptlMask, iMode);
   if (Result)
      RcddPresentTarget(psoDest, seq, NULL, pco);
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

   ULONG seq = RcddBeginDraw(pso);

   Result = EngStrokePath(pso, ppo, pco, pxo, pbo, pptlBrushOrg, plineattrs, mix);
   if (Result)
   {
      RECTL rcl;
      RcddPathBounds(ppo, &rcl);
      RcddPresentTarget(pso, seq, &rcl, pco);
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

   ULONG seq = RcddBeginDraw(pso);

   Result = EngFillPath(pso, ppo, pco, pbo, pptlBrushOrg, mix, flOptions);
   if (Result)
   {
      RECTL rcl;
      RcddPathBounds(ppo, &rcl);
      RcddPresentTarget(pso, seq, &rcl, pco);
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

   ULONG seq = RcddBeginDraw(pso);

   Result = EngStrokeAndFillPath(pso, ppo, pco, pxo, pboStroke, plineattrs,
                                 pboFill, pptlBrushOrg, mixFill, flOptions);
   if (Result)
   {
      RECTL rcl;
      RcddPathBounds(ppo, &rcl);
      RcddPresentTarget(pso, seq, &rcl, pco);
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

   ULONG seq = RcddBeginDraw(psoDest);

   Result = EngStretchBlt(psoDest, psoSrc, psoMask, pco, pxlo, pca, pptlHTOrg,
                          prclDest, prclSrc, pptlMask, iMode);
   if (Result)
      RcddPresentTarget(psoDest, seq, prclDest, pco);

   return Result;
}

BOOL APIENTRY
RcddStretchBltROP(
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
   IN ULONG iMode,
   IN BRUSHOBJ *pbo,
   IN DWORD rop4)
{
   BOOL Result;

   ULONG seq = RcddBeginDraw(psoDest);

   Result = EngStretchBltROP(psoDest, psoSrc, psoMask, pco, pxlo, pca, pptlHTOrg, prclDest, prclSrc, pptlMask, iMode, pbo, rop4);
   if (Result)
      RcddPresentTarget(psoDest, seq, prclDest, pco);
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

   ULONG seq = RcddBeginDraw(psoDest);

   Result = EngAlphaBlend(psoDest, psoSrc, pco, pxlo, prclDest, prclSrc, pBlendObj);
   if (Result)
      RcddPresentTarget(psoDest, seq, prclDest, pco);

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

   ULONG seq = RcddBeginDraw(psoDst);

   Result = EngTransparentBlt(psoDst, psoSrc, pco, pxlo, prclDst, prclSrc,
                              iTransColor, ulReserved);
   if (Result)
      RcddPresentTarget(psoDst, seq, prclDst, pco);

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

   ULONG seq = RcddBeginDraw(psoDest);

   Result = EngGradientFill(psoDest, pco, pxlo, pVertex, nVertex, pMesh, nMesh,
                            prclExtents, pptlDitherOrg, ulMode);
   if (Result)
      RcddPresentTarget(psoDest, seq, prclExtents, pco);

   return Result;
}
