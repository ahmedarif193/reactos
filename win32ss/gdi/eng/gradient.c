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

#define VERTEX(n) (pVertex + gt->n)
#define COMPAREVERTEX(a, b) ((a)->x == (b)->x && (a)->y == (b)->y)

static const BYTE gajBayer4x4[4][4] =
{
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

typedef struct _GRADIENT_OUT
{
    SURFOBJ *psoOutput;
    XLATEOBJ *pxlo;
    POINTL Translate;
    BOOL bAlpha;
} GRADIENT_OUT, *PGRADIENT_OUT;

static ULONG
GradientDither(ULONG c, LONG x, LONG y)
{
    LONG v = (LONG)(c / 128) + gajBayer4x4[y & 3][x & 3];

    v = min(31, max(0, v / 16));
    return (v << 3) | (v >> 2);
}

static VOID
GradientPutPixel(PGRADIENT_OUT pgo, LONG x, LONG y, ULONG r, ULONG g, ULONG b, ULONG a)
{
    ULONG ulColor;

    if (pgo->psoOutput->iBitmapFormat == BMF_16BPP)
    {
        r = GradientDither(r, x, y);
        g = GradientDither(g, x, y);
        b = GradientDither(b, x, y);
    }
    else
    {
        r >>= 8;
        g >>= 8;
        b >>= 8;
    }

    ulColor = XLATEOBJ_iXlate(pgo->pxlo, RGB(r, g, b));
    if (pgo->bAlpha)
        ulColor |= (a >> 8) << 24;

    DibFunctionsForBitmapFormat[pgo->psoOutput->iBitmapFormat].DIB_PutPixel(
        pgo->psoOutput, x + pgo->Translate.x, y + pgo->Translate.y, ulColor);
}

static BOOL
GradientBegin(PGRADIENT_OUT pgo, INTENG_ENTER_LEAVE *pEnterLeave, SURFOBJ *psoDest,
              XLATEOBJ *pxlo, RECTL *prclBounds)
{
    SURFACE *psurf = CONTAINING_RECORD(psoDest, SURFACE, SurfObj);

    if (!IntEngEnter(pEnterLeave, psoDest, prclBounds, FALSE, &pgo->Translate, &pgo->psoOutput))
        return FALSE;

    pgo->pxlo = pxlo;
    pgo->bAlpha = (psoDest->iBitmapFormat == BMF_32BPP) && psurf->ppal &&
                  (psurf->ppal->flFlags & PAL_BGR) &&
                  !(psurf->ppal->flFlags & PAL_BITFIELDS);
    return TRUE;
}

static LONG
GradientEdgeCoord(LONG y, LONG x1, LONG y1, LONG x2, LONG y2)
{
    if (x2 > x1)
        return x2 + (y - y2) * (x2 - x1) / (y2 - y1);
    return x1 + (y - y1) * (x2 - x1) / (y2 - y1);
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
    GRADIENT_OUT go;
    INTENG_ENTER_LEAVE EnterLeave;
    RECT_ENUM RectEnum;
    TRIVERTEX v[2];
    RECTL rcBounds, rcFill;
    BOOL EnumMore;
    ULONG i, i0, i1;
    LONG x, y;
    ULONGLONG len, pos;

    i0 = gRect->UpperLeft;
    i1 = gRect->LowerRight;
    if (i0 >= nVertex || i1 >= nVertex)
        return FALSE;

    if (Horizontal ? (pVertex[i1].x < pVertex[i0].x) : (pVertex[i1].y < pVertex[i0].y))
    {
        ULONG iTmp = i0;
        i0 = i1;
        i1 = iTmp;
    }
    v[0] = pVertex[i0];
    v[1] = pVertex[i1];

    if (Horizontal)
    {
        rcBounds.left = v[0].x;
        rcBounds.right = v[1].x;
        rcBounds.top = min(v[0].y, v[1].y);
        rcBounds.bottom = max(v[0].y, v[1].y);
        len = v[1].x - v[0].x;
    }
    else
    {
        rcBounds.left = min(v[0].x, v[1].x);
        rcBounds.right = max(v[0].x, v[1].x);
        rcBounds.top = v[0].y;
        rcBounds.bottom = v[1].y;
        len = v[1].y - v[0].y;
    }

    if (rcBounds.left >= rcBounds.right || rcBounds.top >= rcBounds.bottom || !len)
        return TRUE;

    if (!GradientBegin(&go, &EnterLeave, psoDest, pxlo, &rcBounds))
        return FALSE;

    CLIPOBJ_cEnumStart(pco, FALSE, CT_RECTANGLES, CD_RIGHTDOWN, 0);
    do
    {
        EnumMore = CLIPOBJ_bEnum(pco, (ULONG)sizeof(RectEnum), (PVOID)&RectEnum);
        for (i = 0; i < RectEnum.c; i++)
        {
            if (!RECTL_bIntersectRect(&rcFill, &RectEnum.arcl[i], &rcBounds))
                continue;

            for (y = rcFill.top; y < rcFill.bottom; y++)
            {
                for (x = rcFill.left; x < rcFill.right; x++)
                {
                    pos = Horizontal ? (ULONGLONG)(x - v[0].x) : (ULONGLONG)(y - v[0].y);
                    GradientPutPixel(&go, x, y,
                        (ULONG)((v[0].Red   * (len - pos) + v[1].Red   * pos) / len),
                        (ULONG)((v[0].Green * (len - pos) + v[1].Green * pos) / len),
                        (ULONG)((v[0].Blue  * (len - pos) + v[1].Blue  * pos) / len),
                        (ULONG)((v[0].Alpha * (len - pos) + v[1].Alpha * pos) / len));
                }
            }
        }
    }
    while (EnumMore);

    return IntEngLeave(&EnterLeave);
}

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
    GRADIENT_OUT go;
    INTENG_ENTER_LEAVE EnterLeave;
    RECT_ENUM RectEnum;
    TRIVERTEX v[3];
    RECTL rcBounds, rcFill;
    BOOL EnumMore;
    ULONG i, a, b, c;
    LONG x, y, x1, x2, left, right;
    LONGLONG det, l1, l2;

    a = gTriangle->Vertex1;
    b = gTriangle->Vertex2;
    c = gTriangle->Vertex3;
    if (a >= nVertex || b >= nVertex || c >= nVertex)
        return FALSE;

    if (pVertex[a].y > pVertex[b].y)
    {
        if (pVertex[c].y < pVertex[b].y)
            { v[0] = pVertex[c]; v[1] = pVertex[b]; v[2] = pVertex[a]; }
        else if (pVertex[c].y < pVertex[a].y)
            { v[0] = pVertex[b]; v[1] = pVertex[c]; v[2] = pVertex[a]; }
        else
            { v[0] = pVertex[b]; v[1] = pVertex[a]; v[2] = pVertex[c]; }
    }
    else
    {
        if (pVertex[c].y < pVertex[a].y)
            { v[0] = pVertex[c]; v[1] = pVertex[a]; v[2] = pVertex[b]; }
        else if (pVertex[c].y < pVertex[b].y)
            { v[0] = pVertex[a]; v[1] = pVertex[c]; v[2] = pVertex[b]; }
        else
            { v[0] = pVertex[a]; v[1] = pVertex[b]; v[2] = pVertex[c]; }
    }

    det = (LONGLONG)(v[2].y - v[1].y) * (v[2].x - v[0].x) -
          (LONGLONG)(v[2].x - v[1].x) * (v[2].y - v[0].y);
    if (!det)
        return FALSE;

    rcBounds.left = min(v[0].x, min(v[1].x, v[2].x));
    rcBounds.right = max(v[0].x, max(v[1].x, v[2].x));
    rcBounds.top = v[0].y;
    rcBounds.bottom = v[2].y;
    if (rcBounds.left >= rcBounds.right || rcBounds.top >= rcBounds.bottom)
        return TRUE;

    if (!GradientBegin(&go, &EnterLeave, psoDest, pxlo, &rcBounds))
        return FALSE;

    CLIPOBJ_cEnumStart(pco, FALSE, CT_RECTANGLES, CD_RIGHTDOWN, 0);
    do
    {
        EnumMore = CLIPOBJ_bEnum(pco, (ULONG)sizeof(RectEnum), (PVOID)&RectEnum);
        for (i = 0; i < RectEnum.c; i++)
        {
            if (!RECTL_bIntersectRect(&rcFill, &RectEnum.arcl[i], &rcBounds))
                continue;

            for (y = rcFill.top; y < rcFill.bottom; y++)
            {
                if (y < v[1].y)
                    x1 = GradientEdgeCoord(y, v[0].x, v[0].y, v[1].x, v[1].y);
                else
                    x1 = GradientEdgeCoord(y, v[1].x, v[1].y, v[2].x, v[2].y);
                x2 = GradientEdgeCoord(y, v[0].x, v[0].y, v[2].x, v[2].y);

                left = max(rcFill.left, min(x1, x2));
                right = min(rcFill.right, max(x1, x2));

                for (x = left; x < right; x++)
                {
                    l1 = (LONGLONG)(v[1].y - v[2].y) * (x - v[2].x) - (LONGLONG)(v[1].x - v[2].x) * (y - v[2].y);
                    l2 = (LONGLONG)(v[2].y - v[0].y) * (x - v[2].x) - (LONGLONG)(v[2].x - v[0].x) * (y - v[2].y);
                    GradientPutPixel(&go, x, y,
                        (ULONG)((v[0].Red   * l1 + v[1].Red   * l2 + v[2].Red   * (det - l1 - l2)) / det),
                        (ULONG)((v[0].Green * l1 + v[1].Green * l2 + v[2].Green * (det - l1 - l2)) / det),
                        (ULONG)((v[0].Blue  * l1 + v[1].Blue  * l2 + v[2].Blue  * (det - l1 - l2)) / det),
                        (ULONG)((v[0].Alpha * l1 + v[1].Alpha * l2 + v[2].Alpha * (det - l1 - l2)) / det));
                }
            }
        }
    }
    while (EnumMore);

    return IntEngLeave(&EnterLeave);
}

static
BOOL
IntEngIsNULLTriangle(TRIVERTEX  *pVertex, GRADIENT_TRIANGLE *gt)
{
    if(COMPAREVERTEX(VERTEX(Vertex1), VERTEX(Vertex2)))
        return TRUE;
    if(COMPAREVERTEX(VERTEX(Vertex1), VERTEX(Vertex3)))
        return TRUE;
    if(COMPAREVERTEX(VERTEX(Vertex2), VERTEX(Vertex3)))
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

    if ((psoDest->iBitmapFormat == BMF_1BPP) && psurf->ppal &&
        !(psurf->ppal->flFlags & PAL_DIBSECTION))
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
