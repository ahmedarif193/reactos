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

static __inline UCHAR
Clamp8(ULONG val)
{
  return (val > 255) ? 255 : (UCHAR)val;
}

static __inline UCHAR
AlphaMul(UCHAR Value, UCHAR Alpha)
{
  return (UCHAR)(((ULONG)Value * Alpha + 127) / 255);
}

static __inline UCHAR
BlendColor(UCHAR Dst, UCHAR Src, ULONG Alpha)
{
  return (UCHAR)(((ULONG)Src * Alpha + (ULONG)Dst * (255 - Alpha) + 127) / 255);
}

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
  EXLATEOBJ_vInitialize(&exloSrcRGB, pexlo->ppalSrc, &gpalRGB, 0, CLR_INVALID, 0);
  EXLATEOBJ_vInitialize(&exloDstRGB, pexlo->ppalDst, &gpalRGB, 0, CLR_INVALID, 0);
  EXLATEOBJ_vInitialize(&exloRGBDst, &gpalRGB, pexlo->ppalDst, CLR_INVALID, 0, 0);

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

      if ((BlendFunc.AlphaFormat & AC_SRC_ALPHA) != 0)
      {
        SrcPixel32.col.red = AlphaMul(SrcPixel32.col.red, BlendFunc.SourceConstantAlpha);
        SrcPixel32.col.green = AlphaMul(SrcPixel32.col.green, BlendFunc.SourceConstantAlpha);
        SrcPixel32.col.blue = AlphaMul(SrcPixel32.col.blue, BlendFunc.SourceConstantAlpha);
        Alpha = AlphaMul(SrcPixel32.col.alpha, BlendFunc.SourceConstantAlpha);
        DstPixel32.col.red = Clamp8(AlphaMul(DstPixel32.col.red, 255 - Alpha) + SrcPixel32.col.red);
        DstPixel32.col.green = Clamp8(AlphaMul(DstPixel32.col.green, 255 - Alpha) + SrcPixel32.col.green);
        DstPixel32.col.blue = Clamp8(AlphaMul(DstPixel32.col.blue, 255 - Alpha) + SrcPixel32.col.blue);
      }
      else
      {
        DstPixel32.col.red = BlendColor(DstPixel32.col.red, SrcPixel32.col.red, BlendFunc.SourceConstantAlpha);
        DstPixel32.col.green = BlendColor(DstPixel32.col.green, SrcPixel32.col.green, BlendFunc.SourceConstantAlpha);
        DstPixel32.col.blue = BlendColor(DstPixel32.col.blue, SrcPixel32.col.blue, BlendFunc.SourceConstantAlpha);
      }
      pfnDibPutPixel(Dest, DstX, DstY, XLATEOBJ_iXlate(&exloRGBDst.xlo, DstPixel32.ul));

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
