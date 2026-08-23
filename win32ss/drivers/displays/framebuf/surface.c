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

static VOID
IntResetShadowState(
   PPDEV ppdev)
{
   ppdev->ShadowActive = FALSE;
   ppdev->ShadowFlushValid = FALSE;
   ppdev->ShadowPendingValid = FALSE;
   ppdev->ShadowFlushStarted = FALSE;
   ppdev->ShadowBatchActive = FALSE;
   ppdev->ShadowBatchValid = FALSE;
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
   ULONG ShadowSize;
   FLONG flHooks = 0;

   /*
    * Set video mode of our adapter.
    */

   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE,
                          &(ppdev->ModeIndex), sizeof(ULONG), NULL, 0,
                          &ulTemp))
   {
      return NULL;
   }

   /*
    * Map the framebuffer into our memory.
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

   /*
    * Expose a hooked bitmap over the cached shadow. Keeping the primary
    * bitmap-backed lets GDI complete a software-pointer update in the shadow
    * before its synchronization hook publishes the final dirty rectangle.
    * The hook layer still redirects normal driver calls to the separate,
    * unhooked shadow surface to avoid recursion.
    */
   IntResetShadowState(ppdev);
   ppdev->hSurfShadow = NULL;
   ppdev->psoShadow = NULL;
   ppdev->ShadowPtr = NULL;
   ShadowSize = ppdev->ScreenHeight * ppdev->ScreenDelta;
   if (ShadowSize != 0)
   {
      ppdev->hSurfShadow = (HSURF)EngCreateBitmap(
         ScreenSize,
         ppdev->ScreenDelta,
         BitmapType,
         (ppdev->ScreenDelta > 0) ? BMF_TOPDOWN : 0,
         NULL);
   }
   if (ppdev->hSurfShadow != NULL &&
       EngAssociateSurface(ppdev->hSurfShadow, ppdev->hDevEng, 0))
   {
      ppdev->psoShadow = EngLockSurface(ppdev->hSurfShadow);
   }
   if (ppdev->psoShadow != NULL && ppdev->psoShadow->pvScan0 != NULL)
   {
      ppdev->ShadowPtr = ppdev->psoShadow->pvScan0;
      memcpy(ppdev->ShadowPtr, ppdev->ScreenPtr, ShadowSize);
      ppdev->ShadowActive = TRUE;
      flHooks = HOOK_BITBLT | HOOK_COPYBITS | HOOK_LINETO | HOOK_PAINT |
                HOOK_STRETCHBLT | HOOK_STRETCHBLTROP | HOOK_ALPHABLEND |
                HOOK_TRANSPARENTBLT | HOOK_GRADIENTFILL |
                HOOK_SYNCHRONIZE;
      hSurface = EngCreateDeviceSurface((DHSURF)ppdev,
                                        ScreenSize,
                                        BitmapType);
   }
   else
   {
      if (ppdev->psoShadow != NULL)
      {
         EngUnlockSurface(ppdev->psoShadow);
         ppdev->psoShadow = NULL;
      }
      if (ppdev->hSurfShadow != NULL)
      {
         EngDeleteSurface(ppdev->hSurfShadow);
         ppdev->hSurfShadow = NULL;
      }

      hSurface = (HSURF)EngCreateBitmap(
         ScreenSize,
         ppdev->ScreenDelta,
         BitmapType,
         (ppdev->ScreenDelta > 0) ? BMF_TOPDOWN : 0,
         ppdev->ScreenPtr);
   }

   if (hSurface == NULL)
   {
      goto Failure;
   }

   /*
    * Associate the surface with our device.
    */

   if (ppdev->ShadowActive)
   {
      if (!EngModifySurface(hSurface,
                            ppdev->hDevEng,
                            flHooks,
                            0,
                            (DHSURF)ppdev,
                            ppdev->ShadowPtr,
                            ppdev->ScreenDelta,
                            NULL))
      {
         EngDeleteSurface(hSurface);
         goto Failure;
      }
   }
   else if (!EngAssociateSurface(hSurface, ppdev->hDevEng, flHooks))
   {
      EngDeleteSurface(hSurface);
      goto Failure;
   }

   ppdev->hSurfEng = hSurface;

   return hSurface;

Failure:
   IntResetShadowState(ppdev);
   ppdev->ShadowPtr = NULL;
   if (ppdev->psoShadow != NULL)
   {
      EngUnlockSurface(ppdev->psoShadow);
      ppdev->psoShadow = NULL;
   }
   if (ppdev->hSurfShadow != NULL)
   {
      EngDeleteSurface(ppdev->hSurfShadow);
      ppdev->hSurfShadow = NULL;
   }
   return NULL;
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

   IntResetShadowState(ppdev);
   ppdev->ShadowPtr = NULL;
   if (ppdev->psoShadow != NULL)
   {
      EngUnlockSurface(ppdev->psoShadow);
      ppdev->psoShadow = NULL;
   }
   if (ppdev->hSurfShadow != NULL)
   {
      EngDeleteSurface(ppdev->hSurfShadow);
      ppdev->hSurfShadow = NULL;
   }

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
      if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE,
                             &(ppdev->ModeIndex), sizeof(ULONG), NULL, 0,
                             &ulTemp))
      {
          /* We failed, bail out */
          return FALSE;
      }
      if (ppdev->BitsPerPixel == 8)
      {
	     IntSetPalette(dhpdev, ppdev->PaletteEntries, 0, 256);
      }

      /* The framebuffer contents are unknown; resync it from the shadow. */
      IntFlushShadowRect(ppdev, NULL);

      return TRUE;
   }
   else
   {
      /*
       * Call the miniport driver to reset the device to a known state.
       */
      return !EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_RESET_DEVICE,
                                 NULL, 0, NULL, 0, &ulTemp);
   }
}
