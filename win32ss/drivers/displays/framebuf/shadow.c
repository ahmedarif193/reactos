/*
 * PROJECT:     ReactOS Generic Framebuffer display driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Cached shadow surface with dirty-rectangle flushing, so the
 *              DIB engine never reads from uncached video memory.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "framebuf.h"

VOID
IntFlushShadowRect(
   PPDEV ppdev,
   const RECTL *prcl)
{
   LONG Left = 0, Top = 0;
   LONG Right = ppdev->ScreenWidth;
   LONG Bottom = ppdev->ScreenHeight;
   ULONG BytesPerPixel = (ppdev->BitsPerPixel + 7) / 8;
   ULONG Delta = ppdev->ScreenDelta;
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

   if (Left == 0 && Right == (LONG)ppdev->ScreenWidth)
   {
      /* Full-width spans are contiguous in both surfaces. */
      memcpy((PUCHAR)ppdev->ScreenPtr + Top * Delta, ppdev->ShadowPtr + Top * Delta, (Bottom - Top) * Delta);
      return;
   }

   Source = ppdev->ShadowPtr + Top * Delta + Left * BytesPerPixel;
   Destination = (PUCHAR)ppdev->ScreenPtr + Top * Delta + Left * BytesPerPixel;
   Bytes = (Right - Left) * BytesPerPixel;

   for (y = Top; y < Bottom; y++)
   {
      memcpy(Destination, Source, Bytes);
      Source += Delta;
      Destination += Delta;
   }
}

/* Flush the target rectangle, narrowed by the clip bounding box if any. */
static VOID
IntFlushTarget(
   SURFOBJ *psoTrg,
   const RECTL *prclTrg,
   CLIPOBJ *pco)
{
   PPDEV ppdev;
   RECTL rcl;

   if (psoTrg == NULL || psoTrg->dhpdev == NULL)
      return;

   ppdev = (PPDEV)psoTrg->dhpdev;
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

   IntFlushShadowRect(ppdev, &rcl);
}

BOOL APIENTRY
DrvBitBlt(
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
      IntFlushTarget(psoTrg, prclTrg, pco);

   return Result;
}

BOOL APIENTRY
DrvCopyBits(
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
      IntFlushTarget(psoDest, prclDest, pco);

   return Result;
}

/* GDI signals direct engine writes to the shadow with DSS_FLUSH_EVENT. */
VOID APIENTRY
DrvSynchronizeSurface(
   IN SURFOBJ *pso,
   IN RECTL *prcl,
   IN FLONG fl)
{
   if (!(fl & DSS_FLUSH_EVENT) || pso == NULL || pso->dhpdev == NULL)
      return;

   if (pso->pvScan0 != ((PPDEV)pso->dhpdev)->ShadowPtr)
      return;

   IntFlushShadowRect((PPDEV)pso->dhpdev, prcl);
}
