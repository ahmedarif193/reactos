/*
 * ReactOS Canonical Display Driver (CDD) - Screen / Mode Enumeration
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
 * Video mode querying, selection, and GDIINFO/DEVINFO initialization.
 * Palette initialization and hardware palette setting.
 *
 * This code parallels framebuf/screen.c and framebuf/palette.c but is
 * simplified: CDD does not need UEFI multi-output or acceleration backend
 * logic, as WDDM miniports handle those concerns.
 */

#include "cdd.h"

static LOGFONTW SystemFont = {
    16, 7, 0, 0, 700, 0, 0, 0,
    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
    DEFAULT_QUALITY, VARIABLE_PITCH | FF_DONTCARE, L"System"
};

static LOGFONTW AnsiVariableFont = {
    12, 9, 0, 0, 400, 0, 0, 0,
    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_STROKE_PRECIS,
    PROOF_QUALITY, VARIABLE_PITCH | FF_DONTCARE, L"MS Sans Serif"
};

static LOGFONTW AnsiFixedFont = {
    12, 9, 0, 0, 400, 0, 0, 0,
    ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_STROKE_PRECIS,
    PROOF_QUALITY, FIXED_PITCH | FF_DONTCARE, L"Courier"
};

/*
 * Standard 20-color base palette for 8-bpp modes.
 */
static const PALETTEENTRY CddBasePalette[20] =
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
 * CddGetAvailableModes
 *
 * Queries the miniport for supported video modes and filters out those
 * that CDD cannot handle (non-graphics, non-planar, unsupported bpp).
 */
DWORD
CddGetAvailableModes(
    HANDLE hDriver,
    PVIDEO_MODE_INFORMATION *ModeInfo,
    DWORD *ModeInfoSize)
{
    ULONG ulTemp;
    VIDEO_NUM_MODES Modes;
    PVIDEO_MODE_INFORMATION ModeInfoPtr;

    if (EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES,
                           NULL, 0, &Modes, sizeof(VIDEO_NUM_MODES), &ulTemp))
    {
        return 0;
    }

    if (Modes.NumModes == 0)
    {
        return 0;
    }

    *ModeInfoSize = Modes.ModeInformationLength;

    *ModeInfo = (PVIDEO_MODE_INFORMATION)EngAllocMem(
        0, Modes.NumModes * Modes.ModeInformationLength, ALLOC_TAG);

    if (*ModeInfo == NULL)
    {
        return 0;
    }

    if (EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_AVAIL_MODES,
                           NULL, 0, *ModeInfo,
                           Modes.NumModes * Modes.ModeInformationLength,
                           &ulTemp))
    {
        EngFreeMem(*ModeInfo);
        *ModeInfo = NULL;
        return 0;
    }

    /*
     * Reject modes that are not single-plane, not graphics, or not a
     * supported bit depth.  Mark rejected modes with Length = 0.
     */
    ulTemp = Modes.NumModes;
    ModeInfoPtr = *ModeInfo;

    while (ulTemp--)
    {
        if ((ModeInfoPtr->NumberOfPlanes != 1) ||
            !(ModeInfoPtr->AttributeFlags & VIDEO_MODE_GRAPHICS) ||
            ((ModeInfoPtr->BitsPerPlane != 8) &&
             (ModeInfoPtr->BitsPerPlane != 16) &&
             (ModeInfoPtr->BitsPerPlane != 24) &&
             (ModeInfoPtr->BitsPerPlane != 32)))
        {
            ModeInfoPtr->Length = 0;
        }

        ModeInfoPtr = (PVIDEO_MODE_INFORMATION)
            (((PUCHAR)ModeInfoPtr) + Modes.ModeInformationLength);
    }

    return Modes.NumModes;
}


/*
 * CddInitScreenInfo
 *
 * Selects a video mode and fills in GDIINFO/DEVINFO for the GDI engine.
 */
BOOL
CddInitScreenInfo(
    PCDD_PDEV ppdev,
    LPDEVMODEW pDevMode,
    PGDIINFO pGdiInfo,
    PDEVINFO pDevInfo)
{
    ULONG ModeCount;
    ULONG ModeInfoSize;
    PVIDEO_MODE_INFORMATION ModeInfo, ModeInfoPtr, SelectedMode = NULL;
    VIDEO_MODE_INFORMATION CurrentModeInfo;
    BOOLEAN HaveCurrentMode = FALSE;
    ULONG ReturnedLength = 0;
    VIDEO_COLOR_CAPABILITIES ColorCapabilities;
    ULONG Temp;

    ModeCount = CddGetAvailableModes(ppdev->hDriver, &ModeInfo, &ModeInfoSize);
    if (ModeCount == 0)
    {
        return FALSE;
    }

    /*
     * Select a video mode.  If the requested DEVMODE has zero dimensions,
     * try to match the current mode; otherwise find an exact match.
     */
    if (pDevMode->dmPelsWidth == 0 && pDevMode->dmPelsHeight == 0 &&
        pDevMode->dmBitsPerPel == 0 && pDevMode->dmDisplayFrequency == 0)
    {
        /* Try to query current mode from miniport */
        if (!EngDeviceIoControl(ppdev->hDriver,
                                IOCTL_VIDEO_QUERY_CURRENT_MODE,
                                NULL, 0,
                                &CurrentModeInfo, sizeof(CurrentModeInfo),
                                &ReturnedLength) &&
            ReturnedLength >= sizeof(CurrentModeInfo))
        {
            HaveCurrentMode = TRUE;
        }

        for (ULONG i = 0; i < ModeCount; ++i)
        {
            ModeInfoPtr = (PVIDEO_MODE_INFORMATION)(((PUCHAR)ModeInfo) + i * ModeInfoSize);
            if (ModeInfoPtr->Length == 0)
                continue;

            if (HaveCurrentMode)
            {
                if (ModeInfoPtr->ModeIndex == CurrentModeInfo.ModeIndex ||
                    (ModeInfoPtr->VisScreenWidth == CurrentModeInfo.VisScreenWidth &&
                     ModeInfoPtr->VisScreenHeight == CurrentModeInfo.VisScreenHeight &&
                     (ModeInfoPtr->BitsPerPlane * ModeInfoPtr->NumberOfPlanes) ==
                         (CurrentModeInfo.BitsPerPlane * CurrentModeInfo.NumberOfPlanes)))
                {
                    SelectedMode = ModeInfoPtr;
                    break;
                }
            }
            else if (!SelectedMode)
            {
                SelectedMode = ModeInfoPtr;
            }
        }

        /* Fallback: pick the first valid mode */
        if (!SelectedMode)
        {
            ModeInfoPtr = ModeInfo;
            for (ULONG i = 0; i < ModeCount; ++i)
            {
                if (ModeInfoPtr->Length != 0)
                {
                    SelectedMode = ModeInfoPtr;
                    break;
                }
                ModeInfoPtr = (PVIDEO_MODE_INFORMATION)(((PUCHAR)ModeInfoPtr) + ModeInfoSize);
            }
        }
    }
    else
    {
        /* Exact match requested */
        for (ULONG i = 0; i < ModeCount; ++i)
        {
            ModeInfoPtr = (PVIDEO_MODE_INFORMATION)(((PUCHAR)ModeInfo) + i * ModeInfoSize);
            if (ModeInfoPtr->Length == 0)
                continue;

            if (pDevMode->dmPelsWidth == ModeInfoPtr->VisScreenWidth &&
                pDevMode->dmPelsHeight == ModeInfoPtr->VisScreenHeight &&
                pDevMode->dmBitsPerPel == (ModeInfoPtr->BitsPerPlane *
                                           ModeInfoPtr->NumberOfPlanes) &&
                pDevMode->dmDisplayFrequency == ModeInfoPtr->Frequency)
            {
                SelectedMode = ModeInfoPtr;
                break;
            }
        }
    }

    if (SelectedMode == NULL)
    {
        CDD_DBG("No usable video mode (ModeCount=%lu)\n",
                (unsigned long)ModeCount);
        EngFreeMem(ModeInfo);
        return FALSE;
    }

    /* Populate PDEV fields from the selected mode */
    ppdev->ModeIndex = SelectedMode->ModeIndex;
    ppdev->ScreenWidth = SelectedMode->VisScreenWidth;
    ppdev->ScreenHeight = SelectedMode->VisScreenHeight;
    ppdev->ScreenDelta = SelectedMode->ScreenStride;
    ppdev->BitsPerPixel = (UCHAR)(SelectedMode->BitsPerPlane *
                                   SelectedMode->NumberOfPlanes);
    ppdev->RedMask = SelectedMode->RedMask;
    ppdev->GreenMask = SelectedMode->GreenMask;
    ppdev->BlueMask = SelectedMode->BlueMask;
    ppdev->SysmemFramebuffer =
        (SelectedMode->DriverSpecificAttributeFlags &
         CDD_DISP_DRIVERSPEC_SYSMEM_FB) ? TRUE : FALSE;

    /* Fill in GDIINFO */
    RtlZeroMemory(pGdiInfo, sizeof(GDIINFO));

    pGdiInfo->ulVersion = GDI_DRIVER_VERSION;
    pGdiInfo->ulTechnology = DT_RASDISPLAY;
    pGdiInfo->ulHorzSize = SelectedMode->XMillimeter;
    pGdiInfo->ulVertSize = SelectedMode->YMillimeter;
    pGdiInfo->ulHorzRes = SelectedMode->VisScreenWidth;
    pGdiInfo->ulVertRes = SelectedMode->VisScreenHeight;
    pGdiInfo->ulPanningHorzRes = SelectedMode->VisScreenWidth;
    pGdiInfo->ulPanningVertRes = SelectedMode->VisScreenHeight;
    pGdiInfo->cBitsPixel = SelectedMode->BitsPerPlane;
    pGdiInfo->cPlanes = SelectedMode->NumberOfPlanes;
    pGdiInfo->ulVRefresh = SelectedMode->Frequency;
    pGdiInfo->ulBltAlignment = 1;
    pGdiInfo->ulLogPixelsX = pDevMode->dmLogPixels;
    pGdiInfo->ulLogPixelsY = pDevMode->dmLogPixels;
    pGdiInfo->flTextCaps = TC_RA_ABLE;
    pGdiInfo->flRaster = 0;
    pGdiInfo->ulDACRed = SelectedMode->NumberRedBits;
    pGdiInfo->ulDACGreen = SelectedMode->NumberGreenBits;
    pGdiInfo->ulDACBlue = SelectedMode->NumberBlueBits;
    pGdiInfo->ulAspectX = 0x24;
    pGdiInfo->ulAspectY = 0x24;
    pGdiInfo->ulAspectXY = 0x33;
    pGdiInfo->xStyleStep = 1;
    pGdiInfo->yStyleStep = 1;
    pGdiInfo->denStyleStep = 3;
    pGdiInfo->ptlPhysOffset.x = 0;
    pGdiInfo->ptlPhysOffset.y = 0;
    pGdiInfo->szlPhysSize.cx = 0;
    pGdiInfo->szlPhysSize.cy = 0;

    /* Color capabilities */
    if (!EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_QUERY_COLOR_CAPABILITIES,
                            NULL, 0, &ColorCapabilities,
                            sizeof(VIDEO_COLOR_CAPABILITIES), &Temp))
    {
        pGdiInfo->ciDevice.Red.x = ColorCapabilities.RedChromaticity_x;
        pGdiInfo->ciDevice.Red.y = ColorCapabilities.RedChromaticity_y;
        pGdiInfo->ciDevice.Green.x = ColorCapabilities.GreenChromaticity_x;
        pGdiInfo->ciDevice.Green.y = ColorCapabilities.GreenChromaticity_y;
        pGdiInfo->ciDevice.Blue.x = ColorCapabilities.BlueChromaticity_x;
        pGdiInfo->ciDevice.Blue.y = ColorCapabilities.BlueChromaticity_y;
        pGdiInfo->ciDevice.AlignmentWhite.x = ColorCapabilities.WhiteChromaticity_x;
        pGdiInfo->ciDevice.AlignmentWhite.y = ColorCapabilities.WhiteChromaticity_y;
        pGdiInfo->ciDevice.AlignmentWhite.Y = ColorCapabilities.WhiteChromaticity_Y;
        if (ColorCapabilities.AttributeFlags & VIDEO_DEVICE_COLOR)
        {
            pGdiInfo->ciDevice.RedGamma = ColorCapabilities.RedGamma;
            pGdiInfo->ciDevice.GreenGamma = ColorCapabilities.GreenGamma;
            pGdiInfo->ciDevice.BlueGamma = ColorCapabilities.BlueGamma;
        }
        else
        {
            pGdiInfo->ciDevice.RedGamma = ColorCapabilities.WhiteGamma;
            pGdiInfo->ciDevice.GreenGamma = ColorCapabilities.WhiteGamma;
            pGdiInfo->ciDevice.BlueGamma = ColorCapabilities.WhiteGamma;
        }
    }
    else
    {
        pGdiInfo->ciDevice.Red.x = 6700;
        pGdiInfo->ciDevice.Red.y = 3300;
        pGdiInfo->ciDevice.Green.x = 2100;
        pGdiInfo->ciDevice.Green.y = 7100;
        pGdiInfo->ciDevice.Blue.x = 1400;
        pGdiInfo->ciDevice.Blue.y = 800;
        pGdiInfo->ciDevice.AlignmentWhite.x = 3127;
        pGdiInfo->ciDevice.AlignmentWhite.y = 3290;
        pGdiInfo->ciDevice.AlignmentWhite.Y = 0;
        pGdiInfo->ciDevice.RedGamma = 20000;
        pGdiInfo->ciDevice.GreenGamma = 20000;
        pGdiInfo->ciDevice.BlueGamma = 20000;
    }

    pGdiInfo->ciDevice.Red.Y = 0;
    pGdiInfo->ciDevice.Green.Y = 0;
    pGdiInfo->ciDevice.Blue.Y = 0;
    pGdiInfo->ciDevice.Cyan.x = 0;
    pGdiInfo->ciDevice.Cyan.y = 0;
    pGdiInfo->ciDevice.Cyan.Y = 0;
    pGdiInfo->ciDevice.Magenta.x = 0;
    pGdiInfo->ciDevice.Magenta.y = 0;
    pGdiInfo->ciDevice.Magenta.Y = 0;
    pGdiInfo->ciDevice.Yellow.x = 0;
    pGdiInfo->ciDevice.Yellow.y = 0;
    pGdiInfo->ciDevice.Yellow.Y = 0;
    pGdiInfo->ciDevice.MagentaInCyanDye = 0;
    pGdiInfo->ciDevice.YellowInCyanDye = 0;
    pGdiInfo->ciDevice.CyanInMagentaDye = 0;
    pGdiInfo->ciDevice.YellowInMagentaDye = 0;
    pGdiInfo->ciDevice.CyanInYellowDye = 0;
    pGdiInfo->ciDevice.MagentaInYellowDye = 0;
    pGdiInfo->ulDevicePelsDPI = 0;
    pGdiInfo->ulPrimaryOrder = PRIMARY_ORDER_CBA;
    pGdiInfo->ulHTPatternSize = HT_PATSIZE_4x4_M;
    pGdiInfo->flHTFlags = HT_FLAG_ADDITIVE_PRIMS;

    /* Fill in DEVINFO */
    RtlZeroMemory(pDevInfo, sizeof(DEVINFO));

    pDevInfo->flGraphicsCaps = 0;
    pDevInfo->lfDefaultFont = SystemFont;
    pDevInfo->lfAnsiVarFont = AnsiVariableFont;
    pDevInfo->lfAnsiFixFont = AnsiFixedFont;
    pDevInfo->cFonts = 0;
    pDevInfo->cxDither = 0;
    pDevInfo->cyDither = 0;
    pDevInfo->hpalDefault = 0;
    pDevInfo->flGraphicsCaps2 = 0;

    if (ppdev->BitsPerPixel == 8)
    {
        pGdiInfo->ulNumColors = 20;
        pGdiInfo->ulNumPalReg = 1 << ppdev->BitsPerPixel;
        pGdiInfo->ulHTOutputFormat = HT_FORMAT_8BPP;
        pDevInfo->flGraphicsCaps |= GCAPS_PALMANAGED;
        pDevInfo->iDitherFormat = BMF_8BPP;
        ppdev->PaletteShift = (UCHAR)(8 - pGdiInfo->ulDACRed);
    }
    else
    {
        pGdiInfo->ulNumColors = (ULONG)(-1);
        pGdiInfo->ulNumPalReg = 0;
        switch (ppdev->BitsPerPixel)
        {
            case 16:
                pGdiInfo->ulHTOutputFormat = HT_FORMAT_16BPP;
                pDevInfo->iDitherFormat = BMF_16BPP;
                break;
            case 24:
                pGdiInfo->ulHTOutputFormat = HT_FORMAT_24BPP;
                pDevInfo->iDitherFormat = BMF_24BPP;
                break;
            default:
                pGdiInfo->ulHTOutputFormat = HT_FORMAT_32BPP;
                pDevInfo->iDitherFormat = BMF_32BPP;
                break;
        }
    }

    EngFreeMem(ModeInfo);
    return TRUE;
}


/*
 * CddInitDefaultPalette
 */
BOOL
CddInitDefaultPalette(
    PCDD_PDEV ppdev,
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

        memcpy(ppdev->PaletteEntries, CddBasePalette, 10 * sizeof(PALETTEENTRY));
        memcpy(ppdev->PaletteEntries + 246, CddBasePalette + 10, 10 * sizeof(PALETTEENTRY));

        ppdev->DefaultPalette = pDevInfo->hpalDefault =
            EngCreatePalette(PAL_INDEXED, 256, (PULONG)ppdev->PaletteEntries, 0, 0, 0);
    }

    return ppdev->DefaultPalette != NULL;
}


/*
 * CddSetPaletteHw
 *
 * Programs the hardware palette through the miniport.
 */
BOOL APIENTRY
CddSetPaletteHw(
    IN DHPDEV dhpdev,
    IN PPALETTEENTRY ppalent,
    IN ULONG iStart,
    IN ULONG cColors)
{
    PVIDEO_CLUT pClut;
    ULONG ClutSize;

    ClutSize = sizeof(VIDEO_CLUT) + (cColors * sizeof(ULONG));
    pClut = EngAllocMem(0, ClutSize, ALLOC_TAG);
    if (!pClut)
        return FALSE;

    pClut->FirstEntry = iStart;
    pClut->NumEntries = cColors;
    memcpy(&pClut->LookupTable[0].RgbLong, ppalent, sizeof(ULONG) * cColors);

    if (((PCDD_PDEV)dhpdev)->PaletteShift)
    {
        while (cColors--)
        {
            pClut->LookupTable[cColors].RgbArray.Red >>= ((PCDD_PDEV)dhpdev)->PaletteShift;
            pClut->LookupTable[cColors].RgbArray.Green >>= ((PCDD_PDEV)dhpdev)->PaletteShift;
            pClut->LookupTable[cColors].RgbArray.Blue >>= ((PCDD_PDEV)dhpdev)->PaletteShift;
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

    if (EngDeviceIoControl(((PCDD_PDEV)dhpdev)->hDriver,
                           IOCTL_VIDEO_SET_COLOR_REGISTERS,
                           pClut, ClutSize, NULL, 0, &cColors))
    {
        EngFreeMem(pClut);
        return FALSE;
    }

    EngFreeMem(pClut);
    return TRUE;
}


/*
 * DrvSetPalette
 */
BOOL APIENTRY
DrvSetPalette(
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

    if (PALOBJ_cGetColors(ppalo, iStart, cColors, (PULONG)PaletteEntries) !=
        cColors)
    {
        EngFreeMem(PaletteEntries);
        return FALSE;
    }

    bRet = CddSetPaletteHw(dhpdev, PaletteEntries, iStart, cColors);
    EngFreeMem(PaletteEntries);
    return bRet;
}


/*
 * DrvGetModes
 *
 * Returns the list of available modes for the device.
 */
ULONG APIENTRY
DrvGetModes(
    IN HANDLE hDriver,
    IN ULONG cjSize,
    OUT DEVMODEW *pdm)
{
    ULONG ModeCount;
    ULONG ModeInfoSize;
    PVIDEO_MODE_INFORMATION ModeInfo, ModeInfoPtr;
    ULONG OutputSize;

    ModeCount = CddGetAvailableModes(hDriver, &ModeInfo, &ModeInfoSize);
    if (ModeCount == 0)
    {
        return 0;
    }

    if (pdm == NULL)
    {
        EngFreeMem(ModeInfo);
        return ModeCount * sizeof(DEVMODEW);
    }

    OutputSize = 0;
    ModeInfoPtr = ModeInfo;

    for (ULONG i = 0; i < ModeCount; ++i)
    {
        if (ModeInfoPtr->Length != 0)
        {
            if (OutputSize + sizeof(DEVMODEW) > cjSize)
                break;

            memset(pdm, 0, sizeof(DEVMODEW));
            memcpy(pdm->dmDeviceName, DEVICE_NAME, sizeof(DEVICE_NAME));
            pdm->dmSpecVersion = DM_SPECVERSION;
            pdm->dmDriverVersion = DM_SPECVERSION;
            pdm->dmSize = sizeof(DEVMODEW);
            pdm->dmDriverExtra = 0;
            pdm->dmBitsPerPel = ModeInfoPtr->NumberOfPlanes * ModeInfoPtr->BitsPerPlane;
            pdm->dmPelsWidth = ModeInfoPtr->VisScreenWidth;
            pdm->dmPelsHeight = ModeInfoPtr->VisScreenHeight;
            pdm->dmDisplayFrequency = ModeInfoPtr->Frequency;
            pdm->dmDisplayFlags = 0;
            pdm->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT |
                            DM_DISPLAYFREQUENCY | DM_DISPLAYFLAGS;

            pdm = (LPDEVMODEW)(((ULONG_PTR)pdm) + sizeof(DEVMODEW));
            OutputSize += sizeof(DEVMODEW);
        }

        ModeInfoPtr = (PVIDEO_MODE_INFORMATION)
            (((ULONG_PTR)ModeInfoPtr) + ModeInfoSize);
    }

    EngFreeMem(ModeInfo);
    return OutputSize;
}
