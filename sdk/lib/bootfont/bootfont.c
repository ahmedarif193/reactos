/*
 * PROJECT:     ReactOS Boot Font Renderer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared FreeType boot-console glyph rasterizer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <windef.h>
#include <bootfont/bootfont.h>
#include "bootfont_data.h"

#include <ft2build.h>
#include FT_FREETYPE_H

static const WCHAR BootFontCp437ControlMap[32] =
{
    0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
    0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
    0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
    0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC
};

static const WCHAR BootFontCp437UpperHalfMap[128] =
{
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

static
WCHAR
BootFontCp437ToUnicode(
    _In_ UCHAR Character)
{
    if (Character < ARRAYSIZE(BootFontCp437ControlMap))
        return BootFontCp437ControlMap[Character];

    if (Character < 0x80)
        return (Character == 0x7F) ? 0x2302 : Character;

    return BootFontCp437UpperHalfMap[Character - 0x80];
}

static
BOOLEAN
BootFontLoadGlyph(
    _In_ FT_Face Face,
    _In_ UCHAR Character)
{
    WCHAR Unicode = BootFontCp437ToUnicode(Character);
    FT_UInt GlyphIndex = FT_Get_Char_Index(Face, Unicode);

    if ((GlyphIndex == 0) && (Unicode != 0))
        return FALSE;

    return FT_Load_Glyph(Face,
                         GlyphIndex,
                         FT_LOAD_DEFAULT |
                         FT_LOAD_NO_BITMAP |
                         FT_LOAD_NO_AUTOHINT) == FT_Err_Ok;
}

static
BOOLEAN
BootFontMeasure(
    _In_ FT_Face Face,
    _In_ ULONG PixelHeight,
    _Out_ PULONG CellWidth,
    _Out_ PULONG CellHeight,
    _Out_ PULONG Baseline)
{
    FT_GlyphSlot Slot;
    FT_Error Error;
    ULONG MaxGlyphWidth = 0;
    ULONG MaxAdvanceWidth = 0;
    LONG MaxTop = 0;
    LONG MaxBottom = 0;
    ULONG Character;
    BOOLEAN AnyGlyph = FALSE;

    Error = FT_Set_Pixel_Sizes(Face, 0, PixelHeight);
    if (Error != FT_Err_Ok)
        return FALSE;

    for (Character = 0; Character < BOOT_FONT_GLYPH_COUNT; ++Character)
    {
        LONG Advance;

        if (!BootFontLoadGlyph(Face, (UCHAR)Character))
            continue;

        Error = FT_Render_Glyph(Face->glyph, FT_RENDER_MODE_NORMAL);
        if (Error != FT_Err_Ok)
            continue;

        Slot = Face->glyph;
        if ((Slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) ||
            (Slot->bitmap.num_grays != 256))
        {
            continue;
        }

        AnyGlyph = TRUE;
        if (Slot->bitmap.width > MaxGlyphWidth)
            MaxGlyphWidth = Slot->bitmap.width;

        Advance = (LONG)((Slot->advance.x + 32) >> 6);
        if ((Advance > 0) && ((ULONG)Advance > MaxAdvanceWidth))
            MaxAdvanceWidth = (ULONG)Advance;

        if (Slot->bitmap_top > MaxTop)
            MaxTop = Slot->bitmap_top;

        if ((LONG)Slot->bitmap.rows - Slot->bitmap_top > MaxBottom)
            MaxBottom = (LONG)Slot->bitmap.rows - Slot->bitmap_top;
    }

    if (!AnyGlyph)
        return FALSE;

    if (MaxAdvanceWidth == 0)
        MaxAdvanceWidth = MaxGlyphWidth;

    *Baseline = ((ULONG)MaxTop > PixelHeight - 2) ? (ULONG)MaxTop : PixelHeight - 2;
    *CellHeight = *Baseline + (ULONG)MaxBottom;
    if (*CellHeight < PixelHeight)
        *CellHeight = PixelHeight;

    *CellWidth = (MaxAdvanceWidth > 8) ? MaxAdvanceWidth : 8;
    return TRUE;
}

static
BOOLEAN
BootFontChooseMetrics(
    _In_ FT_Face Face,
    _In_ ULONG ScreenWidth,
    _In_ ULONG ScreenHeight,
    _Out_ PULONG PixelHeight,
    _Out_ PULONG CellWidth,
    _Out_ PULONG CellHeight,
    _Out_ PULONG Baseline)
{
    ULONG Height;

    Height = ScreenHeight / BOOT_FONT_TARGET_ROWS;
    if (Height < BOOT_FONT_MIN_PIXEL_HEIGHT)
        Height = BOOT_FONT_MIN_PIXEL_HEIGHT;
    else if (Height > BOOT_FONT_MAX_PIXEL_HEIGHT)
        Height = BOOT_FONT_MAX_PIXEL_HEIGHT;

    for (;;)
    {
        if (!BootFontMeasure(Face, Height, CellWidth, CellHeight, Baseline))
            return FALSE;

        if ((((ScreenWidth / *CellWidth) >= BOOT_FONT_MIN_COLUMNS) &&
             ((ScreenHeight / *CellHeight) >= BOOT_FONT_MIN_ROWS)) ||
            (Height == BOOT_FONT_MIN_PIXEL_HEIGHT))
        {
            *PixelHeight = Height;
            return TRUE;
        }

        --Height;
    }
}

static
BOOLEAN
BootFontMeasureGlyphCache(
    _In_ FT_Face Face,
    _Out_ PULONG BitmapSize)
{
    FT_GlyphSlot Slot;
    FT_Error Error;
    ULONG TotalSize = 0;
    ULONG Character;

    for (Character = 0; Character < BOOT_FONT_GLYPH_COUNT; ++Character)
    {
        ULONG GlyphSize;

        if (!BootFontLoadGlyph(Face, (UCHAR)Character))
            continue;

        Error = FT_Render_Glyph(Face->glyph, FT_RENDER_MODE_NORMAL);
        if (Error != FT_Err_Ok)
            continue;

        Slot = Face->glyph;
        if ((Slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) ||
            (Slot->bitmap.num_grays != 256))
        {
            continue;
        }

        if (Slot->bitmap.rows &&
            (Slot->bitmap.width > UINT32_MAX / Slot->bitmap.rows))
        {
            return FALSE;
        }

        GlyphSize = Slot->bitmap.width * Slot->bitmap.rows;
        if (GlyphSize > UINT32_MAX - TotalSize)
            return FALSE;

        TotalSize += GlyphSize;
    }

    if (TotalSize == 0)
        return FALSE;

    *BitmapSize = TotalSize;
    return TRUE;
}

static
BOOLEAN
BootFontCacheGlyphs(
    _In_ FT_Face Face,
    _Inout_ PBOOT_FONT_RENDERER Renderer)
{
    FT_GlyphSlot Slot;
    FT_Error Error;
    ULONG BitmapOffset = 0;
    ULONG Character;

    for (Character = 0; Character < BOOT_FONT_GLYPH_COUNT; ++Character)
    {
        PBOOT_FONT_GLYPH Glyph = &Renderer->Glyphs[Character];
        ULONG GlyphSize;
        ULONG Row;
        LONG SourcePitch;

        if (!BootFontLoadGlyph(Face, (UCHAR)Character))
            continue;

        Error = FT_Render_Glyph(Face->glyph, FT_RENDER_MODE_NORMAL);
        if (Error != FT_Err_Ok)
            continue;

        Slot = Face->glyph;
        if ((Slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) ||
            (Slot->bitmap.num_grays != 256) ||
            (Slot->bitmap.width > UINT16_MAX) ||
            (Slot->bitmap.rows > UINT16_MAX))
        {
            continue;
        }

        GlyphSize = Slot->bitmap.width * Slot->bitmap.rows;
        if ((BitmapOffset > Renderer->BitmapSize) ||
            (GlyphSize > Renderer->BitmapSize - BitmapOffset))
        {
            return FALSE;
        }

        Glyph->Left = (SHORT)Slot->bitmap_left;
        Glyph->Top = (SHORT)Slot->bitmap_top;
        Glyph->Width = (USHORT)Slot->bitmap.width;
        Glyph->Height = (USHORT)Slot->bitmap.rows;
        Glyph->Pitch = Glyph->Width;
        Glyph->BitmapOffset = BitmapOffset;

        SourcePitch = Slot->bitmap.pitch;
        if (Glyph->Width &&
            (((SourcePitch >= 0) && ((ULONG)SourcePitch < Glyph->Width)) ||
             ((SourcePitch < 0) && ((ULONG)-SourcePitch < Glyph->Width)) ||
             !Slot->bitmap.buffer))
        {
            return FALSE;
        }

        for (Row = 0; Row < Glyph->Height; ++Row)
        {
            const UCHAR* Source;

            if (SourcePitch >= 0)
            {
                Source = Slot->bitmap.buffer + Row * SourcePitch;
            }
            else
            {
                Source = Slot->bitmap.buffer +
                         (Glyph->Height - 1 - Row) * -SourcePitch;
            }

            memcpy(Renderer->BitmapBuffer + BitmapOffset +
                   Row * Glyph->Pitch,
                   Source,
                   Glyph->Width);
        }

        BitmapOffset += GlyphSize;
        Glyph->Valid = TRUE;
    }

    return Renderer->Glyphs['A'].Valid && Renderer->Glyphs['0'].Valid;
}

void
BootFontCleanup(
    _Inout_ PBOOT_FONT_RENDERER Renderer)
{
    if (!Renderer)
        return;

    if (Renderer->BitmapBuffer)
        free(Renderer->BitmapBuffer);

    memset(Renderer, 0, sizeof(*Renderer));
}

int
BootFontInitialize(
    _Inout_ PBOOT_FONT_RENDERER Renderer,
    _In_ ULONG ScreenWidth,
    _In_ ULONG ScreenHeight)
{
    FT_Library Library = NULL;
    FT_Face Face = NULL;
    FT_Error Error;
    ULONG BitmapSize;
    BOOLEAN Success = FALSE;

    if (!Renderer || !ScreenWidth || !ScreenHeight)
        return FALSE;

    BootFontCleanup(Renderer);

    Error = FT_Init_FreeType(&Library);
    if (Error != FT_Err_Ok)
        goto Cleanup;

    Error = FT_New_Memory_Face(Library,
                               bootfont_data,
                               bootfont_data_SIZE,
                               0,
                               &Face);
    if (Error != FT_Err_Ok)
        goto Cleanup;

    Error = FT_Select_Charmap(Face, FT_ENCODING_UNICODE);
    if (Error != FT_Err_Ok)
        goto Cleanup;

    if (!BootFontChooseMetrics(Face,
                               ScreenWidth,
                               ScreenHeight,
                               &Renderer->PixelHeight,
                               &Renderer->CellWidth,
                               &Renderer->CellHeight,
                               &Renderer->Baseline))
    {
        goto Cleanup;
    }

    Error = FT_Set_Pixel_Sizes(Face, 0, Renderer->PixelHeight);
    if (Error != FT_Err_Ok)
        goto Cleanup;

    if (!BootFontMeasureGlyphCache(Face, &BitmapSize))
        goto Cleanup;

    Renderer->BitmapBuffer = malloc(BitmapSize);
    if (!Renderer->BitmapBuffer)
        goto Cleanup;

    Renderer->BitmapSize = BitmapSize;
    memset(Renderer->Glyphs, 0, sizeof(Renderer->Glyphs));

    if (!BootFontCacheGlyphs(Face, Renderer))
        goto Cleanup;

    Renderer->Enabled = TRUE;
    Success = TRUE;

Cleanup:
    if (Face)
        FT_Done_Face(Face);
    if (Library)
        FT_Done_FreeType(Library);

    if (!Success)
        BootFontCleanup(Renderer);

    return Success;
}

const BOOT_FONT_GLYPH*
BootFontGetGlyph(
    _In_ const BOOT_FONT_RENDERER* Renderer,
    _In_ UCHAR Character)
{
    if (!Renderer || !Renderer->Enabled || !Renderer->Glyphs[Character].Valid)
        return NULL;

    return &Renderer->Glyphs[Character];
}

const UCHAR*
BootFontGetGlyphBitmap(
    _In_ const BOOT_FONT_RENDERER* Renderer,
    _In_ const BOOT_FONT_GLYPH* Glyph)
{
    ULONG GlyphSize;

    if (!Renderer || !Renderer->Enabled || !Renderer->BitmapBuffer ||
        !Glyph || !Glyph->Valid ||
        (Glyph->BitmapOffset > Renderer->BitmapSize) ||
        (Glyph->Height && (Glyph->Pitch > UINT32_MAX / Glyph->Height)))
    {
        return NULL;
    }

    GlyphSize = Glyph->Pitch * Glyph->Height;
    if (GlyphSize > Renderer->BitmapSize - Glyph->BitmapOffset)
        return NULL;

    return Renderer->BitmapBuffer + Glyph->BitmapOffset;
}
