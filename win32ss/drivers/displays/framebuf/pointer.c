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

static PUCHAR
FrameBufferSurfaceScan(
   IN SURFOBJ *pso,
   IN LONG y)
{
   PUCHAR Bits = pso->pvScan0 ? pso->pvScan0 : pso->pvBits;
   return Bits + (y * pso->lDelta);
}

static BOOL
FrameBufferMonoMaskBit(
   IN SURFOBJ *pso,
   IN LONG x,
   IN LONG y)
{
   PUCHAR Row = FrameBufferSurfaceScan(pso, y);
   return (Row[x >> 3] & (0x80 >> (x & 7))) != 0;
}

BOOL
IntInitHardwarePointer(
   PPDEV ppdev)
{
   ULONG Returned;
   ULONG WidthInBytes;
   ULONG PixelBytes;

   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_QUERY_POINTER_CAPABILITIES,
                          NULL, 0, &ppdev->HwPointerCapabilities,
                          sizeof(ppdev->HwPointerCapabilities), &Returned) != 0)
   {
      return TRUE;
   }

   if (Returned < sizeof(VIDEO_POINTER_CAPABILITIES) ||
       !(ppdev->HwPointerCapabilities.Flags & VIDEO_MODE_COLOR_POINTER) ||
       ppdev->HwPointerCapabilities.MaxWidth == 0 ||
       ppdev->HwPointerCapabilities.MaxHeight == 0)
   {
      return TRUE;
   }

   if (ppdev->HwPointerCapabilities.MaxWidth > ((ULONG)-1) / sizeof(ULONG))
      return TRUE;

   WidthInBytes = ppdev->HwPointerCapabilities.MaxWidth * sizeof(ULONG);
   if (ppdev->HwPointerCapabilities.MaxHeight > ((ULONG)-1) / WidthInBytes)
      return TRUE;

   PixelBytes = WidthInBytes * ppdev->HwPointerCapabilities.MaxHeight;
   if (PixelBytes > ((ULONG)-1) - FIELD_OFFSET(VIDEO_POINTER_ATTRIBUTES, Pixels))
      return TRUE;

   ppdev->HwPointerAttributesSize = FIELD_OFFSET(VIDEO_POINTER_ATTRIBUTES, Pixels) + PixelBytes;
   ppdev->HwPointerAttributes = EngAllocMem(FL_ZERO_MEMORY, ppdev->HwPointerAttributesSize, ALLOC_TAG);
   if (ppdev->HwPointerAttributes == NULL)
      return TRUE;

   ppdev->HwPointerAttributes->Flags = VIDEO_MODE_COLOR_POINTER;
   ppdev->HwPointerAttributes->WidthInBytes = WidthInBytes;
   ppdev->HwPointerAttributes->Width = ppdev->HwPointerCapabilities.MaxWidth;
   ppdev->HwPointerAttributes->Height = ppdev->HwPointerCapabilities.MaxHeight;
   ppdev->HwPointerSupported = TRUE;
   return TRUE;
}

static VOID
FrameBufferDisableHardwarePointer(
   PPDEV ppdev)
{
   ULONG Returned;

   if (ppdev->HwPointerSupported)
   {
      EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_DISABLE_POINTER,
                         NULL, 0, NULL, 0, &Returned);
      ppdev->HwPointerVisible = FALSE;
   }
}

VOID
IntDisableHardwarePointer(
   PPDEV ppdev)
{
   FrameBufferDisableHardwarePointer(ppdev);

   if (ppdev->HwPointerAttributes != NULL)
   {
      EngFreeMem(ppdev->HwPointerAttributes);
      ppdev->HwPointerAttributes = NULL;
   }

   ppdev->HwPointerSupported = FALSE;
   ppdev->HwPointerShapeValid = FALSE;
}

static BOOL
FrameBufferBuildHardwarePointerPixels(
   IN PPDEV ppdev,
   IN SURFOBJ *psoMask,
   IN SURFOBJ *psoColor,
   IN FLONG fl,
   OUT PULONG Width,
   OUT PULONG Height)
{
   PVIDEO_POINTER_ATTRIBUTES Attributes;
   ULONG SourceWidth;
   ULONG SourceHeight;
   ULONG PixelBytes;
   ULONG x;
   ULONG y;

   if (psoColor != NULL)
   {
      if (psoColor->iBitmapFormat != BMF_32BPP)
         return FALSE;

      SourceWidth = psoColor->sizlBitmap.cx;
      SourceHeight = psoColor->sizlBitmap.cy;
   }
   else if (psoMask != NULL)
   {
      SourceWidth = psoMask->sizlBitmap.cx;
      SourceHeight = psoMask->sizlBitmap.cy / 2;
   }
   else
   {
      return FALSE;
   }

   if (SourceWidth == 0 ||
       SourceHeight == 0 ||
       SourceWidth > ppdev->HwPointerCapabilities.MaxWidth ||
       SourceHeight > ppdev->HwPointerCapabilities.MaxHeight)
   {
      return FALSE;
   }

   Attributes = ppdev->HwPointerAttributes;
   PixelBytes = ppdev->HwPointerAttributesSize - FIELD_OFFSET(VIDEO_POINTER_ATTRIBUTES, Pixels);
   memset(Attributes->Pixels, 0, PixelBytes);

   for (y = 0; y < SourceHeight; ++y)
   {
      PULONG DestinationRow = (PULONG)(Attributes->Pixels + (y * Attributes->WidthInBytes));

      if (psoColor != NULL)
      {
         PULONG SourceRow = (PULONG)FrameBufferSurfaceScan(psoColor, y);

         for (x = 0; x < SourceWidth; ++x)
         {
            ULONG Pixel = SourceRow[x];

            if (!(fl & SPS_ALPHA))
               Pixel |= 0xFF000000;

            if (!(fl & SPS_ALPHA) &&
                psoMask != NULL &&
                FrameBufferMonoMaskBit(psoMask, x, y))
            {
               Pixel &= 0x00FFFFFF;
            }

            DestinationRow[x] = Pixel;
         }
      }
      else
      {
         for (x = 0; x < SourceWidth; ++x)
         {
            BOOL AndMask = FrameBufferMonoMaskBit(psoMask, x, y);
            BOOL XorMask = FrameBufferMonoMaskBit(psoMask, x, y + SourceHeight);

            if (AndMask && !XorMask)
               DestinationRow[x] = 0x00000000;
            else if (!AndMask && !XorMask)
               DestinationRow[x] = 0xFF000000;
            else if (!AndMask && XorMask)
               DestinationRow[x] = 0xFFFFFFFF;
            else
               DestinationRow[x] = 0xFF000000;
         }
      }
   }

   *Width = SourceWidth;
   *Height = SourceHeight;
   return TRUE;
}

static BOOL
FrameBufferSetHardwarePointerShape(
   IN PPDEV ppdev,
   IN SURFOBJ *psoMask,
   IN SURFOBJ *psoColor,
   IN LONG xHot,
   IN LONG yHot,
   IN LONG x,
   IN LONG y,
   IN FLONG fl)
{
   PVIDEO_POINTER_ATTRIBUTES Attributes;
   ULONG Returned;
   ULONG Width;
   ULONG Height;

   if (!ppdev->HwPointerSupported)
      return FALSE;

   if (psoMask == NULL && psoColor == NULL)
   {
      FrameBufferDisableHardwarePointer(ppdev);
      ppdev->HwPointerShapeValid = FALSE;
      return TRUE;
   }

   if (!FrameBufferBuildHardwarePointerPixels(ppdev, psoMask, psoColor, fl, &Width, &Height))
   {
      FrameBufferDisableHardwarePointer(ppdev);
      ppdev->HwPointerShapeValid = FALSE;
      return FALSE;
   }

   Attributes = ppdev->HwPointerAttributes;
   Attributes->Flags = VIDEO_MODE_COLOR_POINTER;
   Attributes->Width = Width;
   Attributes->Height = Height;
   Attributes->WidthInBytes = ppdev->HwPointerCapabilities.MaxWidth * sizeof(ULONG);
   Attributes->Enable = (x != -1);
   Attributes->Column = (SHORT)(x - xHot);
   Attributes->Row = (SHORT)(y - yHot);

   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_POINTER_ATTR,
                          Attributes, ppdev->HwPointerAttributesSize,
                          NULL, 0, &Returned) != 0)
   {
      FrameBufferDisableHardwarePointer(ppdev);
      ppdev->HwPointerShapeValid = FALSE;
      return FALSE;
   }

   ppdev->HwPointerHotSpot.x = xHot;
   ppdev->HwPointerHotSpot.y = yHot;
   ppdev->HwPointerShapeValid = TRUE;
   ppdev->HwPointerVisible = (x != -1);
   return TRUE;
}

static BOOL
FrameBufferMoveHardwarePointer(
   IN PPDEV ppdev,
   IN LONG x,
   IN LONG y)
{
   VIDEO_POINTER_POSITION Position;
   ULONG Returned;

   if (!ppdev->HwPointerSupported || !ppdev->HwPointerShapeValid)
      return FALSE;

   if (x == -1)
   {
      FrameBufferDisableHardwarePointer(ppdev);
      return TRUE;
   }

   Position.Column = (SHORT)(x - ppdev->HwPointerHotSpot.x);
   Position.Row = (SHORT)(y - ppdev->HwPointerHotSpot.y);

   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_POINTER_POSITION,
                          &Position, sizeof(Position), NULL, 0, &Returned) != 0)
   {
      return FALSE;
   }

   if (!ppdev->HwPointerVisible)
   {
      if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_ENABLE_POINTER,
                             NULL, 0, NULL, 0, &Returned) != 0)
      {
         return FALSE;
      }
   }

   ppdev->HwPointerVisible = TRUE;
   return TRUE;
}

static VOID
FrameBufferClearPointerExclude(
   IN RECTL *prcl)
{
   if (prcl != NULL)
      prcl->left = prcl->top = prcl->right = prcl->bottom = -1;
}

#ifndef EXPERIMENTAL_MOUSE_CURSOR_SUPPORT

/*
 * DrvSetPointerShape
 *
 * Sets the new pointer shape.
 *
 * Status
 *    @unimplemented
 */

ULONG APIENTRY
DrvSetPointerShape(
   IN SURFOBJ *pso,
   IN SURFOBJ *psoMask,
   IN SURFOBJ *psoColor,
   IN XLATEOBJ *pxlo,
   IN LONG xHot,
   IN LONG yHot,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl,
   IN FLONG fl)
{
   if (pso != NULL &&
       FrameBufferSetHardwarePointerShape((PPDEV)pso->dhpdev, psoMask, psoColor, xHot, yHot, x, y, fl))
   {
      FrameBufferClearPointerExclude(prcl);
      return SPS_ACCEPT_NOEXCLUDE;
   }

   return SPS_DECLINE;
}

/*
 * DrvMovePointer
 *
 * Moves the pointer to a new position and ensures that GDI does not interfere
 * with the display of the pointer.
 *
 * Status
 *    @unimplemented
 */

VOID APIENTRY
DrvMovePointer(
   IN SURFOBJ *pso,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl)
{
   if (pso != NULL &&
       FrameBufferMoveHardwarePointer((PPDEV)pso->dhpdev, x, y))
   {
      FrameBufferClearPointerExclude(prcl);
      return;
   }

   EngMovePointer(pso, x, y, prcl);
}

#else

VOID FASTCALL
IntHideMousePointer(PPDEV ppdev, SURFOBJ *DestSurface)
{
   if (ppdev->PointerAttributes.Enable == FALSE)
   {
      return;
   }

   ppdev->PointerAttributes.Enable = FALSE;
   if (ppdev->PointerSaveSurface != NULL)
   {
      RECTL DestRect;
      POINTL SrcPoint;
      SURFOBJ *SaveSurface;
      SURFOBJ *MaskSurface;

      DestRect.left = max(ppdev->PointerAttributes.Column, 0);
      DestRect.top = max(ppdev->PointerAttributes.Row, 0);
      DestRect.right = min(
         ppdev->PointerAttributes.Column + ppdev->PointerAttributes.Width,
         ppdev->ScreenWidth - 1);
      DestRect.bottom = min(
         ppdev->PointerAttributes.Row + ppdev->PointerAttributes.Height,
         ppdev->ScreenHeight - 1);

      SrcPoint.x = max(-ppdev->PointerAttributes.Column, 0);
      SrcPoint.y = max(-ppdev->PointerAttributes.Row, 0);

      SaveSurface = EngLockSurface(ppdev->PointerSaveSurface);
      MaskSurface = EngLockSurface(ppdev->PointerMaskSurface);
      EngBitBlt(DestSurface, SaveSurface, MaskSurface, NULL, NULL,
                &DestRect, &SrcPoint, &SrcPoint, NULL, NULL, SRCCOPY);
      EngUnlockSurface(MaskSurface);
      EngUnlockSurface(SaveSurface);
   }
}

VOID FASTCALL
IntShowMousePointer(PPDEV ppdev, SURFOBJ *DestSurface)
{
   if (ppdev->PointerAttributes.Enable)
   {
      return;
   }

   ppdev->PointerAttributes.Enable = TRUE;

   /*
    * Copy the pixels under the cursor to temporary surface.
    */

   if (ppdev->PointerSaveSurface != NULL)
   {
      RECTL DestRect;
      POINTL SrcPoint;
      SURFOBJ *SaveSurface;

      SrcPoint.x = max(ppdev->PointerAttributes.Column, 0);
      SrcPoint.y = max(ppdev->PointerAttributes.Row, 0);

      DestRect.left = SrcPoint.x - ppdev->PointerAttributes.Column;
      DestRect.top = SrcPoint.y - ppdev->PointerAttributes.Row;
      DestRect.right = min(
         ppdev->PointerAttributes.Width,
         ppdev->ScreenWidth - ppdev->PointerAttributes.Column - 1);
      DestRect.bottom = min(
         ppdev->PointerAttributes.Height,
         ppdev->ScreenHeight - ppdev->PointerAttributes.Row - 1);

      SaveSurface = EngLockSurface(ppdev->PointerSaveSurface);
      EngBitBlt(SaveSurface, DestSurface, NULL, NULL, NULL,
                &DestRect, &SrcPoint, NULL, NULL, NULL, SRCCOPY);
      EngUnlockSurface(SaveSurface);
   }

   /*
    * Blit the cursor on the screen.
    */

   {
      RECTL DestRect;
      POINTL SrcPoint;
      SURFOBJ *ColorSurf;
      SURFOBJ *MaskSurf;

      DestRect.left = max(ppdev->PointerAttributes.Column, 0);
      DestRect.top = max(ppdev->PointerAttributes.Row, 0);
      DestRect.right = min(
         ppdev->PointerAttributes.Column + ppdev->PointerAttributes.Width,
         ppdev->ScreenWidth - 1);
      DestRect.bottom = min(
         ppdev->PointerAttributes.Row + ppdev->PointerAttributes.Height,
         ppdev->ScreenHeight - 1);

      SrcPoint.x = max(-ppdev->PointerAttributes.Column, 0);
      SrcPoint.y = max(-ppdev->PointerAttributes.Row, 0);

      MaskSurf = EngLockSurface(ppdev->PointerMaskSurface);
      if (ppdev->PointerColorSurface != NULL)
      {
         ColorSurf = EngLockSurface(ppdev->PointerColorSurface);
         EngBitBlt(DestSurface, ColorSurf, MaskSurf, NULL, ppdev->PointerXlateObject,
                   &DestRect, &SrcPoint, &SrcPoint, NULL, NULL, 0xAACC);
         EngUnlockSurface(ColorSurf);
      }
      else
      {
         /* FIXME */
         EngBitBlt(DestSurface, MaskSurf, NULL, NULL, ppdev->PointerXlateObject,
                   &DestRect, &SrcPoint, NULL, NULL, NULL, SRCAND);
         SrcPoint.y += ppdev->PointerAttributes.Height;
         EngBitBlt(DestSurface, MaskSurf, NULL, NULL, ppdev->PointerXlateObject,
                   &DestRect, &SrcPoint, NULL, NULL, NULL, SRCINVERT);
      }
      EngUnlockSurface(MaskSurf);
   }
}

/*
 * DrvSetPointerShape
 *
 * Sets the new pointer shape.
 *
 * Status
 *    @implemented
 */

ULONG APIENTRY
DrvSetPointerShape(
   IN SURFOBJ *pso,
   IN SURFOBJ *psoMask,
   IN SURFOBJ *psoColor,
   IN XLATEOBJ *pxlo,
   IN LONG xHot,
   IN LONG yHot,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl,
   IN FLONG fl)
{
   PPDEV ppdev = (PPDEV)pso->dhpdev;
   SURFOBJ *TempSurfObj;

   IntHideMousePointer(ppdev, pso);

   if (ppdev->PointerColorSurface != NULL)
   {
      /* FIXME: Is this really needed? */
      TempSurfObj = EngLockSurface(ppdev->PointerColorSurface);
      EngFreeMem(TempSurfObj->pvBits);
      TempSurfObj->pvBits = NULL;
      EngUnlockSurface(TempSurfObj);

      EngDeleteSurface(ppdev->PointerColorSurface);
      ppdev->PointerColorSurface = NULL;
   }

   if (ppdev->PointerMaskSurface != NULL)
   {
      /* FIXME: Is this really needed? */
      TempSurfObj = EngLockSurface(ppdev->PointerMaskSurface);
      EngFreeMem(TempSurfObj->pvBits);
      TempSurfObj->pvBits = NULL;
      EngUnlockSurface(TempSurfObj);

      EngDeleteSurface(ppdev->PointerMaskSurface);
      ppdev->PointerMaskSurface = NULL;
   }

   if (ppdev->PointerSaveSurface != NULL)
   {
      EngDeleteSurface(ppdev->PointerSaveSurface);
      ppdev->PointerSaveSurface = NULL;
   }

   /*
    * See if we are being asked to hide the pointer.
    */

   if (psoMask == NULL)
   {
      return SPS_ACCEPT_EXCLUDE;
   }

   ppdev->PointerHotSpot.x = xHot;
   ppdev->PointerHotSpot.y = yHot;

   ppdev->PointerXlateObject = pxlo;
   ppdev->PointerAttributes.Column = x - xHot;
   ppdev->PointerAttributes.Row = y - yHot;
   ppdev->PointerAttributes.Width = psoMask->lDelta << 3;
   ppdev->PointerAttributes.Height = (psoMask->cjBits / psoMask->lDelta) >> 1;

   if (psoColor != NULL)
   {
      SIZEL Size;
      PBYTE Bits;

      Size.cx = ppdev->PointerAttributes.Width;
      Size.cy = ppdev->PointerAttributes.Height;
      Bits = EngAllocMem(0, psoColor->cjBits, ALLOC_TAG);
      memcpy(Bits, psoColor->pvBits, psoColor->cjBits);

      ppdev->PointerColorSurface = (HSURF)EngCreateBitmap(Size,
         psoColor->lDelta, psoColor->iBitmapFormat, 0, Bits);
   }

   {
      SIZEL Size;
      PBYTE Bits;

      Size.cx = ppdev->PointerAttributes.Width;
      Size.cy = ppdev->PointerAttributes.Height << 1;
      Bits = EngAllocMem(0, psoMask->cjBits, ALLOC_TAG);
      memcpy(Bits, psoMask->pvBits, psoMask->cjBits);

      ppdev->PointerMaskSurface = (HSURF)EngCreateBitmap(Size,
         psoMask->lDelta, psoMask->iBitmapFormat, 0, Bits);
   }

   /*
    * Create surface for saving the pixels under the cursor.
    */

   {
      SIZEL Size;
      LONG lDelta;

      Size.cx = ppdev->PointerAttributes.Width;
      Size.cy = ppdev->PointerAttributes.Height;

      switch (pso->iBitmapFormat)
      {
         case BMF_8BPP: lDelta = Size.cx; break;
         case BMF_16BPP: lDelta = Size.cx << 1; break;
         case BMF_24BPP: lDelta = Size.cx * 3; break;
         case BMF_32BPP: lDelta = Size.cx << 2; break;
      }

      ppdev->PointerSaveSurface = (HSURF)EngCreateBitmap(
         Size, lDelta, pso->iBitmapFormat, BMF_NOZEROINIT, NULL);
   }

   IntShowMousePointer(ppdev, pso);

   return SPS_ACCEPT_EXCLUDE;
}

/*
 * DrvMovePointer
 *
 * Moves the pointer to a new position and ensures that GDI does not interfere
 * with the display of the pointer.
 *
 * Status
 *    @implemented
 */

VOID APIENTRY
DrvMovePointer(
   IN SURFOBJ *pso,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl)
{
   PPDEV ppdev = (PPDEV)pso->dhpdev;
   BOOL WasVisible;

   WasVisible = ppdev->PointerAttributes.Enable;
   if (WasVisible)
   {
      IntHideMousePointer(ppdev, pso);
   }

   if (x == -1)
   {
      return;
   }

   ppdev->PointerAttributes.Column = x - ppdev->PointerHotSpot.x;
   ppdev->PointerAttributes.Row = y - ppdev->PointerHotSpot.y;

   if (WasVisible)
   {
      IntShowMousePointer(ppdev, pso);
   }
}

#endif
