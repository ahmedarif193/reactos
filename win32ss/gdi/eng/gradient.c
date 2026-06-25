/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS kernel
 * PURPOSE:           GDI Driver Gradient Functions
 * FILE:              win32ss/gdi/eng/gradient.c
 * PROGRAMER:         Thomas Weidenmueller
 */

#include <win32k.h>

#define NDEBUG
#include <debug.h>

/* MACROS *********************************************************************/

const LONG LINC[2] = {-1, 1};

/* Bayer dithering matrix for 16bpp gradients, matching Wine's dibdrv
 * (dlls/win32u/dibdrv/primitives.c). */
static const BYTE bayer_4x4[4][4] =
{
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

/*
 * Destination format classification for gradient fills.
 *
 * We only special-case the formats where ReactOS's plain XLATEOBJ_iXlate
 * output diverges from Windows/Wine:
 *  - GRAD_FMT_ALPHA: plain BI_RGB a8r8g8b8 (32bpp).  Windows writes the
 *    interpolated alpha vertex into the top byte.  (Note: the bitfields
 *    variant a8r8g8b8 has a Windows bug that zeroes alpha, so it is excluded
 *    via the PAL_BITFIELDS check, matching Wine.)
 *  - GRAD_FMT_DITHER16: any 16bpp destination (555/565/444).  Windows dithers
 *    the gradient with a 4x4 Bayer matrix in device coordinates.
 *
 * Indexed (<=8bpp) destinations are intentionally NOT handled: Windows uses a
 * dithering that is not bit-exact with Wine's bayer_16x16 (the gdi32 winetest
 * even marks those todo on Wine), so there is no reproducible rule to match.
 */
enum
{
    GRAD_FMT_DEFAULT = 0,
    GRAD_FMT_ALPHA,
    GRAD_FMT_DITHER16
};

static
ULONG
IntEngGradientFormat(SURFOBJ *psoDest)
{
    SURFACE *psurf = CONTAINING_RECORD(psoDest, SURFACE, SurfObj);
    PALETTE *ppal = psurf->ppal;

    if (psoDest->iBitmapFormat == BMF_16BPP)
        return GRAD_FMT_DITHER16;

    if ((psoDest->iBitmapFormat == BMF_32BPP) &&
        (ppal != NULL) &&
        (ppal->flFlags & PAL_BGR) &&
        !(ppal->flFlags & PAL_BITFIELDS))
    {
        /* Plain BI_RGB a8r8g8b8: red mask 0xff0000, no bitfields. */
        return GRAD_FMT_ALPHA;
    }

    return GRAD_FMT_DEFAULT;
}

/*
 * Compute one gradient pixel using Wine's exact arithmetic for the special
 * destination formats.  c0/c1 are the two endpoint vertices, pos/len describe
 * the interpolation position along the gradient axis, and x/y are absolute
 * device coordinates (used for the Bayer dither).
 */
static
ULONG
IntEngGradientColor(
    XLATEOBJ *pxlo,
    ULONG fmt,
    const TRIVERTEX *c0,
    const TRIVERTEX *c1,
    LONG pos,
    LONG len,
    LONG x,
    LONG y)
{
    if (len == 0) len = 1;

    if (fmt == GRAD_FMT_DITHER16)
    {
        LONG r, g, b;
        LONG bayer = bayer_4x4[y & 3][x & 3];

        r = ((LONG)c0->Red   * (len - pos) + (LONG)c1->Red   * pos) / len / 128 + bayer;
        g = ((LONG)c0->Green * (len - pos) + (LONG)c1->Green * pos) / len / 128 + bayer;
        b = ((LONG)c0->Blue  * (len - pos) + (LONG)c1->Blue  * pos) / len / 128 + bayer;

        r = min(31, max(0, r / 16));
        g = min(31, max(0, g / 16));
        b = min(31, max(0, b / 16));

        /* Expand the dithered 5-bit channels to 8-bit (as Wine does) and let
         * the xlate pack them into the actual destination format. */
        r = (r << 3) | (r >> 2);
        g = (g << 3) | (g >> 2);
        b = (b << 3) | (b >> 2);

        return XLATEOBJ_iXlate(pxlo, RGB(r, g, b));
    }

    if (fmt == GRAD_FMT_ALPHA)
    {
        ULONG a, color;

        color = XLATEOBJ_iXlate(pxlo,
                                RGB((((LONG)c0->Red   * (len - pos) + (LONG)c1->Red   * pos) / len) >> 8,
                                    (((LONG)c0->Green * (len - pos) + (LONG)c1->Green * pos) / len) >> 8,
                                    (((LONG)c0->Blue  * (len - pos) + (LONG)c1->Blue  * pos) / len) >> 8));

        a = (((LONG)c0->Alpha * (len - pos) + (LONG)c1->Alpha * pos) / len) >> 8;
        return (color & 0x00ffffff) | (a << 24);
    }

    /* GRAD_FMT_DEFAULT should not reach here */
    return XLATEOBJ_iXlate(pxlo,
                           RGB((((LONG)c0->Red   * (len - pos) + (LONG)c1->Red   * pos) / len) >> 8,
                               (((LONG)c0->Green * (len - pos) + (LONG)c1->Green * pos) / len) >> 8,
                               (((LONG)c0->Blue  * (len - pos) + (LONG)c1->Blue  * pos) / len) >> 8));
}

#define VERTEX(n) (pVertex + gt->n)
#define COMPAREVERTEX(a, b) ((a)->x == (b)->x && (a)->y == (b)->y)

/* Check if all three vectors have the same color, either R, G, or B */
#define VCMPCLR(a, b, c, color) (a->color == b->color && a->color == c->color)
/* Check if all three vectors have the same colors for R, G, and B, then
 * NOT the result because we want to check for not using solid color logic */
#define VCMPCLRS(a, b, c) \
  !(VCMPCLR(a, b, c, Red) && VCMPCLR(a, b, c, Green) && VCMPCLR(a, b, c, Blue))

/* Horizontal/Vertical gradients */
#define HVINITCOL(Col, id) \
  c[id] = v1->Col; \
  dc[id] = abs((LONG)v2->Col - c[id]); \
  ec[id] = -(dy >> 1); \
  ic[id] = LINC[v2->Col > c[id]]
#define HVSTEPCOL(id) \
  ec[id] += dc[id]; \
  while(ec[id] > 0) \
  { \
    c[id] += ic[id]; \
    ec[id] -= dy; \
  }

/* FUNCTIONS ******************************************************************/

BOOL
FASTCALL
IntEngGradientFillRect(
    IN SURFOBJ  *psoDest,
    IN CLIPOBJ  *pco,
    IN XLATEOBJ  *pxlo,
    IN TRIVERTEX  *pVertex,
    IN ULONG  nVertex,
    IN PGRADIENT_RECT gRect,
    IN RECTL  *prclExtents,
    IN POINTL  *pptlDitherOrg,
    IN BOOL Horizontal)
{
    SURFOBJ *psoOutput;
    TRIVERTEX *v1, *v2;
    RECTL rcGradient, rcSG;
    RECT_ENUM RectEnum;
    BOOL EnumMore;
    ULONG i;
    POINTL Translate;
    INTENG_ENTER_LEAVE EnterLeave;
    LONG y, dy, c[3], dc[3], ec[3], ic[3];
    ULONG fmt;

    v1 = (pVertex + gRect->UpperLeft);
    v2 = (pVertex + gRect->LowerRight);

    rcGradient.left = min(v1->x, v2->x);
    rcGradient.right = max(v1->x, v2->x);
    rcGradient.top = min(v1->y, v2->y);
    rcGradient.bottom = max(v1->y, v2->y);
    rcSG = rcGradient;

    if(Horizontal)
    {
        dy = abs(rcGradient.right - rcGradient.left);
    }
    else
    {
        dy = abs(rcGradient.bottom - rcGradient.top);
    }

    if(!IntEngEnter(&EnterLeave, psoDest, &rcSG, FALSE, &Translate, &psoOutput))
    {
        return FALSE;
    }

    fmt = IntEngGradientFormat(psoDest);

    /*
     * Special destination formats (plain a8r8g8b8 alpha, and 16bpp dithering)
     * need Wine/Windows-exact per-pixel arithmetic, including a Bayer dither
     * that varies with the device y coordinate.  The DDA path below cannot
     * express that, so handle these formats with an explicit per-pixel loop.
     */
    if (fmt != GRAD_FMT_DEFAULT &&
        (v1->Red != v2->Red || v1->Green != v2->Green || v1->Blue != v2->Blue) && dy > 1)
    {
        TRIVERTEX *cl, *ch;
        LONG len;

        if (Horizontal)
        {
            cl = (v1->x <= v2->x) ? v1 : v2;
            ch = (v1->x <= v2->x) ? v2 : v1;
            len = rcGradient.right - rcGradient.left;
        }
        else
        {
            cl = (v1->y <= v2->y) ? v1 : v2;
            ch = (v1->y <= v2->y) ? v2 : v1;
            len = rcGradient.bottom - rcGradient.top;
        }

        CLIPOBJ_cEnumStart(pco, FALSE, CT_RECTANGLES, CD_RIGHTDOWN, 0);
        do
        {
            RECTL FillRect;
            LONG xx, yy;
            ULONG Color;

            EnumMore = CLIPOBJ_bEnum(pco, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
            for (i = 0; i < RectEnum.c && RectEnum.arcl[i].top <= rcSG.bottom; i++)
            {
                if (RECTL_bIntersectRect(&FillRect, &RectEnum.arcl[i], &rcSG))
                {
                    for (yy = FillRect.top; yy < FillRect.bottom; yy++)
                    {
                        for (xx = FillRect.left; xx < FillRect.right; xx++)
                        {
                            LONG pos = Horizontal ? (xx - rcGradient.left)
                                                  : (yy - rcGradient.top);
                            Color = IntEngGradientColor(pxlo, fmt, cl, ch, pos, len, xx, yy);
                            DibFunctionsForBitmapFormat[psoOutput->iBitmapFormat].DIB_PutPixel(
                                psoOutput, xx + Translate.x, yy + Translate.y, Color);
                        }
                    }
                }
            }
        }
        while (EnumMore);

        return IntEngLeave(&EnterLeave);
    }

    if((v1->Red != v2->Red || v1->Green != v2->Green || v1->Blue != v2->Blue) && dy > 1)
    {
        CLIPOBJ_cEnumStart(pco, FALSE, CT_RECTANGLES, CD_RIGHTDOWN, 0);
        do
        {
            RECTL FillRect;
            ULONG Color;

            if (Horizontal)
            {
                EnumMore = CLIPOBJ_bEnum(pco, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
                for (i = 0; i < RectEnum.c && RectEnum.arcl[i].top <= rcSG.bottom; i++)
                {
                    if (RECTL_bIntersectRect(&FillRect, &RectEnum.arcl[i], &rcSG))
                    {
                        HVINITCOL(Red, 0);
                        HVINITCOL(Green, 1);
                        HVINITCOL(Blue, 2);

                        for (y = rcSG.left; y < FillRect.right; y++)
                        {
                            if (y >= FillRect.left)
                            {
                                Color = XLATEOBJ_iXlate(pxlo, RGB(c[0] >> 8, c[1] >> 8, c[2] >> 8));
                                DibFunctionsForBitmapFormat[psoOutput->iBitmapFormat].DIB_VLine(
                                    psoOutput, y + Translate.x, FillRect.top + Translate.y, FillRect.bottom + Translate.y, Color);
                            }
                            HVSTEPCOL(0);
                            HVSTEPCOL(1);
                            HVSTEPCOL(2);
                        }
                    }
                }

                continue;
            }

            /* vertical */
            EnumMore = CLIPOBJ_bEnum(pco, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
            for (i = 0; i < RectEnum.c && RectEnum.arcl[i].top <= rcSG.bottom; i++)
            {
                if (RECTL_bIntersectRect(&FillRect, &RectEnum.arcl[i], &rcSG))
                {
                    HVINITCOL(Red, 0);
                    HVINITCOL(Green, 1);
                    HVINITCOL(Blue, 2);

                    for (y = rcSG.top; y < FillRect.bottom; y++)
                    {
                        if (y >= FillRect.top)
                        {
                            Color = XLATEOBJ_iXlate(pxlo, RGB(c[0] >> 8, c[1] >> 8, c[2] >> 8));
                            DibFunctionsForBitmapFormat[psoOutput->iBitmapFormat].DIB_HLine(psoOutput,
                                                                                            FillRect.left + Translate.x,
                                                                                            FillRect.right + Translate.x,
                                                                                            y + Translate.y,
                                                                                            Color);
                        }
                        HVSTEPCOL(0);
                        HVSTEPCOL(1);
                        HVSTEPCOL(2);
                    }
                }
            }

        }
        while (EnumMore);

        return IntEngLeave(&EnterLeave);
    }

    /* rectangle has only one color, no calculation required */
    CLIPOBJ_cEnumStart(pco, FALSE, CT_RECTANGLES, CD_RIGHTDOWN, 0);
    do
    {
        RECTL FillRect;
        ULONG Color = XLATEOBJ_iXlate(pxlo, RGB(v1->Red >> 8, v1->Green >> 8, v1->Blue >> 8));

        EnumMore = CLIPOBJ_bEnum(pco, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
        for (i = 0; i < RectEnum.c && RectEnum.arcl[i].top <= rcSG.bottom; i++)
        {
            if (RECTL_bIntersectRect(&FillRect, &RectEnum.arcl[i], &rcSG))
            {
                for (; FillRect.top < FillRect.bottom; FillRect.top++)
                {
                    DibFunctionsForBitmapFormat[psoOutput->iBitmapFormat].DIB_HLine(psoOutput,
                                                                                    FillRect.left + Translate.x,
                                                                                    FillRect.right + Translate.x,
                                                                                    FillRect.top + Translate.y,
                                                                                    Color);
                }
            }
        }
    }
    while (EnumMore);

    return IntEngLeave(&EnterLeave);
}

/* Fill triangle with solid color */
#define S_FILLLINE(linefrom,lineto) \
  if(sx[lineto] < sx[linefrom]) \
    DibFunctionsForBitmapFormat[psoOutput->iBitmapFormat].DIB_HLine(psoOutput, max(sx[lineto], FillRect.left), min(sx[linefrom], FillRect.right), sy, Color); \
  else \
    DibFunctionsForBitmapFormat[psoOutput->iBitmapFormat].DIB_HLine(psoOutput, max(sx[linefrom], FillRect.left), min(sx[lineto], FillRect.right), sy, Color);

#define S_DOLINE(a,b,line) \
  ex[line] += dx[line]; \
  while(ex[line] > 0 && x[line] != destx[line]) \
  { \
    x[line] += incx[line]; \
    sx[line] += incx[line]; \
    ex[line] -= dy[line]; \
  }

#define S_GOLINE(a,b,line) \
  if(y >= a->y && y <= b->y) \
  {

#define S_ENDLINE(a,b,line) \
  }

#define S_INITLINE(a,b,line) \
  x[line] = a->x; \
  sx[line] =  a->x + pptlDitherOrg->x; \
  dx[line] = abs(b->x - a->x); \
  dy[line] = abs(b->y - a->y); \
  incx[line] = LINC[b->x > a->x]; \
  ex[line] = -(dy[line]>>1); \
  destx[line] = b->x

/* Fill triangle with gradient */
#define INITCOL(a,b,line,col,id) \
  c[line][id] = a->col >> 8; \
  dc[line][id] = abs((b->col >> 8) - c[line][id]); \
  ec[line][id] = -(dy[line]>>1); \
  ic[line][id] = LINC[(b->col >> 8) > c[line][id]]

#define STEPCOL(a,b,line,col,id) \
  ec[line][id] += dc[line][id]; \
  if(dy[line] != 0) \
  while(ec[line][id] > 0) \
  { \
    c[line][id] += ic[line][id]; \
    ec[line][id] -= dy[line]; \
  }

#define FINITCOL(linefrom,lineto,colid) \
  gc[colid] = c[linefrom][colid]; \
  gd[colid] = abs(c[lineto][colid] - gc[colid]); \
  ge[colid] = -(gx >> 1); \
  gi[colid] = LINC[c[lineto][colid] > gc[colid]]

#define FDOCOL(linefrom,lineto,colid) \
  ge[colid] += gd[colid]; \
  if (gx != 0) \
  while(ge[colid] > 0) \
  { \
    gc[colid] += gi[colid]; \
    ge[colid] -= gx; \
  }

#define FILLLINE(linefrom,lineto) \
  gx = abs(sx[lineto] - sx[linefrom]); \
  gxi = LINC[sx[linefrom] < sx[lineto]]; \
  FINITCOL(linefrom, lineto, 0); \
  FINITCOL(linefrom, lineto, 1); \
  FINITCOL(linefrom, lineto, 2); \
  g_end = sx[lineto] + gxi; \
  for(g = sx[linefrom]; g != g_end; g += gxi) \
  { \
    if(InY && g >= FillRect.left && g < FillRect.right) \
    { \
      Color = XLATEOBJ_iXlate(pxlo, RGB(gc[0], gc[1], gc[2])); \
      DibFunctionsForBitmapFormat[psoOutput->iBitmapFormat].DIB_PutPixel(psoOutput, g, sy, Color); \
    } \
    FDOCOL(linefrom, lineto, 0); \
    FDOCOL(linefrom, lineto, 1); \
    FDOCOL(linefrom, lineto, 2); \
  }

#define DOLINE(a,b,line) \
  STEPCOL(a, b, line, Red, 0); \
  STEPCOL(a, b, line, Green, 1); \
  STEPCOL(a, b, line, Blue, 2); \
  ex[line] += dx[line]; \
  while(ex[line] > 0 && x[line] != destx[line]) \
  { \
    x[line] += incx[line]; \
    sx[line] += incx[line]; \
    ex[line] -= dy[line]; \
  }

#define GOLINE(a,b,line) \
  if(y >= a->y && y <= b->y) \
  {

#define ENDLINE(a,b,line) \
  }

#define INITLINE(a,b,line) \
  x[line] = a->x; \
  sx[line] = a->x + pptlDitherOrg->x - 1; \
  dx[line] = abs(b->x - a->x); \
  dy[line] = abs(b->y - a->y); \
  incx[line] = LINC[b->x > a->x]; \
  ex[line] = -(dy[line]>>1); \
  destx[line] = b->x

#define DOINIT(a, b, line) \
  INITLINE(a, b, line); \
  INITCOL(a, b, line, Red, 0); \
  INITCOL(a, b, line, Green, 1); \
  INITCOL(a, b, line, Blue, 2);

#define SMALLER(a,b)     (a->y < b->y) || (a->y == b->y && a->x < b->x)

#define SWAP(a,b,c)  c = a;\
                     a = b;\
                     b = c

#define NLINES 3

BOOL
FASTCALL
IntEngGradientFillTriangle(
    IN SURFOBJ  *psoDest,
    IN CLIPOBJ  *pco,
    IN XLATEOBJ  *pxlo,
    IN TRIVERTEX  *pVertex,
    IN ULONG  nVertex,
    IN PGRADIENT_TRIANGLE gTriangle,
    IN RECTL  *prclExtents,
    IN POINTL  *pptlDitherOrg)
{
    SURFOBJ *psoOutput;
    PTRIVERTEX v1, v2, v3;
    RECT_ENUM RectEnum;
    BOOL EnumMore;
    ULONG i;
    POINTL Translate;
    INTENG_ENTER_LEAVE EnterLeave;
    RECTL FillRect = { 0, 0, 0, 0 };
    ULONG Color;
    ULONG fmt;

    BOOL sx[NLINES];
    LONG x[NLINES], dx[NLINES], dy[NLINES], incx[NLINES], ex[NLINES], destx[NLINES];
    LONG sy, y, bt;

    v1 = (pVertex + gTriangle->Vertex1);
    v2 = (pVertex + gTriangle->Vertex2);
    v3 = (pVertex + gTriangle->Vertex3);

    /* bubble sort */
    if (SMALLER(v2, v1))
    {
        TRIVERTEX *t;
        SWAP(v1, v2, t);
    }

    if (SMALLER(v3, v2))
    {
        TRIVERTEX *t;
        SWAP(v2, v3, t);
        if (SMALLER(v2, v1))
        {
            SWAP(v1, v2, t);
        }
    }

    DPRINT("Triangle: (%i,%i) (%i,%i) (%i,%i)\n", v1->x, v1->y, v2->x, v2->y, v3->x, v3->y);

    if (!IntEngEnter(&EnterLeave, psoDest, &FillRect, FALSE, &Translate, &psoOutput))
    {
        return FALSE;
    }

    fmt = IntEngGradientFormat(psoDest);

    if (VCMPCLRS(v1, v2, v3))
    {
      /* Barycentric gradient fill, matching Windows/Wine (device coordinates). */
      LONGLONG llDet;
      LONG dv1x = v1->x + pptlDitherOrg->x, dv1y = v1->y + pptlDitherOrg->y;
      LONG dv2x = v2->x + pptlDitherOrg->x, dv2y = v2->y + pptlDitherOrg->y;
      LONG dv3x = v3->x + pptlDitherOrg->x, dv3y = v3->y + pptlDitherOrg->y;

      llDet = (LONGLONG)(dv3y - dv2y) * (dv3x - dv1x) -
              (LONGLONG)(dv3x - dv2x) * (dv3y - dv1y);
      if (llDet == 0)
      {
        return IntEngLeave(&EnterLeave);
      }

      CLIPOBJ_cEnumStart(pco, FALSE, CT_RECTANGLES, CD_RIGHTDOWN, 0);
      do
      {
        EnumMore = CLIPOBJ_bEnum(pco, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
        for (i = 0; i < RectEnum.c && RectEnum.arcl[i].top <= prclExtents->bottom; i++)
        {
          if (RECTL_bIntersectRect(&FillRect, &RectEnum.arcl[i], prclExtents))
          {
            LONG yy, xx, xl, xr, ex1, ex2;

            for (yy = max(FillRect.top, dv1y); yy < min(FillRect.bottom, dv3y); yy++)
            {
              /* Left/right triangle edge x at this scanline (Wine edge_coord). */
              if (yy < dv2y)
                ex1 = (dv2x > dv1x) ?
                      dv2x + (yy - dv2y) * (dv2x - dv1x) / (dv2y - dv1y) :
                      dv1x + (yy - dv1y) * (dv2x - dv1x) / (dv2y - dv1y);
              else
                ex1 = (dv3x > dv2x) ?
                      dv3x + (yy - dv3y) * (dv3x - dv2x) / (dv3y - dv2y) :
                      dv2x + (yy - dv2y) * (dv3x - dv2x) / (dv3y - dv2y);

              ex2 = (dv3x > dv1x) ?
                    dv3x + (yy - dv3y) * (dv3x - dv1x) / (dv3y - dv1y) :
                    dv1x + (yy - dv1y) * (dv3x - dv1x) / (dv3y - dv1y);

              xl = max(FillRect.left, min(ex1, ex2));
              xr = min(FillRect.right, max(ex1, ex2));

              for (xx = xl; xx < xr; xx++)
              {
                LONGLONG l1, l2, l3;
                LONG cr, cg, cb;

                l1 = (LONGLONG)(dv2y - dv3y) * (xx - dv3x) -
                     (LONGLONG)(dv2x - dv3x) * (yy - dv3y);
                l2 = (LONGLONG)(dv3y - dv1y) * (xx - dv3x) -
                     (LONGLONG)(dv3x - dv1x) * (yy - dv3y);
                l3 = llDet - l1 - l2;

                if (fmt == GRAD_FMT_DITHER16)
                {
                  /* 4x4 Bayer dither in device coordinates (Wine gradient_triangle_555). */
                  LONG bayer = bayer_4x4[yy & 3][xx & 3];

                  cr = (LONG)((v1->Red   * l1 + v2->Red   * l2 + v3->Red   * l3) / llDet / 128) + bayer;
                  cg = (LONG)((v1->Green * l1 + v2->Green * l2 + v3->Green * l3) / llDet / 128) + bayer;
                  cb = (LONG)((v1->Blue  * l1 + v2->Blue  * l2 + v3->Blue  * l3) / llDet / 128) + bayer;

                  cr = min(31, max(0, cr / 16));
                  cg = min(31, max(0, cg / 16));
                  cb = min(31, max(0, cb / 16));

                  cr = (cr << 3) | (cr >> 2);
                  cg = (cg << 3) | (cg >> 2);
                  cb = (cb << 3) | (cb >> 2);

                  Color = XLATEOBJ_iXlate(pxlo, RGB(cr, cg, cb));
                }
                else
                {
                  cr = (LONG)((v1->Red   * l1 + v2->Red   * l2 + v3->Red   * l3) / llDet / 256);
                  cg = (LONG)((v1->Green * l1 + v2->Green * l2 + v3->Green * l3) / llDet / 256);
                  cb = (LONG)((v1->Blue  * l1 + v2->Blue  * l2 + v3->Blue  * l3) / llDet / 256);

                  Color = XLATEOBJ_iXlate(pxlo, RGB(cr, cg, cb));

                  if (fmt == GRAD_FMT_ALPHA)
                  {
                    LONG ca = (LONG)((v1->Alpha * l1 + v2->Alpha * l2 + v3->Alpha * l3) / llDet / 256);
                    Color = (Color & 0x00ffffff) | ((ULONG)ca << 24);
                  }
                }

                DibFunctionsForBitmapFormat[psoOutput->iBitmapFormat].DIB_PutPixel(psoOutput, xx, yy, Color);
              }
            }
          }
        }
      } while (EnumMore);

      return IntEngLeave(&EnterLeave);
    }

    /* fill triangle with one solid color */

    Color = XLATEOBJ_iXlate(pxlo, RGB(v1->Red >> 8, v1->Green >> 8, v1->Blue >> 8));
    CLIPOBJ_cEnumStart(pco, FALSE, CT_RECTANGLES, CD_RIGHTDOWN, 0);
    do
    {
      EnumMore = CLIPOBJ_bEnum(pco, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
      for (i = 0; i < RectEnum.c && RectEnum.arcl[i].top <= prclExtents->bottom; i++)
      {
        if (RECTL_bIntersectRect(&FillRect, &RectEnum.arcl[i], prclExtents))
        {
          S_INITLINE(v1, v3, 0);
          S_INITLINE(v1, v2, 1);
          S_INITLINE(v2, v3, 2);

          y = v1->y;
          sy = v1->y + pptlDitherOrg->y;
          bt = min(v3->y + pptlDitherOrg->y, FillRect.bottom);

          while (sy < bt)
          {
            S_GOLINE(v1, v3, 0);
            S_DOLINE(v1, v3, 0);
            S_ENDLINE(v1, v3, 0);

            S_GOLINE(v1, v2, 1);
            S_DOLINE(v1, v2, 1);
            S_FILLLINE(0, 1);
            S_ENDLINE(v1, v2, 1);

            S_GOLINE(v2, v3, 2);
            S_DOLINE(v2, v3, 2);
            S_FILLLINE(0, 2);
            S_ENDLINE(23, v3, 2);

            y++;
            sy++;
          }
        }
      }
    } while (EnumMore);

    return IntEngLeave(&EnterLeave);
}


static
BOOL
IntEngIsNULLTriangle(TRIVERTEX  *pVertex, GRADIENT_TRIANGLE *gt)
{
    if (COMPAREVERTEX(VERTEX(Vertex1), VERTEX(Vertex2)) &&
        COMPAREVERTEX(VERTEX(Vertex1), VERTEX(Vertex3)))
        return TRUE;
    return FALSE;
}

static
BOOL
IntEngIsDegenerateTriangle(TRIVERTEX  *pVertex, GRADIENT_TRIANGLE *gt)
{
    if (COMPAREVERTEX(VERTEX(Vertex1), VERTEX(Vertex2)))
        return TRUE;
    if (COMPAREVERTEX(VERTEX(Vertex1), VERTEX(Vertex3)))
        return TRUE;
    if (COMPAREVERTEX(VERTEX(Vertex2), VERTEX(Vertex3)))
        return TRUE;
    return FALSE;
}


BOOL
APIENTRY
EngGradientFill(
    _Inout_ SURFOBJ *psoDest,
    _In_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_ TRIVERTEX *pVertex,
    _In_ ULONG nVertex,
    _In_ PVOID pMesh,
    _In_ ULONG nMesh,
    _In_ RECTL *prclExtents,
    _In_ POINTL *pptlDitherOrg,
    _In_ ULONG ulMode)
{
    ULONG i;
    BOOL ret = FALSE;

    /* Check for NULL clip object */
    if (pco == NULL)
    {
        /* Use the trivial one instead */
        pco = (CLIPOBJ *)&gxcoTrivial;//.coClient;
    }

    switch(ulMode)
    {
        case GRADIENT_FILL_RECT_H:
        case GRADIENT_FILL_RECT_V:
        {
            PGRADIENT_RECT gr = (PGRADIENT_RECT)pMesh;
            for (i = 0; i < nMesh; i++, gr++)
            {
                if (!IntEngGradientFillRect(psoDest,
                                            pco,
                                            pxlo,
                                            pVertex,
                                            nVertex,
                                            gr,
                                            prclExtents,
                                            pptlDitherOrg,
                                            (ulMode == GRADIENT_FILL_RECT_H)))
                {
                    break;
                }
            }
            ret = TRUE;
            break;
        }
        case GRADIENT_FILL_TRIANGLE:
        {
            PGRADIENT_TRIANGLE gt = (PGRADIENT_TRIANGLE)pMesh;
            for (i = 0; i < nMesh; i++, gt++)
            {
                if (IntEngIsNULLTriangle(pVertex, gt))
                {
                    /* skip empty triangles */
                    continue;
                }
                if (IntEngIsDegenerateTriangle(pVertex, gt))
                {
                    return FALSE;
                }
                if (!IntEngGradientFillTriangle(psoDest,
                                                pco,
                                                pxlo,
                                                pVertex,
                                                nVertex,
                                                gt,
                                                prclExtents,
                                                pptlDitherOrg))
                {
                    break;
                }
            }
            ret = TRUE;
            break;
        }
    }

    return ret;
}

BOOL
APIENTRY
IntEngGradientFill(
    IN SURFOBJ *psoDest,
    IN CLIPOBJ *pco,
    IN XLATEOBJ *pxlo,
    IN TRIVERTEX *pVertex,
    IN ULONG nVertex,
    IN PVOID pMesh,
    IN ULONG nMesh,
    IN RECTL *prclExtents,
    IN POINTL *pptlDitherOrg,
    IN ULONG ulMode)
{
    BOOL Ret;
    SURFACE *psurf;
    ASSERT(psoDest);

    psurf = CONTAINING_RECORD(psoDest, SURFACE, SurfObj);
    ASSERT(psurf);

    /*
     * Windows' GdiGradientFill leaves a 1bpp device-dependent bitmap (a DDB
     * created e.g. via CreateBitmap, with no backing DIB section) completely
     * untouched - it renders nothing.  This was verified against the gdi32:dib
     * winetest oracle: the "1 ddb" / "1 ddb custom colors inverted" expected
     * hashes equal the SHA1 of the unmodified 0xCC reset pattern for both the
     * rect and triangle gradient checkpoints.  A 1bpp DIB section (hDIBSection
     * set) is still rendered normally.  Match that behaviour here.
     */
    if ((psoDest->iBitmapFormat == BMF_1BPP) && (psurf->hDIBSection == NULL))
    {
        return TRUE;
    }

    if (psurf->flags & HOOK_GRADIENTFILL)
    {
        Ret = GDIDEVFUNCS(psoDest).GradientFill(psoDest,
                                                pco,
                                                pxlo,
                                                pVertex,
                                                nVertex,
                                                pMesh,
                                                nMesh,
                                                prclExtents,
                                                pptlDitherOrg,
                                                ulMode);
    }
    else
    {
        Ret = EngGradientFill(psoDest,
                              pco,
                              pxlo,
                              pVertex,
                              nVertex,
                              pMesh,
                              nMesh,
                              prclExtents,
                              pptlDitherOrg,
                              ulMode);
    }

    return Ret;
}
