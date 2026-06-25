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

/*
 * Resolve a logical COLORREF that may carry a PALETTEINDEX / PALETTERGB /
 * DIBINDEX flag into a plain RGB color, using the DC palette for PALETTEINDEX.
 * This mirrors TranslateCOLORREF() (win32ss/gdi/ntgdi/dcutil.c) and is needed
 * so that the fore/back colors fed to a pattern (e.g. hatch) brush realization
 * are real RGB values rather than raw logical-color magic values.
 */
static
COLORREF
EBRUSHOBJ_crResolveColorRef(EBRUSHOBJ *pebo, COLORREF crColor)
{
    ULONG index;

    if ((crColor & 0xFF000000) == 0)
    {
        /* Plain RGB color */
        return crColor;
    }
    else if (crColor & 0x01000000)
    {
        /* PALETTEINDEX: translate to RGB using the DC palette */
        index = crColor & 0xFFFF;
        if (!pebo->ppalDC || index >= pebo->ppalDC->NumColors)
            index = 0;
        if (pebo->ppalDC)
            return PALETTE_ulGetRGBColorFromIndex(pebo->ppalDC, index);
        return 0;
    }
    else if (crColor & 0x02000000)
    {
        /* PALETTERGB: use the raw RGB value */
        return crColor & 0x00FFFFFF;
    }
    else if ((crColor & 0x10FF0000) == 0x10FF0000)
    {
        /* DIBINDEX: resolve against the target surface palette when indexed */
        if (pebo->ppalSurf && (pebo->ppalSurf->flFlags & PAL_INDEXED))
        {
            index = crColor & 0xFF;
            if (index >= pebo->ppalSurf->NumColors)
                index = 0;
            return PALETTE_ulGetRGBColorFromIndex(pebo->ppalSurf, index);
        }
        return 0;
    }

    return crColor & 0x00FFFFFF;
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

    /* Remember the palette modification times this realization is based on, so
     * a later palette change (e.g. SetPaletteEntries) can be detected and the
     * brush re-realized. */
    pebo->ulDCPalTime = pebo->ppalDC->ulTime;
    pebo->ulSurfPalTime = pebo->ppalSurf->ulTime;

    /* Initialize 1 bpp fore and back colors. These feed pattern brush
     * realization (e.g. hatch fore/back), so resolve PALETTEINDEX etc. to RGB
     * now that the palettes are set up. */
    pebo->crCurrentBack = EBRUSHOBJ_crResolveColorRef(pebo, crBackgroundClr);
    pebo->crCurrentText = EBRUSHOBJ_crResolveColorRef(pebo, crForegroundClr);

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
            pebo->crCurrentText =
                EBRUSHOBJ_crResolveColorRef(pebo, pbrush->BrushAttr.lbColor);
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
    ULONG index;
    EXLATEOBJ exlo;

    /* Never use with non-solid brushes */
    ASSERT(pebo->flattrs & BR_IS_SOLID);

    if ((crColor & 0xFF000000) == 0)
    {
        /* RGB color */
    }
    else if (crColor & 0x01000000)
    {
        index = crColor & 0xFFFF;
        if (index >= pebo->ppalDC->NumColors)
            index = 0;
        crColor = PALETTE_ulGetRGBColorFromIndex(pebo->ppalDC, index);
    }
    else if (crColor & 0x02000000)
    {
        crColor &= 0x00FFFFFF;
    }
    else if ((crColor & 0x10FF0000) == 0x10FF0000)
    {
        if (pebo->ppalSurf->flFlags & PAL_INDEXED)
        {
            index = crColor & 0xFF;
            if (index >= pebo->ppalSurf->NumColors)
                index = 0;
            pebo->crRealize = PALETTE_ulGetRGBColorFromIndex(pebo->ppalSurf, index);
            pebo->ulRGBColor = pebo->crRealize;
            pebo->BrushObject.iSolidColor = index;
            return;
        }
        crColor = 0;
    }
    else
    {
        crColor &= 0x00FFFFFF;
    }

    pebo->crRealize = crColor;
    pebo->ulRGBColor = crColor;

    /* Special handling for mono-surfaces */
    if (pebo->ppalSurf->flFlags & PAL_MONOCHROME)
    {
        /* Determine the indices for back and fore color */
        ULONG iBackIndex =
            PALETTE_ulGetNearestPaletteIndex(pebo->ppalSurf, pebo->crCurrentBack);
        ULONG iForeIndex = iBackIndex ^ 1;

        /* Get the translated back color */
        ULONG rgbBack = PALETTE_ulGetRGBColorFromIndex(pebo->ppalSurf, iBackIndex);

        /* Match the pen color against RGB and translated background color */
        if ((crColor == rgbBack) || (crColor == pebo->crCurrentBack))
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

/*
 * Bayer 16x16 ordered-dither matrix, matching Windows' / Wine's monochrome
 * pattern-brush realization (dlls/win32u/dibdrv/primitives.c rgb_to_pixel_mono).
 */
static const BYTE gaubBayer16x16[16][16] =
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
    { 255, 127, 223,  95, 247, 119, 215,  87, 253, 125, 221,  93, 245, 117, 213,  85 },
};

/*
 * Realize a colour (multi-bpp) pattern brush onto a 1bpp indexed destination
 * using ordered (Bayer) dithering, exactly as Windows does.  ReactOS' normal
 * realization path (EngCopyBits with a nearest-colour XLATEOBJ) does NOT dither,
 * which makes PatBlt with a DIB pattern brush onto a 1bpp DIB produce a
 * different result from Windows (gdi32:dib "* dib brush patblt" failures).
 *
 * For each realized-surface pixel (x, y) we:
 *   1. read the pattern's pixel index and translate it to an RGB colour,
 *   2. threshold it to black/white via luminance + Bayer[y%16][x%16] > 255,
 *   3. map black/white to the nearest destination palette index.
 *
 * The dither coordinate is the realized-surface pixel position, and the pattern
 * pixel is read at the same (x, y); EngCopyBits would copy surface-row -> row in
 * the same way, so top-down / bottom-up DIB pattern orientation is honoured by
 * the pattern surface's own GetPixel.
 */
static
BOOL
EngRealizeBrushDither(
    SURFOBJ *psoRealize,
    SURFOBJ *psoPattern,
    PPALETTE ppalPattern,
    PPALETTE ppalDst)
{
    EXLATEOBJ exloToRGB;
    LONG x, y, cx, cy;
    PFN_DIB_GetPixel pfnGetPattern;

    cx = psoPattern->sizlBitmap.cx;
    cy = psoPattern->sizlBitmap.cy;

    pfnGetPattern = DibFunctionsForBitmapFormat[psoPattern->iBitmapFormat].DIB_GetPixel;
    if (!pfnGetPattern)
        return FALSE;

    /* Build a pattern-palette -> RGB translation so we can recover the true
     * colour of each pattern pixel before thresholding it. */
    EXLATEOBJ_vInitialize(&exloToRGB, ppalPattern, &gpalRGB, 0, 0, 0);

    for (y = 0; y < cy; y++)
    {
        for (x = 0; x < cx; x++)
        {
            ULONG iSrc, crRGB, iDst;
            LONG r, g, b, lum;

            iSrc = pfnGetPattern(psoPattern, x, y);
            crRGB = XLATEOBJ_iXlate(&exloToRGB.xlo, iSrc);

            r = crRGB & 0xFF;
            g = (crRGB >> 8) & 0xFF;
            b = (crRGB >> 16) & 0xFF;

            /* Luminance + ordered dither, thresholded to black or white. */
            lum = (30 * r + 59 * g + 11 * b) / 100;
            if (lum + gaubBayer16x16[y % 16][x % 16] > 255)
                crRGB = RGB(0xFF, 0xFF, 0xFF);
            else
                crRGB = RGB(0x00, 0x00, 0x00);

            iDst = PALETTE_ulGetNearestPaletteIndex(ppalDst, crRGB);

            DIB_1BPP_PutPixel(psoRealize, x, y, iDst);
        }
    }

    EXLATEOBJ_vCleanup(&exloToRGB);
    return TRUE;
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

    /*
     * Windows dithers a colour pattern brush when realizing it onto a 1bpp
     * indexed destination.  Detect that case and use ordered dithering instead
     * of the plain nearest-colour EngCopyBits, otherwise fall back to the
     * generic copy.  pxlo (when present) is an EXLATEOBJ carrying the pattern
     * and destination palettes.
     */
    if ((psoDst->iBitmapFormat == BMF_1BPP) &&
        (psoPattern->iBitmapFormat != BMF_1BPP) &&
        (pxlo != NULL))
    {
        PEXLATEOBJ pexlo = CONTAINING_RECORD(pxlo, EXLATEOBJ, xlo);
        PPALETTE ppalPat = pexlo->ppalSrc;
        PPALETTE ppalDst = pexlo->ppalDst;

        if (ppalDst && (ppalDst->flFlags & PAL_INDEXED) &&
            EngRealizeBrushDither(psoRealize, psoPattern, ppalPat, ppalDst))
        {
            /* Dithered realization done */
        }
        else
        {
            EngCopyBits(psoRealize, psoPattern, NULL, pxlo, &rclDest, &ptlSrc);
        }
    }
    else
    {
        EngCopyBits(psoRealize, psoPattern, NULL, pxlo, &rclDest, &ptlSrc);
    }

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
