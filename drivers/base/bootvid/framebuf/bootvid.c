/*
 * PROJECT:     ReactOS Generic Framebuffer Boot Video Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 *              or MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Main file
 * COPYRIGHT:   Copyright 2023-2026 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

/* Include the Boot-time (POST) display discovery helper functions */
#include <drivers/bootvid/framebuf.c>

/* GLOBALS ********************************************************************/

#define TAG_BOOTVID_BACKBUFFER 'bdiV'

#define BB_PIXEL(x, y) \
    (BackBuffer + (ULONG_PTR)(y) * VidpDisplayWidth + (x))

static ULONG_PTR FrameBufferStart = 0;
static ULONG FrameBufferSize;
static ULONG ScreenWidth, ScreenHeight, BytesPerScanLine;
static UCHAR BytesPerPixel;
static PUCHAR BackBuffer = NULL;
static SIZE_T BackBufferSize;

static RGBQUAD CachedPalette[BV_MAX_COLORS];


/* PRIVATE FUNCTIONS *********************************************************/

static __inline ULONG
LogicalToPhysicalX(_In_ ULONG X)
{
    return (ULONG)(((ULONGLONG)X * ScreenWidth) / VidpDisplayWidth);
}

static __inline ULONG
LogicalToPhysicalY(_In_ ULONG Y)
{
    return (ULONG)(((ULONGLONG)Y * ScreenHeight) / VidpDisplayHeight);
}

static __inline PULONG
FramePixel(_In_ ULONG X, _In_ ULONG Y)
{
    return (PULONG)(FrameBufferStart + (ULONG_PTR)Y * BytesPerScanLine + (ULONG_PTR)X * BytesPerPixel);
}

static VOID
FlushBackBufferRect(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    ULONG x, y;
    ULONG NativeLeft, NativeRight;

    if (!Width || !Height || (Left >= VidpDisplayWidth) || (Top >= VidpDisplayHeight))
    {
        return;
    }

    Width = min(Width, VidpDisplayWidth - Left);
    Height = min(Height, VidpDisplayHeight - Top);
    NativeLeft = LogicalToPhysicalX(Left);
    NativeRight = LogicalToPhysicalX(Left + Width);

    for (y = Top; y < Top + Height; ++y)
    {
        PUCHAR Back = BB_PIXEL(Left, y);
        ULONG NativeTop = LogicalToPhysicalY(y);
        ULONG NativeBottom = LogicalToPhysicalY(y + 1);
        ULONG NativeY;

        for (NativeY = NativeTop; NativeY < NativeBottom; ++NativeY)
        {
            PULONG Pixel = FramePixel(NativeLeft, NativeY);
            ULONG NativeX = NativeLeft;

            for (x = 0; x < Width; ++x)
            {
                ULONG NextNativeX = (x + 1 == Width) ? NativeRight : LogicalToPhysicalX(Left + x + 1);

                while (NativeX < NextNativeX)
                {
                    *Pixel++ = CachedPalette[Back[x]];
                    ++NativeX;
                }
            }
        }
    }
}

static VOID
ApplyPalette(VOID)
{
    /* Screen redraw */
    FlushBackBufferRect(0, 0, VidpDisplayWidth, VidpDisplayHeight);
}

/* PUBLIC FUNCTIONS **********************************************************/

BOOLEAN
NTAPI
VidInitialize(
    _In_ BOOLEAN SetMode)
{
    PHYSICAL_ADDRESS FrameBuffer;
    PHYSICAL_ADDRESS VramAddress;
    ULONG VramSize;
    CM_FRAMEBUF_DEVICE_DATA VideoConfigData; /* Configuration data from hardware tree */
    INTERFACE_TYPE Interface;
    ULONG BusNumber;
    ULONG Dpi, MaximumDpi;
    ULONG LogicalWidth, LogicalHeight;
    SIZE_T BackBufferHeight;
    NTSTATUS Status;

    /* Find boot-time framebuffer display information from the LoaderBlock */
    Status = FindBootDisplay(&VramAddress,
                             &VramSize,
                             &VideoConfigData,
                             NULL, // MonitorConfigData
                             &Interface,
                             &BusNumber);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Boot framebuffer does not exist!\n");
        return FALSE;
    }

    /* The VRAM address must be page-aligned */
    if (VramAddress.QuadPart % PAGE_SIZE != 0) // DPRINTed for diagnostics on some systems
        DPRINT1("** VramAddress 0x%I64X isn't PAGE_SIZE aligned\n", VramAddress.QuadPart);
    ASSERT(VramAddress.QuadPart % PAGE_SIZE == 0);
    if (VramSize % PAGE_SIZE != 0)
        DPRINT1("** VramSize %lu (0x%lx) isn't multiple of PAGE_SIZE\n", VramSize, VramSize);
    // ASSERT(VramSize % PAGE_SIZE == 0); // This assert may fail, e.g. 800x600@32bpp UEFI GOP display

    /* Retrieve the framebuffer address, its visible screen dimensions, and its attributes */
    FrameBuffer.QuadPart = VramAddress.QuadPart + VideoConfigData.FrameBufferOffset;
    ScreenWidth  = VideoConfigData.ScreenWidth;
    ScreenHeight = VideoConfigData.ScreenHeight;
    if (ScreenWidth < SCREEN_WIDTH || ScreenHeight < SCREEN_HEIGHT)
    {
        DPRINT1("Unsupported screen resolution!\n");
        return FALSE;
    }

    BytesPerPixel = (VideoConfigData.BitsPerPixel + 7) / 8; // Round up to nearest byte.
    ASSERT(BytesPerPixel >= 1 && BytesPerPixel <= 4);
    if (BytesPerPixel != 4)
    {
        UNIMPLEMENTED;
        DPRINT1("Unsupported BytesPerPixel = %u\n", BytesPerPixel);
        return FALSE;
    }

    ASSERT(ScreenWidth <= VideoConfigData.PixelsPerScanLine);
    if ((VideoConfigData.PixelsPerScanLine < ScreenWidth) || (VideoConfigData.PixelsPerScanLine > MAXULONG / BytesPerPixel))
    {
        DPRINT1("Invalid PixelsPerScanLine = %lu\n", VideoConfigData.PixelsPerScanLine);
        return FALSE;
    }

    BytesPerScanLine = VideoConfigData.PixelsPerScanLine * BytesPerPixel;
    if ((BytesPerScanLine < 1) || (ScreenHeight > MAXULONG / BytesPerScanLine))
    {
        DPRINT1("Invalid framebuffer stride or height\n");
        return FALSE;
    }

    /* Compute the visible framebuffer size */
    FrameBufferSize = ScreenHeight * BytesPerScanLine;

    /* Verify that the framebuffer actually fits inside the video RAM */
    if ((VideoConfigData.FrameBufferOffset > VramSize) || (FrameBufferSize > VramSize - VideoConfigData.FrameBufferOffset))
    {
        DPRINT1("The framebuffer exceeds video memory bounds!\n");
        return FALSE;
    }

    /*
     * Expose a 96-DPI logical coordinate space. The configured DPI controls
     * the physical size of text and legacy boot graphics, while the native
     * resolution controls how many logical pixels, columns and rows fit.
     *
     * Keep at least the historical 640x480 canvas so old boot resources and
     * their fixed coordinates remain valid even at very high configured DPI.
     */
    Dpi = VideoConfigData.Dpi;
    if ((Dpi < LOADER_PARAMETER_FRAMEBUFFER_DPI_MIN) || (Dpi > LOADER_PARAMETER_FRAMEBUFFER_DPI_MAX))
    {
        Dpi = LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT;
    }

    MaximumDpi = min((ULONG)(((ULONGLONG)ScreenWidth * LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT) / SCREEN_WIDTH), (ULONG)(((ULONGLONG)ScreenHeight * LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT) / SCREEN_HEIGHT));
    Dpi = min(Dpi, MaximumDpi);

    LogicalWidth = (ULONG)(((ULONGLONG)ScreenWidth * LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT + Dpi / 2) / Dpi);
    LogicalHeight = (ULONG)(((ULONGLONG)ScreenHeight * LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT + Dpi / 2) / Dpi);
    LogicalWidth = max(LogicalWidth, SCREEN_WIDTH);
    LogicalHeight = max(LogicalHeight, SCREEN_HEIGHT);

    /* A full-width scroll region must end on an 8-pixel character boundary. */
    LogicalWidth = ALIGN_DOWN_BY(LogicalWidth, BOOTCHAR_WIDTH);

    VidpDisplayWidth = LogicalWidth;
    VidpDisplayHeight = LogicalHeight;
    VidpPhysicalWidth = ScreenWidth;
    VidpPhysicalHeight = ScreenHeight;
    VidpDisplayDpi = Dpi;
    VidpScrollRegion.Left = 0;
    VidpScrollRegion.Top = 0;
    VidpScrollRegion.Right = LogicalWidth - 1;
    VidpScrollRegion.Bottom = LogicalHeight - 1;
    VidpCurrentX = 0;
    VidpCurrentY = 0;

    DPRINT1("Display: native %lux%lu, logical %lux%lu at %lu DPI\n", ScreenWidth, ScreenHeight, VidpDisplayWidth, VidpDisplayHeight, VidpDisplayDpi);

    /* Translate the framebuffer from bus-relative to physical address */
    PHYSICAL_ADDRESS TranslatedAddress;
    ULONG AddressSpace = 0; /* MMIO space */
    if (!BootTranslateBusAddress(Interface,
                                 BusNumber,
                                 FrameBuffer,
                                 &AddressSpace,
                                 &TranslatedAddress))
    {
        DPRINT1("Could not translate framebuffer bus address 0x%I64X\n", FrameBuffer.QuadPart);
        return FALSE;
    }

    /* Map it into system space if necessary */
    ULONG MappedSize = 0;
    PVOID FrameBufferBase = NULL;
    if (AddressSpace == 0)
    {
        /* Calculate page-aligned address and size for MmMapIoSpace() */
        FrameBuffer.HighPart = TranslatedAddress.HighPart;
        FrameBuffer.LowPart  = ALIGN_DOWN_BY(TranslatedAddress.LowPart, PAGE_SIZE);
        MappedSize = FrameBufferSize;
        MappedSize += (ULONG)(TranslatedAddress.QuadPart - FrameBuffer.QuadPart); // BYTE_OFFSET()
        MappedSize = ROUND_TO_PAGES(MappedSize);
        /* Essentially MmMapVideoDisplay() */
        FrameBufferBase = MmMapIoSpace(FrameBuffer, MappedSize, MmFrameBufferCached);
        if (!FrameBufferBase)
            FrameBufferBase = MmMapIoSpace(FrameBuffer, MappedSize, MmNonCached);
        if (!FrameBufferBase)
        {
            DPRINT1("Could not map framebuffer 0x%I64X (%lu bytes)\n",
                    FrameBuffer.QuadPart, MappedSize);
            goto Failure;
        }
        FrameBufferStart = (ULONG_PTR)FrameBufferBase;
        FrameBufferStart += (TranslatedAddress.QuadPart - FrameBuffer.QuadPart); // BYTE_OFFSET()
    }
    else
    {
        /* The base is the translated address, no need to map */
        FrameBufferStart = (ULONG_PTR)TranslatedAddress.QuadPart;
    }


    /*
     * Reserve off-screen area for the backbuffer that contains
     * 8-bit indexed color screen image, plus preserved row data.
     */
    BackBufferHeight = (SIZE_T)VidpDisplayHeight + (BOOTCHAR_HEIGHT + 1);
    if ((BackBufferHeight < VidpDisplayHeight) || ((SIZE_T)VidpDisplayWidth > MAXULONG_PTR / BackBufferHeight))
    {
        DPRINT1("Logical framebuffer dimensions are too large\n");
        goto Failure;
    }
    BackBufferSize = (SIZE_T)VidpDisplayWidth * BackBufferHeight;

    /*
     * Keep the backbuffer in cached system RAM. Reading from the GOP
     * framebuffer is often extremely slow, even when writes are combined.
     * It does not need to be physically contiguous.
     */
    BackBuffer = ExAllocatePoolWithTag(NonPagedPool, BackBufferSize, TAG_BOOTVID_BACKBUFFER);

    if (!BackBuffer && (BackBufferSize <= MAXULONG) && (VideoConfigData.FrameBufferOffset <= VramSize) && (FrameBufferSize <= VramSize - VideoConfigData.FrameBufferOffset) && ((ULONG)BackBufferSize <= VramSize - VideoConfigData.FrameBufferOffset - FrameBufferSize) && ((AddressSpace != 0) || (FrameBufferSize + BackBufferSize <= MappedSize)))
    {
        /* Backbuffer placed following the framebuffer in the hidden part */
        BackBuffer = (PUCHAR)(FrameBufferStart + FrameBufferSize);
        // BackBuffer = (PUCHAR)(VramAddress + VramSize - BackBufferSize); // Or at the end of VRAM.
    }

    if (!BackBuffer)
    {
        DPRINT1("Could not allocate backbuffer (size: %lu)\n", (ULONG)BackBufferSize);
        goto Failure;
    }

    RtlZeroMemory(BackBuffer, BackBufferSize);

    /* Reset the video mode if requested */
    if (SetMode)
        VidResetDisplay(TRUE);

    return TRUE;

Failure:
    /* We failed somewhere; unmap the framebuffer if we mapped it */
    if (FrameBufferBase && (AddressSpace == 0))
        MmUnmapIoSpace(FrameBufferBase, MappedSize);

    return FALSE;
}

VOID
NTAPI
VidCleanUp(VOID)
{
    /* Just fill the screen black */
    VidSolidColorFill(0, 0, VidpDisplayWidth - 1, VidpDisplayHeight - 1, BV_COLOR_BLACK);
}

VOID
ResetDisplay(
    _In_ BOOLEAN SetMode)
{
    RtlZeroMemory(BackBuffer, BackBufferSize);
    RtlZeroMemory((PVOID)FrameBufferStart, FrameBufferSize);

    /* Re-initialize the palette and fill the screen black */
    InitializePalette();
    VidSolidColorFill(0, 0, VidpDisplayWidth - 1, VidpDisplayHeight - 1, BV_COLOR_BLACK);
}

VOID
InitPaletteWithTable(
    _In_reads_(Count) const ULONG* Table,
    _In_ ULONG Count)
{
    const ULONG* Entry = Table;
    ULONG i;
    BOOLEAN HasChanged = FALSE;

    for (i = 0; i < Count; i++, Entry++)
    {
        HasChanged |= !!((CachedPalette[i] ^ *Entry) & 0x00FFFFFF);
        CachedPalette[i] = *Entry | 0xFF000000;
    }

    /* Re-apply the palette if it has changed */
    if (HasChanged)
        ApplyPalette();
}

VOID
SetPixel(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ UCHAR Color)
{
    if ((Left >= VidpDisplayWidth) || (Top >= VidpDisplayHeight) || (Color >= BV_MAX_COLORS))
    {
        return;
    }

    *BB_PIXEL(Left, Top) = Color;
    FlushBackBufferRect(Left, Top, 1, 1);
}

BOOLEAN
VidBufferToScreenBltNative(
    _In_reads_bytes_(Delta * Height) PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Delta)
{
    ULONG y;

    if (!FrameBufferStart || BytesPerPixel != sizeof(ULONG))
        return FALSE;

    /*
     * Legacy bootvid callers pass 4bpp packed scanlines. Only intercept
     * native 32bpp rows used by the GOP boot animation path.
     */
    if (Delta < Width * sizeof(ULONG))
        return FALSE;

    if (Left >= ScreenWidth || Top >= ScreenHeight)
        return TRUE;

    if (Width > ScreenWidth - Left)
        Width = ScreenWidth - Left;
    if (Height > ScreenHeight - Top)
        Height = ScreenHeight - Top;

    for (y = 0; y < Height; ++y)
    {
        PUCHAR Src = Buffer + y * Delta;
        PUCHAR Dst = (PUCHAR)FrameBufferStart + (ULONG_PTR)(Top + y) * BytesPerScanLine + (ULONG_PTR)Left * BytesPerPixel;

        RtlCopyMemory(Dst, Src, Width * sizeof(ULONG));
    }

    return TRUE;
}

VOID
PreserveRow(
    _In_ ULONG CurrentTop,
    _In_ ULONG Height,
    _In_ BOOLEAN Restore)
{
    PUCHAR NewPosition, OldPosition;
    SIZE_T Count;

    if ((CurrentTop >= VidpDisplayHeight) || !Height)
        return;

    Height = min(Height, VidpDisplayHeight - CurrentTop);
    Height = min(Height, (ULONG)(BOOTCHAR_HEIGHT + 1));

    /* Calculate the position in memory for the row */
    if (Restore)
    {
        /* Restore the row by copying back the contents saved off-screen */
        NewPosition = BB_PIXEL(0, CurrentTop);
        OldPosition = BB_PIXEL(0, VidpDisplayHeight);
    }
    else
    {
        /* Preserve the row by saving its contents off-screen */
        NewPosition = BB_PIXEL(0, VidpDisplayHeight);
        OldPosition = BB_PIXEL(0, CurrentTop);
    }

    /* Set the count and copy the pixel data back to the other position in the backbuffer */
    Count = (SIZE_T)Height * VidpDisplayWidth;
    RtlCopyMemory(NewPosition, OldPosition, Count);

    /* On restore, mirror the backbuffer changes to the framebuffer */
    if (Restore)
    {
        FlushBackBufferRect(0, CurrentTop, VidpDisplayWidth, Height);
    }
}

VOID
DoScroll(
    _In_ ULONG Scroll)
{
    ULONG RowSize = VidpScrollRegion.Right - VidpScrollRegion.Left + 1;
    ULONG Height = VidpScrollRegion.Bottom - VidpScrollRegion.Top + 1;
    ULONG RowsToMove;
    PUCHAR OldPosition, NewPosition;

    if (!Scroll || Scroll >= Height || (VidpScrollRegion.Right >= VidpDisplayWidth) || (VidpScrollRegion.Bottom >= VidpDisplayHeight))
    {
        return;
    }

    /* Calculate the position in memory for the row */
    OldPosition = BB_PIXEL(VidpScrollRegion.Left, VidpScrollRegion.Top + Scroll);
    NewPosition = BB_PIXEL(VidpScrollRegion.Left, VidpScrollRegion.Top);
    RowsToMove = Height - Scroll;

    /* Start loop */
    while (RowsToMove--)
    {
        /* Scroll the row */
        RtlCopyMemory(NewPosition, OldPosition, RowSize);

        OldPosition += VidpDisplayWidth;
        NewPosition += VidpDisplayWidth;
    }

    FlushBackBufferRect(VidpScrollRegion.Left,
                        VidpScrollRegion.Top,
                        RowSize,
                        Height);
}

VOID
DisplayCharacter(
    _In_ CHAR Character,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG TextColor,
    _In_ ULONG BackColor)
{
    /* Get the font line for this character */
    const UCHAR* FontChar = GetFontPtr(Character);
    const BOOLEAN Opaque = (BackColor < BV_COLOR_NONE);
    ULONG Width, Height, y;

    if ((Left >= VidpDisplayWidth) || (Top >= VidpDisplayHeight))
        return;

    Width = min((ULONG)BOOTCHAR_WIDTH, VidpDisplayWidth - Left);
    Height = min((ULONG)BOOTCHAR_HEIGHT, VidpDisplayHeight - Top);

    /* Loop each pixel height */
    for (y = 0; y < Height; ++y, FontChar += FONT_PTR_DELTA)
    {
        PUCHAR Back = BB_PIXEL(Left, Top + y);
        UCHAR bit = 1 << (BOOTCHAR_WIDTH - 1);
        ULONG x;

        for (x = 0; x < Width; ++x, bit >>= 1)
        {
            if (*FontChar & bit)
                Back[x] = (UCHAR)TextColor;
            else if (Opaque)
                Back[x] = (UCHAR)BackColor;
        }
    }

    FlushBackBufferRect(Left, Top, Width, Height);
}

VOID
NTAPI
VidSolidColorFill(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ UCHAR Color)
{
    ULONG Width, NativeLeft, NativeRight, NativeTop, NativeBottom;
    ULONG NativeWidth;
    ULONG NativeColor;
    ULONG y;

    if ((Left > Right) || (Top > Bottom) || (Left >= VidpDisplayWidth) || (Top >= VidpDisplayHeight) || (Color >= BV_MAX_COLORS))
    {
        return;
    }

    NativeColor = CachedPalette[Color];
    Right = min(Right, VidpDisplayWidth - 1);
    Bottom = min(Bottom, VidpDisplayHeight - 1);
    Width = Right - Left + 1;

    for (y = Top; y <= Bottom; ++y)
    {
        PUCHAR Back = BB_PIXEL(Left, y);

        RtlFillMemory(Back, Width, Color);
    }

    NativeLeft = LogicalToPhysicalX(Left);
    NativeRight = LogicalToPhysicalX(Right + 1);
    NativeTop = LogicalToPhysicalY(Top);
    NativeBottom = LogicalToPhysicalY(Bottom + 1);
    NativeWidth = (NativeRight - NativeLeft) * BytesPerPixel;

    for (y = NativeTop; y < NativeBottom; ++y)
    {
        RtlFillMemoryUlong(FramePixel(NativeLeft, y), NativeWidth, NativeColor);
    }
}

VOID
NTAPI
VidScreenToBufferBlt(
    _Out_writes_bytes_all_(Height * Stride) PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Stride)
{
    ULONG x, y, CopyWidth, CopyHeight;

    /* Clear the destination buffer */
    RtlZeroMemory(Buffer, Height * Stride);

    if ((Left >= VidpDisplayWidth) || (Top >= VidpDisplayHeight))
        return;

    CopyWidth = min(Width, VidpDisplayWidth - Left);
    CopyHeight = min(Height, VidpDisplayHeight - Top);

    /* Start the outer Y height loop */
    for (y = 0; y < CopyHeight; ++y)
    {
        /* Set current scanline */
        PUCHAR Back = BB_PIXEL(Left, Top + y);
        PUCHAR Buf = Buffer + y * Stride;

        /* Start the X inner loop */
        for (x = 0; (x < CopyWidth) && (x / 2 < Stride); x += 2)
        {
            /* Read the current value */
            *Buf = (*Back++ & 0xF) << 4;
            if (x + 1 < CopyWidth)
                *Buf |= *Back++ & 0xF;
            Buf++;
        }
    }
}

/* EOF */
