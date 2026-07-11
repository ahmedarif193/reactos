/*
 * PROJECT:     ReactOS Canonical Display Driver (cdd.dll)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Primary surface lifetime (DrvEnableSurface/DrvDisableSurface).
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * This is the one file that differs in substance from a plain framebuffer
 * driver: it decides where the primary drawing surface lives.
 *
 *   Option A (shipped): map dxgkrnl's shadow framebuffer via
 *      IOCTL_VIDEO_MAP_VIDEO_MEMORY on \Device\VideoN and wrap it in a GDI DIB.
 *      GDI draws into a cached shadow; RcddPresent (present.c) streams dirty
 *      rectangles to the mapping, and dxgkrnl's present timer scans them to the
 *      GOP. This reuses the already-verified WDDM scan-out path and is the
 *      reason cdd builds and renders today.
 *
 *   Option B (TODO): open a D3DKMT device, CreateAllocation a primary, lock and
 *      map it, wrap THAT in the GDI DIB, and have RcddPresent issue
 *      D3DKMTPresent / SetVidPnSourceAddress. This is the honest WDDM path but
 *      needs an in-kernel D3DKMT client reachable from a win32k-loaded GDI
 *      driver (win32k currently exports only the Eng* surface/IOCTL DDI, not a
 *      kernel D3DKMT client), so it is deferred. The seam is RcddPresent() plus
 *      the mapping below - nothing else in cdd changes for the upgrade.
 */

#include "cdd.h"

/*
 * RcddEnableSurface
 *
 * Sets the WDDM mode, maps the scan-out, and creates the GDI bitmap surface
 * the engine rasterizer draws into.
 */
HSURF APIENTRY
RcddEnableSurface(
   IN DHPDEV dhpdev)
{
   PRCDD_PDEV ppdev = (PRCDD_PDEV)dhpdev;
   HSURF hSurface;
   ULONG BitmapType;
   SIZEL ScreenSize;
   VIDEO_MEMORY VideoMemory;
   VIDEO_MEMORY_INFORMATION VideoMemoryInfo;
   ULONG ulTemp;
   FLONG flHooks = 0;
   PVOID SurfaceBits;

   /* Commit the requested mode on the WDDM display device. */
   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE,
                          &(ppdev->ModeIndex), sizeof(ULONG), NULL, 0,
                          &ulTemp))
   {
      return NULL;
   }

   /*
    * Re-query the mode dxgkrnl actually committed. RcddInitScreenInfo captured
    * the geometry at EnablePDEV time, BEFORE CommitVidPn ran — if the committed
    * native mode differs, drawing with the stale stride would shear the
    * scan-out. Adopt the committed stride; refuse a width/height mismatch
    * (GDI was already promised the EnablePDEV geometry).
    */
   {
      VIDEO_MODE_INFORMATION CommittedMode;

      if (!EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_QUERY_CURRENT_MODE,
                              NULL, 0, &CommittedMode, sizeof(CommittedMode),
                              &ulTemp))
      {
         if (CommittedMode.VisScreenWidth != (ULONG)ppdev->ScreenWidth ||
             CommittedMode.VisScreenHeight != (ULONG)ppdev->ScreenHeight)
         {
            /* Committed native mode differs from what EnablePDEV promised
             * GDI — a sheared scan-out is worse than no surface. */
            return NULL;
         }

         if (CommittedMode.ScreenStride != 0)
            ppdev->ScreenDelta = CommittedMode.ScreenStride;
      }
   }

   /*
    * Map the DOD primary (dxgkrnl's system-memory scan-out surface) into our
    * address space. GDI draws straight into it and RcddPresent scans dirty
    * rectangles out through the miniport's DxgkDdiPresentDisplayOnly.
    */
   VideoMemory.RequestedVirtualAddress = NULL;
   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_MAP_VIDEO_MEMORY,
                          &VideoMemory, sizeof(VIDEO_MEMORY),
                          &VideoMemoryInfo, sizeof(VIDEO_MEMORY_INFORMATION),
                          &ulTemp))
   {
      return NULL;
   }

   ppdev->ScreenPtr = VideoMemoryInfo.FrameBufferBase;
   if (ppdev->ScreenPtr == NULL)
      return NULL;

   switch (ppdev->BitsPerPixel)
   {
      case 8:
         RcddSetPaletteEntries(dhpdev, ppdev->PaletteEntries, 0, 256);
         BitmapType = BMF_8BPP;
         break;

      case 16:
         BitmapType = BMF_16BPP;
         break;

      case 24:
         BitmapType = BMF_24BPP;
         break;

      case 32:
         BitmapType = BMF_32BPP;
         break;

      default:
         return NULL;
   }

   ScreenSize.cx = ppdev->ScreenWidth;
   ScreenSize.cy = ppdev->ScreenHeight;

   /*
    * Hand GDI a plain DIB straight over the mapped DOD primary: the GDI engine
    * draws every operation (blits, text, fills, alpha) directly into the buffer
    * dxgkrnl's DxgkDdiPresentDisplayOnly reads. There is no separate CPU shadow
    * or cache, so nothing can be stranded unpresented. We hook
    * BitBlt/CopyBits/Synchronize only to learn which rectangles changed and then
    * drive an explicit WDDM dirty-rect present (RcddPresent ->
    * IOCTL_VIDEO_DXGK_PRESENT_DIRTY_RECT -> the miniport's PresentDisplayOnly).
    * This is the display-only equivalent of the canonical driver's honest WDDM
    * present — driven by cdd, not dxgkrnl's fallback present timer (which stays
    * as a safety net). The compositor seam is escape.c.
    */
   SurfaceBits = ppdev->ScreenPtr;
   flHooks = HOOK_BITBLT | HOOK_COPYBITS | HOOK_SYNCHRONIZE |
             HOOK_TEXTOUT | HOOK_LINETO | HOOK_STROKEPATH |
             HOOK_FILLPATH | HOOK_STROKEANDFILLPATH | HOOK_STRETCHBLT |
             HOOK_ALPHABLEND | HOOK_TRANSPARENTBLT | HOOK_GRADIENTFILL;

   /*
    * Hand GDI a plain DIB over the draw buffer. No raster ops are implemented
    * here - the engine paints into this bitmap and we only present it.
    */
   hSurface = (HSURF)EngCreateBitmap(ScreenSize, ppdev->ScreenDelta, BitmapType,
                                     (ppdev->ScreenDelta > 0) ? BMF_TOPDOWN : 0,
                                     SurfaceBits);
   if (hSurface == NULL)
   {
      return NULL;
   }

   if (!EngAssociateSurface(hSurface, ppdev->hDevEng, flHooks))
   {
      EngDeleteSurface(hSurface);
      VideoMemory.RequestedVirtualAddress = ppdev->ScreenPtr;
      EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                         &VideoMemory, sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
      ppdev->ScreenPtr = NULL;
      return NULL;
   }

   ppdev->hSurfEng = hSurface;

   return hSurface;
}

/*
 * RcddDisableSurface
 *
 * Notifies the driver that the surface created by RcddEnableSurface is no
 * longer needed and unmaps the scan-out.
 */
VOID APIENTRY
RcddDisableSurface(
   IN DHPDEV dhpdev)
{
   DWORD ulTemp;
   VIDEO_MEMORY VideoMemory;
   PRCDD_PDEV ppdev = (PRCDD_PDEV)dhpdev;

   if (ppdev->hSurfEng != NULL)
   {
      EngDeleteSurface(ppdev->hSurfEng);
      ppdev->hSurfEng = NULL;
   }

   if (ppdev->ScreenPtr != NULL)
   {
      VideoMemory.RequestedVirtualAddress = ppdev->ScreenPtr;
      EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                         &VideoMemory, sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
      ppdev->ScreenPtr = NULL;
   }
}
