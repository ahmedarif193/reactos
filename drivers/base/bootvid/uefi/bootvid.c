/*
 * PROJECT:     ReactOS Boot Video Driver for UEFI GOP systems
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     UEFI Graphics Output Protocol support
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF (arif.ing@outlook.com)
 */

/* UEFI bootvid - standalone driver without VGA dependencies */
#include "precomp.h"
#include <reactos/arc/arc.h>
BOOLEAN NTAPI InbvGetGopFrameBufferInfo(_Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo);
/* Memory cache type for MmMapIoSpace */
#define MmNonCached 0

/* We reuse the BOOTVID 8x13 font and default palette from common sources */
extern UCHAR VidpFontData[];
extern const RGBQUAD VidpDefaultPalette[BV_MAX_COLORS];

typedef struct _BMP_RGBQUAD
{
    UCHAR Blue;
    UCHAR Green;
    UCHAR Red;
    UCHAR Reserved;
} BMP_RGBQUAD, *PBMP_RGBQUAD;

/* BITMAPINFOHEADER and BI_RGB/BI_RLE4 are provided by precomp.h */

/* No external kernel fallbacks; use InbvGetGopFrameBufferInfo only. */

/* Color extractors provided by precomp.h */

/* GLOBALS ********************************************************************/

static PHYSICAL_ADDRESS FrameBufferBase = {0};
static ULONG FrameBufferSize = 0;
static ULONG ScreenWidth = 0;
static ULONG ScreenHeight = 0;
static ULONG PixelsPerScanLine = 0;
static ULONG PixelFormat = 0;
static ULONG RedMask = 0;
static ULONG GreenMask = 0;
static ULONG BlueMask = 0;
static ULONG RedShift = 0;
static ULONG GreenShift = 0;
static ULONG BlueShift = 0;
static ULONG RedMax = 0;
static ULONG GreenMax = 0;
static ULONG BlueMax = 0;
static PULONG FrameBuffer = NULL;
static BOOLEAN DisplayInitialized = FALSE;

/* Text and scrolling state */
static ULONG CurrentX = 0;
static ULONG CurrentY = 0;
static ULONG ScrollLeft = 0;
static ULONG ScrollTop = 0;
static ULONG ScrollRight = 0;
static ULONG ScrollBottom = 0;
static ULONG TextColorIndex = BV_COLOR_WHITE;
static ULONG TextColorValue = 0xFFFFFFFF;
static ULONG BackgroundColorValue = 0x00000000;

/* UEFI GOP Pixel Formats */
#define PixelRedGreenBlueReserved8BitPerColor  0
#define PixelBlueGreenRedReserved8BitPerColor  1
#define PixelBitMask                           2
#define PixelBltOnly                           3

/* PRIVATE FUNCTIONS *********************************************************/

static inline ULONG
UefiLinePitch(VOID)
{
    return PixelsPerScanLine * sizeof(ULONG);
}

static ULONG
UefiMaskShift(ULONG Mask)
{
    ULONG Shift = 0;

    if (!Mask)
        return 0;

    while ((Mask & 1) == 0)
    {
        Mask >>= 1;
        Shift++;
    }

    return Shift;
}

static ULONG
UefiMaskMax(ULONG Mask)
{
    ULONG Value = 0;

    while (Mask)
    {
        Value <<= 1;
        Value |= 1;
        Mask >>= 1;
    }

    return Value;
}

static ULONG
UefiPackColor(UCHAR Red, UCHAR Green, UCHAR Blue)
{
    /* Prefer using masks provided by the loader (covers RGB, BGR and BitMask) */
    if (RedMask || GreenMask || BlueMask)
    {
        ULONG R = RedMask ? ((ULONG)Red * RedMax + 127) / 255 : 0;
        ULONG G = GreenMask ? ((ULONG)Green * GreenMax + 127) / 255 : 0;
        ULONG B = BlueMask ? ((ULONG)Blue * BlueMax + 127) / 255 : 0;

        return (RedMask ? (R << RedShift) & RedMask : 0) |
               (GreenMask ? (G << GreenShift) & GreenMask : 0) |
               (BlueMask ? (B << BlueShift) & BlueMask : 0);
    }

    /* Fallback on PixelFormat if masks are unavailable */
    switch (PixelFormat)
    {
        case PixelRedGreenBlueReserved8BitPerColor:
            return ((ULONG)Red << 16) | ((ULONG)Green << 8) | Blue;  /* 0x00RRGGBB */
        case PixelBlueGreenRedReserved8BitPerColor:
            return ((ULONG)Blue << 16) | ((ULONG)Green << 8) | Red;  /* 0x00BBGGRR */
        default:
            return ((ULONG)Blue << 16) | ((ULONG)Green << 8) | Red;  /* conservative */
    }
}

static ULONG
UefiColorFromIndex(UCHAR Index)
{
    ULONG Entry;

    if (Index == BV_COLOR_NONE)
        return 0;

    if (Index >= BV_MAX_COLORS)
        Index %= BV_MAX_COLORS;

    Entry = VidpDefaultPalette[Index];
    return UefiPackColor(GetRValue(Entry), GetGValue(Entry), GetBValue(Entry));
}

static VOID
UefiGopClearScreen(ULONG Color)
{
    ULONG y;

    if (!FrameBuffer)
        return;

    for (y = 0; y < ScreenHeight; y++)
    {
        PUCHAR Row = (PUCHAR)FrameBuffer + y * UefiLinePitch();
        RtlFillMemoryUlong(Row, (SIZE_T)ScreenWidth * sizeof(ULONG), Color);
    }
}

static VOID
UefiGopFillRect(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom, ULONG Color)
{
    ULONG y;

    if (!FrameBuffer)
        return;

    if (Left > Right || Top > Bottom)
        return;

    if (Right >= ScreenWidth)
        Right = ScreenWidth - 1;
    if (Bottom >= ScreenHeight)
        Bottom = ScreenHeight - 1;

    for (y = Top; y <= Bottom; y++)
    {
        PUCHAR Row = (PUCHAR)FrameBuffer + y * UefiLinePitch() + Left * sizeof(ULONG);
        RtlFillMemoryUlong(Row, (SIZE_T)(Right - Left + 1) * sizeof(ULONG), Color);
    }
}

static VOID
UefiGopSetPixel(ULONG x, ULONG y, ULONG Color)
{
    PUCHAR Pixel;

    if (!FrameBuffer || x >= ScreenWidth || y >= ScreenHeight)
        return;

    Pixel = (PUCHAR)FrameBuffer + y * UefiLinePitch() + x * sizeof(ULONG);
    *((PULONG)Pixel) = Color;
}

/* Internal scrolling helper used by DoScroll */
static VOID
UefiScrollRegion(ULONG Lines, ULONG Left, ULONG Top, ULONG Right, ULONG Bottom)
{
    if (Top >= Bottom) return;
    ULONG Height = Bottom - Top + 1;
    if (Lines >= Height)
    {
        UefiGopFillRect(Left, Top, Right, Bottom, BackgroundColorValue);
        return;
    }

    SIZE_T RowBytes = (SIZE_T)(Right - Left + 1) * sizeof(ULONG);
    ULONG y = 0;
    for (; y < Height - Lines; y++)
    {
        PUCHAR DestRow = (PUCHAR)FrameBuffer + (Top + y) * UefiLinePitch() + Left * sizeof(ULONG);
        PUCHAR SrcRow  = (PUCHAR)FrameBuffer + (Top + y + Lines) * UefiLinePitch() + Left * sizeof(ULONG);
        RtlMoveMemory(DestRow, SrcRow, RowBytes);
    }
    for (; y < Height; y++)
    {
        PUCHAR DestRow = (PUCHAR)FrameBuffer + (Top + y) * UefiLinePitch() + Left * sizeof(ULONG);
        RtlFillMemoryUlong(DestRow, (SIZE_T)(Right - Left + 1) * sizeof(ULONG), BackgroundColorValue);
    }
}

static VOID
UefiScroll(ULONG Lines)
{
    UefiScrollRegion(Lines, ScrollLeft, ScrollTop, ScrollRight, ScrollBottom);
}

static VOID
UefiDrawGlyph(ULONG Left, ULONG Top, UCHAR Character, ULONG FgColor, ULONG BgColor, BOOLEAN Opaque)
{
    ULONG Row, Column;
    PUCHAR Glyph;

    if (!FrameBuffer)
        return;

    Glyph = &VidpFontData[Character * BOOTCHAR_HEIGHT];

    for (Row = 0; Row < BOOTCHAR_HEIGHT; Row++)
    {
        UCHAR Bits = Glyph[Row];

        for (Column = 0; Column < BOOTCHAR_WIDTH; Column++)
        {
            BOOLEAN Set = (Bits & (0x80 >> Column)) != 0;

            if (Set)
            {
                UefiGopSetPixel(Left + Column, Top + Row, FgColor);
            }
            else if (Opaque)
            {
                UefiGopSetPixel(Left + Column, Top + Row, BgColor);
            }
        }
    }

    if (Opaque)
        UefiGopFillRect(Left, Top + BOOTCHAR_HEIGHT, Left + BOOTCHAR_WIDTH - 1, Top + BOOTCHAR_HEIGHT, BgColor);
}

static VOID
UefiBlit4bpp(const PUCHAR Source,
             ULONG Left,
             ULONG Top,
             ULONG Width,
             ULONG Height,
             LONG Delta,
             BOOLEAN TopDown,
             const ULONG *Palette)
{
    LONG Row;

    for (Row = 0; Row < (LONG)Height; Row++)
    {
        LONG SourceRow = TopDown ? Row : ((LONG)Height - 1 - Row);
        const PUCHAR Scan = Source + SourceRow * Delta;
        ULONG y = Top + (ULONG)Row;
        LONG BytesPerRow = (Width + 1) / 2;

        for (LONG Byte = 0; Byte < BytesPerRow; Byte++)
        {
            ULONG x = Left + (ULONG)(Byte * 2);
            UCHAR Data = Scan[Byte];
            UCHAR High = Data >> 4;
            UCHAR Low = Data & 0x0F;

            if (x < ScreenWidth && y < ScreenHeight)
                UefiGopSetPixel(x, y, Palette[High & 0x0F]);

            if ((x + 1) < Left + Width && (x + 1) < ScreenWidth && y < ScreenHeight)
                UefiGopSetPixel(x + 1, y, Palette[Low & 0x0F]);
        }
    }
}

static VOID
UefiBlitRle4(const PUCHAR Source,
             ULONG Left,
             ULONG Top,
             ULONG Width,
             ULONG Height,
             const ULONG *Palette)
{
    ULONG Limit = Left + Width;
    ULONG x = Left;
    LONG y = (LONG)Top + (LONG)Height - 1;
    PUCHAR Ptr = (PUCHAR)Source;

    while (y >= (LONG)Top)
    {
        UCHAR Count = *Ptr++;

        if (Count)
        {
            UCHAR Colors = *Ptr++;
            UCHAR High = Colors >> 4;
            UCHAR Low = Colors & 0x0F;

            for (UCHAR Pixel = 0; Pixel < Count; Pixel++)
            {
                if (x < Limit && (ULONG)y < ScreenHeight)
                {
                    UefiGopSetPixel(x, (ULONG)y, Palette[(Pixel & 1) ? Low : High]);
                }
                x++;
            }

            continue;
        }

        UCHAR Command = *Ptr++;
        switch (Command)
        {
            case 0: /* End of line */
                x = Left;
                y--;
                break;

            case 1: /* End of bitmap */
                return;

            case 2: /* Delta */
            {
                UCHAR Dx = *Ptr++;
                UCHAR Dy = *Ptr++;
                x += Dx;
                y -= Dy;
                break;
            }

            default:
            {
                ULONG Pixels = Command;
                ULONG i;

                for (i = 0; i < Pixels; i++)
                {
                    UCHAR Data = Ptr[i / 2];
                    UCHAR IndexValue = (i & 1) ? (Data & 0x0F) : (Data >> 4);

                    if (x < Limit && (ULONG)y < ScreenHeight)
                        UefiGopSetPixel(x, (ULONG)y, Palette[IndexValue & 0x0F]);

                    x++;
                }

                Ptr += (Pixels + 1) / 2;
                if (((ULONG_PTR)Ptr) & 1)
                    Ptr++;
                break;
            }
        }
    }
}

static ULONG
UefiAdvanceLineHeight(VOID)
{
    return BOOTCHAR_HEIGHT + 1;
}

static VOID
UefiCarriageReturn(VOID)
{
    CurrentX = ScrollLeft;
}

static VOID
UefiLineFeed(VOID)
{
    ULONG Advance = UefiAdvanceLineHeight();

    if (ScrollTop > ScrollBottom)
        return;

    if (CurrentY < ScrollTop)
        CurrentY = ScrollTop;

    CurrentY += Advance;

    if (CurrentY + BOOTCHAR_HEIGHT - 1 > ScrollBottom)
    {
        UefiScroll(Advance);
        if (CurrentY >= Advance)
            CurrentY -= Advance;
        else
            CurrentY = ScrollTop;
    }
}

/* PUBLIC FUNCTIONS **********************************************************/

BOOLEAN
NTAPI
UefiVidInitialize(
    _In_ BOOLEAN ResetMode)
{
    LOADER_PARAMETER_FRAMEBUFFER FbInfo;

    /* Check if already initialized */
    if (DisplayInitialized)
        return TRUE;

    UNREFERENCED_PARAMETER(ResetMode);

    RtlZeroMemory(&FbInfo, sizeof(FbInfo));

    if (!(InbvGetGopFrameBufferInfo(&FbInfo) && FbInfo.FrameBufferSize != 0))
    {
        /* No valid GOP info: decline UEFI path so VGA can take over */
        return FALSE;
    }

    FrameBufferBase = FbInfo.FrameBufferBase;
    FrameBufferSize = FbInfo.FrameBufferSize;
    ScreenWidth = FbInfo.HorizontalResolution;
    ScreenHeight = FbInfo.VerticalResolution;
    PixelsPerScanLine = FbInfo.PixelsPerScanLine ? FbInfo.PixelsPerScanLine : FbInfo.HorizontalResolution;
    PixelFormat = FbInfo.PixelFormat;
    RedMask = FbInfo.RedMask;
    GreenMask = FbInfo.GreenMask;
    BlueMask = FbInfo.BlueMask;

    if (PixelsPerScanLine == 0)
        PixelsPerScanLine = ScreenWidth;

    RedShift = UefiMaskShift(RedMask);
    GreenShift = UefiMaskShift(GreenMask);
    BlueShift = UefiMaskShift(BlueMask);
    RedMax = UefiMaskMax(RedMask >> RedShift);
    GreenMax = UefiMaskMax(GreenMask >> GreenShift);
    BlueMax = UefiMaskMax(BlueMask >> BlueShift);

    /* Map the framebuffer */
    FrameBuffer = (PULONG)MmMapIoSpace(FrameBufferBase, FrameBufferSize, MmNonCached);
    if (!FrameBuffer)
    {
        return FALSE;
    }

    // Initialize scroll region and cursor for common.c
    extern ULONG VidpScrollRegion[4];
    extern ULONG VidpCurrentX;
    extern ULONG VidpCurrentY;
    VidpScrollRegion[0] = 0;
    VidpScrollRegion[1] = 0;
    VidpScrollRegion[2] = ScreenWidth ? ScreenWidth - 1 : 0;
    VidpScrollRegion[3] = ScreenHeight ? ScreenHeight - 1 : 0;
    VidpCurrentX = 0;
    VidpCurrentY = 0;

    BackgroundColorValue = UefiColorFromIndex(BV_COLOR_BLACK);

    /* Clear the screen to the default background */
    UefiGopClearScreen(BackgroundColorValue);

    DisplayInitialized = TRUE;
    return TRUE;
}

VOID
NTAPI
UefiVidCleanUp(VOID)
{
    if (FrameBuffer && DisplayInitialized)
    {
        MmUnmapIoSpace(FrameBuffer, FrameBufferSize);
        FrameBuffer = NULL;
        DisplayInitialized = FALSE;
    }
}

VOID
NTAPI
UefiVidResetDisplay(
    _In_ BOOLEAN HalReset)
{
    /* Clear screen to black */
    if (DisplayInitialized)
        UefiGopClearScreen(BackgroundColorValue);

    UNREFERENCED_PARAMETER(HalReset);
}

VOID
NTAPI
UefiVidScreenToBufferBlt(
    _Out_writes_bytes_(Delta * Height) PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Delta)
{
    ULONG y;

    if (!FrameBuffer || !Buffer || !Width || !Height)
        return;

    for (y = 0; y < Height; y++)
    {
        ULONG ScreenY = Top + y;
        ULONG CopyPixels;
        PULONG Src;

        if (ScreenY >= ScreenHeight)
            break;
        if (Left >= ScreenWidth)
            break;

        CopyPixels = min(Width, ScreenWidth - Left);
        Src = (PULONG)((PUCHAR)FrameBuffer + ScreenY * UefiLinePitch() + Left * sizeof(ULONG));
        RtlCopyMemory(Buffer + y * Delta, Src, CopyPixels * sizeof(ULONG));
        /* NOTE: trailing bytes (if Delta bigger than copied width) are left untouched */
    }
}

VOID
NTAPI
UefiVidBufferToScreenBlt(
    _In_reads_bytes_(Delta * Height) PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Delta)
{
    if (!FrameBuffer || !Buffer || !Width || !Height)
        return;

    for (ULONG y = 0; y < Height; y++)
    {
        ULONG ScreenY = Top + y;
        if (ScreenY >= ScreenHeight) break;
        if (Left >= ScreenWidth) break;

        ULONG CopyPixels = min(Width, ScreenWidth - Left);
        PULONG Src = (PULONG)(Buffer + y * Delta);
        PULONG Dest = (PULONG)((PUCHAR)FrameBuffer + ScreenY * UefiLinePitch() + Left * sizeof(ULONG));
        RtlCopyMemory(Dest, Src, CopyPixels * sizeof(ULONG));
    }
}

VOID
NTAPI
UefiVidDisplayString(
    _In_z_ PUCHAR String)
{
    UCHAR Ch;

    if (!FrameBuffer)
        return;

    while ((Ch = *String++))
    {
        if (Ch == '\r')
        {
            UefiCarriageReturn();
            continue;
        }

        if (Ch == '\n')
        {
            UefiLineFeed();
            UefiCarriageReturn();
            continue;
        }

        if (CurrentX + BOOTCHAR_WIDTH - 1 > ScrollRight)
        {
            UefiLineFeed();
            UefiCarriageReturn();
        }

        UefiDrawGlyph(CurrentX,
                      CurrentY,
                      Ch,
                      TextColorValue,
                      BackgroundColorValue,
                      FALSE);
        CurrentX += BOOTCHAR_WIDTH;
    }
}

ULONG
NTAPI
UefiVidSetTextColor(
    _In_ ULONG Color)
{
    ULONG OldColor = TextColorIndex;
    TextColorIndex = Color;
    TextColorValue = UefiColorFromIndex((UCHAR)Color);
    return OldColor;
}

VOID
NTAPI
UefiVidSolidColorFill(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ UCHAR Color)
{
    ULONG FillColor;

    if (!FrameBuffer || Color >= BV_COLOR_NONE)
        return;

    FillColor = UefiColorFromIndex(Color);
    UefiGopFillRect(Left, Top, Right, Bottom, FillColor);

    /* If the caller just set the full-screen background, update our scroll fill color */
    if (Left == 0 && Top == 0 &&
        Right >= (ScreenWidth ? ScreenWidth - 1 : 0) &&
        Bottom >= (ScreenHeight ? ScreenHeight - 1 : 0))
    {
        BackgroundColorValue = FillColor;
    }
}

VOID
NTAPI
UefiVidSetScrollRegion(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom)
{
    if (!FrameBuffer)
        return;

    if (Left >= ScreenWidth)
        Left = ScreenWidth ? ScreenWidth - 1 : 0;
    if (Top >= ScreenHeight)
        Top = ScreenHeight ? ScreenHeight - 1 : 0;
    if (Right >= ScreenWidth)
        Right = ScreenWidth ? ScreenWidth - 1 : Left;
    if (Bottom >= ScreenHeight)
        Bottom = ScreenHeight ? ScreenHeight - 1 : Top;

    if (Left > Right)
        Right = Left;
    if (Top > Bottom)
        Bottom = Top;

    ScrollLeft = Left;
    ScrollTop = Top;
    ScrollRight = Right;
    ScrollBottom = Bottom;

    CurrentX = ScrollLeft;
    CurrentY = ScrollTop;
}

VOID
NTAPI
UefiVidDisplayStringXY(
    _In_z_ PUCHAR String,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ BOOLEAN Transparent)
{
    ULONG X = Left;
    ULONG Y = Top;
    UCHAR Ch;
    BOOLEAN Opaque = !Transparent;

    if (!FrameBuffer)
        return;

    while ((Ch = *String++))
    {
        if (Ch == '\r')
        {
            X = Left;
            continue;
        }

        if (Ch == '\n')
        {
            X = Left;
            Y += UefiAdvanceLineHeight();
            continue;
        }

        UefiDrawGlyph(X, Y, Ch, TextColorValue, BackgroundColorValue, Opaque);
        X += BOOTCHAR_WIDTH;
    }
}

VOID
NTAPI
UefiVidBitBlt(
    _In_ PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top)
{
    PBITMAPINFOHEADER Header;
    PBMP_RGBQUAD PaletteBase;
    ULONG Palette32[BV_MAX_COLORS] = {0};
    ULONG PaletteCount;
    ULONG Width;
    ULONG Height;
    PUCHAR BitmapBits;
    BOOLEAN TopDown = FALSE;
    LONG Delta;
    ULONG i;

    if (!FrameBuffer)
        return;

    Header = (PBITMAPINFOHEADER)Buffer;
    if (!Header)
        return;

    if (Header->biBitCount != 4 || Header->biWidth <= 0 || Header->biPlanes != 1)
        return;

    Width = (ULONG)Header->biWidth;
    Height = (Header->biHeight < 0) ? (ULONG)(-Header->biHeight) : (ULONG)Header->biHeight;
    if (Header->biHeight < 0)
        TopDown = TRUE;

    if (Height == 0)
        return;

    if (Header->biCompression != BI_RGB && Header->biCompression != BI_RLE4)
    {
        return;
    }

    PaletteCount = Header->biClrUsed ? Header->biClrUsed : BV_MAX_COLORS;
    if (PaletteCount == 0)
        PaletteCount = BV_MAX_COLORS;
    if (PaletteCount > BV_MAX_COLORS)
        PaletteCount = BV_MAX_COLORS;

    PaletteBase = (PBMP_RGBQUAD)(Buffer + Header->biSize);

    /* Detect 'NoPalette' case used by INBV: header palette cleared to zeros. */
    BOOLEAN PaletteAllZero = TRUE;
    for (i = 0; i < PaletteCount; i++)
    {
        if (PaletteBase[i].Red | PaletteBase[i].Green | PaletteBase[i].Blue)
        {
            PaletteAllZero = FALSE;
            break;
        }
    }

    if (PaletteAllZero)
    {
        /* Fall back to BOOTVID default palette mapping */
        for (i = 0; i < min((ULONG)BV_MAX_COLORS, PaletteCount); i++)
        {
            ULONG entry = VidpDefaultPalette[i];
            Palette32[i] = UefiPackColor(GetRValue(entry), GetGValue(entry), GetBValue(entry));
        }
        for (; i < BV_MAX_COLORS; i++)
        {
            Palette32[i] = Palette32[i % BV_MAX_COLORS];
        }
    }
    else
    {
        for (i = 0; i < PaletteCount; i++)
        {
            Palette32[i] = UefiPackColor(PaletteBase[i].Red, PaletteBase[i].Green, PaletteBase[i].Blue);
        }
        for (; i < BV_MAX_COLORS; i++)
        {
            Palette32[i] = Palette32[i % PaletteCount];
        }
    }

    BitmapBits = Buffer + Header->biSize + PaletteCount * sizeof(BMP_RGBQUAD);
    Delta = ((Width + 1) / 2);
    Delta = (Delta + 3) & ~3;

    if (Header->biCompression == BI_RLE4)
    {
        UefiBlitRle4(BitmapBits, Left, Top, Width, Height, Palette32);
    }
    else
    {
        UefiBlit4bpp(BitmapBits, Left, Top, Width, Height, Delta, TopDown, Palette32);
    }
}

/* No arch hooks defined here; VGA provides them. UEFI path calls its own entry points. */
