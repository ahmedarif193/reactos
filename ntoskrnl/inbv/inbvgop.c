/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UEFI GOP-specific boot UI helpers (separate from legacy path)
 * Copyright : Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/arc/arc.h>
#include <drivers/bootvid/bootvid.h>
#include "logo.h"
#include "resource.h"
#include "inbvgop.h"
#include "reactos_gop_logo.h"

extern BOOLEAN ShowProgressBar;

typedef struct tagBITMAPINFOHEADER
{
    ULONG  biSize;
    LONG   biWidth;
    LONG   biHeight;
    USHORT biPlanes;
    USHORT biBitCount;
    ULONG  biCompression;
    ULONG  biSizeImage;
    LONG   biXPelsPerMeter;
    LONG   biYPelsPerMeter;
    ULONG  biClrUsed;
    ULONG  biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;

#ifndef BI_RGB
#define BI_RGB 0
#endif
#ifndef BI_RLE4
#define BI_RLE4 2
#endif

CODE_SEG("INIT")
static
BOOLEAN
InbvGopQueryInfo(
    _Out_ LOADER_PARAMETER_FRAMEBUFFER *FbInfo)
{
    if (!FbInfo)
        return FALSE;

    RtlZeroMemory(FbInfo, sizeof(*FbInfo));

    if (!InbvGetGopFrameBufferInfo(FbInfo))
        return FALSE;

    if (FbInfo->FrameBufferSize == 0 ||
        FbInfo->HorizontalResolution == 0 ||
        FbInfo->VerticalResolution == 0)
    {
        return FALSE;
    }

    return TRUE;
}

static ULONG InbvMaskShift(ULONG Mask)
{
    ULONG Shift = 0;
    if (!Mask) return 0;
    while ((Mask & 1) == 0) { Shift++; Mask >>= 1; }
    return Shift;
}

static ULONG InbvMaskMax(ULONG Mask)
{
    ULONG Value = 0;
    while (Mask) { Value = (Value << 1) | 1; Mask >>= 1; }
    return Value;
}

static ULONG InbvPackColor(ULONG RedMask, ULONG GreenMask, ULONG BlueMask,
                           ULONG RedShift, ULONG GreenShift, ULONG BlueShift,
                           ULONG RedMax, ULONG GreenMax, ULONG BlueMax,
                           UCHAR r, UCHAR g, UCHAR b)
{
    ULONG R = RedMask   ? ((ULONG)r * RedMax   + 127) / 255 : 0;
    ULONG G = GreenMask ? ((ULONG)g * GreenMax + 127) / 255 : 0;
    ULONG B = BlueMask  ? ((ULONG)b * BlueMax  + 127) / 255 : 0;
    return (RedMask   ? ((R << RedShift)   & RedMask)   : 0) |
           (GreenMask ? ((G << GreenShift) & GreenMask) : 0) |
           (BlueMask  ? ((B << BlueShift)  & BlueMask)  : 0);
}

static
BOOLEAN
InbvGopDrawWordmark(
    _In_ ULONG ScreenWidth,
    _In_ ULONG ScreenHeight)
{
    const ULONG BottomMargin = 32;
    const ULONG BitmapFileHeaderSize = 14; /* sizeof(BITMAPFILEHEADER) */
    PUCHAR Bitmap = (PUCHAR)g_ReactOSGopLogoBmp + BitmapFileHeaderSize;
    PBITMAPINFOHEADER Header;
    ULONG SrcWidth, SrcHeight;
    ULONG DstWidth, DstHeight;
    ULONG DestX, DestY;
    BOOLEAN TopDown = FALSE;

    LOADER_PARAMETER_FRAMEBUFFER FbInfo;
    ULONG RedMask = 0, GreenMask = 0, BlueMask = 0;
    ULONG RedShift = 0, GreenShift = 0, BlueShift = 0;
    ULONG RedMax = 0, GreenMax = 0, BlueMax = 0;

    Header = (PBITMAPINFOHEADER)Bitmap;
    if (!Header || Header->biPlanes != 1 || Header->biBitCount != 4)
        return FALSE;

    if (Header->biCompression != BI_RGB && Header->biCompression != BI_RLE4)
        return FALSE;

    SrcWidth = (ULONG)Header->biWidth;
    SrcHeight = (Header->biHeight < 0) ? (ULONG)(-Header->biHeight) : (ULONG)Header->biHeight;
    TopDown = (Header->biHeight < 0);
    if (!SrcWidth || !SrcHeight)
        return FALSE;

    /* Generic downscale: set desired percentage here */
    const ULONG SCALE_NUM = 40;   /* percent numerator */
    const ULONG SCALE_DEN = 100;  /* percent denominator */
    DstWidth = (ULONG)(((ULONGLONG)SrcWidth * SCALE_NUM + (SCALE_DEN - 1)) / SCALE_DEN);
    if (DstWidth == 0) DstWidth = 1;
    DstHeight = (ULONG)(((ULONGLONG)SrcHeight * SCALE_NUM + (SCALE_DEN - 1)) / SCALE_DEN);
    if (DstHeight == 0) DstHeight = 1;

    DestX = (ScreenWidth > DstWidth) ? (ScreenWidth - DstWidth) / 2 : 0;
    if (ScreenHeight > DstHeight + BottomMargin)
        DestY = ScreenHeight - DstHeight - BottomMargin;
    else
        DestY = 0;

    if (!InbvGopQueryInfo(&FbInfo))
        return FALSE;

    RedMask = FbInfo.RedMask; GreenMask = FbInfo.GreenMask; BlueMask = FbInfo.BlueMask;
    RedShift = InbvMaskShift(RedMask); GreenShift = InbvMaskShift(GreenMask); BlueShift = InbvMaskShift(BlueMask);
    RedMax = InbvMaskMax(RedMask >> RedShift); GreenMax = InbvMaskMax(GreenMask >> GreenShift); BlueMax = InbvMaskMax(BlueMask >> BlueShift);

    /* Build 32-bit palette */
    ULONG PaletteCount = Header->biClrUsed ? Header->biClrUsed : 16;
    if (PaletteCount == 0)
        PaletteCount = 16;
    if (PaletteCount > 16)
        PaletteCount = 16;
    typedef struct _BMP_RGBQUAD { UCHAR b,g,r,a; } BMP_RGBQUAD;
    BMP_RGBQUAD* Pal = (BMP_RGBQUAD*)(Bitmap + Header->biSize);
    ULONG Palette32[16] = {0};
    for (ULONG i = 0; i < 16; i++)
    {
        UCHAR r = (i < PaletteCount) ? Pal[i].r : Pal[0].r;
        UCHAR g = (i < PaletteCount) ? Pal[i].g : Pal[0].g;
        UCHAR b = (i < PaletteCount) ? Pal[i].b : Pal[0].b;
        Palette32[i] = InbvPackColor(RedMask,GreenMask,BlueMask,RedShift,GreenShift,BlueShift,RedMax,GreenMax,BlueMax,r,g,b);
    }

    /* Locate pixel data */
    PUCHAR Bits = Bitmap + Header->biSize + PaletteCount * sizeof(BMP_RGBQUAD);
    LONG SrcDelta = ((SrcWidth + 1) / 2);
    SrcDelta = (SrcDelta + 3) & ~3;

    /* Helper to extract 8-bit channel from packed color */
#define INBV_EXTRACT(C,Mask,Shift,Maxv) \
    ((Mask) ? ((Maxv) ? ((((((C) & (Mask)) >> (Shift)) * 255) + ((Maxv)/2)) / (Maxv)) : 0) : 0)

    for (ULONG dy = 0; dy < DstHeight; dy++)
    {
        ULONG sy = (ULONG)min((ULONGLONG)SrcHeight - 1,
                              ((ULONGLONG)dy * SCALE_DEN) / SCALE_NUM);
        ULONG effRow = TopDown ? sy : (SrcHeight - 1 - sy);
        PUCHAR Row = Bits + effRow * SrcDelta;

        /* Write scaled row in chunks to the screen using BootVID */
        static ULONG LineBuf[1024];
        ULONG produced = 0;
        while (produced < DstWidth)
        {
            ULONG toDo = min((ULONG)1024, DstWidth - produced);
            for (ULONG dx = 0; dx < toDo; dx++)
            {
                /* Map destination x back to source and do a simple 2x2 box filter for smoothing */
                ULONG sx0 = (ULONG)min((ULONGLONG)SrcWidth - 1,
                                       ((ULONGLONG)(produced + dx) * SCALE_DEN) / SCALE_NUM);
                ULONG sy0 = sy;
                ULONG sx1 = min(sx0 + 1, SrcWidth  - 1);
                ULONG sy1 = min(sy0 + 1, SrcHeight - 1);

                /* Fetch 4 neighboring indices */
                UCHAR b00 = Row[sx0 / 2];
                UCHAR i00 = (sx0 & 1) ? (b00 & 0x0F) : ((b00 >> 4) & 0x0F);

                UCHAR b10 = Row[sx1 / 2];
                UCHAR i10 = (sx1 & 1) ? (b10 & 0x0F) : ((b10 >> 4) & 0x0F);

                /* For y+1 we need the next source row */
                ULONG effRow1 = TopDown ? sy1 : (SrcHeight - 1 - sy1);
                PUCHAR Row1 = Bits + effRow1 * SrcDelta;

                UCHAR b01 = Row1[sx0 / 2];
                UCHAR i01 = (sx0 & 1) ? (b01 & 0x0F) : ((b01 >> 4) & 0x0F);
                UCHAR b11 = Row1[sx1 / 2];
                UCHAR i11 = (sx1 & 1) ? (b11 & 0x0F) : ((b11 >> 4) & 0x0F);

                /* Unpack to 8-bit RGB by reversing the screen format */
                ULONG c00 = Palette32[i00];
                ULONG c10 = Palette32[i10];
                ULONG c01 = Palette32[i01];
                ULONG c11 = Palette32[i11];

                /* Extract and normalize to 0..255 */
                ULONG r = (INBV_EXTRACT(c00, RedMask,   RedShift,   RedMax)   +
                           INBV_EXTRACT(c10, RedMask,   RedShift,   RedMax)   +
                           INBV_EXTRACT(c01, RedMask,   RedShift,   RedMax)   +
                           INBV_EXTRACT(c11, RedMask,   RedShift,   RedMax)) / 4;
                ULONG g = (INBV_EXTRACT(c00, GreenMask, GreenShift, GreenMax) +
                           INBV_EXTRACT(c10, GreenMask, GreenShift, GreenMax) +
                           INBV_EXTRACT(c01, GreenMask, GreenShift, GreenMax) +
                           INBV_EXTRACT(c11, GreenMask, GreenShift, GreenMax)) / 4;
                ULONG b = (INBV_EXTRACT(c00, BlueMask,  BlueShift,  BlueMax)  +
                           INBV_EXTRACT(c10, BlueMask,  BlueShift,  BlueMax)  +
                           INBV_EXTRACT(c01, BlueMask,  BlueShift,  BlueMax)  +
                           INBV_EXTRACT(c11, BlueMask,  BlueShift,  BlueMax)) / 4;

                LineBuf[dx] = InbvPackColor(RedMask,GreenMask,BlueMask,
                                             RedShift,GreenShift,BlueShift,
                                             RedMax,GreenMax,BlueMax,
                                             (UCHAR)r,(UCHAR)g,(UCHAR)b);
            }
            VidBufferToScreenBlt((PUCHAR)LineBuf,
                                 DestX + produced,
                                 DestY + dy,
                                 toDo,
                                 1,
                                 toDo * sizeof(ULONG));
            produced += toDo;
        }
    }

    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
InbvGopHandleBootBitmap(
    _In_ BOOLEAN TextMode)
{
    LOADER_PARAMETER_FRAMEBUFFER FbInfo;
    ULONG Width, Height;
    BOOLEAN BgrtActive;

    if (!InbvGopQueryInfo(&FbInfo))
        return FALSE;

    Width = FbInfo.HorizontalResolution;
    Height = FbInfo.VerticalResolution;
    BgrtActive = InbvQueryBgrtInfo(NULL);
    UNREFERENCED_PARAMETER(BgrtActive);

    if (TextMode)
    {
        /* Debug/Text mode: raw text only, like legacy console. */
        InbvSetTextColor(BV_COLOR_WHITE);
        InbvSolidColorFill(0, 0, Width - 1, Height - 1, BV_COLOR_BLACK);
        InbvSetScrollRegion(0, 0, Width - 1, Height - 1);
        ShowProgressBar = FALSE;
        InbvEnableDisplayString(TRUE);
        return TRUE;
    }

    /* Non-debug: minimal quiet mode with just the ReactOS wordmark. */
    InbvSetTextColor(BV_COLOR_WHITE);
    /* Do not clear the framebuffer here: preserve any firmware BGRT */

    if (!InbvGopDrawWordmark(Width, Height))
    {
        static const CHAR LoadingMsg[] = "ReactOS";
        const ULONG CharW = 8;
        const ULONG CharH = 13;
        ULONG msgPx = (ULONG)(sizeof(LoadingMsg) - 1) * CharW;
        ULONG x = (Width > msgPx) ? ((Width - msgPx) / 2) : 0;
        ULONG y = (Height > (CharH + 4)) ? (Height - (CharH + 4)) : 0;
        VidDisplayStringXY((PUCHAR)LoadingMsg, x, y, TRUE);
    }

    ShowProgressBar = FALSE;
    InbvEnableDisplayString(FALSE);
    return TRUE;
}
