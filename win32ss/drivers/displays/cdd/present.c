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
   union
   {
      ULONGLONG Alignment;
      UCHAR Bytes[FIELD_OFFSET(DXGK_PRESENT_DIRTY_RECTS_INPUT, Rects) +
                  DXGK_PRESENT_DIRTY_MAX_RECTS * sizeof(RECTL)];
   } Packet;
   PDXGK_PRESENT_DIRTY_RECTS_INPUT Input;
   SIZE_T HeaderSize;
   SIZE_T InputSize;
   ULONG Ret;
   BOOL Result;

   HeaderSize = FIELD_OFFSET(DXGK_PRESENT_DIRTY_RECTS_INPUT, Rects);
   if (Count > DXGK_PRESENT_DIRTY_MAX_RECTS)
      return FALSE;
   InputSize = HeaderSize + Count * sizeof(Input->Rects[0]);
   Input = (PDXGK_PRESENT_DIRTY_RECTS_INPUT)Packet.Bytes;
   RtlZeroMemory(Input, InputSize);

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

VOID
RcddQueryPresentStats(
   PRCDD_PDEV ppdev,
   PDXGK_PRESENT_STATS Stats)
{
   if (ppdev == NULL || Stats == NULL)
      return;

   Stats->PresentQueueDepth = 0;
   Stats->PendingDirtyRect = ppdev->PendingRectCount != 0;
   Stats->PresentQueueHighWatermark = 0;
   Stats->PresentQueued = ppdev->PresentQueuedCount;
   Stats->PresentCompleted = ppdev->PresentCompletedCount;
   Stats->PresentFailed = ppdev->PresentFailedCount;
   Stats->PresentRejected = ppdev->PresentRejectedCount;
   Stats->PresentSynchronous = ppdev->PresentSynchronousCount;
}

static VOID
RcddAccumulateRect(
   RECTL *Rects,
   ULONG *RectCount,
   const RECTL *prcl)
{
   RECTL Union;
   ULONG Best = DXGK_PRESENT_DIRTY_MAX_RECTS;
   LONGLONG BestGrowth = 0;
   LONGLONG Area = RcddRectArea(prcl);
   ULONG Index;

   for (Index = 0; Index < *RectCount; Index++)
   {
      LONGLONG ExistingArea;
      LONGLONG Growth;

      Union = Rects[Index];
      RcddRectUnion(&Union, prcl);
      ExistingArea = RcddRectArea(&Rects[Index]);
      Growth = RcddRectArea(&Union) - ExistingArea - Area;
      if (Growth <= (ExistingArea + Area) / 4)
      {
         Rects[Index] = Union;
         return;
      }
      if (Best == DXGK_PRESENT_DIRTY_MAX_RECTS || Growth < BestGrowth)
      {
         Best = Index;
         BestGrowth = Growth;
      }
   }

   if (*RectCount < DXGK_PRESENT_DIRTY_MAX_RECTS)
      Rects[(*RectCount)++] = *prcl;
   else if (Best != DXGK_PRESENT_DIRTY_MAX_RECTS)
      RcddRectUnion(&Rects[Best], prcl);
}

static VOID
RcddAccumulateDirtyRect(
   PRCDD_PDEV ppdev,
   const RECTL *prcl)
{
   RcddAccumulateRect(ppdev->PendingRects,
                      &ppdev->PendingRectCount,
                      prcl);
}

static BOOL
RcddPublishPending(
   PRCDD_PDEV ppdev,
   ULONG Flags)
{
   RECTL SentRect;
   BOOL Notified;
   ULONG Index;

   if (ppdev->ScreenPtr == NULL)
      return FALSE;

   if (ppdev->PendingRectCount == 0)
      return TRUE;

   SentRect = ppdev->PendingRects[0];
   for (Index = 1; Index < ppdev->PendingRectCount; Index++)
      RcddRectUnion(&SentRect, &ppdev->PendingRects[Index]);

   InterlockedIncrement((volatile LONG *)&ppdev->PresentQueuedCount);
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
      InterlockedIncrement((volatile LONG *)&ppdev->PresentCompletedCount);
      InterlockedIncrement((volatile LONG *)&ppdev->PresentSynchronousCount);
   }
   else
   {
      InterlockedIncrement((volatile LONG *)&ppdev->PresentFailedCount);
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
 * boundaries. The current display-only miniports complete PresentDisplayOnly
 * synchronously, so the mapped primary only has to remain stable for this
 * callback. DSS_RESERVED is not a begin/end batching protocol.
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
                 DXGK_PRESENT_DIRTY_FLUSH);
}

typedef struct _RCDD_DISPLAY_TILE_RANGE
{
   ULONG Left;
   ULONG Top;
   ULONG Right;
   ULONG Bottom;
} RCDD_DISPLAY_TILE_RANGE, *PRCDD_DISPLAY_TILE_RANGE;

static BOOL
RcddGetDisplayTileRange(
   IN PRCDD_PDEV ppdev,
   IN OPTIONAL RECTL *prcl,
   OUT PRCDD_DISPLAY_TILE_RANGE Range)
{
   RECTL Rect;
   LONG Temp;

   if (ppdev == NULL ||
       ppdev->DisplayTileLockCount != RCDD_DISPLAY_TILE_COUNT ||
       ppdev->DisplayTileWidth == 0 || ppdev->DisplayTileHeight == 0)
   {
      return FALSE;
   }

   if (prcl != NULL)
   {
      Rect = *prcl;
   }
   else
   {
      Rect.left = 0;
      Rect.top = 0;
      Rect.right = ppdev->ScreenWidth;
      Rect.bottom = ppdev->ScreenHeight;
   }

   if (Rect.left > Rect.right)
   {
      Temp = Rect.left;
      Rect.left = Rect.right;
      Rect.right = Temp;
   }

   if (Rect.top > Rect.bottom)
   {
      Temp = Rect.top;
      Rect.top = Rect.bottom;
      Rect.bottom = Temp;
   }

   Rect.left = max(Rect.left, 0);
   Rect.top = max(Rect.top, 0);
   Rect.right = min(Rect.right, (LONG)ppdev->ScreenWidth);
   Rect.bottom = min(Rect.bottom, (LONG)ppdev->ScreenHeight);
   if (Rect.left >= Rect.right || Rect.top >= Rect.bottom)
      return FALSE;

   Range->Left = min((ULONG)Rect.left / ppdev->DisplayTileWidth,
                     RCDD_DISPLAY_TILE_DIMENSION - 1);
   Range->Top = min((ULONG)Rect.top / ppdev->DisplayTileHeight,
                    RCDD_DISPLAY_TILE_DIMENSION - 1);
   Range->Right = min((ULONG)Rect.right / ppdev->DisplayTileWidth + 1,
                      RCDD_DISPLAY_TILE_DIMENSION);
   Range->Bottom = min((ULONG)Rect.bottom / ppdev->DisplayTileHeight + 1,
                       RCDD_DISPLAY_TILE_DIMENSION);

   return TRUE;
}

VOID APIENTRY
RcddLockDisplayArea(
   IN DHPDEV dhpdev,
   IN OPTIONAL RECTL *prcl)
{
   PRCDD_PDEV ppdev = (PRCDD_PDEV)dhpdev;
   RCDD_DISPLAY_TILE_RANGE Range;
   BOOLEAN Acquired[RCDD_DISPLAY_TILE_COUNT];
   ULONG X, Y, Index;
   BOOL Contended;

   if (!RcddGetDisplayTileRange(ppdev, prcl, &Range))
      return;

   for (;;)
   {
      KeWaitForSingleObject(&ppdev->DisplayLockWaitMutex,
                            Executive,
                            KernelMode,
                            FALSE,
                            NULL);
      RtlZeroMemory(Acquired, sizeof(Acquired));
      Contended = FALSE;

      for (Y = Range.Top; Y < Range.Bottom && !Contended; Y++)
      {
         for (X = Range.Left; X < Range.Right; X++)
         {
            Index = Y * RCDD_DISPLAY_TILE_DIMENSION + X;
            if (!EngAcquireSemaphoreNoWait(ppdev->DisplayTileLocks[Index]))
            {
               Contended = TRUE;
               break;
            }

            Acquired[Index] = TRUE;
         }
      }

      if (!Contended)
      {
         KeReleaseMutex(&ppdev->DisplayLockWaitMutex, FALSE);
         return;
      }

      for (Index = RCDD_DISPLAY_TILE_COUNT; Index != 0; Index--)
      {
         if (Acquired[Index - 1])
            EngReleaseSemaphore(ppdev->DisplayTileLocks[Index - 1]);
      }

      ASSERT(ppdev->DisplayLockWaiters != MAXLONG);
      ppdev->DisplayLockWaiters++;
      KeReleaseMutex(&ppdev->DisplayLockWaitMutex, FALSE);

      KeWaitForSingleObject(&ppdev->DisplayLockWaitSemaphore,
                            Executive,
                            KernelMode,
                            FALSE,
                            NULL);
   }
}

VOID APIENTRY
RcddUnlockDisplayArea(
   IN DHPDEV dhpdev,
   IN OPTIONAL RECTL *prcl)
{
   PRCDD_PDEV ppdev = (PRCDD_PDEV)dhpdev;
   RCDD_DISPLAY_TILE_RANGE Range;
   ULONG X, Y, Index;

   if (!RcddGetDisplayTileRange(ppdev, prcl, &Range))
      return;

   for (Y = Range.Top; Y < Range.Bottom; Y++)
   {
      for (X = Range.Left; X < Range.Right; X++)
      {
         Index = Y * RCDD_DISPLAY_TILE_DIMENSION + X;
         if (!EngIsSemaphoreOwnedByCurrentThread(ppdev->DisplayTileLocks[Index]))
            continue;

         EngReleaseSemaphore(ppdev->DisplayTileLocks[Index]);
         ASSERT(!EngIsSemaphoreOwnedByCurrentThread(ppdev->DisplayTileLocks[Index]));
      }
   }

   KeWaitForSingleObject(&ppdev->DisplayLockWaitMutex,
                         Executive,
                         KernelMode,
                         FALSE,
                         NULL);
   if (ppdev->DisplayLockWaiters != 0)
   {
      KeReleaseSemaphore(&ppdev->DisplayLockWaitSemaphore,
                         IO_NO_INCREMENT,
                         ppdev->DisplayLockWaiters,
                         FALSE);
      ppdev->DisplayLockWaiters = 0;
   }
   KeReleaseMutex(&ppdev->DisplayLockWaitMutex, FALSE);
}

LONG APIENTRY
RcddSynchronizeRedirectionBitmaps(
   IN DHPDEV dhpdev,
   OUT UINT64 *puiFenceID)
{
   PRCDD_PDEV ppdev = (PRCDD_PDEV)dhpdev;
   PDXGK_REDIRECTION_SURFACES_SYNC Sync;
   PRCDD_BITMAP Bitmap;
   PLIST_ENTRY Entry;
   SIZE_T HeaderSize;
   SIZE_T SyncSize;
   ULONG DirtyCount = 0;
   ULONG Index = 0;
   ULONG BytesReturned;
   ULONG ControlStatus;

   if (dhpdev == NULL || puiFenceID == NULL)
      return -1;

   EngAcquireSemaphore(ppdev->RedirectionLock);
   for (Entry = ppdev->RedirectionBitmapList.Flink;
        Entry != &ppdev->RedirectionBitmapList;
        Entry = Entry->Flink)
   {
      Bitmap = CONTAINING_RECORD(Entry, RCDD_BITMAP, ListEntry);
      if (Bitmap->Dirty)
         DirtyCount++;
   }

   /* Native CDD leaves the output untouched when redirection is idle. */
   if (DirtyCount == 0)
   {
      EngReleaseSemaphore(ppdev->RedirectionLock);
      return 0;
   }

   HeaderSize = FIELD_OFFSET(DXGK_REDIRECTION_SURFACES_SYNC, Surfaces);
   if (DirtyCount > (MAXULONG - HeaderSize) / sizeof(Sync->Surfaces[0]))
   {
      EngReleaseSemaphore(ppdev->RedirectionLock);
      return -1;
   }
   SyncSize = HeaderSize + DirtyCount * sizeof(Sync->Surfaces[0]);
   Sync = EngAllocMem(FL_ZERO_MEMORY, SyncSize, ALLOC_TAG);
   if (Sync == NULL)
   {
      EngReleaseSemaphore(ppdev->RedirectionLock);
      return -1;
   }

   Sync->StructSize = (ULONG)SyncSize;
   Sync->SurfaceCount = DirtyCount;
   for (Entry = ppdev->RedirectionBitmapList.Flink;
        Entry != &ppdev->RedirectionBitmapList;
        Entry = Entry->Flink)
   {
      Bitmap = CONTAINING_RECORD(Entry, RCDD_BITMAP, ListEntry);
      if (!Bitmap->Dirty)
         continue;

      ASSERT(Index < DirtyCount);
      Sync->Surfaces[Index].AllocationHandle = Bitmap->AllocationHandle;
      Sync->Surfaces[Index].ResourceHandle = Bitmap->ResourceHandle;
      Sync->Surfaces[Index].GlobalShare = Bitmap->GlobalShare;
      Sync->Surfaces[Index].Pitch = Bitmap->Pitch;
      Sync->Surfaces[Index].DirtyRect = Bitmap->DirtyRect;
      Index++;
   }
   ASSERT(Index == DirtyCount);

   ControlStatus = EngDeviceIoControl(
                      ppdev->hDriver,
                      IOCTL_VIDEO_DXGK_SYNCHRONIZE_REDIRECTION_SURFACES,
                      Sync, (ULONG)SyncSize,
                      Sync, (ULONG)SyncSize,
                      &BytesReturned);
   if (ControlStatus == 0 && BytesReturned >= HeaderSize &&
       Sync->FenceId != 0)
   {
      for (Entry = ppdev->RedirectionBitmapList.Flink;
           Entry != &ppdev->RedirectionBitmapList;
           Entry = Entry->Flink)
      {
         Bitmap = CONTAINING_RECORD(Entry, RCDD_BITMAP, ListEntry);
         Bitmap->Dirty = FALSE;
         RtlZeroMemory(&Bitmap->DirtyRect, sizeof(Bitmap->DirtyRect));
      }
      *puiFenceID = Sync->FenceId;
   }
   else if (ControlStatus == 0)
   {
      ControlStatus = (ULONG)-1;
   }

   EngFreeMem(Sync);
   EngReleaseSemaphore(ppdev->RedirectionLock);
   return (LONG)ControlStatus;
}

BOOL APIENTRY
RcddAccumulateD3DDirtyRect(
   IN SURFOBJ *psoSurf,
   IN CDDDXGK_REDIRBITMAPPRESENTINFO *pDirty)
{
   PRCDD_BITMAP Bitmap;
   RECTL Bounds;
   UINT Index;

   if (psoSurf == NULL || pDirty == NULL ||
       (pDirty->NumDirtyRects != 0 && pDirty->DirtyRect == NULL) ||
       pDirty->NumContexts > WINDDI_MAX_BROADCAST_CONTEXT + 1)
   {
      return FALSE;
   }

   Bitmap = (PRCDD_BITMAP)psoSurf->dhsurf;
   if (Bitmap == NULL || Bitmap->Pdev == NULL ||
       Bitmap->Pdev->RedirectionLock == NULL ||
       (DHPDEV)Bitmap->Pdev != psoSurf->dhpdev ||
       (psoSurf->iType != STYPE_DEVBITMAP &&
        psoSurf->iType != STYPE_BITMAP))
   {
      return FALSE;
   }

   Bounds.left = 0;
   Bounds.top = 0;
   Bounds.right = Bitmap->Width;
   Bounds.bottom = Bitmap->Height;

   EngAcquireSemaphore(Bitmap->Pdev->RedirectionLock);
   if (pDirty->NumDirtyRects == 0)
   {
      Bitmap->DirtyRect = Bounds;
      Bitmap->Dirty = TRUE;
   }
   else
   {
      for (Index = 0; Index < pDirty->NumDirtyRects; ++Index)
      {
         RECTL Rect;

         Rect.left = pDirty->DirtyRect[Index].left;
         Rect.top = pDirty->DirtyRect[Index].top;
         Rect.right = pDirty->DirtyRect[Index].right;
         Rect.bottom = pDirty->DirtyRect[Index].bottom;

         Rect.left = max(Rect.left, Bounds.left);
         Rect.top = max(Rect.top, Bounds.top);
         Rect.right = min(Rect.right, Bounds.right);
         Rect.bottom = min(Rect.bottom, Bounds.bottom);
         if (Rect.left >= Rect.right || Rect.top >= Rect.bottom)
            continue;

         if (!Bitmap->Dirty)
         {
            Bitmap->DirtyRect = Rect;
            Bitmap->Dirty = TRUE;
         }
         else
         {
            RcddRectUnion(&Bitmap->DirtyRect, &Rect);
         }
      }
   }
   EngReleaseSemaphore(Bitmap->Pdev->RedirectionLock);
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
