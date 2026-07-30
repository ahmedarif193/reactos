/*
 * PROJECT:     ReactOS Boot Font Renderer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared FreeType boot-console glyph rasterizer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_FONT_GLYPH_COUNT       256
#define BOOT_FONT_MIN_PIXEL_HEIGHT  16
#define BOOT_FONT_MAX_PIXEL_HEIGHT  64
#define BOOT_FONT_TARGET_ROWS       38
#define BOOT_FONT_MIN_COLUMNS       80
#define BOOT_FONT_MIN_ROWS          25

typedef struct _BOOT_FONT_GLYPH
{
    unsigned char Valid;
    short Left;
    short Top;
    unsigned short Width;
    unsigned short Height;
    unsigned short Pitch;
    unsigned long BitmapOffset;
} BOOT_FONT_GLYPH, *PBOOT_FONT_GLYPH;

typedef struct _BOOT_FONT_RENDERER
{
    unsigned char Enabled;
    unsigned long PixelHeight;
    unsigned long CellWidth;
    unsigned long CellHeight;
    unsigned long Baseline;
    unsigned char* BitmapBuffer;
    unsigned long BitmapSize;
    BOOT_FONT_GLYPH Glyphs[BOOT_FONT_GLYPH_COUNT];
} BOOT_FONT_RENDERER, *PBOOT_FONT_RENDERER;

/* The renderer must be zero-initialized before its first use. */
int
BootFontInitialize(
    PBOOT_FONT_RENDERER Renderer,
    unsigned long ScreenWidth,
    unsigned long ScreenHeight);

void
BootFontCleanup(
    PBOOT_FONT_RENDERER Renderer);

const BOOT_FONT_GLYPH*
BootFontGetGlyph(
    const BOOT_FONT_RENDERER* Renderer,
    unsigned char Character);

const unsigned char*
BootFontGetGlyphBitmap(
    const BOOT_FONT_RENDERER* Renderer,
    const BOOT_FONT_GLYPH* Glyph);

#ifdef __cplusplus
}
#endif
