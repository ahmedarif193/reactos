/*
 * ReactOS Canonical Display Driver (CDD) - Driver Enable / PDEV
 *
 * Copyright (C) 2026 ReactOS Team
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
 *
 * DESCRIPTION
 * -----------
 * DrvEnableDriver, DrvEnablePDEV, DrvCompletePDEV, DrvDisablePDEV.
 *
 * CDD registers hooks for all drawing DDI functions so that every GDI
 * operation routes through our Drv* wrappers.  This is required because
 * the primary surface is STYPE_DEVICE (not STYPE_BITMAP) -- the engine
 * only dispatches to hooked entry points for device surfaces.  Each
 * wrapper punts to the corresponding Eng* function on the shadow bitmap
 * and then flushes the dirty rectangle to VRAM.
 */

#include "cdd.h"

/* Forward declarations for DrvEnableDirectDraw / DrvDisableDirectDraw */
BOOL APIENTRY DrvEnableDirectDraw(DHPDEV, DD_CALLBACKS*, DD_SURFACECALLBACKS*, DD_PALETTECALLBACKS*);
VOID APIENTRY DrvDisableDirectDraw(DHPDEV);
VOID APIENTRY DrvDisableDriver(VOID);

/*
 * DDI function dispatch table.
 *
 * CDD hooks every drawing entry point so that all GDI operations are
 * routed through our Drv* functions.  Each one substitutes the shadow
 * bitmap for the device surface before punting to the Eng* equivalent,
 * then flushes the affected rectangle from shadow to VRAM.
 *
 * Win7 CDD has 38 entries.  The extended indices (72-73, 94-102) are
 * Vista+/Win7 DDI additions for WDDM compositing support.
 */
static DRVFN CddFunctionTable[] =
{
    /* Lifecycle */
    {INDEX_DrvEnablePDEV,           (PFN)DrvEnablePDEV},
    {INDEX_DrvCompletePDEV,         (PFN)DrvCompletePDEV},
    {INDEX_DrvDisablePDEV,          (PFN)DrvDisablePDEV},
    {INDEX_DrvEnableSurface,        (PFN)DrvEnableSurface},
    {INDEX_DrvDisableSurface,       (PFN)DrvDisableSurface},
    {INDEX_DrvAssertMode,           (PFN)DrvAssertMode},
    {INDEX_DrvGetModes,             (PFN)DrvGetModes},
    {INDEX_DrvSetPalette,           (PFN)DrvSetPalette},
    {8,                             (PFN)DrvDisableDriver},
    /* Device bitmap */
    {INDEX_DrvCreateDeviceBitmap,   (PFN)DrvCreateDeviceBitmap},
    {INDEX_DrvDeleteDeviceBitmap,   (PFN)DrvDeleteDeviceBitmap},
    /* Pointer */
    {INDEX_DrvSetPointerShape,      (PFN)DrvSetPointerShape},
    {INDEX_DrvMovePointer,          (PFN)DrvMovePointer},
    /* Drawing */
    {INDEX_DrvBitBlt,               (PFN)DrvBitBlt},
    {INDEX_DrvCopyBits,             (PFN)DrvCopyBits},
    {INDEX_DrvTextOut,              (PFN)DrvTextOut},
    {INDEX_DrvLineTo,               (PFN)DrvLineTo},
    {INDEX_DrvStrokePath,           (PFN)DrvStrokePath},
    {INDEX_DrvFillPath,             (PFN)DrvFillPath},
    {INDEX_DrvStrokeAndFillPath,    (PFN)DrvStrokeAndFillPath},
    {INDEX_DrvPaint,                (PFN)DrvPaint},
    {INDEX_DrvStretchBlt,           (PFN)DrvStretchBlt},
    {INDEX_DrvStretchBltROP,        (PFN)DrvStretchBltROP},
    {INDEX_DrvAlphaBlend,           (PFN)DrvAlphaBlend},
    {INDEX_DrvTransparentBlt,       (PFN)DrvTransparentBlt},
    {INDEX_DrvGradientFill,         (PFN)DrvGradientFill},
    {INDEX_DrvPlgBlt,               (PFN)DrvPlgBlt},
    /* DirectDraw */
    {INDEX_DrvEnableDirectDraw,     (PFN)DrvEnableDirectDraw},
    {INDEX_DrvDisableDirectDraw,    (PFN)DrvDisableDirectDraw},
    /* Font synthesis (Win7 WDDM) */
    {INDEX_DrvSynthesizeFont,       (PFN)DrvSynthesizeFont},
    {INDEX_DrvGetSynthesizedFontFiles, (PFN)DrvGetSynthesizedFontFiles},
    /* WDDM redirection bitmap and composition (Win7) */
    {INDEX_DrvCreateDeviceBitmapEx, (PFN)DrvCreateDeviceBitmapEx},
    {INDEX_DrvDeleteDeviceBitmapEx, (PFN)DrvDeleteDeviceBitmapEx},
    {INDEX_DrvAssociateSharedSurface, (PFN)DrvAssociateSharedSurface},
    {INDEX_DrvSynchronizeRedirectionBitmaps, (PFN)DrvSynchronizeRedirectionBitmaps},
    {INDEX_DrvAccumulateD3DDirtyRect, (PFN)DrvAccumulateD3DDirtyRect},
    /* DX/GDI interop */
    {INDEX_DrvStartDxInterop,       (PFN)DrvStartDxInterop},
    {INDEX_DrvEndDxInterop,         (PFN)DrvEndDxInterop},
    /* Display area lock */
    {INDEX_DrvLockDisplayArea,      (PFN)DrvLockDisplayArea},
    {INDEX_DrvUnlockDisplayArea,    (PFN)DrvUnlockDisplayArea},
};

/*
 * DrvEnableDirectDraw / DrvDisableDirectDraw
 *
 * Minimal stubs -- CDD does not accelerate DirectDraw, but must respond
 * to the query so the engine does not fall back to software.
 */
BOOL APIENTRY
DrvEnableDirectDraw(
    DHPDEV dhpdev,
    DD_CALLBACKS *pCallbacks,
    DD_SURFACECALLBACKS *pSurfaceCallbacks,
    DD_PALETTECALLBACKS *pPaletteCallbacks)
{
    UNREFERENCED_PARAMETER(dhpdev);

    RtlZeroMemory(pCallbacks, sizeof(*pCallbacks));
    RtlZeroMemory(pSurfaceCallbacks, sizeof(*pSurfaceCallbacks));
    RtlZeroMemory(pPaletteCallbacks, sizeof(*pPaletteCallbacks));

    pCallbacks->dwSize = sizeof(*pCallbacks);
    pSurfaceCallbacks->dwSize = sizeof(*pSurfaceCallbacks);
    pPaletteCallbacks->dwSize = sizeof(*pPaletteCallbacks);

    return TRUE;
}

VOID APIENTRY
DrvDisableDirectDraw(
    DHPDEV dhpdev)
{
    UNREFERENCED_PARAMETER(dhpdev);
}

/*
 * DrvDisableDriver
 *
 * Final cleanup when the driver is being unloaded.
 */
VOID APIENTRY
DrvDisableDriver(VOID)
{
    /* Nothing to do -- all per-device state is freed in DrvDisablePDEV */
}

/*
 * DrvEnableDriver
 *
 * Initial driver entry point exported by the driver DLL.  Fills in the
 * DRVENABLEDATA structure with the DDI version and function table.
 *
 * CDD reports DDI_DRIVER_VERSION_NT5 for compatibility with ReactOS's
 * win32k (which does not yet support NT6 DDI version negotiation).
 */
BOOL APIENTRY
DrvEnableDriver(
    ULONG iEngineVersion,
    ULONG cj,
    PDRVENABLEDATA pded)
{
    UNREFERENCED_PARAMETER(iEngineVersion);

    if (cj >= sizeof(DRVENABLEDATA))
    {
        pded->c = sizeof(CddFunctionTable) / sizeof(DRVFN);
        pded->pdrvfn = CddFunctionTable;
        pded->iDriverVersion = DDI_DRIVER_VERSION_NT5;
        return TRUE;
    }

    return FALSE;
}

/*
 * DrvEnablePDEV
 *
 * Creates the physical device (PDEV) that describes adapter capabilities
 * to GDI.  Queries video modes from the miniport and initializes the
 * palette.  The D3DKMT fields are zeroed for Phase 1.
 */
DHPDEV APIENTRY
DrvEnablePDEV(
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
    PCDD_PDEV ppdev;
    GDIINFO GdiInfo;
    DEVINFO DevInfo;

    UNREFERENCED_PARAMETER(pwszLogAddress);
    UNREFERENCED_PARAMETER(cPat);
    UNREFERENCED_PARAMETER(phsurfPatterns);
    UNREFERENCED_PARAMETER(hdev);
    UNREFERENCED_PARAMETER(pwszDeviceName);

    ppdev = EngAllocMem(FL_ZERO_MEMORY, sizeof(CDD_PDEV), ALLOC_TAG);
    if (ppdev == NULL)
    {
        CDD_DBG("EngAllocMem for PDEV failed\n");
        return NULL;
    }

    ppdev->hDriver = hDriver;

    /* Initialize WDDM locking primitives */
    CddInitLocking(ppdev);

    if (!CddInitScreenInfo(ppdev, pdm, &GdiInfo, &DevInfo))
    {
        CDD_DBG("CddInitScreenInfo failed (mode %ux%u %ubpp)\n",
                pdm ? pdm->dmPelsWidth : 0,
                pdm ? pdm->dmPelsHeight : 0,
                pdm ? pdm->dmBitsPerPel : 0);
        EngFreeMem(ppdev);
        return NULL;
    }

    if (!CddInitDefaultPalette(ppdev, &DevInfo))
    {
        CDD_DBG("CddInitDefaultPalette failed\n");
        EngFreeMem(ppdev);
        return NULL;
    }

    memcpy(pdi, &DevInfo, min(sizeof(DEVINFO), cjDevInfo));
    memcpy(pdevcaps, &GdiInfo, min(sizeof(GDIINFO), cjCaps));

    return (DHPDEV)ppdev;
}

/*
 * DrvCompletePDEV
 *
 * Stores the GDI engine handle in the PDEV.
 */
VOID APIENTRY
DrvCompletePDEV(
    IN DHPDEV dhpdev,
    IN HDEV hdev)
{
    ((PCDD_PDEV)dhpdev)->hDevEng = hdev;
}

/*
 * DrvDisablePDEV
 *
 * Releases resources allocated in DrvEnablePDEV.  DrvDisableSurface is
 * always called before this if a surface was enabled.
 */
VOID APIENTRY
DrvDisablePDEV(
    IN DHPDEV dhpdev)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)dhpdev;

    /* Stop present worker before tearing down locks */
    CddStopPresentWorker(ppdev);
    CddDestroyLocking(ppdev);

    if (ppdev->DefaultPalette)
    {
        EngDeletePalette(ppdev->DefaultPalette);
    }

    if (ppdev->PaletteEntries != NULL)
    {
        EngFreeMem(ppdev->PaletteEntries);
    }

    EngFreeMem(ppdev);
}
