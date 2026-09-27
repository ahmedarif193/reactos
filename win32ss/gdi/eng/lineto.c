/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * PURPOSE:          Line functions
 * FILE:             win32ss/gdi/eng/lineto.c
 * PROGRAMER:        ReactOS Team
 */

#include <win32k.h>

#define NDEBUG
#include <debug.h>

static void FASTCALL
TranslateRects(RECT_ENUM *RectEnum, POINTL* Translate)
{
    RECTL* CurrentRect;

    if (0 != Translate->x || 0 != Translate->y)
    {
        for (CurrentRect = RectEnum->arcl; CurrentRect < RectEnum->arcl + RectEnum->c; CurrentRect++)
        {
            CurrentRect->left += Translate->x;
            CurrentRect->right += Translate->x;
            CurrentRect->top += Translate->y;
            CurrentRect->bottom += Translate->y;
        }
    }
}

static ULONG
LineRop2Pixel(ULONG Rop2, ULONG Pen, ULONG Dest)
{
    switch (Rop2)
    {
        case R2_BLACK: return 0;
        case R2_NOTMERGEPEN: return ~(Pen | Dest);
        case R2_MASKNOTPEN: return ~Pen & Dest;
        case R2_NOTCOPYPEN: return ~Pen;
        case R2_MASKPENNOT: return Pen & ~Dest;
        case R2_NOT: return ~Dest;
        case R2_XORPEN: return Pen ^ Dest;
        case R2_NOTMASKPEN: return ~(Pen & Dest);
        case R2_MASKPEN: return Pen & Dest;
        case R2_NOTXORPEN: return ~(Pen ^ Dest);
        case R2_NOP: return Dest;
        case R2_MERGENOTPEN: return ~Pen | Dest;
        case R2_MERGEPENNOT: return Pen | ~Dest;
        case R2_MERGEPEN: return Pen | Dest;
        case R2_WHITE: return ~0UL;
        default: return Pen;
    }
}

static VOID
LinePutPixel(SURFOBJ *OutputObj, LONG x, LONG y, ULONG Pixel, ULONG Rop2)
{
    if (Rop2 != R2_COPYPEN)
    {
        Pixel = LineRop2Pixel(Rop2, Pixel,
            DibFunctionsForBitmapFormat[OutputObj->iBitmapFormat].DIB_GetPixel(OutputObj, x, y));
    }
    DibFunctionsForBitmapFormat[OutputObj->iBitmapFormat].DIB_PutPixel(OutputObj, x, y, Pixel);
}

static LONG
LineStyleLength(PEBRUSHOBJ pebo, ULONG iStyle)
{
    ULONG PenStyle = pebo->pbrush->ulPenStyle & PS_STYLE_MASK;
    LONG Length = pebo->pbrush->pStyle[iStyle];

    if (PenStyle >= PS_DASH && PenStyle <= PS_DASHDOTDOT)
        Length *= 3;

    return Length;
}

LONG
HandleStyles(
    BRUSHOBJ *pbo,
    POINTL* Translate,
    LONG x,
    LONG y,
    LONG deltax,
    LONG deltay,
    LONG dx,
    LONG dy,
    PULONG piStyle,
    LONG lStyle)
{
    PEBRUSHOBJ pebo = (PEBRUSHOBJ)pbo;
    ULONG cStyles = pebo->pbrush->dwStyleCount;
    LONG lTotal = 0, lLength;
    ULONG i;

    UNREFERENCED_PARAMETER(Translate);

    *piStyle = 0;
    if (cStyles == 0)
        return MAXLONG;

    for (i = 0; i < cStyles; i++)
        lTotal += LineStyleLength(pebo, i);
    if (lTotal <= 0)
        return MAXLONG;

    lStyle %= lTotal;
    for (i = 0; ; i++)
    {
        lLength = LineStyleLength(pebo, i);
        if (lStyle < lLength)
            break;
        lStyle -= lLength;
    }
    *piStyle = i;
    lLength -= lStyle;

    if (deltax < deltay)
        return y + dy * lLength;

    return x + dx * lLength;
}

/*
 * Draw a line from top-left to bottom-right
 */
void FASTCALL
NWtoSE(SURFOBJ* OutputObj, CLIPOBJ* Clip,
       BRUSHOBJ* pbo, LONG x, LONG y, LONG deltax, LONG deltay,
       POINTL* Translate, ULONG Mix, ULONG iBackColor, LONG lStyle)
{
    int i;
    int error;
    BOOLEAN EnumMore;
    RECTL* ClipRect;
    RECT_ENUM RectEnum;
    ULONG Pixel = pbo->iSolidColor;
    ULONG Rop2 = Mix & 0xFF, Rop2Back = (Mix >> 8) & 0xFF;
    LONG delta;
    PEBRUSHOBJ pebo = (PEBRUSHOBJ)pbo;
    ULONG iStyle, cStyles = pebo->pbrush->dwStyleCount;
    LONG lStyleMax;

    lStyleMax = HandleStyles(pbo, Translate, x, y, deltax, deltay, 1, 1, &iStyle, lStyle);

    CLIPOBJ_cEnumStart(Clip, FALSE, CT_RECTANGLES, CD_RIGHTDOWN, 0);
    EnumMore = CLIPOBJ_bEnum(Clip, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
    TranslateRects(&RectEnum, Translate);
    ClipRect = RectEnum.arcl;
    delta = max(deltax, deltay);
    i = 0;
    error = (delta - 1) >> 1;
    while (i < delta && (ClipRect < RectEnum.arcl + RectEnum.c || EnumMore))
    {
        while ((ClipRect < RectEnum.arcl + RectEnum.c /* there's still a current clip rect */
                && (ClipRect->bottom <= y             /* but it's above us */
                    || (ClipRect->top <= y && ClipRect->right <= x))) /* or to the left of us */
                || EnumMore)                           /* no current clip rect, but rects left */
        {
            /* Skip to the next clip rect */
            if (RectEnum.arcl + RectEnum.c <= ClipRect)
            {
                EnumMore = CLIPOBJ_bEnum(Clip, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
                TranslateRects(&RectEnum, Translate);
                ClipRect = RectEnum.arcl;
            }
            else
            {
                ClipRect++;
            }
        }
        if (ClipRect < RectEnum.arcl + RectEnum.c) /* If there's no current clip rect we're done */
        {
            if (ClipRect->left <= x && ClipRect->top <= y)
            {
                if ((iStyle & 1) == 0)
                    LinePutPixel(OutputObj, x, y, Pixel, Rop2);
                else if (Rop2Back != R2_NOP)
                    LinePutPixel(OutputObj, x, y, iBackColor, Rop2Back);
            }
            if (deltax < deltay)
            {
                y++;
                if (y == lStyleMax)
                {
                    ASSERT(cStyles);
                    iStyle = (iStyle + 1) % cStyles;
                    lStyleMax = y + LineStyleLength(pebo, iStyle);
                }
                error = error + deltax;
                if (deltay <= error)
                {
                    x++;
                    error = error - deltay;
                }
            }
            else
            {
                x++;
                if (x == lStyleMax)
                {
                    ASSERT(cStyles);
                    iStyle = (iStyle + 1) % cStyles;
                    lStyleMax = x + LineStyleLength(pebo, iStyle);
                }
                error = error + deltay;
                if (deltax <= error)
                {
                    y++;
                    error = error - deltax;
                }
            }
            i++;
        }
    }
}

void FASTCALL
SWtoNE(SURFOBJ* OutputObj, CLIPOBJ* Clip,
       BRUSHOBJ* pbo, LONG x, LONG y, LONG deltax, LONG deltay,
       POINTL* Translate, ULONG Mix, ULONG iBackColor, LONG lStyle)
{
    int i;
    int error;
    BOOLEAN EnumMore;
    RECTL* ClipRect;
    RECT_ENUM RectEnum;
    ULONG Pixel = pbo->iSolidColor;
    ULONG Rop2 = Mix & 0xFF, Rop2Back = (Mix >> 8) & 0xFF;
    LONG delta;
    PEBRUSHOBJ pebo = (PEBRUSHOBJ)pbo;
    ULONG iStyle, cStyles = pebo->pbrush->dwStyleCount;
    LONG lStyleMax;

    lStyleMax = HandleStyles(pbo, Translate, x, y, deltax, deltay, 1, -1, &iStyle, lStyle);

    CLIPOBJ_cEnumStart(Clip, FALSE, CT_RECTANGLES, CD_RIGHTUP, 0);
    EnumMore = CLIPOBJ_bEnum(Clip, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
    TranslateRects(&RectEnum, Translate);
    ClipRect = RectEnum.arcl;
    delta = max(deltax, deltay);
    i = 0;
    error = (delta - 1 + (deltax > deltay ? 1 : 0)) >> 1;
    while (i < delta && (ClipRect < RectEnum.arcl + RectEnum.c || EnumMore))
    {
        while ((ClipRect < RectEnum.arcl + RectEnum.c
                && (y < ClipRect->top
                    || (y < ClipRect->bottom && ClipRect->right <= x)))
                || EnumMore)
        {
            if (RectEnum.arcl + RectEnum.c <= ClipRect)
            {
                EnumMore = CLIPOBJ_bEnum(Clip, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
                TranslateRects(&RectEnum, Translate);
                ClipRect = RectEnum.arcl;
            }
            else
            {
                ClipRect++;
            }
        }
        if (ClipRect < RectEnum.arcl + RectEnum.c)
        {
            if (ClipRect->left <= x && y < ClipRect->bottom)
            {
                if ((iStyle & 1) == 0)
                    LinePutPixel(OutputObj, x, y, Pixel, Rop2);
                else if (Rop2Back != R2_NOP)
                    LinePutPixel(OutputObj, x, y, iBackColor, Rop2Back);
            }
            if (deltax < deltay)
            {
                y--;
                if (y == lStyleMax)
                {
                    ASSERT(cStyles);
                    iStyle = (iStyle - 1) % cStyles;
                    lStyleMax = y - LineStyleLength(pebo, iStyle);
                }
                error = error + deltax;
                if (deltay <= error)
                {
                    x++;
                    error = error - deltay;
                }
            }
            else
            {
                x++;
                if (x == lStyleMax)
                {
                    ASSERT(cStyles);
                    iStyle = (iStyle + 1) % cStyles;
                    lStyleMax = x + LineStyleLength(pebo, iStyle);
                }
                error = error + deltay;
                if (deltax <= error)
                {
                    y--;
                    error = error - deltax;
                }
            }
            i++;
        }
    }
}

void FASTCALL
NEtoSW(SURFOBJ* OutputObj, CLIPOBJ* Clip,
       BRUSHOBJ* pbo, LONG x, LONG y, LONG deltax, LONG deltay,
       POINTL* Translate, ULONG Mix, ULONG iBackColor, LONG lStyle)
{
    int i;
    int error;
    BOOLEAN EnumMore;
    RECTL* ClipRect;
    RECT_ENUM RectEnum;
    ULONG Pixel = pbo->iSolidColor;
    ULONG Rop2 = Mix & 0xFF, Rop2Back = (Mix >> 8) & 0xFF;
    LONG delta;
    PEBRUSHOBJ pebo = (PEBRUSHOBJ)pbo;
    ULONG iStyle, cStyles = pebo->pbrush->dwStyleCount;
    LONG lStyleMax;

    lStyleMax = HandleStyles(pbo, Translate, x, y, deltax, deltay, -1, 1, &iStyle, lStyle);

    CLIPOBJ_cEnumStart(Clip, FALSE, CT_RECTANGLES, CD_LEFTDOWN, 0);
    EnumMore = CLIPOBJ_bEnum(Clip, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
    TranslateRects(&RectEnum, Translate);
    ClipRect = RectEnum.arcl;
    delta = max(deltax, deltay);
    i = 0;
    error = (delta - 1 + (deltax > deltay ? 0 : 1)) >> 1;
    while (i < delta && (ClipRect < RectEnum.arcl + RectEnum.c || EnumMore))
    {
        while ((ClipRect < RectEnum.arcl + RectEnum.c
                && (ClipRect->bottom <= y
                    || (ClipRect->top <= y && x < ClipRect->left)))
                || EnumMore)
        {
            if (RectEnum.arcl + RectEnum.c <= ClipRect)
            {
                EnumMore = CLIPOBJ_bEnum(Clip, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
                TranslateRects(&RectEnum, Translate);
                ClipRect = RectEnum.arcl;
            }
            else
            {
                ClipRect++;
            }
        }
        if (ClipRect < RectEnum.arcl + RectEnum.c)
        {
            if (x < ClipRect->right && ClipRect->top <= y)
            {
                if ((iStyle & 1) == 0)
                    LinePutPixel(OutputObj, x, y, Pixel, Rop2);
                else if (Rop2Back != R2_NOP)
                    LinePutPixel(OutputObj, x, y, iBackColor, Rop2Back);
            }
            if (deltax < deltay)
            {
                y++;
                if (y == lStyleMax)
                {
                    ASSERT(cStyles);
                    iStyle = (iStyle + 1) % cStyles;
                    lStyleMax = y + LineStyleLength(pebo, iStyle);
                }
                error = error + deltax;
                if (deltay <= error)
                {
                    x--;
                    error = error - deltay;
                }
            }
            else
            {
                x--;
                if (x == lStyleMax)
                {
                    ASSERT(cStyles);
                    iStyle = (iStyle - 1) % cStyles;
                    lStyleMax = x - LineStyleLength(pebo, iStyle);
                }
                error = error + deltay;
                if (deltax <= error)
                {
                    y++;
                    error = error - deltax;
                }
            }
            i++;
        }
    }
}

void FASTCALL
SEtoNW(SURFOBJ* OutputObj, CLIPOBJ* Clip,
       BRUSHOBJ* pbo, LONG x, LONG y, LONG deltax, LONG deltay,
       POINTL* Translate, ULONG Mix, ULONG iBackColor, LONG lStyle)
{
    int i;
    int error;
    BOOLEAN EnumMore;
    RECTL* ClipRect;
    RECT_ENUM RectEnum;
    ULONG Pixel = pbo->iSolidColor;
    ULONG Rop2 = Mix & 0xFF, Rop2Back = (Mix >> 8) & 0xFF;
    LONG delta;
    PEBRUSHOBJ pebo = (PEBRUSHOBJ)pbo;
    ULONG iStyle, cStyles = pebo->pbrush->dwStyleCount;
    LONG lStyleMax;

    lStyleMax = HandleStyles(pbo, Translate, x, y, deltax, deltay, -1, -1, &iStyle, lStyle);

    CLIPOBJ_cEnumStart(Clip, FALSE, CT_RECTANGLES, CD_LEFTUP, 0);
    EnumMore = CLIPOBJ_bEnum(Clip, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
    TranslateRects(&RectEnum, Translate);
    ClipRect = RectEnum.arcl;
    delta = max(deltax, deltay);
    i = 0;
    error = delta >> 1;
    while (i < delta && (ClipRect < RectEnum.arcl + RectEnum.c || EnumMore))
    {
        while ((ClipRect < RectEnum.arcl + RectEnum.c
                && (y < ClipRect->top
                    || (y < ClipRect->bottom && x < ClipRect->left)))
                || EnumMore)
        {
            if (RectEnum.arcl + RectEnum.c <= ClipRect)
            {
                EnumMore = CLIPOBJ_bEnum(Clip, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
                TranslateRects(&RectEnum, Translate);
                ClipRect = RectEnum.arcl;
            }
            else
            {
                ClipRect++;
            }
        }
        if (ClipRect < RectEnum.arcl + RectEnum.c)
        {
            if (x < ClipRect->right && y < ClipRect->bottom)
            {
                if ((iStyle & 1) == 0)
                    LinePutPixel(OutputObj, x, y, Pixel, Rop2);
                else if (Rop2Back != R2_NOP)
                    LinePutPixel(OutputObj, x, y, iBackColor, Rop2Back);
            }
            if (deltax < deltay)
            {
                y--;
                if (y == lStyleMax)
                {
                    ASSERT(cStyles);
                    iStyle = (iStyle - 1) % cStyles;
                    lStyleMax = y - LineStyleLength(pebo, iStyle);
                }
                error = error + deltax;
                if (deltay <= error)
                {
                    x--;
                    error = error - deltay;
                }
            }
            else
            {
                x--;
                if (x == lStyleMax)
                {
                    ASSERT(cStyles);
                    iStyle = (iStyle - 1) % cStyles;
                    lStyleMax = x - LineStyleLength(pebo, iStyle);
                }
                error = error + deltay;
                if (deltax <= error)
                {
                    y--;
                    error = error - deltax;
                }
            }
            i++;
        }
    }
}

static BOOL
EngLineToWorker(
    _Inout_ SURFOBJ *DestObj,
    _In_ CLIPOBJ *Clip,
    _In_ BRUSHOBJ *pbo,
    _In_ LONG x1,
    _In_ LONG y1,
    _In_ LONG x2,
    _In_ LONG y2,
    _In_opt_ RECTL *RectBounds,
    _In_ MIX mix,
    _In_ ULONG iBackColor,
    _In_ LONG lStyle)
{
    LONG x, y, deltax, deltay, xchange, ychange, hx, vy;
    ULONG i;
    ULONG Pixel = pbo->iSolidColor;
    SURFOBJ *OutputObj;
    RECTL DestRect;
    POINTL Translate;
    INTENG_ENTER_LEAVE EnterLeave;
    RECT_ENUM RectEnum;
    BOOL EnumMore;
    CLIPOBJ *pcoPriv = NULL;
    PEBRUSHOBJ pebo = (PEBRUSHOBJ)pbo;
    ULONG cStyles = pebo->pbrush->dwStyleCount;
    ULONG Rop2 = mix & 0xFF;
    ULONG Rop2Back = (mix >> 8) & 0xFF;

    if (Rop2 < R2_BLACK || Rop2 > R2_WHITE)
        Rop2 = R2_COPYPEN;
    if (Rop2Back < R2_BLACK || Rop2Back > R2_WHITE)
        Rop2Back = R2_NOP;
    mix = Rop2 | (Rop2Back << 8);

    if (x1 < x2)
    {
        DestRect.left = x1;
        DestRect.right = x2;
    }
    else
    {
        DestRect.left = x2;
        DestRect.right = x1 + 1;
    }
    if (y1 < y2)
    {
        DestRect.top = y1;
        DestRect.bottom = y2;
    }
    else
    {
        DestRect.top = y2;
        DestRect.bottom = y1 + 1;
    }

    if (! IntEngEnter(&EnterLeave, DestObj, &DestRect, FALSE, &Translate, &OutputObj))
    {
        return FALSE;
    }

    if (!Clip)
    {
        Clip = pcoPriv = EngCreateClip();
        if (!Clip)
        {
            return FALSE;
        }
        IntEngUpdateClipRegion((XCLIPOBJ*)Clip, 0, 0, RectBounds);
    }

    x1 += Translate.x;
    x2 += Translate.x;
    y1 += Translate.y;
    y2 += Translate.y;

    x = x1;
    y = y1;
    deltax = x2 - x1;
    deltay = y2 - y1;

    if (0 == deltax && 0 == deltay)
    {
        return TRUE;
    }

    if (deltax < 0)
    {
        xchange = -1;
        deltax = - deltax;
        hx = x2 + 1;
    }
    else
    {
        xchange = 1;
        hx = x1;
    }

    if (deltay < 0)
    {
        ychange = -1;
        deltay = - deltay;
        vy = y2 + 1;
    }
    else
    {
        ychange = 1;
        vy = y1;
    }

    if ((y1 == y2) && (cStyles == 0))
    {
        CLIPOBJ_cEnumStart(Clip, FALSE, CT_RECTANGLES, CD_RIGHTDOWN, 0);
        do
        {
            EnumMore = CLIPOBJ_bEnum(Clip, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
            for (i = 0; i < RectEnum.c && RectEnum.arcl[i].top + Translate.y <= y1; i++)
            {
                if (y1 < RectEnum.arcl[i].bottom + Translate.y &&
                        RectEnum.arcl[i].left + Translate.x <= hx + deltax &&
                        hx < RectEnum.arcl[i].right + Translate.x &&
                        max(hx, RectEnum.arcl[i].left + Translate.x) <
                        min(hx + deltax, RectEnum.arcl[i].right + Translate.x))
                {
                    LONG xs = max(hx, RectEnum.arcl[i].left + Translate.x);
                    LONG xe = min(hx + deltax, RectEnum.arcl[i].right + Translate.x);

                    if (Rop2 == R2_COPYPEN)
                    {
                        DibFunctionsForBitmapFormat[OutputObj->iBitmapFormat].DIB_HLine(
                            OutputObj, xs, xe, y1, Pixel);
                    }
                    else
                    {
                        for (; xs < xe; xs++)
                            LinePutPixel(OutputObj, xs, y1, Pixel, Rop2);
                    }
                }
            }
        }
        while (EnumMore);
    }
    else if ((x1 == x2) && (cStyles == 0))
    {
        CLIPOBJ_cEnumStart(Clip, FALSE, CT_RECTANGLES, CD_RIGHTDOWN, 0);
        do
        {
            EnumMore = CLIPOBJ_bEnum(Clip, (ULONG) sizeof(RectEnum), (PVOID) &RectEnum);
            for (i = 0; i < RectEnum.c; i++)
            {
                if (RectEnum.arcl[i].left + Translate.x <= x1 &&
                        x1 < RectEnum.arcl[i].right + Translate.x &&
                        RectEnum.arcl[i].top + Translate.y <= vy + deltay &&
                        vy < RectEnum.arcl[i].bottom + Translate.y)
                {
                    LONG ys = max(vy, RectEnum.arcl[i].top + Translate.y);
                    LONG ye = min(vy + deltay, RectEnum.arcl[i].bottom + Translate.y);

                    if (Rop2 == R2_COPYPEN)
                    {
                        DibFunctionsForBitmapFormat[OutputObj->iBitmapFormat].DIB_VLine(
                            OutputObj, x1, ys, ye, Pixel);
                    }
                    else
                    {
                        for (; ys < ye; ys++)
                            LinePutPixel(OutputObj, x1, ys, Pixel, Rop2);
                    }
                }
            }
        }
        while (EnumMore);
    }
    else
    {
        if (0 < xchange)
        {
            if (0 < ychange)
            {
                NWtoSE(OutputObj, Clip, pbo, x, y, deltax, deltay, &Translate, mix, iBackColor, lStyle);
            }
            else
            {
                SWtoNE(OutputObj, Clip, pbo, x, y, deltax, deltay, &Translate, mix, iBackColor, lStyle);
            }
        }
        else
        {
            if (0 < ychange)
            {
                NEtoSW(OutputObj, Clip, pbo, x, y, deltax, deltay, &Translate, mix, iBackColor, lStyle);
            }
            else
            {
                SEtoNW(OutputObj, Clip, pbo, x, y, deltax, deltay, &Translate, mix, iBackColor, lStyle);
            }
        }
    }

    if (pcoPriv)
    {
        EngDeleteClip(pcoPriv);
    }

    return IntEngLeave(&EnterLeave);
}

/*
 * @implemented
 */
BOOL APIENTRY
EngLineTo(
    _Inout_ SURFOBJ *DestObj,
    _In_ CLIPOBJ *Clip,
    _In_ BRUSHOBJ *pbo,
    _In_ LONG x1,
    _In_ LONG y1,
    _In_ LONG x2,
    _In_ LONG y2,
    _In_opt_ RECTL *RectBounds,
    _In_ MIX mix)
{
    return EngLineToWorker(DestObj, Clip, pbo, x1, y1, x2, y2, RectBounds,
                           (mix & 0xFF) | (R2_NOP << 8), 0, 0);
}

BOOL APIENTRY
IntEngLineToEx(SURFOBJ *psoDest,
               CLIPOBJ *ClipObj,
               BRUSHOBJ *pbo,
               LONG x1,
               LONG y1,
               LONG x2,
               LONG y2,
               RECTL *RectBounds,
               MIX Mix,
               ULONG iBackColor,
               PLONG plStyle)
{
    BOOLEAN ret;
    SURFACE *psurfDest;
    PEBRUSHOBJ GdiBrush;
    RECTL b;
    LONG lStyle = 0;

    ASSERT(psoDest);
    psurfDest = CONTAINING_RECORD(psoDest, SURFACE, SurfObj);
    ASSERT(psurfDest);

    GdiBrush = CONTAINING_RECORD(
                   pbo,
                   EBRUSHOBJ,
                   BrushObject);
    ASSERT(GdiBrush);
    ASSERT(GdiBrush->pbrush);

    if (GdiBrush->pbrush->flAttrs & BR_IS_NULL)
        return TRUE;

    if (plStyle)
    {
        lStyle = *plStyle;
        *plStyle += max(abs(x2 - x1), abs(y2 - y1));
    }

    /* No success yet */
    ret = FALSE;

    /* Clip lines totally outside the clip region. This is not done as an
     * optimization (there are very few lines drawn outside the region) but
     * as a workaround for what seems to be a problem in the CL54XX driver */
    if (NULL == ClipObj || DC_TRIVIAL == ClipObj->iDComplexity)
    {
        b.left = 0;
        b.right = psoDest->sizlBitmap.cx;
        b.top = 0;
        b.bottom = psoDest->sizlBitmap.cy;
    }
    else
    {
        b = ClipObj->rclBounds;
    }
    if ((x1 < b.left && x2 < b.left) || (b.right <= x1 && b.right <= x2) ||
            (y1 < b.top && y2 < b.top) || (b.bottom <= y1 && b.bottom <= y2))
    {
        return TRUE;
    }

    b.left = min(x1, x2);
    b.right = max(x1, x2);
    b.top = min(y1, y2);
    b.bottom = max(y1, y2);
    if (b.left == b.right) b.right++;
    if (b.top == b.bottom) b.bottom++;

    if (psurfDest->flags & HOOK_LINETO)
    {
        /* Call the driver's DrvLineTo */
        ret = GDIDEVFUNCS(psoDest).LineTo(
                  psoDest, ClipObj, pbo, x1, y1, x2, y2, &b, Mix);
    }

#if 0
    if (! ret && (psurfDest->flags & HOOK_STROKEPATH))
    {
        /* FIXME: Emulate LineTo using drivers DrvStrokePath and set ret on success */
    }
#endif

    if (! ret)
    {
        ret = EngLineToWorker(psoDest, ClipObj, pbo, x1, y1, x2, y2, RectBounds,
                              Mix, iBackColor, lStyle);
    }

    return ret;
}

BOOL APIENTRY
IntEngLineTo(SURFOBJ *psoDest,
             CLIPOBJ *ClipObj,
             BRUSHOBJ *pbo,
             LONG x1,
             LONG y1,
             LONG x2,
             LONG y2,
             RECTL *RectBounds,
             MIX Mix)
{
    return IntEngLineToEx(psoDest, ClipObj, pbo, x1, y1, x2, y2, RectBounds,
                          (Mix & 0xFF) | (R2_NOP << 8), 0, NULL);
}

BOOL APIENTRY
IntEngPolyline(SURFOBJ *psoDest,
               CLIPOBJ *Clip,
               BRUSHOBJ *pbo,
               CONST LPPOINT  pt,
               LONG dCount,
               MIX Mix,
               ULONG iBackColor)
{
    LONG i;
    RECTL rect;
    BOOL ret = FALSE;
    LONG lStyle = 0;

    // Draw the Polyline with a call to IntEngLineTo for each segment.
    for (i = 1; i < dCount; i++)
    {
        rect.left = min(pt[i-1].x, pt[i].x);
        rect.top = min(pt[i-1].y, pt[i].y);
        rect.right = max(pt[i-1].x, pt[i].x);
        rect.bottom = max(pt[i-1].y, pt[i].y);
        ret = IntEngLineToEx(psoDest,
                             Clip,
                             pbo,
                             pt[i-1].x,
                             pt[i-1].y,
                             pt[i].x,
                             pt[i].y,
                             &rect,
                             Mix,
                             iBackColor,
                             &lStyle);
        if (!ret)
        {
            break;
        }
    }

    return ret;
}

/* EOF */
