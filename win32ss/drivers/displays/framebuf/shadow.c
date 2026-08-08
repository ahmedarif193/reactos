/*
 * PROJECT:     ReactOS Generic Framebuffer display driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Cached shadow surface with bounded dirty-rectangle publication.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "framebuf.h"

#define FRAMEBUF_DEFERRED_FLUSH_BYTES (1024 * 1024)
#define FRAMEBUF_TIMER_FLUSH_BYTES (1024 * 1024)

static BOOL
IntClipShadowRect(
   PPDEV ppdev,
   const RECTL *prcl,
   RECTL *Clipped)
{
   Clipped->left = 0;
   Clipped->top = 0;
   Clipped->right = ppdev->ScreenWidth;
   Clipped->bottom = ppdev->ScreenHeight;

   if (prcl != NULL)
   {
      Clipped->left = max(prcl->left, Clipped->left);
      Clipped->top = max(prcl->top, Clipped->top);
      Clipped->right = min(prcl->right, Clipped->right);
      Clipped->bottom = min(prcl->bottom, Clipped->bottom);
   }

   return Clipped->left < Clipped->right && Clipped->top < Clipped->bottom;
}

static ULONG
IntShadowRowBytes(
   PPDEV ppdev,
   const RECTL *prcl,
   PULONG ByteLeft)
{
   ULONG BytesPerPixel = (ppdev->BitsPerPixel + 7) / 8;
   ULONG ByteRight;

   *ByteLeft = ((ULONG)prcl->left * BytesPerPixel) & ~63UL;
   ByteRight = ((ULONG)prcl->right * BytesPerPixel + 63) & ~63UL;
   if (ByteRight > ppdev->ScreenDelta)
      ByteRight = ppdev->ScreenDelta;

   return ByteRight - *ByteLeft;
}

static VOID
IntCopyShadowRect(
   PPDEV ppdev,
   const RECTL *prcl)
{
   RECTL Clipped;
   ULONG Delta = ppdev->ScreenDelta;
   ULONG ByteLeft, Bytes;
   PUCHAR Source, Destination;
   LONG y;

   if (!ppdev->ShadowActive || !IntClipShadowRect(ppdev, prcl, &Clipped))
      return;

   /* Whole cache-line spans keep the shadow and WC writes co-aligned. */
   Bytes = IntShadowRowBytes(ppdev, &Clipped, &ByteLeft);
   if (ByteLeft == 0 && Bytes == Delta)
   {
      memcpy((PUCHAR)ppdev->ScreenPtr + Clipped.top * Delta, ppdev->ShadowPtr + Clipped.top * Delta, (Clipped.bottom - Clipped.top) * Delta);
      return;
   }

   Source = ppdev->ShadowPtr + Clipped.top * Delta + ByteLeft;
   Destination = (PUCHAR)ppdev->ScreenPtr + Clipped.top * Delta + ByteLeft;
   for (y = Clipped.top; y < Clipped.bottom; ++y)
   {
      memcpy(Destination, Source, Bytes);
      Source += Delta;
      Destination += Delta;
   }
}

static VOID
IntUnionShadowRect(
   RECTL *Destination,
   const RECTL *Source)
{
   Destination->left = min(Destination->left, Source->left);
   Destination->top = min(Destination->top, Source->top);
   Destination->right = max(Destination->right, Source->right);
   Destination->bottom = max(Destination->bottom, Source->bottom);
}

static VOID
IntQueueShadowRect(
   PPDEV ppdev,
   const RECTL *prcl)
{
   RECTL Clipped;
   ULONG ByteLeft, RowBytes;
   ULONGLONG Bytes;

   if (!IntClipShadowRect(ppdev, prcl, &Clipped))
      return;

   RowBytes = IntShadowRowBytes(ppdev, &Clipped, &ByteLeft);
   Bytes = (ULONGLONG)RowBytes * (Clipped.bottom - Clipped.top);
   if (Bytes < FRAMEBUF_DEFERRED_FLUSH_BYTES)
   {
      IntCopyShadowRect(ppdev, &Clipped);
      return;
   }

   if (!ppdev->ShadowFlushValid)
   {
      ppdev->ShadowFlushRect = Clipped;
      ppdev->ShadowFlushValid = TRUE;
      ppdev->ShadowFlushStarted = FALSE;
   }
   else if (!ppdev->ShadowFlushStarted)
   {
      IntUnionShadowRect(&ppdev->ShadowFlushRect, &Clipped);
   }
   else if (ppdev->ShadowPendingValid)
   {
      IntUnionShadowRect(&ppdev->ShadowPendingRect, &Clipped);
   }
   else
   {
      ppdev->ShadowPendingRect = Clipped;
      ppdev->ShadowPendingValid = TRUE;
   }
}

static VOID
IntPromotePendingShadowRect(
   PPDEV ppdev)
{
   if (!ppdev->ShadowFlushValid && ppdev->ShadowPendingValid)
   {
      ppdev->ShadowFlushRect = ppdev->ShadowPendingRect;
      ppdev->ShadowFlushValid = TRUE;
      ppdev->ShadowPendingValid = FALSE;
      ppdev->ShadowFlushStarted = FALSE;
   }
}

static VOID
IntFlushShadowStripe(
   PPDEV ppdev)
{
   RECTL Stripe;
   ULONG ByteLeft, RowBytes, Rows;

   IntPromotePendingShadowRect(ppdev);
   if (!ppdev->ShadowFlushValid)
      return;

   RowBytes = IntShadowRowBytes(ppdev, &ppdev->ShadowFlushRect, &ByteLeft);
   if (RowBytes == 0)
   {
      ppdev->ShadowFlushValid = FALSE;
      ppdev->ShadowFlushStarted = FALSE;
      return;
   }

   Rows = FRAMEBUF_TIMER_FLUSH_BYTES / RowBytes;
   if (Rows == 0)
      Rows = 1;

   Stripe = ppdev->ShadowFlushRect;
   Stripe.bottom = min(Stripe.top + (LONG)Rows, ppdev->ShadowFlushRect.bottom);
   ppdev->ShadowFlushStarted = TRUE;
   IntCopyShadowRect(ppdev, &Stripe);

   if (Stripe.bottom == ppdev->ShadowFlushRect.bottom)
   {
      ppdev->ShadowFlushValid = FALSE;
      ppdev->ShadowFlushStarted = FALSE;
   }
   else
   {
      ppdev->ShadowFlushRect.top = Stripe.bottom;
   }
}

VOID
IntFlushShadowRect(
   PPDEV ppdev,
   const RECTL *prcl)
{
   IntCopyShadowRect(ppdev, prcl);

   if (prcl == NULL)
   {
      ppdev->ShadowFlushValid = FALSE;
      ppdev->ShadowPendingValid = FALSE;
      ppdev->ShadowFlushStarted = FALSE;
   }
}

/* Publish the target rectangle, narrowed by the clip bounding box if any. */
static VOID
IntPublishTarget(
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

   IntQueueShadowRect(ppdev, &rcl);
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
      IntPublishTarget(psoTrg, prclTrg, pco);

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
      IntPublishTarget(psoDest, prclDest, pco);

   return Result;
}

VOID APIENTRY
DrvSynchronizeSurface(
   IN SURFOBJ *pso,
   IN RECTL *prcl,
   IN FLONG fl)
{
   PPDEV ppdev;

   if (pso == NULL || pso->dhpdev == NULL)
      return;

   ppdev = (PPDEV)pso->dhpdev;
   if (!ppdev->ShadowActive || pso->pvScan0 != ppdev->ShadowPtr)
      return;

   if (fl & DSS_TIMER_EVENT)
   {
      IntFlushShadowStripe(ppdev);
      return;
   }

   if (fl & DSS_FLUSH_EVENT)
   {
      if (prcl != NULL)
         IntQueueShadowRect(ppdev, prcl);
      else
         IntFlushShadowStripe(ppdev);
   }
}
