/*
 * ReactOS Generic Framebuffer display driver
 *
 * Copyright (C) 2004 Filip Navara
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
 */

#include "framebuf.h"

static BOOL
FbSelectSafeMode(_Inout_ PPDEV ppdev)
{
    PVIDEO_MODE_INFORMATION modes = NULL;
    DWORD modeInfoSize = 0;
    DWORD modeCount;
    PVIDEO_MODE_INFORMATION entry;
    PVIDEO_MODE_INFORMATION best = NULL;
    VIDEO_MODE_INFORMATION safeMode;
    VIDEO_MODE set = {0};
    ULONG ulTemp;
    DWORD idx;

    modeCount = GetAvailableModes(ppdev->hDriver, &modes, &modeInfoSize);
    if ((modeCount == 0) || (modes == NULL))
        return FALSE;

    entry = modes;
    for (idx = 0; idx < modeCount; ++idx)
    {
        if (entry->Length == 0)
        {
            entry = (PVIDEO_MODE_INFORMATION)(((PUCHAR)entry) + modeInfoSize);
            continue;
        }

        if (best == NULL)
        {
            best = entry;
        }

        if (entry->VisScreenWidth == 800 &&
            entry->VisScreenHeight == 600 &&
            (entry->BitsPerPlane * entry->NumberOfPlanes) == 32)
        {
            best = entry;
            break;
        }

        entry = (PVIDEO_MODE_INFORMATION)(((PUCHAR)entry) + modeInfoSize);
    }

    if (best == NULL)
    {
        EngFreeMem(modes);
        return FALSE;
    }

    safeMode = *best;
    EngFreeMem(modes);

    set.RequestedMode = safeMode.ModeIndex;

    if (EngDeviceIoControl(ppdev->hDriver,
                           IOCTL_VIDEO_SET_CURRENT_MODE,
                           &set,
                           sizeof(set),
                           NULL,
                           0,
                           &ulTemp))
    {
        return FALSE;
    }

    ppdev->ModeIndex = safeMode.ModeIndex;
    ppdev->ScreenWidth = safeMode.VisScreenWidth;
    ppdev->ScreenHeight = safeMode.VisScreenHeight;
    ppdev->ScreenDelta = safeMode.ScreenStride;
    ppdev->BitsPerPixel = (UCHAR)(safeMode.BitsPerPlane * safeMode.NumberOfPlanes);
    ppdev->MemWidth = safeMode.VideoMemoryBitmapWidth;
    ppdev->MemHeight = safeMode.VideoMemoryBitmapHeight;
    ppdev->RedMask = safeMode.RedMask;
    ppdev->GreenMask = safeMode.GreenMask;
    ppdev->BlueMask = safeMode.BlueMask;

    FB_DBG("Fallback to safe mode %lux%lu %ubpp (mode %lu)\n",
           (unsigned long)ppdev->ScreenWidth,
           (unsigned long)ppdev->ScreenHeight,
           (unsigned int)ppdev->BitsPerPixel,
           (unsigned long)ppdev->ModeIndex);
    return TRUE;
}

/*
 * DrvEnableSurface
 *
 * Create engine bitmap around frame buffer and set the video mode requested
 * when PDEV was initialized.
 *
 * Status
 *    @implemented
 */

HSURF APIENTRY
DrvEnableSurface(
   IN DHPDEV dhpdev)
{
   PPDEV ppdev = (PPDEV)dhpdev;
   HSURF hSurface;
   ULONG BitmapType;
   SIZEL ScreenSize;
   VIDEO_MEMORY VideoMemory;
   VIDEO_MEMORY_INFORMATION VideoMemoryInfo;
   ULONG ulTemp;
   BOOLEAN TriedUefiFallback = FALSE;
   VIDEO_MODE current = {0};

   /*
    * Set video mode of our adapter.
    */

   FB_DBG("Setting current mode index %lu (%ux%u %ubpp)\n",
          (unsigned long)ppdev->ModeIndex,
          (unsigned int)ppdev->ScreenWidth,
          (unsigned int)ppdev->ScreenHeight,
          (unsigned int)ppdev->BitsPerPixel);
   current.RequestedMode = ppdev->ModeIndex;
   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE,
                          &current,
                          sizeof(current),
                          NULL,
                          0,
                          &ulTemp))
   {
      FB_DBG("IOCTL_VIDEO_SET_CURRENT_MODE failed for mode %lu\n",
             (unsigned long)ppdev->ModeIndex);
      return NULL;
   }

   /*
    * Map the framebuffer into our memory.
    */

MapFramebuffer:
   VideoMemory.RequestedVirtualAddress = NULL;
   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_MAP_VIDEO_MEMORY,
                          &VideoMemory, sizeof(VIDEO_MEMORY),
                          &VideoMemoryInfo, sizeof(VIDEO_MEMORY_INFORMATION),
                          &ulTemp))
   {
      FB_DBG("IOCTL_VIDEO_MAP_VIDEO_MEMORY failed\n");
      if (!TriedUefiFallback && ppdev->UefiLinearOnly && FbSelectSafeMode(ppdev))
      {
         TriedUefiFallback = TRUE;
         goto MapFramebuffer;
      }
      return NULL;
   }

   ppdev->ScreenPtr = VideoMemoryInfo.FrameBufferBase;
   FB_DBG("Mapped framebuffer @ %p (screen %ux%u delta %u bpp %u)\n",
          ppdev->ScreenPtr,
          (unsigned int)ppdev->ScreenWidth,
          (unsigned int)ppdev->ScreenHeight,
          (unsigned int)ppdev->ScreenDelta,
          (unsigned int)ppdev->BitsPerPixel);

   ppdev->VmwareFifo = FALSE;
   ppdev->VmwareCaps = 0;
   ppdev->UefiLinearOnly = FALSE;

   if (ppdev->BitsPerPixel >= 15)
   {
       VMWARE_VIDEO_CAPS caps = {0};
       DWORD returned = 0;

       if (!EngDeviceIoControl(ppdev->hDriver,
                               IOCTL_VIDEO_VMWARE_QUERY_CAPS,
                               NULL,
                               0,
                               &caps,
                               sizeof(caps),
                               &returned) &&
           returned >= sizeof(caps) &&
           caps.Version == VMWARE_VIDEO_CAPS_VERSION &&
           (caps.Caps & VMWARE_VIDEO_CAP_FIFO))
       {
           ppdev->VmwareFifo = TRUE;
           ppdev->VmwareCaps = caps.Caps;
       }

       /* Optionally query UEFIFB caps to detect linear-only fallback */
       if (!ppdev->VmwareFifo)
       {
           UEFIFB_CAPS uefi = {0};
           returned = 0;
           if (!EngDeviceIoControl(ppdev->hDriver,
                                   IOCTL_VIDEO_UEFIFB_QUERY_CAPS,
                                   NULL,
                                   0,
                                   &uefi,
                                   sizeof(uefi),
                                   &returned) &&
               returned >= sizeof(uefi) &&
               uefi.Version == UEFIFB_CAPS_VERSION &&
               (uefi.Caps & UEFIFB_CAP_LINEAR_ONLY))
           {
               ppdev->UefiLinearOnly = TRUE;
           }
       }
   }

   switch (ppdev->BitsPerPixel)
   {
      case 8:
         IntSetPalette(dhpdev, ppdev->PaletteEntries, 0, 256);
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

   ppdev->iDitherFormat = BitmapType;

   ScreenSize.cx = ppdev->ScreenWidth;
   ScreenSize.cy = ppdev->ScreenHeight;

   hSurface = (HSURF)EngCreateBitmap(ScreenSize, ppdev->ScreenDelta, BitmapType,
                                     (ppdev->ScreenDelta > 0) ? BMF_TOPDOWN : 0,
                                     ppdev->ScreenPtr);
   if (hSurface == NULL)
   {
      FB_DBG("EngCreateBitmap failed (size %ux%u, delta %u, bmf %u, topdown %u, base %p)\n",
             (unsigned int)ScreenSize.cx,
             (unsigned int)ScreenSize.cy,
             (unsigned int)ppdev->ScreenDelta,
             (unsigned int)BitmapType,
             (ppdev->ScreenDelta > 0) ? 1u : 0u,
             ppdev->ScreenPtr);
      return NULL;
   }

   /*
    * Associate the surface with our device.
    */

   if (!EngAssociateSurface(hSurface, ppdev->hDevEng, 0))
   {
      EngDeleteSurface(hSurface);
      FB_DBG("EngAssociateSurface failed (hdev %p, hsurf %p)\n",
             ppdev->hDevEng, hSurface);
      return NULL;
   }

   ppdev->hSurfEng = hSurface;

   return hSurface;
}

/*
 * DrvDisableSurface
 *
 * Used by GDI to notify a driver that the surface created by DrvEnableSurface
 * for the current device is no longer needed.
 *
 * Status
 *    @implemented
 */

VOID APIENTRY
DrvDisableSurface(
   IN DHPDEV dhpdev)
{
   DWORD ulTemp;
   VIDEO_MEMORY VideoMemory;
   PPDEV ppdev = (PPDEV)dhpdev;

   EngDeleteSurface(ppdev->hSurfEng);
   ppdev->hSurfEng = NULL;

#ifdef EXPERIMENTAL_MOUSE_CURSOR_SUPPORT
   /* Clear all mouse pointer surfaces. */
   DrvSetPointerShape(NULL, NULL, NULL, NULL, 0, 0, 0, 0, NULL, 0);
#endif

   /*
    * Unmap the framebuffer.
    */

   VideoMemory.RequestedVirtualAddress = ((PPDEV)dhpdev)->ScreenPtr;
   EngDeviceIoControl(((PPDEV)dhpdev)->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                      &VideoMemory, sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
}

/*
 * DrvAssertMode
 *
 * Sets the mode of the specified physical device to either the mode specified
 * when the PDEV was initialized or to the default mode of the hardware.
 *
 * Status
 *    @implemented
 */

BOOL APIENTRY
DrvAssertMode(
   IN DHPDEV dhpdev,
   IN BOOL bEnable)
{
   PPDEV ppdev = (PPDEV)dhpdev;
   ULONG ulTemp;

   if (bEnable)
   {
      /*
       * Reinitialize the device to a clean state.
       */
      FB_DBG("DrvAssertMode(TRUE) mode %lu (%ux%u %ubpp)\n",
             (unsigned long)ppdev->ModeIndex,
             (unsigned int)ppdev->ScreenWidth,
             (unsigned int)ppdev->ScreenHeight,
             (unsigned int)ppdev->BitsPerPixel);
      if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE,
                             &(ppdev->ModeIndex), sizeof(ULONG), NULL, 0,
                             &ulTemp))
      {
          /* We failed, bail out */
          FB_DBG("DrvAssertMode(TRUE) IOCTL_VIDEO_SET_CURRENT_MODE failed\n");
          return FALSE;
      }
      if (ppdev->BitsPerPixel == 8)
      {
	     IntSetPalette(dhpdev, ppdev->PaletteEntries, 0, 256);
      }

      return TRUE;
   }
   else
   {
      /*
       * Call the miniport driver to reset the device to a known state.
       */
      FB_DBG("DrvAssertMode(FALSE) reset device\n");
      if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_RESET_DEVICE,
                             NULL, 0, NULL, 0, &ulTemp))
      {
          FB_DBG("DrvAssertMode(FALSE) IOCTL_VIDEO_RESET_DEVICE failed\n");
          return FALSE;
      }
      return TRUE;
   }
}
