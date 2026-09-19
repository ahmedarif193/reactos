/*
 * PROJECT:     ReactOS Generic Framebuffer Boot Video Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 *              or MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Main file
 * COPYRIGHT:   Copyright 2023-2026 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

#include "precomp.h"
#include <bootfont/bootfont.h>

#define NDEBUG
#include <debug.h>

/* Include the Boot-time (POST) display discovery helper functions */
#include <drivers/bootvid/framebuf.c>

/* GLOBALS ********************************************************************/

#define TAG_BOOTVID_BACKBUFFER 'bdiV'

#define BOOTVID_FALLBACK_FONT_SCALE 2
#define BOOTVID_FALLBACK_CELL_WIDTH (BOOTCHAR_WIDTH * BOOTVID_FALLBACK_FONT_SCALE)
#define BOOTVID_FALLBACK_CELL_HEIGHT ((BOOTCHAR_HEIGHT + 1) * BOOTVID_FALLBACK_FONT_SCALE)

#define BB_PIXEL(x, y) \
    (BackBuffer + (ULONG_PTR)(y) * VidpDisplayWidth + (x))

static ULONG_PTR FrameBufferStart = 0;
static ULONG_PTR PhysicalFrameBufferStart = 0;
static ULONG FrameBufferSize;
static ULONG ScreenWidth, ScreenHeight, BytesPerScanLine;

#define BV_FADE_MAX_STEPS     32
#define BV_FADE_STEP_DELAY_US 8000
#define TAG_BOOTVID_FADE      'fdiV'
static ULONG FrameBufferWidth, FrameBufferHeight;
static ULONG FrameBufferRotation;
static UCHAR BytesPerPixel;
static PUCHAR BackBuffer = NULL;
static SIZE_T BackBufferSize;

static RGBQUAD CachedPalette[BV_MAX_COLORS];
static UCHAR CachedBlendPalette[BV_MAX_COLORS][BV_MAX_COLORS][MAXUCHAR + 1];
static BOOT_FONT_RENDERER BootVidFont;

NTSTATUS
NTAPI
VidSetVirtualFrameBuffer(
    _In_opt_ PVOID VirtualFrameBuffer)
{
    InterlockedExchangePointer((PVOID volatile *)&FrameBufferStart,
                               VirtualFrameBuffer != NULL ?
                                   VirtualFrameBuffer :
                                   (PVOID)PhysicalFrameBufferStart);
    return STATUS_SUCCESS;
}


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
    ULONG PhysicalX, PhysicalY;

    switch (FrameBufferRotation)
    {
        case LoaderFramebufferRotation90:
            PhysicalX = FrameBufferWidth - 1 - Y;
            PhysicalY = X;
            break;

        case LoaderFramebufferRotation180:
            PhysicalX = FrameBufferWidth - 1 - X;
            PhysicalY = FrameBufferHeight - 1 - Y;
            break;

        case LoaderFramebufferRotation270:
            PhysicalX = Y;
            PhysicalY = FrameBufferHeight - 1 - X;
            break;

        case LoaderFramebufferRotationIdentity:
        default:
            PhysicalX = X;
            PhysicalY = Y;
            break;
    }

    return (PULONG)(FrameBufferStart +
                    (ULONG_PTR)PhysicalY * BytesPerScanLine +
                    (ULONG_PTR)PhysicalX * BytesPerPixel);
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

    if (FrameBufferRotation == LoaderFramebufferRotationIdentity)
    {
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
        return;
    }

    for (y = 0; y < Height; ++y)
    {
        PUCHAR Back = BB_PIXEL(Left, Top + y);
        ULONG NativeTop = LogicalToPhysicalY(Top + y);
        ULONG NativeBottom = LogicalToPhysicalY(Top + y + 1);

        for (x = 0; x < Width; ++x)
        {
            ULONG NativeX;
            ULONG NativeY;
            ULONG Pixel = CachedPalette[Back[x]];
            ULONG PixelLeft = LogicalToPhysicalX(Left + x);
            ULONG PixelRight = LogicalToPhysicalX(Left + x + 1);

            for (NativeY = NativeTop; NativeY < NativeBottom; ++NativeY)
            {
                for (NativeX = PixelLeft; NativeX < PixelRight; ++NativeX)
                    *FramePixel(NativeX, NativeY) = Pixel;
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

static UCHAR
FindClosestBlendedPaletteColor(
    _In_ UCHAR Background,
    _In_ UCHAR Foreground,
    _In_ UCHAR Alpha)
{
    ULONG BackgroundColor;
    ULONG ForegroundColor;
    LONG Red;
    LONG Green;
    LONG Blue;
    ULONG BestDistance = MAXULONG;
    UCHAR BestIndex = Background;
    UCHAR Index;

    if ((Alpha == 0) || (Foreground >= BV_MAX_COLORS))
        return Background;

    if ((Alpha == 0xFF) || (Background >= BV_MAX_COLORS))
        return Foreground;

    BackgroundColor = CachedPalette[Background];
    ForegroundColor = CachedPalette[Foreground];
    Red = (GetRValue(BackgroundColor) * (255 - Alpha) +
           GetRValue(ForegroundColor) * Alpha + 127) / 255;
    Green = (GetGValue(BackgroundColor) * (255 - Alpha) +
             GetGValue(ForegroundColor) * Alpha + 127) / 255;
    Blue = (GetBValue(BackgroundColor) * (255 - Alpha) +
            GetBValue(ForegroundColor) * Alpha + 127) / 255;

    for (Index = 0; Index < BV_MAX_COLORS; ++Index)
    {
        LONG DeltaRed = Red - GetRValue(CachedPalette[Index]);
        LONG DeltaGreen = Green - GetGValue(CachedPalette[Index]);
        LONG DeltaBlue = Blue - GetBValue(CachedPalette[Index]);
        ULONG Distance = DeltaRed * DeltaRed +
                         DeltaGreen * DeltaGreen +
                         DeltaBlue * DeltaBlue;

        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            BestIndex = Index;
        }
    }

    return BestIndex;
}

static VOID
CachePaletteBlends(VOID)
{
    ULONG Background;
    ULONG Foreground;
    ULONG Alpha;

    for (Background = 0; Background < BV_MAX_COLORS; ++Background)
    {
        for (Foreground = 0; Foreground < BV_MAX_COLORS; ++Foreground)
        {
            for (Alpha = 0; Alpha <= MAXUCHAR; ++Alpha)
            {
                CachedBlendPalette[Background][Foreground][Alpha] =
                    FindClosestBlendedPaletteColor((UCHAR)Background,
                                                   (UCHAR)Foreground,
                                                   (UCHAR)Alpha);
            }
        }
    }
}

static VOID
DisplayBitmapCharacter(
    _In_ CHAR Character,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG TextColor,
    _In_ ULONG BackColor)
{
    const UCHAR* FontChar = GetFontPtr((UCHAR)Character);
    const BOOLEAN Opaque = (BackColor < BV_COLOR_NONE);
    ULONG Width, Height, y;

    Width = min(VidpCharacterWidth, VidpDisplayWidth - Left);
    Height = min(VidpCharacterHeight, VidpDisplayHeight - Top);

    for (y = 0; y < Height; ++y)
    {
        ULONG SourceY = (y * (BOOTCHAR_HEIGHT + 1)) / VidpCharacterHeight;
        PUCHAR Back = BB_PIXEL(Left, Top + y);
        ULONG x;

        for (x = 0; x < Width; ++x)
        {
            ULONG SourceX = (x * BOOTCHAR_WIDTH) / VidpCharacterWidth;
            UCHAR Bit = 1 << (BOOTCHAR_WIDTH - 1 - SourceX);

            if ((SourceY < BOOTCHAR_HEIGHT) &&
                (FontChar[(LONG)SourceY * FONT_PTR_DELTA] & Bit))
            {
                Back[x] = (UCHAR)TextColor;
            }
            else if (Opaque)
            {
                Back[x] = (UCHAR)BackColor;
            }
        }
    }

    FlushBackBufferRect(Left, Top, Width, Height);
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
    ULONG Dpi, ReadableDpi, MaximumDpi;
    ULONG LogicalWidth, LogicalHeight, ScrollWidth;
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
    FrameBufferWidth = VideoConfigData.ScreenWidth;
    FrameBufferHeight = VideoConfigData.ScreenHeight;
    FrameBufferRotation = VideoConfigData.Rotation;

    if (((FrameBufferRotation == LoaderFramebufferRotation90) ||
         (FrameBufferRotation == LoaderFramebufferRotation270)) &&
        (VideoConfigData.LogicalWidth == FrameBufferHeight) &&
        (VideoConfigData.LogicalHeight == FrameBufferWidth))
    {
        ScreenWidth = VideoConfigData.LogicalWidth;
        ScreenHeight = VideoConfigData.LogicalHeight;
    }
    else if (((FrameBufferRotation == LoaderFramebufferRotationIdentity) ||
              (FrameBufferRotation == LoaderFramebufferRotation180)) &&
             (VideoConfigData.LogicalWidth == FrameBufferWidth) &&
             (VideoConfigData.LogicalHeight == FrameBufferHeight))
    {
        ScreenWidth = VideoConfigData.LogicalWidth;
        ScreenHeight = VideoConfigData.LogicalHeight;
    }
    else
    {
        FrameBufferRotation = LoaderFramebufferRotationIdentity;
        ScreenWidth = FrameBufferWidth;
        ScreenHeight = FrameBufferHeight;
    }

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

    ASSERT(FrameBufferWidth <= VideoConfigData.PixelsPerScanLine);
    if ((VideoConfigData.PixelsPerScanLine < FrameBufferWidth) || (VideoConfigData.PixelsPerScanLine > MAXULONG / BytesPerPixel))
    {
        DPRINT1("Invalid PixelsPerScanLine = %lu\n", VideoConfigData.PixelsPerScanLine);
        return FALSE;
    }

    BytesPerScanLine = VideoConfigData.PixelsPerScanLine * BytesPerPixel;
    if ((BytesPerScanLine < 1) || (FrameBufferHeight > MAXULONG / BytesPerScanLine))
    {
        DPRINT1("Invalid framebuffer stride or height\n");
        return FALSE;
    }

    /* Compute the visible framebuffer size */
    FrameBufferSize = FrameBufferHeight * BytesPerScanLine;

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

    /*
     * The UEFI console chooses its font to retain about 38 visible rows.
     * Preserve that minimum physical text size after the kernel takes over;
     * the configured system DPI may still request a larger scale.
     */
    ReadableDpi = (ULONG)(((ULONGLONG)ScreenHeight *
                           LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT +
                           BOOT_FONT_TARGET_ROWS *
                           BOOTVID_FALLBACK_CELL_HEIGHT - 1) /
                          (BOOT_FONT_TARGET_ROWS *
                           BOOTVID_FALLBACK_CELL_HEIGHT));
    ReadableDpi = min(max(ReadableDpi,
                          (ULONG)LOADER_PARAMETER_FRAMEBUFFER_DPI_MIN),
                      (ULONG)LOADER_PARAMETER_FRAMEBUFFER_DPI_MAX);
    Dpi = max(Dpi, ReadableDpi);

    MaximumDpi = min((ULONG)(((ULONGLONG)ScreenWidth * LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT) / SCREEN_WIDTH), (ULONG)(((ULONGLONG)ScreenHeight * LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT) / SCREEN_HEIGHT));
    Dpi = min(Dpi, MaximumDpi);

    LogicalWidth = (ULONG)(((ULONGLONG)ScreenWidth * LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT + Dpi / 2) / Dpi);
    LogicalHeight = (ULONG)(((ULONGLONG)ScreenHeight * LOADER_PARAMETER_FRAMEBUFFER_DPI_DEFAULT + Dpi / 2) / Dpi);
    LogicalWidth = max(LogicalWidth, SCREEN_WIDTH);
    LogicalHeight = max(LogicalHeight, SCREEN_HEIGHT);

    if (BootFontInitialize(&BootVidFont, LogicalWidth, LogicalHeight))
    {
        VidpCharacterWidth = BootVidFont.CellWidth;
        VidpCharacterHeight = BootVidFont.CellHeight;
    }
    else
    {
        DPRINT1("Could not initialize the FreeType boot console font\n");
        VidpCharacterWidth = BOOTVID_FALLBACK_CELL_WIDTH;
        VidpCharacterHeight = BOOTVID_FALLBACK_CELL_HEIGHT;
    }

    /*
     * Text scrolling must end on a character boundary, but that is a
     * property of the console region rather than of the display mode.
     * Keep the loader-provided logical width intact so drawing, coordinate
     * conversion, and the framebuffer handoff continue to describe the
     * complete display.  Only leave the fractional cell at the right edge
     * outside the scrolling rectangle.
     */
    ScrollWidth = LogicalWidth - (LogicalWidth % VidpCharacterWidth);

    VidpDisplayWidth = LogicalWidth;
    VidpDisplayHeight = LogicalHeight;
    VidpPhysicalWidth = ScreenWidth;
    VidpPhysicalHeight = ScreenHeight;
    VidpDisplayDpi = Dpi;
    VidpScrollRegion.Left = 0;
    VidpScrollRegion.Top = 0;
    VidpScrollRegion.Right = ScrollWidth - 1;
    VidpScrollRegion.Bottom = LogicalHeight - 1;
    VidpCurrentX = 0;
    VidpCurrentY = 0;

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
        FrameBufferBase = MmMapIoSpace(FrameBuffer, MappedSize, MmWriteCombined);
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
    PhysicalFrameBufferStart = FrameBufferStart;

    /* Publish the active scanout for the later WDDM ownership transfer. */
    RtlZeroMemory(&VidpFrameBufferInfo, sizeof(VidpFrameBufferInfo));
    VidpFrameBufferInfo.FrameBufferBase = TranslatedAddress;
    VidpFrameBufferInfo.FrameBufferSize = FrameBufferSize;
    VidpFrameBufferInfo.HorizontalResolution = FrameBufferWidth;
    VidpFrameBufferInfo.VerticalResolution = FrameBufferHeight;
    VidpFrameBufferInfo.PixelsPerScanLine = VideoConfigData.PixelsPerScanLine;
    VidpFrameBufferInfo.PixelFormat = VideoConfigData.BitsPerPixel;
    VidpFrameBufferInfo.RedMask = VideoConfigData.PixelMasks.RedMask;
    VidpFrameBufferInfo.GreenMask = VideoConfigData.PixelMasks.GreenMask;
    VidpFrameBufferInfo.BlueMask = VideoConfigData.PixelMasks.BlueMask;
    VidpFrameBufferInfo.Reserved = VideoConfigData.PixelMasks.ReservedMask;
    VidpFrameBufferInfo.Dpi = Dpi;


    /*
     * Reserve off-screen area for the backbuffer that contains
     * 8-bit indexed color screen image, plus preserved row data.
     */
    BackBufferHeight = (SIZE_T)VidpDisplayHeight + VidpCharacterHeight;
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
    RtlZeroMemory(&VidpFrameBufferInfo, sizeof(VidpFrameBufferInfo));
    BootFontCleanup(&BootVidFont);

    /* We failed somewhere; unmap the framebuffer if we mapped it */
    if (FrameBufferBase && (AddressSpace == 0))
        MmUnmapIoSpace(FrameBufferBase, MappedSize);

    return FALSE;
}

VOID
NTAPI
VidFadeToBlack(
    _In_ ULONG Steps)
{
    PULONG Snapshot;
    SIZE_T Pixels;
    ULONG Step, x, y;
    UCHAR Lut[256];

    if (!FrameBufferStart || BytesPerPixel != sizeof(ULONG))
        return;
    if (ScreenWidth == 0 || ScreenHeight == 0)
        return;

    if (Steps < 2)
        Steps = 2;
    else if (Steps > BV_FADE_MAX_STEPS)
        Steps = BV_FADE_MAX_STEPS;

    Pixels = (SIZE_T)ScreenWidth * ScreenHeight;
    if (Pixels > MAXULONG_PTR / sizeof(ULONG))
        return;

    /*
     * Reading the framebuffer back is far too slow to do once per step, so
     * take one copy into cached memory and scale every step out of that.
     * Without the copy there is nothing to fade from, so just leave the
     * screen to the caller.
     */
    Snapshot = ExAllocatePoolWithTag(NonPagedPool, Pixels * sizeof(ULONG), TAG_BOOTVID_FADE);
    if (!Snapshot)
        return;

    if (FrameBufferRotation == LoaderFramebufferRotationIdentity)
    {
        for (y = 0; y < ScreenHeight; ++y)
        {
            RtlCopyMemory(Snapshot + (SIZE_T)y * ScreenWidth,
                          FramePixel(0, y),
                          (SIZE_T)ScreenWidth * sizeof(ULONG));
        }
    }
    else
    {
        for (y = 0; y < ScreenHeight; ++y)
            for (x = 0; x < ScreenWidth; ++x)
                Snapshot[(SIZE_T)y * ScreenWidth + x] = *FramePixel(x, y);
    }

    for (Step = 1; Step < Steps; ++Step)
    {
        ULONG Level = Steps - Step;
        ULONG i;

        for (i = 0; i < 256; ++i)
            Lut[i] = (UCHAR)(i * Level / Steps);

        for (y = 0; y < ScreenHeight; ++y)
        {
            const ULONG *Src = Snapshot + (SIZE_T)y * ScreenWidth;

            if (FrameBufferRotation == LoaderFramebufferRotationIdentity)
            {
                PULONG Dst = FramePixel(0, y);

                for (x = 0; x < ScreenWidth; ++x)
                {
                    ULONG Pixel = Src[x];

                    Dst[x] = ((ULONG)Lut[(Pixel >> 16) & 0xFF] << 16) |
                             ((ULONG)Lut[(Pixel >>  8) & 0xFF] <<  8) |
                              (ULONG)Lut[ Pixel        & 0xFF];
                }
            }
            else
            {
                for (x = 0; x < ScreenWidth; ++x)
                {
                    ULONG Pixel = Src[x];

                    *FramePixel(x, y) = ((ULONG)Lut[(Pixel >> 16) & 0xFF] << 16) |
                                        ((ULONG)Lut[(Pixel >>  8) & 0xFF] <<  8) |
                                         (ULONG)Lut[ Pixel        & 0xFF];
                }
            }
        }

        KeStallExecutionProcessor(BV_FADE_STEP_DELAY_US);
    }

    ExFreePoolWithTag(Snapshot, TAG_BOOTVID_FADE);

    VidSolidColorFill(0, 0, ScreenWidth - 1, ScreenHeight - 1, BV_COLOR_BLACK);
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
    {
        CachePaletteBlends();
        ApplyPalette();
    }
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
    ULONG x, y;

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

    if (FrameBufferRotation == LoaderFramebufferRotationIdentity)
    {
        for (y = 0; y < Height; ++y)
        {
            PUCHAR Src = Buffer + y * Delta;
            PUCHAR Dst = (PUCHAR)FrameBufferStart +
                         (ULONG_PTR)(Top + y) * BytesPerScanLine +
                         (ULONG_PTR)Left * BytesPerPixel;

            RtlCopyMemory(Dst, Src, Width * sizeof(ULONG));
        }
        return TRUE;
    }

    for (y = 0; y < Height; ++y)
    {
        PULONG Src = (PULONG)(Buffer + y * Delta);

        for (x = 0; x < Width; ++x)
            *FramePixel(Left + x, Top + y) = Src[x];
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
    Height = min(Height, VidpCharacterHeight);

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
    const BOOT_FONT_GLYPH* Glyph;
    const UCHAR* Bitmap;
    const BOOLEAN Opaque = (BackColor < BV_COLOR_NONE);
    ULONG Width, Height;
    ULONG Row;

    if ((Left >= VidpDisplayWidth) || (Top >= VidpDisplayHeight))
        return;

    if (TextColor >= BV_MAX_COLORS)
        return;

    Glyph = BootFontGetGlyph(&BootVidFont, (UCHAR)Character);
    if (!Glyph)
    {
        DisplayBitmapCharacter(Character, Left, Top, TextColor, BackColor);
        return;
    }

    Width = min(VidpCharacterWidth, VidpDisplayWidth - Left);
    Height = min(VidpCharacterHeight, VidpDisplayHeight - Top);

    if (Opaque)
    {
        for (Row = 0; Row < Height; ++Row)
            RtlFillMemory(BB_PIXEL(Left, Top + Row), Width, (UCHAR)BackColor);
    }

    Bitmap = BootFontGetGlyphBitmap(&BootVidFont, Glyph);
    if (Bitmap && Glyph->Width && Glyph->Height)
    {
        LONG RelativeX = max((LONG)Glyph->Left, 0L);
        LONG RelativeY = (LONG)BootVidFont.Baseline - Glyph->Top;

        for (Row = 0; Row < Glyph->Height; ++Row)
        {
            LONG Y = RelativeY + Row;
            ULONG Column;
            PUCHAR Back;

            if ((Y < 0) || (Y >= (LONG)Height))
                continue;

            Back = BB_PIXEL(Left, Top + Y);
            for (Column = 0; Column < Glyph->Width; ++Column)
            {
                LONG X = RelativeX + Column;
                UCHAR Alpha;

                if ((X < 0) || (X >= (LONG)Width))
                    continue;

                Alpha = Bitmap[Row * Glyph->Pitch + Column];
                if (Alpha != 0)
                    Back[X] = CachedBlendPalette[Back[X]][TextColor][Alpha];
            }
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

    if (FrameBufferRotation != LoaderFramebufferRotationIdentity)
    {
        FlushBackBufferRect(Left, Top, Width, Bottom - Top + 1);
        return;
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
