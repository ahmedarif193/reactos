/*
 * PROJECT:     ReactOS Canonical Display Driver (cdd.dll)
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Driver entry point, DDI dispatch table and PDEV lifetime.
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "cdd.h"

/*
 * The GDI display DDI table. win32k retrieves this from RcddEnableDriver and
 * calls each hook by INDEX_*, so the C symbol names are private (Rcdd*). cdd is
 * a non-accelerated driver: every draw hook punts straight to the GDI engine
 * rasterizer. The hooks exist ONLY to learn which rectangles changed and drive
 * an explicit WDDM dirty-rect present — every primitive that can touch the
 * primary is hooked, so nothing reaches the screen without a present (the
 * fallback timer is suppressed around dirty activity and must never be the
 * sole carrier of a primitive's output).
 */
static const DRVFN gaRcddDriverFunctions[] =
{
   {INDEX_DrvEnablePDEV, (PFN)RcddEnablePDEV},
   {INDEX_DrvCompletePDEV, (PFN)RcddCompletePDEV},
   {INDEX_DrvDisablePDEV, (PFN)RcddDisablePDEV},
   {INDEX_DrvEnableSurface, (PFN)RcddEnableSurface},
   {INDEX_DrvDisableSurface, (PFN)RcddDisableSurface},
   {INDEX_DrvAssertMode, (PFN)RcddAssertMode},
   {INDEX_DrvGetModes, (PFN)RcddGetModes},
   {INDEX_DrvSetPalette, (PFN)RcddSetPalette},
   {INDEX_DrvSetPointerShape, (PFN)RcddSetPointerShape},
   {INDEX_DrvMovePointer, (PFN)RcddMovePointer},
   {INDEX_DrvBitBlt, (PFN)RcddBitBlt},
   {INDEX_DrvCopyBits, (PFN)RcddCopyBits},
   {INDEX_DrvSynchronizeSurface, (PFN)RcddSynchronizeSurface},
   {INDEX_DrvTextOut, (PFN)RcddTextOut},
   {INDEX_DrvLineTo, (PFN)RcddLineTo},
   {INDEX_DrvStrokePath, (PFN)RcddStrokePath},
   {INDEX_DrvFillPath, (PFN)RcddFillPath},
   {INDEX_DrvStrokeAndFillPath, (PFN)RcddStrokeAndFillPath},
   {INDEX_DrvStretchBlt, (PFN)RcddStretchBlt},
   {INDEX_DrvAlphaBlend, (PFN)RcddAlphaBlend},
   {INDEX_DrvTransparentBlt, (PFN)RcddTransparentBlt},
   {INDEX_DrvGradientFill, (PFN)RcddGradientFill},
   {INDEX_DrvEscape, (PFN)RcddEscape},
};

/*
 * RcddEnableDriver
 *
 * The driver's entry point (the PE AddressOfEntryPoint). win32k's loader
 * (EngFindImageProcAddress "DrvEnableDriver" -> pGdiDriverInfo->EntryPoint)
 * calls this to obtain the DDI version and the function table.
 */
BOOL APIENTRY
RcddEnableDriver(
   ULONG iEngineVersion,
   ULONG cj,
   PDRVENABLEDATA pded)
{
   UNREFERENCED_PARAMETER(iEngineVersion);

   if (cj >= sizeof(DRVENABLEDATA))
   {
      pded->c = sizeof(gaRcddDriverFunctions) / sizeof(DRVFN);
      pded->pdrvfn = (DRVFN *)gaRcddDriverFunctions;
      pded->iDriverVersion = DDI_DRIVER_VERSION_NT5;
      return TRUE;
   }

   return FALSE;
}

/*
 * RcddEnablePDEV
 *
 * Returns a description of the physical device's characteristics to GDI. The
 * mode (width/height/bpp, GDIINFO/DEVINFO) is queried from the WDDM display
 * device behind \Device\VideoN (dxgkrnl).
 */
DHPDEV APIENTRY
RcddEnablePDEV(
   IN DEVMODEW *pdm,
   IN LPWSTR pwszLogAddress,
   IN ULONG cPat,
   OUT HSURF *phsurfPatterns,
   IN ULONG cjCaps,
   OUT ULONG *pdevcaps,
   IN ULONG cjDevInfo,
   OUT DEVINFO *pdi,
   IN HDEV hdev,
   IN LPWSTR pwszDeviceName,
   IN HANDLE hDriver)
{
   PRCDD_PDEV ppdev;
   GDIINFO GdiInfo;
   DEVINFO DevInfo;

   UNREFERENCED_PARAMETER(pwszLogAddress);
   UNREFERENCED_PARAMETER(cPat);
   UNREFERENCED_PARAMETER(phsurfPatterns);
   UNREFERENCED_PARAMETER(hdev);
   UNREFERENCED_PARAMETER(pwszDeviceName);

   ppdev = EngAllocMem(FL_ZERO_MEMORY, sizeof(RCDD_PDEV), ALLOC_TAG);
   if (ppdev == NULL)
   {
      return NULL;
   }

   ppdev->hDriver = hDriver;

   if (!RcddInitScreenInfo(ppdev, pdm, &GdiInfo, &DevInfo))
   {
      EngFreeMem(ppdev);
      return NULL;
   }

   if (!RcddInitDefaultPalette(ppdev, &DevInfo))
   {
      RcddDisableHardwarePointer(ppdev);
      EngFreeMem(ppdev);
      return NULL;
   }

   memcpy(pdi, &DevInfo, min(sizeof(DEVINFO), cjDevInfo));
   memcpy(pdevcaps, &GdiInfo, min(sizeof(GDIINFO), cjCaps));

   return (DHPDEV)ppdev;
}

/*
 * RcddCompletePDEV
 *
 * Stores the GDI handle (hdev) of the physical device in the PDEV. The driver
 * retains this handle for use when calling GDI services.
 */
VOID APIENTRY
RcddCompletePDEV(
   IN DHPDEV dhpdev,
   IN HDEV hdev)
{
   ((PRCDD_PDEV)dhpdev)->hDevEng = hdev;
}

/*
 * RcddDisablePDEV
 *
 * Releases the resources allocated in RcddEnablePDEV. If a surface has been
 * enabled RcddDisableSurface will already have been called.
 */
VOID APIENTRY
RcddDisablePDEV(
   IN DHPDEV dhpdev)
{
   PRCDD_PDEV ppdev = (PRCDD_PDEV)dhpdev;

   if (ppdev->DefaultPalette)
   {
      EngDeletePalette(ppdev->DefaultPalette);
   }

   if (ppdev->PaletteEntries != NULL)
   {
      EngFreeMem(ppdev->PaletteEntries);
   }

   RcddDisableHardwarePointer(ppdev);

   EngFreeMem(dhpdev);
}
