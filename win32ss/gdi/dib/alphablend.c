/*
 * PROJECT:         Win32 subsystem
 * LICENSE:         See COPYING in the top level directory
 * FILE:            win32ss/gdi/dib/alphablend.c
 * PURPOSE:         AlphaBlend implementation suitable for all bit depths
 * PROGRAMMERS:     Jérôme Gardou
 */

#include <win32k.h>

#define NDEBUG
#include <debug.h>

typedef union
{
  ULONG ul;
  struct
  {
    UCHAR red;
    UCHAR green;
    UCHAR blue;
    UCHAR alpha;
  } col;
} NICEPIXEL32;

/* The blended color is quantized to the center of a 5 bit bucket per
   channel before the nearest-palette lookup on 4/8 bpp destinations */
#define QUANTIZE5_CENTER(c) (((c) & 0xf8) | 4)

BOOLEAN
DIB_XXBPP_AlphaBlend(SURFOBJ* Dest, SURFOBJ* Source, RECTL* DestRect,
                     RECTL* SourceRect, CLIPOBJ* ClipRegion,
                     XLATEOBJ* ColorTranslation, BLENDOBJ* BlendObj)
{
  INT DstX, DstY, SrcX, SrcY;
  BLENDFUNCTION BlendFunc;
  register NICEPIXEL32 DstPixel32;
  register NICEPIXEL32 SrcPixel32;
  UCHAR Alpha, SrcBpp = BitsPerFormat(Source->iBitmapFormat);
  ULONG ulDstRedLen = 0, ulDstGreenLen = 0, ulDstBlueLen = 0;
  BOOL bSrcAlpha, bScaleSrc, bReplicate, b1bppNearest, bQuantize;
  LONG lC0Red = 0, lC0Green = 0, lC0Blue = 0;
  LONG lC1Red = 0, lC1Green = 0, lC1Blue = 0;
  EXLATEOBJ* pexlo;
  EXLATEOBJ exloSrcRGB, exloDstRGB, exloRGBDst;
  PFN_DIB_PutPixel pfnDibPutPixel = DibFunctionsForBitmapFormat[Dest->iBitmapFormat].DIB_PutPixel;

  DPRINT("DIB_XXBPP_AlphaBlend: srcRect: (%d,%d)-(%d,%d), dstRect: (%d,%d)-(%d,%d)\n",
    SourceRect->left, SourceRect->top, SourceRect->right, SourceRect->bottom,
    DestRect->left, DestRect->top, DestRect->right, DestRect->bottom);

  BlendFunc = BlendObj->BlendFunction;
  if (BlendFunc.BlendOp != AC_SRC_OVER)
  {
    DPRINT1("BlendOp != AC_SRC_OVER\n");
    return FALSE;
  }
  if (BlendFunc.BlendFlags != 0)
  {
    DPRINT1("BlendFlags != 0\n");
    return FALSE;
  }
  if ((BlendFunc.AlphaFormat & ~AC_SRC_ALPHA) != 0)
  {
    DPRINT1("Unsupported AlphaFormat (0x%x)\n", BlendFunc.AlphaFormat);
    return FALSE;
  }
  if ((BlendFunc.AlphaFormat & AC_SRC_ALPHA) != 0 &&
    SrcBpp != 32)
  {
    DPRINT1("Source bitmap must be 32bpp when AC_SRC_ALPHA is set\n");
    return FALSE;
  }

  if (!ColorTranslation)
  {
    DPRINT1("ColorTranslation must not be NULL!\n");
    return FALSE;
  }

  pexlo = CONTAINING_RECORD(ColorTranslation, EXLATEOBJ, xlo);
  EXLATEOBJ_vInitialize(&exloSrcRGB, pexlo->ppalSrc, &gpalRGB, 0, 0, 0);
  EXLATEOBJ_vInitialize(&exloDstRGB, pexlo->ppalDst, &gpalRGB, 0, 0, 0);
  EXLATEOBJ_vInitialize(&exloRGBDst, &gpalRGB, pexlo->ppalDst, 0, 0, 0);

  /* For bitfield destinations the conversion to RGB leaves the low bits
     of each expanded channel zero; Windows reads them with the top bits
     replicated, which matters for the blend result */
  if (pexlo->ppalDst->flFlags & PAL_BITFIELDS)
  {
    ULONG ulHigh, ulLow;
    if (BitScanReverse(&ulHigh, pexlo->ppalDst->RedMask) &&
        BitScanForward(&ulLow, pexlo->ppalDst->RedMask))
      ulDstRedLen = ulHigh - ulLow + 1;
    if (BitScanReverse(&ulHigh, pexlo->ppalDst->GreenMask) &&
        BitScanForward(&ulLow, pexlo->ppalDst->GreenMask))
      ulDstGreenLen = ulHigh - ulLow + 1;
    if (BitScanReverse(&ulHigh, pexlo->ppalDst->BlueMask) &&
        BitScanForward(&ulLow, pexlo->ppalDst->BlueMask))
      ulDstBlueLen = ulHigh - ulLow + 1;

    /* Channels of 8 or more bits need no replication; a shift of 8
       makes them a no-op below without a per-channel test */
    ulDstRedLen = min(ulDstRedLen, 8);
    ulDstGreenLen = min(ulDstGreenLen, 8);
    ulDstBlueLen = min(ulDstBlueLen, 8);
  }

  /* Hoist the per-call decisions out of the pixel loop */
  bSrcAlpha = (BlendFunc.AlphaFormat & AC_SRC_ALPHA) != 0;
  bScaleSrc = bSrcAlpha && (BlendFunc.SourceConstantAlpha != 255);
  bReplicate = ((ulDstRedLen > 0) && (ulDstRedLen < 8)) ||
               ((ulDstGreenLen > 0) && (ulDstGreenLen < 8)) ||
               ((ulDstBlueLen > 0) && (ulDstBlueLen < 8));
  b1bppNearest = (Dest->iBitmapFormat == BMF_1BPP) &&
                 (pexlo->ppalDst->flFlags & PAL_INDEXED) &&
                 (pexlo->ppalDst->NumColors >= 2);
  bQuantize = (Dest->iBitmapFormat == BMF_4BPP) ||
              (Dest->iBitmapFormat == BMF_8BPP);
  if (b1bppNearest)
  {
    lC0Red = pexlo->ppalDst->IndexedColors[0].peRed;
    lC0Green = pexlo->ppalDst->IndexedColors[0].peGreen;
    lC0Blue = pexlo->ppalDst->IndexedColors[0].peBlue;
    lC1Red = pexlo->ppalDst->IndexedColors[1].peRed;
    lC1Green = pexlo->ppalDst->IndexedColors[1].peGreen;
    lC1Blue = pexlo->ppalDst->IndexedColors[1].peBlue;
  }

  SrcY = SourceRect->top;
  DstY = DestRect->top;
  while ( DstY < DestRect->bottom )
  {
    SrcX = SourceRect->left;
    DstX = DestRect->left;
    while(DstX < DestRect->right)
    {
      SrcPixel32.ul = DIB_GetSource(Source, SrcX, SrcY, &exloSrcRGB.xlo);
      DstPixel32.ul = DIB_GetSource(Dest, DstX, DstY, &exloDstRGB.xlo);

      /* Replicate the top bits into the zero-filled tail of each
         expanded destination channel */
      if (bReplicate)
      {
        DstPixel32.col.red |= DstPixel32.col.red >> ulDstRedLen;
        DstPixel32.col.green |= DstPixel32.col.green >> ulDstGreenLen;
        DstPixel32.col.blue |= DstPixel32.col.blue >> ulDstBlueLen;
      }

      if (bSrcAlpha)
      {
        /* Premultiplied source: scale by the constant alpha, then blend
           with the per-pixel alpha, rounding to nearest like Windows */
        if (bScaleSrc)
        {
          SrcPixel32.col.red = DIB_ScaleAlpha(SrcPixel32.col.red, BlendFunc.SourceConstantAlpha);
          SrcPixel32.col.green = DIB_ScaleAlpha(SrcPixel32.col.green, BlendFunc.SourceConstantAlpha);
          SrcPixel32.col.blue = DIB_ScaleAlpha(SrcPixel32.col.blue, BlendFunc.SourceConstantAlpha);
          SrcPixel32.col.alpha = DIB_ScaleAlpha(SrcPixel32.col.alpha, BlendFunc.SourceConstantAlpha);
        }
        Alpha = SrcPixel32.col.alpha;
        DstPixel32.col.red = DIB_BlendOver(SrcPixel32.col.red, DstPixel32.col.red, Alpha);
        DstPixel32.col.green = DIB_BlendOver(SrcPixel32.col.green, DstPixel32.col.green, Alpha);
        DstPixel32.col.blue = DIB_BlendOver(SrcPixel32.col.blue, DstPixel32.col.blue, Alpha);
      }
      else
      {
        Alpha = BlendFunc.SourceConstantAlpha;
        DstPixel32.col.red = DIB_BlendLerp(SrcPixel32.col.red, DstPixel32.col.red, Alpha);
        DstPixel32.col.green = DIB_BlendLerp(SrcPixel32.col.green, DstPixel32.col.green, Alpha);
        DstPixel32.col.blue = DIB_BlendLerp(SrcPixel32.col.blue, DstPixel32.col.blue, Alpha);
      }

      if (b1bppNearest)
      {
        /* Windows picks the nearest of the two colors of the exact
           blended value (no quantization, squared distance) */
        LONG lDiff;
        ULONG ulDist0, ulDist1;

        lDiff = (LONG)DstPixel32.col.red - lC0Red;
        ulDist0 = lDiff * lDiff;
        lDiff = (LONG)DstPixel32.col.green - lC0Green;
        ulDist0 += lDiff * lDiff;
        lDiff = (LONG)DstPixel32.col.blue - lC0Blue;
        ulDist0 += lDiff * lDiff;

        lDiff = (LONG)DstPixel32.col.red - lC1Red;
        ulDist1 = lDiff * lDiff;
        lDiff = (LONG)DstPixel32.col.green - lC1Green;
        ulDist1 += lDiff * lDiff;
        lDiff = (LONG)DstPixel32.col.blue - lC1Blue;
        ulDist1 += lDiff * lDiff;

        pfnDibPutPixel(Dest, DstX, DstY, (ulDist1 < ulDist0) ? 1 : 0);
      }
      else
      {
        /* Windows quantizes the blended color to 5 bit bucket centers
           before the nearest-palette lookup on 4 and 8 bpp */
        if (bQuantize)
        {
          DstPixel32.col.red = QUANTIZE5_CENTER(DstPixel32.col.red);
          DstPixel32.col.green = QUANTIZE5_CENTER(DstPixel32.col.green);
          DstPixel32.col.blue = QUANTIZE5_CENTER(DstPixel32.col.blue);
        }

        /* Convert the blended RGB directly into the destination format,
           round-tripping through the source palette would collapse the
           blend result onto the source's colors */
        pfnDibPutPixel(Dest, DstX, DstY,
                       XLATEOBJ_iXlate(&exloRGBDst.xlo, DstPixel32.ul));
      }

      DstX++;
      SrcX = SourceRect->left + ((DstX-DestRect->left)*(SourceRect->right - SourceRect->left))
                                            /(DestRect->right-DestRect->left);
    }
    DstY++;
    SrcY = SourceRect->top + ((DstY-DestRect->top)*(SourceRect->bottom - SourceRect->top))
                                            /(DestRect->bottom-DestRect->top);
  }

  EXLATEOBJ_vCleanup(&exloDstRGB);
  EXLATEOBJ_vCleanup(&exloRGBDst);
  EXLATEOBJ_vCleanup(&exloSrcRGB);

  return TRUE;
}

