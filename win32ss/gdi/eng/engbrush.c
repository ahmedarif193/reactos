/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS kernel
 * PURPOSE:           GDI Driver Brush Functions
 * FILE:              win32ss/gdi/eng/engbrush.c
 * PROGRAMER:         Jason Filby
 *                    Timo Kreuzer
 */

#include <win32k.h>

DBG_DEFAULT_CHANNEL(EngBrush);

static const ULONG gaulHatchBrushes[HS_DDI_MAX][8] =
{
    {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF}, /* HS_HORIZONTAL */
    {0xF7, 0xF7, 0xF7, 0xF7, 0xF7, 0xF7, 0xF7, 0xF7}, /* HS_VERTICAL   */
    {0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F}, /* HS_FDIAGONAL  */
    {0x7F, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD, 0xFE}, /* HS_BDIAGONAL  */
    {0xF7, 0xF7, 0xF7, 0xF7, 0x00, 0xF7, 0xF7, 0xF7}, /* HS_CROSS      */
    {0x7E, 0xBD, 0xDB, 0xE7, 0xE7, 0xDB, 0xBD, 0x7E}  /* HS_DIAGCROSS  */
};

HSURF gahsurfHatch[HS_DDI_MAX];

static const BYTE gajBayer8x8[8][8] =
{
    {  0, 32,  8, 40,  2, 34, 10, 42 },
    { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44,  4, 36, 14, 46,  6, 38 },
    { 60, 28, 52, 20, 62, 30, 54, 22 },
    {  3, 35, 11, 43,  1, 33,  9, 41 },
    { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47,  7, 39, 13, 45,  5, 37 },
    { 63, 31, 55, 23, 61, 29, 53, 21 }
};

static const BYTE gajBayer16x16[16][16] =
{
    {   0, 128,  32, 160,   8, 136,  40, 168,   2, 130,  34, 162,  10, 138,  42, 170 },
    { 192,  64, 224,  96, 200,  72, 232, 104, 194,  66, 226,  98, 202,  74, 234, 106 },
    {  48, 176,  16, 144,  56, 184,  24, 152,  50, 178,  18, 146,  58, 186,  26, 154 },
    { 240, 112, 208,  80, 248, 120, 216,  88, 242, 114, 210,  82, 250, 122, 218,  90 },
    {  12, 140,  44, 172,   4, 132,  36, 164,  14, 142,  46, 174,   6, 134,  38, 166 },
    { 204,  76, 236, 108, 196,  68, 228, 100, 206,  78, 238, 110, 198,  70, 230, 102 },
    {  60, 188,  28, 156,  52, 180,  20, 148,  62, 190,  30, 158,  54, 182,  22, 150 },
    { 252, 124, 220,  92, 244, 116, 212,  84, 254, 126, 222,  94, 246, 118, 214,  86 },
    {   3, 131,  35, 163,  11, 139,  43, 171,   1, 129,  33, 161,   9, 137,  41, 169 },
    { 195,  67, 227,  99, 203,  75, 235, 107, 193,  65, 225,  97, 201,  73, 233, 105 },
    {  51, 179,  19, 147,  59, 187,  27, 155,  49, 177,  17, 145,  57, 185,  25, 153 },
    { 243, 115, 211,  83, 251, 123, 219,  91, 241, 113, 209,  81, 249, 121, 217,  89 },
    {  15, 143,  47, 175,   7, 135,  39, 167,  13, 141,  45, 173,   5, 133,  37, 165 },
    { 207,  79, 239, 111, 199,  71, 231, 103, 205,  77, 237, 109, 197,  69, 229, 101 },
    {  63, 191,  31, 159,  55, 183,  23, 151,  61, 189,  29, 157,  53, 181,  21, 149 },
    { 255, 127, 223,  95, 247, 119, 215,  87, 253, 125, 221,  93, 245, 117, 213,  85 }
};

/** Internal functions ********************************************************/

CODE_SEG("INIT")
NTSTATUS
NTAPI
InitBrushImpl(VOID)
{
    ULONG i;
    SIZEL sizl = {8, 8};

    /* Loop all hatch styles */
    for (i = 0; i < HS_DDI_MAX; i++)
    {
        /* Create a default hatch bitmap */
        gahsurfHatch[i] = (HSURF)EngCreateBitmap(sizl,
                                                 0,
                                                 BMF_1BPP,
                                                 0,
                                                 (PVOID)gaulHatchBrushes[i]);
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
EBRUSHOBJ_vInit(EBRUSHOBJ *pebo,
    PBRUSH pbrush,
    PSURFACE psurf,
    COLORREF crBackgroundClr,
    COLORREF crForegroundClr,
    PPALETTE ppalDC)
{
    ASSERT(pebo);
    ASSERT(pbrush);

    pebo->BrushObject.flColorType = 0;
    pebo->BrushObject.pvRbrush = NULL;
    pebo->pbrush = pbrush;
    pebo->pengbrush = NULL;
    pebo->flattrs = pbrush->flAttrs;
    pebo->psoMask = NULL;

    if (!(pbrush->flAttrs & (BR_IS_PEN | BR_IS_OLDSTYLEPEN)))
        pebo->flattrs |= BR_DITHER_OK;

    /* Initialize 1 bpp fore and back colors */
    pebo->crCurrentBack = crBackgroundClr;
    pebo->crCurrentText = crForegroundClr;

    pebo->psurfTrg = psurf;
    /* We are initializing for a new memory DC */
    if(!pebo->psurfTrg)
        pebo->psurfTrg = psurfDefaultBitmap;
    ASSERT(pebo->psurfTrg);
    ASSERT(pebo->psurfTrg->ppal);

    /* Initialize palettes */
    pebo->ppalSurf = pebo->psurfTrg->ppal;
    GDIOBJ_vReferenceObjectByPointer(&pebo->ppalSurf->BaseObject);
    pebo->ppalDC = ppalDC;
    if(!pebo->ppalDC)
        pebo->ppalDC = gppalDefault;
    GDIOBJ_vReferenceObjectByPointer(&pebo->ppalDC->BaseObject);
    pebo->ppalDIB = NULL;

    pebo->ulDCPalTime = pebo->ppalDC->ulTime;
    pebo->crCurrentBack = PALETTE_crResolveColor(pebo->ppalDC, pebo->ppalSurf, crBackgroundClr);
    pebo->crCurrentText = PALETTE_crResolveColor(pebo->ppalDC, pebo->ppalSurf, crForegroundClr);

    if (pbrush->flAttrs & BR_IS_NULL)
    {
        /* NULL brushes don't need a color */
        pebo->BrushObject.iSolidColor = 0;
    }
    else if (pbrush->flAttrs & BR_IS_SOLID)
    {
        /* Set the RGB color */
        EBRUSHOBJ_vSetSolidRGBColor(pebo, pbrush->BrushAttr.lbColor);
    }
    else
    {
        /* This is a pattern brush that needs realization */
        pebo->BrushObject.iSolidColor = 0xFFFFFFFF;

        /* Use foreground color of hatch brushes */
        if (pbrush->flAttrs & BR_IS_HATCH)
            pebo->crCurrentText = PALETTE_crResolveColor(pebo->ppalDC, pebo->ppalSurf, pbrush->BrushAttr.lbColor);
    }
}

VOID
NTAPI
EBRUSHOBJ_vInitFromDC(EBRUSHOBJ *pebo,
    PBRUSH pbrush, PDC pdc)
{
    EBRUSHOBJ_vInit(pebo, pbrush, pdc->dclevel.pSurface,
        pdc->pdcattr->crBackgroundClr, pdc->pdcattr->crForegroundClr,
        pdc->dclevel.ppal);
}

VOID
FASTCALL
EBRUSHOBJ_vSetSolidRGBColor(EBRUSHOBJ *pebo, COLORREF crColor)
{
    ULONG iSolidColor;
    EXLATEOBJ exlo;
    COLORREF crOriginal = crColor;

    /* Never use with non-solid brushes */
    ASSERT(pebo->flattrs & BR_IS_SOLID);

    if (pebo->pengbrush)
    {
        SURFACE_ShareUnlockSurface(pebo->pengbrush);
        pebo->pengbrush = NULL;
    }

    if (crColor & 0x01000000)
    {
        ULONG iIndex = crColor & 0xFFFF;

        if (iIndex >= pebo->ppalDC->NumColors)
            iIndex = 0;
        crColor = PALETTE_ulGetRGBColorFromIndex(pebo->ppalDC, iIndex);
    }
    else if ((crColor & 0xFFFF0000) == 0x10FF0000)
    {
        ULONG iIndex = crColor & 0xFF;

        if (!(pebo->ppalSurf->flFlags & PAL_INDEXED) ||
            iIndex >= pebo->ppalSurf->NumColors)
        {
            iIndex = 0;
            crColor = 0;
        }
        else
        {
            crColor = PALETTE_ulGetRGBColorFromIndex(pebo->ppalSurf, iIndex);
        }
        pebo->crRealize = crColor;
        pebo->ulRGBColor = crColor;
        pebo->BrushObject.iSolidColor = iIndex;
        return;
    }

    /* Set the RGB color */
    crColor &= 0xFFFFFF;
    pebo->crRealize = crColor;
    pebo->ulRGBColor = crColor;

    /* Special handling for mono-surfaces */
    if (pebo->ppalSurf->flFlags & PAL_MONOCHROME)
    {
        if ((pebo->flattrs & BR_DITHER_OK) &&
            !(pebo->ppalSurf->flFlags & PAL_DIBSECTION) &&
            (crColor != RGB(0, 0, 0)) &&
            (crColor != RGB(0xFF, 0xFF, 0xFF)))
        {
            pebo->BrushObject.iSolidColor = 0xFFFFFFFF;
            return;
        }

        /* Determine the indices for back and fore color */
        ULONG iBackIndex =
            PALETTE_ulGetNearestPaletteIndex(pebo->ppalSurf, pebo->crCurrentBack);
        ULONG iForeIndex = iBackIndex ^ 1;

        /* Get the translated back color */
        ULONG rgbBack = PALETTE_ulGetRGBColorFromIndex(pebo->ppalSurf, iBackIndex);

        /* Match the pen color against RGB and translated background color */
        if ((crColor == rgbBack) || (crOriginal == pebo->crCurrentBack))
                pebo->BrushObject.iSolidColor = iBackIndex;
        else
            pebo->BrushObject.iSolidColor = iForeIndex;

        pebo->flattrs |= BR_NEED_BK_CLR;
    }
    else
    {
        /* Initialize an XLATEOBJ RGB -> surface */
        EXLATEOBJ_vInitialize(&exlo,
                              &gpalRGB,
                              pebo->ppalSurf,
                              pebo->crCurrentBack,
                              0,
                              0);

        /* Translate the brush color to the target format */
        iSolidColor = XLATEOBJ_iXlate(&exlo.xlo, crColor);
        pebo->BrushObject.iSolidColor = iSolidColor;

        /* Clean up the XLATEOBJ */
        EXLATEOBJ_vCleanup(&exlo);
    }
}

VOID
NTAPI
EBRUSHOBJ_vCleanup(EBRUSHOBJ *pebo)
{
    /* Check if there's a GDI realisation */
    if (pebo->pengbrush)
    {
        /* Unlock the bitmap again */
        SURFACE_ShareUnlockSurface(pebo->pengbrush);
        pebo->pengbrush = NULL;
    }

    /* Check if there's a driver's realisation */
    if (pebo->BrushObject.pvRbrush)
    {
        /* Free allocated driver memory */
        EngFreeMem(pebo->BrushObject.pvRbrush);
        pebo->BrushObject.pvRbrush = NULL;
    }

    if (pebo->psoMask != NULL)
    {
        SURFACE_ShareUnlockSurface(pebo->psoMask);
        pebo->psoMask = NULL;
    }

    /* Dereference the palettes */
    if (pebo->ppalSurf)
    {
        PALETTE_ShareUnlockPalette(pebo->ppalSurf);
    }
    if (pebo->ppalDC)
    {
        PALETTE_ShareUnlockPalette(pebo->ppalDC);
    }
    if (pebo->ppalDIB)
    {
        PALETTE_ShareUnlockPalette(pebo->ppalDIB);
    }
}

VOID
NTAPI
EBRUSHOBJ_vUpdateFromDC(
    EBRUSHOBJ *pebo,
    PBRUSH pbrush,
    PDC pdc)
{
    /* Cleanup the brush */
    EBRUSHOBJ_vCleanup(pebo);

    /* Reinitialize */
    EBRUSHOBJ_vInitFromDC(pebo, pbrush, pdc);
}

/**
 * This function is not exported, because it makes no sense for
 * The driver to punt back to this function */
BOOL
APIENTRY
EngRealizeBrush(
    BRUSHOBJ *pbo,
    SURFOBJ  *psoDst,
    SURFOBJ  *psoPattern,
    SURFOBJ  *psoMask,
    XLATEOBJ *pxlo,
    ULONG    iHatch)
{
    EBRUSHOBJ *pebo;
    HBITMAP hbmpRealize;
    SURFOBJ *psoRealize;
    PSURFACE psurfRealize;
    POINTL ptlSrc = {0, 0};
    RECTL rclDest;
    ULONG lWidth;

    /* Calculate width in bytes of the realized brush */
    lWidth = WIDTH_BYTES_ALIGN32(psoPattern->sizlBitmap.cx,
                                  BitsPerFormat(psoDst->iBitmapFormat));

    /* Allocate a bitmap */
    hbmpRealize = EngCreateBitmap(psoPattern->sizlBitmap,
                                  lWidth,
                                  psoDst->iBitmapFormat,
                                  BMF_NOZEROINIT,
                                  NULL);
    if (!hbmpRealize)
    {
        return FALSE;
    }

    /* Lock the bitmap */
    psurfRealize = SURFACE_ShareLockSurface(hbmpRealize);

    /* Already delete the pattern bitmap (will be kept until dereferenced) */
    EngDeleteSurface((HSURF)hbmpRealize);

    if (!psurfRealize)
    {
        return FALSE;
    }

    /* Copy the bits to the new format bitmap */
    rclDest.left = rclDest.top = 0;
    rclDest.right = psoPattern->sizlBitmap.cx;
    rclDest.bottom = psoPattern->sizlBitmap.cy;
    psoRealize = &psurfRealize->SurfObj;
    EngCopyBits(psoRealize, psoPattern, NULL, pxlo, &rclDest, &ptlSrc);


    pebo = CONTAINING_RECORD(pbo, EBRUSHOBJ, BrushObject);
    pebo->pengbrush = (PVOID)psurfRealize;

    return TRUE;
}

static
PPALETTE
FixupDIBBrushPalette(
    _In_ PPALETTE ppalDIB,
    _In_ PPALETTE ppalDC)
{
    PPALETTE ppalNew;
    ULONG i, iPalIndex, crColor;

    /* Allocate a new palette */
    ppalNew = PALETTE_AllocPalette(PAL_INDEXED,
                                   ppalDIB->NumColors,
                                   NULL,
                                   0,
                                   0,
                                   0);
    if (ppalNew == NULL)
    {
        ERR("Failed to allcate palette for brush\n");
        return NULL;
    }

    /* Loop all colors */
    for (i = 0; i < ppalDIB->NumColors; i++)
    {
        /* Get the RGB color, which is the index into the DC palette */
        iPalIndex = PALETTE_ulGetRGBColorFromIndex(ppalDIB, i);

        /* Roll over when index is too big */
        iPalIndex %= ppalDC->NumColors;

        /* Set the indexed DC color as the new color */
        crColor = PALETTE_ulGetRGBColorFromIndex(ppalDC, iPalIndex);
        PALETTE_vSetRGBColorForIndex(ppalNew, i, crColor);
    }

    /* Return the new palette */
    return ppalNew;
}

static
BOOL
EBRUSHOBJ_bRealizeDitheredBrush(
    _In_ EBRUSHOBJ *pebo,
    _In_ PFN_DrvRealizeBrush pfnRealizeBrush,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ PSURFACE psurfPattern,
    _In_opt_ PPALETTE ppalPattern,
    _In_opt_ PSURFACE psurfHatch)
{
    SIZEL sizl = {8, 8};
    HBITMAP hbmDither;
    PSURFACE psurfDither;
    EXLATEOBJ exloRGB, exlo;
    PFN_DIB_GetPixel pfnGetPixel = NULL;
    ULONG iWhite, iBlack, iFore = 0, iBack = 0, ulColor, ulGrey, iPixel;
    LONG x, y;
    BOOL bWhite, bResult;

    if (psurfPattern)
        sizl = psurfPattern->SurfObj.sizlBitmap;

    if (psurfHatch)
    {
        iBack = PALETTE_ulGetNearestPaletteIndex(pebo->ppalSurf, pebo->crCurrentBack);
        if (pebo->crCurrentText == PALETTE_ulGetRGBColorFromIndex(pebo->ppalSurf, 0))
            iFore = 0;
        else if (pebo->crCurrentText == PALETTE_ulGetRGBColorFromIndex(pebo->ppalSurf, 1))
            iFore = 1;
        else
            iFore = (pebo->crCurrentText == pebo->crCurrentBack) ? iBack : !iBack;
        iBack = (pebo->crCurrentText != pebo->crCurrentBack) ? !iFore : iFore;
        pfnGetPixel = DibFunctionsForBitmapFormat[BMF_1BPP].DIB_GetPixel;
    }

    hbmDither = EngCreateBitmap(sizl, 0, BMF_1BPP, BMF_TOPDOWN, NULL);
    if (!hbmDither)
        return FALSE;

    psurfDither = SURFACE_ShareLockSurface(hbmDither);
    if (!psurfDither)
    {
        EngDeleteSurface((HSURF)hbmDither);
        return FALSE;
    }

    iWhite = PALETTE_ulGetNearestPaletteIndex(pebo->ppalSurf, RGB(0xFF, 0xFF, 0xFF));
    iBlack = PALETTE_ulGetNearestPaletteIndex(pebo->ppalSurf, RGB(0, 0, 0));

    if (psurfPattern)
    {
        EXLATEOBJ_vInitialize(&exloRGB, ppalPattern, &gpalRGB, 0, CLR_INVALID, 0);
        pfnGetPixel = DibFunctionsForBitmapFormat[psurfPattern->SurfObj.iBitmapFormat].DIB_GetPixel;
    }

    for (y = 0; y < sizl.cy; y++)
    {
        for (x = 0; x < sizl.cx; x++)
        {
            if (psurfHatch)
            {
                iPixel = pfnGetPixel(&psurfHatch->SurfObj, x, y) ? iBack : iFore;
                DibFunctionsForBitmapFormat[BMF_1BPP].DIB_PutPixel(&psurfDither->SurfObj, x, y, iPixel);
                continue;
            }
            else if (psurfPattern)
            {
                ulColor = XLATEOBJ_iXlate(&exloRGB.xlo, pfnGetPixel(&psurfPattern->SurfObj, x, y));
                ulGrey = 77 * GetRValue(ulColor) + 151 * GetGValue(ulColor) + 28 * GetBValue(ulColor);
                bWhite = (ulGrey + 255 * gajBayer16x16[y % 16][x % 16]) > 255 * 255;
            }
            else
            {
                ulGrey = (77 * GetRValue(pebo->crRealize) + 151 * GetGValue(pebo->crRealize) +
                          28 * GetBValue(pebo->crRealize)) >> 8;
                bWhite = (((ulGrey + 1) >> 2) + gajBayer8x8[7 - y][x]) > 63;
            }

            DibFunctionsForBitmapFormat[BMF_1BPP].DIB_PutPixel(&psurfDither->SurfObj, x, y, bWhite ? iWhite : iBlack);
        }
    }

    if (psurfPattern)
        EXLATEOBJ_vCleanup(&exloRGB);

    EXLATEOBJ_vInitialize(&exlo, pebo->ppalSurf, pebo->ppalSurf, 0, 0, 0);

    bResult = pfnRealizeBrush(&pebo->BrushObject,
                              &pebo->psurfTrg->SurfObj,
                              &psurfDither->SurfObj,
                              psoMask,
                              &exlo.xlo,
                              -1);

    EXLATEOBJ_vCleanup(&exlo);

    SURFACE_ShareUnlockSurface(psurfDither);
    EngDeleteSurface((HSURF)hbmDither);

    return bResult;
}

BOOL
NTAPI
EBRUSHOBJ_bRealizeBrush(EBRUSHOBJ *pebo, BOOL bCallDriver)
{
    BOOL bResult;
    PFN_DrvRealizeBrush pfnRealizeBrush = NULL;
    PSURFACE psurfPattern;
    SURFOBJ *psoMask;
    PPDEVOBJ ppdev;
    EXLATEOBJ exlo;
    PPALETTE ppalPattern;
    PBRUSH pbr = pebo->pbrush;
    HBITMAP hbmPattern;
    ULONG iHatch;

    /* All EBRUSHOBJs have a surface, see EBRUSHOBJ_vInit */
    ASSERT(pebo->psurfTrg);

    ppdev = (PPDEVOBJ)pebo->psurfTrg->SurfObj.hdev;
    if (!ppdev)
        ppdev = gpmdev->ppdevGlobal;

    if (bCallDriver)
    {
        /* Get the Drv function */
        pfnRealizeBrush = ppdev->DriverFunctions.RealizeBrush;
        if (pfnRealizeBrush == NULL)
        {
            ERR("No DrvRealizeBrush. Cannot realize brush\n");
            return FALSE;
        }

        /* Get the mask */
        psoMask = EBRUSHOBJ_psoMask(pebo);
    }
    else
    {
        /* Use the Eng function */
        pfnRealizeBrush = EngRealizeBrush;

        /* We don't handle the mask bitmap here. We do this only on demand */
        psoMask = NULL;
    }

    if ((pbr->flAttrs & BR_IS_SOLID) && (pebo->BrushObject.iSolidColor == 0xFFFFFFFF))
    {
        return EBRUSHOBJ_bRealizeDitheredBrush(pebo, pfnRealizeBrush, psoMask, NULL, NULL, NULL);
    }

    /* Check if this is a hatch brush */
    if (pbr->flAttrs & BR_IS_HATCH)
    {
        /* Get the hatch brush pattern from the PDEV */
        hbmPattern = (HBITMAP)ppdev->ahsurf[pbr->iHatch];
        iHatch = pbr->iHatch;
    }
    else
    {
        /* Use the brushes pattern */
        hbmPattern = pbr->hbmPattern;
        iHatch = -1;
    }

    psurfPattern = SURFACE_ShareLockSurface(hbmPattern);
    ASSERT(psurfPattern);
    ASSERT(psurfPattern->ppal);

    /* DIB brushes with DIB_PAL_COLORS usage need a new palette */
    if (pbr->flAttrs & BR_IS_DIBPALCOLORS)
    {
        /* Create a palette with the colors from the DC */
        ppalPattern = FixupDIBBrushPalette(psurfPattern->ppal, pebo->ppalDC);
        if (ppalPattern == NULL)
        {
            ERR("FixupDIBBrushPalette() failed.\n");
            return FALSE;
        }

        pebo->ppalDIB = ppalPattern;
    }
    else
    {
        /* The palette is already as it should be */
        ppalPattern = psurfPattern->ppal;
    }

    if ((pbr->flAttrs & BR_IS_HATCH) &&
        (pebo->ppalSurf->flFlags & PAL_MONOCHROME))
    {
        bResult = EBRUSHOBJ_bRealizeDitheredBrush(pebo, pfnRealizeBrush, psoMask, NULL, NULL, psurfPattern);
        SURFACE_ShareUnlockSurface(psurfPattern);
        return bResult;
    }

    if ((pebo->psurfTrg->SurfObj.iBitmapFormat == BMF_1BPP) &&
        (pbr->flAttrs & (BR_IS_BITMAP | BR_IS_DIB)) &&
        (psurfPattern->SurfObj.iBitmapFormat >= BMF_1BPP) &&
        (psurfPattern->SurfObj.iBitmapFormat <= BMF_32BPP) &&
        !((pbr->flAttrs & BR_IS_BITMAP) &&
          (psurfPattern->SurfObj.iBitmapFormat == BMF_1BPP) &&
          !(psurfPattern->ppal->flFlags & PAL_DIBSECTION)))
    {
        bResult = EBRUSHOBJ_bRealizeDitheredBrush(pebo, pfnRealizeBrush, psoMask, psurfPattern, ppalPattern, NULL);
        SURFACE_ShareUnlockSurface(psurfPattern);
        return bResult;
    }

    /* Initialize XLATEOBJ for the brush */
    EXLATEOBJ_vInitialize(&exlo,
                          ppalPattern,
                          pebo->psurfTrg->ppal,
                          0,
                          pebo->crCurrentBack,
                          pebo->crCurrentText);

    /* Create the realization */
    bResult = pfnRealizeBrush(&pebo->BrushObject,
                              &pebo->psurfTrg->SurfObj,
                              &psurfPattern->SurfObj,
                              psoMask,
                              &exlo.xlo,
                              iHatch);

    /* Cleanup the XLATEOBJ */
    EXLATEOBJ_vCleanup(&exlo);

    /* Unlock surface */
    SURFACE_ShareUnlockSurface(psurfPattern);

    return bResult;
}

PVOID
NTAPI
EBRUSHOBJ_pvGetEngBrush(EBRUSHOBJ *pebo)
{
    BOOL bResult;

    if (!pebo->pengbrush)
    {
        bResult = EBRUSHOBJ_bRealizeBrush(pebo, FALSE);
        if (!bResult)
        {
            if (pebo->pengbrush)
                EngDeleteSurface(pebo->pengbrush);
            pebo->pengbrush = NULL;
        }
    }

    return pebo->pengbrush;
}

SURFOBJ*
NTAPI
EBRUSHOBJ_psoPattern(EBRUSHOBJ *pebo)
{
    PSURFACE psurfPattern;

    psurfPattern = EBRUSHOBJ_pvGetEngBrush(pebo);

    return psurfPattern ? &psurfPattern->SurfObj : NULL;
}

SURFOBJ*
NTAPI
EBRUSHOBJ_psoMask(EBRUSHOBJ *pebo)
{
    HBITMAP hbmMask;
    PSURFACE psurfMask;
    PPDEVOBJ ppdev;

    /* Check if we don't have a mask yet */
    if (pebo->psoMask == NULL)
    {
        /* Check if this is a hatch brush */
        if (pebo->flattrs & BR_IS_HATCH)
        {
            /* Get the PDEV */
            ppdev = (PPDEVOBJ)pebo->psurfTrg->SurfObj.hdev;
            if (!ppdev)
                ppdev = gpmdev->ppdevGlobal;

            /* Use the hatch bitmap as the mask */
            hbmMask = (HBITMAP)ppdev->ahsurf[pebo->pbrush->iHatch];
            psurfMask = SURFACE_ShareLockSurface(hbmMask);
            if (psurfMask == NULL)
            {
                ERR("Failed to lock hatch brush for PDEV %p, iHatch %lu\n",
                    ppdev, pebo->pbrush->iHatch);
                return NULL;
            }

            NT_ASSERT(psurfMask->SurfObj.iBitmapFormat == BMF_1BPP);
            pebo->psoMask = &psurfMask->SurfObj;
        }
    }

    return pebo->psoMask;
}

/** Exported DDI functions ****************************************************/

/*
 * @implemented
 */
PVOID APIENTRY
BRUSHOBJ_pvAllocRbrush(
    IN BRUSHOBJ *pbo,
    IN ULONG cj)
{
    pbo->pvRbrush = EngAllocMem(0, cj, GDITAG_RBRUSH);
    return pbo->pvRbrush;
}

/*
 * @implemented
 */
PVOID APIENTRY
BRUSHOBJ_pvGetRbrush(
    IN BRUSHOBJ *pbo)
{
    EBRUSHOBJ *pebo = CONTAINING_RECORD(pbo, EBRUSHOBJ, BrushObject);
    BOOL bResult;

    if (!pbo->pvRbrush)
    {
        bResult = EBRUSHOBJ_bRealizeBrush(pebo, TRUE);
        if (!bResult)
        {
            if (pbo->pvRbrush)
            {
                EngFreeMem(pbo->pvRbrush);
                pbo->pvRbrush = NULL;
            }
        }
    }

    return pbo->pvRbrush;
}

/*
 * @implemented
 */
ULONG APIENTRY
BRUSHOBJ_ulGetBrushColor(
    IN BRUSHOBJ *pbo)
{
    EBRUSHOBJ *pebo = CONTAINING_RECORD(pbo, EBRUSHOBJ, BrushObject);
    return pebo->ulRGBColor;
}

/* EOF */
