/*
 *  FreeLoader framebuffer context shared with the kernel
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#pragma once

typedef struct _FREELDR_FRAMEBUFFER_INFO
{
    unsigned long long BaseAddress;
    unsigned long      BufferSize;
    unsigned int       ScreenWidth;
    unsigned int       ScreenHeight;
    unsigned int       PixelsPerScanLine;
    unsigned int       PixelFormat;
    unsigned int       RedMask;
    unsigned int       GreenMask;
    unsigned int       BlueMask;
    unsigned int       ReservedMask;
} FREELDR_FRAMEBUFFER_INFO, *PFREELDR_FRAMEBUFFER_INFO;

/*
 * PixelFormat values match the EFI GOP definitions used by the kernel:
 *   0 - PixelRedGreenBlueReserved8BitPerColor
 *   1 - PixelBlueGreenRedReserved8BitPerColor
 *   2 - PixelBitMask
 */
