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
 */

#include "cdd.h"

#define RCDD_PRESENT_SYNCHRONOUS 0x80000000u

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
 * GDI's engine draws straight into ppdev->ScreenPtr. Drawing hooks accumulate
 * damage locally; DrvSynchronizeSurface publishes that damage only after GDI
 * declares the current update complete.
 */
static BOOL
RcddNotifyDirtyRects(
   PRCDD_PDEV ppdev,
   PVOID Source,
   ULONG SourcePitch,
   ULONG SourceSize,
   const RECTL *prcl,
   ULONG Count,
   ULONG Flags)
{
   PDXGK_PRESENT_DIRTY_RECTS_INPUT Input;
   SIZE_T HeaderSize;
   SIZE_T InputSize;
   ULONG Ret;
   BOOL Result;

   HeaderSize = FIELD_OFFSET(DXGK_PRESENT_DIRTY_RECTS_INPUT, Rects);
   if (Count > (~(ULONG)0 - HeaderSize) / sizeof(Input->Rects[0]))
      return FALSE;
   InputSize = HeaderSize + Count * sizeof(Input->Rects[0]);
   Input = EngAllocMem(FL_ZERO_MEMORY, InputSize, ALLOC_TAG);
   if (Input == NULL)
      return FALSE;

   Input->StructSize = (ULONG)InputSize;
   Input->Flags = Flags;
   Input->RectCount = Count;
   Input->SourcePitch = SourcePitch;
   Input->SourceSize = SourceSize;
   Input->Source = (ULONG_PTR)Source;
   if (Count != 0)
      RtlCopyMemory(Input->Rects, prcl, Count * sizeof(Input->Rects[0]));

   Result = EngDeviceIoControl(ppdev->hDriver,
                               IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT,
                               Input,
                               (ULONG)InputSize,
                               NULL,
                               0,
                               &Ret) == 0;
   EngFreeMem(Input);
   return Result;
}

static LONGLONG
RcddRectArea(
   const RECTL *prcl)
{
   return (LONGLONG)(prcl->right - prcl->left) *
          (prcl->bottom - prcl->top);
}

static VOID
RcddRectUnion(
   RECTL *prclDest,
   const RECTL *prclSource)
{
   prclDest->left = min(prclDest->left, prclSource->left);
   prclDest->top = min(prclDest->top, prclSource->top);
   prclDest->right = max(prclDest->right, prclSource->right);
   prclDest->bottom = max(prclDest->bottom, prclSource->bottom);
}

static VOID
NTAPI
RcddPresentWorkerThread(
   PVOID Context)
{
   PRCDD_PDEV ppdev = Context;
   PRCDD_PRESENT_SLOT Slot;
   ULONG Index;
   BOOL Stop;
   BOOL Result;

   for (;;)
   {
      EngWaitForSingleObject(ppdev->PresentWakeEvent, NULL);

      for (;;)
      {
         Slot = NULL;
         EngAcquireSemaphore(ppdev->PresentLock);
         for (Index = 0; Index < RCDD_PRESENT_SLOT_COUNT; Index++)
         {
            if (ppdev->PresentSlots[Index].State == RcddPresentSlotQueued)
            {
               Slot = &ppdev->PresentSlots[Index];
               Slot->State = RcddPresentSlotActive;
               break;
            }
         }
         Stop = ppdev->PresentWorkerStop;
         if (Slot == NULL && Stop && ppdev->PresentPendingCount == 0)
         {
            EngReleaseSemaphore(ppdev->PresentLock);
            EngSetEvent(ppdev->PresentExitEvent);
            PsTerminateSystemThread(STATUS_SUCCESS);
            return;
         }
         EngReleaseSemaphore(ppdev->PresentLock);

         if (Slot == NULL)
            break;

         Result = RcddNotifyDirtyRects(ppdev,
                                       Slot->Buffer,
                                       ppdev->ScreenDelta,
                                       (ULONG)Slot->BufferSize,
                                       Slot->Rects,
                                       Slot->RectCount,
                                       DXGK_PRESENT_DIRTY_FLUSH);

         EngAcquireSemaphore(ppdev->PresentLock);
         if (Result)
            ppdev->PresentCompletedCount++;
         else
            ppdev->PresentFailedCount++;
         Slot->RectCount = 0;
         Slot->State = RcddPresentSlotFree;
         ASSERT(ppdev->PresentPendingCount != 0);
         if (ppdev->PresentPendingCount != 0)
            ppdev->PresentPendingCount--;
         if (ppdev->PresentPendingCount == 0)
            EngSetEvent(ppdev->PresentDrainEvent);
         EngReleaseSemaphore(ppdev->PresentLock);
      }
   }
}

static VOID
RcddReleasePresentWorkerResources(
   PRCDD_PDEV ppdev)
{
   ULONG Index;

   for (Index = 0; Index < RCDD_PRESENT_SLOT_COUNT; Index++)
   {
      if (ppdev->PresentSlots[Index].Buffer != NULL)
      {
         EngFreeMem(ppdev->PresentSlots[Index].Buffer);
         ppdev->PresentSlots[Index].Buffer = NULL;
      }
      ppdev->PresentSlots[Index].BufferSize = 0;
      ppdev->PresentSlots[Index].RectCount = 0;
      ppdev->PresentSlots[Index].State = RcddPresentSlotFree;
   }

   if (ppdev->PresentDrainEvent != NULL)
   {
      EngDeleteEvent(ppdev->PresentDrainEvent);
      ppdev->PresentDrainEvent = NULL;
   }
   if (ppdev->PresentExitEvent != NULL)
   {
      EngDeleteEvent(ppdev->PresentExitEvent);
      ppdev->PresentExitEvent = NULL;
   }
   if (ppdev->PresentWakeEvent != NULL)
   {
      EngDeleteEvent(ppdev->PresentWakeEvent);
      ppdev->PresentWakeEvent = NULL;
   }
   if (ppdev->PresentLock != NULL)
   {
      EngDeleteSemaphore(ppdev->PresentLock);
      ppdev->PresentLock = NULL;
   }
}

BOOL
RcddStartPresentWorker(
   PRCDD_PDEV ppdev)
{
   OBJECT_ATTRIBUTES ObjectAttributes;
   ULONGLONG BufferSize64;
   ULONG BufferSize;
   ULONG Index;
   NTSTATUS Status;

   if (ppdev == NULL || ppdev->ScreenPtr == NULL ||
       ppdev->ScreenDelta == 0 || ppdev->ScreenHeight == 0 ||
       ppdev->PresentWorkerActive)
   {
      return FALSE;
   }

   BufferSize64 = (ULONGLONG)ppdev->ScreenDelta * ppdev->ScreenHeight;
   if (BufferSize64 == 0 || BufferSize64 > MAXULONG)
      return FALSE;
   BufferSize = (ULONG)BufferSize64;

   ppdev->PresentLock = EngCreateSemaphore();
   if (ppdev->PresentLock == NULL ||
       !EngCreateEvent(&ppdev->PresentWakeEvent) ||
       !EngCreateEvent(&ppdev->PresentExitEvent) ||
       !EngCreateEvent(&ppdev->PresentDrainEvent))
   {
      RcddReleasePresentWorkerResources(ppdev);
      return FALSE;
   }

   for (Index = 0; Index < RCDD_PRESENT_SLOT_COUNT; Index++)
   {
      ppdev->PresentSlots[Index].Buffer =
         EngAllocMem(FL_NONPAGED_MEMORY, BufferSize, ALLOC_TAG);
      if (ppdev->PresentSlots[Index].Buffer == NULL)
      {
         RcddReleasePresentWorkerResources(ppdev);
         return FALSE;
      }
      ppdev->PresentSlots[Index].BufferSize = BufferSize;
      ppdev->PresentSlots[Index].State = RcddPresentSlotFree;
   }

   EngSetEvent(ppdev->PresentDrainEvent);
   ppdev->PresentWorkerStop = FALSE;
   InitializeObjectAttributes(&ObjectAttributes,
                              NULL,
                              OBJ_KERNEL_HANDLE,
                              NULL,
                              NULL);
   Status = PsCreateSystemThread(&ppdev->PresentThread,
                                 THREAD_ALL_ACCESS,
                                 &ObjectAttributes,
                                 NULL,
                                 NULL,
                                 RcddPresentWorkerThread,
                                 ppdev);
   if (!NT_SUCCESS(Status))
   {
      ppdev->PresentThread = NULL;
      RcddReleasePresentWorkerResources(ppdev);
      return FALSE;
   }

   ppdev->PresentWorkerActive = TRUE;
   return TRUE;
}

VOID
RcddStopPresentWorker(
   PRCDD_PDEV ppdev)
{
   if (ppdev == NULL || !ppdev->PresentWorkerActive)
      return;

   EngAcquireSemaphore(ppdev->PresentLock);
   ppdev->PresentWorkerActive = FALSE;
   ppdev->PresentWorkerStop = TRUE;
   EngReleaseSemaphore(ppdev->PresentLock);
   EngSetEvent(ppdev->PresentWakeEvent);
   EngWaitForSingleObject(ppdev->PresentExitEvent, NULL);

   if (ppdev->PresentThread != NULL)
   {
      ZwClose(ppdev->PresentThread);
      ppdev->PresentThread = NULL;
   }
   RcddReleasePresentWorkerResources(ppdev);
}

VOID
RcddQueryPresentWorkerStats(
   PRCDD_PDEV ppdev,
   PDXGK_PRESENT_STATS Stats)
{
   if (ppdev == NULL || Stats == NULL || ppdev->PresentLock == NULL)
      return;

   EngAcquireSemaphore(ppdev->PresentLock);
   Stats->PresentQueueDepth = ppdev->PresentPendingCount;
   Stats->PendingDirtyRect =
      ppdev->PresentPendingCount != 0 || ppdev->PendingRectCount != 0;
   Stats->PresentQueueHighWatermark = ppdev->PresentQueueHighWatermark;
   Stats->PresentQueued = ppdev->PresentQueuedCount;
   Stats->PresentCompleted = ppdev->PresentCompletedCount;
   Stats->PresentFailed = ppdev->PresentFailedCount;
   Stats->PresentRejected = ppdev->PresentRejectedCount;
   Stats->PresentSynchronous = ppdev->PresentSynchronousCount;
   EngReleaseSemaphore(ppdev->PresentLock);
}

static BOOL
RcddQueuePresent(
   PRCDD_PDEV ppdev,
   const RECTL *Rects,
   ULONG Count)
{
   PRCDD_PRESENT_SLOT Slot = NULL;
   ULONG BytesPerPixel;
   ULONG BytesPerRow;
   ULONG Index;
   LONG Y;

   if (ppdev == NULL || Rects == NULL || Count == 0 ||
       Count > DXGK_PRESENT_DIRTY_MAX_RECTS ||
       ppdev->PresentLock == NULL)
   {
      return FALSE;
   }

   EngAcquireSemaphore(ppdev->PresentLock);
   if (ppdev->PresentWorkerActive && !ppdev->PresentWorkerStop)
   {
      for (Index = 0; Index < RCDD_PRESENT_SLOT_COUNT; Index++)
      {
         if (ppdev->PresentSlots[Index].State == RcddPresentSlotFree)
         {
            Slot = &ppdev->PresentSlots[Index];
            Slot->State = RcddPresentSlotCapturing;
            ppdev->PresentPendingCount++;
            if (ppdev->PresentPendingCount > ppdev->PresentQueueHighWatermark)
               ppdev->PresentQueueHighWatermark = ppdev->PresentPendingCount;
            EngClearEvent(ppdev->PresentDrainEvent);
            break;
         }
      }
   }
   if (Slot == NULL)
      ppdev->PresentRejectedCount++;
   EngReleaseSemaphore(ppdev->PresentLock);
   if (Slot == NULL)
      return FALSE;

   BytesPerPixel = (ppdev->BitsPerPixel + 7) / 8;
   RtlCopyMemory(Slot->Rects, Rects, Count * sizeof(Rects[0]));
   Slot->RectCount = Count;
   for (Index = 0; Index < Count; Index++)
   {
      BytesPerRow = (Rects[Index].right - Rects[Index].left) * BytesPerPixel;
      for (Y = Rects[Index].top; Y < Rects[Index].bottom; Y++)
      {
         RtlCopyMemory((PUCHAR)Slot->Buffer +
                          (SIZE_T)Y * ppdev->ScreenDelta +
                          (SIZE_T)Rects[Index].left * BytesPerPixel,
                       (PUCHAR)ppdev->ScreenPtr +
                          (SIZE_T)Y * ppdev->ScreenDelta +
                          (SIZE_T)Rects[Index].left * BytesPerPixel,
                       BytesPerRow);
      }
   }

   EngAcquireSemaphore(ppdev->PresentLock);
   if (ppdev->PresentWorkerActive && !ppdev->PresentWorkerStop &&
       Slot->State == RcddPresentSlotCapturing)
   {
      Slot->State = RcddPresentSlotQueued;
      ppdev->PresentQueuedCount++;
      EngReleaseSemaphore(ppdev->PresentLock);
      EngSetEvent(ppdev->PresentWakeEvent);
      return TRUE;
   }

   Slot->RectCount = 0;
   Slot->State = RcddPresentSlotFree;
   ASSERT(ppdev->PresentPendingCount != 0);
   if (ppdev->PresentPendingCount != 0)
      ppdev->PresentPendingCount--;
   if (ppdev->PresentPendingCount == 0)
      EngSetEvent(ppdev->PresentDrainEvent);
   EngReleaseSemaphore(ppdev->PresentLock);
   EngSetEvent(ppdev->PresentWakeEvent);
   return FALSE;
}

static VOID
RcddAccumulateDirtyRect(
   PRCDD_PDEV ppdev,
   const RECTL *prcl)
{
   RECTL Union;
   ULONG Best = DXGK_PRESENT_DIRTY_MAX_RECTS;
   LONGLONG BestGrowth = 0;
   LONGLONG Area = RcddRectArea(prcl);
   ULONG Index;

   for (Index = 0; Index < ppdev->PendingRectCount; Index++)
   {
      LONGLONG ExistingArea;
      LONGLONG Growth;

      Union = ppdev->PendingRects[Index];
      RcddRectUnion(&Union, prcl);
      ExistingArea = RcddRectArea(&ppdev->PendingRects[Index]);
      Growth = RcddRectArea(&Union) - ExistingArea - Area;
      if (Growth <= (ExistingArea + Area) / 4)
      {
         ppdev->PendingRects[Index] = Union;
         return;
      }
      if (Best == DXGK_PRESENT_DIRTY_MAX_RECTS || Growth < BestGrowth)
      {
         Best = Index;
         BestGrowth = Growth;
      }
   }

   if (ppdev->PendingRectCount < DXGK_PRESENT_DIRTY_MAX_RECTS)
      ppdev->PendingRects[ppdev->PendingRectCount++] = *prcl;
   else if (Best != DXGK_PRESENT_DIRTY_MAX_RECTS)
      RcddRectUnion(&ppdev->PendingRects[Best], prcl);
}

static BOOL
RcddPublishPending(
   PRCDD_PDEV ppdev,
   ULONG Flags)
{
   RECTL SentRect;
   BOOL Notified;
   BOOL Synchronous;
   ULONG Index;

   if (ppdev->ScreenPtr == NULL)
      return FALSE;

   if (ppdev->PendingRectCount == 0)
      return TRUE;

   SentRect = ppdev->PendingRects[0];
   for (Index = 1; Index < ppdev->PendingRectCount; Index++)
      RcddRectUnion(&SentRect, &ppdev->PendingRects[Index]);

   Synchronous = (Flags & RCDD_PRESENT_SYNCHRONOUS) != 0;
   Flags &= ~RCDD_PRESENT_SYNCHRONOUS;
   if (Synchronous)
   {
      Notified = RcddNotifyDirtyRects(ppdev,
                                      ppdev->ScreenPtr,
                                      ppdev->ScreenDelta,
                                      ppdev->ScreenDelta * ppdev->ScreenHeight,
                                      ppdev->PendingRects,
                                      ppdev->PendingRectCount,
                                      Flags);
      if (!Notified)
      {
         Notified = RcddNotifyDirtyRects(ppdev,
                                         ppdev->ScreenPtr,
                                         ppdev->ScreenDelta,
                                         ppdev->ScreenDelta * ppdev->ScreenHeight,
                                         &SentRect,
                                         1,
                                         Flags);
      }
      if (Notified)
      {
         EngAcquireSemaphore(ppdev->PresentLock);
         ppdev->PresentSynchronousCount++;
         EngReleaseSemaphore(ppdev->PresentLock);
      }
   }
   else
   {
      Notified = RcddQueuePresent(ppdev,
                                  ppdev->PendingRects,
                                  ppdev->PendingRectCount);
   }
   if (!Notified)
      return FALSE;

   ppdev->PendingRectCount = 0;
   ppdev->SentSeq = ppdev->DrawSeq;
   ppdev->SentRect = SentRect;
   return TRUE;
}

VOID
RcddPresentEx(
   PRCDD_PDEV ppdev,
   const RECTL *prcl,
   ULONG Flags)
{
   RECTL Dirty;

   if (ppdev->ScreenPtr == NULL)
      return;

   if (prcl != NULL)
   {
      Dirty.left   = 0;
      Dirty.top    = 0;
      Dirty.right  = ppdev->ScreenWidth;
      Dirty.bottom = ppdev->ScreenHeight;
      Dirty.left   = max(Dirty.left, prcl->left);
      Dirty.top    = max(Dirty.top, prcl->top);
      Dirty.right  = min(Dirty.right, prcl->right);
      Dirty.bottom = min(Dirty.bottom, prcl->bottom);
   }
   else if (Flags == 0)
   {
      Dirty.left   = 0;
      Dirty.top    = 0;
      Dirty.right  = ppdev->ScreenWidth;
      Dirty.bottom = ppdev->ScreenHeight;
   }
   else
   {
      Dirty.left = Dirty.top = Dirty.right = Dirty.bottom = 0;
   }

   if (Dirty.left < Dirty.right && Dirty.top < Dirty.bottom)
      RcddAccumulateDirtyRect(ppdev, &Dirty);

   if (Flags & DXGK_PRESENT_DIRTY_FLUSH)
      RcddPublishPending(ppdev, Flags);
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

   if (ppdev->SentSeq == seq && ppdev->PendingRectCount == 0 &&
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
 * Windows uses DSS_TIMER_EVENT and DSS_FLUSH_EVENT as completed-update
 * boundaries. DSS_RESERVED selects the synchronous command form on the native
 * CDD path; it is not a begin/end batching protocol.
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

   if (!(fl & (DSS_TIMER_EVENT | DSS_FLUSH_EVENT)))
      return;

   RcddPresentEx(ppdev,
                 prcl,
                 DXGK_PRESENT_DIRTY_FLUSH |
                    ((fl & DSS_RESERVED) ? RCDD_PRESENT_SYNCHRONOUS : 0));
}

/* ReactOS does not create CDD device redirection bitmaps yet. Keep the Windows
 * DDI surface in place without aliasing these operations to the primary. */
VOID APIENTRY
RcddLockDisplayArea(
   IN DHPDEV dhpdev,
   IN OPTIONAL RECTL *prcl)
{
   UNREFERENCED_PARAMETER(dhpdev);
   UNREFERENCED_PARAMETER(prcl);
}

VOID APIENTRY
RcddUnlockDisplayArea(
   IN DHPDEV dhpdev,
   IN OPTIONAL RECTL *prcl)
{
   UNREFERENCED_PARAMETER(dhpdev);
   UNREFERENCED_PARAMETER(prcl);
}

LONG APIENTRY
RcddSynchronizeRedirectionBitmaps(
   IN DHPDEV dhpdev,
   OUT UINT64 *puiFenceID)
{
   if (dhpdev == NULL || puiFenceID == NULL)
      return -1;

   /* Native CDD leaves the output untouched when redirection is disabled. */
   return 0;
}

BOOL APIENTRY
RcddAccumulateD3DDirtyRect(
   IN SURFOBJ *psoSurf,
   IN CDDDXGK_REDIRBITMAPPRESENTINFO *pDirty)
{
   if (psoSurf == NULL || pDirty == NULL ||
       (pDirty->NumDirtyRects != 0 && pDirty->DirtyRect == NULL))
   {
      return FALSE;
   }

   /* Native CDD dispatches only STYPE_DEVBITMAP objects to its redirection
    * bitmap implementation and succeeds as a no-op for every other surface. */
   return TRUE;
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
 * notification.
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
