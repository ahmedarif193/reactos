/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     User-mode compositor: pulls frame metadata from win32k, maps
 *              the per-window section surfaces read-only and composes them
 *              into the primary. See sdk/include/reactos/dwmframe.h.
 */

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <reactos/dwmcore.h>
#include <reactos/dwmframe.h>
#include <reactos/ntdcomp.h>

#include "dxsurface.h"
#include "gpucomp.h"
#include "presenttrace.h"
#include "settings.h"

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

static BOOL g_frameStats;
static ULONGLONG g_statBlurDownTicks, g_statBlurFilterTicks, g_statBlurUpTicks;
static void DwmStatCounter(LARGE_INTEGER *Counter);

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
    /* All three convolution surfaces are half-resolution. */
    PixelCount = (((SIZE_T)Width + 1) / 2) * (((SIZE_T)Height + 1) / 2);
    if (PixelCount > (SIZE_T)-1 / sizeof(ULONG))
        return FALSE;
    LineCount = (SIZE_T)(Width > Height ? Width : Height) +
                2 * DWM_BLUR_RADIUS_MAX + 8;
    if (LineCount > (SIZE_T)-1 / (3 * sizeof(ULONG)))
        return FALSE;
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
    /* The horizontal pass uses pixels; the vertical pass reuses this
     * scratch for three 32-bit channel sums per column. */
    Line = VirtualAlloc(NULL, LineCount * 3 * sizeof(ULONG),
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
                  ULONG * __restrict Half, LONG HalfWidth,
                  LONG Left, LONG Top, LONG Right, LONG Bottom)
{
    LONG hx, hy;
    LONG InteriorRight = Right < Width / 2 ? Right : Width / 2;

    for (hy = Top; hy < Bottom; ++hy)
    {
        LONG y0 = hy * 2, y1 = (y0 + 1 < Height) ? y0 + 1 : y0;
        const ULONG *Row0 = Composition + (SIZE_T)y0 * Width;
        const ULONG *Row1 = Composition + (SIZE_T)y1 * Width;
        ULONG *Out = Half + (SIZE_T)hy * HalfWidth;

        for (hx = Left; hx < InteriorRight; ++hx)
        {
            LONG x0 = hx * 2, x1 = x0 + 1;
            ULONG p0 = Row0[x0], p1 = Row0[x1], p2 = Row1[x0], p3 = Row1[x1];
            ULONG rb = (p0 & 0xff00ffu) + (p1 & 0xff00ffu) +
                       (p2 & 0xff00ffu) + (p3 & 0xff00ffu);
            ULONG g = (p0 & 0xff00u) + (p1 & 0xff00u) +
                      (p2 & 0xff00u) + (p3 & 0xff00u);

            Out[hx] = 0xff000000u | ((rb >> 2) & 0xff00ffu) | ((g >> 2) & 0xff00u);
        }
        /* Only an odd-width last column needs a replicated neighbour.
         * Keeping it outside the interior loop permits paired vector loads. */
        if (hx < Right)
        {
            ULONG p0 = Row0[hx * 2], p1 = Row1[hx * 2];
            ULONG rb = (p0 & 0xff00ffu) + (p1 & 0xff00ffu);
            ULONG g = (p0 & 0xff00u) + (p1 & 0xff00u);
            Out[hx] = 0xff000000u | ((rb >> 1) & 0xff00ffu) | ((g >> 1) & 0xff00u);
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

    /* Separate channel sums keep the vertical pass contiguous and permit
     * four 32-bit channels to be averaged together by the compiler. Each
     * sum is at most Divisor*255; multiplying by ceil(2^24/Divisor) fits in
     * 32 bits for every supported radius and preserves the original floor. */
    {
        ULONG * __restrict Red = Line;
        ULONG * __restrict Green = Line + (Right - Left);
        ULONG * __restrict Blue = Line + 2 * (Right - Left);
        ULONG Factor = (ULONG)Reciprocal;

        for (x = Left; x < Right; ++x)
            Red[x - Left] = Green[x - Left] = Blue[x - Left] = 0;
        for (i = -Radius; i <= Radius; ++i)
        {
            const ULONG *Row = g_blurTemp +
                (SIZE_T)DwmClampCoordinate(Top + i, Height) * Width;
            for (x = Left; x < Right; ++x)
            {
                ULONG Pixel = Row[x];
                Red[x - Left] += (Pixel >> 16) & 255;
                Green[x - Left] += (Pixel >> 8) & 255;
                Blue[x - Left] += Pixel & 255;
            }
        }
        for (y = Top; y < Bottom; ++y)
        {
            ULONG * __restrict Out = Dest + (SIZE_T)y * Width;
            const ULONG *Entering = g_blurTemp +
                (SIZE_T)DwmClampCoordinate(y + Radius + 1, Height) * Width;
            const ULONG *Leaving = g_blurTemp +
                (SIZE_T)DwmClampCoordinate(y - Radius, Height) * Width;

            for (x = Left; x < Right; ++x)
            {
                Out[x] = 0xff000000u |
                         ((Red[x - Left] * Factor) >> 24 << 16) |
                         ((Green[x - Left] * Factor) >> 24 << 8) |
                         ((Blue[x - Left] * Factor) >> 24);
            }
            if (y + 1 < Bottom)
            {
                for (x = Left; x < Right; ++x)
                {
                    ULONG In = Entering[x], Old = Leaving[x];
                    Red[x - Left] += ((In >> 16) & 255) - ((Old >> 16) & 255);
                    Green[x - Left] += ((In >> 8) & 255) - ((Old >> 8) & 255);
                    Blue[x - Left] += (In & 255) - (Old & 255);
                }
            }
        }
    }
}


/* A channel-minus-luma difference is in [-255,255]. In that domain this
 * 16-bit reciprocal gives exactly the scalar /100, including truncation
 * toward zero, while keeping the multiply in 32-bit SIMD lanes. */
static LONG
DwmSaturateDelta(LONG Delta)
{
    const ULONG Factor = (65536u * (100u + DWM_MATERIAL_SATURATION) + 99u) / 100u;
    ULONG Magnitude = Delta < 0 ? (ULONG)-Delta : (ULONG)Delta;
    LONG Value = (LONG)((Magnitude * Factor) >> 16);
    return Delta < 0 ? -Value : Value;
}

static void
DwmMaterialFinishRow(ULONG * __restrict Row, LONG Left, LONG Right, LONG y)
{
    while (Left < Right)
    {
        LONG Count = 64 - (Left & 63), i;
        const BYTE * __restrict Noise = g_noiseTile + ((y & 63) << 6) + (Left & 63);
        ULONG * __restrict Out = Row + Left;
        if (Count > Right - Left) Count = Right - Left;
        /* Contiguous noise loads let the compiler process several pixels
         * together, without a wraparound lookup in each SIMD lane. */
        for (i = 0; i < Count; ++i)
        {
            ULONG Pixel = Out[i];
            LONG Red = (Pixel >> 16) & 255, Green = (Pixel >> 8) & 255;
            LONG Blue = Pixel & 255;
            LONG Luma = (Red * 77 + Green * 151 + Blue * 28) >> 8;
            LONG Grain = (LONG)Noise[i] - DWM_MATERIAL_NOISE;
            Red = Luma + DwmSaturateDelta(Red - Luma) + Grain;
            Green = Luma + DwmSaturateDelta(Green - Luma) + Grain;
            Blue = Luma + DwmSaturateDelta(Blue - Luma) + Grain;
            if (Red < 0) Red = 0; else if (Red > 255) Red = 255;
            if (Green < 0) Green = 0; else if (Green > 255) Green = 255;
            if (Blue < 0) Blue = 0; else if (Blue > 255) Blue = 255;
            Out[i] = 0xff000000u | ((ULONG)Red << 16) | ((ULONG)Green << 8) | (ULONG)Blue;
        }
        Left += Count;
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
            /* Adjacent interior pixels share the same four samples. Keep
             * the unrounded vertical sums until the final /16, so both
             * outputs are identical to the original bilinear weights. */
            if (Alpha == 255 && x > 0 && (x & 1) && x + 1 < Right &&
                x / 2 + 1 < HalfWidth)
            {
                LONG hx = x / 2;
                ULONG p00 = Row0[hx], p10 = Row0[hx + 1];
                ULONG p01 = Row1[hx], p11 = Row1[hx + 1];
                ULONG rb0 = (p00 & 0xff00ffu) * (4 - fy) + (p01 & 0xff00ffu) * fy;
                ULONG rb1 = (p10 & 0xff00ffu) * (4 - fy) + (p11 & 0xff00ffu) * fy;
                ULONG g0 = (p00 & 0xff00u) * (4 - fy) + (p01 & 0xff00u) * fy;
                ULONG g1 = (p10 & 0xff00u) * (4 - fy) + (p11 & 0xff00u) * fy;
                ULONG First = (((rb0 * 3 + rb1) >> 4) & 0xff00ffu) |
                              (((g0 * 3 + g1) >> 4) & 0xff00u);
                ULONG Second = (((rb0 + rb1 * 3) >> 4) & 0xff00ffu) |
                               (((g0 + g1 * 3) >> 4) & 0xff00u);
                Out[x] = First;
                Out[x + 1] = Second;
                ++x;
                continue;
            }
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
        if (Alpha == 255) DwmMaterialFinishRow(Out, Left, Right, y);
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

/*
 * Which blur path won on this machine: negative while still measuring, 0 for
 * software, positive for hardware. Small taskbar strips and large window
 * backdrops have separate decisions because GPU setup and transfer costs do
 * not scale like the software filter.
 */
#define DWM_BLUR_CALIBRATION_SAMPLES 12
#define DWM_BLUR_LARGE_PIXELS 65536
static int g_blurUseGpu = -1;
static int g_blurLargeUseGpu = -1;
static ULONG g_blurCalibrationFrames;
static ULONG g_blurLargeCalibrationFrames;
static ULONGLONG g_blurGpuTicks, g_blurGpuPixels;
static ULONGLONG g_blurCpuTicks, g_blurCpuPixels;
static ULONGLONG g_blurLargeGpuTicks, g_blurLargeGpuPixels;
static ULONGLONG g_blurLargeCpuTicks, g_blurLargeCpuPixels;
static ULONG g_blurGpuDeclines;
static ULONG g_blurLargeGpuDeclines;
static BOOL g_blurGpuWarmed;

/* The software cascade, factored out so both callers run the same code. */
static const ULONG *
DwmBlurSoftwarePasses(LONG hL, LONG hT, LONG hR, LONG hB,
                      LONG HalfWidth, LONG HalfHeight, LONG HalfRadius)
{
    const ULONG *Result = g_blurSource;
    LONG Pass;

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
    return Result;
}

static RECTL
DwmBlurHalfBounds(const RECTL *Rect, LONG Grow, LONG Width, LONG Height)
{
    /* Bilinear upsampling reads the neighbour on either side of the output
     * interval. Include it before adding the convolution's dependency halo. */
    RECTL Half = {Rect->left / 2 - 1 - Grow,
                  Rect->top / 2 - 1 - Grow,
                  (Rect->right + 1) / 2 + 1 + Grow,
                  (Rect->bottom + 1) / 2 + 1 + Grow};

    if (Half.left < 0) Half.left = 0;
    if (Half.top < 0) Half.top = 0;
    if (Half.right > Width) Half.right = Width;
    if (Half.bottom > Height) Half.bottom = Height;
    return Half;
}

static const ULONG *
DwmBlurSoftwareRegions(const ULONG *Composition, LONG Width, LONG Height,
                        const RECTL *Regions, ULONG Count, LONG HalfRadius)
{
    LONG HalfWidth = (Width + 1) / 2, HalfHeight = (Height + 1) / 2;
    const ULONG *Result = g_blurSource;
    LONG Pass;
    ULONG Index;

    /* Snapshot every input before filtering. Processing one strip all the
     * way through upsampling first would blur its neighbours a second time. */
    for (Index = 0; Index < Count; ++Index)
    {
        RECTL Half = DwmBlurHalfBounds(&Regions[Index],
                                       HalfRadius * DWM_BLUR_PASSES,
                                       HalfWidth, HalfHeight);
        DwmDownsampleHalf(Composition, Width, Height, g_blurSource, HalfWidth,
                          Half.left, Half.top, Half.right, Half.bottom);
    }
    for (Pass = DWM_BLUR_PASSES - 1; Pass >= 0; --Pass)
    {
        ULONG *Dest = (Result == g_blurSource) ? g_blurPass : g_blurSource;

        for (Index = 0; Index < Count; ++Index)
        {
            RECTL Half = DwmBlurHalfBounds(&Regions[Index], HalfRadius * Pass,
                                           HalfWidth, HalfHeight);
            DwmBoxBlurPass(Result, Dest, HalfWidth, HalfHeight,
                           Half.left, Half.top, Half.right, Half.bottom,
                           HalfRadius);
        }
        Result = Dest;
    }
    return Result;
}

/*
 * Accumulates one timing sample and, once both paths have been exercised
 * enough, latches the faster.  Comparing time per pixel rather than per frame
 * keeps regions of different sizes commensurable.
 */
static void
DwmBlurRecordSample(BOOL Gpu, BOOL Large, const RECT *Region, LONGLONG Ticks)
{
    ULONGLONG Pixels;
    int *UseGpu = Large ? &g_blurLargeUseGpu : &g_blurUseGpu;
    ULONG *Frames = Large ? &g_blurLargeCalibrationFrames :
                            &g_blurCalibrationFrames;
    ULONGLONG *GpuTicks = Large ? &g_blurLargeGpuTicks : &g_blurGpuTicks;
    ULONGLONG *GpuPixels = Large ? &g_blurLargeGpuPixels : &g_blurGpuPixels;
    ULONGLONG *CpuTicks = Large ? &g_blurLargeCpuTicks : &g_blurCpuTicks;
    ULONGLONG *CpuPixels = Large ? &g_blurLargeCpuPixels : &g_blurCpuPixels;

    if (Ticks < 0)
        return;
    Pixels = (ULONGLONG)(Region->right - Region->left) *
             (ULONGLONG)(Region->bottom - Region->top);
    if (Pixels == 0)
        return;

    if (Gpu)
    {
        *GpuTicks += (ULONGLONG)Ticks;
        *GpuPixels += Pixels;
    }
    else
    {
        *CpuTicks += (ULONGLONG)Ticks;
        *CpuPixels += Pixels;
    }
    ++*Frames;

    if (*Frames < DWM_BLUR_CALIBRATION_SAMPLES ||
        *GpuPixels == 0 || *CpuPixels == 0)
    {
        return;
    }

    /*
     * Cross-multiplied so the comparison stays in integers:
     *   gpuTicks/gpuPixels < cpuTicks/cpuPixels
     */
    if (*GpuTicks * *CpuPixels < *CpuTicks * *GpuPixels)
    {
        *UseGpu = 1;
        OutputDebugStringA(Large ?
            "DWM: large blur path = GPU (measured faster)\n" :
            "DWM: small blur path = GPU (measured faster)\n");
    }
    else
    {
        *UseGpu = 0;
        OutputDebugStringA(Large ?
            "DWM: large blur path = software (measured faster)\n" :
            "DWM: small blur path = software (measured faster)\n");
    }
}

static void
DwmApplyBlur(const ULONG *Input, ULONG *Composition, LONG Width, LONG Height,
             LONG ClipLeft, LONG ClipTop, LONG ClipRight, LONG ClipBottom,
             const DWM_WIN *Window, const RECTL *Rectangles)
{
    RECTL Entire = {0, 0, Window->cx, Window->cy};
    RECTL Union = {0, 0, 0, 0}, Clipped;
    RECTL Regions[4];
    const RECTL *Rectangle;
    ULONG Index, Count, Alpha = 255;
    LONG Radius, HalfRadius, HalfWidth, HalfHeight, WindowX, WindowY;
    LONG hL, hT, hR, hB, Reach;
    const ULONG *Result = NULL;
    BOOL HaveUnion = FALSE;
    ULONG RegionCount = 0;
    LARGE_INTEGER BlurStart, BlurDown, BlurFilter, BlurEnd;

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
        if (RegionCount < ARRAYSIZE(Regions))
            Regions[RegionCount++] = Clipped;
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

    DwmStatCounter(&BlurStart);
    BlurDown = BlurStart;

    /* Non-client glass is four thin strips, not the opaque client between
     * them. Filter the strips separately when their halos cost less than the
     * bounding box. Keep the existing GPU calibration for dense regions. */
    if (Count <= ARRAYSIZE(Regions) && RegionCount > 1 &&
        (g_blurUseGpu == 0 || !DwmGpuIsActive()))
    {
        ULONGLONG SparsePixels = 0;
        RECTL Dense = DwmBlurHalfBounds(&Union,
                                        HalfRadius * DWM_BLUR_PASSES,
                                        HalfWidth, HalfHeight);

        for (Index = 0; Index < RegionCount; ++Index)
        {
            RECTL Half = DwmBlurHalfBounds(&Regions[Index],
                                           HalfRadius * DWM_BLUR_PASSES,
                                           HalfWidth, HalfHeight);
            SparsePixels += (ULONGLONG)(Half.right - Half.left) *
                            (Half.bottom - Half.top);
        }
        if (SparsePixels < (ULONGLONG)(Dense.right - Dense.left) *
                           (Dense.bottom - Dense.top))
        {
            Result = DwmBlurSoftwareRegions(Input, Width, Height,
                                             Regions, RegionCount, HalfRadius);
            goto Upsample;
        }
    }

    Clipped = DwmBlurHalfBounds(&Union, 0, HalfWidth, HalfHeight);
    hL = Clipped.left;
    hT = Clipped.top;
    hR = Clipped.right;
    hB = Clipped.bottom;
    Reach = HalfRadius * DWM_BLUR_PASSES + 2;
    DwmDownsampleHalf(Input, Width, Height, g_blurSource, HalfWidth,
                      (hL - Reach < 0) ? 0 : hL - Reach,
                      (hT - Reach < 0) ? 0 : hT - Reach,
                      (hR + Reach > HalfWidth) ? HalfWidth : hR + Reach,
                      (hB + Reach > HalfHeight) ? HalfHeight : hB + Reach);

    DwmStatCounter(&BlurDown);

    /*
     * The software passes below are a sliding-window box blur: O(1) work per
     * pixel per axis, roughly six integer operations, independent of radius.
     * That is already cheap, so a GPU is not automatically the faster choice
     * here -- the hardware path has to pay an upload and a readback per
     * frame, which on a small GPU with uncached memory can cost more than
     * the convolution it saves.
     *
     * Rather than assume either way, measure both on this machine: spend the
     * first frames alternating between the two, accumulate time per pixel,
     * then latch whichever actually won and use it from then on.  Both paths
     * produce a blur, so the alternation is not visible.  The software path
     * remains the fallback and is what runs whenever the hardware path is
     * absent, declines a region, or loses the comparison.
     */
    {
        LONG GpuLeft = hL - HalfRadius * DWM_BLUR_PASSES;
        LONG GpuTop = hT - HalfRadius * DWM_BLUR_PASSES;
        LONG GpuRight = hR + HalfRadius * DWM_BLUR_PASSES;
        LONG GpuBottom = hB + HalfRadius * DWM_BLUR_PASSES;
        RECT GpuRect;
        BOOL TryGpu;
        BOOL Large;
        int *UseGpu;
        ULONG *CalibrationFrames, *GpuDeclines;

        if (GpuLeft < 0) GpuLeft = 0;
        if (GpuTop < 0) GpuTop = 0;
        if (GpuRight > HalfWidth) GpuRight = HalfWidth;
        if (GpuBottom > HalfHeight) GpuBottom = HalfHeight;
        GpuRect.left = GpuLeft;
        GpuRect.top = GpuTop;
        GpuRect.right = GpuRight;
        GpuRect.bottom = GpuBottom;
        Large = (ULONGLONG)(GpuRight - GpuLeft) *
                (ULONGLONG)(GpuBottom - GpuTop) >= DWM_BLUR_LARGE_PIXELS;
        UseGpu = Large ? &g_blurLargeUseGpu : &g_blurUseGpu;
        CalibrationFrames = Large ? &g_blurLargeCalibrationFrames :
                                    &g_blurCalibrationFrames;
        GpuDeclines = Large ? &g_blurLargeGpuDeclines : &g_blurGpuDeclines;

        if (!DwmGpuIsActive())
            TryGpu = FALSE;
        else if (*UseGpu > 0)
            TryGpu = TRUE;
        else if (*UseGpu == 0)
            TryGpu = FALSE;
        else
            TryGpu = (*CalibrationFrames & 1) != 0;

        if (TryGpu)
        {
            LARGE_INTEGER Start, End;
            BOOL Blurred;

            QueryPerformanceCounter(&Start);
            /* Match the reach of the box cascade so both paths look alike. */
            Blurred = DwmGpuBlurRect(g_blurSource, HalfWidth, HalfHeight,
                                     &GpuRect,
                                     (ULONG)(HalfRadius * DWM_BLUR_PASSES));
            QueryPerformanceCounter(&End);
            if (Blurred)
            {
                if (*UseGpu < 0)
                {
                    /* The first hardware call allocates persistent VC4
                     * resources. Exclude that one-time cost from the path
                     * decision, then immediately retry a measured sample. */
                    if (!g_blurGpuWarmed)
                    {
                        g_blurGpuWarmed = TRUE;
                        OutputDebugStringA(
                            "DWM: GPU blur warmup complete (sample ignored)\n");
                    }
                    else
                    {
                        DwmBlurRecordSample(TRUE, Large, &GpuRect,
                                            End.QuadPart - Start.QuadPart);
                    }
                }
                /* The hardware path blurs g_blurSource in place. */
                Result = g_blurSource;
                goto Upsample;
            }
            /*
             * Declined.  Every attempt still costs a context switch, and a
             * hardware path that never accepts a region would otherwise keep
             * the comparison open forever, since it contributes no samples.
             * Give up on it after a bounded number of refusals.
             */
            if (*UseGpu < 0 &&
                ++*GpuDeclines >= DWM_BLUR_CALIBRATION_SAMPLES)
            {
                *UseGpu = 0;
                OutputDebugStringA(Large ?
                    "DWM: large blur path = software (hardware declined)\n" :
                    "DWM: small blur path = software (hardware declined)\n");
            }
        }

        if (*UseGpu < 0 && DwmGpuIsActive())
        {
            LARGE_INTEGER Start, End;

            QueryPerformanceCounter(&Start);
            Result = DwmBlurSoftwarePasses(hL, hT, hR, hB, HalfWidth,
                                           HalfHeight, HalfRadius);
            QueryPerformanceCounter(&End);
            DwmBlurRecordSample(FALSE, Large, &GpuRect,
                                End.QuadPart - Start.QuadPart);
            goto Upsample;
        }
    }

    Result = DwmBlurSoftwarePasses(hL, hT, hR, hB, HalfWidth, HalfHeight,
                                   HalfRadius);

Upsample:
    DwmStatCounter(&BlurFilter);

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
    DwmStatCounter(&BlurEnd);
    if (g_frameStats)
    {
        g_statBlurDownTicks += BlurDown.QuadPart - BlurStart.QuadPart;
        g_statBlurFilterTicks += BlurFilter.QuadPart - BlurDown.QuadPart;
        g_statBlurUpTicks += BlurEnd.QuadPart - BlurFilter.QuadPart;
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

/* Retain the last dense, screen-space backdrop blur. During a move, nearly
 * all of the new glass overlaps the preceding frame and the pixels below it
 * are usually identical. The cache compares those source pixels exactly;
 * changed content therefore takes the ordinary blur path with no stale
 * result. A source snapshot also lets the exposed edge strips be filtered
 * independently without one strip feeding another one's convolution. */
static ULONG *g_backdropCacheInput, *g_backdropCacheOutput;
static LONG g_backdropCacheWidth, g_backdropCacheHeight;
static LONG g_backdropCacheRadius;
static RECTL g_backdropCacheBounds, g_backdropCacheSource;
static BOOL g_backdropCacheValid;
static DWM_WIN g_backdropCacheLower[DWM_MAX_WINDOWS];
static ULONG g_backdropCacheLowerCount;
static ULONG g_backdropCacheOwnerId, g_backdropCacheOwnerGeneration;
static ULONGLONG g_statBackdropCacheHits, g_statBackdropCachePartials;
static ULONGLONG g_statBackdropCacheMisses;
static ULONGLONG g_statBackdropMetadataHits;

static void
DwmFreeBackdropCache(void)
{
    if (g_backdropCacheInput != NULL)
        VirtualFree(g_backdropCacheInput, 0, MEM_RELEASE);
    if (g_backdropCacheOutput != NULL)
        VirtualFree(g_backdropCacheOutput, 0, MEM_RELEASE);
    g_backdropCacheInput = g_backdropCacheOutput = NULL;
    g_backdropCacheValid = FALSE;
    g_backdropCacheWidth = g_backdropCacheHeight = 0;
    g_backdropCacheLowerCount = 0;
}

static BOOL
DwmEnsureBackdropCache(LONG Width, LONG Height)
{
    SIZE_T Pixels, Bytes;

    if (Width <= 0 || Height <= 0 ||
        (SIZE_T)Width > (SIZE_T)-1 / (SIZE_T)Height)
        return FALSE;
    if (g_backdropCacheInput != NULL && g_backdropCacheOutput != NULL &&
        g_backdropCacheWidth == Width && g_backdropCacheHeight == Height)
        return TRUE;
    DwmFreeBackdropCache();
    Pixels = (SIZE_T)Width * Height;
    if (Pixels > (SIZE_T)-1 / sizeof(ULONG))
        return FALSE;
    Bytes = Pixels * sizeof(ULONG);
    g_backdropCacheInput = VirtualAlloc(NULL, Bytes,
                                        MEM_COMMIT | MEM_RESERVE,
                                        PAGE_READWRITE);
    g_backdropCacheOutput = VirtualAlloc(NULL, Bytes,
                                         MEM_COMMIT | MEM_RESERVE,
                                         PAGE_READWRITE);
    if (g_backdropCacheInput == NULL || g_backdropCacheOutput == NULL)
    {
        DwmFreeBackdropCache();
        return FALSE;
    }
    g_backdropCacheWidth = Width;
    g_backdropCacheHeight = Height;
    return TRUE;
}

static RECTL
DwmBackdropSourceBounds(const RECTL *Output, LONG Radius,
                        LONG Width, LONG Height)
{
    LONG HalfRadius = (Radius + 1) / 2;
    LONG Margin = 2 * (HalfRadius * DWM_BLUR_PASSES + 3);
    RECTL Source = {Output->left - Margin, Output->top - Margin,
                    Output->right + Margin, Output->bottom + Margin};

    if (Source.left < 0) Source.left = 0;
    if (Source.top < 0) Source.top = 0;
    if (Source.right > Width) Source.right = Width;
    if (Source.bottom > Height) Source.bottom = Height;
    return Source;
}

static BOOL
DwmRectContains(const RECTL *Outer, const RECTL *Inner)
{
    return Inner->left >= Outer->left && Inner->top >= Outer->top &&
           Inner->right <= Outer->right && Inner->bottom <= Outer->bottom;
}

static void
DwmCopyBackdropRect(ULONG *Dest, const ULONG *Source, LONG Width,
                    const RECTL *Rect)
{
    LONG y;
    SIZE_T Bytes = (SIZE_T)(Rect->right - Rect->left) * sizeof(ULONG);

    for (y = Rect->top; y < Rect->bottom; ++y)
    {
        SIZE_T Offset = (SIZE_T)y * Width + Rect->left;
        RtlCopyMemory(Dest + Offset, Source + Offset, Bytes);
    }
}

static void
DwmCopyBackdropDifference(ULONG *Dest, const ULONG *Source, LONG Width,
                          const RECTL *Current, const RECTL *Overlap)
{
    RECTL Missing[4] = {
        {Current->left, Current->top, Current->right, Overlap->top},
        {Current->left, Overlap->bottom, Current->right, Current->bottom},
        {Current->left, Overlap->top, Overlap->left, Overlap->bottom},
        {Overlap->right, Overlap->top, Current->right, Overlap->bottom}
    };
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Missing); ++Index)
    {
        if (Missing[Index].right > Missing[Index].left &&
            Missing[Index].bottom > Missing[Index].top)
            DwmCopyBackdropRect(Dest, Source, Width, &Missing[Index]);
    }
}

static BOOL
DwmBackdropInputEqual(const ULONG *Current, LONG Width, const RECTL *Rect)
{
    LONG y;
    SIZE_T Bytes = (SIZE_T)(Rect->right - Rect->left) * sizeof(ULONG);

    for (y = Rect->top; y < Rect->bottom; ++y)
    {
        SIZE_T Offset = (SIZE_T)y * Width + Rect->left;
        if (memcmp(Current + Offset, g_backdropCacheInput + Offset, Bytes))
            return FALSE;
    }
    return TRUE;
}

static BOOL
DwmBackdropSceneEqual(const DWM_WIN *Window, const DWM_WIN *Lower,
                      ULONG LowerCount)
{
    ULONG Index;

    if (Window == NULL || Lower == NULL || LowerCount > DWM_MAX_WINDOWS ||
        Window->SurfaceId != g_backdropCacheOwnerId ||
        Window->Generation != g_backdropCacheOwnerGeneration ||
        LowerCount != g_backdropCacheLowerCount)
        return FALSE;
    for (Index = 0; Index < LowerCount; ++Index)
    {
        DWM_WIN Current = Lower[Index];

        /* Update IDs cover shared and section-backed surfaces. Retain the
         * damage check for metadata changes. Custom blur rectangles are not
         * copied into this small snapshot and retain the pixel comparison. */
        if (Current.Damaged || Current.BlurRectCount != 0)
            return FALSE;
        Current.Damaged = 0;
        if (memcmp(&Current, &g_backdropCacheLower[Index], sizeof(Current)))
            return FALSE;
    }
    return TRUE;
}

static void
DwmBackdropStoreScene(const DWM_WIN *Window, const DWM_WIN *Lower,
                      ULONG LowerCount)
{
    ULONG Index;

    if (Window == NULL || LowerCount > DWM_MAX_WINDOWS)
    {
        g_backdropCacheLowerCount = 0;
        return;
    }
    for (Index = 0; Index < LowerCount; ++Index)
    {
        g_backdropCacheLower[Index] = Lower[Index];
        g_backdropCacheLower[Index].Damaged = 0;
    }
    g_backdropCacheOwnerId = Window->SurfaceId;
    g_backdropCacheOwnerGeneration = Window->Generation;
    g_backdropCacheLowerCount = LowerCount;
}

static void
DwmCopyBackdropShape(ULONG *Dest, const ULONG *Source, LONG Width, LONG Height,
                     LONG ClipLeft, LONG ClipTop, LONG ClipRight,
                     LONG ClipBottom, const DWM_WIN *Window,
                     const RECTL *Rectangles)
{
    RECTL Entire = {0, 0, Window->cx, Window->cy};
    const RECTL *Rectangle;
    RECTL Clipped;
    ULONG Count, Index;
    LONG WindowX = Window->x - g_originX;
    LONG WindowY = Window->y - g_originY;

    if (Window->BlurFlags & DWM_BLUR_REGION_ENTIRE_WINDOW)
    {
        Rectangle = &Entire;
        Count = 1;
    }
    else
    {
        Rectangle = Rectangles;
        Count = Window->BlurRectCount;
    }
    for (Index = 0; Rectangle != NULL && Index < Count; ++Index)
    {
        if (DwmClipBlurRect(&Rectangle[Index], WindowX, WindowY, Window,
                            ClipLeft, ClipTop, ClipRight, ClipBottom,
                            Width, Height, &Clipped))
            DwmCopyBackdropRect(Dest, Source, Width, &Clipped);
    }
}

static BOOL
DwmBackdropBlurBounds(LONG Width, LONG Height,
                      LONG ClipLeft, LONG ClipTop, LONG ClipRight,
                      LONG ClipBottom, const DWM_WIN *Window,
                      const RECTL *Rectangles, RECTL *Union,
                      ULONGLONG *Pixels)
{
    RECTL Entire = {0, 0, Window->cx, Window->cy};
    const RECTL *Rectangle;
    RECTL Clipped;
    ULONG Count, Index;
    LONG WindowX = Window->x - g_originX;
    LONG WindowY = Window->y - g_originY;
    BOOL Found = FALSE;

    *Pixels = 0;
    if (Window->BlurFlags & DWM_BLUR_REGION_ENTIRE_WINDOW)
    {
        Rectangle = &Entire;
        Count = 1;
    }
    else
    {
        Rectangle = Rectangles;
        Count = Window->BlurRectCount;
    }
    for (Index = 0; Rectangle != NULL && Index < Count; ++Index)
    {
        if (!DwmClipBlurRect(&Rectangle[Index], WindowX, WindowY, Window,
                             ClipLeft, ClipTop, ClipRight, ClipBottom,
                             Width, Height, &Clipped))
            continue;
        *Pixels += (ULONGLONG)(Clipped.right - Clipped.left) *
                   (ULONGLONG)(Clipped.bottom - Clipped.top);
        if (!Found)
        {
            *Union = Clipped;
            Found = TRUE;
        }
        else
        {
            if (Clipped.left < Union->left) Union->left = Clipped.left;
            if (Clipped.top < Union->top) Union->top = Clipped.top;
            if (Clipped.right > Union->right) Union->right = Clipped.right;
            if (Clipped.bottom > Union->bottom) Union->bottom = Clipped.bottom;
        }
    }
    return Found;
}

static const ULONG *
DwmApplyBackdropBlurCached(ULONG *Composition, LONG Width, LONG Height,
                           LONG ClipLeft, LONG ClipTop, LONG ClipRight,
                           LONG ClipBottom, const DWM_WIN *Window,
                           const RECTL *Rectangles, BOOL Direct,
                           const DWM_WIN *Lower, ULONG LowerCount)
{
    RECTL Current, CurrentSource, Overlap, OverlapSource, Missing[4];
    DWM_WIN CacheWindow;
    ULONGLONG Pixels, CurrentArea, OverlapArea;
    LONG Radius = DwmBlurRadius();
    ULONG Index;
    BOOL Reuse = FALSE;
    BOOL SceneEqual = FALSE;

    if (!DwmBackdropBlurBounds(Width, Height, ClipLeft, ClipTop,
                               ClipRight, ClipBottom, Window, Rectangles,
                               &Current, &Pixels))
        return NULL;
    CurrentArea = (ULONGLONG)(Current.right - Current.left) *
                  (ULONGLONG)(Current.bottom - Current.top);
    if (((Window->LayerFlags & DWM_LWA_ALPHA) && Window->Alpha < 255) ||
        (Pixels >= DWM_BLUR_LARGE_PIXELS * 4ull ?
             g_blurLargeUseGpu : g_blurUseGpu) != 0 || Pixels < 65536 ||
        Pixels < CurrentArea / 2 || !DwmEnsureBackdropCache(Width, Height))
    {
        DwmApplyBlur(Composition, Composition, Width, Height,
                     ClipLeft, ClipTop, ClipRight, ClipBottom,
                     Window, Rectangles);
        return NULL;
    }

    Overlap.left = Current.left > g_backdropCacheBounds.left ?
                   Current.left : g_backdropCacheBounds.left;
    Overlap.top = Current.top > g_backdropCacheBounds.top ?
                  Current.top : g_backdropCacheBounds.top;
    Overlap.right = Current.right < g_backdropCacheBounds.right ?
                    Current.right : g_backdropCacheBounds.right;
    Overlap.bottom = Current.bottom < g_backdropCacheBounds.bottom ?
                     Current.bottom : g_backdropCacheBounds.bottom;
    if (g_backdropCacheValid && g_backdropCacheRadius == Radius &&
        Overlap.right > Overlap.left && Overlap.bottom > Overlap.top)
    {
        OverlapArea = (ULONGLONG)(Overlap.right - Overlap.left) *
                      (ULONGLONG)(Overlap.bottom - Overlap.top);
        OverlapSource = DwmBackdropSourceBounds(&Overlap, Radius,
                                                Width, Height);
        SceneEqual = DwmBackdropSceneEqual(Window, Lower, LowerCount);
        Reuse = OverlapArea * 2 >= CurrentArea &&
                DwmRectContains(&g_backdropCacheSource, &OverlapSource) &&
                (SceneEqual ||
                 DwmBackdropInputEqual(Composition, Width, &OverlapSource));
        if (Reuse && SceneEqual && g_frameStats)
            ++g_statBackdropMetadataHits;
    }
    CurrentSource = DwmBackdropSourceBounds(&Current, Radius, Width, Height);
    if (Reuse && DwmRectContains(&g_backdropCacheBounds, &Current))
    {
        DwmBackdropStoreScene(Window, Lower, LowerCount);
        if (g_frameStats) ++g_statBackdropCacheHits;
        if (Direct)
            return g_backdropCacheOutput;
        DwmCopyBackdropShape(Composition, g_backdropCacheOutput, Width, Height,
                             ClipLeft, ClipTop, ClipRight, ClipBottom,
                             Window, Rectangles);
        return NULL;
    }

    /* Preserve the complete lower-layer input before either cached pixels or
     * newly filtered strips are written into the composition surface. */
    if (Reuse)
        DwmCopyBackdropDifference(g_backdropCacheInput, Composition, Width,
                                  &CurrentSource, &OverlapSource);
    else
        DwmCopyBackdropRect(g_backdropCacheInput, Composition, Width,
                            &CurrentSource);
    CacheWindow = *Window;
    CacheWindow.BlurFlags = DWM_BLUR_ENABLE |
                            DWM_BLUR_REGION_ENTIRE_WINDOW;
    CacheWindow.BlurRectCount = 0;
    if (!Reuse)
    {
        DwmApplyBlur(g_backdropCacheInput, g_backdropCacheOutput,
                     Width, Height, Current.left, Current.top,
                     Current.right, Current.bottom, &CacheWindow, NULL);
        if (g_frameStats) ++g_statBackdropCacheMisses;
    }
    else
    {
        Missing[0] = (RECTL){Current.left, Current.top,
                             Current.right, Overlap.top};
        Missing[1] = (RECTL){Current.left, Overlap.bottom,
                             Current.right, Current.bottom};
        Missing[2] = (RECTL){Current.left, Overlap.top,
                             Overlap.left, Overlap.bottom};
        Missing[3] = (RECTL){Overlap.right, Overlap.top,
                             Current.right, Overlap.bottom};
        for (Index = 0; Index < ARRAYSIZE(Missing); ++Index)
        {
            if (Missing[Index].right <= Missing[Index].left ||
                Missing[Index].bottom <= Missing[Index].top)
                continue;
            DwmApplyBlur(g_backdropCacheInput, g_backdropCacheOutput,
                         Width, Height,
                         Missing[Index].left, Missing[Index].top,
                         Missing[Index].right, Missing[Index].bottom,
                         &CacheWindow, NULL);
        }
        if (g_frameStats) ++g_statBackdropCachePartials;
    }
    g_backdropCacheBounds = Current;
    g_backdropCacheSource = CurrentSource;
    g_backdropCacheRadius = Radius;
    g_backdropCacheValid = TRUE;
    DwmBackdropStoreScene(Window, Lower, LowerCount);
    if (Direct)
        return g_backdropCacheOutput;
    DwmCopyBackdropShape(Composition, g_backdropCacheOutput, Width, Height,
                         ClipLeft, ClipTop, ClipRight, ClipBottom,
                         Window, Rectangles);
    return NULL;
}

static const ULONG *
DwmApplyBackdropBlur(ULONG *Composition, LONG Width, LONG Height,
                     LONG ClipLeft, LONG ClipTop, LONG ClipRight,
                     LONG ClipBottom, const DWM_WIN *Window,
                     const DWM_WIN *Lower, ULONG LowerCount)
{
    DWM_WIN BlurWindow;
    RECTL Rectangles[4];
    LONG NcBottom;

    if (Window->BackdropType != DWM_BACKDROP_TRANSIENT)
        return NULL;

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
                return DwmApplyBackdropBlurCached(
                    Composition, Width, Height, ClipLeft, ClipTop,
                    ClipRight, ClipBottom, &BlurWindow, Rounded,
                    !(Window->LayerFlags & (DWM_LWA_ALPHA |
                                             DWM_LWA_COLORKEY)) &&
                    !(Window->BlurFlags & DWM_BLUR_ENABLE),
                    Lower, LowerCount);
            }
        }
        BlurWindow.BlurFlags |= DWM_BLUR_REGION_ENTIRE_WINDOW;
        BlurWindow.BlurRectCount = 0;
        return DwmApplyBackdropBlurCached(
            Composition, Width, Height, ClipLeft, ClipTop, ClipRight,
            ClipBottom, &BlurWindow, NULL,
            !(Window->LayerFlags & (DWM_LWA_ALPHA | DWM_LWA_COLORKEY)) &&
            !(Window->BlurFlags & DWM_BLUR_ENABLE), Lower, LowerCount);
    }
    if (Window->BackdropRegion != DWM_BACKDROP_REGION_NONCLIENT)
        return NULL;

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
    DwmApplyBackdropBlurCached(Composition, Width, Height,
                               ClipLeft, ClipTop, ClipRight, ClipBottom,
                               &BlurWindow, Rectangles, FALSE,
                               Lower, LowerCount);
    return NULL;
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

static BOOL
DwmBlendShadow(ULONG *comp, LONG scrW, LONG scrH,
               LONG clipL, LONG clipT, LONG clipR, LONG clipB,
               const DWM_WIN *w)
{
    DWM_WIN AnimatedWindow;

    if (w->AnimFlags != 0)
    {
        if (w->AnimCx <= 0 || w->AnimCy <= 0)
            return TRUE;
        AnimatedWindow = *w;
        AnimatedWindow.x = w->AnimX;
        AnimatedWindow.y = w->AnimY;
        AnimatedWindow.cx = w->AnimCx;
        AnimatedWindow.cy = w->AnimCy;
        w = &AnimatedWindow;
    }

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
        return TRUE;

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
            return TRUE;
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
        return TRUE;

    x0 = (shadowLeft < clipL) ? clipL : (LONG)shadowLeft;
    x1 = (shadowRight > clipR) ? clipR : (LONG)shadowRight;
    y0 = (shadowTop < clipT) ? clipT : (LONG)shadowTop;
    y1 = (shadowBottom > clipB) ? clipB : (LONG)shadowBottom;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > scrW) x1 = scrW;
    if (y1 > scrH) y1 = scrH;
    if (x1 <= x0 || y1 <= y0)
        return TRUE;

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

    if (DwmGpuComposeIsActive())
    {
        RECT Bounds = {x0, y0, x1, y1};
        return DwmGpuComposeShadow(&Bounds, ownerX, ownerY, w->cx, w->cy,
                                  verticalOffset, g_shadowMarginLeft, active,
                                  wideOpacity, tightOpacity, windowAlpha);
    }

    if (!DwmEnsureShadowCoverage(scrW))
        return TRUE;
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
        return TRUE;

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
    return TRUE;
}


static ULONGLONG g_statGlassPixels, g_statBlitPixels;
static ULONGLONG g_statConstantGlassPixels, g_statFullWeightGlassPixels;

/* Exact signed /255 for every channel delta times an opacity.  Keeping this
 * in 32-bit arithmetic lets the ARM64 compiler process four pixels per
 * vector instead of widening each product to 64 bits. */
static LONG
DwmDivideChannel255(LONG Value)
{
    ULONG Magnitude = Value < 0 ? (ULONG)-Value : (ULONG)Value;
    LONG Quotient = (LONG)((Magnitude * 65794u) >> 24);

    return Value < 0 ? -Quotient : Quotient;
}

/* Uniform glass runs share all material classification. Keep the signed
 * division used by the scalar path: a weighted average rounds differently
 * when the backdrop channel is darker than the tint. */
static void
DwmBlendConstantGlass(ULONG * __restrict Dest,
                      const ULONG * __restrict Base, LONG Count,
                      ULONG Color, ULONG Key, ULONG Opacity, ULONG Weight,
                      const BYTE * __restrict Reflection)
{
    LONG Red = (Color >> 16) & 255, Green = (Color >> 8) & 255;
    LONG Blue = Color & 255;
    LONG KeyRed = (Key >> 16) & 255, KeyGreen = (Key >> 8) & 255;
    LONG KeyBlue = Key & 255;
    LONG Shift = (255u - Opacity) * Weight / 255u;
    LONG Index;

    for (Index = 0; Index < Count; ++Index)
    {
        ULONG Under = Base[Index];
        LONG Glow = DWM_REFLECT_STRENGTH * Reflection[Index] * Weight / (255u * 255u);
        LONG R = Red + DwmDivideChannel255(
            ((LONG)((Under >> 16) & 255) - KeyRed) * Shift) + Glow;
        LONG G = Green + DwmDivideChannel255(
            ((LONG)((Under >> 8) & 255) - KeyGreen) * Shift) + Glow;
        LONG B = Blue + DwmDivideChannel255(
            ((LONG)(Under & 255) - KeyBlue) * Shift) + Glow;

        if (R < 0) R = 0; else if (R > 255) R = 255;
        if (G < 0) G = 0; else if (G > 255) G = 255;
        if (B < 0) B = 0; else if (B > 255) B = 255;
        Dest[Index] = ((ULONG)R << 16) | ((ULONG)G << 8) | (ULONG)B;
    }
}

static void
DwmBlendGlassSpan(ULONG * __restrict Dest, const ULONG * __restrict Base,
                  const ULONG * __restrict Source,
                  LONG Count, ULONG BackdropKey, ULONG ColorizationKey,
                  ULONG Opacity, const BYTE * __restrict Reflection)
{
    LONG Index;

    for (Index = 0; Index < Count; ++Index)
    {
        ULONG Color = Source[Index], Under = Base[Index];
        ULONG Near = DwmChannelDistance(Color, BackdropKey);
        ULONG Other = DwmChannelDistance(Color, ColorizationKey);
        ULONG Key = Other < Near ? ColorizationKey : BackdropKey;
        ULONG Weight, Shift;
        LONG Red, Green, Blue, Glow;

        if (Other < Near) Near = Other;
        Weight = Near <= DWM_MATERIAL_TINT_BAND ? 255 :
                 Near < DWM_MATERIAL_FRINGE ?
                 (DWM_MATERIAL_FRINGE - Near) * 255u /
                 (DWM_MATERIAL_FRINGE - DWM_MATERIAL_TINT_BAND) : 0;
        Shift = (255u - Opacity) * Weight / 255u;
        Glow = DWM_REFLECT_STRENGTH * Reflection[Index] * Weight /
               (255u * 255u);
        Red = ((Color >> 16) & 255) + DwmDivideChannel255(
            ((LONG)((Under >> 16) & 255) -
             (LONG)((Key >> 16) & 255)) * (LONG)Shift) + Glow;
        Green = ((Color >> 8) & 255) + DwmDivideChannel255(
            ((LONG)((Under >> 8) & 255) -
             (LONG)((Key >> 8) & 255)) * (LONG)Shift) + Glow;
        Blue = (Color & 255) + DwmDivideChannel255(
            ((LONG)(Under & 255) - (LONG)(Key & 255)) * (LONG)Shift) + Glow;
        if (Red < 0) Red = 0; else if (Red > 255) Red = 255;
        if (Green < 0) Green = 0; else if (Green > 255) Green = 255;
        if (Blue < 0) Blue = 0; else if (Blue > 255) Blue = 255;
        Dest[Index] = ((ULONG)Red << 16) | ((ULONG)Green << 8) | (ULONG)Blue;
    }
}

static void
DwmBlitWindow(ULONG *comp, LONG scrW,
              LONG clipL, LONG clipT, LONG clipR, LONG clipB,
              const BYTE *pix, const ULONG *wallpaper,
              const ULONG *backdropBase, const DWM_WIN *w)
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
        const ULONG *baserow;
        ULONG *dstrow;
        LONG opaqueStart = 0, opaqueEnd = 0;
        LONG reflectStart, glassStart = 0, glassEnd = width;
        LONG directStart = 0, directEnd = 0;
        const BYTE *reflection = NULL;

        if (g_frameStats) g_statBlitPixels += width;
        dy = (LONG)(wy + r);
        srcrow = (const ULONG *)(pix + (SIZE_T)r * w->Stride) + srcx0;
        dstrow = comp + (SIZE_T)dy * scrW + x0;
        baserow = dstrow;
        if (wallpaper != NULL)
            wallpaperrow = wallpaper + (SIZE_T)dy * scrW + x0;
        if (backdropBase != NULL)
        {
            LONG Radius = (LONG)w->CornerRadius;
            LONG Inset = 0;

            baserow = backdropBase + (SIZE_T)dy * scrW + x0;
            if (Radius > w->cx / 2) Radius = w->cx / 2;
            if (Radius > w->cy / 2) Radius = w->cy / 2;
            if (Radius > 0 && (r < Radius || r >= w->cy - Radius))
            {
                double CornerY = r < Radius ?
                    (double)Radius - ((double)r + 0.5) :
                    ((double)r + 0.5) - (double)(w->cy - Radius);
                double CornerX = sqrt((double)Radius * Radius -
                                      CornerY * CornerY);
                Inset = Radius - (LONG)(CornerX + 0.5);
                if (Inset < 0) Inset = 0;
            }
            directStart = Inset > srcx0 ? Inset - srcx0 : 0;
            directEnd = w->cx - Inset - srcx0;
            if (directEnd > width) directEnd = width;
        }

        if (!useKey && !useAlpha && !usePixelAlpha && !useBackdrop &&
            !useCorner)
        {
            RtlCopyMemory(dstrow, srcrow, (SIZE_T)width * 4);
            continue;
        }

        /* Glass and rounded corners do not make the interior of a normal
         * client translucent. Copy its span without evaluating material,
         * corner coverage and alpha for every pixel during a drag. */
        if (!useKey && !useAlpha && !usePixelAlpha && !useEdge &&
            (!useBackdrop ||
             (w->BackdropRegion == DWM_BACKDROP_REGION_NONCLIENT &&
              r >= ncBottom && r < w->ClientY + w->ClientHeight)))
        {
            LONG left = useBackdrop ? ncLeft : 0;
            LONG right = useBackdrop ? w->ClientX + w->ClientWidth : w->cx;
            LONG radius = (LONG)w->CornerRadius;

            if (radius > w->cx / 2) radius = w->cx / 2;
            if (radius > w->cy / 2) radius = w->cy / 2;
            if (useCorner && (r < radius || r >= w->cy - radius))
            {
                if (left < radius) left = radius;
                if (right > w->cx - radius) right = w->cx - radius;
            }
            opaqueStart = left > srcx0 ? left - srcx0 : 0;
            opaqueEnd = right - srcx0;
            if (opaqueEnd > width) opaqueEnd = width;
        }

        reflectStart = x0 + (dy * 6) / 5;
        if (!useKey && !useAlpha && !usePixelAlpha &&
            w->BackdropType == DWM_BACKDROP_TRANSIENT &&
            (w->BackdropRegion == DWM_BACKDROP_REGION_NONCLIENT ||
             w->BackdropRegion == DWM_BACKDROP_REGION_WINDOW) &&
            w->BackdropOpacity <= 255 && g_reflectLut != NULL &&
            reflectStart >= 0 && reflectStart <= g_reflectLen - width)
        {
            LONG Radius = (LONG)w->CornerRadius;
            LONG Inset = 0;

            if (Radius > w->cx / 2) Radius = w->cx / 2;
            if (Radius > w->cy / 2) Radius = w->cy / 2;
            if (useCorner && (r < Radius || r >= w->cy - Radius))
                Inset = Radius;
            /* The scalar path still handles rounded coverage and the
             * one-pixel material rim. Interior runs have full coverage. */
            if (useEdge && Inset < 1) Inset = 1;
            if (glassStart < Inset - srcx0) glassStart = Inset - srcx0;
            if (glassEnd > w->cx - Inset - srcx0)
                glassEnd = w->cx - Inset - srcx0;
            if (backdropBase != NULL)
            {
                if (glassStart < directStart) glassStart = directStart;
                if (glassEnd > directEnd) glassEnd = directEnd;
            }
            if (!useEdge || (r > 0 && r < w->cy - 1))
                reflection = g_reflectLut + reflectStart;
        }

        for (x = 0; x < width; x++)
        {
            if (x == opaqueStart && opaqueEnd > opaqueStart)
            {
                for (; x < opaqueEnd; ++x)
                    dstrow[x] = srcrow[x] & 0x00ffffffu;
                --x;
                continue;
            }
            if (reflection != NULL && x >= glassStart && x < glassEnd)
            {
                LONG sourceX = srcx0 + x;
                LONG Limit = glassEnd, End;
                ULONG Color = srcrow[x] & 0x00ffffffu;
                BOOL Material = w->BackdropRegion == DWM_BACKDROP_REGION_WINDOW ||
                                r < ncBottom || r >= w->ClientY + w->ClientHeight;

                if (!Material && sourceX < ncLeft)
                {
                    Material = TRUE;
                    if (Limit > ncLeft - srcx0) Limit = ncLeft - srcx0;
                }
                if (sourceX >= w->ClientX + w->ClientWidth) Material = TRUE;
                if (Material)
                {
                    for (End = x + 1; End < Limit; ++End)
                        if ((srcrow[End] & 0x00ffffffu) != Color) break;
                    if (End - x >= 8)
                    {
                        ULONG Near = DwmChannelDistance(Color, backdropKey);
                        ULONG Other = DwmChannelDistance(Color, colorizationKey);
                        ULONG Key = backdropKey, Weight = 0;

                        if (Other < Near)
                        {
                            Near = Other;
                            Key = colorizationKey;
                        }
                        if (Near <= DWM_MATERIAL_TINT_BAND)
                            Weight = 255;
                        else if (Near < DWM_MATERIAL_FRINGE)
                            Weight = (DWM_MATERIAL_FRINGE - Near) * 255u /
                                     (DWM_MATERIAL_FRINGE - DWM_MATERIAL_TINT_BAND);
                        DwmBlendConstantGlass(dstrow + x, baserow + x,
                                             End - x, Color, Key,
                                             w->BackdropOpacity, Weight, reflection + x);
                        if (g_frameStats)
                        {
                            g_statGlassPixels += End - x;
                            g_statConstantGlassPixels += End - x;
                            if (Weight == 255)
                                g_statFullWeightGlassPixels += End - x;
                        }
                        x = End - 1;
                        continue;
                    }
                    /* This interior span has no key, alpha, edge or corner
                     * work. Classify varying source colours in a bounded
                     * batch so the compiler can vectorize the channel math. */
                    End = x + 64;
                    if (End > Limit) End = Limit;
                    DwmBlendGlassSpan(dstrow + x, baserow + x, srcrow + x,
                                      End - x,
                                      backdropKey, colorizationKey,
                                      w->BackdropOpacity, reflection + x);
                    if (g_frameStats) g_statGlassPixels += End - x;
                    x = End - 1;
                    continue;
                }
            }
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

            d = backdropBase != NULL && x >= directStart && x < directEnd ?
                baserow[x] : dstrow[x];
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

/* Find one opaque cover, inset by the sampling reach of every intervening
 * blur. A skipped lower pixel can only affect pixels inside this cover before
 * its opaque client overwrites them. Keep shadows and blur passes intact. */
static BOOL
DwmFindOpaqueCover(const DWM_WIN *Windows, ULONG Count, ULONG Index,
                   LONG ClipLeft, LONG ClipTop, LONG ClipRight, LONG ClipBottom,
                   RECTL *Result)
{
    LONGLONG Reach = 0, BestArea = 0;
    LONG FilterReach = 2 * (((DwmBlurRadius() + 1) / 2) * DWM_BLUR_PASSES + 2);
    ULONG Above;

    /* GPU blur has its own sampling footprint. Use the uncropped path while
     * calibrating or using that filter. */
    if ((g_blurUseGpu != 0 || g_blurLargeUseGpu != 0) &&
        DwmGpuIsActive())
        return FALSE;
    for (Above = Index + 1; Above < Count; ++Above)
    {
        const DWM_WIN *Cover = &Windows[Above];
        LONGLONG Left = 0, Top = 0, Right = Cover->cx, Bottom = Cover->cy;
        LONGLONG X = (LONGLONG)Cover->x - g_originX;
        LONGLONG Y = (LONGLONG)Cover->y - g_originY;
        LONGLONG Radius = Cover->CornerRadius, Area;
        BOOL Backdrop = Cover->BackdropType >= DWM_BACKDROP_MAIN &&
                        Cover->BackdropType <= DWM_BACKDROP_TABBED &&
                        Cover->BackdropRegion != 0;

        if (Cover->AnimFlags != 0)
            return FALSE;
        if (Cover->BlurFlags & DWM_BLUR_ENABLE)
            Reach += FilterReach;
        if (Cover->BackdropType == DWM_BACKDROP_TRANSIENT && Cover->BackdropRegion != 0)
            Reach += FilterReach;
        if (Cover->cx <= 0 || Cover->cy <= 0 ||
            (Cover->LayerFlags & DWM_LWA_COLORKEY) ||
            ((Cover->LayerFlags & DWM_LWA_ALPHA) && Cover->Alpha < 255) ||
            (Cover->BlurFlags & DWM_BLUR_ENABLE))
            continue;
        if (Backdrop)
        {
            if (Cover->BackdropRegion != DWM_BACKDROP_REGION_NONCLIENT)
                continue;
            Left = DwmBackdropNcLeft(Cover);
            Top = DwmBackdropNcBottom(Cover);
            Right = (LONGLONG)Cover->ClientX + Cover->ClientWidth;
            Bottom = (LONGLONG)Cover->ClientY + Cover->ClientHeight;
        }
        if (Radius > Cover->cx / 2) Radius = Cover->cx / 2;
        if (Radius > Cover->cy / 2) Radius = Cover->cy / 2;
        if (Left < Radius) Left = Radius;
        if (Top < Radius) Top = Radius;
        if (Right > Cover->cx - Radius) Right = Cover->cx - Radius;
        if (Bottom > Cover->cy - Radius) Bottom = Cover->cy - Radius;
        Left += X + Reach; Top += Y + Reach;
        Right += X - Reach; Bottom += Y - Reach;
        if (Left < ClipLeft) Left = ClipLeft;
        if (Top < ClipTop) Top = ClipTop;
        if (Right > ClipRight) Right = ClipRight;
        if (Bottom > ClipBottom) Bottom = ClipBottom;
        if (Right <= Left || Bottom <= Top)
            continue;
        Area = (Right - Left) * (Bottom - Top);
        if (Area > BestArea)
        {
            Result->left = (LONG)Left; Result->top = (LONG)Top;
            Result->right = (LONG)Right; Result->bottom = (LONG)Bottom;
            BestArea = Area;
        }
    }
    return BestArea != 0;
}

static void
DwmBlitWindowVisible(ULONG *Composition, LONG Width,
                     LONG Left, LONG Top, LONG Right, LONG Bottom,
                     const BYTE *Pixels, const ULONG *Wallpaper,
                     const ULONG *BackdropBase, const DWM_WIN *Window,
                     const RECTL *Cover)
{
    if (Cover == NULL)
    {
        DwmBlitWindow(Composition, Width, Left, Top, Right, Bottom,
                      Pixels, Wallpaper, BackdropBase, Window);
        return;
    }
    DwmBlitWindow(Composition, Width, Left, Top, Right, Cover->top,
                  Pixels, Wallpaper, BackdropBase, Window);
    DwmBlitWindow(Composition, Width, Left, Cover->bottom, Right, Bottom,
                  Pixels, Wallpaper, BackdropBase, Window);
    DwmBlitWindow(Composition, Width, Left, Cover->top, Cover->left, Cover->bottom,
                  Pixels, Wallpaper, BackdropBase, Window);
    DwmBlitWindow(Composition, Width, Cover->right, Cover->top, Right, Cover->bottom,
                  Pixels, Wallpaper, BackdropBase, Window);
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
static ULONGLONG g_lastFrameQpc;

/*
 * Frame accounting.  The compositor's cost is dominated by whole-surface work
 * -- clearing the backdrop, blitting each window, and the final blit to the
 * screen -- all of which scale with the damaged area rather than with the
 * number of effects, so the useful measure is nanoseconds per damaged pixel
 * alongside the wall-clock split between composing and presenting.
 * Set DWM_FRAME_STATS=1 to enable the stage timers and serial reports.
 */
/* Amortize serial output so an enabled diagnostic does not dominate a drag. */
#define DWM_STAT_FRAMES 60
static ULONG g_statFrames;
static ULONG g_statFirstFrames;

static void
DwmStatCounter(LARGE_INTEGER *Counter)
{
    if (g_frameStats)
        QueryPerformanceCounter(Counter);
    else
        Counter->QuadPart = 0;
}

/* Prefer full-frame GPU composition when the adapter supports it. The
 * environment override remains available for diagnosis. The registered output
 * swapchain is promoted to a KMT flip and never read back to the CPU. */
static BOOL
DwmGpuComposeEnabled(void)
{
    WCHAR Value[8];
    DWORD Length = GetEnvironmentVariableW(L"DWM_GPU_COMPOSE", Value,
                                           ARRAYSIZE(Value));

    return !(Length == 1 && Value[0] == L'0');
}
static ULONGLONG g_statBlurTicks;
static ULONGLONG g_statBackdropBlurTicks;
static ULONGLONG g_statShadowTicks;
static ULONGLONG g_statBlitTicks;
static ULONGLONG g_statAnimTicks;
static ULONG g_statWindows;
static ULONGLONG g_statComposeTicks;
static ULONGLONG g_statPresentTicks;
static ULONGLONG g_statBackdropTicks;
static ULONGLONG g_statPixels;
static ULONGLONG g_statFrameMin;
static ULONGLONG g_statFrameMax;
static ULONGLONG g_statFetchTicks;
static ULONGLONG g_statPrepareTicks;
static ULONGLONG g_frameFetchTicks;
static ULONGLONG g_framePrepareTicks;
static ULONGLONG g_statGpuDrawKernel100ns;
static ULONGLONG g_statGpuDrawUser100ns;

typedef struct
{
    ULONGLONG Kernel, User;
    BOOL Valid;
} DWM_THREAD_TIME;

static void
DwmStatThreadTime(DWM_THREAD_TIME *Time)
{
    FILETIME Creation, Exit, Kernel, User;
    Time->Valid = g_frameStats && GetThreadTimes(GetCurrentThread(), &Creation,
                                                &Exit, &Kernel, &User);
    if (Time->Valid)
    {
        Time->Kernel = ((ULONGLONG)Kernel.dwHighDateTime << 32) | Kernel.dwLowDateTime;
        Time->User = ((ULONGLONG)User.dwHighDateTime << 32) | User.dwLowDateTime;
    }
}


static void
DwmFrameStat(ULONGLONG BackdropTicks, ULONGLONG ComposeTicks,
             ULONGLONG PresentTicks, ULONGLONG Pixels)
{
    ULONGLONG Total = BackdropTicks + ComposeTicks + PresentTicks;
    char Message[224];

    if (!g_frameStats)
        return;

    g_statFetchTicks += g_frameFetchTicks;
    g_statPrepareTicks += g_framePrepareTicks;
    g_statBackdropTicks += BackdropTicks;
    g_statComposeTicks += ComposeTicks;
    g_statPresentTicks += PresentTicks;
    g_statPixels += Pixels;
    if (g_statFrames == 0 || Total < g_statFrameMin)
        g_statFrameMin = Total;
    if (Total > g_statFrameMax)
        g_statFrameMax = Total;
    if (g_qpcPerSecond == 0)
        return;

    if (g_statFirstFrames < 4)
    {
        _snprintf(Message, sizeof(Message) - 1,
                  "DWM: frame#%lu %llu us (backdrop %llu, compose %llu, "
                  "present %llu) %llu px\n",
                  (unsigned long)g_statFirstFrames,
                  (Total * 1000000ull) / g_qpcPerSecond,
                  (BackdropTicks * 1000000ull) / g_qpcPerSecond,
                  (ComposeTicks * 1000000ull) / g_qpcPerSecond,
                  (PresentTicks * 1000000ull) / g_qpcPerSecond,
                  Pixels);
        Message[sizeof(Message) - 1] = '\0';
        OutputDebugStringA(Message);

        ++g_statFirstFrames;
    }

    if (++g_statFrames < DWM_STAT_FRAMES)
        return;

    {
        ULONGLONG Us = 1000000ull;
        ULONGLONG Frames = g_statFrames;
        ULONGLONG AllTicks = g_statBackdropTicks + g_statComposeTicks +
                             g_statPresentTicks;

        _snprintf(Message, sizeof(Message) - 1,
                  "DWM: GPU draw CPU avg user %llu us, kernel %llu us over %llu frames\n",
                  g_statGpuDrawUser100ns / (10 * Frames),
                  g_statGpuDrawKernel100ns / (10 * Frames), Frames);
        Message[sizeof(Message) - 1] = '\0';
        OutputDebugStringA(Message);

        _snprintf(Message, sizeof(Message) - 1,
                  "DWM: preparation avg fetch %llu us, backdrop/metadata %llu us, "
                  "render %llu us over %llu frames\n",
                  (g_statFetchTicks * Us) / (g_qpcPerSecond * Frames),
                  (g_statPrepareTicks * Us) / (g_qpcPerSecond * Frames),
                  (AllTicks * Us) / (g_qpcPerSecond * Frames), Frames);
        Message[sizeof(Message) - 1] = '\0';
        OutputDebugStringA(Message);

        _snprintf(Message, sizeof(Message) - 1,
                  "DWM: frame avg %llu us (backdrop %llu, compose %llu, "
                  "present %llu) min %llu max %llu, %llu Mpix, %llu ns/pix\n",
                  (AllTicks * Us) / (g_qpcPerSecond * Frames),
                  (g_statBackdropTicks * Us) / (g_qpcPerSecond * Frames),
                  (g_statComposeTicks * Us) / (g_qpcPerSecond * Frames),
                  (g_statPresentTicks * Us) / (g_qpcPerSecond * Frames),
                  (g_statFrameMin * Us) / g_qpcPerSecond,
                  (g_statFrameMax * Us) / g_qpcPerSecond,
                  g_statPixels / 1048576ull,
                  g_statPixels ? (AllTicks * 1000000000ull) /
                                     (g_qpcPerSecond * g_statPixels) : 0);
        Message[sizeof(Message) - 1] = '\0';
        OutputDebugStringA(Message);

        if (DwmGpuComposeIsActive())
        {
            ULONGLONG Filtered, Reused;
            DwmGpuComposeBlurStats(&Filtered, &Reused);
            _snprintf(Message, sizeof(Message) - 1,
                      "DWM: GPU blur filtered %llu, reused %llu over %llu frames\n",
                      Filtered, Reused, Frames);
            Message[sizeof(Message) - 1] = '\0';
            OutputDebugStringA(Message);
        }

        _snprintf(Message, sizeof(Message) - 1,
                  "DWM: compose split blur %llu, backdrop %llu, shadow %llu, "
                  "blit %llu, anim %llu us/frame over %lu window-visits, glass %llu/%llu px\n",
                  (g_statBlurTicks * Us) / (g_qpcPerSecond * Frames),
                  (g_statBackdropBlurTicks * Us) / (g_qpcPerSecond * Frames),
                  (g_statShadowTicks * Us) / (g_qpcPerSecond * Frames),
                  (g_statBlitTicks * Us) / (g_qpcPerSecond * Frames),
                  (g_statAnimTicks * Us) / (g_qpcPerSecond * Frames),
                  (unsigned long)g_statWindows, g_statGlassPixels, g_statBlitPixels);
        Message[sizeof(Message) - 1] = '\0';
        OutputDebugStringA(Message);

        _snprintf(Message, sizeof(Message) - 1,
                  "DWM: glass constant %llu px, full-weight %llu px of %llu fast px\n",
                  g_statConstantGlassPixels, g_statFullWeightGlassPixels,
                  g_statGlassPixels);
        Message[sizeof(Message) - 1] = '\0';
        OutputDebugStringA(Message);
    }
    {
        char Message[192];
        _snprintf(Message, sizeof(Message) - 1,
                  "DWM: blur split down %llu, filter %llu, finish %llu us/frame, "
                  "backdrop cache %llu full/%llu partial/%llu miss, %llu metadata\n",
                  g_statBlurDownTicks * 1000000ull / (g_qpcPerSecond * g_statFrames),
                  g_statBlurFilterTicks * 1000000ull / (g_qpcPerSecond * g_statFrames),
                  g_statBlurUpTicks * 1000000ull / (g_qpcPerSecond * g_statFrames),
                  g_statBackdropCacheHits, g_statBackdropCachePartials,
                  g_statBackdropCacheMisses, g_statBackdropMetadataHits);
        Message[sizeof(Message) - 1] = '\0';
        OutputDebugStringA(Message);
    }
    g_statBlurDownTicks = g_statBlurFilterTicks = g_statBlurUpTicks = 0;
    g_statBackdropCacheHits = g_statBackdropCachePartials = 0;
    g_statBackdropCacheMisses = 0;
    g_statBackdropMetadataHits = 0;
    g_statFrames = 0;
    g_statBlurTicks = 0;
    g_statBackdropBlurTicks = 0;
    g_statShadowTicks = 0;
    g_statBlitTicks = 0;
    g_statAnimTicks = 0;
    g_statWindows = 0;
    g_statGlassPixels = g_statBlitPixels = 0;
    g_statConstantGlassPixels = g_statFullWeightGlassPixels = 0;
    g_statBackdropTicks = 0;
    g_statComposeTicks = 0;
    g_statPresentTicks = 0;
    g_statPixels = 0;
    g_statFrameMin = 0;
    g_statFrameMax = 0;
    g_statFetchTicks = g_statPrepareTicks = 0;
    g_statGpuDrawKernel100ns = g_statGpuDrawUser100ns = 0;
}
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
    BOOL restoreGpu;

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

    /* Allocate every replacement before retiring the old output. The GPU
     * canvas and wallpaper use that output's dimensions; they must stop
     * reading before either CPU bitmap is replaced by a different size. */
    restoreGpu = DwmGpuComposeIsActive();
    if (restoreGpu)
        DwmGpuComposeShutdown();

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
    if (restoreGpu && !DwmGpuComposeInitialize(W, H))
    {
        /* Registration has immutable geometry. Recreate the carrier and
         * its source claim together; a failed recreation leaves a usable
         * CPU composition, never an old texture with the new bitmap. */
        DwmGpuComposeShutdown();
        DwmLog("DWM: GPU output recreation failed; restoring software composition\n");
    }
    return TRUE;
}

static void
DwmComposeLoop(HANDLE hStopEvent)
{
    LARGE_INTEGER stgA = {{0}};
    LARGE_INTEGER stgB = {{0}};
    LARGE_INTEGER statFrameStart = {{0}};
    LARGE_INTEGER statFetchStart = {{0}};
    LARGE_INTEGER statFetchEnd = {{0}};
    DWM_THREAD_TIME statDrawCpuStart, statDrawCpuEnd;
    LARGE_INTEGER statBackdropEnd = {{0}};
    LARGE_INTEGER statComposeEnd = {{0}};
    LARGE_INTEGER statPresentEnd = {{0}};
    HDC hdcScreen = GetDC(NULL);
    DWM_ATTACH att;
    HANDLE hWake;
    HANDLE hConnection;
    HWND SettingsWindow;
    DWM_SETTINGS Settings = {DWM_EFFECT_ALL, FALSE, DwmGpuComposeIsActive};
    BOOL forceFull = TRUE;
    BOOL gpuDeferred = FALSE;
    DWORD gpuLastOutputCheck = 0;
    LONG vw, vh, primW, primH;
    ULONG ViewIndex;

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
        WCHAR Value[8];
        DWORD Length = GetEnvironmentVariableW(L"DWM_FRAME_STATS", Value,
                                               ARRAYSIZE(Value));
        /* Profiling must not add thread queries or serial traffic by default. */
        g_frameStats = Length == 1 && Value[0] == L'1';
    }

    /* Allow a known-working software renderer while GPU effects are tested. */
    {
        WCHAR Value[8];
        DWORD Length = GetEnvironmentVariableW(L"DWM_GPU_EFFECTS", Value,
                                               ARRAYSIZE(Value));
        if (Length == 1 && Value[0] == L'0')
        {
            g_blurUseGpu = g_blurLargeUseGpu = 0;
            OutputDebugStringA("DWM: standalone GPU readback effects disabled\n");
        }
        else if (DwmGpuInitialize(hdcScreen))
            OutputDebugStringA("DWM: GPU effects active\n");
        else
            OutputDebugStringA("DWM: GPU effects unavailable, using software\n");
    }

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

    if (NtDCompositionCreateConnection(FALSE, hWake, &hConnection) < 0)
    {
        DwmLog("DWM: composition connection creation failed\n");
        RtlZeroMemory(&att, sizeof(att));
        att.Attach = 0;
        (void)NtUserCallOneParam((DWORD_PTR)&att, DWM_ROUTINE_ATTACH);
        CloseHandle(hWake);
        ReleaseDC(NULL, hdcScreen);
        return;
    }

    /* Registration of the trusted scanout swapchain is accepted only after
     * this process has attached as the desktop compositor. */
    if (DwmGpuComposeEnabled() && DwmGpuComposeInitialize(g_W, g_H))
        OutputDebugStringA("DWM: GPU composition enabled; GPU copy to scanout\n");

    if (DwmSettingsRead(&Settings.Effects) != ERROR_SUCCESS)
        DwmLog("DWM: could not read effect preferences\n");
    SettingsWindow = DwmSettingsCreateWindow(GetModuleHandleW(NULL), &Settings);

    for (;;)
    {
        PDWM_FRAME_HEADER hdr = (PDWM_FRAME_HEADER)g_buf;
        PDWM_WIN wins;
        PRECTL blurRects;
        LONG st;
        ULONG i;
        MSG message;
        DPT_SCOPE FetchTrace;

        /* This thread owns the GPU carrier. Service sent messages even
         * while composition is idle, so broadcasts cannot block Explorer. */
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (Settings.Changed)
        {
            /* A preference change can expose pixels outside ordinary
             * window damage. Rebuild the scene and its cached backdrops. */
            forceFull = TRUE;
            Settings.Changed = FALSE;
        }

        if (WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0)
            break;

        if (gpuDeferred)
        {
            DWORD Elapsed = GetTickCount() - gpuLastOutputCheck;
            DWM_GPU_RESULT Output;

            /* Keep servicing this thread's windows while an exclusive owner
             * has the output. Dirty notifications must not turn suspension
             * into a render loop; only test output availability periodically. */
            DwmSetTimerPrecision(FALSE);
            if (Elapsed < 200)
            {
                if (MsgWaitForMultipleObjects(1, &hStopEvent, FALSE,
                                              200 - Elapsed, QS_ALLINPUT) == WAIT_OBJECT_0)
                    break;
                continue;
            }
            Output = DwmGpuComposeCheckOutput();
            gpuLastOutputCheck = GetTickCount();
            if (Output == DWM_GPU_DEFERRED)
                continue;
            gpuDeferred = FALSE;
            forceFull = TRUE;
            g_lastFrameQpc = 0;
            if (Output == DWM_GPU_FAILED)
            {
                DwmGpuComposeShutdown();
                DwmLog("DWM: GPU output recovery failed; restoring software composition\n");
            }
            else
            {
                DwmLog("DWM: GPU output available; repairing the desktop\n");
            }
        }

        /* Budget from the start of the last frame. Waiting a full refresh
         * period after presentation adds rendering time to every interval. */
        if (g_refreshPeriodQpc != 0 && g_lastFrameQpc != 0)
        {
            LARGE_INTEGER Counter;
            ULONGLONG Elapsed;
            QueryPerformanceCounter(&Counter);
            Elapsed = (ULONGLONG)Counter.QuadPart - g_lastFrameQpc;
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

        DwmStatCounter(&statFetchStart);
        FetchTrace = DptBegin(&g_DwmPresentTrace, DPT_FETCH);
        st = (LONG)NtUserCallOneParam((DWORD_PTR)g_buf, DWM_ROUTINE_GETFRAME);
        DptEnd(&g_DwmPresentTrace, FetchTrace, st >= 0, 0);
        DwmStatCounter(&statFetchEnd);
        g_frameFetchTicks = (ULONGLONG)(statFetchEnd.QuadPart - statFetchStart.QuadPart);
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

        wins = (PDWM_WIN)(g_buf + hdr->WinArrayBase);
        DwmDxSweepSurfaces(wins, hdr->Count);

        if ((LONG)hdr->ScreenW != primW || (LONG)hdr->ScreenH != primH)
        {
            LONG newPrimW = (LONG)hdr->ScreenW;
            LONG newPrimH = (LONG)hdr->ScreenH;
            LONG newOriginX = GetSystemMetrics(SM_XVIRTUALSCREEN);
            LONG newOriginY = GetSystemMetrics(SM_YVIRTUALSCREEN);

            vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            if (vw <= 0 || vh <= 0) { vw = newPrimW; vh = newPrimH; newOriginX = newOriginY = 0; }
            if (newPrimW == 0 || newPrimH == 0 || !DwmCreateSurfaces(hdcScreen, vw, vh))
            {
                Sleep(50);
                continue;
            }
            primW = newPrimW;
            primH = newPrimH;
            g_originX = newOriginX;
            g_originY = newOriginY;
            DwmShadowInit(hdcScreen);
            g_lastFrameQpc = 0;
            forceFull = TRUE;
            continue;
        }

        if (hdr->Dirty == 0 && !forceFull)
        {
            HANDLE WaitHandles[2] = {hStopEvent, hWake};

            DwmSetTimerPrecision(FALSE);
            if (MsgWaitForMultipleObjects(ARRAYSIZE(WaitHandles), WaitHandles,
                                          FALSE, 200, QS_ALLINPUT) == WAIT_OBJECT_0)
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
            for (i = 0; i < hdr->Count; ++i)
                DwmSettingsApplyWindow(&Settings, &wins[i]);

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

            /* A GPU redraw covering the screen does not change wallpaper.
             * Repainting/uploading it here makes large window moves perform
             * a full-screen CPU copy on every frame. Explicit full damage
             * and surface recreation still refresh the cached wallpaper. */
            if (!DwmGpuComposeIsActive() &&
                cl == 0 && ct == 0 && cr == g_W && cb == g_H)
                refreshBackdrop = TRUE;
            forceFull = FALSE;

            if (pr > pl && pb > pt)
            {
                if (refreshBackdrop)
                {
                    RECT fullBackdrop = {0, 0, g_W, g_H};

                    g_backdropCacheValid = FALSE;
                    if (!PaintDesktop(g_hdcBackdrop) &&
                        !FillRect(g_hdcBackdrop, &fullBackdrop,
                                  GetSysColorBrush(COLOR_DESKTOP)))
                    {
                        forceFull = TRUE;
                        continue;
                    }
                }

                QueryPerformanceCounter(&statFrameStart);
                g_framePrepareTicks = g_frameStats ?
                    (ULONGLONG)(statFrameStart.QuadPart - statFetchEnd.QuadPart) : 0;

                /*
                 * Hardware frame.  Every window is drawn from a resident
                 * texture, so an unchanged one costs a quad and no upload,
                 * and the finished frame is copied to scanout by the GPU
                 * without CPU readback. If any step declines, the software
                 * path below can rebuild the frame.
                 */
                if (DwmGpuComposeIsActive())
                {
                    DPT_SCOPE FrameTrace = DptBegin(&g_DwmPresentTrace, DPT_FRAME);
                    DPT_SCOPE AckTrace;
                    BOOL gpuFrame;
                    DWM_GPU_RESULT gpuResult;
                    RECT gpuDamage = {pl, pt, pr, pb};
                    RECT gpuShadowMargins = {g_shadowMarginLeft, g_shadowMarginTop,
                                              g_shadowMarginRight, g_shadowMarginBottom};
                    /* A dirty frame includes metadata/preparation before Begin.
                     * The fetch counter also exposes an unfinished metadata call. */
                    if (FrameTrace.Epoch && FrameTrace.Epoch == FetchTrace.Epoch)
                        FrameTrace.Start = FetchTrace.Start;
                    DwmGpuComposeScene(wins, hdr->Count,
                                        (const RECTL *)(g_buf + hdr->BlurRectArrayBase),
                                        hdr->BlurRectCount, g_originX, g_originY,
                                        refreshBackdrop, DwmBlurRadius(), &gpuShadowMargins);
                    gpuFrame = DwmGpuComposeBegin(GetSysColor(COLOR_DESKTOP),
                                                       g_backdropBits, refreshBackdrop,
                                                       &gpuDamage);

                    DwmStatCounter(&statBackdropEnd);

                    DwmStatThreadTime(&statDrawCpuStart);
                    for (i = 0; gpuFrame && i < hdr->Count; i++)
                    {
                        const BYTE *gpix;

                        /* Submit the scene in order. Begin's scissor limits
                         * raster work to the repaired buffer region. */
                        gpix = DwmGpuComposeNeedsSurfacePixels(&wins[i]) ?
                                   DwmGetSurfaceView(&wins[i]) : NULL;
                        DwmGpuComposePrepareWindow(&wins[i], i);
                        if (wins[i].BlurRectBase > hdr->BlurRectCount ||
                            wins[i].BlurRectCount > hdr->BlurRectCount - wins[i].BlurRectBase)
                        {
                            gpuFrame = FALSE;
                            break;
                        }
                        if (!DwmGpuComposeBlurWindow(&wins[i],
                                wins[i].BlurRectCount != 0 ?
                                    &((const RECTL *)(g_buf + hdr->BlurRectArrayBase))[wins[i].BlurRectBase] : NULL,
                                g_originX, g_originY, DwmBlurRadius()) ||
                            !DwmBlendShadow(NULL, g_W, g_H, 0, 0, g_W, g_H, &wins[i]) ||
                            !DwmGpuComposeWindow(&wins[i], gpix,
                                                 g_originX, g_originY))
                        {
                            gpuFrame = FALSE;
                        }
                    }

                    DwmStatThreadTime(&statDrawCpuEnd);
                    if (statDrawCpuStart.Valid && statDrawCpuEnd.Valid)
                    {
                        g_statGpuDrawKernel100ns += statDrawCpuEnd.Kernel - statDrawCpuStart.Kernel;
                        g_statGpuDrawUser100ns += statDrawCpuEnd.User - statDrawCpuStart.User;
                    }
                    DwmStatCounter(&statComposeEnd);

                    gpuResult = gpuFrame ? DwmGpuComposeEnd() : DwmGpuComposeAbort();
                    if (gpuResult == DWM_GPU_COMPLETE ||
                        gpuResult == DWM_GPU_DEFERRED)
                    {
                        DwmStatCounter(&statPresentEnd);
                        /* A deferred present has still completed the GPU
                         * copies into DWM-owned textures. Release those client
                         * publications without claiming output was displayed. */
                        AckTrace = DptBegin(&g_DwmPresentTrace, DPT_ACK);
                        for (i = 0; i < hdr->Count; ++i)
                            DwmDxAcknowledgeSurface(&wins[i]);
                        DptEnd(&g_DwmPresentTrace, AckTrace, TRUE, 0);
                    }
                    if (gpuResult == DWM_GPU_COMPLETE)
                    {
                        g_lastFrameQpc = (ULONGLONG)statFrameStart.QuadPart;
                        /* On the GPU path these fields mean clear/context,
                         * window draw/upload, and swap/direct-flip.  Keeping
                         * the existing labels makes the serial format stable
                         * while exposing where a slow hardware frame waits. */
                        DwmFrameStat(
                                     (ULONGLONG)(statBackdropEnd.QuadPart -
                                                 statFrameStart.QuadPart),
                                     (ULONGLONG)(statComposeEnd.QuadPart -
                                                 statBackdropEnd.QuadPart),
                                     (ULONGLONG)(statPresentEnd.QuadPart -
                                                 statComposeEnd.QuadPart),
                                     (ULONGLONG)((pr - pl) * (pb - pt)));
                        DptEnd(&g_DwmPresentTrace, FrameTrace, TRUE, 0);
                        ++g_frameSeq;
                        if ((g_frameSeq & 255) == 0)
                            DwmSweepViews();
                        forceFull = FALSE;
                        continue;
                    }
                    DptEnd(&g_DwmPresentTrace, FrameTrace, FALSE, 0);
                    if (gpuResult == DWM_GPU_RETRY)
                    {
                        /* A GDI FRONT can be replaced after GETFRAME but
                         * before its OpenResource. Preserve the last complete
                         * scanout and pull current metadata before retrying. */
                        forceFull = TRUE;
                        continue;
                    }
                    if (gpuResult == DWM_GPU_DEFERRED)
                    {
                        gpuDeferred = TRUE;
                        gpuLastOutputCheck = GetTickCount();
                        forceFull = TRUE;
                        DwmLog("DWM: GPU output occluded; retaining composition until it is available\n");
                        continue;
                    }
                    /* A failed GPU frame leaves no reusable CPU composition.
                     * Retire the context and rebuild the entire scene. */
                    DwmGpuComposeShutdown();
                    OutputDebugStringA("DWM: GPU frame failed; restoring software composition\n");
                    pl = cl = 0; pt = ct = 0;
                    pr = cr = g_W; pb = cb = g_H;
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

                DwmStatCounter(&statBackdropEnd);

                blurRects = (PRECTL)(g_buf + hdr->BlurRectArrayBase);
                for (i = 0; i < hdr->Count; i++)
                {
                    const BYTE *pix;
                    const BYTE *dxpix;
                    const ULONG *backdropBase;
                    const RECTL *windowBlurRects = NULL;
                    RECTL Cover;
                    BOOL HaveCover;

                    if (DwmWindowIsHidden(wins, hdr->Count, i, cl, ct, cr, cb))
                        continue;
                    HaveCover = DwmFindOpaqueCover(wins, hdr->Count, i,
                                                    cl, ct, cr, cb, &Cover);
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
                    if (g_frameStats)
                        ++g_statWindows;
                    if (wins[i].AnimFlags != 0)
                    {
                        DwmStatCounter(&stgA);
                        DwmBlitWindowAnimated((ULONG *)g_compBits, g_W,
                                              cl, ct, cr, cb, pix,
                                              DwmDxGetSurfaceSnapshot(&wins[i]),
                                              &wins[i]);
                        DwmStatCounter(&stgB);
                        if (g_frameStats)
                            g_statAnimTicks += (ULONGLONG)(stgB.QuadPart - stgA.QuadPart);
                        continue;
                    }
                    DwmStatCounter(&stgA);
                    DwmApplyBlur((const ULONG *)g_compBits,
                                 (ULONG *)g_compBits, g_W, g_H,
                                 pl, pt, pr, pb, &wins[i],
                                 windowBlurRects);
                    DwmStatCounter(&stgB);
                    if (g_frameStats)
                        g_statBlurTicks += (ULONGLONG)(stgB.QuadPart - stgA.QuadPart);
                    backdropBase = DwmApplyBackdropBlur(
                        (ULONG *)g_compBits, g_W, g_H,
                        pl, pt, pr, pb, &wins[i], wins, i);
                    DwmStatCounter(&stgA);
                    if (g_frameStats)
                        g_statBackdropBlurTicks += (ULONGLONG)(stgA.QuadPart - stgB.QuadPart);
                    /* A non-client shadow is a compositor layer immediately
                     * below its owner. Since wins[] is bottom-to-top, higher
                     * windows and their shadows naturally occlude lower ones. */
                    DwmBlendShadow((ULONG *)g_compBits, g_W, g_H,
                                   cl, ct, cr, cb, &wins[i]);
                    DwmStatCounter(&stgB);
                    if (g_frameStats)
                        g_statShadowTicks += (ULONGLONG)(stgB.QuadPart - stgA.QuadPart);
                    DwmBlitWindowVisible((ULONG *)g_compBits, g_W, cl, ct, cr, cb,
                                         pix, (const ULONG *)g_backdropBits,
                                         backdropBase, &wins[i],
                                         HaveCover ? &Cover : NULL);
                    DwmStatCounter(&stgA);
                    if (g_frameStats)
                        g_statBlitTicks += (ULONGLONG)(stgA.QuadPart - stgB.QuadPart);

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
                        DwmBlitWindowVisible((ULONG *)g_compBits, g_W,
                                             cl, ct, cr, cb, dxpix,
                                             (const ULONG *)g_backdropBits,
                                             NULL, &client,
                                             HaveCover ? &Cover : NULL);
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
                    BOOL bltResult;
                    BOOL flushResult;

                    DwmStatCounter(&statComposeEnd);
                    DWM_PRESENT_BITMAP publication;
                    publication.Bitmap = (ULONG_PTR)g_hbmComp;
                    publication.Rect = (RECTL){pl, pt, pr, pb};
                    bltResult = g_originX == 0 && g_originY == 0 &&
                        ExtEscape(hdcScreen, DWM_ESCAPE_PRESENT_BITMAP,
                                  sizeof(publication), (LPCSTR)&publication,
                                  0, NULL) > 0;
                    if (bltResult)
                    {
                        static BOOL Reported;
                        flushResult = TRUE; /* The escape completes synchronously. */
                        if (g_frameStats && !Reported)
                        {
                            OutputDebugStringA("DWM: cached bitmap presentation active\n");
                            Reported = TRUE;
                        }
                    }
                    else
                    {
                        bltResult = BitBlt(hdcScreen, g_originX + pl, g_originY + pt,
                                           pr - pl, pb - pt, g_hdcComp, pl, pt,
                                           SRCCOPY);
                        flushResult = GdiFlush();
                    }
                    DwmStatCounter(&statPresentEnd);
                    DwmFrameStat(
                        (ULONGLONG)(statBackdropEnd.QuadPart - statFrameStart.QuadPart),
                        (ULONGLONG)(statComposeEnd.QuadPart - statBackdropEnd.QuadPart),
                        (ULONGLONG)(statPresentEnd.QuadPart - statComposeEnd.QuadPart),
                        (ULONGLONG)((pr - pl) * (pb - pt)));
                    if (!bltResult || !flushResult)
                        forceFull = TRUE;
                    else
                    {
                        g_lastFrameQpc = (ULONGLONG)statFrameStart.QuadPart;
                        for (i = 0; i < hdr->Count; ++i)
                        {
                            /* Native GPU textures have no linear CPU view.
                             * A software frame cannot acknowledge their use. */
                            if (wins[i].DxPitch != 0)
                                DwmDxAcknowledgeSurface(&wins[i]);
                        }
                    }
                }
            }
        }

        g_frameSeq++;
        if ((g_frameSeq & 255) == 0)
            DwmSweepViews();
    }

    DwmSetTimerPrecision(FALSE);
    if (SettingsWindow != NULL)
        DestroyWindow(SettingsWindow);
    DwmGpuComposeShutdown();
    DwmFreeBackdropCache();
    DwmDxCleanupSurfaces();
    for (ViewIndex = 0; ViewIndex < DWM_VIEW_CACHE_SIZE; ++ViewIndex)
        DwmDropView(&g_views[ViewIndex]);
    (void)NtDCompositionDestroyConnection(hConnection);
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
