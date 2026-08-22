/*
 * PROJECT:         ReactOS Win32k subsystem
 * LICENSE:         See COPYING in the top level directory
 * FILE:            win32ss/gdi/dib/stretchblt.c
 * PURPOSE:         StretchBlt implementation suitable for all bit depths
 * PROGRAMMERS:     Magnus Olsen
 *                  Evgeniy Boltik
 *                  Gregor Schneider
 *                  Doug Lyons
 */

#include <win32k.h>

#define NDEBUG
#include <debug.h>

static ULONG
DIB_BilinearColor(
    ULONG Color00,
    ULONG Color01,
    ULONG Color10,
    ULONG Color11,
    ULONG FractionX,
    ULONG FractionY)
{
  ULONGLONG InverseX = 0x10000 - FractionX;
  ULONGLONG InverseY = 0x10000 - FractionY;
  ULONGLONG Weight00 = InverseX * InverseY;
  ULONGLONG Weight01 = FractionX * InverseY;
  ULONGLONG Weight10 = InverseX * FractionY;
  ULONGLONG Weight11 = FractionX * FractionY;
  ULONGLONG Blue;
  ULONGLONG Green;
  ULONGLONG Red;

  Blue = ((Color00 & 0xff) * Weight00) + ((Color01 & 0xff) * Weight01) + ((Color10 & 0xff) * Weight10) + ((Color11 & 0xff) * Weight11);
  Green = (((Color00 >> 8) & 0xff) * Weight00) + (((Color01 >> 8) & 0xff) * Weight01) + (((Color10 >> 8) & 0xff) * Weight10) + (((Color11 >> 8) & 0xff) * Weight11);
  Red = (((Color00 >> 16) & 0xff) * Weight00) + (((Color01 >> 16) & 0xff) * Weight01) + (((Color10 >> 16) & 0xff) * Weight10) + (((Color11 >> 16) & 0xff) * Weight11);
  return (ULONG)(((Blue + 0x80000000ULL) >> 32) | (((Green + 0x80000000ULL) >> 32) << 8) | (((Red + 0x80000000ULL) >> 32) << 16));
}

BOOLEAN DIB_XXBPP_StretchBlt(SURFOBJ *DestSurf, SURFOBJ *SourceSurf, SURFOBJ *MaskSurf,
                            SURFOBJ *PatternSurface,
                            RECTL *DestRect, RECTL *SourceRect,
                            POINTL *MaskOrigin, BRUSHOBJ *Brush,
                            POINTL *BrushOrigin, XLATEOBJ *ColorTranslation,
                            ULONG Mode,
                            ROP4 ROP)
{
  LONG sx = 0;
  LONG sy = 0;
  LONG DesX;
  LONG DesY;

  LONG DstHeight;
  LONG DstWidth;
  LONG SrcHeight;
  LONG SrcWidth;
  LONG MaskCy;
  LONG SourceCy;

  ULONG Color;
  ULONG Dest, Source = 0, Pattern = 0;
  ULONG Color00, Color01, Color10, Color11;
  ULONG FractionX, FractionY;
  ULONG xxBPPMask;
  BOOLEAN CanDraw;
  BOOLEAN UseHalftone;
  ULONGLONG StepX, StepY;
  ULONGLONG SourceFixedX, SourceFixedY;
  LONG sx1, sy1;

  PFN_DIB_GetPixel fnSource_GetPixel = NULL;
  PFN_DIB_GetPixel fnDest_GetPixel = NULL;
  PFN_DIB_PutPixel fnDest_PutPixel = NULL;
  PFN_DIB_GetPixel fnPattern_GetPixel = NULL;
  PFN_DIB_GetPixel fnMask_GetPixel = NULL;

  LONG PatternX = 0, PatternY = 0;

  BOOL UsesSource = ROP4_USES_SOURCE(ROP);
  BOOL UsesPattern = ROP4_USES_PATTERN(ROP);
  BOOLEAN bTopToBottom, bLeftToRight;

  ASSERT(IS_VALID_ROP4(ROP));

  fnDest_GetPixel = DibFunctionsForBitmapFormat[DestSurf->iBitmapFormat].DIB_GetPixel;
  fnDest_PutPixel = DibFunctionsForBitmapFormat[DestSurf->iBitmapFormat].DIB_PutPixel;

  DPRINT("Dest BPP: %u, DestRect: (%d,%d)-(%d,%d)\n",
    BitsPerFormat(DestSurf->iBitmapFormat), DestRect->left, DestRect->top, DestRect->right, DestRect->bottom);

  DstHeight = DestRect->bottom - DestRect->top;
  DstWidth = DestRect->right - DestRect->left;
  SrcHeight = SourceRect->bottom - SourceRect->top;
  SrcWidth = SourceRect->right - SourceRect->left;

  UseHalftone = (Mode == HALFTONE && ROP == ROP4_SRCCOPY && UsesSource && !MaskSurf && !PatternSurface && SrcWidth > 0 && SrcHeight > 0 && DstWidth > 0 && DstHeight > 0 && (DestSurf->iBitmapFormat == BMF_24BPP || DestSurf->iBitmapFormat == BMF_32BPP));
  StepX = UseHalftone ? ((ULONGLONG)(ULONG)SrcWidth << 32) / (ULONG)DstWidth : 0;
  StepY = UseHalftone ? ((ULONGLONG)(ULONG)SrcHeight << 32) / (ULONG)DstHeight : 0;

  /* Here we do the tests and set our conditions */
  if (((SrcWidth < 0) && (DstWidth < 0)) || ((SrcWidth >= 0) && (DstWidth >= 0)))
    bLeftToRight = FALSE;
  else
    bLeftToRight = TRUE;

  if (((SrcHeight < 0) && (DstHeight < 0)) || ((SrcHeight >= 0) && (DstHeight >= 0)))
    bTopToBottom = FALSE;
  else
    bTopToBottom = TRUE;

  /* Make Well Ordered to start */
  RECTL_vMakeWellOrdered(DestRect);

  if (UsesSource)
  {
    SourceCy = SourceSurf->sizlBitmap.cy;
    fnSource_GetPixel = DibFunctionsForBitmapFormat[SourceSurf->iBitmapFormat].DIB_GetPixel;
    DPRINT("Source BPP: %u, SourceRect: (%d,%d)-(%d,%d)\n",
      BitsPerFormat(SourceSurf->iBitmapFormat), SourceRect->left, SourceRect->top, SourceRect->right, SourceRect->bottom);
  }

  if (MaskSurf)
  {
    DPRINT("MaskSurf is not NULL.\n");
    fnMask_GetPixel = DibFunctionsForBitmapFormat[MaskSurf->iBitmapFormat].DIB_GetPixel;
    MaskCy = MaskSurf->sizlBitmap.cy;
  }

  DstHeight = DestRect->bottom - DestRect->top;
  DstWidth = DestRect->right - DestRect->left;
  SrcHeight = SourceRect->bottom - SourceRect->top;
  SrcWidth = SourceRect->right - SourceRect->left;

  /* FIXME: MaskOrigin? */

  switch(DestSurf->iBitmapFormat)
  {
  case BMF_1BPP: xxBPPMask = 0x1; break;
  case BMF_4BPP: xxBPPMask = 0xF; break;
  case BMF_8BPP: xxBPPMask = 0xFF; break;
  case BMF_16BPP: xxBPPMask = 0xFFFF; break;
  case BMF_24BPP: xxBPPMask = 0xFFFFFF; break;
  default:
    xxBPPMask = 0xFFFFFFFF;
  }
  DPRINT("xxBPPMask is 0x%x.\n", xxBPPMask);

  if (UsesPattern)
  {
    DPRINT("UsesPattern is not NULL.\n");
    if (PatternSurface)
    {
      PatternY = (DestRect->top - BrushOrigin->y) % PatternSurface->sizlBitmap.cy;
      if (PatternY < 0)
      {
        PatternY += PatternSurface->sizlBitmap.cy;
      }
      fnPattern_GetPixel = DibFunctionsForBitmapFormat[PatternSurface->iBitmapFormat].DIB_GetPixel;
    }
    else
    {
      if (Brush)
        Pattern = Brush->iSolidColor;
    }
  }

  if (PatternSurface)
  {
    DPRINT("PatternSurface is not NULL.\n");
  }

  DPRINT("bLeftToRight is '%d' and bTopToBottom is '%d'.\n", bLeftToRight, bTopToBottom);

  for (DesY = DestRect->top; DesY < DestRect->bottom; DesY++)
  {
    if (PatternSurface)
    {
      PatternX = (DestRect->left - BrushOrigin->x) % PatternSurface->sizlBitmap.cx;
      if (PatternX < 0)
      {
        PatternX += PatternSurface->sizlBitmap.cx;
      }
    }
    if (UsesSource)
    {
      if (UseHalftone)
      {
        SourceFixedY = (ULONGLONG)(DesY - DestRect->top) * StepY;
        sy = SourceRect->top + (LONG)(SourceFixedY >> 32);
        sy1 = min(sy + 1, SourceRect->bottom - 1);
        FractionY = (ULONG)((SourceFixedY >> 16) & 0xffff);
      }
      else if (bTopToBottom)
      {
        sy = SourceRect->bottom-(DesY - DestRect->top) * SrcHeight / DstHeight;  // flips about the x-axis
      }
      else
      {
        sy = SourceRect->top+(DesY - DestRect->top) * SrcHeight / DstHeight;
      }
    }

    for (DesX = DestRect->left; DesX < DestRect->right; DesX++)
    {
      CanDraw = TRUE;

      if (fnMask_GetPixel)
      {
        if (bLeftToRight)
        {
          sx = SourceRect->right - (DesX - DestRect->left) * SrcWidth / DstWidth;  // flips about the y-axis
        }
        else
        {
          sx = SourceRect->left+(DesX - DestRect->left) * SrcWidth / DstWidth;
        }
        if (sx < 0 || sy < 0 ||
          MaskSurf->sizlBitmap.cx <= sx || MaskCy <= sy ||
          fnMask_GetPixel(MaskSurf, sx, sy) != 0)
        {
          CanDraw = FALSE;
        }
      }

      if (UsesSource && CanDraw)
      {
        if (UseHalftone)
        {
          SourceFixedX = (ULONGLONG)(DesX - DestRect->left) * StepX;
          sx = SourceRect->left + (LONG)(SourceFixedX >> 32);
          sx1 = min(sx + 1, SourceRect->right - 1);
          FractionX = (ULONG)((SourceFixedX >> 16) & 0xffff);
          Color00 = XLATEOBJ_iXlate(ColorTranslation, fnSource_GetPixel(SourceSurf, sx, sy));
          Color01 = XLATEOBJ_iXlate(ColorTranslation, fnSource_GetPixel(SourceSurf, sx1, sy));
          Color10 = XLATEOBJ_iXlate(ColorTranslation, fnSource_GetPixel(SourceSurf, sx, sy1));
          Color11 = XLATEOBJ_iXlate(ColorTranslation, fnSource_GetPixel(SourceSurf, sx1, sy1));
          Source = DIB_BilinearColor(Color00, Color01, Color10, Color11, FractionX, FractionY);
        }
        else if (bLeftToRight)
        {
          sx = SourceRect->right-(DesX - DestRect->left) * SrcWidth / DstWidth;  // flips about the y-axis
        }
        else
        {
          sx = SourceRect->left + (DesX - DestRect->left) * SrcWidth / DstWidth;
        }
        if (!UseHalftone && sx >= 0 && sy >= 0 &&
          SourceSurf->sizlBitmap.cx > sx && SourceCy > sy)
        {
          Source = XLATEOBJ_iXlate(ColorTranslation, fnSource_GetPixel(SourceSurf, sx, sy));
        }
        else if (!UseHalftone)
        {
          Source = 0;
          CanDraw = ((ROP & 0xFF) != R3_OPINDEX_SRCCOPY);
        }
      }

      if (CanDraw)
      {
        if (UsesPattern && PatternSurface)
        {
          Pattern = fnPattern_GetPixel(PatternSurface, PatternX, PatternY);
          PatternX++;
          PatternX %= PatternSurface->sizlBitmap.cx;
        }

        Dest = fnDest_GetPixel(DestSurf, DesX, DesY);
        Color = DIB_DoRop(ROP, Dest, Source, Pattern) & xxBPPMask;

        fnDest_PutPixel(DestSurf, DesX, DesY, Color);
      }
    }

    if (PatternSurface)
    {
      PatternY++;
      PatternY %= PatternSurface->sizlBitmap.cy;
    }
  }

  return TRUE;
}

/* EOF */
