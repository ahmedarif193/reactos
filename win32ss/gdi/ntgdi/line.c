/*
 * PROJECT:         ReactOS Win32k Subsystem
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            win32ss/gdi/ntgdi/line.c
 * PURPOSE:         Line functions
 * PROGRAMMERS:     ...
 */

#include <win32k.h>

#define NDEBUG
#include <debug.h>

DBG_DEFAULT_CHANNEL(GdiLine);

// Some code from the WINE project source (www.winehq.com)

VOID FASTCALL
AddPenLinesBounds(PDC dc, int count, POINT *points)
{
    DWORD join, endcap;
    RECTL bounds, rect;
    LONG lWidth;
    PBRUSH pbrLine;

    /* Get BRUSH from current pen. */
    pbrLine = dc->dclevel.pbrLine;
    ASSERT(pbrLine);

    lWidth = 0;

    // Setup bounds
    bounds.left = bounds.top = INT_MAX;
    bounds.right = bounds.bottom = INT_MIN;

    if (((pbrLine->ulPenStyle & PS_TYPE_MASK) & PS_GEOMETRIC) || pbrLine->lWidth > 1)
    {
        /* Windows uses some heuristics to estimate the distance from the point that will be painted */
        lWidth = pbrLine->lWidth + 2;
        endcap = (PS_ENDCAP_MASK & pbrLine->ulPenStyle);
        join   = (PS_JOIN_MASK   & pbrLine->ulPenStyle);
        if (join == PS_JOIN_MITER)
        {
           lWidth *= 5;
           if (endcap == PS_ENDCAP_SQUARE) lWidth = (lWidth * 3 + 1) / 2;
        }
        else
        {
           if (endcap == PS_ENDCAP_SQUARE) lWidth -= lWidth / 4;
           else lWidth = (lWidth + 1) / 2;
        }
    }

    while (count-- > 0)
    {
        rect.left   = points->x - lWidth;
        rect.top    = points->y - lWidth;
        rect.right  = points->x + lWidth + 1;
        rect.bottom = points->y + lWidth + 1;
        RECTL_bUnionRect(&bounds, &bounds, &rect);
        points++;
    }

    DPRINT("APLB dc %p l %d t %d\n",dc,rect.left,rect.top);
    DPRINT("                 r %d b %d\n",rect.right,rect.bottom);

#ifdef __REACTOS__
    IntUpdateBoundsRect(dc, &bounds);
#else
    {
       RECTL rcRgn = dc->erclClip; // Use the clip box for now.

       if (RECTL_bIntersectRect(&rcRgn, &rcRgn, &bounds))
           IntUpdateBoundsRect(dc, &rcRgn);
       else
           IntUpdateBoundsRect(dc, &bounds);
    }
#endif
}

// Should use Fx in Point
//
BOOL FASTCALL
IntGdiMoveToEx(DC      *dc,
               int     X,
               int     Y,
               LPPOINT Point)
{
    PDC_ATTR pdcattr = dc->pdcattr;
    if ( Point )
    {
        if ( pdcattr->ulDirty_ & DIRTY_PTLCURRENT ) // Double hit!
        {
            Point->x = pdcattr->ptfxCurrent.x; // ret prev before change.
            Point->y = pdcattr->ptfxCurrent.y;
            IntDPtoLP ( dc, Point, 1);         // Reconvert back.
        }
        else
        {
            Point->x = pdcattr->ptlCurrent.x;
            Point->y = pdcattr->ptlCurrent.y;
        }
    }
    pdcattr->ptlCurrent.x = X;
    pdcattr->ptlCurrent.y = Y;
    pdcattr->ptfxCurrent = pdcattr->ptlCurrent;
    CoordLPtoDP(dc, &pdcattr->ptfxCurrent); // Update fx
    pdcattr->ulDirty_ &= ~(DIRTY_PTLCURRENT|DIRTY_PTFXCURRENT|DIRTY_STYLESTATE);

    return TRUE;
}

BOOL FASTCALL
GreMoveTo( HDC hdc,
           INT x,
           INT y,
           LPPOINT pptOut)
{
   BOOL Ret;
   PDC dc;
   if (!(dc = DC_LockDc(hdc)))
   {
      EngSetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
   }
   Ret = IntGdiMoveToEx(dc, x, y, pptOut);
   DC_UnlockDc(dc);
   return Ret;
}

// Should use Fx in pt
//
VOID FASTCALL
IntGetCurrentPositionEx(PDC dc, LPPOINT pt)
{
    PDC_ATTR pdcattr = dc->pdcattr;

    if ( pt )
    {
        if (pdcattr->ulDirty_ & DIRTY_PTFXCURRENT)
        {
            pdcattr->ptfxCurrent = pdcattr->ptlCurrent;
            CoordLPtoDP(dc, &pdcattr->ptfxCurrent); // Update fx
            pdcattr->ulDirty_ &= ~(DIRTY_PTFXCURRENT|DIRTY_STYLESTATE);
        }
        pt->x = pdcattr->ptlCurrent.x;
        pt->y = pdcattr->ptlCurrent.y;
    }
}

typedef struct _WIDEPEN_FACE
{
    POINT start, end;
    INT dx, dy;
} WIDEPEN_FACE, *PWIDEPEN_FACE;

typedef struct _WIDEPEN
{
    PREGION prgnTotal;
    PREGION prgnRound;
    INT iWidth;
    ULONG iEndCap;
    ULONG iJoin;
    FLOAT eMiterLimit;
    ULONG aulDash[16];
    ULONG cDash;
    ULONG ulDashTotal;
    ULONG iDash;
    ULONG ulDashLeft;
    BOOL bDashMark;
} WIDEPEN, *PWIDEPEN;

static LONG
WidePenRound(double d)
{
    return (LONG)((d > 0) ? (d + 0.5) : (d - 0.5));
}

static VOID
WidePenAddRect(PWIDEPEN pwp, const RECTL *prcl)
{
    if (prcl->left < prcl->right && prcl->top < prcl->bottom)
        REGION_UnionRectWithRgn(pwp->prgnTotal, prcl);
}

static VOID
WidePenAddPolygon(PWIDEPEN pwp, const POINT *ppt, ULONG cpt)
{
    PREGION prgn = IntSysCreateRectpRgn(0, 0, 0, 0);

    if (!prgn)
        return;
    if (REGION_SetPolyPolygonRgn(prgn, ppt, &cpt, 1, ALTERNATE))
        IntGdiCombineRgn(pwp->prgnTotal, pwp->prgnTotal, prgn, RGN_OR);
    REGION_Delete(prgn);
}

static VOID
WidePenAddRound(PWIDEPEN pwp, const POINT *ppt)
{
    if (!pwp->prgnRound)
        return;
    REGION_bOffsetRgn(pwp->prgnRound, ppt->x, ppt->y);
    IntGdiCombineRgn(pwp->prgnTotal, pwp->prgnTotal, pwp->prgnRound, RGN_OR);
    REGION_bOffsetRgn(pwp->prgnRound, -ppt->x, -ppt->y);
}

static VOID
WidePenAddCap(PWIDEPEN pwp, const POINT *ppt)
{
    if (pwp->iEndCap == PS_ENDCAP_ROUND)
        WidePenAddRound(pwp, ppt);
}

static BOOL
WidePenAddMiter(PWIDEPEN pwp, const POINT *ppt, const WIDEPEN_FACE *pf1, const WIDEPEN_FACE *pf2)
{
    INT det = pf1->dx * pf2->dy - pf1->dy * pf2->dx;
    POINT pt1, pt2, apt[5];
    double a, b, x, y;

    if (det == 0)
        return FALSE;

    if (det < 0)
    {
        const WIDEPEN_FACE *pfTmp = pf1;
        pf1 = pf2;
        pf2 = pfTmp;
        det = -det;
    }

    pt1 = pf1->start;
    pt2 = pf2->end;

    a = (double)(pt2.x * pf2->dy - pt2.y * pf2->dx) / det;
    b = (double)(pt1.x * pf1->dy - pt1.y * pf1->dx) / det;

    x = a * pf1->dx - b * pf2->dx;
    y = a * pf1->dy - b * pf2->dy;

    if (((x - ppt->x) * (x - ppt->x) + (y - ppt->y) * (y - ppt->y)) * 4 >
        (double)pwp->eMiterLimit * pwp->eMiterLimit * pwp->iWidth * pwp->iWidth)
    {
        return FALSE;
    }

    apt[0] = pf2->start;
    apt[1] = pf1->start;
    apt[2].x = WidePenRound(x);
    apt[2].y = WidePenRound(y);
    apt[3] = pf2->end;
    apt[4] = pf1->end;
    WidePenAddPolygon(pwp, apt, 5);
    return TRUE;
}

static VOID
WidePenAddJoin(PWIDEPEN pwp, const POINT *ppt, const WIDEPEN_FACE *pf1, const WIDEPEN_FACE *pf2)
{
    POINT apt[4];

    if (pwp->iJoin == PS_JOIN_ROUND)
    {
        WidePenAddRound(pwp, ppt);
        return;
    }

    if (pwp->iJoin == PS_JOIN_MITER && WidePenAddMiter(pwp, ppt, pf1, pf2))
        return;

    apt[0] = pf1->start;
    apt[1] = pf2->end;
    apt[2] = pf1->end;
    apt[3] = pf2->start;
    WidePenAddPolygon(pwp, apt, 4);
}

static BOOL
WidePenSegment(PWIDEPEN pwp, const POINT *ppt1, const POINT *ppt2, INT dx, INT dy,
               BOOL bCap1, BOOL bCap2, PWIDEPEN_FACE pf1, PWIDEPEN_FACE pf2)
{
    RECTL rcl;
    INT iWidth = pwp->iWidth;
    BOOL bSquare1 = bCap1 && (pwp->iEndCap == PS_ENDCAP_SQUARE);
    BOOL bSquare2 = bCap2 && (pwp->iEndCap == PS_ENDCAP_SQUARE);

    if (dx == 0 && dy == 0)
        return FALSE;

    if (dy == 0)
    {
        rcl.left = min(ppt1->x, ppt2->x);
        rcl.right = max(ppt1->x, ppt2->x);
        rcl.top = ppt1->y - iWidth / 2;
        rcl.bottom = rcl.top + iWidth;
        if ((bSquare1 && dx > 0) || (bSquare2 && dx < 0)) rcl.left -= iWidth / 2;
        if ((bSquare2 && dx > 0) || (bSquare1 && dx < 0)) rcl.right += iWidth / 2;
        WidePenAddRect(pwp, &rcl);
        if (dx > 0)
        {
            pf1->start.x = pf1->end.x = rcl.left;
            pf1->start.y = pf2->end.y = rcl.bottom;
            pf1->end.y = pf2->start.y = rcl.top;
            pf2->start.x = pf2->end.x = rcl.right - 1;
        }
        else
        {
            pf1->start.x = pf1->end.x = rcl.right;
            pf1->start.y = pf2->end.y = rcl.top;
            pf1->end.y = pf2->start.y = rcl.bottom;
            pf2->start.x = pf2->end.x = rcl.left + 1;
        }
    }
    else if (dx == 0)
    {
        rcl.top = min(ppt1->y, ppt2->y);
        rcl.bottom = max(ppt1->y, ppt2->y);
        rcl.left = ppt1->x - iWidth / 2;
        rcl.right = rcl.left + iWidth;
        if ((bSquare1 && dy > 0) || (bSquare2 && dy < 0)) rcl.top -= iWidth / 2;
        if ((bSquare2 && dy > 0) || (bSquare1 && dy < 0)) rcl.bottom += iWidth / 2;
        WidePenAddRect(pwp, &rcl);
        if (dy > 0)
        {
            pf1->start.x = pf2->end.x = rcl.left;
            pf1->start.y = pf1->end.y = rcl.top;
            pf1->end.x = pf2->start.x = rcl.right;
            pf2->start.y = pf2->end.y = rcl.bottom - 1;
        }
        else
        {
            pf1->start.x = pf2->end.x = rcl.right;
            pf1->start.y = pf1->end.y = rcl.bottom;
            pf1->end.x = pf2->start.x = rcl.left;
            pf2->start.y = pf2->end.y = rcl.top + 1;
        }
    }
    else
    {
        double len = sqrt((double)dx * dx + (double)dy * dy);
        double width_x = iWidth * abs(dy) / len;
        double width_y = iWidth * abs(dx) / len;
        POINT apt[4], ptWide, ptNarrow;

        ptNarrow.x = WidePenRound(width_x / 2);
        ptNarrow.y = WidePenRound(width_y / 2);
        ptWide.x = WidePenRound((width_x + 1) / 2);
        ptWide.y = WidePenRound((width_y + 1) / 2);

        if (dx < 0)
        {
            ptWide.y = -ptWide.y;
            ptNarrow.y = -ptNarrow.y;
        }

        if (dy < 0)
        {
            POINT ptTmp = ptNarrow;
            ptNarrow = ptWide;
            ptWide = ptTmp;
            ptWide.x = -ptWide.x;
            ptNarrow.x = -ptNarrow.x;
        }

        apt[0].x = ppt1->x - ptNarrow.x;
        apt[0].y = ppt1->y + ptNarrow.y;
        apt[1].x = ppt1->x + ptWide.x;
        apt[1].y = ppt1->y - ptWide.y;
        apt[2].x = ppt2->x + ptWide.x;
        apt[2].y = ppt2->y - ptWide.y;
        apt[3].x = ppt2->x - ptNarrow.x;
        apt[3].y = ppt2->y + ptNarrow.y;

        if (bSquare1)
        {
            apt[0].x -= ptNarrow.y;
            apt[1].x -= ptNarrow.y;
            apt[0].y -= ptNarrow.x;
            apt[1].y -= ptNarrow.x;
        }

        if (bSquare2)
        {
            apt[2].x += ptWide.y;
            apt[3].x += ptWide.y;
            apt[2].y += ptWide.x;
            apt[3].y += ptWide.x;
        }

        WidePenAddPolygon(pwp, apt, 4);

        pf1->start = apt[0];
        pf1->end = apt[1];
        pf2->start = apt[2];
        pf2->end = apt[3];
    }

    pf1->dx = pf2->dx = dx;
    pf1->dy = pf2->dy = dy;
    return TRUE;
}

static VOID
WidePenSegments(PWIDEPEN pwp, INT cpt, const POINT *ppt, BOOL bClose, INT iStart, INT cSeg,
                const POINT *pptFirst, const POINT *pptLast)
{
    WIDEPEN_FACE f1, f2, fPrev, fFirst;
    const POINT *ppt1, *ppt2;
    INT i;

    if (!bClose)
    {
        WidePenAddCap(pwp, pptFirst);
        WidePenAddCap(pwp, pptLast);
    }

    if (cSeg == 1)
    {
        ppt1 = &ppt[iStart];
        ppt2 = &ppt[(iStart + 1) % cpt];
        WidePenSegment(pwp, pptFirst, pptLast, ppt2->x - ppt1->x, ppt2->y - ppt1->y,
                       TRUE, TRUE, &f1, &f2);
        return;
    }

    ppt1 = &ppt[iStart];
    ppt2 = &ppt[(iStart + 1) % cpt];
    WidePenSegment(pwp, pptFirst, ppt2, ppt2->x - ppt1->x, ppt2->y - ppt1->y,
                   !bClose, FALSE, &fFirst, &fPrev);

    for (i = 1; i < cSeg - 1; i++)
    {
        ppt1 = &ppt[(iStart + i) % cpt];
        ppt2 = &ppt[(iStart + i + 1) % cpt];
        if (WidePenSegment(pwp, ppt1, ppt2, ppt2->x - ppt1->x, ppt2->y - ppt1->y,
                           FALSE, FALSE, &f1, &f2))
        {
            WidePenAddJoin(pwp, ppt1, &fPrev, &f1);
            fPrev = f2;
        }
    }

    ppt1 = &ppt[(iStart + cSeg - 1) % cpt];
    ppt2 = &ppt[(iStart + cSeg) % cpt];
    WidePenSegment(pwp, ppt1, pptLast, ppt2->x - ppt1->x, ppt2->y - ppt1->y,
                   FALSE, !bClose, &f1, &f2);
    WidePenAddJoin(pwp, ppt1, &fPrev, &f1);
    if (bClose)
        WidePenAddJoin(pwp, pptLast, &f2, &fFirst);
}

static VOID
WidePenSkipDash(PWIDEPEN pwp, ULONG ulSkip)
{
    ulSkip %= pwp->ulDashTotal;
    do
    {
        if (pwp->ulDashLeft > ulSkip)
        {
            pwp->ulDashLeft -= ulSkip;
            return;
        }
        ulSkip -= pwp->ulDashLeft;
        if (++pwp->iDash == pwp->cDash)
            pwp->iDash = 0;
        pwp->ulDashLeft = pwp->aulDash[pwp->iDash];
        pwp->bDashMark = !pwp->bDashMark;
    }
    while (ulSkip);
}

static VOID
WidePenDashedLines(PWIDEPEN pwp, INT cpt, const POINT *ppt, BOOL bClose)
{
    INT i, iStart = 0, iInitial = 0;
    LONG lCur = 0;
    POINT ptInitial = { 0, 0 }, ptStart = ppt[0], ptEnd;

    for (i = 0; i < (bClose ? cpt : cpt - 1); i++)
    {
        const POINT *ppt1 = &ppt[i];
        const POINT *ppt2 = &ppt[(bClose && i == cpt - 1) ? 0 : i + 1];
        INT dx = ppt2->x - ppt1->x;
        INT dy = ppt2->y - ppt1->y;

        if (dx == 0 && dy == 0)
            continue;

        if (dy == 0)
        {
            if (abs(dx) - lCur < (LONG)pwp->ulDashLeft)
            {
                WidePenSkipDash(pwp, abs(dx) - lCur);
                lCur = 0;
                continue;
            }
            lCur += pwp->ulDashLeft;
            dx = (dx > 0) ? lCur : -lCur;
        }
        else if (dx == 0)
        {
            if (abs(dy) - lCur < (LONG)pwp->ulDashLeft)
            {
                WidePenSkipDash(pwp, abs(dy) - lCur);
                lCur = 0;
                continue;
            }
            lCur += pwp->ulDashLeft;
            dy = (dy > 0) ? lCur : -lCur;
        }
        else
        {
            double len = sqrt((double)dx * dx + (double)dy * dy);

            if (len - lCur < pwp->ulDashLeft)
            {
                WidePenSkipDash(pwp, (ULONG)(len - lCur));
                lCur = 0;
                continue;
            }
            lCur += pwp->ulDashLeft;
            dx = (INT)(dx * lCur / len);
            dy = (INT)(dy * lCur / len);
        }
        ptEnd.x = ppt1->x + dx;
        ptEnd.y = ppt1->y + dy;

        if (pwp->bDashMark)
        {
            if (!iInitial && bClose)
            {
                iInitial = i - iStart + 1;
                ptInitial = ptEnd;
            }
            else
            {
                WidePenSegments(pwp, cpt, ppt, FALSE, iStart, i - iStart + 1, &ptStart, &ptEnd);
            }
        }
        if (!iInitial)
            iInitial = -1;

        WidePenSkipDash(pwp, pwp->ulDashLeft);
        ptStart = ptEnd;
        iStart = i;
        i--;
    }

    if (pwp->bDashMark)
    {
        INT cSeg;

        if (iInitial > 0)
        {
            cSeg = cpt - iStart + iInitial;
            ptEnd = ptInitial;
        }
        else if (bClose)
        {
            cSeg = cpt - iStart;
            ptEnd = ppt[0];
        }
        else
        {
            cSeg = cpt - iStart - 1;
            ptEnd = ppt[cpt - 1];
        }
        WidePenSegments(pwp, cpt, ppt, FALSE, iStart, cSeg, &ptStart, &ptEnd);
    }
    else if (iInitial > 0)
    {
        WidePenSegments(pwp, cpt, ppt, FALSE, 0, iInitial, &ppt[0], &ptInitial);
    }
}

static BOOL
WidePenSetDashes(PWIDEPEN pwp, PBRUSH pbrLine)
{
    static const ULONG aulGeometric[4][6] =
    {
        { 3, 1 },
        { 1, 1 },
        { 3, 1, 1, 1 },
        { 3, 1, 1, 1, 1, 1 }
    };
    static const ULONG acGeometric[4] = { 2, 2, 4, 6 };
    ULONG iStyle = pbrLine->ulPenStyle & PS_STYLE_MASK;
    BOOL bGeometric = (pbrLine->ulPenStyle & PS_TYPE_MASK) == PS_GEOMETRIC &&
                      !(pbrLine->flAttrs & BR_IS_OLDSTYLEPEN);
    ULONG i, ulScale;

    pwp->cDash = 0;
    if (!bGeometric || iStyle == PS_SOLID || iStyle == PS_INSIDEFRAME)
        return TRUE;

    if (iStyle >= PS_DASH && iStyle <= PS_DASHDOTDOT)
    {
        pwp->cDash = acGeometric[iStyle - PS_DASH];
        RtlCopyMemory(pwp->aulDash, aulGeometric[iStyle - PS_DASH], pwp->cDash * sizeof(ULONG));
        ulScale = pwp->iWidth;
    }
    else if (iStyle == PS_USERSTYLE && pbrLine->pStyle &&
             pbrLine->dwStyleCount && pbrLine->dwStyleCount <= RTL_NUMBER_OF(pwp->aulDash))
    {
        pwp->cDash = pbrLine->dwStyleCount;
        RtlCopyMemory(pwp->aulDash, pbrLine->pStyle, pwp->cDash * sizeof(ULONG));
        ulScale = 1;
    }
    else
    {
        return FALSE;
    }

    pwp->ulDashTotal = 0;
    for (i = 0; i < pwp->cDash; i++)
        pwp->ulDashTotal += pwp->aulDash[i];
    if (pwp->cDash % 2)
        pwp->ulDashTotal *= 2;
    if (!pwp->ulDashTotal)
        return FALSE;

    if (ulScale > 1)
    {
        for (i = 0; i < pwp->cDash; i++)
            pwp->aulDash[i] *= ulScale;
        pwp->ulDashTotal *= ulScale;
        if (pwp->iEndCap != PS_ENDCAP_FLAT)
        {
            for (i = 0; i + 1 < pwp->cDash; i += 2)
            {
                pwp->aulDash[i] -= ulScale;
                pwp->aulDash[i + 1] += ulScale;
            }
        }
    }

    pwp->iDash = 0;
    pwp->ulDashLeft = pwp->aulDash[0];
    pwp->bDashMark = TRUE;
    return TRUE;
}

static BOOL
WidePenPaint(PDC pdc, PREGION prgnPen)
{
    PREGION prgnClip;
    XCLIPOBJ xcoClip;
    DWORD rop2Fg;
    MIX mix;
    BOOL bRet;

    prgnClip = IntSysCreateRectpRgn(0, 0, 0, 0);
    if (!prgnClip)
        return FALSE;

    IntGdiCombineRgn(prgnClip, pdc->prgnRao ? pdc->prgnRao : pdc->prgnVis, NULL, RGN_COPY);
    REGION_bOffsetRgn(prgnClip, pdc->ptlDCOrig.x, pdc->ptlDCOrig.y);
    IntGdiCombineRgn(prgnClip, prgnClip, prgnPen, RGN_AND);

    IntEngInitClipObj(&xcoClip);
    IntEngUpdateClipRegion(&xcoClip,
                           prgnClip->rdh.nCount,
                           prgnClip->Buffer,
                           &prgnClip->rdh.rcBound);

    rop2Fg = FIXUP_ROP2(pdc->pdcattr->jROP2);
    mix = rop2Fg | (pdc->pdcattr->jBkMode == OPAQUE ? rop2Fg : R2_NOP) << 8;

    bRet = IntEngPaint(&pdc->dclevel.pSurface->SurfObj,
                       (CLIPOBJ *)&xcoClip,
                       &pdc->eboLine.BrushObject,
                       &pdc->ptlFillOrigin,
                       mix);

    REGION_Delete(prgnClip);
    IntEngFreeClipResources(&xcoClip);
    return bRet;
}

BOOL FASTCALL
IntWidePenLines(PDC pdc, PPOINT ppt, INT cpt, BOOL bClose)
{
    PBRUSH pbrLine = pdc->dclevel.pbrLine;
    WIDEPEN wp;
    HRGN hrgnRound = NULL;
    BOOL bRet;

    if ((DC_pmxWorldToDevice(pdc)->flAccel & (XFORM_SCALE | XFORM_UNITY)) != (XFORM_SCALE | XFORM_UNITY))
        return FALSE;

    if (!pdc->dclevel.pSurface || cpt < 2)
        return TRUE;

    while (cpt > 2 && ppt[0].x == ppt[1].x && ppt[0].y == ppt[1].y)
    {
        ppt++;
        cpt--;
    }
    while (cpt > 2 && ppt[cpt - 1].x == ppt[cpt - 2].x && ppt[cpt - 1].y == ppt[cpt - 2].y)
        cpt--;

    wp.iWidth = pbrLine->lWidth;
    wp.iEndCap = pbrLine->ulPenStyle & PS_ENDCAP_MASK;
    wp.iJoin = pbrLine->ulPenStyle & PS_JOIN_MASK;
    wp.eMiterLimit = pdc->dclevel.laPath.eMiterLimit;
    if (wp.iWidth <= 1)
        wp.iEndCap = PS_ENDCAP_FLAT;
    if (!WidePenSetDashes(&wp, pbrLine))
        return FALSE;
    wp.prgnRound = NULL;
    wp.prgnTotal = IntSysCreateRectpRgn(0, 0, 0, 0);
    if (!wp.prgnTotal)
        return FALSE;

    if (wp.iJoin == PS_JOIN_ROUND || wp.iEndCap == PS_ENDCAP_ROUND)
    {
        hrgnRound = NtGdiCreateEllipticRgn(-(wp.iWidth / 2), -(wp.iWidth / 2),
                                           (wp.iWidth + 1) / 2 + 1, (wp.iWidth + 1) / 2 + 1);
        if (hrgnRound)
            wp.prgnRound = REGION_LockRgn(hrgnRound);
    }

    if (pdc->pdcattr->ulDirty_ & (DIRTY_LINE | DC_PEN_DIRTY))
        DC_vUpdateLineBrush(pdc);

    if (wp.cDash)
        WidePenDashedLines(&wp, cpt, ppt, bClose);
    else if (bClose)
        WidePenSegments(&wp, cpt, ppt, TRUE, 0, cpt, &ppt[0], &ppt[0]);
    else
        WidePenSegments(&wp, cpt, ppt, FALSE, 0, cpt - 1, &ppt[0], &ppt[cpt - 1]);

    bRet = WidePenPaint(pdc, wp.prgnTotal);

    if (wp.prgnRound)
        REGION_UnlockRgn(wp.prgnRound);
    if (hrgnRound)
        GreDeleteObject(hrgnRound);
    REGION_Delete(wp.prgnTotal);
    return bRet;
}

MIX FASTCALL
IntGdiLineMix(DC *dc)
{
    PDC_ATTR pdcattr = dc->pdcattr;
    ULONG Rop2Back = R2_NOP;

    if (pdcattr->jBkMode == OPAQUE &&
        (dc->dclevel.pbrLine->flAttrs & BR_IS_OLDSTYLEPEN))
    {
        Rop2Back = pdcattr->jROP2;
    }

    return pdcattr->jROP2 | (Rop2Back << 8);
}

BOOL FASTCALL
IntGdiLineTo(DC  *dc,
             int XEnd,
             int YEnd)
{
    SURFACE *psurf;
    BOOL      Ret = TRUE;
    PBRUSH pbrLine;
    RECTL     Bounds;
    POINT     Points[2];
    PDC_ATTR  pdcattr;
    PPATH     pPath;

    ASSERT_DC_PREPARED(dc);

    pdcattr = dc->pdcattr;

    if (PATH_IsPathOpen(dc->dclevel))
    {
        Ret = PATH_LineTo(dc, XEnd, YEnd);
    }
    else
    {
        psurf = dc->dclevel.pSurface;

        Points[0].x = pdcattr->ptlCurrent.x;
        Points[0].y = pdcattr->ptlCurrent.y;
        Points[1].x = XEnd;
        Points[1].y = YEnd;

        IntLPtoDP(dc, Points, 2);

        /* The DCOrg is in device coordinates */
        Points[0].x += dc->ptlDCOrig.x;
        Points[0].y += dc->ptlDCOrig.y;
        Points[1].x += dc->ptlDCOrig.x;
        Points[1].y += dc->ptlDCOrig.y;

        Bounds.left = min(Points[0].x, Points[1].x);
        Bounds.top = min(Points[0].y, Points[1].y);
        Bounds.right = max(Points[0].x, Points[1].x);
        Bounds.bottom = max(Points[0].y, Points[1].y);

        /* Get BRUSH from current pen. */
        pbrLine = dc->dclevel.pbrLine;
        ASSERT(pbrLine);

        if (dc->fs & (DC_ACCUM_APP|DC_ACCUM_WMGR))
        {
           DPRINT("Bounds dc %p l %d t %d\n",dc,Bounds.left,Bounds.top);
           DPRINT("                   r %d b %d\n",Bounds.right,Bounds.bottom);
           AddPenLinesBounds(dc, 2, Points);
        }

        if (psurf && !(pbrLine->flAttrs & BR_IS_NULL))
        {
            if ((IntIsEffectiveWidePen(pbrLine) || !(pbrLine->flAttrs & BR_IS_SOLID)) &&
                IntWidePenLines(dc, Points, 2, FALSE))
            {
                Ret = TRUE;
            }
            else if (IntIsEffectiveWidePen(pbrLine))
            {
                /* Clear the path */
                PATH_Delete(dc->dclevel.hPath);
                dc->dclevel.hPath = NULL;

                /* Begin a path */
                pPath = PATH_CreatePath(2);
                dc->dclevel.flPath |= DCPATH_ACTIVE;
                dc->dclevel.hPath = pPath->BaseObject.hHmgr;
                IntGetCurrentPositionEx(dc, &pPath->pos);
                IntLPtoDP(dc, &pPath->pos, 1);

                PATH_MoveTo(dc, pPath);
                PATH_LineTo(dc, XEnd, YEnd);

                /* Close the path */
                pPath->state = PATH_Closed;
                dc->dclevel.flPath &= ~DCPATH_ACTIVE;

                /* Actually stroke a path */
                Ret = PATH_StrokePath(dc, pPath);

                /* Clear the path */
                PATH_UnlockPath(pPath);
                PATH_Delete(dc->dclevel.hPath);
                dc->dclevel.hPath = NULL;
            }
            else
            {
                Ret = IntEngLineToEx(&psurf->SurfObj,
                                     (CLIPOBJ *)&dc->co,
                                     &dc->eboLine.BrushObject,
                                     Points[0].x, Points[0].y,
                                     Points[1].x, Points[1].y,
                                     &Bounds,
                                     IntGdiLineMix(dc),
                                     TranslateCOLORREF(dc, pdcattr->crBackgroundClr),
                                     NULL);
            }
        }
    }

    if (Ret)
    {
        pdcattr->ptlCurrent.x = XEnd;
        pdcattr->ptlCurrent.y = YEnd;
        pdcattr->ptfxCurrent = pdcattr->ptlCurrent;
        CoordLPtoDP(dc, &pdcattr->ptfxCurrent); // Update fx
        pdcattr->ulDirty_ &= ~(DIRTY_PTLCURRENT|DIRTY_PTFXCURRENT|DIRTY_STYLESTATE);
    }

    return Ret;
}

BOOL FASTCALL
IntGdiPolyBezier(DC      *dc,
                 LPPOINT pt,
                 DWORD   Count)
{
    BOOL ret = FALSE; // Default to FAILURE

    if ( PATH_IsPathOpen(dc->dclevel) )
    {
        return PATH_PolyBezier ( dc, pt, Count );
    }

    /* We'll convert it into line segments and draw them using Polyline */
    {
        POINT *Pts;
        INT nOut;

        Pts = GDI_Bezier ( pt, Count, &nOut );
        if ( Pts )
        {
            ret = IntGdiPolyline(dc, Pts, nOut);
            ExFreePoolWithTag(Pts, TAG_BEZIER);
        }
    }

    return ret;
}

BOOL FASTCALL
IntGdiPolyBezierTo(DC      *dc,
                   LPPOINT pt,
                   DWORD  Count)
{
    BOOL ret = FALSE; // Default to failure
    PDC_ATTR pdcattr = dc->pdcattr;

    if ( PATH_IsPathOpen(dc->dclevel) )
        ret = PATH_PolyBezierTo ( dc, pt, Count );
    else /* We'll do it using PolyBezier */
    {
        POINT *npt;
        npt = ExAllocatePoolWithTag(PagedPool,
                                    sizeof(POINT) * (Count + 1),
                                    TAG_BEZIER);
        if ( npt )
        {
            npt[0].x = pdcattr->ptlCurrent.x;
            npt[0].y = pdcattr->ptlCurrent.y;
            memcpy(npt + 1, pt, sizeof(POINT) * Count);
            ret = IntGdiPolyBezier(dc, npt, Count+1);
            ExFreePoolWithTag(npt, TAG_BEZIER);
        }
    }
    if ( ret )
    {
        pdcattr->ptlCurrent.x = pt[Count-1].x;
        pdcattr->ptlCurrent.y = pt[Count-1].y;
        pdcattr->ptfxCurrent = pdcattr->ptlCurrent;
        CoordLPtoDP(dc, &pdcattr->ptfxCurrent); // Update fx
        pdcattr->ulDirty_ &= ~(DIRTY_PTLCURRENT|DIRTY_PTFXCURRENT|DIRTY_STYLESTATE);
    }

    return ret;
}

BOOL FASTCALL
IntGdiPolyline(DC      *dc,
               LPPOINT pt,
               int     Count)
{
    SURFACE *psurf;
    BRUSH *pbrLine;
    LPPOINT Points;
    BOOL Ret = TRUE;
    LONG i;
    PDC_ATTR pdcattr = dc->pdcattr;
    PPATH pPath;

    if (!dc->dclevel.pSurface)
    {
        return TRUE;
    }

    DC_vPrepareDCsForBlit(dc, NULL, NULL, NULL);
    psurf = dc->dclevel.pSurface;

    /* Get BRUSHOBJ from current pen. */
    pbrLine = dc->dclevel.pbrLine;
    ASSERT(pbrLine);

    if (!(pbrLine->flAttrs & BR_IS_NULL))
    {
        Points = EngAllocMem(0, Count * sizeof(POINT), GDITAG_TEMP);
        if (Points != NULL)
        {
            RtlCopyMemory(Points, pt, Count * sizeof(POINT));
            IntLPtoDP(dc, Points, Count);

            /* Offset the array of points by the DC origin */
            for (i = 0; i < Count; i++)
            {
                Points[i].x += dc->ptlDCOrig.x;
                Points[i].y += dc->ptlDCOrig.y;
            }

            if (dc->fs & (DC_ACCUM_APP|DC_ACCUM_WMGR))
            {
               AddPenLinesBounds(dc, Count, Points);
            }

            if ((IntIsEffectiveWidePen(pbrLine) || !(pbrLine->flAttrs & BR_IS_SOLID)) &&
                IntWidePenLines(dc, Points, Count, FALSE))
            {
                Ret = TRUE;
            }
            else if (IntIsEffectiveWidePen(pbrLine))
            {
                /* Clear the path */
                PATH_Delete(dc->dclevel.hPath);
                dc->dclevel.hPath = NULL;

                /* Begin a path */
                pPath = PATH_CreatePath(Count);
                dc->dclevel.flPath |= DCPATH_ACTIVE;
                dc->dclevel.hPath = pPath->BaseObject.hHmgr;
                pPath->pos = pt[0];
                IntLPtoDP(dc, &pPath->pos, 1);

                PATH_MoveTo(dc, pPath);
                for (i = 1; i < Count; ++i)
                {
                    PATH_LineTo(dc, pt[i].x, pt[i].y);
                }

                /* Close the path */
                pPath->state = PATH_Closed;
                dc->dclevel.flPath &= ~DCPATH_ACTIVE;

                /* Actually stroke a path */
                Ret = PATH_StrokePath(dc, pPath);

                /* Clear the path */
                PATH_UnlockPath(pPath);
                PATH_Delete(dc->dclevel.hPath);
                dc->dclevel.hPath = NULL;
            }
            else
            {
                Ret = IntEngPolyline(&psurf->SurfObj,
                                     (CLIPOBJ *)&dc->co,
                                     &dc->eboLine.BrushObject,
                                     Points,
                                     Count,
                                     IntGdiLineMix(dc),
                                     TranslateCOLORREF(dc, pdcattr->crBackgroundClr));
            }
            EngFreeMem(Points);
        }
        else
        {
            Ret = FALSE;
        }
    }

    DC_vFinishBlit(dc, NULL);

    return Ret;
}

BOOL FASTCALL
IntGdiPolylineTo(DC      *dc,
                 LPPOINT pt,
                 DWORD   Count)
{
    BOOL ret = FALSE; // Default to failure
    PDC_ATTR pdcattr = dc->pdcattr;

    if (PATH_IsPathOpen(dc->dclevel))
    {
        ret = PATH_PolylineTo(dc, pt, Count);
    }
    else /* Do it using Polyline */
    {
        POINT *pts = ExAllocatePoolWithTag(PagedPool,
                                           sizeof(POINT) * (Count + 1),
                                           TAG_SHAPE);
        if ( pts )
        {
            pts[0].x = pdcattr->ptlCurrent.x;
            pts[0].y = pdcattr->ptlCurrent.y;
            memcpy( pts + 1, pt, sizeof(POINT) * Count);
            ret = IntGdiPolyline(dc, pts, Count + 1);
            ExFreePoolWithTag(pts, TAG_SHAPE);
        }
    }
    if ( ret )
    {
        pdcattr->ptlCurrent.x = pt[Count-1].x;
        pdcattr->ptlCurrent.y = pt[Count-1].y;
        pdcattr->ptfxCurrent = pdcattr->ptlCurrent;
        CoordLPtoDP(dc, &pdcattr->ptfxCurrent); // Update fx
        pdcattr->ulDirty_ &= ~(DIRTY_PTLCURRENT|DIRTY_PTFXCURRENT|DIRTY_STYLESTATE);
    }

    return ret;
}


BOOL FASTCALL
IntGdiPolyPolyline(DC      *dc,
                   LPPOINT pt,
                   PULONG  PolyPoints,
                   DWORD   Count)
{
    ULONG i;
    LPPOINT pts;
    PULONG pc;
    BOOL ret = FALSE; // Default to failure
    pts = pt;
    pc = PolyPoints;

    if (PATH_IsPathOpen(dc->dclevel))
    {
        return PATH_PolyPolyline( dc, pt, PolyPoints, Count );
    }
    for (i = 0; i < Count; i++)
    {
        ret = IntGdiPolyline ( dc, pts, *pc );
        if (ret == FALSE)
        {
            return ret;
        }
        pts+=*pc++;
    }

    return ret;
}

/******************************************************************************/

BOOL
APIENTRY
NtGdiLineTo(HDC  hDC,
            int  XEnd,
            int  YEnd)
{
    DC *dc;
    BOOL Ret;
    RECT rcLockRect ;

    dc = DC_LockDc(hDC);
    if (!dc)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    rcLockRect.left = dc->pdcattr->ptlCurrent.x;
    rcLockRect.top = dc->pdcattr->ptlCurrent.y;
    rcLockRect.right = XEnd;
    rcLockRect.bottom = YEnd;

    IntLPtoDP(dc, (PPOINT)&rcLockRect, 2);

    /* The DCOrg is in device coordinates */
    rcLockRect.left += dc->ptlDCOrig.x;
    rcLockRect.top += dc->ptlDCOrig.y;
    rcLockRect.right += dc->ptlDCOrig.x;
    rcLockRect.bottom += dc->ptlDCOrig.y;

    /* Endpoints are not half-open pixel bounds: a horizontal/vertical line
     * has an empty axis here and a wide pen extends beyond both endpoints. */
    DC_vPrepareDCsForBlit(dc, (dc->fs & DC_REDIRECTION) ? NULL : &rcLockRect, NULL, NULL);

    Ret = IntGdiLineTo(dc, XEnd, YEnd);

    DC_vFinishBlit(dc, NULL);

    DC_UnlockDc(dc);
    return Ret;
}

// FIXME: This function is completely broken
static
BOOL
GdiPolyDraw(
    IN HDC hdc,
    IN LPPOINT lppt,
    IN LPBYTE lpbTypes,
    IN ULONG cCount)
{
    PDC dc;
    PDC_ATTR pdcattr;
    POINT bzr[4];
    volatile PPOINT line_pts, line_pts_old, bzr_pts;
    INT num_pts, num_bzr_pts, space, space_old, size;
    ULONG i;
    BOOL result = FALSE;

    dc = DC_LockDc(hdc);
    if (!dc) return FALSE;
    pdcattr = dc->pdcattr;

    if (pdcattr->ulDirty_ & (DIRTY_FILL | DC_BRUSH_DIRTY))
       DC_vUpdateFillBrush(dc);

    if (pdcattr->ulDirty_ & (DIRTY_LINE | DC_PEN_DIRTY))
       DC_vUpdateLineBrush(dc);

    if (!cCount)
    {
       DC_UnlockDc(dc);
       return TRUE;
    }

    line_pts = NULL;
    line_pts_old = NULL;
    bzr_pts = NULL;

    {
        if (PATH_IsPathOpen(dc->dclevel))
        {
           result = PATH_PolyDraw(dc, (const POINT *)lppt, (const BYTE *)lpbTypes, cCount);
           goto Cleanup;
        }

        /* Check for valid point types */
        for (i = 0; i < cCount; i++)
        {
           switch (lpbTypes[i])
           {
           case PT_MOVETO:
           case PT_LINETO | PT_CLOSEFIGURE:
           case PT_LINETO:
               break;
           case PT_BEZIERTO:
               if((i + 2 < cCount) && (lpbTypes[i + 1] == PT_BEZIERTO) &&
                  ((lpbTypes[i + 2] & ~PT_CLOSEFIGURE) == PT_BEZIERTO))
               {
                   i += 2;
                   break;
               }
           default:
               goto Cleanup;
           }
        }

        space = cCount + 300;
        line_pts = ExAllocatePoolWithTag(PagedPool, space * sizeof(POINT), TAG_SHAPE);
        if (line_pts == NULL)
        {
            result = FALSE;
            goto Cleanup;
        }

        num_pts = 1;

        line_pts[0].x = pdcattr->ptlCurrent.x;
        line_pts[0].y = pdcattr->ptlCurrent.y;

        for ( i = 0; i < cCount; i++ )
        {
           switch (lpbTypes[i])
           {
           case PT_MOVETO:
               if (num_pts >= 2) IntGdiPolyline( dc, line_pts, num_pts );
               num_pts = 0;
               line_pts[num_pts++] = lppt[i];
               break;
           case PT_LINETO:
           case (PT_LINETO | PT_CLOSEFIGURE):
               line_pts[num_pts++] = lppt[i];
               break;
           case PT_BEZIERTO:
               bzr[0].x = line_pts[num_pts - 1].x;
               bzr[0].y = line_pts[num_pts - 1].y;
               RtlCopyMemory( &bzr[1], &lppt[i], 3 * sizeof(POINT) );

               if ((bzr_pts = GDI_Bezier( bzr, 4, &num_bzr_pts )))
               {
                   size = num_pts + (cCount - i) + num_bzr_pts;
                   if (space < size)
                   {
                      space_old = space;
                      space = size * 2;
                      line_pts_old = line_pts;
                      line_pts = ExAllocatePoolWithTag(PagedPool, space * sizeof(POINT), TAG_SHAPE);
                      if (!line_pts) goto Cleanup;
                      RtlCopyMemory(line_pts, line_pts_old, space_old * sizeof(POINT));
                      ExFreePoolWithTag(line_pts_old, TAG_SHAPE);
                      line_pts_old = NULL;
                   }
                   RtlCopyMemory( &line_pts[num_pts], &bzr_pts[1], (num_bzr_pts - 1) * sizeof(POINT) );
                   num_pts += num_bzr_pts - 1;
                   ExFreePoolWithTag(bzr_pts, TAG_BEZIER);
                   bzr_pts = NULL;
               }
               i += 2;
               break;
           }
           if (lpbTypes[i] & PT_CLOSEFIGURE) line_pts[num_pts++] = line_pts[0];
        }

        if (num_pts >= 2) IntGdiPolyline( dc, line_pts, num_pts );
        IntGdiMoveToEx( dc, line_pts[num_pts - 1].x, line_pts[num_pts - 1].y, NULL );
        result = TRUE;
    }

Cleanup:

    if (line_pts != NULL)
    {
        ExFreePoolWithTag(line_pts, TAG_SHAPE);
    }

    if ((line_pts_old != NULL) && (line_pts_old != line_pts))
    {
        ExFreePoolWithTag(line_pts_old, TAG_SHAPE);
    }

    if (bzr_pts != NULL)
    {
        ExFreePoolWithTag(bzr_pts, TAG_BEZIER);
    }

    DC_UnlockDc(dc);

    return result;
}

__kernel_entry
W32KAPI
BOOL
APIENTRY
NtGdiPolyDraw(
    _In_ HDC hdc,
    _In_reads_(cpt) LPPOINT ppt,
    _In_reads_(cpt) LPBYTE pjAttr,
    _In_ ULONG cpt)
{
    PBYTE pjBuffer;
    PPOINT pptSafe;
    PBYTE pjSafe;
    SIZE_T cjSizePt;
    BOOL bResult;

    if (cpt == 0)
    {
        ERR("cpt is 0\n");
        return FALSE;
    }

    /* Validate that cpt isn't too large */
    if (cpt > ((MAXULONG - cpt) / sizeof(*ppt)))
    {
        ERR("cpt is too large\n", cpt);
        return FALSE;
    }

    /* Calculate size for a buffer */
    cjSizePt = cpt * sizeof(*ppt);
    ASSERT(cjSizePt + cpt > cjSizePt);

    /* Allocate a buffer for all data */
    pjBuffer = ExAllocatePoolWithTag(PagedPool, cjSizePt + cpt, TAG_SHAPE);
    if (pjBuffer == NULL)
    {
        ERR("Failed to allocate buffer\n");
        return FALSE;
    }

    pptSafe = (PPOINT)pjBuffer;
    pjSafe = pjBuffer + cjSizePt;

    _SEH2_TRY
    {
        ProbeArrayForRead(ppt, sizeof(*ppt), cpt, sizeof(ULONG));
        ProbeArrayForRead(pjAttr, sizeof(*pjAttr), cpt, sizeof(BYTE));

        /* Copy the arrays */
        RtlCopyMemory(pptSafe, ppt, cjSizePt);
        RtlCopyMemory(pjSafe, pjAttr, cpt);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        SetLastNtError(_SEH2_GetExceptionCode());
        bResult = FALSE;
        goto Cleanup;
    }
    _SEH2_END;

    /* Call the internal function */
    bResult = GdiPolyDraw(hdc, pptSafe, pjSafe, cpt);

Cleanup:

    /* Free the buffer */
    ExFreePoolWithTag(pjBuffer, TAG_SHAPE);

    return bResult;
}

/*
 * @implemented
 */
_Success_(return != FALSE)
BOOL
APIENTRY
NtGdiMoveTo(
    IN HDC hdc,
    IN INT x,
    IN INT y,
    OUT OPTIONAL LPPOINT pptOut)
{
    PDC pdc;
    BOOL Ret;
    POINT Point;

    pdc = DC_LockDc(hdc);
    if (!pdc) return FALSE;

    Ret = IntGdiMoveToEx(pdc, x, y, &Point);

    if (Ret && pptOut)
    {
       _SEH2_TRY
       {
           ProbeForWrite(pptOut, sizeof(POINT), 1);
           RtlCopyMemory(pptOut, &Point, sizeof(POINT));
       }
       _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
       {
           SetLastNtError(_SEH2_GetExceptionCode());
           Ret = FALSE; // CHECKME: is this correct?
       }
       _SEH2_END;
    }

    DC_UnlockDc(pdc);

    return Ret;
}

/* EOF */
