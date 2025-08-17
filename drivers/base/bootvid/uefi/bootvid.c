/*
 * PROJECT:     ReactOS Boot Video Driver for UEFI
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UEFI GOP framebuffer support
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include "precomp.h"
#include <debug.h>

DBG_DEFAULT_CHANNEL(BOOTVID);

/* GLOBALS *******************************************************************/

static PULONG FrameBuffer = NULL;
static PHYSICAL_ADDRESS FrameBufferBase = {0};
static ULONG FrameBufferSize = 0;
static ULONG ScreenWidth = 0;
static ULONG ScreenHeight = 0;
static ULONG PixelsPerScanLine = 0;
static ULONG PixelFormat = 0;
static BOOLEAN UefiVideoInitialized = FALSE;

/* FUNCTIONS *****************************************************************/

BOOLEAN
NTAPI
VidInitialize(
    _In_ BOOLEAN ResetDisplay)
{
    PLOADER_PARAMETER_BLOCK LoaderBlock;
    PLOADER_PARAMETER_EXTENSION Extension;
    
    /* Get loader block */
    LoaderBlock = KeLoaderBlock;
    if (!LoaderBlock)
    {
        DPRINT1("UEFI VidInitialize: No loader block\n");
        return FALSE;
    }
    
    /* Get extension */
    Extension = LoaderBlock->Extension;
    if (!Extension)
    {
        DPRINT1("UEFI VidInitialize: No loader extension\n");
        return FALSE;
    }
    
    /* Check if we have UEFI framebuffer info */
    if (!Extension->BootViaEFI)
    {
        DPRINT1("UEFI VidInitialize: Not a UEFI boot\n");
        return FALSE;
    }
    
    if (!Extension->UefiFramebuffer.FrameBufferBase.QuadPart)
    {
        DPRINT1("UEFI VidInitialize: No framebuffer base address\n");
        return FALSE;
    }
    
    /* Save framebuffer info */
    FrameBufferBase = Extension->UefiFramebuffer.FrameBufferBase;
    FrameBufferSize = Extension->UefiFramebuffer.FrameBufferSize;
    ScreenWidth = Extension->UefiFramebuffer.ScreenWidth;
    ScreenHeight = Extension->UefiFramebuffer.ScreenHeight;
    PixelsPerScanLine = Extension->UefiFramebuffer.PixelsPerScanLine;
    PixelFormat = Extension->UefiFramebuffer.PixelFormat;
    
    /* Map the framebuffer */
    FrameBuffer = (PULONG)MmMapIoSpace(FrameBufferBase, FrameBufferSize, MmNonCached);
    if (!FrameBuffer)
    {
        DPRINT1("UEFI VidInitialize: Failed to map framebuffer\n");
        return FALSE;
    }
    
    DPRINT("UEFI VidInitialize: Framebuffer mapped at %p (phys 0x%llx), %dx%d\n",
           FrameBuffer, FrameBufferBase.QuadPart, ScreenWidth, ScreenHeight);
    
    /* Clear screen if requested */
    if (ResetDisplay)
    {
        VidResetDisplay(TRUE);
    }
    
    UefiVideoInitialized = TRUE;
    return TRUE;
}

VOID
NTAPI
VidCleanUp(VOID)
{
    /* Unmap framebuffer if mapped */
    if (FrameBuffer && FrameBufferSize)
    {
        MmUnmapIoSpace(FrameBuffer, FrameBufferSize);
        FrameBuffer = NULL;
    }
    
    UefiVideoInitialized = FALSE;
}

VOID
NTAPI
VidResetDisplay(
    _In_ BOOLEAN HalReset)
{
    ULONG PixelCount;
    PULONG Pixel;
    
    if (!UefiVideoInitialized || !FrameBuffer)
        return;
    
    /* Clear the framebuffer to black */
    PixelCount = ScreenWidth * ScreenHeight;
    Pixel = FrameBuffer;
    
    while (PixelCount--)
    {
        *Pixel++ = 0xFF000000; /* Black with full alpha */
    }
}

static VOID
UefiSetPixel(
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Color)
{
    PULONG Pixel;
    
    if (!FrameBuffer || X >= ScreenWidth || Y >= ScreenHeight)
        return;
    
    /* Calculate pixel position */
    Pixel = FrameBuffer + (Y * PixelsPerScanLine + X);
    *Pixel = Color;
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
    ULONG X, Y;
    ULONG UefiColor;
    
    if (!UefiVideoInitialized || !FrameBuffer)
        return;
    
    /* Convert VGA color to UEFI color (BGRA) */
    switch (Color)
    {
        case 0:  UefiColor = 0xFF000000; break; /* Black */
        case 1:  UefiColor = 0xFF800000; break; /* Blue */
        case 2:  UefiColor = 0xFF008000; break; /* Green */
        case 3:  UefiColor = 0xFF808000; break; /* Cyan */
        case 4:  UefiColor = 0xFF000080; break; /* Red */
        case 5:  UefiColor = 0xFF800080; break; /* Magenta */
        case 6:  UefiColor = 0xFF008080; break; /* Brown */
        case 7:  UefiColor = 0xFFC0C0C0; break; /* Light Gray */
        case 8:  UefiColor = 0xFF808080; break; /* Dark Gray */
        case 9:  UefiColor = 0xFFFF0000; break; /* Light Blue */
        case 10: UefiColor = 0xFF00FF00; break; /* Light Green */
        case 11: UefiColor = 0xFFFFFF00; break; /* Light Cyan */
        case 12: UefiColor = 0xFF0000FF; break; /* Light Red */
        case 13: UefiColor = 0xFFFF00FF; break; /* Light Magenta */
        case 14: UefiColor = 0xFF00FFFF; break; /* Yellow */
        case 15: UefiColor = 0xFFFFFFFF; break; /* White */
        default: UefiColor = 0xFF000000; break; /* Default to black */
    }
    
    /* Fill the rectangle */
    for (Y = Top; Y <= Bottom && Y < ScreenHeight; Y++)
    {
        for (X = Left; X <= Right && X < ScreenWidth; X++)
        {
            UefiSetPixel(X, Y, UefiColor);
        }
    }
}

VOID
NTAPI
VidScreenToBufferBlt(
    _Out_writes_bytes_(Delta * Height) PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Delta)
{
    ULONG X, Y;
    PULONG Src;
    PULONG Dst;
    
    if (!UefiVideoInitialized || !FrameBuffer)
        return;
    
    /* Copy from framebuffer to buffer */
    for (Y = 0; Y < Height && (Top + Y) < ScreenHeight; Y++)
    {
        Src = FrameBuffer + ((Top + Y) * PixelsPerScanLine + Left);
        Dst = (PULONG)(Buffer + Y * Delta);
        
        for (X = 0; X < Width && (Left + X) < ScreenWidth; X++)
        {
            *Dst++ = *Src++;
        }
    }
}

VOID
NTAPI
VidBufferToScreenBlt(
    _In_reads_bytes_(Delta * Height) PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Delta)
{
    ULONG X, Y;
    PULONG Src;
    PULONG Dst;
    
    if (!UefiVideoInitialized || !FrameBuffer)
        return;
    
    /* Copy from buffer to framebuffer */
    for (Y = 0; Y < Height && (Top + Y) < ScreenHeight; Y++)
    {
        Src = (PULONG)(Buffer + Y * Delta);
        Dst = FrameBuffer + ((Top + Y) * PixelsPerScanLine + Left);
        
        for (X = 0; X < Width && (Left + X) < ScreenWidth; X++)
        {
            *Dst++ = *Src++;
        }
    }
}

VOID
NTAPI
VidBitBlt(
    _In_ PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top)
{
    /* Simple 1-bit bitmap blit for text/logos */
    /* This would need proper implementation based on the bitmap format */
    /* For now, just do nothing */
}

VOID
NTAPI
VidDisplayString(
    _In_ PUCHAR String)
{
    /* Text display is not directly supported in framebuffer mode */
    /* This would need font rendering implementation */
}

VOID
NTAPI
VidSetTextColor(
    _In_ ULONG Color)
{
    /* Text color setting for future text rendering */
}

ULONG
NTAPI
VidGetTextColor(VOID)
{
    return 0x0F; /* Default white on black */
}

VOID
NTAPI
VidDisplayStringXY(
    _In_ PUCHAR String,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ BOOLEAN Transparent)
{
    /* Text display at specific position */
    /* Would need font rendering */
}

VOID
NTAPI
VidSetScrollRegion(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom)
{
    /* Scrolling region for text mode - not used in framebuffer */
}