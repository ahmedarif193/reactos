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
PVOID APIENTRY
EngAllocSectionMem(
    _Outptr_ PVOID *ppvSection,
    _In_ ULONG fl,
    _In_ SIZE_T cjSize,
    _In_ ULONG ulTag);

BOOL APIENTRY
EngFreeSectionMem(
    _In_opt_ PVOID pvSection,
    _In_opt_ PVOID pvMappedBase);

BOOLEAN
FbQueryUefiCaps(_Inout_ PPDEV ppdev)
{
    UEFIFB_CAPS uefi = {0};
    DWORD returned = 0;

    if (EngDeviceIoControl(ppdev->hDriver,
                           IOCTL_VIDEO_UEFIFB_QUERY_CAPS,
                           NULL,
                           0,
                           &uefi,
                           sizeof(uefi),
                           &returned))
    {
        ppdev->UefiCapsValid = FALSE;
        ppdev->RosUefiFramebuffer = FALSE;
        ppdev->UefiChildCount = 0;
        ppdev->UefiLargeFramebuffer = FALSE;
        ppdev->UefiFramebufferLength = 0;
        return FALSE;
    }

    if (returned < (FIELD_OFFSET(UEFIFB_CAPS, Caps) + sizeof(uefi.Caps)) ||
        uefi.Version == 0)
    {
        ppdev->UefiCapsValid = FALSE;
        ppdev->RosUefiFramebuffer = FALSE;
        ppdev->UefiChildCount = 0;
        ppdev->UefiLargeFramebuffer = FALSE;
        ppdev->UefiFramebufferLength = 0;
        return FALSE;
    }

    ppdev->UefiCapsValid = TRUE;
    ppdev->RosUefiFramebuffer = TRUE;
    ppdev->UefiChildCount = 1;
    ppdev->UefiPrimaryChild = 0;
    ppdev->UefiFramebufferLength = 0;
    if (ppdev->UefiSelectedChild == (ULONG)-1)
        ppdev->UefiSelectedChild = 0;

    ppdev->UefiLinearOnly = (uefi.Caps & UEFIFB_CAP_LINEAR_ONLY) ? TRUE : FALSE;
    ppdev->UefiLargeFramebuffer = (uefi.Caps & UEFIFB_CAP_LARGE_FB) ? TRUE : FALSE;

    if ((uefi.Version >= 2) && (returned >= sizeof(uefi)))
    {
        ppdev->UefiChildCount = uefi.OutputCount;
        ppdev->UefiPrimaryChild = uefi.PrimaryChild;
        ppdev->UefiFramebufferLength = uefi.FrameBufferLength;
        if (ppdev->UefiSelectedChild == (ULONG)-1)
            ppdev->UefiSelectedChild = uefi.PrimaryChild;
    }

    return TRUE;
}

static VOID
FbDestroyFallbackSurface(_Inout_ PPDEV ppdev)
{
    if (!ppdev->FallbackMapping && !ppdev->FallbackSection)
        return;

    EngFreeSectionMem(ppdev->FallbackSection, ppdev->FallbackMapping);
    ppdev->FallbackSection = NULL;

    ppdev->FallbackMapping = NULL;
    ppdev->UsingFallbackSurface = FALSE;
    ppdev->ScreenPtr = NULL;
}

BOOLEAN
FbMapFramebufferFallback(_Inout_ PPDEV ppdev,
                         ULONGLONG Length)
{
    SIZE_T allocLength;
    PVOID mapping;

    if (Length == 0)
        return FALSE;

    FbDestroyFallbackSurface(ppdev);

    if (Length > (ULONGLONG)~(SIZE_T)0)
        allocLength = ~(SIZE_T)0;
    else
        allocLength = (SIZE_T)Length;

    mapping = EngAllocSectionMem(&ppdev->FallbackSection,
                                 FL_ZERO_MEMORY,
                                 allocLength,
                                 ALLOC_TAG);
    if (!mapping)
        return FALSE;

    ppdev->ScreenPtr = mapping;
    ppdev->UsingFallbackSurface = TRUE;
    ppdev->FallbackMapping = mapping;
    return TRUE;
}

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
   VIDEO_MEMORY_INFORMATION64 VideoMemoryInfo;
   ULONG ulTemp = 0;
   BOOLEAN TriedUefiFallback = FALSE;
   VIDEO_MODE current = {0};
   PVIDEO_MEMORY_INFORMATION VideoMemoryInfo32 =
       (PVIDEO_MEMORY_INFORMATION)&VideoMemoryInfo;

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
    * After SET_CURRENT_MODE, query the actual mode that the miniport set.
    * This handles cases where the firmware rejected the mode switch and the
    * miniport fell back to the current mode (e.g., VirtualBox UEFI after
    * ExitBootServices where GOP mode changes are no longer possible).
    */
   {
      VIDEO_MODE_INFORMATION actualMode = {0};
      ULONG actualModeLen = 0;

      if (!EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_QUERY_CURRENT_MODE,
                              NULL, 0,
                              &actualMode, sizeof(actualMode),
                              &actualModeLen) &&
          actualModeLen >= sizeof(actualMode))
      {
         if (actualMode.VisScreenWidth != ppdev->ScreenWidth ||
             actualMode.VisScreenHeight != ppdev->ScreenHeight ||
             actualMode.ScreenStride != ppdev->ScreenDelta)
         {
            FB_DBG("Mode changed by miniport: requested %ux%u -> actual %ux%u\n",
                   (unsigned int)ppdev->ScreenWidth,
                   (unsigned int)ppdev->ScreenHeight,
                   (unsigned int)actualMode.VisScreenWidth,
                   (unsigned int)actualMode.VisScreenHeight);

            ppdev->ModeIndex = actualMode.ModeIndex;
            ppdev->ScreenWidth = actualMode.VisScreenWidth;
            ppdev->ScreenHeight = actualMode.VisScreenHeight;
            ppdev->ScreenDelta = actualMode.ScreenStride;
            ppdev->BitsPerPixel = (UCHAR)(actualMode.BitsPerPlane * actualMode.NumberOfPlanes);
            ppdev->MemWidth = actualMode.VideoMemoryBitmapWidth;
            ppdev->MemHeight = actualMode.VideoMemoryBitmapHeight;
            ppdev->RedMask = actualMode.RedMask;
            ppdev->GreenMask = actualMode.GreenMask;
            ppdev->BlueMask = actualMode.BlueMask;
         }
      }
   }

   /*
    * Map the framebuffer into our memory.
    */

   (VOID)FbQueryUefiCaps(ppdev);

MapFramebuffer:
   VideoMemory.RequestedVirtualAddress = NULL;
   RtlZeroMemory(&VideoMemoryInfo, sizeof(VideoMemoryInfo));
   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_MAP_VIDEO_MEMORY,
                          &VideoMemory, sizeof(VIDEO_MEMORY),
                          &VideoMemoryInfo, sizeof(VIDEO_MEMORY_INFORMATION64),
                          &ulTemp))
   {
      FB_DBG("IOCTL_VIDEO_MAP_VIDEO_MEMORY failed\n");
      if (!TriedUefiFallback && ppdev->RosUefiFramebuffer && FbSelectSafeMode(ppdev))
      {
         TriedUefiFallback = TRUE;
         goto MapFramebuffer;
      }

      if (!FbMapFramebufferFallback(ppdev,
                                     (ULONGLONG)ppdev->ScreenDelta *
                                     (ULONGLONG)ppdev->ScreenHeight))
      {
         return NULL;
      }
   }

   if (!ppdev->UsingFallbackSurface)
   {
      if (ulTemp >= sizeof(VIDEO_MEMORY_INFORMATION64))
          ppdev->ScreenPtr = VideoMemoryInfo.FrameBufferBase;
      else
          ppdev->ScreenPtr = VideoMemoryInfo32->FrameBufferBase;
      ppdev->UsingFallbackSurface = FALSE;
      ppdev->FallbackMapping = NULL;
   }

   FB_DBG("Mapped framebuffer @ %p (screen %ux%u delta %u bpp %u)%s\n",
          ppdev->ScreenPtr,
          (unsigned int)ppdev->ScreenWidth,
          (unsigned int)ppdev->ScreenHeight,
          (unsigned int)ppdev->ScreenDelta,
          (unsigned int)ppdev->BitsPerPixel,
          ppdev->UsingFallbackSurface ? " (fallback)" : "");

   ppdev->VmwareFifo = FALSE;
   ppdev->VmwareCaps = 0;
   ppdev->UefiLinearOnly = FALSE;
   ppdev->UefiLargeFramebuffer = FALSE;

   if (!ppdev->UsingFallbackSurface && ppdev->BitsPerPixel >= 15)
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
   }

   (VOID)FbQueryUefiCaps(ppdev);

   if (ppdev->UsingFallbackSurface)
   {
       ppdev->FramebufferBytes = (ULONGLONG)ppdev->ScreenDelta *
                                 (ULONGLONG)ppdev->ScreenHeight;
   }
   else
   {
       ULONGLONG fbLength = VideoMemoryInfo.FrameBufferLength;
       if (ppdev->UefiCapsValid && ppdev->UefiFramebufferLength > fbLength)
           fbLength = ppdev->UefiFramebufferLength;
       ppdev->FramebufferBytes = fbLength;
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

   if (ppdev->UsingFallbackSurface)
   {
      FbDestroyFallbackSurface(ppdev);
      return;
   }

   VideoMemory.RequestedVirtualAddress = ((PPDEV)dhpdev)->ScreenPtr;
   EngDeviceIoControl(((PPDEV)dhpdev)->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                      &VideoMemory, sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
   ppdev->ScreenPtr = NULL;
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
