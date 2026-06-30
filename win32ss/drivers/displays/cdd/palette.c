/*
 * PROJECT:     ReactOS Canonical Display Driver (cdd.dll)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Palette support (only relevant at <= 8bpp).
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "cdd.h"

/*
 * Standard colors that must be in the palette because they are used for
 * drawing window borders and other GUI elements.
 */
static const PALETTEENTRY BASEPALETTE[20] =
{
   { 0x00, 0x00, 0x00, 0x00 },
   { 0x80, 0x00, 0x00, 0x00 },
   { 0x00, 0x80, 0x00, 0x00 },
   { 0x80, 0x80, 0x00, 0x00 },
   { 0x00, 0x00, 0x80, 0x00 },
   { 0x80, 0x00, 0x80, 0x00 },
   { 0x00, 0x80, 0x80, 0x00 },
   { 0xC0, 0xC0, 0xC0, 0x00 },
   { 0xC0, 0xDC, 0xC0, 0x00 },
   { 0xD4, 0xD0, 0xC8, 0x00 },
   { 0xFF, 0xFB, 0xF0, 0x00 },
   { 0x3A, 0x6E, 0xA5, 0x00 },
   { 0x80, 0x80, 0x80, 0x00 },
   { 0xFF, 0x00, 0x00, 0x00 },
   { 0x00, 0xFF, 0x00, 0x00 },
   { 0xFF, 0xFF, 0x00, 0x00 },
   { 0x00, 0x00, 0xFF, 0x00 },
   { 0xFF, 0x00, 0xFF, 0x00 },
   { 0x00, 0xFF, 0xFF, 0x00 },
   { 0xFF, 0xFF, 0xFF, 0x00 },
};

/*
 * RcddInitDefaultPalette
 *
 * Initializes the default palette for the PDEV. At > 8bpp the palette is a
 * bitfield palette built from the channel masks; at 8bpp it is an indexed
 * palette seeded with the standard GUI colors.
 */
BOOL
RcddInitDefaultPalette(
   PRCDD_PDEV ppdev,
   PDEVINFO pDevInfo)
{
   ULONG ColorLoop;
   PPALETTEENTRY PaletteEntryPtr;

   if (ppdev->BitsPerPixel > 8)
   {
      ppdev->DefaultPalette = pDevInfo->hpalDefault =
         EngCreatePalette(PAL_BITFIELDS, 0, NULL,
            ppdev->RedMask, ppdev->GreenMask, ppdev->BlueMask);
   }
   else
   {
      ppdev->PaletteEntries = EngAllocMem(0, sizeof(PALETTEENTRY) << 8, ALLOC_TAG);
      if (ppdev->PaletteEntries == NULL)
      {
         return FALSE;
      }

      for (ColorLoop = 256, PaletteEntryPtr = ppdev->PaletteEntries;
           ColorLoop != 0;
           ColorLoop--, PaletteEntryPtr++)
      {
         PaletteEntryPtr->peRed = ((ColorLoop >> 5) & 7) * 255 / 7;
         PaletteEntryPtr->peGreen = ((ColorLoop >> 3) & 3) * 255 / 3;
         PaletteEntryPtr->peBlue = (ColorLoop & 7) * 255 / 7;
         PaletteEntryPtr->peFlags = 0;
      }

      memcpy(ppdev->PaletteEntries, BASEPALETTE, 10 * sizeof(PALETTEENTRY));
      memcpy(ppdev->PaletteEntries + 246, BASEPALETTE + 10, 10 * sizeof(PALETTEENTRY));

      ppdev->DefaultPalette = pDevInfo->hpalDefault =
         EngCreatePalette(PAL_INDEXED, 256, (PULONG)ppdev->PaletteEntries, 0, 0, 0);
   }

   return ppdev->DefaultPalette != NULL;
}

/*
 * RcddSetPaletteEntries
 *
 * Programs the device CLUT with the given palette entries.
 */
BOOL APIENTRY
RcddSetPaletteEntries(
   IN DHPDEV dhpdev,
   IN PPALETTEENTRY ppalent,
   IN ULONG iStart,
   IN ULONG cColors)
{
   PVIDEO_CLUT pClut;
   ULONG ClutSize;

   ClutSize = sizeof(VIDEO_CLUT) + (cColors * sizeof(ULONG));
   pClut = EngAllocMem(0, ClutSize, ALLOC_TAG);
   if (pClut == NULL)
      return FALSE;

   pClut->FirstEntry = iStart;
   pClut->NumEntries = cColors;
   memcpy(&pClut->LookupTable[0].RgbLong, ppalent, sizeof(ULONG) * cColors);

   if (((PRCDD_PDEV)dhpdev)->PaletteShift)
   {
      while (cColors--)
      {
         pClut->LookupTable[cColors].RgbArray.Red >>= ((PRCDD_PDEV)dhpdev)->PaletteShift;
         pClut->LookupTable[cColors].RgbArray.Green >>= ((PRCDD_PDEV)dhpdev)->PaletteShift;
         pClut->LookupTable[cColors].RgbArray.Blue >>= ((PRCDD_PDEV)dhpdev)->PaletteShift;
         pClut->LookupTable[cColors].RgbArray.Unused = 0;
      }
   }
   else
   {
      while (cColors--)
      {
         pClut->LookupTable[cColors].RgbArray.Unused = 0;
      }
   }

   if (EngDeviceIoControl(((PRCDD_PDEV)dhpdev)->hDriver, IOCTL_VIDEO_SET_COLOR_REGISTERS,
                          pClut, ClutSize, NULL, 0, &cColors))
   {
      EngFreeMem(pClut);
      return FALSE;
   }

   EngFreeMem(pClut);
   return TRUE;
}

/*
 * RcddSetPalette
 *
 * Realizes a GDI palette onto the device.
 */
BOOL APIENTRY
RcddSetPalette(
   IN DHPDEV dhpdev,
   IN PALOBJ *ppalo,
   IN FLONG fl,
   IN ULONG iStart,
   IN ULONG cColors)
{
   PPALETTEENTRY PaletteEntries;
   BOOL bRet;

   UNREFERENCED_PARAMETER(fl);

   if (cColors == 0)
      return FALSE;

   PaletteEntries = EngAllocMem(0, cColors * sizeof(ULONG), ALLOC_TAG);
   if (PaletteEntries == NULL)
   {
      return FALSE;
   }

   if (PALOBJ_cGetColors(ppalo, iStart, cColors, (PULONG)PaletteEntries) != cColors)
   {
      EngFreeMem(PaletteEntries);
      return FALSE;
   }

   bRet = RcddSetPaletteEntries(dhpdev, PaletteEntries, iStart, cColors);
   EngFreeMem(PaletteEntries);
   return bRet;
}
