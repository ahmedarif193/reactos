/*
 * PROJECT:     ReactOS Win32k graphics engine
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Generic parallelogram bit-block transfers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <win32k.h>

#define NDEBUG
#include <debug.h>

#define PLGBLT_MAX_FIXED_DELTA 0x1fffffffLL

typedef struct _PLGBLT_SNAPSHOT
{
    HBITMAP hBitmap;
    SURFOBJ *pso;
    RECTL SourceRect;
} PLGBLT_SNAPSHOT, *PPLGBLT_SNAPSHOT;

typedef struct _PLGBLT_RENDER
{
    SURFOBJ *psoDest;
    SURFOBJ *psoSource;
    SURFOBJ *psoMask;
    XLATEOBJ *pxlo;
    PFN_DIB_PutPixel pfnPutPixel;
    PFN_DIB_GetPixel pfnGetMaskPixel;
    LONGLONG ax;
    LONGLONG ay;
    LONGLONG ux;
    LONGLONG uy;
    LONGLONG vx;
    LONGLONG vy;
    LONGLONG Determinant;
    BOOL ReverseOrientation;
    RECTL SourceRect;
    RECTL AccessibleSourceRect;
} PLGBLT_RENDER, *PPLGBLT_RENDER;

static BOOL
EngpIntersectSurfaceRect(
    _In_ SURFOBJ *pso,
    _In_ const RECTL *prcl,
    _Out_ RECTL *prclClipped)
{
    prclClipped->left = max(prcl->left, 0);
    prclClipped->top = max(prcl->top, 0);
    prclClipped->right = min(prcl->right, pso->sizlBitmap.cx);
    prclClipped->bottom = min(prcl->bottom, pso->sizlBitmap.cy);
    return prclClipped->left < prclClipped->right &&
           prclClipped->top < prclClipped->bottom;
}

static VOID
EngpDeleteSnapshot(
    _Inout_ PPLGBLT_SNAPSHOT Snapshot)
{
    if (Snapshot->pso != NULL)
    {
        EngUnlockSurface(Snapshot->pso);
        Snapshot->pso = NULL;
    }
    if (Snapshot->hBitmap != NULL)
    {
        EngDeleteSurface((HSURF)Snapshot->hBitmap);
        Snapshot->hBitmap = NULL;
    }
}

static BOOL
EngpCreateSnapshot(
    _In_ SURFOBJ *psoSource,
    _In_ const RECTL *prclSource,
    _Out_ PPLGBLT_SNAPSHOT Snapshot)
{
    INTENG_ENTER_LEAVE EnterLeave;
    SURFOBJ *psoInput;
    POINTL Translate;
    POINTL SourcePoint;
    RECTL InputRect;
    RECTL DestRect;
    SIZEL Size;
    LONG Delta;
    BOOL Result;

    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    InputRect = *prclSource;
    Size.cx = InputRect.right - InputRect.left;
    Size.cy = InputRect.bottom - InputRect.top;
    if (Size.cx <= 0 || Size.cy <= 0 ||
        psoSource->iBitmapFormat < BMF_1BPP ||
        psoSource->iBitmapFormat > BMF_32BPP)
    {
        return FALSE;
    }

    if (!IntEngEnter(&EnterLeave,
                     psoSource,
                     &InputRect,
                     TRUE,
                     &Translate,
                     &psoInput))
    {
        return FALSE;
    }

    Delta = WIDTH_BYTES_ALIGN32(Size.cx,
                                BitsPerFormat(psoSource->iBitmapFormat));
    Snapshot->hBitmap = EngCreateBitmap(Size,
                                        Delta,
                                        psoSource->iBitmapFormat,
                                        BMF_TOPDOWN | BMF_NOZEROINIT,
                                        NULL);
    if (Snapshot->hBitmap == NULL)
    {
        IntEngLeave(&EnterLeave);
        return FALSE;
    }

    Snapshot->pso = EngLockSurface((HSURF)Snapshot->hBitmap);
    if (Snapshot->pso == NULL)
    {
        IntEngLeave(&EnterLeave);
        EngpDeleteSnapshot(Snapshot);
        return FALSE;
    }

    DestRect.left = 0;
    DestRect.top = 0;
    DestRect.right = Size.cx;
    DestRect.bottom = Size.cy;
    SourcePoint.x = InputRect.left + Translate.x;
    SourcePoint.y = InputRect.top + Translate.y;
    Result = EngCopyBits(Snapshot->pso,
                         psoInput,
                         NULL,
                         NULL,
                         &DestRect,
                         &SourcePoint);
    IntEngLeave(&EnterLeave);
    if (!Result)
    {
        EngpDeleteSnapshot(Snapshot);
        return FALSE;
    }

    Snapshot->SourceRect = *prclSource;
    return TRUE;
}

static LONGLONG
EngpFloorFixed(
    _In_ LONGLONG Value)
{
    if (Value >= 0)
        return Value / 16;
    return -((-Value + 15) / 16);
}

static LONGLONG
EngpCeilingFixed(
    _In_ LONGLONG Value)
{
    if (Value >= 0)
        return (Value + 15) / 16;
    return -((-Value) / 16);
}

static BOOL
EngpGetDestinationBounds(
    _In_ SURFOBJ *psoDest,
    _In_reads_(3) POINTFIX *pptfx,
    _Out_ RECTL *prclBounds)
{
    LONGLONG dx;
    LONGLONG dy;
    LONGLONG MinX;
    LONGLONG MinY;
    LONGLONG MaxX;
    LONGLONG MaxY;
    LONGLONG Left;
    LONGLONG Top;
    LONGLONG Right;
    LONGLONG Bottom;

    dx = (LONGLONG)pptfx[1].x + pptfx[2].x - pptfx[0].x;
    dy = (LONGLONG)pptfx[1].y + pptfx[2].y - pptfx[0].y;
    MinX = min(min((LONGLONG)pptfx[0].x, (LONGLONG)pptfx[1].x),
               min((LONGLONG)pptfx[2].x, dx));
    MinY = min(min((LONGLONG)pptfx[0].y, (LONGLONG)pptfx[1].y),
               min((LONGLONG)pptfx[2].y, dy));
    MaxX = max(max((LONGLONG)pptfx[0].x, (LONGLONG)pptfx[1].x),
               max((LONGLONG)pptfx[2].x, dx));
    MaxY = max(max((LONGLONG)pptfx[0].y, (LONGLONG)pptfx[1].y),
               max((LONGLONG)pptfx[2].y, dy));

    Left = max(EngpCeilingFixed(MinX), 0);
    Top = max(EngpCeilingFixed(MinY), 0);
    Right = min(EngpFloorFixed(MaxX) + 1,
                (LONGLONG)psoDest->sizlBitmap.cx);
    Bottom = min(EngpFloorFixed(MaxY) + 1,
                 (LONGLONG)psoDest->sizlBitmap.cy);
    if (Left >= Right || Top >= Bottom)
        return FALSE;

    prclBounds->left = (LONG)Left;
    prclBounds->top = (LONG)Top;
    prclBounds->right = (LONG)Right;
    prclBounds->bottom = (LONG)Bottom;
    return TRUE;
}

/* Return floor(Numerator * Multiplier / Denominator) without overflowing. */
static ULONG
EngpScaleFraction(
    _In_ ULONGLONG Numerator,
    _In_ ULONG Multiplier,
    _In_ ULONGLONG Denominator)
{
    ULONGLONG Quotient = 0;
    ULONGLONG Remainder = 0;
    ULONG Bit;

    for (Bit = 0x80000000; Bit != 0; Bit >>= 1)
    {
        Quotient <<= 1;
        if (Remainder >= Denominator - Remainder)
        {
            Remainder -= Denominator - Remainder;
            Quotient++;
        }
        else
        {
            Remainder += Remainder;
        }

        if (Multiplier & Bit)
        {
            if (Remainder >= Denominator - Numerator)
            {
                Remainder -= Denominator - Numerator;
                Quotient++;
            }
            else
            {
                Remainder += Numerator;
            }
        }
    }

    return (ULONG)Quotient;
}

static VOID
EngpRenderRect(
    _In_ PPLGBLT_RENDER Render,
    _In_ const RECTL *prcl)
{
    LONGLONG rx;
    LONGLONG ry;
    LONGLONG u;
    LONGLONG v;
    ULONG SourceWidth;
    ULONG SourceHeight;
    ULONG SourceOffsetX;
    ULONG SourceOffsetY;
    ULONG Color;
    LONG SourceX;
    LONG SourceY;
    LONG x;
    LONG y;

    SourceWidth = Render->SourceRect.right - Render->SourceRect.left;
    SourceHeight = Render->SourceRect.bottom - Render->SourceRect.top;

    for (y = prcl->top; y < prcl->bottom; ++y)
    {
        for (x = prcl->left; x < prcl->right; ++x)
        {
            rx = (LONGLONG)x * 16 - Render->ax;
            ry = (LONGLONG)y * 16 - Render->ay;
            u = rx * Render->vy - ry * Render->vx;
            v = Render->ux * ry - Render->uy * rx;
            if (Render->ReverseOrientation)
            {
                u = -u;
                v = -v;
            }
            if (u < 0 || v < 0 ||
                u >= Render->Determinant ||
                v >= Render->Determinant)
            {
                continue;
            }

            SourceOffsetX = EngpScaleFraction((ULONGLONG)u,
                                               SourceWidth,
                                               Render->Determinant);
            SourceOffsetY = EngpScaleFraction((ULONGLONG)v,
                                               SourceHeight,
                                               Render->Determinant);
            SourceX = Render->SourceRect.left + SourceOffsetX;
            SourceY = Render->SourceRect.top + SourceOffsetY;
            if (SourceX < Render->AccessibleSourceRect.left ||
                SourceX >= Render->AccessibleSourceRect.right ||
                SourceY < Render->AccessibleSourceRect.top ||
                SourceY >= Render->AccessibleSourceRect.bottom)
            {
                continue;
            }

            if (Render->psoMask != NULL)
            {
                if (Render->pfnGetMaskPixel(Render->psoMask,
                                            SourceOffsetX,
                                            SourceOffsetY) == 0)
                {
                    continue;
                }
            }

            Color = DIB_GetSource(Render->psoSource,
                                  SourceX - Render->AccessibleSourceRect.left,
                                  SourceY - Render->AccessibleSourceRect.top,
                                  Render->pxlo);
            Render->pfnPutPixel(Render->psoDest, x, y, Color);
        }
    }
}

BOOL
APIENTRY
EngPlgBlt(
    IN SURFOBJ *psoDest,
    IN SURFOBJ *psoSource,
    IN SURFOBJ *psoMask,
    IN CLIPOBJ *pco,
    IN XLATEOBJ *pxlo,
    IN COLORADJUSTMENT *pca,
    IN POINTL *pptlBrushOrg,
    IN POINTFIX *pptfx,
    IN RECTL *prclSource,
    IN POINTL *pptlMask,
    IN ULONG iMode)
{
    PLGBLT_SNAPSHOT SourceSnapshot;
    PLGBLT_SNAPSHOT MaskSnapshot;
    PLGBLT_RENDER Render;
    INTENG_ENTER_LEAVE EnterLeaveDest;
    SURFOBJ *psoOutput;
    POINTL Translate;
    RECTL AccessibleSourceRect;
    RECTL OutputRect;
    RECTL RenderRect;
    RECTL ClipRect;
    RECTL MaskRect;
    RECT_ENUM RectEnum;
    LONGLONG Determinant;
    LONGLONG MaskRight;
    LONGLONG MaskBottom;
    LONG SourceWidth;
    LONG SourceHeight;
    BOOL EnumMore;
    BOOL Result;
    ULONG i;

    UNREFERENCED_PARAMETER(pca);
    UNREFERENCED_PARAMETER(pptlBrushOrg);
    UNREFERENCED_PARAMETER(iMode);

    RtlZeroMemory(&SourceSnapshot, sizeof(SourceSnapshot));
    RtlZeroMemory(&MaskSnapshot, sizeof(MaskSnapshot));
    if (psoDest == NULL || psoSource == NULL || pptfx == NULL ||
        prclSource == NULL ||
        psoDest->iBitmapFormat < BMF_1BPP ||
        psoDest->iBitmapFormat > BMF_32BPP ||
        psoSource->iBitmapFormat < BMF_1BPP ||
        psoSource->iBitmapFormat > BMF_32BPP ||
        prclSource->left >= prclSource->right ||
        prclSource->top >= prclSource->bottom ||
        (LONGLONG)prclSource->right - prclSource->left > MAXLONG ||
        (LONGLONG)prclSource->bottom - prclSource->top > MAXLONG)
    {
        return FALSE;
    }
    if (psoMask != NULL &&
        (psoMask->iBitmapFormat != BMF_1BPP ||
         psoMask->sizlBitmap.cx <= 0 ||
         psoMask->sizlBitmap.cy <= 0 ||
         pptlMask == NULL))
    {
        return FALSE;
    }

    SourceWidth = prclSource->right - prclSource->left;
    SourceHeight = prclSource->bottom - prclSource->top;
    if (psoMask != NULL)
    {
        MaskRight = (LONGLONG)pptlMask->x + SourceWidth;
        MaskBottom = (LONGLONG)pptlMask->y + SourceHeight;
        if (pptlMask->x < 0 || pptlMask->y < 0 ||
            MaskRight > psoMask->sizlBitmap.cx ||
            MaskBottom > psoMask->sizlBitmap.cy)
        {
            return FALSE;
        }
    }

    Render.ux = (LONGLONG)pptfx[1].x - pptfx[0].x;
    Render.uy = (LONGLONG)pptfx[1].y - pptfx[0].y;
    Render.vx = (LONGLONG)pptfx[2].x - pptfx[0].x;
    Render.vy = (LONGLONG)pptfx[2].y - pptfx[0].y;
    if (Render.ux > PLGBLT_MAX_FIXED_DELTA ||
        Render.ux < -PLGBLT_MAX_FIXED_DELTA ||
        Render.uy > PLGBLT_MAX_FIXED_DELTA ||
        Render.uy < -PLGBLT_MAX_FIXED_DELTA ||
        Render.vx > PLGBLT_MAX_FIXED_DELTA ||
        Render.vx < -PLGBLT_MAX_FIXED_DELTA ||
        Render.vy > PLGBLT_MAX_FIXED_DELTA ||
        Render.vy < -PLGBLT_MAX_FIXED_DELTA)
    {
        return FALSE;
    }

    Determinant = Render.ux * Render.vy - Render.uy * Render.vx;
    if (Determinant == 0)
        return FALSE;
    Render.ReverseOrientation = Determinant < 0;
    Render.Determinant = Render.ReverseOrientation ?
        -Determinant : Determinant;

    if (!EngpGetDestinationBounds(psoDest, pptfx, &OutputRect))
        return TRUE;
    if (pco != NULL && pco->iDComplexity != DC_TRIVIAL &&
        !RECTL_bIntersectRect(&OutputRect, &OutputRect, &pco->rclBounds))
    {
        return TRUE;
    }
    if (!EngpIntersectSurfaceRect(psoSource,
                                  prclSource,
                                  &AccessibleSourceRect))
    {
        return TRUE;
    }

    if (!EngpCreateSnapshot(psoSource,
                            &AccessibleSourceRect,
                            &SourceSnapshot))
    {
        return FALSE;
    }
    if (psoMask != NULL)
    {
        MaskRect.left = pptlMask->x;
        MaskRect.top = pptlMask->y;
        MaskRect.right = (LONG)MaskRight;
        MaskRect.bottom = (LONG)MaskBottom;
        if (!EngpCreateSnapshot(psoMask, &MaskRect, &MaskSnapshot))
        {
            EngpDeleteSnapshot(&SourceSnapshot);
            return FALSE;
        }
    }

    if (!IntEngEnter(&EnterLeaveDest,
                     psoDest,
                     &OutputRect,
                     FALSE,
                     &Translate,
                     &psoOutput))
    {
        EngpDeleteSnapshot(&MaskSnapshot);
        EngpDeleteSnapshot(&SourceSnapshot);
        return FALSE;
    }

    Render.psoDest = psoOutput;
    Render.psoSource = SourceSnapshot.pso;
    Render.psoMask = MaskSnapshot.pso;
    Render.pxlo = pxlo;
    Render.pfnPutPixel =
        DibFunctionsForBitmapFormat[psoOutput->iBitmapFormat].DIB_PutPixel;
    Render.pfnGetMaskPixel = psoMask != NULL ?
        DibFunctionsForBitmapFormat[BMF_1BPP].DIB_GetPixel : NULL;
    Render.ax = (LONGLONG)pptfx[0].x + (LONGLONG)Translate.x * 16;
    Render.ay = (LONGLONG)pptfx[0].y + (LONGLONG)Translate.y * 16;
    Render.SourceRect = *prclSource;
    Render.AccessibleSourceRect = AccessibleSourceRect;

    RenderRect = OutputRect;
    RenderRect.left += Translate.x;
    RenderRect.right += Translate.x;
    RenderRect.top += Translate.y;
    RenderRect.bottom += Translate.y;
    Result = TRUE;
    if (pco == NULL || pco->iDComplexity == DC_TRIVIAL)
    {
        EngpRenderRect(&Render, &RenderRect);
    }
    else if (pco->iDComplexity == DC_RECT)
    {
        ClipRect = pco->rclBounds;
        if (RECTL_bIntersectRect(&ClipRect, &ClipRect, &OutputRect))
        {
            ClipRect.left += Translate.x;
            ClipRect.right += Translate.x;
            ClipRect.top += Translate.y;
            ClipRect.bottom += Translate.y;
            EngpRenderRect(&Render, &ClipRect);
        }
    }
    else
    {
        CLIPOBJ_cEnumStart(pco, FALSE, CT_RECTANGLES, CD_ANY, 0);
        do
        {
            EnumMore = CLIPOBJ_bEnum(pco,
                                     sizeof(RectEnum),
                                     (PULONG)&RectEnum);
            for (i = 0; i < RectEnum.c; ++i)
            {
                if (RECTL_bIntersectRect(&ClipRect,
                                         &RectEnum.arcl[i],
                                         &OutputRect))
                {
                    ClipRect.left += Translate.x;
                    ClipRect.right += Translate.x;
                    ClipRect.top += Translate.y;
                    ClipRect.bottom += Translate.y;
                    EngpRenderRect(&Render, &ClipRect);
                }
            }
        }
        while (EnumMore);
    }

    Result = IntEngLeave(&EnterLeaveDest) && Result;
    EngpDeleteSnapshot(&MaskSnapshot);
    EngpDeleteSnapshot(&SourceSnapshot);
    return Result;
}

BOOL
APIENTRY
IntEngPlgBlt(
    _Inout_ SURFOBJ *psoDest,
    _Inout_ SURFOBJ *psoSource,
    _In_opt_ SURFOBJ *psoMask,
    _In_opt_ CLIPOBJ *pco,
    _In_opt_ XLATEOBJ *pxlo,
    _In_opt_ COLORADJUSTMENT *pca,
    _In_opt_ POINTL *pptlBrushOrg,
    _In_reads_(3) POINTFIX *pptfx,
    _In_ RECTL *prclSource,
    _In_opt_ POINTL *pptlMask,
    _In_ ULONG iMode)
{
    SURFACE *psurfDest;
    BOOL Result = FALSE;

    if (psoDest == NULL || psoSource == NULL)
        return FALSE;

    psurfDest = CONTAINING_RECORD(psoDest, SURFACE, SurfObj);
    if ((psurfDest->flags & HOOK_PLGBLT) &&
        GDIDEVFUNCS(psoDest).PlgBlt != NULL)
    {
        Result = GDIDEVFUNCS(psoDest).PlgBlt(psoDest,
                                             psoSource,
                                             psoMask,
                                             pco,
                                             pxlo,
                                             pca,
                                             pptlBrushOrg,
                                             pptfx,
                                             prclSource,
                                             pptlMask,
                                             iMode);
    }

    if (!Result)
    {
        Result = EngPlgBlt(psoDest,
                           psoSource,
                           psoMask,
                           pco,
                           pxlo,
                           pca,
                           pptlBrushOrg,
                           pptfx,
                           prclSource,
                           pptlMask,
                           iMode);
    }
    return Result;
}
