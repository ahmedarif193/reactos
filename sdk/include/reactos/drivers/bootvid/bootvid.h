/*
 * PROJECT:     ReactOS Boot Video Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main BOOTVID header.
 * COPYRIGHT:   Copyright 2007-2020 Alex Ionescu (alex.ionescu@reactos.org)
 */

#ifndef _BOOTVID_
#define _BOOTVID_

#pragma once

#include "display.h"

/*
 * Bootvid drawing coordinates are expressed in Width x Height logical pixels.
 * PhysicalWidth x PhysicalHeight describes the underlying scanout. Legacy
 * backends report identical logical and physical 640x480 dimensions.
 */
typedef struct _VID_DISPLAY_INFO
{
    ULONG Width;
    ULONG Height;
    ULONG PhysicalWidth;
    ULONG PhysicalHeight;
    ULONG CharacterWidth;
    ULONG CharacterHeight;
    ULONG Dpi;
} VID_DISPLAY_INFO, *PVID_DISPLAY_INFO;

BOOLEAN
NTAPI
VidInitialize(
    _In_ BOOLEAN SetMode);

BOOLEAN
NTAPI
VidQueryDisplayInfo(_Out_ PVID_DISPLAY_INFO DisplayInfo);

VOID
NTAPI
VidResetDisplay(
    _In_ BOOLEAN SetMode);

VOID
NTAPI
VidCleanUp(VOID);

VOID
NTAPI
VidDisplayString(
    _In_ PCSTR String);

VOID
NTAPI
VidDisplayStringXY(
    _In_ PCSTR String,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ BOOLEAN Transparent);

VOID
NTAPI
VidSetScrollRegion(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom);

ULONG
NTAPI
VidSetTextColor(
    _In_ ULONG Color);

VOID
NTAPI
VidBitBlt(
    _In_ PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top);

VOID
NTAPI
VidBufferToScreenBlt(
    _In_reads_bytes_(Height * Stride) PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Stride);

VOID
NTAPI
VidScreenToBufferBlt(
    _Out_writes_bytes_all_(Height * Stride) PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Stride);

VOID
NTAPI
VidSolidColorFill(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ UCHAR Color);

#endif // _BOOTVID_
