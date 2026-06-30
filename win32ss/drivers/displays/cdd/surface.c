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
   ULONG ShadowSize;
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
    * Map the WDDM scan-out surface into our address space.
    *
    * Option-B upgrade point: replace this IOCTL_VIDEO_MAP_VIDEO_MEMORY with a
    * D3DKMT CreateAllocation(primary) + lock/map so dxgkrnl owns the
    * allocation and the scan-out directly.
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
    * Draw GDI directly into the mapped scan-out. The buffer we map here
    * (IOCTL_VIDEO_MAP_VIDEO_MEMORY) is dxgkrnl's NonPagedPool shadow framebuffer
    * (cached system RAM, not slow write-combined MMIO), and dxgkrnl's present
    * timer scans the WHOLE shadow to the GOP every frame. So a separate cached
    * CPU shadow with selective dirty-rect presenting buys nothing AND is unsafe:
    * only HOOK_BITBLT/COPYBITS ops would be presented, so un-hooked engine
    * drawing (fills, text, alpha, gradients) would stay in the cache and never
    * reach the screen — the cause of the black desktop region. Hand GDI a plain
    * DIB straight over the mapping with no hooks (the framebuf model); every
    * draw lands in the shadow and dxgkrnl presents it. (Option B will swap the
    * mapping for a D3DKMT primary; the compositor seam is escape.c, not a cache.)
    */
   UNREFERENCED_PARAMETER(ShadowSize);
   ppdev->ShadowActive = FALSE;
   SurfaceBits = ppdev->ScreenPtr;
   flHooks = 0;

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

   EngDeleteSurface(ppdev->hSurfEng);
   ppdev->hSurfEng = NULL;

   ppdev->ShadowActive = FALSE;
   if (ppdev->ShadowPtr != NULL)
   {
      EngFreeMem(ppdev->ShadowPtr);
      ppdev->ShadowPtr = NULL;
   }

   /* Unmap the scan-out (Option-B: D3DKMT DestroyAllocation instead). */
   VideoMemory.RequestedVirtualAddress = ppdev->ScreenPtr;
   EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                      &VideoMemory, sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
   ppdev->ScreenPtr = NULL;
}
