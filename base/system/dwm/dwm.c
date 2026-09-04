/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     User-mode compositor: pulls frame metadata from win32k, maps
 *              the per-window section surfaces read-only and composes them
 *              into the primary. See sdk/include/reactos/dwmframe.h.
 */

#include <windows.h>
#include <math.h>
#include <reactos/dwmcore.h>
#include <reactos/dwmframe.h>

#include "dxsurface.h"

DWORD_PTR NTAPI NtUserCallOneParam(DWORD_PTR Param, DWORD Routine);

static void DwmLog(const char *s) { OutputDebugStringA(s); }

static LONG g_originX, g_originY;

typedef struct _DWM_SURFACE
{
    ULONG  Id;
    ULONG  Generation;
    HANDLE hSection;
    BYTE  *View;
    ULONG  LastSeenFrame;
    BOOL   Used;
} DWM_SURFACE;
#define DWM_VIEW_CACHE_SIZE (DWM_MAX_WINDOWS * 2)
static DWM_SURFACE g_views[DWM_VIEW_CACHE_SIZE];
static ULONG g_frameSeq;

static void
DwmDropView(DWM_SURFACE *v)
{
    if (v->View)     UnmapViewOfFile(v->View);
    if (v->hSection) CloseHandle(v->hSection);
    RtlZeroMemory(v, sizeof(*v));
}

static const BYTE *
DwmGetSurfaceView(const DWM_WIN *w)
{
    DWM_SURFACE *slot = NULL;
    DWM_SURFACE *sameIdOldest = NULL;
    DWM_OPEN_SURFACE req;
    ULONG i;

    if (w->BaseGlobalShare != 0)
        return DwmDxGetRedirectionSnapshot(w);

    for (i = 0; i < DWM_VIEW_CACHE_SIZE; i++)
    {
        if (g_views[i].Used && g_views[i].Id == w->SurfaceId &&
            g_views[i].Generation == w->Generation)
        {
            slot = &g_views[i];
            break;
        }
        if (!g_views[i].Used && slot == NULL)
            slot = &g_views[i];
        else if (g_views[i].Used && g_views[i].Id == w->SurfaceId &&
                 (sameIdOldest == NULL ||
                  g_views[i].LastSeenFrame < sameIdOldest->LastSeenFrame))
            sameIdOldest = &g_views[i];
    }
    if (slot != NULL && slot->Used)
    {
        slot->LastSeenFrame = g_frameSeq;
        return slot->View;
    }

    if (slot == NULL)
        slot = sameIdOldest;
    if (slot == NULL)
        return NULL;
    if (slot->Used)
        DwmDropView(slot);

    req.SurfaceId = w->SurfaceId;
    req.Generation = w->Generation;
    req.hSection = NULL;
    if ((LONG)NtUserCallOneParam((DWORD_PTR)&req, DWM_ROUTINE_OPENSURFACE) < 0 ||
        req.hSection == NULL)
        return NULL;

    slot->View = (BYTE *)MapViewOfFile(req.hSection, FILE_MAP_READ, 0, 0, 0);
    if (slot->View == NULL)
    {
        CloseHandle(req.hSection);
        return NULL;
    }
    slot->hSection = req.hSection;
    slot->Id = w->SurfaceId;
    slot->Generation = w->Generation;
    slot->LastSeenFrame = g_frameSeq;
    slot->Used = TRUE;
    return slot->View;
}

static void
DwmSweepViews(void)
{
    ULONG i;
    for (i = 0; i < DWM_VIEW_CACHE_SIZE; i++)
    {
        if (g_views[i].Used && (g_frameSeq - g_views[i].LastSeenFrame) > 256)
            DwmDropView(&g_views[i]);
    }
}

#define DWM_SHADOW_WIDE_EXTENT_96       24
#define DWM_SHADOW_TOP_MARGIN_96        12
#define DWM_SHADOW_BOTTOM_MARGIN_96     32
#define DWM_SHADOW_ACTIVE_OFFSET_NUMERATOR_96   32
#define DWM_SHADOW_INACTIVE_OFFSET_NUMERATOR_96 18
#define DWM_SHADOW_OFFSET_DENOMINATOR_96        3
#define DWM_SHADOW_ACTIVE_WIDE_OPACITY  280
#define DWM_SHADOW_ACTIVE_TIGHT_OPACITY 220
#define DWM_SHADOW_INACTIVE_WIDE_OPACITY 190
#define DWM_SHADOW_INACTIVE_TIGHT_OPACITY 150
#define DWM_SHADOW_DARK_ACTIVE_WIDE_OPACITY 560
#define DWM_SHADOW_DARK_ACTIVE_TIGHT_OPACITY 550
#define DWM_SHADOW_DARK_INACTIVE_OPACITY 370
#define DWM_SHADOW_WEIGHT_SCALE         1048576.0

typedef struct _DWM_SHADOW_KERNEL
{
    LONG Support;
    ULONGLONG Total;
    ULONGLONG *Prefix;
} DWM_SHADOW_KERNEL;

static LONG g_shadowDpi;
static LONG g_shadowMarginLeft;
static LONG g_shadowMarginTop;
static LONG g_shadowMarginRight;
static LONG g_shadowMarginBottom;
static LONG g_shadowActiveOffset;
static LONG g_shadowInactiveOffset;
static DWM_SHADOW_KERNEL g_shadowWide;
static DWM_SHADOW_KERNEL g_shadowActiveTight;
static DWM_SHADOW_KERNEL g_shadowInactiveTight;
static ULONG *g_shadowCoverWide;
static ULONG *g_shadowCoverTight;
static LONG g_shadowCoverWidth;

static BOOL
DwmEnsureShadowCoverage(LONG Width)
{
    ULONG *Wide, *Tight;
    SIZE_T Bytes;

    if (Width <= 0 || (SIZE_T)Width > (SIZE_T)-1 / sizeof(ULONG))
        return FALSE;
    if (g_shadowCoverWide != NULL && g_shadowCoverTight != NULL &&
        g_shadowCoverWidth == Width)
        return TRUE;

    Bytes = (SIZE_T)Width * sizeof(ULONG);
    Wide = VirtualAlloc(NULL, Bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Wide == NULL)
        return FALSE;
    Tight = VirtualAlloc(NULL, Bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Tight == NULL)
    {
        VirtualFree(Wide, 0, MEM_RELEASE);
        return FALSE;
    }

    if (g_shadowCoverWide != NULL)
        VirtualFree(g_shadowCoverWide, 0, MEM_RELEASE);
    if (g_shadowCoverTight != NULL)
        VirtualFree(g_shadowCoverTight, 0, MEM_RELEASE);
    g_shadowCoverWide = Wide;
    g_shadowCoverTight = Tight;
    g_shadowCoverWidth = Width;
    return TRUE;
}

static void
DwmShadowFreeKernel(DWM_SHADOW_KERNEL *Kernel)
{
    if (Kernel->Prefix != NULL)
        VirtualFree(Kernel->Prefix, 0, MEM_RELEASE);
    RtlZeroMemory(Kernel, sizeof(*Kernel));
}

static BOOL
DwmShadowBuildKernel(DWM_SHADOW_KERNEL *Kernel, LONG Support, double Sigma)
{
    SIZE_T Count, Bytes;
    LONG Distance;

    RtlZeroMemory(Kernel, sizeof(*Kernel));
    if (Support <= 0 || Support > 4096 || Sigma <= 0.0)
        return FALSE;

    Count = (SIZE_T)Support * 2 + 1;
    if (Count > (((SIZE_T)-1) / sizeof(*Kernel->Prefix)) - 1)
        return FALSE;
    Bytes = (Count + 1) * sizeof(*Kernel->Prefix);
    Kernel->Prefix = (ULONGLONG *)VirtualAlloc(NULL, Bytes,
                                               MEM_COMMIT | MEM_RESERVE,
                                               PAGE_READWRITE);
    if (Kernel->Prefix == NULL)
        return FALSE;

    Kernel->Support = Support;
    Kernel->Prefix[0] = 0;
    for (Distance = -Support; Distance <= Support; ++Distance)
    {
        double Position = (double)Distance / Sigma;
        ULONGLONG Weight = (ULONGLONG)(exp(-0.5 * Position * Position) *
                                          DWM_SHADOW_WEIGHT_SCALE + 0.5);
        SIZE_T Index = (SIZE_T)(Distance + Support);

        Kernel->Prefix[Index + 1] = Kernel->Prefix[Index] + Weight;
    }
    Kernel->Total = Kernel->Prefix[Count];
    if (Kernel->Total == 0)
    {
        DwmShadowFreeKernel(Kernel);
        return FALSE;
    }
    return TRUE;
}

static BOOL
DwmShadowInit(HDC hdcScreen)
{
    LONG dpi = GetDeviceCaps(hdcScreen, LOGPIXELSX);
    LONG wideExtent, support;
    double wideSigma, activeTightSigma, inactiveTightSigma;
    DWM_SHADOW_KERNEL Wide, ActiveTight, InactiveTight;

    if (dpi <= 0)
        dpi = 96;
    if (g_shadowWide.Prefix != NULL &&
        g_shadowActiveTight.Prefix != NULL &&
        g_shadowInactiveTight.Prefix != NULL &&
        g_shadowDpi == dpi)
        return TRUE;

    wideExtent = MulDiv(DWM_SHADOW_WIDE_EXTENT_96, dpi, 96);
    support = MulDiv(DWM_SHADOW_BOTTOM_MARGIN_96, dpi, 96);
    if (wideExtent < 3)
        wideExtent = 3;
    if (support < wideExtent)
        support = wideExtent;

    wideSigma = (double)wideExtent / 3.0;
    activeTightSigma = (double)wideExtent * 2.0 / 9.0;
    inactiveTightSigma = (double)wideExtent / 9.0;
    if (!DwmShadowBuildKernel(&Wide, support, wideSigma))
        return FALSE;
    if (!DwmShadowBuildKernel(&ActiveTight, support, activeTightSigma))
    {
        DwmShadowFreeKernel(&Wide);
        return FALSE;
    }
    if (!DwmShadowBuildKernel(&InactiveTight, support, inactiveTightSigma))
    {
        DwmShadowFreeKernel(&Wide);
        DwmShadowFreeKernel(&ActiveTight);
        return FALSE;
    }

    DwmShadowFreeKernel(&g_shadowWide);
    DwmShadowFreeKernel(&g_shadowActiveTight);
    DwmShadowFreeKernel(&g_shadowInactiveTight);
    g_shadowWide = Wide;
    g_shadowActiveTight = ActiveTight;
    g_shadowInactiveTight = InactiveTight;
    g_shadowDpi = dpi;
    g_shadowMarginLeft = wideExtent;
    g_shadowMarginTop = MulDiv(DWM_SHADOW_TOP_MARGIN_96, dpi, 96);
    g_shadowMarginRight = wideExtent;
    g_shadowMarginBottom = support;
    g_shadowActiveOffset = MulDiv(
        DWM_SHADOW_ACTIVE_OFFSET_NUMERATOR_96, dpi,
        96 * DWM_SHADOW_OFFSET_DENOMINATOR_96);
    g_shadowInactiveOffset = MulDiv(
        DWM_SHADOW_INACTIVE_OFFSET_NUMERATOR_96, dpi,
        96 * DWM_SHADOW_OFFSET_DENOMINATOR_96);
    return TRUE;
}

static ULONG
DwmShadowCoverage(const DWM_SHADOW_KERNEL *Kernel,
                  LONGLONG Position, LONG Length)
{
    LONGLONG Low, High;
    SIZE_T LowIndex, HighIndex;
    ULONGLONG Sum;

    Low = Position - (LONGLONG)Length + 1;
    High = Position;
    if (Low < -Kernel->Support)
        Low = -Kernel->Support;
    if (High > Kernel->Support)
        High = Kernel->Support;
    if (Low > High)
        return 0;

    LowIndex = (SIZE_T)(Low + Kernel->Support);
    HighIndex = (SIZE_T)(High + Kernel->Support + 1);
    Sum = Kernel->Prefix[HighIndex] - Kernel->Prefix[LowIndex];
    return (ULONG)((Sum * 65535u + Kernel->Total / 2) / Kernel->Total);
}

static ULONG
DwmShadowLayerAlpha(ULONG CoverageX, ULONG CoverageY, ULONG OpacityPermille)
{
    const ULONGLONG Divisor = 1000ull * 65535ull * 65535ull;
    ULONGLONG Numerator;

    Numerator = (ULONGLONG)OpacityPermille * 255u * CoverageX * CoverageY;
    return (ULONG)((Numerator + Divisor / 2) / Divisor);
}

static ULONG *g_blurSource;
static ULONG *g_blurTemp;
static SIZE_T g_blurPixelCount;

static BOOL
DwmEnsureBlurBuffers(LONG Width, LONG Height)
{
    ULONG *Source, *Temp;
    SIZE_T PixelCount, Bytes;

    if (Width <= 0 || Height <= 0 ||
        (SIZE_T)Width > (SIZE_T)-1 / (SIZE_T)Height)
        return FALSE;
    PixelCount = (SIZE_T)Width * (SIZE_T)Height;
    if (PixelCount > (SIZE_T)-1 / sizeof(ULONG))
        return FALSE;
    if (g_blurSource != NULL && g_blurTemp != NULL &&
        g_blurPixelCount == PixelCount)
        return TRUE;

    Bytes = PixelCount * sizeof(ULONG);
    Source = VirtualAlloc(NULL, Bytes, MEM_COMMIT | MEM_RESERVE,
                          PAGE_READWRITE);
    if (Source == NULL)
        return FALSE;
    Temp = VirtualAlloc(NULL, Bytes, MEM_COMMIT | MEM_RESERVE,
                        PAGE_READWRITE);
    if (Temp == NULL)
    {
        VirtualFree(Source, 0, MEM_RELEASE);
        return FALSE;
    }

    if (g_blurSource != NULL)
        VirtualFree(g_blurSource, 0, MEM_RELEASE);
    if (g_blurTemp != NULL)
        VirtualFree(g_blurTemp, 0, MEM_RELEASE);
    g_blurSource = Source;
    g_blurTemp = Temp;
    g_blurPixelCount = PixelCount;
    return TRUE;
}

static LONG
DwmClampCoordinate(LONG Value, LONG Limit)
{
    if (Value < 0)
        return 0;
    if (Value >= Limit)
        return Limit - 1;
    return Value;
}

static void
DwmAddPixel(ULONGLONG *Red, ULONGLONG *Green, ULONGLONG *Blue, ULONG Pixel)
{
    *Red += (Pixel >> 16) & 0xffu;
    *Green += (Pixel >> 8) & 0xffu;
    *Blue += Pixel & 0xffu;
}

static void
DwmSubtractPixel(ULONGLONG *Red, ULONGLONG *Green, ULONGLONG *Blue,
                 ULONG Pixel)
{
    *Red -= (Pixel >> 16) & 0xffu;
    *Green -= (Pixel >> 8) & 0xffu;
    *Blue -= Pixel & 0xffu;
}

static void
DwmBlurRectangle(ULONG *Composition, LONG Width, LONG Height,
                 LONG Left, LONG Top, LONG Right, LONG Bottom, LONG Radius,
                 ULONG Alpha)
{
    const ULONG Divisor = (ULONG)Radius * 2u + 1u;
    const ULONGLONG Reciprocal = (0x1000000ull + Divisor - 1u) / Divisor;
    LONG SampleTop, SampleBottom, x, y, Offset;

    if (Right <= Left || Bottom <= Top || Radius <= 0)
        return;
    SampleTop = Top - Radius;
    if (SampleTop < 0)
        SampleTop = 0;
    SampleBottom = Bottom + Radius;
    if (SampleBottom > Height)
        SampleBottom = Height;

    /* Horizontal box pass over every row needed by the vertical pass. */
    for (y = SampleTop; y < SampleBottom; ++y)
    {
        ULONGLONG Red = 0, Green = 0, Blue = 0;

        for (Offset = -Radius; Offset <= Radius; ++Offset)
        {
            LONG SampleX = DwmClampCoordinate(Left + Offset, Width);
            DwmAddPixel(&Red, &Green, &Blue,
                        g_blurSource[(SIZE_T)y * Width + SampleX]);
        }
        for (x = Left; x < Right; ++x)
        {
            g_blurTemp[(SIZE_T)y * Width + x] =
                0xff000000u |
                ((ULONG)((Red * Reciprocal) >> 24) << 16) |
                ((ULONG)((Green * Reciprocal) >> 24) << 8) |
                (ULONG)((Blue * Reciprocal) >> 24);
            DwmSubtractPixel(
                &Red, &Green, &Blue,
                g_blurSource[(SIZE_T)y * Width +
                    DwmClampCoordinate(x - Radius, Width)]);
            DwmAddPixel(
                &Red, &Green, &Blue,
                g_blurSource[(SIZE_T)y * Width +
                    DwmClampCoordinate(x + Radius + 1, Width)]);
        }
    }

    /* Vertical pass writes only the requested blur region. */
    for (x = Left; x < Right; ++x)
    {
        ULONGLONG Red = 0, Green = 0, Blue = 0;

        for (Offset = -Radius; Offset <= Radius; ++Offset)
        {
            LONG SampleY = DwmClampCoordinate(Top + Offset, Height);
            DwmAddPixel(&Red, &Green, &Blue,
                        g_blurTemp[(SIZE_T)SampleY * Width + x]);
        }
        for (y = Top; y < Bottom; ++y)
        {
            ULONG Blurred =
                0xff000000u |
                ((ULONG)((Red * Reciprocal) >> 24) << 16) |
                ((ULONG)((Green * Reciprocal) >> 24) << 8) |
                (ULONG)((Blue * Reciprocal) >> 24);

            if (Alpha < 255)
            {
                ULONG Base = g_blurSource[(SIZE_T)y * Width + x];
                ULONG Inverse = 255u - Alpha;

                Blurred =
                    0xff000000u |
                    ((((Blurred >> 16) & 0xffu) * Alpha +
                      ((Base >> 16) & 0xffu) * Inverse) / 255u << 16) |
                    ((((Blurred >> 8) & 0xffu) * Alpha +
                      ((Base >> 8) & 0xffu) * Inverse) / 255u << 8) |
                    (((Blurred & 0xffu) * Alpha +
                      (Base & 0xffu) * Inverse) / 255u);
            }
            Composition[(SIZE_T)y * Width + x] = Blurred;
            DwmSubtractPixel(
                &Red, &Green, &Blue,
                g_blurTemp[(SIZE_T)DwmClampCoordinate(y - Radius, Height) *
                           Width + x]);
            DwmAddPixel(
                &Red, &Green, &Blue,
                g_blurTemp[(SIZE_T)DwmClampCoordinate(y + Radius + 1,
                                                      Height) * Width + x]);
        }
    }
}

static void
DwmApplyBlur(ULONG *Composition, LONG Width, LONG Height,
             LONG ClipLeft, LONG ClipTop, LONG ClipRight, LONG ClipBottom,
             const DWM_WIN *Window, const RECTL *Rectangles)
{
    RECTL Entire = {0, 0, Window->cx, Window->cy};
    const RECTL *Rectangle;
    ULONG Index, Count, Alpha = 255;
    LONG Radius, WindowX, WindowY, SampleTop, SampleBottom;
    LONG SampleLeft, SampleRight, SampleRow;
    SIZE_T Bytes;

    if (!(Window->BlurFlags & DWM_BLUR_ENABLE) ||
        Window->cx <= 0 || Window->cy <= 0)
        return;
    if (Window->LayerFlags & DWM_LWA_ALPHA)
        Alpha = Window->Alpha;
    if (Alpha == 0)
        return;
    if (Window->BlurFlags & DWM_BLUR_REGION_ENTIRE_WINDOW)
    {
        Rectangle = &Entire;
        Count = 1;
    }
    else
    {
        Rectangle = Rectangles;
        Count = Window->BlurRectCount;
        if (Rectangle == NULL || Count == 0)
            return;
    }
    if (!DwmEnsureBlurBuffers(Width, Height))
        return;

    Radius = MulDiv(12, g_shadowDpi > 0 ? g_shadowDpi : 96, 96);
    if (Radius < 1)
        Radius = 1;
    if (Radius > 64)
        Radius = 64;
    WindowX = Window->x - g_originX;
    WindowY = Window->y - g_originY;

    SampleTop = (WindowY < ClipTop) ? ClipTop : WindowY;
    SampleTop -= Radius;
    if (SampleTop < 0)
        SampleTop = 0;
    SampleBottom = WindowY + Window->cy;
    if (SampleBottom > ClipBottom)
        SampleBottom = ClipBottom;
    SampleBottom += Radius;
    if (SampleBottom > Height)
        SampleBottom = Height;
    if (SampleBottom <= SampleTop)
        return;

    SampleLeft = (WindowX < ClipLeft) ? ClipLeft : WindowX;
    SampleLeft -= Radius;
    if (SampleLeft < 0)
        SampleLeft = 0;
    SampleRight = WindowX + Window->cx;
    if (SampleRight > ClipRight)
        SampleRight = ClipRight;
    SampleRight += Radius + 1;
    if (SampleRight > Width)
        SampleRight = Width;
    if (SampleRight <= SampleLeft)
        return;

    Bytes = (SIZE_T)(SampleRight - SampleLeft) * sizeof(ULONG);
    for (SampleRow = SampleTop; SampleRow < SampleBottom; ++SampleRow)
    {
        SIZE_T Offset = (SIZE_T)SampleRow * Width + SampleLeft;

        RtlCopyMemory(g_blurSource + Offset, Composition + Offset, Bytes);
    }

    for (Index = 0; Index < Count; ++Index)
    {
        LONG Left = WindowX + Rectangle[Index].left;
        LONG Top = WindowY + Rectangle[Index].top;
        LONG Right = WindowX + Rectangle[Index].right;
        LONG Bottom = WindowY + Rectangle[Index].bottom;
        LONG WindowRight = WindowX + Window->cx;
        LONG WindowBottom = WindowY + Window->cy;

        if (Left < WindowX) Left = WindowX;
        if (Top < WindowY) Top = WindowY;
        if (Right > WindowRight) Right = WindowRight;
        if (Bottom > WindowBottom) Bottom = WindowBottom;
        if (Left < ClipLeft) Left = ClipLeft;
        if (Top < ClipTop) Top = ClipTop;
        if (Right > ClipRight) Right = ClipRight;
        if (Bottom > ClipBottom) Bottom = ClipBottom;
        if (Left < 0) Left = 0;
        if (Top < 0) Top = 0;
        if (Right > Width) Right = Width;
        if (Bottom > Height) Bottom = Height;
        DwmBlurRectangle(Composition, Width, Height,
                         Left, Top, Right, Bottom, Radius, Alpha);
    }
}

static BOOL
DwmWindowIsOpaque(const DWM_WIN *Window)
{
    if (Window->cx <= 0 || Window->cy <= 0)
        return FALSE;
    if (Window->AnimFlags != 0)
        return FALSE;
    if (Window->LayerFlags & DWM_LWA_COLORKEY)
        return FALSE;
    if ((Window->LayerFlags & DWM_LWA_ALPHA) && Window->Alpha < 255)
        return FALSE;
    if (Window->BlurFlags & DWM_BLUR_ENABLE)
        return FALSE;
    if (Window->BackdropType >= DWM_BACKDROP_MAIN &&
        Window->BackdropType <= DWM_BACKDROP_TABBED &&
        Window->BackdropRegion != 0)
        return FALSE;
    return TRUE;
}

static BOOL
DwmWindowIsHidden(const DWM_WIN *Windows, ULONG Count, ULONG Index,
                  LONG ClipLeft, LONG ClipTop, LONG ClipRight, LONG ClipBottom)
{
    const DWM_WIN *Window = &Windows[Index];
    LONGLONG Left = (LONGLONG)Window->x - g_originX;
    LONGLONG Top = (LONGLONG)Window->y - g_originY;
    LONGLONG Right = Left + Window->cx;
    LONGLONG Bottom = Top + Window->cy;
    ULONG Above;

    if (Window->AnimFlags != 0)
    {
        Left = (LONGLONG)Window->AnimX - g_originX;
        Top = (LONGLONG)Window->AnimY - g_originY;
        Right = Left + Window->AnimCx;
        Bottom = Top + Window->AnimCy;
    }
    else if (Window->LayerFlags & DWM_WINDOW_NC_SHADOW)
    {
        Left -= g_shadowMarginLeft;
        Top -= g_shadowMarginTop;
        Right += g_shadowMarginRight;
        Bottom += g_shadowMarginBottom;
    }
    if (Left < ClipLeft) Left = ClipLeft;
    if (Top < ClipTop) Top = ClipTop;
    if (Right > ClipRight) Right = ClipRight;
    if (Bottom > ClipBottom) Bottom = ClipBottom;
    if (Right <= Left || Bottom <= Top)
        return TRUE;

    for (Above = Index + 1; Above < Count; ++Above)
    {
        const DWM_WIN *Cover = &Windows[Above];
        LONGLONG CoverLeft, CoverTop, CoverRight, CoverBottom;

        if (!DwmWindowIsOpaque(Cover))
            continue;
        CoverLeft = (LONGLONG)Cover->x - g_originX;
        CoverTop = (LONGLONG)Cover->y - g_originY;
        CoverRight = CoverLeft + Cover->cx;
        CoverBottom = CoverTop + Cover->cy;
        if (CoverLeft <= Left && CoverTop <= Top &&
            CoverRight >= Right && CoverBottom >= Bottom)
            return TRUE;
    }
    return FALSE;
}

static BOOL
DwmWindowBlursBackdrop(const DWM_WIN *Window)
{
    if (Window->AnimFlags != 0)
        return FALSE;
    if (Window->BlurFlags & DWM_BLUR_ENABLE)
        return TRUE;
    return Window->BackdropType == DWM_BACKDROP_TRANSIENT &&
           Window->BackdropRegion != 0;
}

static void
DwmApplyBackdropBlur(ULONG *Composition, LONG Width, LONG Height,
                     LONG ClipLeft, LONG ClipTop, LONG ClipRight,
                     LONG ClipBottom, const DWM_WIN *Window)
{
    DWM_WIN BlurWindow;
    RECTL Rectangles[4];

    if (Window->BackdropType != DWM_BACKDROP_TRANSIENT)
        return;

    BlurWindow = *Window;
    BlurWindow.BlurFlags = DWM_BLUR_ENABLE;
    if (Window->BackdropRegion == DWM_BACKDROP_REGION_WINDOW)
    {
        BlurWindow.BlurFlags |= DWM_BLUR_REGION_ENTIRE_WINDOW;
        BlurWindow.BlurRectCount = 0;
        DwmApplyBlur(Composition, Width, Height,
                     ClipLeft, ClipTop, ClipRight, ClipBottom,
                     &BlurWindow, NULL);
        return;
    }
    if (Window->BackdropRegion != DWM_BACKDROP_REGION_NONCLIENT)
        return;

    Rectangles[0] = (RECTL){0, 0, Window->cx, Window->ClientY};
    Rectangles[1] = (RECTL){0, Window->ClientY + Window->ClientHeight,
                            Window->cx, Window->cy};
    Rectangles[2] = (RECTL){0, Window->ClientY, Window->ClientX,
                            Window->ClientY + Window->ClientHeight};
    Rectangles[3] = (RECTL){Window->ClientX + Window->ClientWidth,
                            Window->ClientY, Window->cx,
                            Window->ClientY + Window->ClientHeight};
    BlurWindow.BlurRectCount = ARRAYSIZE(Rectangles);
    DwmApplyBlur(Composition, Width, Height,
                 ClipLeft, ClipTop, ClipRight, ClipBottom,
                 &BlurWindow, Rectangles);
}

static void
DwmBlendShadowSpan(ULONG *Row, LONG X0, LONG X1,
                   ULONG WideY, ULONG TightY,
                   ULONG WideOpacity, ULONG TightOpacity, ULONG WindowAlpha)
{
    LONG x;

    for (x = X0; x < X1; ++x)
    {
        ULONG WideAlpha = DwmShadowLayerAlpha(g_shadowCoverWide[x], WideY,
                                              WideOpacity);
        ULONG TightAlpha = DwmShadowLayerAlpha(g_shadowCoverTight[x], TightY,
                                               TightOpacity);
        ULONG Alpha = WideAlpha + TightAlpha - (WideAlpha * TightAlpha) / 255u;
        ULONG Inverse, Pixel;

        if (WindowAlpha != 255)
            Alpha = (Alpha * WindowAlpha) / 255u;
        if (Alpha == 0)
            continue;

        Inverse = 255u - Alpha;
        Pixel = Row[x];
        Row[x] = ((((Pixel >> 16) & 0xFFu) * Inverse / 255u) << 16) |
                 ((((Pixel >> 8) & 0xFFu) * Inverse / 255u) << 8) |
                 ((Pixel & 0xFFu) * Inverse / 255u);
    }
}

static void
DwmBlendShadow(ULONG *comp, LONG scrW, LONG scrH,
               LONG clipL, LONG clipT, LONG clipR, LONG clipB,
               const DWM_WIN *w)
{
    MONITORINFO monitorInfo;
    HMONITOR monitor;
    RECT ownerRect;
    LONGLONG ownerX = (LONGLONG)w->x - g_originX;
    LONGLONG ownerY = (LONGLONG)w->y - g_originY;
    LONGLONG ownerRight = ownerX + w->cx;
    LONGLONG ownerBottom = ownerY + w->cy;
    LONGLONG shadowLeft = ownerX - g_shadowMarginLeft;
    LONGLONG shadowTop = ownerY - g_shadowMarginTop;
    LONGLONG shadowRight = ownerRight + g_shadowMarginRight;
    LONGLONG shadowBottom = ownerBottom + g_shadowMarginBottom;
    LONG y0, y1, x0, x1, x, y;
    ULONG wideOpacity, tightOpacity, windowAlpha = 255;
    const DWM_SHADOW_KERNEL *tightKernel;
    LONG verticalOffset;
    BOOL active, dark;

    if (g_shadowWide.Prefix == NULL ||
        g_shadowActiveTight.Prefix == NULL ||
        g_shadowInactiveTight.Prefix == NULL ||
        w->cx <= 0 || w->cy <= 0 ||
        !(w->LayerFlags & DWM_WINDOW_NC_SHADOW))
        return;

    ownerRect.left = w->x;
    ownerRect.top = w->y;
    ownerRect.right = w->x + w->cx;
    ownerRect.bottom = w->y + w->cy;
    monitor = MonitorFromRect(&ownerRect, MONITOR_DEFAULTTONEAREST);
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (monitor != NULL && GetMonitorInfoW(monitor, &monitorInfo))
    {
        LONGLONG workLeft = (LONGLONG)monitorInfo.rcWork.left - g_originX;
        LONGLONG workTop = (LONGLONG)monitorInfo.rcWork.top - g_originY;
        LONGLONG workRight = (LONGLONG)monitorInfo.rcWork.right - g_originX;
        LONGLONG workBottom = (LONGLONG)monitorInfo.rcWork.bottom - g_originY;

        /* Appbars occupy the monitor area excluded from rcWork. They are
         * shell layers, not ordinary framed windows, and must not acquire an
         * active/inactive non-client shadow when their focus state changes. */
        if ((monitorInfo.rcWork.left > monitorInfo.rcMonitor.left &&
             ownerRect.right <= monitorInfo.rcWork.left) ||
            (monitorInfo.rcWork.top > monitorInfo.rcMonitor.top &&
             ownerRect.bottom <= monitorInfo.rcWork.top) ||
            (monitorInfo.rcWork.right < monitorInfo.rcMonitor.right &&
             ownerRect.left >= monitorInfo.rcWork.right) ||
            (monitorInfo.rcWork.bottom < monitorInfo.rcMonitor.bottom &&
             ownerRect.top >= monitorInfo.rcWork.bottom))
        {
            return;
        }

        if (monitorInfo.rcWork.left > monitorInfo.rcMonitor.left &&
            shadowLeft < workLeft)
            shadowLeft = workLeft;
        if (monitorInfo.rcWork.top > monitorInfo.rcMonitor.top &&
            shadowTop < workTop)
            shadowTop = workTop;
        if (monitorInfo.rcWork.right < monitorInfo.rcMonitor.right &&
            shadowRight > workRight)
            shadowRight = workRight;
        if (monitorInfo.rcWork.bottom < monitorInfo.rcMonitor.bottom &&
            shadowBottom > workBottom)
            shadowBottom = workBottom;
    }

    if (shadowRight <= clipL || shadowLeft >= clipR ||
        shadowBottom <= clipT || shadowTop >= clipB)
        return;

    x0 = (shadowLeft < clipL) ? clipL : (LONG)shadowLeft;
    x1 = (shadowRight > clipR) ? clipR : (LONG)shadowRight;
    y0 = (shadowTop < clipT) ? clipT : (LONG)shadowTop;
    y1 = (shadowBottom > clipB) ? clipB : (LONG)shadowBottom;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > scrW) x1 = scrW;
    if (y1 > scrH) y1 = scrH;
    if (x1 <= x0 || y1 <= y0)
        return;

    active = (w->LayerFlags & DWM_WINDOW_ACTIVE) != 0;
    dark = (w->LayerFlags & DWM_WINDOW_DARK) != 0;
    tightKernel = active ? &g_shadowActiveTight : &g_shadowInactiveTight;
    verticalOffset = active ? g_shadowActiveOffset : g_shadowInactiveOffset;
    if (dark)
    {
        if (active)
        {
            wideOpacity = DWM_SHADOW_DARK_ACTIVE_WIDE_OPACITY;
            tightOpacity = DWM_SHADOW_DARK_ACTIVE_TIGHT_OPACITY;
        }
        else
        {
            wideOpacity = DWM_SHADOW_DARK_INACTIVE_OPACITY;
            tightOpacity = 0;
        }
    }
    else if (active)
    {
        wideOpacity = DWM_SHADOW_ACTIVE_WIDE_OPACITY;
        tightOpacity = DWM_SHADOW_ACTIVE_TIGHT_OPACITY;
    }
    else
    {
        wideOpacity = DWM_SHADOW_INACTIVE_WIDE_OPACITY;
        tightOpacity = DWM_SHADOW_INACTIVE_TIGHT_OPACITY;
    }
    if (w->LayerFlags & DWM_LWA_ALPHA)
        windowAlpha = w->Alpha;

    if (!DwmEnsureShadowCoverage(scrW))
        return;
    for (x = x0; x < x1; ++x)
    {
        LONGLONG Position = (LONGLONG)x - ownerX;

        g_shadowCoverWide[x] = DwmShadowCoverage(&g_shadowWide, Position,
                                                 w->cx);
        g_shadowCoverTight[x] = DwmShadowCoverage(tightKernel, Position,
                                                  w->cx);
    }

    for (y = y0; y < y1; y++)
    {
        ULONG *row = comp + (SIZE_T)y * scrW;
        LONGLONG PositionY = (LONGLONG)y - ownerY - verticalOffset;
        ULONG WideY = DwmShadowCoverage(&g_shadowWide, PositionY, w->cy);
        ULONG TightY = DwmShadowCoverage(tightKernel, PositionY, w->cy);

        if ((LONGLONG)y >= ownerY && (LONGLONG)y < ownerBottom)
        {
            LONG leftEnd = (ownerX <= x0) ? x0 :
                           (ownerX >= x1) ? x1 : (LONG)ownerX;
            LONG rightStart = (ownerRight <= x0) ? x0 :
                              (ownerRight >= x1) ? x1 : (LONG)ownerRight;

            DwmBlendShadowSpan(row, x0, leftEnd,
                               WideY, TightY, wideOpacity, tightOpacity,
                               windowAlpha);
            DwmBlendShadowSpan(row, rightStart, x1,
                               WideY, TightY, wideOpacity, tightOpacity,
                               windowAlpha);
        }
        else
        {
            DwmBlendShadowSpan(row, x0, x1,
                               WideY, TightY, wideOpacity, tightOpacity,
                               windowAlpha);
        }
    }
}

static void
DwmBlitWindow(ULONG *comp, LONG scrW,
              LONG clipL, LONG clipT, LONG clipR, LONG clipB,
              const BYTE *pix, const ULONG *wallpaper, const DWM_WIN *w)
{
    LONG r, r0, r1, x0, x1, srcx0, dy, x, width;
    LONGLONG wx = (LONGLONG)w->x - g_originX;
    LONGLONG wy = (LONGLONG)w->y - g_originY;
    LONGLONG right, bottom;
    BOOL useKey = (w->LayerFlags & DWM_LWA_COLORKEY) != 0;
    BOOL useAlpha = (w->LayerFlags & DWM_LWA_ALPHA) != 0 && w->Alpha < 255;
    BOOL usePixelAlpha = (w->BlurFlags & DWM_BLUR_ENABLE) != 0;
    BOOL useBackdrop = w->BackdropType >= DWM_BACKDROP_MAIN &&
                       w->BackdropType <= DWM_BACKDROP_TABBED &&
                       w->BackdropRegion != 0;
    ULONG a = w->Alpha, key = 0, backdropKey = 0, colorizationKey = 0;

    if (w->cx <= 0 || w->cy <= 0 ||
        (ULONG)w->cx > ((ULONG)-1) / sizeof(ULONG) ||
        w->Stride < (ULONG)w->cx * sizeof(ULONG))
        return;

    right = wx + w->cx;
    bottom = wy + w->cy;
    if (right <= clipL || wx >= clipR || bottom <= clipT || wy >= clipB)
        return;
    x0 = (wx < clipL) ? clipL : (LONG)wx;
    x1 = (right > clipR) ? clipR : (LONG)right;
    if (x1 <= x0) return;
    srcx0 = (LONG)(x0 - wx);
    width = x1 - x0;

    r0 = (wy < clipT) ? (LONG)(clipT - wy) : 0;
    r1 = (bottom > clipB) ? (LONG)(clipB - wy) : w->cy;
    if (r1 <= r0) return;

    if (useKey)
    {
        ULONG c = w->ColorKey;
        key = ((c & 0xFFu) << 16) | (c & 0xFF00u) | ((c >> 16) & 0xFFu);
    }
    if (useBackdrop)
    {
        ULONG c = w->BackdropColor;
        backdropKey = ((c & 0xFFu) << 16) |
                      (c & 0xFF00u) | ((c >> 16) & 0xFFu);
        c = w->BackdropColorization;
        colorizationKey = ((c & 0xFFu) << 16) |
                          (c & 0xFF00u) | ((c >> 16) & 0xFFu);
    }

    for (r = r0; r < r1; r++)
    {
        const ULONG *srcrow;
        const ULONG *wallpaperrow = NULL;
        ULONG *dstrow;

        dy = (LONG)(wy + r);
        srcrow = (const ULONG *)(pix + (SIZE_T)r * w->Stride) + srcx0;
        dstrow = comp + (SIZE_T)dy * scrW + x0;
        if (wallpaper != NULL)
            wallpaperrow = wallpaper + (SIZE_T)dy * scrW + x0;

        if (!useKey && !useAlpha && !usePixelAlpha && !useBackdrop)
        {
            RtlCopyMemory(dstrow, srcrow, (SIZE_T)width * 4);
            continue;
        }

        for (x = 0; x < width; x++)
        {
            ULONG s = srcrow[x], d, pixelAlpha = 255;
            LONG sourceX = srcx0 + x;
            BOOL materialPixel = FALSE;

            if (useKey && (s & 0x00FFFFFFu) == key)
                continue;
            if (useBackdrop &&
                ((s & 0x00FFFFFFu) == backdropKey ||
                 (s & 0x00FFFFFFu) == colorizationKey))
            {
                if (w->BackdropRegion == DWM_BACKDROP_REGION_WINDOW ||
                    sourceX < w->ClientX ||
                    sourceX >= w->ClientX + w->ClientWidth ||
                    r < w->ClientY ||
                    r >= w->ClientY + w->ClientHeight)
                {
                    materialPixel = TRUE;
                }
            }
            if (usePixelAlpha)
                pixelAlpha = (s >> 24) & 0xffu;

            d = dstrow[x];
            if (materialPixel &&
                (w->BackdropType == DWM_BACKDROP_MAIN ||
                 w->BackdropType == DWM_BACKDROP_TABBED) &&
                wallpaperrow != NULL)
            {
                ULONG base = wallpaperrow[x];
                ULONG opacity = w->BackdropOpacity;
                ULONG inverse = 255u - opacity;

                s = ((((s >> 16) & 0xFFu) * opacity +
                      ((base >> 16) & 0xFFu) * inverse) / 255u << 16) |
                    ((((s >> 8) & 0xFFu) * opacity +
                      ((base >> 8) & 0xFFu) * inverse) / 255u << 8) |
                    (((s & 0xFFu) * opacity +
                      (base & 0xFFu) * inverse) / 255u);
            }
            else if (materialPixel)
            {
                pixelAlpha = pixelAlpha * w->BackdropOpacity / 255u;
            }
            if (useAlpha)
                pixelAlpha = pixelAlpha * a / 255u;
            if (pixelAlpha == 0)
                continue;
            if (pixelAlpha == 255)
            {
                dstrow[x] = s & 0x00ffffffu;
                continue;
            }
            {
                ULONG inverse = 255u - pixelAlpha;
                dstrow[x] =
                    ((((s >> 16) & 0xFFu) * pixelAlpha +
                      ((d >> 16) & 0xFFu) * inverse) / 255u << 16) |
                    ((((s >> 8) & 0xFFu) * pixelAlpha +
                      ((d >> 8) & 0xFFu) * inverse) / 255u << 8) |
                    (((s & 0xFFu) * pixelAlpha +
                      (d & 0xFFu) * inverse) / 255u);
            }
        }
    }
}

#define DWM_ANIM_TAPS 4

static void
DwmBlitScaled(ULONG *comp, LONG scrW,
              LONG clipL, LONG clipT, LONG clipR, LONG clipB,
              const BYTE *pix, LONG srcCx, LONG srcCy, ULONG srcStride,
              LONG dstX, LONG dstY, LONG dstCx, LONG dstCy, ULONG alpha,
              const DWM_WIN *material)
{
    LONG y, x, y0, y1, x0, x1;
    ULONG matKey = 0xFFFFFFFFu, matKey2 = 0xFFFFFFFFu, matOpacity = 255;
    BOOL matWholeWindow = FALSE;
    LONG matCx0 = 0, matCy0 = 0, matCx1 = 0, matCy1 = 0;

    if (material != NULL &&
        material->BackdropType >= DWM_BACKDROP_MAIN &&
        material->BackdropType <= DWM_BACKDROP_TABBED &&
        material->BackdropRegion != 0)
    {
        ULONG c = material->BackdropColor;

        matWholeWindow =
            (material->BackdropRegion == DWM_BACKDROP_REGION_WINDOW);
        matCx0 = material->ClientX;
        matCy0 = material->ClientY;
        matCx1 = material->ClientX + material->ClientWidth;
        matCy1 = material->ClientY + material->ClientHeight;

        matKey = ((c & 0xFFu) << 16) | (c & 0xFF00u) | ((c >> 16) & 0xFFu);
        c = material->BackdropColorization;
        matKey2 = ((c & 0xFFu) << 16) | (c & 0xFF00u) | ((c >> 16) & 0xFFu);
        matOpacity = material->BackdropOpacity;
    }

    if (pix == NULL || srcCx <= 0 || srcCy <= 0 || dstCx <= 0 || dstCy <= 0 ||
        alpha == 0)
        return;
    if ((ULONG)srcCx > ((ULONG)-1) / sizeof(ULONG) ||
        srcStride < (ULONG)srcCx * sizeof(ULONG))
        return;

    y0 = (dstY < clipT) ? clipT : dstY;
    y1 = (dstY + dstCy > clipB) ? clipB : dstY + dstCy;
    x0 = (dstX < clipL) ? clipL : dstX;
    x1 = (dstX + dstCx > clipR) ? clipR : dstX + dstCx;
    if (y1 <= y0 || x1 <= x0)
        return;

    for (y = y0; y < y1; y++)
    {
        ULONG *dstrow = comp + (SIZE_T)y * scrW;
        LONG sy0 = (LONG)(((LONGLONG)(y - dstY) * srcCy) / dstCy);
        LONG sy1 = (LONG)(((LONGLONG)(y - dstY + 1) * srcCy) / dstCy);
        LONG stepY;

        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > srcCy) sy1 = srcCy;
        stepY = (sy1 - sy0 + DWM_ANIM_TAPS - 1) / DWM_ANIM_TAPS;
        if (stepY < 1) stepY = 1;

        for (x = x0; x < x1; x++)
        {
            LONG sx0 = (LONG)(((LONGLONG)(x - dstX) * srcCx) / dstCx);
            LONG sx1 = (LONG)(((LONGLONG)(x - dstX + 1) * srcCx) / dstCx);
            LONG stepX, sy, sx;
            ULONG taps = 0, sr = 0, sg = 0, sb = 0, s, d, inverse;

            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > srcCx) sx1 = srcCx;
            stepX = (sx1 - sx0 + DWM_ANIM_TAPS - 1) / DWM_ANIM_TAPS;
            if (stepX < 1) stepX = 1;

            for (sy = sy0; sy < sy1; sy += stepY)
            {
                const ULONG *srcrow =
                    (const ULONG *)(pix + (SIZE_T)sy * srcStride);

                for (sx = sx0; sx < sx1; sx += stepX)
                {
                    ULONG c = srcrow[sx];

                    sr += (c >> 16) & 0xFFu;
                    sg += (c >> 8) & 0xFFu;
                    sb += c & 0xFFu;
                    taps++;
                }
            }
            if (taps == 0)
                continue;
            s = ((sr / taps) << 16) | ((sg / taps) << 8) | (sb / taps);
            if (matKey != 0xFFFFFFFFu &&
                ((s & 0x00FFFFFFu) == matKey ||
                 (s & 0x00FFFFFFu) == matKey2) &&
                (matWholeWindow ||
                 sx0 < matCx0 || sx0 >= matCx1 ||
                 sy0 < matCy0 || sy0 >= matCy1))
            {
                ULONG ma = matOpacity * alpha / 255u;
                ULONG mi = 255u - ma;

                d = dstrow[x];
                dstrow[x] =
                    ((((s >> 16) & 0xFFu) * ma +
                      ((d >> 16) & 0xFFu) * mi) / 255u << 16) |
                    ((((s >> 8) & 0xFFu) * ma +
                      ((d >> 8) & 0xFFu) * mi) / 255u << 8) |
                    (((s & 0xFFu) * ma + (d & 0xFFu) * mi) / 255u);
                continue;
            }
            if (alpha >= 255)
            {
                dstrow[x] = s;
                continue;
            }
            d = dstrow[x];
            inverse = 255u - alpha;
            dstrow[x] =
                ((((s >> 16) & 0xFFu) * alpha +
                  ((d >> 16) & 0xFFu) * inverse) / 255u << 16) |
                ((((s >> 8) & 0xFFu) * alpha +
                  ((d >> 8) & 0xFFu) * inverse) / 255u << 8) |
                (((s & 0xFFu) * alpha + (d & 0xFFu) * inverse) / 255u);
        }
    }
}

static void
DwmBlitWindowAnimated(ULONG *comp, LONG scrW,
                      LONG clipL, LONG clipT, LONG clipR, LONG clipB,
                      const BYTE *pix, const BYTE *dxpix, const DWM_WIN *w)
{
    LONG dstX = w->AnimX - g_originX;
    LONG dstY = w->AnimY - g_originY;
    ULONG alpha = 255;

    if (w->cx <= 0 || w->cy <= 0 || w->AnimCx <= 0 || w->AnimCy <= 0)
        return;
    if ((w->LayerFlags & DWM_LWA_ALPHA) && w->Alpha < 255)
        alpha = w->Alpha;

    DwmBlitScaled(comp, scrW, clipL, clipT, clipR, clipB,
                  pix, w->cx, w->cy, w->Stride,
                  dstX, dstY, w->AnimCx, w->AnimCy, alpha, w);

    if (dxpix != NULL && w->DxWidth != 0 && w->DxHeight != 0)
    {
        LONG cx = (LONG)((LONGLONG)w->DxWidth * w->AnimCx / w->cx);
        LONG cy = (LONG)((LONGLONG)w->DxHeight * w->AnimCy / w->cy);

        if (cx < 1) cx = 1;
        if (cy < 1) cy = 1;
        DwmBlitScaled(comp, scrW, clipL, clipT, clipR, clipB,
                      dxpix, (LONG)w->DxWidth, (LONG)w->DxHeight, w->DxPitch,
                      dstX + (LONG)((LONGLONG)w->DxClientX * w->AnimCx / w->cx),
                      dstY + (LONG)((LONGLONG)w->DxClientY * w->AnimCy / w->cy),
                      cx, cy, alpha, NULL);
    }
}

static HDC     g_hdcComp;
static HBITMAP g_hbmComp;
static void   *g_compBits;
static ULONGLONG g_qpcPerSecond;
static ULONGLONG g_refreshPeriodQpc;
static ULONGLONG g_lastPresentQpc;
static HDC     g_hdcBackdrop;
static HBITMAP g_hbmBackdrop;
static void   *g_backdropBits;
static BYTE   *g_buf;
static ULONG   g_bufSize;
static LONG    g_W, g_H;

static BOOL
DwmCreateSurfaces(HDC hdcScreen, LONG W, LONG H)
{
    BITMAPINFO bmi;
    HDC hdcNew;
    HDC hdcBackdropNew;
    HBITMAP hbmNew;
    HBITMAP hbmBackdropNew;
    void *bitsNew = NULL;
    void *backdropBitsNew = NULL;

    if (W <= 0 || H <= 0 || (ULONG)W > ((ULONG)-1) / sizeof(ULONG) ||
        (ULONG)H > ((ULONG)-1) / ((ULONG)W * sizeof(ULONG)))
        return FALSE;

    hdcNew = CreateCompatibleDC(hdcScreen);
    if (hdcNew == NULL)
        return FALSE;

    RtlZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    hbmNew = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bitsNew, NULL, 0);
    if (hbmNew == NULL || bitsNew == NULL)
    {
        if (hbmNew != NULL)
            DeleteObject(hbmNew);
        DeleteDC(hdcNew);
        return FALSE;
    }
    if (SelectObject(hdcNew, hbmNew) == NULL)
    {
        DeleteObject(hbmNew);
        DeleteDC(hdcNew);
        return FALSE;
    }

    hdcBackdropNew = CreateCompatibleDC(hdcScreen);
    if (hdcBackdropNew == NULL)
    {
        DeleteDC(hdcNew);
        DeleteObject(hbmNew);
        return FALSE;
    }

    hbmBackdropNew = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS,
                                      &backdropBitsNew, NULL, 0);
    if (hbmBackdropNew == NULL || backdropBitsNew == NULL)
    {
        if (hbmBackdropNew != NULL)
            DeleteObject(hbmBackdropNew);
        DeleteDC(hdcBackdropNew);
        DeleteDC(hdcNew);
        DeleteObject(hbmNew);
        return FALSE;
    }
    if (SelectObject(hdcBackdropNew, hbmBackdropNew) == NULL)
    {
        DeleteObject(hbmBackdropNew);
        DeleteDC(hdcBackdropNew);
        DeleteDC(hdcNew);
        DeleteObject(hbmNew);
        return FALSE;
    }

    if (g_buf == NULL)
    {
        g_bufSize = DWM_FRAME_BYTES;
        g_buf = (BYTE *)VirtualAlloc(NULL, g_bufSize, MEM_COMMIT | MEM_RESERVE,
                                     PAGE_READWRITE);
        if (g_buf == NULL)
        {
            DeleteDC(hdcBackdropNew);
            DeleteObject(hbmBackdropNew);
            DeleteDC(hdcNew);
            DeleteObject(hbmNew);
            return FALSE;
        }
    }

    if (g_hdcComp != NULL)
        DeleteDC(g_hdcComp);
    if (g_hbmComp != NULL)
        DeleteObject(g_hbmComp);
    if (g_hdcBackdrop != NULL)
        DeleteDC(g_hdcBackdrop);
    if (g_hbmBackdrop != NULL)
        DeleteObject(g_hbmBackdrop);
    g_hdcComp = hdcNew;
    g_hbmComp = hbmNew;
    g_compBits = bitsNew;
    g_hdcBackdrop = hdcBackdropNew;
    g_hbmBackdrop = hbmBackdropNew;
    g_backdropBits = backdropBitsNew;

    g_W = W;
    g_H = H;
    return TRUE;
}

static void
DwmComposeLoop(HANDLE hStopEvent)
{
    HDC hdcScreen = GetDC(NULL);
    DWM_ATTACH att;
    HANDLE hWake;
    BOOL forceFull = TRUE;
    LONG vw, vh, primW, primH;

    if (hdcScreen == NULL)
    {
        DwmLog("DWM: screen DC unavailable\n");
        return;
    }

    primW = GetSystemMetrics(SM_CXSCREEN);
    primH = GetSystemMetrics(SM_CYSCREEN);
    vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    g_originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0)
    {
        vw = (primW > 0) ? primW : 800;
        vh = (primH > 0) ? primH : 600;
        g_originX = g_originY = 0;
    }

    if (!DwmCreateSurfaces(hdcScreen, vw, vh))
    {
        DwmLog("DWM: surface creation failed\n");
        return;
    }
    DwmShadowInit(hdcScreen);

    {
        LARGE_INTEGER Frequency;
        LONG Hz = GetDeviceCaps(hdcScreen, VREFRESH);

        if (QueryPerformanceFrequency(&Frequency) && Frequency.QuadPart > 0)
        {
            g_qpcPerSecond = (ULONGLONG)Frequency.QuadPart;
            if (Hz < 24 || Hz > 480)
                Hz = 60;
            g_refreshPeriodQpc = g_qpcPerSecond / (ULONGLONG)Hz;
        }
    }

    RtlZeroMemory(&att, sizeof(att));
    att.Attach = 1;
    if ((LONG)NtUserCallOneParam((DWORD_PTR)&att, DWM_ROUTINE_ATTACH) < 0)
    {
        DwmLog("DWM: attach refused\n");
        return;
    }
    hWake = att.hWake;
    if (att.hVblank != NULL)
        CloseHandle(att.hVblank);
    if (hWake == NULL)
    {
        DwmLog("DWM: attach refused (no composition on this display stack)\n");
        return;
    }
    DwmLog("DWM: attached\n");

    for (;;)
    {
        PDWM_FRAME_HEADER hdr = (PDWM_FRAME_HEADER)g_buf;
        PDWM_WIN wins;
        PRECTL blurRects;
        LONG st;
        ULONG i;

        if (WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0)
            break;

        if (g_refreshPeriodQpc != 0 && g_lastPresentQpc != 0)
        {
            LARGE_INTEGER Counter;
            ULONGLONG Elapsed;
            QueryPerformanceCounter(&Counter);
            Elapsed = (ULONGLONG)Counter.QuadPart - g_lastPresentQpc;
            if (Elapsed < g_refreshPeriodQpc)
            {
                DWORD WaitMs = (DWORD)(((g_refreshPeriodQpc - Elapsed) *
                                        1000ull) / g_qpcPerSecond);

                if (WaitMs != 0 &&
                    WaitForSingleObject(hStopEvent, WaitMs) == WAIT_OBJECT_0)
                    break;
            }
        }

        hdr->Magic = DWM_FRAME_MAGIC;
        hdr->BufBytes = g_bufSize;

        st = (LONG)NtUserCallOneParam((DWORD_PTR)g_buf, DWM_ROUTINE_GETFRAME);
        if (st < 0)
        {
            Sleep(50);
            continue;
        }

        if (hdr->Magic != DWM_FRAME_MAGIC ||
            hdr->WinArrayBase != DWM_WINARRAY_BASE ||
            hdr->BlurRectArrayBase != DWM_BLURRECTARRAY_BASE ||
            hdr->Count > DWM_MAX_WINDOWS ||
            hdr->BlurRectCount > DWM_MAX_BLUR_RECTS ||
            hdr->ScreenW > MAXLONG || hdr->ScreenH > MAXLONG)
        {
            DwmLog("DWM: invalid frame metadata\n");
            Sleep(50);
            continue;
        }

        if ((LONG)hdr->ScreenW != primW || (LONG)hdr->ScreenH != primH)
        {
            primW = (LONG)hdr->ScreenW;
            primH = (LONG)hdr->ScreenH;
            vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            g_originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
            g_originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
            if (vw <= 0 || vh <= 0) { vw = primW; vh = primH; g_originX = g_originY = 0; }
            if (primW == 0 || primH == 0 || !DwmCreateSurfaces(hdcScreen, vw, vh))
            {
                Sleep(50);
                continue;
            }
            DwmShadowInit(hdcScreen);
            forceFull = TRUE;
            continue;
        }

        if (hdr->Dirty == 0 && !forceFull)
        {
            HANDLE WaitHandles[2] = {hStopEvent, hWake};

            if (WaitForMultipleObjects(ARRAYSIZE(WaitHandles), WaitHandles,
                                       FALSE, 200) == WAIT_OBJECT_0)
                break;
            continue;
        }

        {
            LONG pl, pt, pr, pb;
            BOOL completeFrame = TRUE;
            BOOL refreshBackdrop = forceFull || hdr->FullDamage;

            if (forceFull || hdr->FullDamage ||
                hdr->DmgR <= hdr->DmgL || hdr->DmgB <= hdr->DmgT)
            {
                pl = 0; pt = 0; pr = g_W; pb = g_H;
            }
            else
            {
                LONGLONG l = (LONGLONG)hdr->DmgL - g_originX;
                LONGLONG t = (LONGLONG)hdr->DmgT - g_originY;
                LONGLONG r = (LONGLONG)hdr->DmgR - g_originX;
                LONGLONG b = (LONGLONG)hdr->DmgB - g_originY;
                pl = (l < 0) ? 0 : (l > g_W ? g_W : (LONG)l);
                pt = (t < 0) ? 0 : (t > g_H ? g_H : (LONG)t);
                pr = (r < 0) ? 0 : (r > g_W ? g_W : (LONG)r);
                pb = (b < 0) ? 0 : (b > g_H ? g_H : (LONG)b);
            }
            pl -= g_shadowMarginLeft;
            pt -= g_shadowMarginTop;
            pr += g_shadowMarginRight;
            pb += g_shadowMarginBottom;
            if (pl < 0) pl = 0;
            if (pt < 0) pt = 0;
            if (pr > g_W) pr = g_W;
            if (pb > g_H) pb = g_H;
            wins = (PDWM_WIN)(g_buf + hdr->WinArrayBase);

            for (;;)
            {
                BOOL grown = FALSE;

                for (i = 0; i < hdr->Count; i++)
                {
                    LONGLONG wl, wt, wr, wb;

                    if (!DwmWindowBlursBackdrop(&wins[i]))
                        continue;
                    wl = (LONGLONG)wins[i].x - g_originX;
                    wt = (LONGLONG)wins[i].y - g_originY;
                    wr = wl + wins[i].cx;
                    wb = wt + wins[i].cy;
                    if (wl < 0) wl = 0;
                    if (wt < 0) wt = 0;
                    if (wr > g_W) wr = g_W;
                    if (wb > g_H) wb = g_H;
                    if (wr <= wl || wb <= wt ||
                        wr <= pl || wl >= pr || wb <= pt || wt >= pb)
                        continue;
                    if (wl < pl) { pl = (LONG)wl; grown = TRUE; }
                    if (wt < pt) { pt = (LONG)wt; grown = TRUE; }
                    if (wr > pr) { pr = (LONG)wr; grown = TRUE; }
                    if (wb > pb) { pb = (LONG)wb; grown = TRUE; }
                }
                if (!grown)
                    break;
            }

            if (pl == 0 && pt == 0 && pr == g_W && pb == g_H)
                refreshBackdrop = TRUE;
            forceFull = FALSE;

            if (pr > pl && pb > pt)
            {
                if (refreshBackdrop)
                {
                    RECT fullBackdrop = {0, 0, g_W, g_H};

                    if (!PaintDesktop(g_hdcBackdrop) &&
                        !FillRect(g_hdcBackdrop, &fullBackdrop,
                                  GetSysColorBrush(COLOR_DESKTOP)))
                    {
                        forceFull = TRUE;
                        continue;
                    }
                }

                {
                    BOOL covered = FALSE;

                    for (i = 0; i < hdr->Count; i++)
                    {
                        LONGLONG wl, wt, wr, wb;

                        if (!DwmWindowIsOpaque(&wins[i]))
                            continue;
                        wl = (LONGLONG)wins[i].x - g_originX;
                        wt = (LONGLONG)wins[i].y - g_originY;
                        wr = wl + wins[i].cx;
                        wb = wt + wins[i].cy;
                        if (wl <= pl && wt <= pt && wr >= pr && wb >= pb)
                        {
                            covered = TRUE;
                            break;
                        }
                    }

                    if (!covered &&
                        !BitBlt(g_hdcComp, pl, pt, pr - pl, pb - pt,
                                g_hdcBackdrop, pl, pt, SRCCOPY))
                    {
                        forceFull = TRUE;
                        continue;
                    }
                }

                blurRects = (PRECTL)(g_buf + hdr->BlurRectArrayBase);
                for (i = 0; i < hdr->Count; i++)
                {
                    const BYTE *pix;
                    const BYTE *dxpix;
                    const RECTL *windowBlurRects = NULL;

                    if (DwmWindowIsHidden(wins, hdr->Count, i, pl, pt, pr, pb))
                        continue;
                    pix = DwmGetSurfaceView(&wins[i]);
                    if (pix == NULL)
                    {
                        completeFrame = FALSE;
                        break;
                    }
                    if (wins[i].BlurRectBase > hdr->BlurRectCount ||
                        wins[i].BlurRectCount >
                            hdr->BlurRectCount - wins[i].BlurRectBase)
                    {
                        completeFrame = FALSE;
                        break;
                    }
                    if (wins[i].BlurRectCount != 0)
                        windowBlurRects = &blurRects[wins[i].BlurRectBase];
                    if (wins[i].AnimFlags != 0)
                    {
                        DwmBlitWindowAnimated((ULONG *)g_compBits, g_W,
                                              pl, pt, pr, pb, pix,
                                              DwmDxGetSurfaceSnapshot(&wins[i]),
                                              &wins[i]);
                        continue;
                    }
                    DwmApplyBlur((ULONG *)g_compBits, g_W, g_H,
                                 pl, pt, pr, pb, &wins[i],
                                 windowBlurRects);
                    DwmApplyBackdropBlur((ULONG *)g_compBits, g_W, g_H,
                                         pl, pt, pr, pb, &wins[i]);
                    /* A non-client shadow is a compositor layer immediately
                     * below its owner. Since wins[] is bottom-to-top, higher
                     * windows and their shadows naturally occlude lower ones. */
                    DwmBlendShadow((ULONG *)g_compBits, g_W, g_H,
                                   pl, pt, pr, pb, &wins[i]);
                    DwmBlitWindow((ULONG *)g_compBits, g_W, pl, pt, pr, pb,
                                  pix, (const ULONG *)g_backdropBits,
                                  &wins[i]);

                    dxpix = DwmDxGetSurfaceSnapshot(&wins[i]);
                    if (dxpix != NULL)
                    {
                        DWM_WIN client = wins[i];

                        client.x += wins[i].DxClientX;
                        client.y += wins[i].DxClientY;
                        client.cx = (LONG)wins[i].DxWidth;
                        client.cy = (LONG)wins[i].DxHeight;
                        client.Stride = wins[i].DxPitch;
                        if (client.BackdropRegion == DWM_BACKDROP_REGION_NONCLIENT)
                            client.BackdropType = 0;
                        DwmBlitWindow((ULONG *)g_compBits, g_W,
                                      pl, pt, pr, pb, dxpix,
                                      (const ULONG *)g_backdropBits, &client);
                    }
                }

                /* Never replace the last complete scan-out with a partially
                 * composed buffer. A surface can legitimately be recreated
                 * between GETFRAME and OPENSURFACE; the next idle metadata
                 * pull contains all current FRONTs and retries this frame. */
                if (!completeFrame)
                {
                    forceFull = TRUE;
                }
                else
                {
                    BOOL bltResult = BitBlt(hdcScreen, g_originX + pl, g_originY + pt,
                                            pr - pl, pb - pt, g_hdcComp, pl, pt,
                                            SRCCOPY);
                    BOOL flushResult = GdiFlush();
                    if (!bltResult || !flushResult)
                        forceFull = TRUE;
                    else
                    {
                        LARGE_INTEGER Counter;

                        QueryPerformanceCounter(&Counter);
                        g_lastPresentQpc = (ULONGLONG)Counter.QuadPart;
                        for (i = 0; i < hdr->Count; ++i)
                            DwmDxAcknowledgeSurface(&wins[i]);
                    }
                }
            }
        }

        g_frameSeq++;
        if ((g_frameSeq & 255) == 0)
        {
            DwmSweepViews();
            DwmDxSweepSurfaces(g_frameSeq);
        }
    }

    RtlZeroMemory(&att, sizeof(att));
    att.Attach = 0;
    (void)NtUserCallOneParam((DWORD_PTR)&att, DWM_ROUTINE_ATTACH);
    CloseHandle(hWake);
    ReleaseDC(NULL, hdcScreen);
}

DWORD WINAPI
DwmCoreCompositorThread(LPVOID Parameter)
{
    DwmComposeLoop((HANDLE)Parameter);
    return 0;
}
