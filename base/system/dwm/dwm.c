/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
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
LONG NTAPI NtSetTimerResolution(ULONG DesiredResolution, BOOLEAN SetResolution,
                                PULONG CurrentResolution);

static BOOL g_timerPrecise;

static void
DwmSetTimerPrecision(BOOL Precise)
{
    ULONG Res = 0;

    if (Precise == g_timerPrecise)
        return;
    if (NtSetTimerResolution(10000, (BOOLEAN)Precise, &Res) >= 0)
        g_timerPrecise = Precise;
}

static void DwmLog(const char *s) { OutputDebugStringA(s); }

static LONG g_originX, g_originY;
static LONG g_W, g_H;

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

static ULONG *g_blurSource;
static ULONG *g_blurTemp;
static ULONG *g_blurPass;
static SIZE_T g_blurPixelCount;
static ULONG *g_blurLine;
static SIZE_T g_blurLineCount;
static BYTE g_noiseTile[64 * 64];
static BOOL g_noiseReady;

#define DWM_BLUR_RADIUS_96          10
#define DWM_BLUR_RADIUS_MAX         48
#define DWM_BLUR_PASSES             3
#define DWM_MATERIAL_SATURATION     20
#define DWM_MATERIAL_NOISE          4

static BOOL
DwmEnsureBlurBuffers(LONG Width, LONG Height)
{
    ULONG *Source, *Temp, *Pass, *Line;
    SIZE_T PixelCount, Bytes, LineCount;

    if (Width <= 0 || Height <= 0 ||
        (SIZE_T)Width > (SIZE_T)-1 / (SIZE_T)Height)
        return FALSE;
    PixelCount = (SIZE_T)Width * (SIZE_T)Height;
    if (PixelCount > (SIZE_T)-1 / sizeof(ULONG))
        return FALSE;
    LineCount = (SIZE_T)(Width > Height ? Width : Height) +
                2 * DWM_BLUR_RADIUS_MAX + 8;
    if (g_blurSource != NULL && g_blurTemp != NULL && g_blurPass != NULL &&
        g_blurLine != NULL && g_blurPixelCount == PixelCount &&
        g_blurLineCount == LineCount)
        return TRUE;

    Bytes = PixelCount * sizeof(ULONG);
    Source = VirtualAlloc(NULL, Bytes, MEM_COMMIT | MEM_RESERVE,
                          PAGE_READWRITE);
    Temp = VirtualAlloc(NULL, Bytes, MEM_COMMIT | MEM_RESERVE,
                        PAGE_READWRITE);
    Pass = VirtualAlloc(NULL, Bytes, MEM_COMMIT | MEM_RESERVE,
                        PAGE_READWRITE);
    Line = VirtualAlloc(NULL, LineCount * sizeof(ULONG),
                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Source == NULL || Temp == NULL || Pass == NULL || Line == NULL)
    {
        if (Source) VirtualFree(Source, 0, MEM_RELEASE);
        if (Temp) VirtualFree(Temp, 0, MEM_RELEASE);
        if (Pass) VirtualFree(Pass, 0, MEM_RELEASE);
        if (Line) VirtualFree(Line, 0, MEM_RELEASE);
        return FALSE;
    }

    if (g_blurSource != NULL)
        VirtualFree(g_blurSource, 0, MEM_RELEASE);
    if (g_blurTemp != NULL)
        VirtualFree(g_blurTemp, 0, MEM_RELEASE);
    if (g_blurPass != NULL)
        VirtualFree(g_blurPass, 0, MEM_RELEASE);
    if (g_blurLine != NULL)
        VirtualFree(g_blurLine, 0, MEM_RELEASE);
    g_blurSource = Source;
    g_blurTemp = Temp;
    g_blurPass = Pass;
    g_blurLine = Line;
    g_blurPixelCount = PixelCount;
    g_blurLineCount = LineCount;
    return TRUE;
}

static void
DwmEnsureNoise(void)
{
    LONG x, y;

    if (g_noiseReady)
        return;
    for (y = 0; y < 64; ++y)
    {
        for (x = 0; x < 64; ++x)
        {
            ULONG Hash = (ULONG)x * 0x9E3779B1u ^ (ULONG)y * 0x85EBCA77u;

            Hash ^= Hash >> 15;
            Hash *= 0x2C1B3C6Du;
            Hash ^= Hash >> 12;
            g_noiseTile[y * 64 + x] =
                (BYTE)(Hash % (2u * DWM_MATERIAL_NOISE + 1u));
        }
    }
    g_noiseReady = TRUE;
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

static ULONGLONG
DwmPackPixel(ULONG Pixel)
{
    return ((ULONGLONG)((Pixel >> 16) & 0xffu) << 40) |
           ((ULONGLONG)((Pixel >> 8) & 0xffu) << 20) |
           (ULONGLONG)(Pixel & 0xffu);
}

static ULONG
DwmUnpackAverage(ULONGLONG Sum, ULONGLONG Reciprocal)
{
    ULONG Red = (ULONG)((((Sum >> 40) & 0xfffffu) * Reciprocal) >> 24);
    ULONG Green = (ULONG)((((Sum >> 20) & 0xfffffu) * Reciprocal) >> 24);
    ULONG Blue = (ULONG)(((Sum & 0xfffffu) * Reciprocal) >> 24);

    return 0xff000000u | (Red << 16) | (Green << 8) | Blue;
}

static ULONG
DwmMaterialFinish(ULONG Pixel, LONG x, LONG y)
{
    LONG Red = (LONG)((Pixel >> 16) & 0xffu);
    LONG Green = (LONG)((Pixel >> 8) & 0xffu);
    LONG Blue = (LONG)(Pixel & 0xffu);
    LONG Luma = (Red * 77 + Green * 151 + Blue * 28) >> 8;
    LONG Noise = (LONG)g_noiseTile[((y & 63) << 6) | (x & 63)] -
                 DWM_MATERIAL_NOISE;

    Red = Luma + (Red - Luma) * (100 + DWM_MATERIAL_SATURATION) / 100 + Noise;
    Green = Luma + (Green - Luma) * (100 + DWM_MATERIAL_SATURATION) / 100 + Noise;
    Blue = Luma + (Blue - Luma) * (100 + DWM_MATERIAL_SATURATION) / 100 + Noise;
    if (Red < 0) Red = 0; else if (Red > 255) Red = 255;
    if (Green < 0) Green = 0; else if (Green > 255) Green = 255;
    if (Blue < 0) Blue = 0; else if (Blue > 255) Blue = 255;
    return 0xff000000u | ((ULONG)Red << 16) | ((ULONG)Green << 8) | (ULONG)Blue;
}

static void
DwmDownsampleHalf(const ULONG *Composition, LONG Width, LONG Height,
                  ULONG *Half, LONG HalfWidth,
                  LONG Left, LONG Top, LONG Right, LONG Bottom)
{
    LONG hx, hy;

    for (hy = Top; hy < Bottom; ++hy)
    {
        LONG y0 = hy * 2, y1 = (y0 + 1 < Height) ? y0 + 1 : y0;
        const ULONG *Row0 = Composition + (SIZE_T)y0 * Width;
        const ULONG *Row1 = Composition + (SIZE_T)y1 * Width;
        ULONG *Out = Half + (SIZE_T)hy * HalfWidth;

        for (hx = Left; hx < Right; ++hx)
        {
            LONG x0 = hx * 2, x1 = (x0 + 1 < Width) ? x0 + 1 : x0;
            ULONG p0 = Row0[x0], p1 = Row0[x1], p2 = Row1[x0], p3 = Row1[x1];
            ULONG rb = (p0 & 0xff00ffu) + (p1 & 0xff00ffu) +
                       (p2 & 0xff00ffu) + (p3 & 0xff00ffu);
            ULONG g = (p0 & 0xff00u) + (p1 & 0xff00u) +
                      (p2 & 0xff00u) + (p3 & 0xff00u);

            Out[hx] = 0xff000000u | ((rb >> 2) & 0xff00ffu) | ((g >> 2) & 0xff00u);
        }
    }
}

static void
DwmBoxBlurPass(const ULONG *Source, ULONG *Dest, LONG Width, LONG Height,
               LONG Left, LONG Top, LONG Right, LONG Bottom, LONG Radius)
{
    const ULONG Divisor = (ULONG)Radius * 2u + 1u;
    const ULONGLONG Reciprocal = (0x1000000ull + Divisor - 1u) / Divisor;
    ULONG *Line = g_blurLine;
    LONG SampleTop, SampleBottom, x, y, i, Span;

    if (Right <= Left || Bottom <= Top || Radius <= 0)
        return;
    SampleTop = Top - Radius;
    if (SampleTop < 0)
        SampleTop = 0;
    SampleBottom = Bottom + Radius;
    if (SampleBottom > Height)
        SampleBottom = Height;

    Span = Right - Left + 2 * Radius + 1;
    for (y = SampleTop; y < SampleBottom; ++y)
    {
        const ULONG *Row = Source + (SIZE_T)y * Width;
        ULONG *Out = g_blurTemp + (SIZE_T)y * Width;
        ULONGLONG Sum = 0;

        for (i = 0; i < Span; ++i)
            Line[i] = Row[DwmClampCoordinate(Left - Radius + i, Width)];
        for (i = 0; i <= 2 * Radius; ++i)
            Sum += DwmPackPixel(Line[i]);
        for (x = Left; x < Right; ++x)
        {
            Out[x] = DwmUnpackAverage(Sum, Reciprocal);
            Sum += DwmPackPixel(Line[x - Left + 2 * Radius + 1]);
            Sum -= DwmPackPixel(Line[x - Left]);
        }
    }

    Span = Bottom - Top + 2 * Radius + 1;
    for (x = Left; x < Right; ++x)
    {
        ULONGLONG Sum = 0;

        for (i = 0; i < Span; ++i)
        {
            Line[i] = g_blurTemp[(SIZE_T)DwmClampCoordinate(Top - Radius + i,
                                                             Height) * Width + x];
        }
        for (i = 0; i <= 2 * Radius; ++i)
            Sum += DwmPackPixel(Line[i]);
        for (y = Top; y < Bottom; ++y)
        {
            Dest[(SIZE_T)y * Width + x] = DwmUnpackAverage(Sum, Reciprocal);
            Sum += DwmPackPixel(Line[y - Top + 2 * Radius + 1]);
            Sum -= DwmPackPixel(Line[y - Top]);
        }
    }
}

static void
DwmBlurUpsample(const ULONG *Half, LONG HalfWidth, LONG HalfHeight,
                ULONG *Composition, LONG Width,
                LONG Left, LONG Top, LONG Right, LONG Bottom, ULONG Alpha)
{
    LONG x, y;

    for (y = Top; y < Bottom; ++y)
    {
        LONG qy = 2 * y - 1;
        LONG hy0 = qy >> 2, fy = qy & 3, hy1;
        const ULONG *Row0, *Row1;
        ULONG *Out = Composition + (SIZE_T)y * Width;

        if (hy0 < 0) hy0 = 0;
        if (hy0 > HalfHeight - 1) hy0 = HalfHeight - 1;
        hy1 = (hy0 + 1 < HalfHeight) ? hy0 + 1 : hy0;
        Row0 = Half + (SIZE_T)hy0 * HalfWidth;
        Row1 = Half + (SIZE_T)hy1 * HalfWidth;
        for (x = Left; x < Right; ++x)
        {
            LONG qx = 2 * x - 1;
            LONG hx0 = qx >> 2, fx = qx & 3, hx1;
            ULONG w00, w10, w01, w11, p00, p10, p01, p11, rb, g, Blurred;

            if (hx0 < 0) hx0 = 0;
            if (hx0 > HalfWidth - 1) hx0 = HalfWidth - 1;
            hx1 = (hx0 + 1 < HalfWidth) ? hx0 + 1 : hx0;
            w11 = (ULONG)(fx * fy);
            w10 = (ULONG)(fx * (4 - fy));
            w01 = (ULONG)((4 - fx) * fy);
            w00 = 16u - w11 - w10 - w01;
            p00 = Row0[hx0]; p10 = Row0[hx1]; p01 = Row1[hx0]; p11 = Row1[hx1];
            rb = (p00 & 0xff00ffu) * w00 + (p10 & 0xff00ffu) * w10 +
                 (p01 & 0xff00ffu) * w01 + (p11 & 0xff00ffu) * w11;
            g = (p00 & 0xff00u) * w00 + (p10 & 0xff00u) * w10 +
                (p01 & 0xff00u) * w01 + (p11 & 0xff00u) * w11;
            Blurred = ((rb >> 4) & 0xff00ffu) | ((g >> 4) & 0xff00u);
            Blurred = DwmMaterialFinish(Blurred, x, y);
            if (Alpha < 255)
            {
                ULONG Base = Out[x];
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
            Out[x] = Blurred;
        }
    }
}

static LONG
DwmBlurRadius(void)
{
    LONG Radius = MulDiv(DWM_BLUR_RADIUS_96,
                         g_shadowDpi > 0 ? g_shadowDpi : 96, 96);

    if (Radius < 1)
        Radius = 1;
    if (Radius > DWM_BLUR_RADIUS_MAX)
        Radius = DWM_BLUR_RADIUS_MAX;
    return Radius;
}

static BOOL
DwmClipBlurRect(const RECTL *Rectangle, LONG WindowX, LONG WindowY,
                const DWM_WIN *Window,
                LONG ClipLeft, LONG ClipTop, LONG ClipRight, LONG ClipBottom,
                LONG Width, LONG Height, RECTL *Out)
{
    LONG Left = WindowX + Rectangle->left;
    LONG Top = WindowY + Rectangle->top;
    LONG Right = WindowX + Rectangle->right;
    LONG Bottom = WindowY + Rectangle->bottom;
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
    if (Right <= Left || Bottom <= Top)
        return FALSE;
    Out->left = Left;
    Out->top = Top;
    Out->right = Right;
    Out->bottom = Bottom;
    return TRUE;
}

static void
DwmApplyBlur(ULONG *Composition, LONG Width, LONG Height,
             LONG ClipLeft, LONG ClipTop, LONG ClipRight, LONG ClipBottom,
             const DWM_WIN *Window, const RECTL *Rectangles)
{
    RECTL Entire = {0, 0, Window->cx, Window->cy};
    RECTL Union = {0, 0, 0, 0}, Clipped;
    const RECTL *Rectangle;
    ULONG Index, Count, Alpha = 255;
    LONG Radius, HalfRadius, HalfWidth, HalfHeight, WindowX, WindowY;
    LONG hL, hT, hR, hB, Reach, Pass;
    const ULONG *Result;
    BOOL HaveUnion = FALSE;

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
    DwmEnsureNoise();

    Radius = DwmBlurRadius();
    HalfRadius = (Radius + 1) / 2;
    if (HalfRadius < 1)
        HalfRadius = 1;
    HalfWidth = (Width + 1) / 2;
    HalfHeight = (Height + 1) / 2;
    WindowX = Window->x - g_originX;
    WindowY = Window->y - g_originY;

    for (Index = 0; Index < Count; ++Index)
    {
        if (!DwmClipBlurRect(&Rectangle[Index], WindowX, WindowY, Window,
                             ClipLeft, ClipTop, ClipRight, ClipBottom,
                             Width, Height, &Clipped))
            continue;
        if (!HaveUnion)
        {
            Union = Clipped;
            HaveUnion = TRUE;
            continue;
        }
        if (Clipped.left < Union.left) Union.left = Clipped.left;
        if (Clipped.top < Union.top) Union.top = Clipped.top;
        if (Clipped.right > Union.right) Union.right = Clipped.right;
        if (Clipped.bottom > Union.bottom) Union.bottom = Clipped.bottom;
    }
    if (!HaveUnion)
        return;

    hL = Union.left / 2;
    hT = Union.top / 2;
    hR = (Union.right + 1) / 2;
    hB = (Union.bottom + 1) / 2;
    Reach = HalfRadius * DWM_BLUR_PASSES + 2;
    DwmDownsampleHalf(Composition, Width, Height, g_blurSource, HalfWidth,
                      (hL - Reach < 0) ? 0 : hL - Reach,
                      (hT - Reach < 0) ? 0 : hT - Reach,
                      (hR + Reach > HalfWidth) ? HalfWidth : hR + Reach,
                      (hB + Reach > HalfHeight) ? HalfHeight : hB + Reach);

    Result = g_blurSource;
    for (Pass = DWM_BLUR_PASSES - 1; Pass >= 0; --Pass)
    {
        LONG Grow = HalfRadius * Pass;
        LONG Left = hL - Grow, Top = hT - Grow;
        LONG Right = hR + Grow, Bottom = hB + Grow;
        ULONG *Dest = (Result == g_blurSource) ? g_blurPass : g_blurSource;

        if (Left < 0) Left = 0;
        if (Top < 0) Top = 0;
        if (Right > HalfWidth) Right = HalfWidth;
        if (Bottom > HalfHeight) Bottom = HalfHeight;
        DwmBoxBlurPass(Result, Dest, HalfWidth, HalfHeight,
                       Left, Top, Right, Bottom, HalfRadius);
        Result = Dest;
    }

    for (Index = 0; Index < Count; ++Index)
    {
        if (!DwmClipBlurRect(&Rectangle[Index], WindowX, WindowY, Window,
                             ClipLeft, ClipTop, ClipRight, ClipBottom,
                             Width, Height, &Clipped))
            continue;
        DwmBlurUpsample(Result, HalfWidth, HalfHeight, Composition, Width,
                        Clipped.left, Clipped.top, Clipped.right,
                        Clipped.bottom, Alpha);
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

#define DWM_CORNER_MAX_RECTS 129
#define DWM_REFLECT_STRENGTH 34u

static BYTE *g_reflectLut;
static LONG g_reflectLen;

static double
DwmReflectBump(double t, double Center, double HalfWidth, double Peak)
{
    double d = (t - Center) / HalfWidth;

    if (d <= -1.0 || d >= 1.0)
        return 0.0;
    return Peak * 0.5 * (1.0 + cos(3.14159265358979 * d));
}

static BOOL
DwmEnsureReflection(LONG Width, LONG Height)
{
    LONG Length = Width + (Height * 6) / 5 + 2, u;
    BYTE *Lut;

    if (Length <= 0)
        return FALSE;
    if (g_reflectLut != NULL && g_reflectLen == Length)
        return TRUE;
    Lut = VirtualAlloc(NULL, (SIZE_T)Length, MEM_COMMIT | MEM_RESERVE,
                       PAGE_READWRITE);
    if (Lut == NULL)
        return FALSE;
    for (u = 0; u < Length; ++u)
    {
        double t = (double)u / (double)Length;
        double v = DwmReflectBump(t, 0.30, 0.14, 1.0) +
                   DwmReflectBump(t, 0.60, 0.05, 0.55) +
                   DwmReflectBump(t, 0.80, 0.11, 0.40);

        if (v > 1.0)
            v = 1.0;
        Lut[u] = (BYTE)(v * 255.0 + 0.5);
    }
    if (g_reflectLut != NULL)
        VirtualFree(g_reflectLut, 0, MEM_RELEASE);
    g_reflectLut = Lut;
    g_reflectLen = Length;
    return TRUE;
}

static ULONG
DwmReflection(LONG ScreenX, LONG ScreenY)
{
    LONG u = ScreenX + (ScreenY * 6) / 5;

    if (g_reflectLut == NULL || u < 0 || u >= g_reflectLen)
        return 0;
    return g_reflectLut[u];
}
#define DWM_MATERIAL_FRINGE 96u
#define DWM_MATERIAL_TINT_BAND 48u
#define DWM_MATERIAL_EDGE_LIGHT 26u

static ULONG
DwmChannelDistance(ULONG a, ULONG b)
{
    LONG dr = (LONG)((a >> 16) & 0xFFu) - (LONG)((b >> 16) & 0xFFu);
    LONG dg = (LONG)((a >> 8) & 0xFFu) - (LONG)((b >> 8) & 0xFFu);
    LONG db = (LONG)(a & 0xFFu) - (LONG)(b & 0xFFu);

    if (dr < 0) dr = -dr;
    if (dg < 0) dg = -dg;
    if (db < 0) db = -db;
    if (dg > dr) dr = dg;
    if (db > dr) dr = db;
    return (ULONG)dr;
}

static ULONG
DwmCornerAlpha(LONG x, LONG y, LONG cx, LONG cy, ULONG Radius)
{
    LONG r = (LONG)Radius;
    double dx, dy, Distance, Cover;

    if (r <= 0)
        return 255;
    if (r * 2 > cx)
        r = cx / 2;
    if (r * 2 > cy)
        r = cy / 2;
    if (r <= 0)
        return 255;
    if (x >= r && x < cx - r)
        return 255;
    if (y >= r && y < cy - r)
        return 255;

    dx = (x < r) ? (double)r - ((double)x + 0.5)
                 : ((double)x + 0.5) - (double)(cx - r);
    dy = (y < r) ? (double)r - ((double)y + 0.5)
                 : ((double)y + 0.5) - (double)(cy - r);
    if (dx < 0.0)
        dx = 0.0;
    if (dy < 0.0)
        dy = 0.0;

    Distance = sqrt(dx * dx + dy * dy);
    Cover = (double)r - Distance + 0.5;
    if (Cover <= 0.0)
        return 0;
    if (Cover >= 1.0)
        return 255;
    return (ULONG)(Cover * 255.0);
}

static ULONG
DwmBuildRoundedRects(RECTL *Rects, ULONG Max, LONG cx, LONG cy, ULONG Radius)
{
    LONG r = (LONG)Radius, y;
    ULONG Count = 0;

    if (r <= 0 || cx <= 0 || cy <= 0)
        return 0;
    if (r * 2 > cx)
        r = cx / 2;
    if (r * 2 > cy)
        r = cy / 2;
    if (r <= 0 || (ULONG)(r * 2 + 1) > Max)
        return 0;

    for (y = 0; y < r; ++y)
    {
        double dy = (double)r - ((double)y + 0.5);
        double dx = sqrt((double)r * (double)r - dy * dy);
        LONG Inset = r - (LONG)(dx + 0.5);

        if (Inset < 0)
            Inset = 0;
        Rects[Count].left = Inset;
        Rects[Count].top = y;
        Rects[Count].right = cx - Inset;
        Rects[Count].bottom = y + 1;
        Count++;
    }

    Rects[Count].left = 0;
    Rects[Count].top = r;
    Rects[Count].right = cx;
    Rects[Count].bottom = cy - r;
    Count++;

    for (y = cy - r; y < cy; ++y)
    {
        double dy = ((double)y + 0.5) - (double)(cy - r);
        double dx = sqrt((double)r * (double)r - dy * dy);
        LONG Inset = r - (LONG)(dx + 0.5);

        if (Inset < 0)
            Inset = 0;
        Rects[Count].left = Inset;
        Rects[Count].top = y;
        Rects[Count].right = cx - Inset;
        Rects[Count].bottom = y + 1;
        Count++;
    }

    return Count;
}

static LONG
DwmBackdropNcBottom(const DWM_WIN *Window)
{
    LONG Bottom = Window->ClientY + (LONG)Window->BackdropNcExtend;
    LONG Limit = Window->ClientY + Window->ClientHeight;

    if (Bottom > Limit)
        Bottom = Limit;
    return Bottom;
}

static LONG
DwmBackdropNcLeft(const DWM_WIN *Window)
{
    LONG Left = Window->ClientX + (LONG)Window->BackdropNcExtendLeft;
    LONG Limit = Window->ClientX + Window->ClientWidth;

    if (Left > Limit)
        Left = Limit;
    return Left;
}

static void
DwmApplyBackdropBlur(ULONG *Composition, LONG Width, LONG Height,
                     LONG ClipLeft, LONG ClipTop, LONG ClipRight,
                     LONG ClipBottom, const DWM_WIN *Window)
{
    DWM_WIN BlurWindow;
    RECTL Rectangles[4];
    LONG NcBottom;

    if (Window->BackdropType != DWM_BACKDROP_TRANSIENT)
        return;

    BlurWindow = *Window;
    BlurWindow.BlurFlags = DWM_BLUR_ENABLE;
    if (Window->BackdropRegion == DWM_BACKDROP_REGION_WINDOW)
    {
        if (Window->CornerRadius != 0)
        {
            RECTL Rounded[DWM_CORNER_MAX_RECTS];
            ULONG Count = DwmBuildRoundedRects(Rounded, ARRAYSIZE(Rounded),
                                               Window->cx, Window->cy,
                                               Window->CornerRadius);
            if (Count != 0)
            {
                BlurWindow.BlurRectCount = Count;
                DwmApplyBlur(Composition, Width, Height,
                             ClipLeft, ClipTop, ClipRight, ClipBottom,
                             &BlurWindow, Rounded);
                return;
            }
        }
        BlurWindow.BlurFlags |= DWM_BLUR_REGION_ENTIRE_WINDOW;
        BlurWindow.BlurRectCount = 0;
        DwmApplyBlur(Composition, Width, Height,
                     ClipLeft, ClipTop, ClipRight, ClipBottom,
                     &BlurWindow, NULL);
        return;
    }
    if (Window->BackdropRegion != DWM_BACKDROP_REGION_NONCLIENT)
        return;

    NcBottom = DwmBackdropNcBottom(Window);
    Rectangles[0] = (RECTL){0, 0, Window->cx, NcBottom};
    Rectangles[1] = (RECTL){0, Window->ClientY + Window->ClientHeight,
                            Window->cx, Window->cy};
    Rectangles[2] = (RECTL){0, NcBottom, DwmBackdropNcLeft(Window),
                            Window->ClientY + Window->ClientHeight};
    Rectangles[3] = (RECTL){Window->ClientX + Window->ClientWidth,
                            NcBottom, Window->cx,
                            Window->ClientY + Window->ClientHeight};
    BlurWindow.BlurRectCount = ARRAYSIZE(Rectangles);
    DwmApplyBlur(Composition, Width, Height,
                 ClipLeft, ClipTop, ClipRight, ClipBottom,
                 &BlurWindow, Rectangles);
}

static void
DwmBlendShadowSpan(ULONG *Row, LONG X0, LONG X1, ULONG WideY, ULONG TightY)
{
    LONG x;

    for (x = X0; x < X1; ++x)
    {
        ULONG WideAlpha = (g_shadowCoverWide[x] * WideY) >> 8;
        ULONG TightAlpha = (g_shadowCoverTight[x] * TightY) >> 8;
        ULONG Alpha = WideAlpha + TightAlpha - ((WideAlpha * TightAlpha) >> 8);
        ULONG Inverse, Pixel;

        if (Alpha == 0)
            continue;
        if (Alpha > 255)
            Alpha = 255;
        Inverse = 256u - Alpha;
        Pixel = Row[x];
        Row[x] = ((((Pixel & 0xff00ffu) * Inverse) >> 8) & 0xff00ffu) |
                 ((((Pixel & 0xff00u) * Inverse) >> 8) & 0xff00u);
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
    {
        LONG xs = x1, xe = x0;

        for (x = x0; x < x1; ++x)
        {
            LONGLONG Position = (LONGLONG)x - ownerX;
            ULONG Wide = DwmShadowCoverage(&g_shadowWide, Position, w->cx);
            ULONG Tight = DwmShadowCoverage(tightKernel, Position, w->cx);

            g_shadowCoverWide[x] = (Wide * 256u + 32767u) / 65535u;
            g_shadowCoverTight[x] = (Tight * 256u + 32767u) / 65535u;
            if (g_shadowCoverWide[x] != 0 || g_shadowCoverTight[x] != 0)
            {
                if (x < xs) xs = x;
                xe = x + 1;
            }
        }
        x0 = xs;
        x1 = xe;
    }
    if (x1 <= x0)
        return;

    for (y = y0; y < y1; y++)
    {
        ULONG *row = comp + (SIZE_T)y * scrW;
        LONGLONG PositionY = (LONGLONG)y - ownerY - verticalOffset;
        ULONG WideY = DwmShadowCoverage(&g_shadowWide, PositionY, w->cy);
        ULONG TightY = DwmShadowCoverage(tightKernel, PositionY, w->cy);

        WideY = (ULONG)(((ULONGLONG)WideY * wideOpacity * windowAlpha) /
                        (65535ull * 1000ull));
        TightY = (ULONG)(((ULONGLONG)TightY * tightOpacity * windowAlpha) /
                         (65535ull * 1000ull));
        if (WideY == 0 && TightY == 0)
            continue;

        if ((LONGLONG)y >= ownerY && (LONGLONG)y < ownerBottom)
        {
            LONG leftEnd = (ownerX <= x0) ? x0 :
                           (ownerX >= x1) ? x1 : (LONG)ownerX;
            LONG rightStart = (ownerRight <= x0) ? x0 :
                              (ownerRight >= x1) ? x1 : (LONG)ownerRight;

            DwmBlendShadowSpan(row, x0, leftEnd, WideY, TightY);
            DwmBlendShadowSpan(row, rightStart, x1, WideY, TightY);
        }
        else
        {
            DwmBlendShadowSpan(row, x0, x1, WideY, TightY);
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
    BOOL useCorner = w->CornerRadius != 0;
    BOOL useAlpha = (w->LayerFlags & DWM_LWA_ALPHA) != 0 && w->Alpha < 255;
    BOOL usePixelAlpha = (w->BlurFlags & DWM_BLUR_ENABLE) != 0;
    BOOL useBackdrop = w->BackdropType >= DWM_BACKDROP_MAIN &&
                       w->BackdropType <= DWM_BACKDROP_TABBED &&
                       w->BackdropRegion != 0;
    LONG ncBottom = DwmBackdropNcBottom(w);
    LONG ncLeft = DwmBackdropNcLeft(w);
    ULONG a = w->Alpha, key = 0, backdropKey = 0, colorizationKey = 0;
    BOOL useEdge = useBackdrop && w->BackdropRegion == DWM_BACKDROP_REGION_WINDOW;
    BOOL edgeLeft, edgeTop, edgeRight, edgeBottom;

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
    edgeLeft = wx > 0;
    edgeTop = wy > 0;
    edgeRight = right < g_W;
    edgeBottom = bottom < g_H;
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

        if (!useKey && !useAlpha && !usePixelAlpha && !useBackdrop &&
            !useCorner)
        {
            RtlCopyMemory(dstrow, srcrow, (SIZE_T)width * 4);
            continue;
        }

        for (x = 0; x < width; x++)
        {
            ULONG s = srcrow[x], d, pixelAlpha = 255, cornerAlpha = 255;
            LONG sourceX = srcx0 + x;
            BOOL materialPixel = FALSE;
            ULONG materialWeight = 0, materialKey = 0, edge = 0;

            if (useCorner)
                cornerAlpha = DwmCornerAlpha(sourceX, r, w->cx, w->cy,
                                             w->CornerRadius);
            if (useEdge)
            {
                ULONG inner;

                if ((sourceX == 0 && edgeLeft) || (r == 0 && edgeTop) ||
                    (sourceX == w->cx - 1 && edgeRight) ||
                    (r == w->cy - 1 && edgeBottom))
                    inner = 0;
                else if (useCorner)
                    inner = DwmCornerAlpha(sourceX - 1, r - 1,
                                           w->cx - 2, w->cy - 2,
                                           w->CornerRadius - 1);
                else
                    inner = 255;
                edge = cornerAlpha > inner ? cornerAlpha - inner : 0;
            }

            if (useKey && (s & 0x00FFFFFFu) == key)
                continue;
            if (useBackdrop)
            {
                if (w->BackdropRegion == DWM_BACKDROP_REGION_WINDOW ||
                    sourceX < ncLeft ||
                    sourceX >= w->ClientX + w->ClientWidth ||
                    r < ncBottom ||
                    r >= w->ClientY + w->ClientHeight)
                {
                    ULONG Near = DwmChannelDistance(s, backdropKey);
                    ULONG Other = DwmChannelDistance(s, colorizationKey);

                    materialKey = backdropKey;
                    if (Other < Near)
                    {
                        Near = Other;
                        materialKey = colorizationKey;
                    }
                    if (Near == 0)
                    {
                        materialPixel = TRUE;
                        materialWeight = 255;
                    }
                    else if (Near <= DWM_MATERIAL_TINT_BAND)
                    {
                        materialWeight = 255;
                    }
                    else if (Near < DWM_MATERIAL_FRINGE)
                    {
                        materialWeight = (DWM_MATERIAL_FRINGE - Near) * 255u /
                                         (DWM_MATERIAL_FRINGE -
                                          DWM_MATERIAL_TINT_BAND);
                    }
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
            else if (materialWeight != 0)
            {
                ULONG Shift = (255u - w->BackdropOpacity) * materialWeight / 255u;
                LONG sr = (LONG)((s >> 16) & 0xFFu);
                LONG sg = (LONG)((s >> 8) & 0xFFu);
                LONG sb = (LONG)(s & 0xFFu);

                sr += ((LONG)((d >> 16) & 0xFFu) -
                       (LONG)((materialKey >> 16) & 0xFFu)) * (LONG)Shift / 255;
                sg += ((LONG)((d >> 8) & 0xFFu) -
                       (LONG)((materialKey >> 8) & 0xFFu)) * (LONG)Shift / 255;
                sb += ((LONG)(d & 0xFFu) -
                       (LONG)(materialKey & 0xFFu)) * (LONG)Shift / 255;
                if (w->BackdropType == DWM_BACKDROP_TRANSIENT)
                {
                    LONG glow = (LONG)(DWM_REFLECT_STRENGTH *
                                       DwmReflection(x0 + x, dy) *
                                       materialWeight / (255u * 255u));

                    sr += glow;
                    sg += glow;
                    sb += glow;
                }
                if (sr < 0) sr = 0; else if (sr > 255) sr = 255;
                if (sg < 0) sg = 0; else if (sg > 255) sg = 255;
                if (sb < 0) sb = 0; else if (sb > 255) sb = 255;
                s = ((ULONG)sr << 16) | ((ULONG)sg << 8) | (ULONG)sb;
            }
            if (edge != 0 && (!useBackdrop || materialPixel))
            {
                ULONG lift = DWM_MATERIAL_EDGE_LIGHT * edge / 255u;
                ULONG sr = ((s >> 16) & 0xFFu) + lift;
                ULONG sg = ((s >> 8) & 0xFFu) + lift;
                ULONG sb = (s & 0xFFu) + lift;

                if (sr > 255) sr = 255;
                if (sg > 255) sg = 255;
                if (sb > 255) sb = 255;
                s = (sr << 16) | (sg << 8) | sb;
            }
            if (useAlpha)
                pixelAlpha = pixelAlpha * a / 255u;
            if (useCorner)
                pixelAlpha = pixelAlpha * cornerAlpha / 255u;
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

static LONG *g_animCols;
static SIZE_T g_animColCount;

static BOOL
DwmEnsureAnimColumns(LONG Width)
{
    LONG *Cols;
    SIZE_T Count = (SIZE_T)Width * 3;

    if (Width <= 0)
        return FALSE;
    if (g_animCols != NULL && g_animColCount == Count)
        return TRUE;
    Cols = VirtualAlloc(NULL, Count * sizeof(LONG), MEM_COMMIT | MEM_RESERVE,
                        PAGE_READWRITE);
    if (Cols == NULL)
        return FALSE;
    if (g_animCols != NULL)
        VirtualFree(g_animCols, 0, MEM_RELEASE);
    g_animCols = Cols;
    g_animColCount = Count;
    return TRUE;
}

static void
DwmBlitScaled(ULONG *comp, LONG scrW,
              LONG clipL, LONG clipT, LONG clipR, LONG clipB,
              const BYTE *pix, LONG srcCx, LONG srcCy, ULONG srcStride,
              LONG dstX, LONG dstY, LONG dstCx, LONG dstCy, ULONG alpha,
              const DWM_WIN *material)
{
    static const ULONG TapRecip[DWM_ANIM_TAPS * DWM_ANIM_TAPS + 1] =
    {
        0, 65536, 32768, 21846, 16384, 13108, 10923, 9363, 8192,
        7282, 6554, 5958, 5462, 5042, 4682, 4370, 4096
    };
    LONG y, x, y0, y1, x0, x1;
    LONG *sx0Tab, *sx1Tab, *stepXTab;
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
        matCx0 = DwmBackdropNcLeft(material);
        matCy0 = DwmBackdropNcBottom(material);
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
    if (!DwmEnsureAnimColumns(scrW))
        return;
    sx0Tab = g_animCols;
    sx1Tab = g_animCols + scrW;
    stepXTab = g_animCols + 2 * (SIZE_T)scrW;

    for (x = x0; x < x1; x++)
    {
        LONG sx0 = (LONG)(((LONGLONG)(x - dstX) * srcCx) / dstCx);
        LONG sx1 = (LONG)(((LONGLONG)(x - dstX + 1) * srcCx) / dstCx);
        LONG stepX;

        if (sx1 <= sx0) sx1 = sx0 + 1;
        if (sx1 > srcCx) sx1 = srcCx;
        stepX = (sx1 - sx0 + DWM_ANIM_TAPS - 1) / DWM_ANIM_TAPS;
        if (stepX < 1) stepX = 1;
        sx0Tab[x] = sx0;
        sx1Tab[x] = sx1;
        stepXTab[x] = stepX;
    }

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
            LONG sx0 = sx0Tab[x], sx1 = sx1Tab[x], stepX = stepXTab[x];
            LONG sy, sx;
            ULONG taps = 0, rb = 0, g = 0, s, d, inverse, recip;

            for (sy = sy0; sy < sy1; sy += stepY)
            {
                const ULONG *srcrow =
                    (const ULONG *)(pix + (SIZE_T)sy * srcStride);

                for (sx = sx0; sx < sx1; sx += stepX)
                {
                    ULONG c = srcrow[sx];

                    rb += c & 0xFF00FFu;
                    g += c & 0xFF00u;
                    taps++;
                }
            }
            if (taps == 0)
                continue;
            recip = (taps <= DWM_ANIM_TAPS * DWM_ANIM_TAPS) ? TapRecip[taps]
                                                            : 65536u / taps;
            s = ((((rb >> 16) & 0xFFFFu) * recip >> 16) << 16) |
                ((((g >> 8) & 0xFFFFu) * recip >> 16) << 8) |
                ((rb & 0xFFFFu) * recip >> 16);
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
    DwmEnsureReflection(W, H);
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

                if (WaitMs != 0)
                {
                    DwmSetTimerPrecision(TRUE);
                    if (WaitForSingleObject(hStopEvent, WaitMs) == WAIT_OBJECT_0)
                        break;
                }
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

            DwmSetTimerPrecision(FALSE);
            if (WaitForMultipleObjects(ARRAYSIZE(WaitHandles), WaitHandles,
                                       FALSE, 200) == WAIT_OBJECT_0)
                break;
            continue;
        }

        {
            LONG pl, pt, pr, pb;
            LONG cl, ct, cr, cb;
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

            cl = pl; ct = pt; cr = pr; cb = pb;
            for (i = 0; i < hdr->Count; i++)
            {
                LONGLONG wl, wt, wr, wb;
                LONG margin;

                if (!DwmWindowBlursBackdrop(&wins[i]))
                    continue;
                wl = (LONGLONG)wins[i].x - g_originX;
                wt = (LONGLONG)wins[i].y - g_originY;
                wr = wl + wins[i].cx;
                wb = wt + wins[i].cy;
                if (wr <= pl || wl >= pr || wb <= pt || wt >= pb)
                    continue;
                margin = DwmBlurRadius() * DWM_BLUR_PASSES + 1;
                cl = pl - margin; ct = pt - margin;
                cr = pr + margin; cb = pb + margin;
                if (cl < 0) cl = 0;
                if (ct < 0) ct = 0;
                if (cr > g_W) cr = g_W;
                if (cb > g_H) cb = g_H;
                break;
            }

            if (cl == 0 && ct == 0 && cr == g_W && cb == g_H)
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
                        if (wl <= cl && wt <= ct && wr >= cr && wb >= cb)
                        {
                            covered = TRUE;
                            break;
                        }
                    }

                    if (!covered &&
                        !BitBlt(g_hdcComp, cl, ct, cr - cl, cb - ct,
                                g_hdcBackdrop, cl, ct, SRCCOPY))
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

                    if (DwmWindowIsHidden(wins, hdr->Count, i, cl, ct, cr, cb))
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
                                              cl, ct, cr, cb, pix,
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
                                   cl, ct, cr, cb, &wins[i]);
                    DwmBlitWindow((ULONG *)g_compBits, g_W, cl, ct, cr, cb,
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
                                      cl, ct, cr, cb, dxpix,
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

    DwmSetTimerPrecision(FALSE);
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
