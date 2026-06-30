/*
 * PROJECT:     ReactOS Canonical Display Driver (cdd.dll)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Pointer (cursor) support with DWM cursor suppression.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * cdd prefers a hardware pointer when the WDDM device advertises one; on the
 * software GPU there is none, so RcddSetPointerShape declines and GDI draws a
 * software cursor (RcddMovePointer falls back to EngMovePointer). When the DWM
 * compositor takes over the cursor (CDD_ESCAPE_SUPPRESS_CURSOR, see escape.c)
 * both paths are suppressed: the compositor draws the cursor itself.
 */

#include "cdd.h"

static PUCHAR
RcddSurfaceScan(
   IN SURFOBJ *pso,
   IN LONG y)
{
   PUCHAR Bits = pso->pvScan0 ? pso->pvScan0 : pso->pvBits;
   return Bits + (y * pso->lDelta);
}

static BOOL
RcddMonoMaskBit(
   IN SURFOBJ *pso,
   IN LONG x,
   IN LONG y)
{
   PUCHAR Row = RcddSurfaceScan(pso, y);
   return (Row[x >> 3] & (0x80 >> (x & 7))) != 0;
}

BOOL
RcddInitHardwarePointer(
   PRCDD_PDEV ppdev)
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

/* Hide the hardware pointer without releasing its attribute buffer. */
static VOID
RcddHideHardwarePointer(
   PRCDD_PDEV ppdev)
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
RcddDisableHardwarePointer(
   PRCDD_PDEV ppdev)
{
   RcddHideHardwarePointer(ppdev);

   if (ppdev->HwPointerAttributes != NULL)
   {
      EngFreeMem(ppdev->HwPointerAttributes);
      ppdev->HwPointerAttributes = NULL;
   }

   ppdev->HwPointerSupported = FALSE;
   ppdev->HwPointerShapeValid = FALSE;
}

static BOOL
RcddBuildHardwarePointerPixels(
   IN PRCDD_PDEV ppdev,
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
         PULONG SourceRow = (PULONG)RcddSurfaceScan(psoColor, y);

         for (x = 0; x < SourceWidth; ++x)
         {
            ULONG Pixel = SourceRow[x];

            if (!(fl & SPS_ALPHA))
               Pixel |= 0xFF000000;

            if (!(fl & SPS_ALPHA) &&
                psoMask != NULL &&
                RcddMonoMaskBit(psoMask, x, y))
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
            BOOL AndMask = RcddMonoMaskBit(psoMask, x, y);
            BOOL XorMask = RcddMonoMaskBit(psoMask, x, y + SourceHeight);

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
RcddSetHardwarePointerShape(
   IN PRCDD_PDEV ppdev,
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
      RcddHideHardwarePointer(ppdev);
      ppdev->HwPointerShapeValid = FALSE;
      return TRUE;
   }

   if (!RcddBuildHardwarePointerPixels(ppdev, psoMask, psoColor, fl, &Width, &Height))
   {
      RcddHideHardwarePointer(ppdev);
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
      RcddHideHardwarePointer(ppdev);
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
RcddMoveHardwarePointer(
   IN PRCDD_PDEV ppdev,
   IN LONG x,
   IN LONG y)
{
   VIDEO_POINTER_POSITION Position;
   ULONG Returned;

   if (!ppdev->HwPointerSupported || !ppdev->HwPointerShapeValid)
      return FALSE;

   if (x == -1)
   {
      RcddHideHardwarePointer(ppdev);
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
RcddClearPointerExclude(
   IN RECTL *prcl)
{
   if (prcl != NULL)
      prcl->left = prcl->top = prcl->right = prcl->bottom = -1;
}

/*
 * RcddSetPointerShape
 *
 * Sets the new pointer shape. While the compositor suppresses the cursor, the
 * driver claims the pointer and draws nothing so neither the hardware nor the
 * GDI software cursor appears.
 */
ULONG APIENTRY
RcddSetPointerShape(
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
   PRCDD_PDEV ppdev = pso ? (PRCDD_PDEV)pso->dhpdev : NULL;

   UNREFERENCED_PARAMETER(pxlo);

   if (ppdev != NULL && ppdev->CursorSuppressed)
   {
      RcddHideHardwarePointer(ppdev);
      RcddClearPointerExclude(prcl);
      return SPS_ACCEPT_NOEXCLUDE;
   }

   if (pso != NULL &&
       RcddSetHardwarePointerShape((PRCDD_PDEV)pso->dhpdev, psoMask, psoColor, xHot, yHot, x, y, fl))
   {
      RcddClearPointerExclude(prcl);
      return SPS_ACCEPT_NOEXCLUDE;
   }

   return SPS_DECLINE;
}

/*
 * RcddMovePointer
 *
 * Moves the pointer to a new position. While suppressed the cursor stays
 * hidden; otherwise we use the hardware pointer or fall back to the GDI
 * software cursor.
 */
VOID APIENTRY
RcddMovePointer(
   IN SURFOBJ *pso,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl)
{
   PRCDD_PDEV ppdev = pso ? (PRCDD_PDEV)pso->dhpdev : NULL;

   if (ppdev != NULL && ppdev->CursorSuppressed)
   {
      RcddHideHardwarePointer(ppdev);
      RcddClearPointerExclude(prcl);
      return;
   }

   if (pso != NULL &&
       RcddMoveHardwarePointer((PRCDD_PDEV)pso->dhpdev, x, y))
   {
      RcddClearPointerExclude(prcl);
      return;
   }

   EngMovePointer(pso, x, y, prcl);
}
