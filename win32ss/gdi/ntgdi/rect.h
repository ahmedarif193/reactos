#pragma once

FORCEINLINE
VOID
RECTL_vSetRect(
    _Out_ RECTL *prcl,
    _In_ LONG left,
    _In_ LONG top,
    _In_ LONG right,
    _In_ LONG bottom)
{
    prcl->left = left;
    prcl->top = top;
    prcl->right = right;
    prcl->bottom = bottom;
}

FORCEINLINE
VOID
RECTL_vSetEmptyRect(
    _Out_ RECTL *prcl)
{
    prcl->left = 0;
    prcl->top = 0;
    prcl->right = 0;
    prcl->bottom = 0;
}

FORCEINLINE
VOID
RECTL_vOffsetRect(
    _Inout_ RECTL *prcl,
    _In_ INT cx,
    _In_ INT cy)
{
    prcl->left += cx;
    prcl->right += cx;
    prcl->top += cy;
    prcl->bottom += cy;
}

FORCEINLINE
BOOL
RECTL_bIsEmptyRect(
    _In_ const RECTL *prcl)
{
    return (prcl->left >= prcl->right || prcl->top >= prcl->bottom);
}

FORCEINLINE
BOOL
RECTL_bPointInRect(
    _In_ const RECTL *prcl,
    _In_ INT x,
    _In_ INT y)
{
    return (x >= prcl->left && x < prcl->right &&
            y >= prcl->top  && y < prcl->bottom);
}

FORCEINLINE
BOOL
RECTL_bIsWellOrdered(
    _In_ const RECTL *prcl)
{
    return ((prcl->left <= prcl->right) &&
            (prcl->top  <= prcl->bottom));
}

FORCEINLINE
BOOL
RECTL_bClipRectBySize(
    _Out_ RECTL *prclDst,
    _In_ const RECTL *prclSrc,
    _In_ const SIZEL *pszl)
{
    prclDst->left = max(prclSrc->left, 0);
    prclDst->top = max(prclSrc->top, 0);
    prclDst->right = min(prclSrc->right, pszl->cx);
    prclDst->bottom = min(prclSrc->bottom, pszl->cy);
    return !RECTL_bIsEmptyRect(prclDst);
}

FORCEINLINE
LONG
RECTL_lGetHeight(_In_ const RECTL* prcl)
{
    return prcl->bottom - prcl->top;
}

FORCEINLINE
LONG
RECTL_lGetWidth(_In_ const RECTL* prcl)
{
    return prcl->right - prcl->left;
}

BOOL
FASTCALL
RECTL_bUnionRect(
    _Out_ RECTL *prclDst,
    _In_ const RECTL *prcl1,
    _In_ const RECTL *prcl2);

BOOL
FASTCALL
RECTL_bIntersectRect(
    _Out_ RECTL* prclDst,
    _In_ const RECTL* prcl1,
    _In_ const RECTL* prcl2);

VOID
FASTCALL
RECTL_vMakeWellOrdered(
    _Inout_ RECTL *prcl);

/*
 * Make a device-space rectangle well-ordered after a coordinate transform.
 *
 * Transforming a half-open logical rect [L, R) through a mirroring (RTL)
 * world-to-device transform yields a reversed rect where left > right.
 * Simply swapping the edges (as RECTL_vMakeWellOrdered does) is off by one,
 * because the right edge must remain exclusive after mirroring.  Windows
 * (and Wine's get_bounding_rect) handle this by adding 1 to both edges on
 * reversal, so logical [L, R) maps to device [width - R, width - L).
 */
FORCEINLINE
VOID
RECTL_vMakeWellOrderedXform(
    _Inout_ RECTL *prcl)
{
    LONG lTmp;
    if (prcl->left > prcl->right)
    {
        lTmp = prcl->left;
        prcl->left = prcl->right + 1;
        prcl->right = lTmp + 1;
    }
    if (prcl->top > prcl->bottom)
    {
        lTmp = prcl->top;
        prcl->top = prcl->bottom + 1;
        prcl->bottom = lTmp + 1;
    }
}

VOID
FASTCALL
RECTL_vInflateRect(
    _Inout_ RECTL *rect,
    _In_ INT dx,
    _In_ INT dy);
