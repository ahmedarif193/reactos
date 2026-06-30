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
   LONG Left = 0, Top = 0;
   LONG Right = ppdev->ScreenWidth;
   LONG Bottom = ppdev->ScreenHeight;
   ULONG BytesPerPixel = (ppdev->BitsPerPixel + 7) / 8;
   ULONG Delta = ppdev->ScreenDelta;
   ULONG ByteLeft, ByteRight;
   PUCHAR Source, Destination;
   ULONG Bytes;
   LONG y;

   if (!ppdev->ShadowActive)
      return;

   if (prcl != NULL)
   {
      Left = max(prcl->left, Left);
      Top = max(prcl->top, Top);
      Right = min(prcl->right, Right);
      Bottom = min(prcl->bottom, Bottom);
   }

   if (Left >= Right || Top >= Bottom)
      return;

   ByteLeft = ((ULONG)Left * BytesPerPixel) & ~63UL;
   ByteRight = ((ULONG)Right * BytesPerPixel + 63) & ~63UL;
   if (ByteRight > Delta)
      ByteRight = Delta;

   if (ByteLeft == 0 && ByteRight == Delta)
   {
      /* Full-width spans are contiguous in both surfaces. */
      memcpy((PUCHAR)ppdev->ScreenPtr + Top * Delta, ppdev->ShadowPtr + Top * Delta, (Bottom - Top) * Delta);
      return;
   }

   Source = ppdev->ShadowPtr + Top * Delta + ByteLeft;
   Destination = (PUCHAR)ppdev->ScreenPtr + Top * Delta + ByteLeft;
   Bytes = ByteRight - ByteLeft;

   for (y = Top; y < Bottom; y++)
   {
      memcpy(Destination, Source, Bytes);
      Source += Delta;
      Destination += Delta;
   }
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
   if (!ppdev->ShadowActive || psoTrg->pvScan0 != ppdev->ShadowPtr)
      return;

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

   if (pso->pvScan0 != ((PRCDD_PDEV)pso->dhpdev)->ShadowPtr)
      return;

   RcddPresent((PRCDD_PDEV)pso->dhpdev, prcl);
}
