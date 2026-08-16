/*
 * PROJECT:     ReactOS Generic Framebuffer display driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Generic XPDM driver entry point
 * COPYRIGHT:   Copyright 2004 Filip Navara
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "framebuf.h"

static DRVFN DrvFunctionTable[] =
{
   {INDEX_DrvEnablePDEV, (PFN)FrameBufferEnablePDEV},
   {INDEX_DrvCompletePDEV, (PFN)DrvCompletePDEV},
   {INDEX_DrvDisablePDEV, (PFN)DrvDisablePDEV},
   {INDEX_DrvEnableSurface, (PFN)DrvEnableSurface},
   {INDEX_DrvDisableSurface, (PFN)DrvDisableSurface},
   {INDEX_DrvAssertMode, (PFN)DrvAssertMode},
   {INDEX_DrvGetModes, (PFN)DrvGetModes},
   {INDEX_DrvSetPalette, (PFN)DrvSetPalette},
   {INDEX_DrvSetPointerShape, (PFN)DrvSetPointerShape},
   {INDEX_DrvMovePointer, (PFN)DrvMovePointer},
   {INDEX_DrvEnableDirectDraw, (PFN)DrvEnableDirectDraw},
   {INDEX_DrvDisableDirectDraw, (PFN)DrvDisableDirectDraw},
   {INDEX_DrvBitBlt, (PFN)DrvBitBlt},
   {INDEX_DrvCopyBits, (PFN)DrvCopyBits},
   {INDEX_DrvLineTo, (PFN)DrvLineTo},
   {INDEX_DrvPaint, (PFN)DrvPaint},
   {INDEX_DrvStretchBlt, (PFN)DrvStretchBlt},
   {INDEX_DrvStretchBltROP, (PFN)DrvStretchBltROP},
   {INDEX_DrvAlphaBlend, (PFN)DrvAlphaBlend},
   {INDEX_DrvTransparentBlt, (PFN)DrvTransparentBlt},
   {INDEX_DrvGradientFill, (PFN)DrvGradientFill},
   {INDEX_DrvSynchronizeSurface, (PFN)DrvSynchronizeSurface},
};

BOOL APIENTRY
DrvEnableDriver(
   ULONG iEngineVersion,
   ULONG cj,
   PDRVENABLEDATA pded)
{
   UNREFERENCED_PARAMETER(iEngineVersion);

   if (cj < sizeof(DRVENABLEDATA))
      return FALSE;

   pded->c = RTL_NUMBER_OF(DrvFunctionTable);
   pded->pdrvfn = DrvFunctionTable;
   pded->iDriverVersion = DDI_DRIVER_VERSION_NT5;
   return TRUE;
}
