/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Focused end-to-end XPDM GDI hook validation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <windows.h>
#include <reactos/rpi5vc4_xpdm.h>
#include <stdio.h>
#include <stdarg.h>

#define GDI_TEST_WIDTH  320
#define GDI_TEST_HEIGHT 192
#define GDI_SOURCE_SIZE 64
#define GDI_MASK_WIDTH  48
#define GDI_MASK_HEIGHT 32
#define GDI_MASK_STRIDE (((GDI_MASK_WIDTH + 31) / 32) * 4)

typedef struct _RPI5_MASK_BITMAP_INFO
{
    BITMAPINFOHEADER Header;
    RGBQUAD Colors[2];
} RPI5_MASK_BITMAP_INFO;

static const PCSTR Rpi5GdiHookNames[RPI5VC4_GDI_HOOK_COUNT] =
{
    "BitBlt",
    "CopyBits",
    "LineTo",
    "Paint",
    "StretchBlt",
    "StretchBltROP",
    "AlphaBlend",
    "TransparentBlt",
    "GradientFill",
    "Synchronize"
};

static VOID
Rpi5GdiPrint(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[1024];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Arguments);
    va_end(Arguments);
    Buffer[sizeof(Buffer) - 1] = ANSI_NULL;
    fputs(Buffer, stdout);
    fflush(stdout);
    OutputDebugStringA(Buffer);
}

static BOOL
Rpi5QueryGdiStats(
    _In_ HDC Display,
    _Out_ PRPI5VC4_GDI_STATS Stats)
{
    ULONG Escape = RPI5VC4_ESCAPE_QUERY_GDI_STATS;
    INT Returned;

    Returned = ExtEscape(Display,
                         QUERYESCSUPPORT,
                         sizeof(Escape),
                         (LPCSTR)&Escape,
                         0,
                         NULL);
    if (Returned <= 0)
        return FALSE;

    ZeroMemory(Stats, sizeof(*Stats));
    Returned = ExtEscape(Display,
                         RPI5VC4_ESCAPE_QUERY_GDI_STATS,
                         0,
                         NULL,
                         sizeof(*Stats),
                         (LPSTR)Stats);
    return Returned == sizeof(*Stats) &&
           Stats->Size == sizeof(*Stats) &&
           Stats->AbiVersion == RPI5VC4_XPDM_ABI_VERSION;
}

static HBITMAP
Rpi5CreateSourceBitmap(
    _In_ HDC Display,
    _Outptr_ PULONG *Pixels,
    _Out_ PBITMAPINFO BitmapInfo)
{
    HBITMAP Bitmap;
    ULONG X;
    ULONG Y;

    ZeroMemory(BitmapInfo, sizeof(*BitmapInfo));
    BitmapInfo->bmiHeader.biSize = sizeof(BitmapInfo->bmiHeader);
    BitmapInfo->bmiHeader.biWidth = GDI_SOURCE_SIZE;
    BitmapInfo->bmiHeader.biHeight = -GDI_SOURCE_SIZE;
    BitmapInfo->bmiHeader.biPlanes = 1;
    BitmapInfo->bmiHeader.biBitCount = 32;
    BitmapInfo->bmiHeader.biCompression = BI_RGB;
    BitmapInfo->bmiHeader.biSizeImage =
        GDI_SOURCE_SIZE * GDI_SOURCE_SIZE * sizeof(ULONG);

    Bitmap = CreateDIBSection(Display,
                              BitmapInfo,
                              DIB_RGB_COLORS,
                              (PVOID *)Pixels,
                              NULL,
                              0);
    if (Bitmap == NULL || *Pixels == NULL)
        return NULL;

    for (Y = 0; Y < GDI_SOURCE_SIZE; Y++)
    {
        for (X = 0; X < GDI_SOURCE_SIZE; X++)
        {
            ULONG Red = (X * 255u) / (GDI_SOURCE_SIZE - 1u);
            ULONG Green = (Y * 255u) / (GDI_SOURCE_SIZE - 1u);
            ULONG Blue = ((X ^ Y) * 255u) / (GDI_SOURCE_SIZE - 1u);

            (*Pixels)[Y * GDI_SOURCE_SIZE + X] =
                0xFF000000u | (Red << 16) | (Green << 8) | Blue;
        }
    }
    (*Pixels)[0] = 0xFFFF00FFu;
    return Bitmap;
}

static BOOL
Rpi5RequiredHookPassed(
    _In_ ULONG Hook,
    _In_ const RPI5VC4_GDI_STATS *Before,
    _In_ const RPI5VC4_GDI_STATS *After)
{
    return After->HookAttempts[Hook] > Before->HookAttempts[Hook] &&
           After->HookHardwarePresents[Hook] >
               Before->HookHardwarePresents[Hook];
}

static BOOL
Rpi5GdiApiPublished(
    _In_ HDC Display,
    _In_z_ PCSTR Name,
    _In_ const RPI5VC4_GDI_STATS *Before)
{
    RPI5VC4_GDI_STATS After;
    ULONG Hook;
    ULONG HardwarePresents = 0;

    GdiFlush();
    if (!Rpi5QueryGdiStats(Display, &After))
    {
        Rpi5GdiPrint("RPI5_GDI_API_FAIL name=%s reason=stats\n", Name);
        return FALSE;
    }

    for (Hook = 0; Hook < RPI5VC4_GDI_HOOK_COUNT; Hook++)
    {
        HardwarePresents += After.HookHardwarePresents[Hook] -
                            Before->HookHardwarePresents[Hook];
    }

    Rpi5GdiPrint("RPI5_GDI_API name=%s hardware=%lu cpu-fallback=%lu "
                 "failure=%lu\n",
                 Name,
                 HardwarePresents,
                 After.CpuFallbackCount - Before->CpuFallbackCount,
                 After.FailureFallbackCount - Before->FailureFallbackCount);
    return HardwarePresents != 0 &&
           After.CpuFallbackCount == Before->CpuFallbackCount &&
           After.FailureFallbackCount == Before->FailureFallbackCount;
}

int
main(VOID)
{
    static const ULONG RequiredHooks[] =
    {
        RPI5VC4_GDI_HOOK_BITBLT,
        RPI5VC4_GDI_HOOK_COPYBITS,
        RPI5VC4_GDI_HOOK_LINETO,
#if (NTDDI_VERSION < NTDDI_VISTA)
        RPI5VC4_GDI_HOOK_PAINT,
#endif
        RPI5VC4_GDI_HOOK_STRETCHBLTROP,
        RPI5VC4_GDI_HOOK_ALPHABLEND,
        RPI5VC4_GDI_HOOK_TRANSPARENTBLT,
        RPI5VC4_GDI_HOOK_GRADIENTFILL
    };
    BITMAPINFO BitmapInfo;
    RPI5_MASK_BITMAP_INFO MaskInfo;
    RPI5VC4_GDI_STATS Before;
    RPI5VC4_GDI_STATS After;
    TRIVERTEX Vertices[2];
    GRADIENT_RECT Gradient;
    BLENDFUNCTION Blend;
    HDC Display = NULL;
    HDC SourceDc = NULL;
    HDC SaveDc = NULL;
    HBITMAP SourceBitmap = NULL;
    HBITMAP SaveBitmap = NULL;
    HBITMAP MaskBitmap = NULL;
    HBITMAP OldSourceBitmap = NULL;
    HBITMAP OldSaveBitmap = NULL;
    HBRUSH Brush = NULL;
    HBRUSH OldBrush = NULL;
    HPEN Pen = NULL;
    HPEN OldPen = NULL;
    HRGN Region = NULL;
    PULONG Pixels = NULL;
    PBYTE MaskBits = NULL;
    ULONG ApiFailures = 0;
    ULONG HookFailures = 0;
    ULONG Hook;
    ULONG Index;
    LONG X = 24;
    LONG Y = 24;
    LONG Width;
    LONG Height;
    BOOL Saved = FALSE;
    BOOL StatsReady = FALSE;
    RPI5VC4_GDI_STATS ApiBefore;
    POINT PlgPoints[3];
    COLORREF PlgInsideFirst;
    COLORREF PlgInsideCenter;
    COLORREF PlgOutsideBefore;
    COLORREF PlgOutsideAfter;
    COLORREF PlgExpectedCenter;
    COLORREF PlgMaskCopied;
    COLORREF PlgMaskBlocked;
    COLORREF PlgMaskExpected;

    Rpi5GdiPrint("RPI5_GDI_BEGIN\n");
    Display = GetDC(NULL);
    if (Display == NULL)
        goto Cleanup;

    Width = min(GDI_TEST_WIDTH, GetDeviceCaps(Display, HORZRES) - X);
    Height = min(GDI_TEST_HEIGHT, GetDeviceCaps(Display, VERTRES) - Y);
    if (Width < 280 || Height < 176)
        goto Cleanup;

    SourceDc = CreateCompatibleDC(Display);
    SaveDc = CreateCompatibleDC(Display);
    SourceBitmap = Rpi5CreateSourceBitmap(Display, &Pixels, &BitmapInfo);
    SaveBitmap = CreateCompatibleBitmap(Display, Width, Height);
    ZeroMemory(&MaskInfo, sizeof(MaskInfo));
    MaskInfo.Header.biSize = sizeof(MaskInfo.Header);
    MaskInfo.Header.biWidth = GDI_MASK_WIDTH;
    MaskInfo.Header.biHeight = -GDI_MASK_HEIGHT;
    MaskInfo.Header.biPlanes = 1;
    MaskInfo.Header.biBitCount = 1;
    MaskInfo.Header.biCompression = BI_RGB;
    MaskInfo.Header.biSizeImage = GDI_MASK_STRIDE * GDI_MASK_HEIGHT;
    MaskInfo.Colors[1].rgbBlue = 0xff;
    MaskInfo.Colors[1].rgbGreen = 0xff;
    MaskInfo.Colors[1].rgbRed = 0xff;
    MaskBitmap = CreateDIBSection(Display,
                                  (PBITMAPINFO)&MaskInfo,
                                  DIB_RGB_COLORS,
                                  (PVOID *)&MaskBits,
                                  NULL,
                                  0);
    if (SourceDc == NULL || SaveDc == NULL ||
        SourceBitmap == NULL || SaveBitmap == NULL ||
        MaskBitmap == NULL || MaskBits == NULL)
    {
        goto Cleanup;
    }
    ZeroMemory(MaskBits, MaskInfo.Header.biSizeImage);
    for (Index = 0; Index < GDI_MASK_HEIGHT; Index++)
    {
        MaskBits[Index * GDI_MASK_STRIDE] = 0xff;
        MaskBits[Index * GDI_MASK_STRIDE + 1] = 0xff;
        MaskBits[Index * GDI_MASK_STRIDE + 2] = 0xff;
    }

    OldSourceBitmap = SelectObject(SourceDc, SourceBitmap);
    OldSaveBitmap = SelectObject(SaveDc, SaveBitmap);
    if (OldSourceBitmap == NULL || OldSaveBitmap == NULL)
        goto Cleanup;

    Saved = BitBlt(SaveDc, 0, 0, Width, Height,
                   Display, X, Y, SRCCOPY);
    if (!Saved || !Rpi5QueryGdiStats(Display, &Before))
        goto Cleanup;
    StatsReady = TRUE;

    if (!BitBlt(Display, X, Y, 48, 48, SourceDc, 0, 0, SRCCOPY))
        ApiFailures++;

    if (GetPixel(Display, X + 4, Y + 4) == CLR_INVALID)
        ApiFailures++;

    Pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 0));
    if (Pen == NULL)
    {
        ApiFailures++;
    }
    else
    {
        OldPen = SelectObject(Display, Pen);
        if (!MoveToEx(Display, X + 4, Y + 56, NULL) ||
            !LineTo(Display, X + 116, Y + 56))
        {
            ApiFailures++;
        }
        SelectObject(Display, OldPen);
        OldPen = NULL;
    }

    Brush = CreateSolidBrush(RGB(16, 112, 224));
    Region = CreateRectRgn(X + 128, Y, X + 184, Y + 48);
    if (Brush == NULL || Region == NULL ||
        !FillRgn(Display, Region, Brush))
    {
        ApiFailures++;
    }

    if (Brush != NULL)
        OldBrush = SelectObject(Display, Brush);

    if (!StretchBlt(Display, X + 192, Y, 80, 48,
                    SourceDc, 0, 0, GDI_SOURCE_SIZE,
                    GDI_SOURCE_SIZE, SRCCOPY))
    {
        ApiFailures++;
    }

    Blend.BlendOp = AC_SRC_OVER;
    Blend.BlendFlags = 0;
    Blend.SourceConstantAlpha = 160;
    Blend.AlphaFormat = 0;
    if (!GdiAlphaBlend(Display, X, Y + 72, 64, 48,
                       SourceDc, 0, 0, GDI_SOURCE_SIZE,
                       GDI_SOURCE_SIZE, Blend))
    {
        ApiFailures++;
    }

    if (!GdiTransparentBlt(Display, X + 80, Y + 72, 64, 48,
                           SourceDc, 0, 0, GDI_SOURCE_SIZE,
                           GDI_SOURCE_SIZE, RGB(255, 0, 255)))
    {
        ApiFailures++;
    }

    ZeroMemory(Vertices, sizeof(Vertices));
    Vertices[0].x = X + 160;
    Vertices[0].y = Y + 72;
    Vertices[0].Red = 0xFFFF;
    Vertices[0].Blue = 0x2000;
    Vertices[1].x = X + 272;
    Vertices[1].y = Y + 120;
    Vertices[1].Green = 0xFFFF;
    Vertices[1].Blue = 0xFFFF;
    Gradient.UpperLeft = 0;
    Gradient.LowerRight = 1;
    if (!GdiGradientFill(Display, Vertices, 2, &Gradient, 1,
                         GRADIENT_FILL_RECT_H))
    {
        ApiFailures++;
    }

    PlgPoints[0].x = X + 200;
    PlgPoints[0].y = Y + 128;
    PlgPoints[1].x = X + 256;
    PlgPoints[1].y = Y + 132;
    PlgPoints[2].x = X + 204;
    PlgPoints[2].y = Y + 168;
    PlgOutsideBefore = GetPixel(Display, X + 259, Y + 128);
    if (!Rpi5QueryGdiStats(Display, &ApiBefore) ||
        !PlgBlt(Display, PlgPoints, SourceDc, 0, 0, 48, 32,
                NULL, 0, 0) ||
        !Rpi5GdiApiPublished(Display, "PlgBlt", &ApiBefore))
    {
        ApiFailures++;
    }
    else
    {
        PlgInsideFirst = GetPixel(Display, X + 200, Y + 128);
        PlgInsideCenter = GetPixel(Display, X + 230, Y + 150);
        PlgOutsideAfter = GetPixel(Display, X + 259, Y + 128);
        PlgExpectedCenter = RGB((Pixels[16 * GDI_SOURCE_SIZE + 24] >> 16) & 0xff,
                                (Pixels[16 * GDI_SOURCE_SIZE + 24] >> 8) & 0xff,
                                Pixels[16 * GDI_SOURCE_SIZE + 24] & 0xff);
        Rpi5GdiPrint("RPI5_GDI_PLGBLT_PIXELS first=%08lx center=%08lx "
                     "outside-before=%08lx outside-after=%08lx\n",
                     PlgInsideFirst,
                     PlgInsideCenter,
                     PlgOutsideBefore,
                     PlgOutsideAfter);
        if (PlgInsideFirst != RGB(255, 0, 255) ||
            PlgInsideCenter != PlgExpectedCenter ||
            PlgOutsideBefore == CLR_INVALID ||
            PlgOutsideAfter != PlgOutsideBefore)
        {
            ApiFailures++;
        }
    }

    PlgPoints[0].x = X + 200;
    PlgPoints[0].y = Y + 128;
    PlgPoints[1].x = X + 248;
    PlgPoints[1].y = Y + 128;
    PlgPoints[2].x = X + 200;
    PlgPoints[2].y = Y + 160;
    if (!PatBlt(Display, X + 200, Y + 128,
                GDI_MASK_WIDTH, GDI_MASK_HEIGHT, BLACKNESS) ||
        !Rpi5QueryGdiStats(Display, &ApiBefore) ||
        !PlgBlt(Display, PlgPoints, SourceDc,
                0, 0, GDI_MASK_WIDTH, GDI_MASK_HEIGHT,
                MaskBitmap, 0, 0) ||
        !Rpi5GdiApiPublished(Display, "PlgBltMask", &ApiBefore))
    {
        ApiFailures++;
    }
    else
    {
        PlgMaskCopied = GetPixel(Display, X + 212, Y + 144);
        PlgMaskBlocked = GetPixel(Display, X + 236, Y + 144);
        PlgMaskExpected = RGB((Pixels[16 * GDI_SOURCE_SIZE + 12] >> 16) & 0xff,
                              (Pixels[16 * GDI_SOURCE_SIZE + 12] >> 8) & 0xff,
                              Pixels[16 * GDI_SOURCE_SIZE + 12] & 0xff);
        Rpi5GdiPrint("RPI5_GDI_PLGBLT_MASK copied=%08lx blocked=%08lx\n",
                     PlgMaskCopied,
                     PlgMaskBlocked);
        if (PlgMaskCopied != PlgMaskExpected ||
            PlgMaskBlocked != RGB(0, 0, 0))
        {
            ApiFailures++;
        }
    }

    if (!Rpi5QueryGdiStats(Display, &ApiBefore) ||
        !BeginPath(Display) ||
        !MoveToEx(Display, X + 8, Y + 128, NULL) ||
        !LineTo(Display, X + 56, Y + 136) ||
        !LineTo(Display, X + 16, Y + 144) ||
        !EndPath(Display) ||
        !StrokePath(Display) ||
        !Rpi5GdiApiPublished(Display, "StrokePath", &ApiBefore))
    {
        AbortPath(Display);
        ApiFailures++;
    }

    if (!Rpi5QueryGdiStats(Display, &ApiBefore) ||
        !BeginPath(Display) ||
        !MoveToEx(Display, X + 64, Y + 128, NULL) ||
        !LineTo(Display, X + 104, Y + 128) ||
        !LineTo(Display, X + 84, Y + 148) ||
        !CloseFigure(Display) ||
        !EndPath(Display) ||
        !FillPath(Display) ||
        !Rpi5GdiApiPublished(Display, "FillPath", &ApiBefore))
    {
        AbortPath(Display);
        ApiFailures++;
    }

    if (!Rpi5QueryGdiStats(Display, &ApiBefore) ||
        !BeginPath(Display) ||
        !MoveToEx(Display, X + 112, Y + 128, NULL) ||
        !LineTo(Display, X + 152, Y + 128) ||
        !LineTo(Display, X + 132, Y + 148) ||
        !CloseFigure(Display) ||
        !EndPath(Display) ||
        !StrokeAndFillPath(Display) ||
        !Rpi5GdiApiPublished(Display, "StrokeAndFillPath", &ApiBefore))
    {
        AbortPath(Display);
        ApiFailures++;
    }

    SetBkMode(Display, TRANSPARENT);
    SetTextColor(Display, RGB(255, 255, 255));
    if (!Rpi5QueryGdiStats(Display, &ApiBefore) ||
        !TextOutW(Display, X, Y + 160, L"XPDM GDI", 8) ||
        !Rpi5GdiApiPublished(Display, "TextOut", &ApiBefore))
        ApiFailures++;

    if (!Rpi5QueryGdiStats(Display, &ApiBefore) ||
        !SetPixelV(Display, X + 112, Y + 164, RGB(255, 64, 64)) ||
        !Rpi5GdiApiPublished(Display, "SetPixel", &ApiBefore))
        ApiFailures++;

    GdiFlush();
    if (!Rpi5QueryGdiStats(Display, &After))
        goto Cleanup;

    for (Hook = 0; Hook < RPI5VC4_GDI_HOOK_COUNT; Hook++)
    {
        Rpi5GdiPrint("RPI5_GDI_HOOK name=%s attempts=%lu hardware=%lu\n",
                     Rpi5GdiHookNames[Hook],
                     After.HookAttempts[Hook] - Before.HookAttempts[Hook],
                     After.HookHardwarePresents[Hook] -
                         Before.HookHardwarePresents[Hook]);
    }
#if (NTDDI_VERSION >= NTDDI_VISTA)
    Rpi5GdiPrint("RPI5_GDI_HOOK_SKIPPED name=Paint "
                 "reason=NT6+-EngPaint-via-BitBlt\n");
#endif

    for (Index = 0; Index < ARRAYSIZE(RequiredHooks); Index++)
    {
        Hook = RequiredHooks[Index];
        if (!Rpi5RequiredHookPassed(Hook, &Before, &After))
        {
            HookFailures++;
            Rpi5GdiPrint("RPI5_GDI_HOOK_FAIL name=%s\n",
                         Rpi5GdiHookNames[Hook]);
        }
    }

    Rpi5GdiPrint("RPI5_GDI_STATS presents=%lu cpu-fallback=%lu busy=%lu failure=%lu last-status=%lu last-size=%lux%lu\n",
                 After.HardwarePresentCount - Before.HardwarePresentCount,
                 After.CpuFallbackCount - Before.CpuFallbackCount,
                 After.BusyFallbackCount - Before.BusyFallbackCount,
                 After.FailureFallbackCount - Before.FailureFallbackCount,
                 After.LastHardwareStatus,
                 After.LastWidth,
                 After.LastHeight);

Cleanup:
    if (Saved && SaveDc != NULL && Display != NULL)
    {
        BitBlt(Display, X, Y, Width, Height, SaveDc, 0, 0, SRCCOPY);
        GdiFlush();
    }
    if (OldPen != NULL && Display != NULL)
        SelectObject(Display, OldPen);
    if (OldBrush != NULL && Display != NULL)
        SelectObject(Display, OldBrush);
    if (OldSourceBitmap != NULL && SourceDc != NULL)
        SelectObject(SourceDc, OldSourceBitmap);
    if (OldSaveBitmap != NULL && SaveDc != NULL)
        SelectObject(SaveDc, OldSaveBitmap);
    if (Region != NULL)
        DeleteObject(Region);
    if (Brush != NULL)
        DeleteObject(Brush);
    if (Pen != NULL)
        DeleteObject(Pen);
    if (SourceBitmap != NULL)
        DeleteObject(SourceBitmap);
    if (MaskBitmap != NULL)
        DeleteObject(MaskBitmap);
    if (SaveBitmap != NULL)
        DeleteObject(SaveBitmap);
    if (SourceDc != NULL)
        DeleteDC(SourceDc);
    if (SaveDc != NULL)
        DeleteDC(SaveDc);
    if (Display != NULL)
        ReleaseDC(NULL, Display);

    if (!StatsReady)
    {
        Rpi5GdiPrint("RPI5_GDI_FAIL setup-or-stats\n");
        return 1;
    }
    if (ApiFailures != 0 || HookFailures != 0 ||
        After.HardwarePresentCount == Before.HardwarePresentCount ||
        After.FailureFallbackCount != Before.FailureFallbackCount)
    {
        Rpi5GdiPrint("RPI5_GDI_FAIL api=%lu hooks=%lu\n",
                     ApiFailures, HookFailures);
        return 1;
    }

    Rpi5GdiPrint("RPI5_GDI_PASS\n");
    return 0;
}
