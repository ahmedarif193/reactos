/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Video support for linear framebuffers
 * COPYRIGHT:   Authors of uefivid.c and xboxvideo.c
 *              Copyright 2025-2026 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include <freeldr.h>
#include "vidfb.h"
#include "vgafont.h"

#ifdef UEFIBOOT
#include <bootfont/bootfont.h>
#endif

#include <debug.h>
DBG_DEFAULT_CHANNEL(UI);

/* This is used to introduce artificial symmetric borders at the top and bottom */
#define TOP_BOTTOM_LINES 0

/* GLOBALS ********************************************************************/

typedef struct _FRAMEBUFFER_INFO
{
    ULONG_PTR BaseAddress;
    ULONG BufferSize;

    /* Horizontal and Vertical resolution in pixels */
    ULONG ScreenWidth;
    ULONG ScreenHeight;

    /* Number of pixel elements per video memory line */
    ULONG PixelsPerScanLine; // aka. "Pitch" or "ScreenStride", but Stride is in bytes or bits...
    ULONG BitsPerPixel;      // aka. "PixelStride".

    /* Physical format of the pixel for BPP > 8, specified by bit-mask */
    PIXEL_BITMASK PixelMasks;

/** Calculated values */

    ULONG BytesPerPixel;
    ULONG Delta;             // aka. "Pitch": actual size in bytes of a scanline.

    UCHAR RedShift;
    UCHAR GreenShift;
    UCHAR BlueShift;
    UCHAR ReservedShift;
    UCHAR RedBits;
    UCHAR GreenBits;
    UCHAR BlueBits;
    UCHAR ReservedBits;
} FRAMEBUFFER_INFO, *PFRAMEBUFFER_INFO;

static FRAMEBUFFER_INFO framebufInfo = {0};
static CM_FRAMEBUF_DEVICE_DATA FrameBufferData = {0};
static PUCHAR FbConsCachedTextBuffer = NULL;
static ULONG FbConsCachedTextBufferSize = 0;
static BOOLEAN FbConsCachedTextBufferValid = FALSE;
static BOOLEAN VidFbDirtyValid = FALSE;
static ULONG VidFbDirtyLeft = 0;
static ULONG VidFbDirtyTop = 0;
static ULONG VidFbDirtyRight = 0;
static ULONG VidFbDirtyBottom = 0;

static VOID
FbConsReleaseTextCache(VOID);

#ifdef UEFIBOOT
static BOOT_FONT_RENDERER FbConsFont = {0};
#endif


/* FUNCTIONS ******************************************************************/

VOID
FbConsResetDirtyRect(VOID)
{
    VidFbDirtyValid = FALSE;
    VidFbDirtyLeft = 0;
    VidFbDirtyTop = 0;
    VidFbDirtyRight = 0;
    VidFbDirtyBottom = 0;
}

VOID
FbConsMarkDirtyRect(
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    if ((Width == 0) || (Height == 0))
        return;

    if ((X >= framebufInfo.ScreenWidth) || (Y >= framebufInfo.ScreenHeight))
        return;

    Width = min(Width, framebufInfo.ScreenWidth - X);
    Height = min(Height, framebufInfo.ScreenHeight - Y);

    if (!VidFbDirtyValid)
    {
        VidFbDirtyLeft = X;
        VidFbDirtyTop = Y;
        VidFbDirtyRight = X + Width;
        VidFbDirtyBottom = Y + Height;
        VidFbDirtyValid = TRUE;
        return;
    }

    VidFbDirtyLeft = min(VidFbDirtyLeft, X);
    VidFbDirtyTop = min(VidFbDirtyTop, Y);
    VidFbDirtyRight = max(VidFbDirtyRight, X + Width);
    VidFbDirtyBottom = max(VidFbDirtyBottom, Y + Height);
}

BOOLEAN
FbConsTakeDirtyRect(
    _Out_ PULONG X,
    _Out_ PULONG Y,
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    if (!VidFbDirtyValid)
        return FALSE;

    *X = VidFbDirtyLeft;
    *Y = VidFbDirtyTop;
    *Width = VidFbDirtyRight - VidFbDirtyLeft;
    *Height = VidFbDirtyBottom - VidFbDirtyTop;

    FbConsResetDirtyRect();
    return TRUE;
}

#ifdef UEFIBOOT
static
__inline
UCHAR
VidFbMaskShift(
    _In_ ULONG Mask)
{
    UCHAR Shift = 0;

    if (!Mask)
        return 0;

    while ((Mask & 1) == 0)
    {
        ++Shift;
        Mask >>= 1;
    }

    return Shift;
}

VOID
VidFbSetFrameBuffer(
    _In_ ULONG_PTR BaseAddress,
    _In_ ULONG BufferSize,
    _In_ UINT32 PixelsPerScanLine,
    _In_ UINT32 BitsPerPixel,
    _In_ PPIXEL_BITMASK PixelMasks)
{
    framebufInfo.BaseAddress = BaseAddress;
    framebufInfo.BufferSize = BufferSize;
    framebufInfo.PixelsPerScanLine = PixelsPerScanLine;
    framebufInfo.BitsPerPixel = BitsPerPixel;
    framebufInfo.BytesPerPixel = (BitsPerPixel + 7) / 8;
    framebufInfo.Delta = (PixelsPerScanLine * framebufInfo.BytesPerPixel + 3) & ~3;

    RtlCopyMemory(&framebufInfo.PixelMasks,
                  PixelMasks,
                  sizeof(framebufInfo.PixelMasks));

    framebufInfo.RedShift = VidFbMaskShift(framebufInfo.PixelMasks.RedMask);
    framebufInfo.GreenShift = VidFbMaskShift(framebufInfo.PixelMasks.GreenMask);
    framebufInfo.BlueShift = VidFbMaskShift(framebufInfo.PixelMasks.BlueMask);
    framebufInfo.ReservedShift = VidFbMaskShift(framebufInfo.PixelMasks.ReservedMask);

    framebufInfo.RedBits = (UCHAR)CountNumberOfBits(framebufInfo.PixelMasks.RedMask);
    framebufInfo.GreenBits = (UCHAR)CountNumberOfBits(framebufInfo.PixelMasks.GreenMask);
    framebufInfo.BlueBits = (UCHAR)CountNumberOfBits(framebufInfo.PixelMasks.BlueMask);
    framebufInfo.ReservedBits = (UCHAR)CountNumberOfBits(framebufInfo.PixelMasks.ReservedMask);

    FbConsResetDirtyRect();
}

static
__inline
ULONG
VidFbScaleColorComponentToMask(
    _In_ UCHAR Component,
    _In_ UCHAR Bits)
{
    ULONG MaxValue;

    if (Bits == 0)
        return 0;

    MaxValue = (1UL << Bits) - 1;
    return (Component * MaxValue + 127) / 255;
}

static
__inline
UCHAR
VidFbScaleColorComponentFromMask(
    _In_ ULONG Component,
    _In_ UCHAR Bits)
{
    ULONG MaxValue;

    if (Bits == 0)
        return 0;

    MaxValue = (1UL << Bits) - 1;
    return (UCHAR)((Component * 255 + MaxValue / 2) / MaxValue);
}

static
__inline
UINT32
VidFbComposePixel(
    _In_ UCHAR Red,
    _In_ UCHAR Green,
    _In_ UCHAR Blue)
{
    UINT32 Pixel = 0;

    Pixel |= (VidFbScaleColorComponentToMask(Red, framebufInfo.RedBits)
              << framebufInfo.RedShift) & framebufInfo.PixelMasks.RedMask;
    Pixel |= (VidFbScaleColorComponentToMask(Green, framebufInfo.GreenBits)
              << framebufInfo.GreenShift) & framebufInfo.PixelMasks.GreenMask;
    Pixel |= (VidFbScaleColorComponentToMask(Blue, framebufInfo.BlueBits)
              << framebufInfo.BlueShift) & framebufInfo.PixelMasks.BlueMask;

    if (framebufInfo.ReservedBits != 0)
    {
        Pixel |= (VidFbScaleColorComponentToMask(0xFF, framebufInfo.ReservedBits)
                  << framebufInfo.ReservedShift) & framebufInfo.PixelMasks.ReservedMask;
    }

    return Pixel;
}

static
__inline
VOID
VidFbExpandPixel(
    _In_ UINT32 Pixel,
    _Out_ PUCHAR Red,
    _Out_ PUCHAR Green,
    _Out_ PUCHAR Blue)
{
    *Red = VidFbScaleColorComponentFromMask(
        (Pixel & framebufInfo.PixelMasks.RedMask) >> framebufInfo.RedShift,
        framebufInfo.RedBits);
    *Green = VidFbScaleColorComponentFromMask(
        (Pixel & framebufInfo.PixelMasks.GreenMask) >> framebufInfo.GreenShift,
        framebufInfo.GreenBits);
    *Blue = VidFbScaleColorComponentFromMask(
        (Pixel & framebufInfo.PixelMasks.BlueMask) >> framebufInfo.BlueShift,
        framebufInfo.BlueBits);
}

static
__inline
UINT32
VidFbBlendPixels(
    _In_ UINT32 BackgroundPixel,
    _In_ UINT32 ForegroundPixel,
    _In_ UCHAR Alpha)
{
    UCHAR BgRed, BgGreen, BgBlue;
    UCHAR FgRed, FgGreen, FgBlue;
    UCHAR OutRed, OutGreen, OutBlue;

    if (Alpha == 0)
        return BackgroundPixel;

    if (Alpha == 0xFF)
        return ForegroundPixel;

    VidFbExpandPixel(BackgroundPixel, &BgRed, &BgGreen, &BgBlue);
    VidFbExpandPixel(ForegroundPixel, &FgRed, &FgGreen, &FgBlue);

    OutRed   = (UCHAR)((BgRed   * (255 - Alpha) + FgRed   * Alpha + 127) / 255);
    OutGreen = (UCHAR)((BgGreen * (255 - Alpha) + FgGreen * Alpha + 127) / 255);
    OutBlue  = (UCHAR)((BgBlue  * (255 - Alpha) + FgBlue  * Alpha + 127) / 255);

    return VidFbComposePixel(OutRed, OutGreen, OutBlue);
}

static
VOID
VidFbFillRect(
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ UINT32 Color)
{
    ULONG Line, Col;
    PUINT32 Pixel;

    if ((Width == 0) || (Height == 0))
        return;

    if ((X >= framebufInfo.ScreenWidth) ||
        (Y >= (framebufInfo.ScreenHeight - 2 * TOP_BOTTOM_LINES)))
    {
        return;
    }

    Width = min(Width, framebufInfo.ScreenWidth - X);
    Height = min(Height, (framebufInfo.ScreenHeight - 2 * TOP_BOTTOM_LINES) - Y);

    for (Line = 0; Line < Height; ++Line)
    {
        Pixel = (PUINT32)((PUCHAR)framebufInfo.BaseAddress +
                          (Y + TOP_BOTTOM_LINES + Line) * framebufInfo.Delta +
                          X * sizeof(UINT32));

        for (Col = 0; Col < Width; ++Col)
            Pixel[Col] = Color;
    }

    FbConsMarkDirtyRect(X, Y + TOP_BOTTOM_LINES, Width, Height);
}

static
VOID
VidFbOutputBitmapChar(
    _In_ UCHAR Char,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ UINT32 FgColor,
    _In_ UINT32 BgColor)
{
    const UCHAR* FontPtr;
    PUINT32 Pixel;
    UCHAR Mask;
    ULONG Line, Col;

    if ((X + CHAR_WIDTH - 1 >= framebufInfo.ScreenWidth) ||
        (Y + CHAR_HEIGHT - 1 >= (framebufInfo.ScreenHeight - 2 * TOP_BOTTOM_LINES)))
    {
        return;
    }

    FontPtr = BitmapFont8x16 + Char * CHAR_HEIGHT;
    Pixel = (PUINT32)((PUCHAR)framebufInfo.BaseAddress +
                      (Y + TOP_BOTTOM_LINES) * framebufInfo.Delta +
                      X * sizeof(UINT32));

    for (Line = 0; Line < CHAR_HEIGHT; ++Line)
    {
        Mask = 0x80;
        for (Col = 0; Col < CHAR_WIDTH; ++Col)
        {
            Pixel[Col] = ((FontPtr[Line] & Mask) != 0) ? FgColor : BgColor;
            Mask >>= 1;
        }
        Pixel = (PUINT32)((PUCHAR)Pixel + framebufInfo.Delta);
    }

    FbConsMarkDirtyRect(X, Y + TOP_BOTTOM_LINES, CHAR_WIDTH, CHAR_HEIGHT);
}

static
__inline
ULONG
FbConsCellWidth(VOID)
{
    return FbConsFont.Enabled ? FbConsFont.CellWidth : CHAR_WIDTH;
}

static
__inline
ULONG
FbConsCellHeight(VOID)
{
    return FbConsFont.Enabled ? FbConsFont.CellHeight : CHAR_HEIGHT;
}

static
__inline
ULONG
FbConsWidth(VOID)
{
    return max(framebufInfo.ScreenWidth / FbConsCellWidth(), 1UL);
}

static
__inline
ULONG
FbConsHeight(VOID)
{
    ULONG VisibleHeight = framebufInfo.ScreenHeight - 2 * TOP_BOTTOM_LINES;
    return max(VisibleHeight / FbConsCellHeight(), 1UL);
}

static
VOID
FbConsInitializeFontRenderer(VOID)
{
    if (!BootFontInitialize(&FbConsFont,
                            framebufInfo.ScreenWidth,
                            framebufInfo.ScreenHeight - 2 * TOP_BOTTOM_LINES))
    {
        return;
    }

    TRACE("Using FreeType framebuffer font at %lu px (%lux%lu cells, console %lux%lu)\n",
          FbConsFont.PixelHeight,
          FbConsFont.CellWidth,
          FbConsFont.CellHeight,
          FbConsWidth(),
          FbConsHeight());
}
#endif

#if DBG
static VOID
VidFbPrintFramebufferInfo(VOID)
{
    TRACE("Framebuffer format:\n");
    TRACE("    BaseAddress       : 0x%X\n", framebufInfo.BaseAddress);
    TRACE("    BufferSize        : %lu\n", framebufInfo.BufferSize);
    TRACE("    ScreenWidth       : %lu\n", framebufInfo.ScreenWidth);
    TRACE("    ScreenHeight      : %lu\n", framebufInfo.ScreenHeight);
    TRACE("    PixelsPerScanLine : %lu\n", framebufInfo.PixelsPerScanLine);
    TRACE("    BitsPerPixel      : %lu\n", framebufInfo.BitsPerPixel);
    TRACE("    BytesPerPixel     : %lu\n", framebufInfo.BytesPerPixel);
    TRACE("    Delta             : %lu\n", framebufInfo.Delta);
    TRACE("    ARGB masks:       : %08x/%08x/%08x/%08x\n",
          framebufInfo.PixelMasks.ReservedMask,
          framebufInfo.PixelMasks.RedMask,
          framebufInfo.PixelMasks.GreenMask,
          framebufInfo.PixelMasks.BlueMask);
}
#endif

/**
 * @brief
 * Initializes internal framebuffer information based on the given parameters.
 *
 * @param[in]   BaseAddress
 * The framebuffer physical base address.
 *
 * @param[in]   BufferSize
 * The framebuffer size, in bytes.
 *
 * @param[in]   ScreenWidth
 * @param[in]   ScreenHeight
 * The width and height of the visible framebuffer area, in pixels.
 *
 * @param[in]   PixelsPerScanLine
 * The size in number of pixels of a whole horizontal video memory scanline.
 *
 * @param[in]   BitsPerPixel
 * The number of usable bits (not counting the reserved ones) per pixel.
 *
 * @param[in]   PixelMasks
 * Optional pointer to a PIXEL_BITMASK structure describing the pixel
 * format used by the framebuffer.
 *
 * @return
 * TRUE if initialization is successful; FALSE if not.
 **/
BOOLEAN
VidFbInitializeVideo(
    _Out_opt_ PCM_FRAMEBUF_DEVICE_DATA* pFbData,
    _In_ ULONG_PTR BaseAddress,
    _In_ ULONG BufferSize,
    _In_ UINT32 ScreenWidth,
    _In_ UINT32 ScreenHeight,
    _In_ UINT32 PixelsPerScanLine,
    _In_ UINT32 BitsPerPixel,
    _In_opt_ PPIXEL_BITMASK PixelMasks)
{
    PPIXEL_BITMASK BitMasks = &framebufInfo.PixelMasks;

    if (pFbData)
        *pFbData = NULL;

    FbConsReleaseTextCache();
    FbConsResetDirtyRect();
    RtlZeroMemory(&framebufInfo, sizeof(framebufInfo));

    /* Verify framebuffer dimensions */
    if ((ScreenWidth < 1) || (ScreenHeight < 1))
    {
        ERR("Invalid framebuffer dimensions\n");
        return FALSE;
    }

    framebufInfo.BaseAddress  = BaseAddress;
    framebufInfo.BufferSize   = BufferSize;
    framebufInfo.ScreenWidth  = ScreenWidth;
    framebufInfo.ScreenHeight = ScreenHeight;
    framebufInfo.PixelsPerScanLine = PixelsPerScanLine;
    framebufInfo.BitsPerPixel = BitsPerPixel;

    framebufInfo.BytesPerPixel = (BitsPerPixel + 7) / 8; // Round up to nearest byte.
    framebufInfo.Delta = (PixelsPerScanLine * framebufInfo.BytesPerPixel + 3) & ~3;

    /* Verify that the framebuffer fits inside the video RAM */
    if (!(ScreenHeight * framebufInfo.Delta <= BufferSize))
    {
        ERR("Framebuffer doesn't fit inside the video RAM (FB size: %lu, VRAM size: %lu)\n",
            ScreenHeight * framebufInfo.Delta, BufferSize);
        return FALSE;
    }

    /* We currently only support 32bpp */
    if (BitsPerPixel != 32)
    {
        /* Unsupported BPP */
        ERR("Unsupported %lu bits per pixel format\n", BitsPerPixel);
        return FALSE;
    }

    //ASSERT((BitsPerPixel <= 8 && !PixelMasks) || (BitsPerPixel > 8));
    if (BitsPerPixel > 8)
    {
        if (!PixelMasks ||
            (PixelMasks->RedMask   == 0 &&
             PixelMasks->GreenMask == 0 &&
             PixelMasks->BlueMask  == 0 /* &&
             PixelMasks->ReservedMask == 0 */))
        {
            /* Determine pixel mask given color depth and color channel */
            switch (BitsPerPixel)
            {
                case 32:
                case 24: /* 8:8:8 */
                    BitMasks->RedMask   = 0x00FF0000; // 0x00FF0000;
                    BitMasks->GreenMask = 0x0000FF00; // 0x00FF0000 >> 8;
                    BitMasks->BlueMask  = 0x000000FF; // 0x00FF0000 >> 16;
                    BitMasks->ReservedMask = ((1 << (BitsPerPixel - 24)) - 1) << 24;
                    break;

                case 16: /* 5:6:5 */
                    BitMasks->RedMask   = 0xF800; // 0xF800;
                    BitMasks->GreenMask = 0x07E0; // (0xF800 >> 5) | 0x20;
                    BitMasks->BlueMask  = 0x001F; // 0xF800 >> 11;
                    BitMasks->ReservedMask = 0;
                    break;

                case 15: /* 5:5:5 */
                    BitMasks->RedMask   = 0x7C00; // 0x7C00;
                    BitMasks->GreenMask = 0x03E0; // 0x7C00 >> 5;
                    BitMasks->BlueMask  = 0x001F; // 0x7C00 >> 10;
                    BitMasks->ReservedMask = 0x8000;
                    break;

                default:
                    /* Unsupported BPP */
                    UNIMPLEMENTED;
                    RtlZeroMemory(BitMasks, sizeof(*BitMasks));
            }
        }
        else
        {
            /* Copy the pixel masks */
            RtlCopyMemory(BitMasks, PixelMasks, sizeof(*BitMasks));
        }
    }
    else
    {
        /* Palettized modes don't use masks */
        RtlZeroMemory(BitMasks, sizeof(*BitMasks));
    }

    framebufInfo.RedShift = VidFbMaskShift(BitMasks->RedMask);
    framebufInfo.GreenShift = VidFbMaskShift(BitMasks->GreenMask);
    framebufInfo.BlueShift = VidFbMaskShift(BitMasks->BlueMask);
    framebufInfo.ReservedShift = VidFbMaskShift(BitMasks->ReservedMask);

    framebufInfo.RedBits = (UCHAR)CountNumberOfBits(BitMasks->RedMask);
    framebufInfo.GreenBits = (UCHAR)CountNumberOfBits(BitMasks->GreenMask);
    framebufInfo.BlueBits = (UCHAR)CountNumberOfBits(BitMasks->BlueMask);
    framebufInfo.ReservedBits = (UCHAR)CountNumberOfBits(BitMasks->ReservedMask);

#if DBG
    VidFbPrintFramebufferInfo();
    {
    ULONG BppFromMasks =
        PixelBitmasksToBpp(BitMasks->RedMask,
                           BitMasks->GreenMask,
                           BitMasks->BlueMask,
                           BitMasks->ReservedMask);
        TRACE("BitsPerPixel = %lu , BppFromMasks = %lu\n", BitsPerPixel, BppFromMasks);
    //ASSERT(BitsPerPixel == BppFromMasks);
    }
#endif

    /* Initialize the hardware device configuration data if specified */
    if (pFbData)
    {
        FrameBufferData.FrameBufferOffset = 0;
        FrameBufferData.ScreenWidth  = framebufInfo.ScreenWidth;
        FrameBufferData.ScreenHeight = framebufInfo.ScreenHeight;
        FrameBufferData.PixelsPerScanLine = framebufInfo.PixelsPerScanLine;
        FrameBufferData.BitsPerPixel = framebufInfo.BitsPerPixel;
        FrameBufferData.Dpi = LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT;

        RtlCopyMemory(&FrameBufferData.PixelMasks,
                      &framebufInfo.PixelMasks, sizeof(framebufInfo.PixelMasks));

        *pFbData = &FrameBufferData;
    }

#ifdef UEFIBOOT
    FbConsInitializeFontRenderer();

    if (!FbConsFont.Enabled)
    {
        TRACE("FreeType framebuffer font unavailable, falling back to the built-in VGA font\n");
    }
#endif

    return TRUE;
}

VOID
VidFbClearScreenColor(
    _In_ UINT32 Color,
    _In_ BOOLEAN FullScreen)
{
    ULONG Line, Col;
    PUINT32 p;

    for (Line = 0; Line < framebufInfo.ScreenHeight - (FullScreen ? 0 : 2 * TOP_BOTTOM_LINES); Line++)
    {
        p = (PUINT32)((PUCHAR)framebufInfo.BaseAddress + (Line + (FullScreen ? 0 : TOP_BOTTOM_LINES)) * framebufInfo.Delta);
        for (Col = 0; Col < framebufInfo.ScreenWidth; Col++)
        {
            *p++ = Color;
        }
    }

    FbConsMarkDirtyRect(0,
                        FullScreen ? 0 : TOP_BOTTOM_LINES,
                        framebufInfo.ScreenWidth,
                        framebufInfo.ScreenHeight - (FullScreen ? 0 : 2 * TOP_BOTTOM_LINES));
}

/**
 * @brief
 * Displays a character at a given pixel position with specific foreground
 * and background colors.
 **/
VOID
VidFbOutputChar(
    _In_ UCHAR Char,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ UINT32 FgColor,
    _In_ UINT32 BgColor)
{
#ifdef UEFIBOOT
    const BOOT_FONT_GLYPH* Glyph;
    ULONG GlyphX, GlyphY;
    ULONG Row, Col;
    PUINT32 Pixel;
    const UCHAR* Bitmap;

    if (!FbConsFont.Enabled)
    {
        VidFbOutputBitmapChar(Char, X, Y, FgColor, BgColor);
        return;
    }

    if ((X + FbConsCellWidth() - 1 >= framebufInfo.ScreenWidth) ||
        (Y + FbConsCellHeight() - 1 >= (framebufInfo.ScreenHeight - 2 * TOP_BOTTOM_LINES)))
    {
        return;
    }

    VidFbFillRect(X, Y, FbConsCellWidth(), FbConsCellHeight(), BgColor);

    Glyph = BootFontGetGlyph(&FbConsFont, Char);
    if (!Glyph)
    {
        VidFbOutputBitmapChar(Char,
                              X + (FbConsCellWidth() - CHAR_WIDTH) / 2,
                              Y + (FbConsCellHeight() - CHAR_HEIGHT) / 2,
                              FgColor,
                              BgColor);
        return;
    }

    if ((Glyph->Width == 0) || (Glyph->Height == 0))
        return;

    GlyphX = X + (ULONG)max((LONG)Glyph->Left, 0L);
    GlyphY = Y + FbConsFont.Baseline - Glyph->Top;

    for (Row = 0; Row < Glyph->Height; ++Row)
    {
        if (GlyphY + Row >= (framebufInfo.ScreenHeight - 2 * TOP_BOTTOM_LINES))
            break;

        Pixel = (PUINT32)((PUCHAR)framebufInfo.BaseAddress +
                          (GlyphY + TOP_BOTTOM_LINES + Row) * framebufInfo.Delta +
                          GlyphX * sizeof(UINT32));
        Bitmap = BootFontGetGlyphBitmap(&FbConsFont, Glyph) + Row * Glyph->Pitch;

        for (Col = 0; Col < Glyph->Width; ++Col)
        {
            if (GlyphX + Col >= framebufInfo.ScreenWidth)
                break;

            if (Bitmap[Col] != 0)
                Pixel[Col] = VidFbBlendPixels(Pixel[Col], FgColor, Bitmap[Col]);
        }
    }
#else
    VidFbOutputBitmapChar(Char, X, Y, FgColor, BgColor);
#endif
}

/**
 * @brief
 * Returns the width and height in pixels, of the whole visible area
 * of the graphics framebuffer.
 **/
VOID
VidFbGetDisplaySize(
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG Depth)
{
    *Width  = framebufInfo.ScreenWidth;
    *Height = framebufInfo.ScreenHeight - 2 * TOP_BOTTOM_LINES;
    *Depth  = framebufInfo.BitsPerPixel;
}

/**
 * @brief
 * Returns the size in bytes, of a full graphics pixel buffer rectangle
 * that can fill the whole visible area of the graphics framebuffer.
 **/
ULONG
VidFbGetBufferSize(VOID)
{
    return ((framebufInfo.ScreenHeight - 2 * TOP_BOTTOM_LINES) *
            framebufInfo.ScreenWidth * framebufInfo.BytesPerPixel);
}

VOID
VidFbScrollUp(
    _In_ UINT32 Color,
    _In_ ULONG Scroll)
{
    ULONG VisibleHeight = framebufInfo.ScreenHeight - 2 * TOP_BOTTOM_LINES;
    ULONG Line, Col;
    PUINT32 Pixel;
    PUCHAR Dst;
    PUCHAR Src;

    if (Scroll == 0)
        return;

    if (Scroll >= VisibleHeight)
    {
        VidFbClearScreenColor(Color, FALSE);
        return;
    }

    for (Line = 0; Line < VisibleHeight - Scroll; ++Line)
    {
        Dst = (PUCHAR)framebufInfo.BaseAddress +
              (TOP_BOTTOM_LINES + Line) * framebufInfo.Delta;
        Src = (PUCHAR)framebufInfo.BaseAddress +
              (TOP_BOTTOM_LINES + Line + Scroll) * framebufInfo.Delta;

        RtlMoveMemory(Dst, Src, framebufInfo.ScreenWidth * sizeof(UINT32));
    }

    for (; Line < VisibleHeight; ++Line)
    {
        Pixel = (PUINT32)((PUCHAR)framebufInfo.BaseAddress +
                          (TOP_BOTTOM_LINES + Line) * framebufInfo.Delta);

        for (Col = 0; Col < framebufInfo.ScreenWidth; ++Col)
            Pixel[Col] = Color;
    }

    FbConsMarkDirtyRect(0, TOP_BOTTOM_LINES, framebufInfo.ScreenWidth, VisibleHeight);
}

#if 0
VOID
VidFbSetTextCursorPosition(UCHAR X, UCHAR Y)
{
    /* We don't have a cursor yet */
}

VOID
VidFbHideShowTextCursor(BOOLEAN Show)
{
    /* We don't have a cursor yet */
}

BOOLEAN
VidFbIsPaletteFixed(VOID)
{
    return FALSE;
}

VOID
VidFbSetPaletteColor(
    _In_ UCHAR Color,
    _In_ UCHAR Red, _In_ UCHAR Green, _In_ UCHAR Blue)
{
    /* Not supported */
}

VOID
VidFbGetPaletteColor(
    _In_ UCHAR Color,
    _Out_ PUCHAR Red, _Out_ PUCHAR Green, _Out_ PUCHAR Blue)
{
    /* Not supported */
}
#endif



/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 *              or MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Linear framebuffer based console support
 * COPYRIGHT:   Copyright 2025-2026 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#define VGA_CHAR_SIZE 2

static VOID
FbConsReleaseTextCache(VOID)
{
    if (FbConsCachedTextBuffer)
    {
        MmFreeMemory(FbConsCachedTextBuffer);
        FbConsCachedTextBuffer = NULL;
    }

    FbConsCachedTextBufferSize = 0;
    FbConsCachedTextBufferValid = FALSE;
}

static VOID
FbConsInvalidateTextCache(VOID)
{
    FbConsCachedTextBufferValid = FALSE;
}

static VOID
FbConsUpdateTextCacheCell(
    _In_ UCHAR Char,
    _In_ UCHAR Attr,
    _In_ ULONG Column,
    _In_ ULONG Row)
{
    PUCHAR Cell;
    ULONG Width = FbConsWidth();
    ULONG Height = FbConsHeight();
    ULONG Offset;

    if (!FbConsCachedTextBufferValid)
        return;

    if ((Column >= Width) || (Row >= Height))
        return;

    Offset = (Row * Width + Column) * VGA_CHAR_SIZE;
    if (Offset + VGA_CHAR_SIZE > FbConsCachedTextBufferSize)
    {
        FbConsInvalidateTextCache();
        return;
    }

    Cell = FbConsCachedTextBuffer + Offset;
    Cell[0] = Char;
    Cell[1] = Attr;
}

static VOID
FbConsClearTextCache(
    _In_ UCHAR Attr)
{
    ULONG Index;
    ULONG Width = FbConsWidth();
    ULONG Height = FbConsHeight();
    ULONG BufferSize = Width * Height * VGA_CHAR_SIZE;
    PUCHAR Cell = FbConsCachedTextBuffer;

    if (!FbConsCachedTextBufferValid)
        return;

    if (BufferSize != FbConsCachedTextBufferSize)
    {
        FbConsInvalidateTextCache();
        return;
    }

    for (Index = 0; Index < Width * Height; ++Index)
    {
        Cell[0] = ' ';
        Cell[1] = Attr;
        Cell += VGA_CHAR_SIZE;
    }
}

VOID
FbConsScrollTextCache(
    _In_ UCHAR Attr,
    _In_ ULONG Lines)
{
    ULONG Index;
    ULONG Width = FbConsWidth();
    ULONG Height = FbConsHeight();
    ULONG RowSize = Width * VGA_CHAR_SIZE;
    ULONG BufferSize = Height * RowSize;
    ULONG ScrollSize;
    PUCHAR Cell;

    if (!FbConsCachedTextBufferValid)
        return;

    if ((Height == 0) || (RowSize == 0) || (BufferSize != FbConsCachedTextBufferSize))
    {
        FbConsInvalidateTextCache();
        return;
    }

    if (Lines == 0)
        return;

    if (Lines >= Height)
    {
        FbConsClearTextCache(Attr);
        return;
    }

    ScrollSize = Lines * RowSize;
    RtlMoveMemory(FbConsCachedTextBuffer, FbConsCachedTextBuffer + ScrollSize, BufferSize - ScrollSize);

    Cell = FbConsCachedTextBuffer + BufferSize - ScrollSize;
    for (Index = 0; Index < Lines * Width; ++Index)
    {
        Cell[0] = ' ';
        Cell[1] = Attr;
        Cell += VGA_CHAR_SIZE;
    }
}

static BOOLEAN
FbConsEnsureTextCache(
    _In_ ULONG BufferSize)
{
    if (!BufferSize)
        return FALSE;

    if (FbConsCachedTextBufferSize != BufferSize)
        FbConsReleaseTextCache();

    if (!FbConsCachedTextBuffer)
    {
        FbConsCachedTextBuffer = MmAllocateMemoryWithType(BufferSize, LoaderFirmwareTemporary);
        if (!FbConsCachedTextBuffer)
            return FALSE;

        FbConsCachedTextBufferSize = BufferSize;
        FbConsCachedTextBufferValid = FALSE;
    }

    return TRUE;
}

static inline
UINT32
FbConsAttrToSingleColor(
    _In_ UCHAR Attr)
{
    UCHAR Intensity = ((Attr & 0x08) == 0) ? 127 : 255;

    return VidFbComposePixel((Attr & 0x04) ? Intensity : 0,
                             (Attr & 0x02) ? Intensity : 0,
                             (Attr & 0x01) ? Intensity : 0);
}

/**
 * @brief
 * Maps a text-mode CGA-style character attribute to separate
 * foreground and background colors in the framebuffer's native format.
 **/
static VOID
FbConsAttrToColors(
    _In_ UCHAR Attr,
    _Out_ PUINT32 FgColor,
    _Out_ PUINT32 BgColor)
{
    *FgColor = FbConsAttrToSingleColor(Attr & 0x0F);
    *BgColor = FbConsAttrToSingleColor((Attr >> 4) & 0x0F);
}

VOID
FbConsClearScreen(
    _In_ UCHAR Attr)
{
    UINT32 FgColor, BgColor;
    FbConsAttrToColors(Attr, &FgColor, &BgColor);
    VidFbClearScreenColor(BgColor, FALSE);
    FbConsClearTextCache(Attr);
}

/**
 * @brief
 * Displays a character at a given position with specific foreground
 * and background colors.
 **/
VOID
FbConsOutputChar(
    _In_ UCHAR Char,
    _In_ ULONG Column,
    _In_ ULONG Row,
    _In_ UINT32 FgColor,
    _In_ UINT32 BgColor)
{
    /* Don't display outside of the screen */
    if ((Column >= FbConsWidth()) || (Row >= FbConsHeight()))
        return;

    VidFbOutputChar(Char,
                    Column * FbConsCellWidth(),
                    Row * FbConsCellHeight(),
                    FgColor,
                    BgColor);
}

/**
 * @brief
 * Displays a character with specific text attributes at a given position.
 **/
VOID
FbConsPutChar(
    _In_ UCHAR Char,
    _In_ UCHAR Attr,
    _In_ ULONG Column,
    _In_ ULONG Row)
{
    UINT32 FgColor, BgColor;

    FbConsAttrToColors(Attr, &FgColor, &BgColor);
    FbConsOutputChar(Char, Column, Row, FgColor, BgColor);
    FbConsUpdateTextCacheCell(Char, Attr, Column, Row);
}

/**
 * @brief
 * Returns the width and height in number of CGA characters/attributes, of a
 * full text-mode CGA-style character buffer rectangle that can fill the whole console.
 **/
VOID
FbConsGetDisplaySize(
    _Out_ PULONG Width,
    _Out_ PULONG Height,
    _Out_ PULONG Depth)
{
    // VidFbGetDisplaySize(Width, Height, Depth);
    // *Width  /= CHAR_WIDTH;
    // *Height /= CHAR_HEIGHT;
    *Width = FbConsWidth();
    *Height = FbConsHeight();
    *Depth = framebufInfo.BitsPerPixel;
}

VOID
FbConsGetCellSize(
    _Out_ PULONG Width,
    _Out_ PULONG Height)
{
    *Width = FbConsCellWidth();
    *Height = FbConsCellHeight();
}

/**
 * @brief
 * Draws a progress bar in framebuffer pixels using text-grid coordinates.
 **/
VOID
FbConsDrawProgressBar(
    _In_ ULONG Left,
    _In_ ULONG Right,
    _In_ ULONG Row,
    _In_ UCHAR FillAttr,
    _In_ UCHAR EmptyAttr,
    _In_ ULONG SubPercentTimes100)
{
    UINT32 FillColor, EmptyColor, Dummy;
    ULONG CellWidth, CellHeight;
    ULONG Margin, BarLeft, BarTop, BarWidth, BarHeight;
    ULONG FillWidth;

    if ((Left > Right) || (Row >= FbConsHeight()))
        return;

    CellWidth = FbConsCellWidth();
    CellHeight = FbConsCellHeight();
    Margin = (CellHeight > 2) ? 1 : 0;

    BarLeft = Left * CellWidth;
    BarTop = Row * CellHeight + Margin;
    BarWidth = (Right - Left + 1) * CellWidth;
    BarHeight = max(CellHeight - 2 * Margin, 1UL);
    FillWidth = BarWidth * min(SubPercentTimes100, 100UL * 100UL) / (100 * 100);

    FbConsAttrToColors(FillAttr, &Dummy, &FillColor);
    FbConsAttrToColors(EmptyAttr, &Dummy, &EmptyColor);

    if (FillWidth > 0)
    {
        VidFbFillRect(BarLeft, BarTop,
                      FillWidth, BarHeight,
                      FillColor);
    }
    if (FillWidth < BarWidth)
    {
        VidFbFillRect(BarLeft + FillWidth, BarTop,
                      BarWidth - FillWidth, BarHeight,
                      EmptyColor);
    }
}

/**
 * @brief
 * Returns the size in bytes, of a full text-mode CGA-style
 * character buffer rectangle that can fill the whole console.
 **/
ULONG
FbConsGetBufferSize(VOID)
{
    return FbConsHeight() * FbConsWidth() * VGA_CHAR_SIZE;
}

/**
 * @brief
 * Copies a full text-mode CGA-style character buffer rectangle to the console.
 **/
// TODO: Write a VidFb "BitBlt" equivalent.
VOID
FbConsCopyOffScreenBufferToVRAM(
    _In_ PVOID Buffer)
{
    PUCHAR OffScreenBuffer = (PUCHAR)Buffer;
    PUCHAR CachedBuffer;
    ULONG Row, Col;
    ULONG BufferSize;
    BOOLEAN CacheReady;
    BOOLEAN RedrawAll;
    // ULONG Width, Height, Depth;
    // FbConsGetDisplaySize(&Width, &Height, &Depth);
    ULONG Width = FbConsWidth();
    ULONG Height = FbConsHeight();

    if (!OffScreenBuffer)
        return;

    BufferSize = Height * Width * VGA_CHAR_SIZE;
    CacheReady = FbConsEnsureTextCache(BufferSize);
    RedrawAll = !CacheReady || !FbConsCachedTextBufferValid;
    CachedBuffer = FbConsCachedTextBuffer;

    for (Row = 0; Row < Height; ++Row)
    {
        for (Col = 0; Col < Width; ++Col)
        {
            if (RedrawAll ||
                (CachedBuffer[0] != OffScreenBuffer[0]) ||
                (CachedBuffer[1] != OffScreenBuffer[1]))
            {
                UINT32 FgColor, BgColor;

                FbConsAttrToColors(OffScreenBuffer[1], &FgColor, &BgColor);
                FbConsOutputChar(OffScreenBuffer[0], Col, Row, FgColor, BgColor);

                if (CacheReady)
                {
                    CachedBuffer[0] = OffScreenBuffer[0];
                    CachedBuffer[1] = OffScreenBuffer[1];
                }
            }

            OffScreenBuffer += VGA_CHAR_SIZE;
            if (CacheReady)
                CachedBuffer += VGA_CHAR_SIZE;
        }
    }

    if (CacheReady)
        FbConsCachedTextBufferValid = TRUE;
}

VOID
FbConsClearScrollArea(
    _In_ UCHAR Attr,
    _In_ ULONG Lines)
{
    UINT32 BgColor, Dummy;
    ULONG VisibleHeight = framebufInfo.ScreenHeight - 2 * TOP_BOTTOM_LINES;
    ULONG Scroll;

    Lines = min(Lines, FbConsHeight());
    Scroll = min(Lines * FbConsCellHeight(), VisibleHeight);
    if (Scroll == 0)
        return;

    FbConsAttrToColors(Attr, &Dummy, &BgColor);
    VidFbFillRect(0, VisibleHeight - Scroll, framebufInfo.ScreenWidth, Scroll, BgColor);
}

VOID
FbConsScrollUp(
    _In_ UCHAR Attr,
    _In_ ULONG Lines)
{
    UINT32 BgColor, Dummy;

    Lines = min(Lines, FbConsHeight());

    FbConsAttrToColors(Attr, &Dummy, &BgColor);
    VidFbScrollUp(BgColor, Lines * FbConsCellHeight());
    FbConsScrollTextCache(Attr, Lines);
}
