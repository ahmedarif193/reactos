/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Text screen rendered on the Open Firmware console as a VT100 terminal
 */

#include <freeldr.h>

#define OFW_TEXT_WIDTH  80
#define OFW_TEXT_HEIGHT 25

/* What the terminal currently shows, so that a buffer copy only sends changed cells. */
static USHORT OfwShadow[OFW_TEXT_HEIGHT][OFW_TEXT_WIDTH];
static BOOLEAN OfwShadowValid;
static ULONG OfwCursorX = (ULONG)-1, OfwCursorY = (ULONG)-1;
static UCHAR OfwCurrentAttr = 0xFF;

/* Output is batched: the firmware write call is comparatively expensive. */
static CHAR OfwOutBuffer[512];
static ULONG OfwOutLength;

/* CP437 0x80-0xFF as Unicode, so the TUI line art reaches a UTF-8 terminal. */
static const USHORT OfwCp437[128] =
{
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

/* VGA color index to ANSI color index. */
static const UCHAR OfwVgaToAnsi[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

static
VOID
OfwFlush(VOID)
{
    if (OfwOutLength != 0 && OfwStdout != OFW_INVALID)
        OfwWrite(OfwStdout, OfwOutBuffer, OfwOutLength);
    OfwOutLength = 0;
}

static
VOID
OfwEmit(_In_reads_(Length) PCSTR Text, _In_ ULONG Length)
{
    while (Length--)
    {
        if (OfwOutLength == sizeof(OfwOutBuffer))
            OfwFlush();
        OfwOutBuffer[OfwOutLength++] = *Text++;
    }
}

static
VOID
OfwEmitString(_In_z_ PCSTR Text)
{
    OfwEmit(Text, (ULONG)strlen(Text));
}

static
VOID
OfwEmitNumber(_In_ ULONG Value)
{
    CHAR Digits[12];
    ULONG Count = 0;

    do
    {
        Digits[Count++] = (CHAR)('0' + Value % 10);
        Value /= 10;
    } while (Value);

    while (Count)
        OfwEmit(&Digits[--Count], 1);
}

static
VOID
OfwMoveCursor(_In_ ULONG X, _In_ ULONG Y)
{
    if (X == OfwCursorX && Y == OfwCursorY)
        return;

    OfwEmitString("\x1b[");
    OfwEmitNumber(Y + 1);
    OfwEmit(";", 1);
    OfwEmitNumber(X + 1);
    OfwEmit("H", 1);
    OfwCursorX = X;
    OfwCursorY = Y;
}

static
VOID
OfwSetAttr(_In_ UCHAR Attr)
{
    UCHAR Fore = Attr & 0x0F, Back = (Attr >> 4) & 0x07;

    if (Attr == OfwCurrentAttr)
        return;

    OfwEmitString("\x1b[0;");
    OfwEmitNumber((Fore & 8 ? 90 : 30) + OfwVgaToAnsi[Fore & 7]);
    OfwEmit(";", 1);
    OfwEmitNumber(40 + OfwVgaToAnsi[Back]);
    OfwEmit("m", 1);
    OfwCurrentAttr = Attr;
}

static
VOID
OfwEmitChar(_In_ UCHAR Ch)
{
    CHAR Utf8[3];
    USHORT Code;

    if (Ch < 0x20 || Ch == 0x7F)
        Ch = ' ';
    if (Ch < 0x80)
    {
        OfwEmit((PCSTR)&Ch, 1);
        return;
    }

    Code = OfwCp437[Ch - 0x80];
    if (Code < 0x800)
    {
        Utf8[0] = (CHAR)(0xC0 | (Code >> 6));
        Utf8[1] = (CHAR)(0x80 | (Code & 0x3F));
        OfwEmit(Utf8, 2);
    }
    else
    {
        Utf8[0] = (CHAR)(0xE0 | (Code >> 12));
        Utf8[1] = (CHAR)(0x80 | ((Code >> 6) & 0x3F));
        Utf8[2] = (CHAR)(0x80 | (Code & 0x3F));
        OfwEmit(Utf8, 3);
    }
}

static
VOID
OfwPutCell(_In_ ULONG X, _In_ ULONG Y, _In_ UCHAR Ch, _In_ UCHAR Attr)
{
    USHORT Cell = (USHORT)(Ch | (Attr << 8));

    if (X >= OFW_TEXT_WIDTH || Y >= OFW_TEXT_HEIGHT)
        return;
    if (OfwShadowValid && OfwShadow[Y][X] == Cell)
        return;

    OfwMoveCursor(X, Y);
    OfwSetAttr(Attr);
    OfwEmitChar(Ch);
    OfwShadow[Y][X] = Cell;

    /* Avoid the terminal's autowrap at the last column. */
    OfwCursorX = (X + 1 < OFW_TEXT_WIDTH) ? X + 1 : (ULONG)-1;
}

VOID
OfwVideoClearScreen(UCHAR Attr)
{
    ULONG X, Y;

    OfwCurrentAttr = 0xFF;
    OfwSetAttr(Attr);
    OfwEmitString("\x1b[2J\x1b[H");
    OfwCursorX = OfwCursorY = 0;
    for (Y = 0; Y < OFW_TEXT_HEIGHT; Y++)
        for (X = 0; X < OFW_TEXT_WIDTH; X++)
            OfwShadow[Y][X] = (USHORT)(' ' | (Attr << 8));
    OfwShadowValid = TRUE;
    OfwFlush();
}

VIDEODISPLAYMODE
OfwVideoSetDisplayMode(PCSTR DisplayMode, BOOLEAN Init)
{
    if (Init)
    {
        /* Leave auto-wrap off while the TUI owns the screen. */
        OfwEmitString("\x1b[?7l");
        OfwFlush();
    }
    else
    {
        OfwEmitString("\x1b[0m\x1b[?7h\x1b[?25h");
        OfwFlush();
        OfwCurrentAttr = 0xFF;
        OfwShadowValid = FALSE;
    }
    return VideoTextMode;
}

VOID
OfwVideoGetDisplaySize(PULONG Width, PULONG Height, PULONG Depth)
{
    *Width = OFW_TEXT_WIDTH;
    *Height = OFW_TEXT_HEIGHT;
    *Depth = 0;
}

ULONG
OfwVideoGetBufferSize(VOID)
{
    return OFW_TEXT_WIDTH * OFW_TEXT_HEIGHT * 2;
}

VOID
OfwVideoGetFontsFromFirmware(PULONG RomFontPointers)
{
    UNREFERENCED_PARAMETER(RomFontPointers);
}

VOID
OfwVideoSetTextCursorPosition(UCHAR X, UCHAR Y)
{
    OfwCursorX = OfwCursorY = (ULONG)-1;
    OfwMoveCursor(X, Y);
    OfwFlush();
}

VOID
OfwVideoHideShowTextCursor(BOOLEAN Show)
{
    OfwEmitString(Show ? "\x1b[?25h" : "\x1b[?25l");
    OfwFlush();
}

VOID
OfwVideoPutChar(int Ch, UCHAR Attr, unsigned X, unsigned Y)
{
    OfwPutCell(X, Y, (UCHAR)Ch, Attr);
    OfwFlush();
}

VOID
OfwVideoCopyOffScreenBufferToVRAM(PVOID Buffer)
{
    PUCHAR Cells = Buffer;
    ULONG X, Y;

    for (Y = 0; Y < OFW_TEXT_HEIGHT; Y++)
    {
        for (X = 0; X < OFW_TEXT_WIDTH; X++)
        {
            ULONG Index = (Y * OFW_TEXT_WIDTH + X) * 2;
            OfwPutCell(X, Y, Cells[Index], Cells[Index + 1]);
        }
    }
    OfwShadowValid = TRUE;
    OfwFlush();
}

BOOLEAN
OfwVideoIsPaletteFixed(VOID)
{
    return TRUE;
}

VOID
OfwVideoSetPaletteColor(UCHAR Color, UCHAR Red, UCHAR Green, UCHAR Blue)
{
    UNREFERENCED_PARAMETER(Color);
    UNREFERENCED_PARAMETER(Red);
    UNREFERENCED_PARAMETER(Green);
    UNREFERENCED_PARAMETER(Blue);
}

VOID
OfwVideoGetPaletteColor(UCHAR Color, UCHAR *Red, UCHAR *Green, UCHAR *Blue)
{
    UNREFERENCED_PARAMETER(Color);
    *Red = *Green = *Blue = 0;
}

VOID
OfwVideoSync(VOID)
{
    OfwFlush();
}

VOID
OfwBeep(VOID)
{
    OfwEmit("\a", 1);
    OfwFlush();
}
