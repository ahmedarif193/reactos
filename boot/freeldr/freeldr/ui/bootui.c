/*
 * PROJECT:     LiberNT Boot Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Centered graphical boot menus and dialogs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <freeldr.h>
#include "../arch/vidfb.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include "bootui_font.h"
#include "bootui_icons.h"
#include "bootui_logo.h"

#define BOOTUI_BACKGROUND 0x202020
#define BOOTUI_TEXT 0xF3F6F8
#define BOOTUI_MUTED 0x9EABC3
#define BOOTUI_ACCENT 0x7AAAF0
#define BOOTUI_PANEL 0x101A2B

static PULONG BootUiPixels;
static ULONG BootUiWidth, BootUiHeight, BootUiFontSize;
static ULONG BootUiCellWidth, BootUiCellHeight;
static FT_Library BootUiLibrary;
static FT_Face BootUiFace;
static CHAR BootUiStatus[260];
static CHAR BootUiProgressText[260];
static MACH_POINTER_STATE BootUiPointer;
static BOOLEAN BootUiPointerVisible;
static BOOLEAN BootUiTabletMode;

typedef struct
{
    ULONG Left, Top, Width, RowHeight, First, Visible;
} BOOTUI_MENU_LAYOUT;

static ULONG
BootUiBlend(ULONG Background, ULONG Foreground, ULONG Alpha)
{
    ULONG R, G, B;
    R = (((Background >> 16) & 255) * (255 - Alpha) + ((Foreground >> 16) & 255) * Alpha + 127) / 255;
    G = (((Background >> 8) & 255) * (255 - Alpha) + ((Foreground >> 8) & 255) * Alpha + 127) / 255;
    B = ((Background & 255) * (255 - Alpha) + (Foreground & 255) * Alpha + 127) / 255;
    return (R << 16) | (G << 8) | B;
}

static VOID
BootUiPixel(LONG X, LONG Y, ULONG Color, ULONG Alpha)
{
    PULONG Pixel;
    if (!BootUiPixels || X < 0 || Y < 0 ||
        (ULONG)X >= BootUiWidth || (ULONG)Y >= BootUiHeight)
        return;
    Pixel = &BootUiPixels[Y * BootUiWidth + X];
    *Pixel = Alpha == 255 ? Color : BootUiBlend(*Pixel, Color, Alpha);
}

static VOID
BootUiRect(LONG X, LONG Y, ULONG Width, ULONG Height, ULONG Radius,
           ULONG Color, ULONG Alpha)
{
    LONG Row, Col, Dx, Dy;
    ULONG Coverage;
    Radius = min(Radius, min(Width, Height) / 2);
    for (Row = max(Y, 0); Row < min(Y + (LONG)Height, (LONG)BootUiHeight); ++Row)
    {
        for (Col = max(X, 0); Col < min(X + (LONG)Width, (LONG)BootUiWidth); ++Col)
        {
            Dx = max((LONG)Radius - (Col - X), Col - (X + (LONG)Width - 1 - (LONG)Radius));
            Dy = max((LONG)Radius - (Row - Y), Row - (Y + (LONG)Height - 1 - (LONG)Radius));
            Coverage = Alpha;
            if (Radius && Dx > 0 && Dy > 0)
            {
                LONG Distance = Dx * Dx + Dy * Dy;
                if (Distance > (LONG)(Radius * Radius))
                    continue;
                if (Distance > (LONG)((Radius - 1) * (Radius - 1)))
                    Coverage = Alpha * (Radius * Radius - Distance) / (2 * Radius - 1);
            }
            BootUiPixel(Col, Row, Color, Coverage);
        }
    }
}

static VOID
BootUiPresentRegion(ULONG Top, ULONG Height)
{
    if (!BootUiPixels)
        return;
    VidFbPresent(BootUiPixels, 0, Top, BootUiWidth, Height);
    MachVideoSync();
}

static VOID
BootUiPresent(VOID)
{
    BootUiPresentRegion(0, BootUiHeight);
}

static VOID
BootUiBackground(ULONG Height)
{
    ULONG Index, Count = min(Height, BootUiHeight) * BootUiWidth;
    if (!BootUiPixels)
        return;
    for (Index = 0; Index < Count; ++Index)
        BootUiPixels[Index] = (UiKeepFirmwareScreen || UiProgressBar.Show) ? 0 : BOOTUI_BACKGROUND;
}

static ULONG
BootUiTextWidth(PCSTR Text, ULONG Size, ULONG Count)
{
    ULONG Width = 0, I;
    if (!Text || !BootUiFace)
        return 0;
    FT_Set_Pixel_Sizes(BootUiFace, 0, Size);
    for (I = 0; Text[I] && Text[I] != '\n' && I < Count; ++I)
    {
        if (!FT_Load_Char(BootUiFace, (UCHAR)Text[I], FT_LOAD_DEFAULT))
            Width += (BootUiFace->glyph->advance.x + 32) >> 6;
    }
    return Width;
}

static VOID
BootUiText(LONG X, LONG Y, PCSTR Text, ULONG Size, ULONG Color,
           ULONG Width, ULONG Count)
{
    FT_GlyphSlot Glyph;
    ULONG I, Row, Col;
    LONG Cursor = X;
    if (!Text || !BootUiFace)
        return;
    FT_Set_Pixel_Sizes(BootUiFace, 0, Size);
    for (I = 0; Text[I] && Text[I] != '\n' && I < Count; ++I)
    {
        if (FT_Load_Char(BootUiFace, (UCHAR)Text[I], FT_LOAD_RENDER))
            continue;
        Glyph = BootUiFace->glyph;
        if (Cursor + ((Glyph->advance.x + 32) >> 6) > X + (LONG)Width)
            break;
        for (Row = 0; Row < Glyph->bitmap.rows; ++Row)
        {
            const UCHAR* Pixels = Glyph->bitmap.buffer + Row * Glyph->bitmap.pitch;
            for (Col = 0; Col < Glyph->bitmap.width; ++Col)
                BootUiPixel(Cursor + Glyph->bitmap_left + Col,
                            Y + Size - Glyph->bitmap_top + Row,
                            Color, Pixels[Col]);
        }
        Cursor += (Glyph->advance.x + 32) >> 6;
    }
}

static VOID
BootUiCentered(ULONG Y, PCSTR Text, ULONG Size, ULONG Color)
{
    ULONG Width = min(BootUiTextWidth(Text, Size, MAXULONG), BootUiWidth - 32);
    BootUiText((BootUiWidth - Width) / 2, Y, Text, Size, Color, Width, MAXULONG);
}

static VOID
BootUiIcon(LONG X, LONG Y, ULONG Index)
{
    ULONG Row, Col;
    for (Row = 0; Row < 24; ++Row)
        for (Col = 0; Col < 24; ++Col)
            BootUiPixel(X + Col, Y + Row, BOOTUI_TEXT,
                        bootui_icons[Index * 24 * 24 + Row * 24 + Col]);
}

static VOID
BootUiCursor(VOID)
{
    if (BootUiPointerVisible)
        BootUiIcon((LONG)BootUiPointer.X - 6, (LONG)BootUiPointer.Y - 3, 3);
}

static VOID
BootUiInputLabel(VOID)
{
    if (BootUiTabletMode)
        BootUiText(24, 24, "Tablet mode", max(14, BootUiFontSize - 3),
                   BOOTUI_MUTED, BootUiWidth - 48, MAXULONG);
}

static BOOLEAN
BootUiReadPointer(PMACH_POINTER_STATE Previous)
{
    MACH_POINTER_STATE State;
    if (!MachGetPointerState(BootUiWidth, BootUiHeight, &State))
        return FALSE;
    *Previous = BootUiPointer;
    BootUiPointer = State;
    BootUiPointerVisible = !State.Absolute;
    return TRUE;
}

static VOID
BootUiLogo(ULONG Left, ULONG Top, ULONG Scale)
{
    ULONG Width = 48 / Scale, Height = 42 / Scale;
    ULONG X, Y;
    for (Y = 0; Y < Height; ++Y)
    {
        for (X = 0; X < Width; ++X)
        {
            const UCHAR* Pixel = bootui_logo + (Y * Scale * 48 + X * Scale) * 4;
            ULONG Color, Alpha = Pixel[3];
            if (!Alpha)
                continue;
            Color = (min(255, Pixel[0] * 255 / Alpha) << 16) |
                    (min(255, Pixel[1] * 255 / Alpha) << 8) |
                    min(255, Pixel[2] * 255 / Alpha);
            BootUiPixel(Left + X, Top + Y, Color, Alpha);
        }
    }
}

static VOID
BootUiBrand(VOID)
{
    ULONG Scale = BootUiHeight < 600 ? 2 : 1;
    ULONG Width = 48 / Scale, Height = 42 / Scale;
    ULONG Size = BootUiHeight < 600 ? 20 : 26;
    ULONG TextWidth = BootUiTextWidth("LiberNT", Size, MAXULONG);
    ULONG Left = (BootUiWidth - Width - 14 - TextWidth) / 2;
    ULONG Top = BootUiHeight < 600 ? 12 : 28;
    BootUiLogo(Left, Top, Scale);
    BootUiText(Left + Width + 14, Top + (Height - min(Height, Size)) / 2 - 1,
               "LiberNT", Size, BOOTUI_TEXT, TextWidth, MAXULONG);
}

static VOID
BootUiUnInitialize(VOID)
{
    MachSetInputTimer(0);
    if (BootUiFace)
        FT_Done_Face(BootUiFace);
    if (BootUiLibrary)
        FT_Done_FreeType(BootUiLibrary);
    if (BootUiPixels)
        MmFreeMemory(BootUiPixels);
    BootUiFace = NULL;
    BootUiLibrary = NULL;
    BootUiPixels = NULL;
}

static BOOLEAN
BootUiInitialize(VOID)
{
    ULONG Depth;
    BootUiUnInitialize();
    VideoFreeOffScreenBuffer();
    VidFbGetDisplaySize(&BootUiWidth, &BootUiHeight, &Depth);
    if (Depth != 32 || BootUiWidth < 320 || BootUiHeight < 240 ||
        BootUiWidth > MAXULONG / sizeof(ULONG) / BootUiHeight)
        return FALSE;
    BootUiPixels = MmAllocateMemoryWithType(BootUiWidth * BootUiHeight * sizeof(ULONG),
                                            LoaderFirmwareTemporary);
    if (!BootUiPixels || FT_Init_FreeType(&BootUiLibrary) ||
        FT_New_Memory_Face(BootUiLibrary, bootui_font, sizeof(bootui_font), 0, &BootUiFace))
    {
        BootUiUnInitialize();
        return FALSE;
    }
    FbConsGetCellSize(&BootUiCellWidth, &BootUiCellHeight);
    BootUiFontSize = max(16, min(32, BootUiHeight / 38));
    BootUiStatus[0] = 0;
    UiShowTime = FALSE;
    UiMenuBox = FALSE;
    UiCenterMenu = TRUE;
    UiUseSpecialEffects = FALSE;
    UiTextColor = UiMenuFgColor = UiSelectedTextColor = COLOR_WHITE;
    UiMenuBgColor = UiBackdropBgColor = COLOR_BLACK;
    UiBackdropFgColor = UiStatusBarFgColor = COLOR_GRAY;
    UiStatusBarBgColor = UiTitleBoxBgColor = COLOR_BLACK;
    UiMessageBoxFgColor = UiEditBoxTextColor = UiTitleBoxFgColor = COLOR_WHITE;
    UiMessageBoxBgColor = UiEditBoxBgColor = UiSelectedTextBgColor = COLOR_DARKGRAY;
    UiBackdropFillStyle = ' ';
    return TRUE;
}

static VOID
BootUiDrawBackdrop(ULONG DrawHeight)
{
    BootUiBackground(min(DrawHeight * BootUiCellHeight, BootUiHeight));
    BootUiPresent();
}

static VOID
BootUiDrawText2(ULONG X, ULONG Y, ULONG Count, PCSTR Text, UCHAR Attr)
{
    if (X >= UiScreenWidth || Y >= UiScreenHeight)
        return;
    BootUiText(X * BootUiCellWidth, Y * BootUiCellHeight, Text, BootUiFontSize,
               (Attr & 15) == COLOR_WHITE ? BOOTUI_TEXT : BOOTUI_MUTED,
               BootUiWidth - X * BootUiCellWidth, Count ? Count : MAXULONG);
    BootUiPresent();
}

static VOID
BootUiDrawText(ULONG X, ULONG Y, PCSTR Text, UCHAR Attr)
{
    BootUiDrawText2(X, Y, 0, Text, Attr);
}

static VOID
BootUiFillArea(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom, CHAR Fill, UCHAR Attr)
{
    if (Left > Right || Top > Bottom || Left >= UiScreenWidth || Top >= UiScreenHeight)
        return;
    BootUiRect(Left * BootUiCellWidth, Top * BootUiCellHeight,
               (min(Right, UiScreenWidth - 1) - Left + 1) * BootUiCellWidth,
               (min(Bottom, UiScreenHeight - 1) - Top + 1) * BootUiCellHeight,
               0, BOOTUI_PANEL, 255);
    BootUiPresent();
}

static VOID
BootUiDrawShadow(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom)
{
}

static VOID
BootUiDrawBox(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom,
              UCHAR VertStyle, UCHAR HorzStyle, BOOLEAN Fill, BOOLEAN Shadow, UCHAR Attr)
{
    if (Left > Right || Top > Bottom || Left >= UiScreenWidth || Top >= UiScreenHeight)
        return;
    BootUiRect(Left * BootUiCellWidth, Top * BootUiCellHeight,
               (min(Right, UiScreenWidth - 1) - Left + 1) * BootUiCellWidth,
               (min(Bottom, UiScreenHeight - 1) - Top + 1) * BootUiCellHeight,
               12, BOOTUI_PANEL, 255);
    BootUiPresent();
}

static VOID
BootUiDrawCenteredText(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom,
                       PCSTR Text, UCHAR Attr)
{
    PCSTR End;
    ULONG Width, Y = Top * BootUiCellHeight;
    if (Left > Right || Top > Bottom || Left >= UiScreenWidth || Top >= UiScreenHeight)
        return;
    Width = (min(Right, UiScreenWidth - 1) - Left + 1) * BootUiCellWidth;
    while (*Text && Y < min((Bottom + 1) * BootUiCellHeight, BootUiHeight))
    {
        ULONG Count, TextWidth;
        End = strchr(Text, '\n');
        Count = End ? (ULONG)(End - Text) : (ULONG)strlen(Text);
        TextWidth = min(Width, BootUiTextWidth(Text, BootUiFontSize, Count));
        BootUiText(Left * BootUiCellWidth + (Width - TextWidth) / 2, Y,
                   Text, BootUiFontSize, BOOTUI_TEXT, Width, Count);
        if (!End)
            break;
        Text = End + 1;
        Y += BootUiCellHeight;
    }
    BootUiPresent();
}

static VOID
BootUiDrawStatusText(PCSTR Text)
{
    RtlStringCbCopyA(BootUiStatus, sizeof(BootUiStatus), Text);
}

static VOID
BootUiNoOperation(VOID)
{
}

static ULONG
BootUiReadKey(PBOOLEAN Extended)
{
    ULONG Key;
    while (!MachConsKbHit())
        MachHwIdle();
    Key = MachConsGetCh();
    *Extended = (Key == KEY_EXTENDED);
    if (*Extended)
    {
        Key = MachConsGetCh();
        if (Key == KEY_ESC)
            *Extended = FALSE;
    }
    return Key;
}

static VOID
BootUiMessageBoxCritical(PCSTR Message)
{
    static const CHAR Prompt[] = "Enter, volume button, or tap  Continue";
    ULONG X = 2, Y = 2, I;
    BOOLEAN Extended;
    BOOLEAN KeepFirmwareScreen = UiKeepFirmwareScreen;
    BOOLEAN ShowProgress = UiProgressBar.Show;
    ULONG Key;
    UiKeepFirmwareScreen = FALSE;
    UiProgressBar.Show = FALSE;
    FbConsClearScreen(ATTR(COLOR_WHITE, COLOR_BLACK));
    while (*Message && Y + 3 < UiScreenHeight)
    {
        if (*Message == '\n')
        {
            X = 2;
            ++Y;
        }
        else
        {
            FbConsPutChar((UCHAR)*Message, ATTR(COLOR_WHITE, COLOR_BLACK), X++, Y);
            if (X + 2 >= UiScreenWidth)
            {
                X = 2;
                ++Y;
            }
        }
        ++Message;
    }
    for (I = 0; Prompt[I] && I + 2 < UiScreenWidth; ++I)
        FbConsPutChar(Prompt[I], ATTR(COLOR_WHITE, COLOR_BLACK), I + 2, UiScreenHeight - 2);
    MachVideoSync();
    for (;;)
    {
        MACH_POINTER_STATE Previous;
        if (MachConsKbHit())
        {
            Key = BootUiReadKey(&Extended);
            if (Key == KEY_ENTER || Key == KEY_ESC || Key == KEY_VOLUME_UP || Key == KEY_VOLUME_DOWN)
                break;
        }
        else if (BootUiReadPointer(&Previous) && BootUiPointer.LeftButton && !Previous.LeftButton)
            break;
        MachHwIdle();
    }
    UiKeepFirmwareScreen = KeepFirmwareScreen;
    UiProgressBar.Show = ShowProgress;
}

static ULONG
BootUiParagraph(PCSTR Text, ULONG X, ULONG Y, ULONG Width, ULONG MaxLines, ULONG Skip)
{
    ULONG Lines = 0, Count, Fit, LastSpace, Size = max(14, BootUiFontSize - 2);
    while (*Text)
    {
        Fit = LastSpace = 0;
        for (Count = 0; Text[Count] && Text[Count] != '\n'; ++Count)
        {
            if (BootUiTextWidth(Text, Size, Count + 1) > Width)
                break;
            Fit = Count + 1;
            if (Text[Count] == ' ')
                LastSpace = Count;
        }
        if (Text[Count] && Text[Count] != '\n' && LastSpace)
            Fit = LastSpace;
        if (!Fit && Text[0] != '\n')
            Fit = 1;
        if (Lines >= Skip && Lines - Skip < MaxLines)
            BootUiText(X, Y + (Lines - Skip) * (Size + 8), Text, Size, BOOTUI_TEXT, Width, Fit);
        ++Lines;
        Text += Fit;
        if (*Text == '\n' || *Text == ' ')
            ++Text;
    }
    return Lines;
}

static BOOLEAN
BootUiDialog(PCSTR Message, PCHAR Edit, ULONG Length)
{
    ULONG Width = min(BootUiWidth - 48, 780);
    ULONG Left = (BootUiWidth - Width) / 2;
    ULONG Size = max(14, BootUiFontSize - 2), LineHeight = Size + 8;
    ULONG MaxLines = max(1, (BootUiHeight - 180) / LineHeight);
    ULONG Lines, Visible, Top, PanelHeight, Key, Offset = 0, Cursor = 0, Start = 0;
    BOOLEAN Extended;
    BOOLEAN KeepFirmwareScreen = UiKeepFirmwareScreen;
    BOOLEAN ShowProgress = UiProgressBar.Show;
    BOOLEAN Accept = !Edit, Timer = FALSE;
    LONG Remaining = -1, PressedButton = -1;
    ULONG Second = ArcGetTime()->Second;
    MACH_POINTER_STATE Previous;
    PCHAR Original = NULL;
    if (Edit)
    {
        if (!Length || !memchr(Edit, 0, Length))
            return FALSE;
        Original = FrLdrTempAlloc(Length, 'eUiB');
        if (!Original)
            return FALSE;
        RtlStringCbCopyA(Original, Length, Edit);
        Cursor = (ULONG)strlen(Edit);
    }
    UiKeepFirmwareScreen = FALSE;
    UiProgressBar.Show = FALSE;
    Lines = BootUiParagraph(Message, 0, 0, Width - 48, 0, 0);
    Visible = min(Lines, MaxLines);
    PanelHeight = Visible * LineHeight + (Edit ? 152 : 112);
    Top = (BootUiHeight - min(PanelHeight, BootUiHeight)) / 2;
    for (;;)
    {
        ULONG EditY = Top + 40 + Visible * LineHeight;
        ULONG ButtonY = Top + PanelHeight - 56;
        ULONG ButtonWidth = (Width - 64) / 2;
        BootUiBackground(BootUiHeight);
        BootUiRect(Left, Top, Width, PanelHeight, 14, BOOTUI_PANEL, 245);
        BootUiParagraph(Message, Left + 24, Top + 24, Width - 48, Visible, Offset);
        if (Edit)
        {
            ULONG TextWidth;
            if (Start > Cursor)
                Start = Cursor;
            while (BootUiTextWidth(Edit + Start, BootUiFontSize, Cursor - Start) > Width - 80)
                ++Start;
            BootUiRect(Left + 24, EditY, Width - 48, BootUiFontSize + 20, 6, 0x081020, 255);
            BootUiText(Left + 36, EditY + 8, Edit + Start, BootUiFontSize, BOOTUI_TEXT, Width - 72, MAXULONG);
            TextWidth = BootUiTextWidth(Edit + Start, BootUiFontSize, Cursor - Start);
            BootUiRect(Left + 36 + TextWidth, EditY + 8, 2, BootUiFontSize + 4, 0, BOOTUI_ACCENT, 255);
        }
        BootUiInputLabel();
        BootUiRect(Left + 24, ButtonY, ButtonWidth, 40, 6, BOOTUI_TEXT, Accept ? 14 : 40);
        BootUiRect(Left + 40 + ButtonWidth, ButtonY, ButtonWidth, 40, 6, BOOTUI_TEXT, Accept ? 40 : 14);
        BootUiText(Left + 40, ButtonY + 8, Edit ? "Cancel" : "Back", Size,
                   BOOTUI_TEXT, ButtonWidth - 32, MAXULONG);
        BootUiText(Left + 56 + ButtonWidth, ButtonY + 8, Edit ? "Save" : "Continue", Size,
                   BOOTUI_TEXT, ButtonWidth - 32, MAXULONG);
        if (Remaining > 0)
        {
            CHAR Countdown[80];
            RtlStringCbPrintfA(Countdown, sizeof(Countdown), "%s in %ld seconds",
                               Accept ? (Edit ? "Save" : "Continue") : "Back", Remaining);
            BootUiCentered(BootUiHeight - 62, Countdown, Size, BOOTUI_ACCENT);
        }
        if (Lines > Visible)
            BootUiCentered(BootUiHeight - 28, "Up / Down  Scroll message", Size, BOOTUI_MUTED);
        BootUiCursor();
        BootUiPresent();
        Key = 0;
        Extended = FALSE;
        for (;;)
        {
            if (MachConsKbHit())
            {
                Key = BootUiReadKey(&Extended);
                if (Extended && (Key == KEY_VOLUME_UP || Key == KEY_VOLUME_DOWN))
                {
                    BootUiTabletMode = TRUE;
                    Accept = !Accept;
                    Remaining = 5;
                    Timer = MachSetInputTimer(1000);
                    Second = ArcGetTime()->Second;
                    Key = 0;
                }
                else
                {
                    BootUiTabletMode = FALSE;
                    Remaining = -1;
                    MachSetInputTimer(0);
                }
                break;
            }
            if (BootUiReadPointer(&Previous))
            {
                LONG Button = -1;
                Remaining = -1;
                MachSetInputTimer(0);
                if (BootUiPointer.Y >= ButtonY && BootUiPointer.Y < ButtonY + 40)
                {
                    if (BootUiPointer.X >= Left + 24 && BootUiPointer.X < Left + 24 + ButtonWidth)
                        Button = 0;
                    if (BootUiPointer.X >= Left + 40 + ButtonWidth && BootUiPointer.X < Left + Width - 24)
                        Button = 1;
                }
                if (Button >= 0)
                    Accept = Button != 0;
                if (BootUiPointer.LeftButton && !Previous.LeftButton)
                    PressedButton = Button;
                if (!BootUiPointer.LeftButton && Previous.LeftButton)
                {
                    if (Button >= 0 && Button == PressedButton)
                        Key = Button ? KEY_ENTER : KEY_ESC;
                    PressedButton = -1;
                }
                if (BootUiPointer.Wheel)
                {
                    Key = BootUiPointer.Wheel > 0 ? KEY_UP : KEY_DOWN;
                    Extended = TRUE;
                }
                if (BootUiPointer.RightButton && !Previous.RightButton)
                {
                    Key = KEY_ESC;
                    Extended = FALSE;
                }
                break;
            }
            if (Remaining > 0 && (Timer ? MachInputTimerExpired() : ArcGetTime()->Second != Second))
            {
                Second = ArcGetTime()->Second;
                if (!--Remaining)
                    Key = Accept ? KEY_ENTER : KEY_ESC;
                else
                    Timer = MachSetInputTimer(1000);
                break;
            }
            MachHwIdle();
        }
        if (!Extended && (Key == KEY_ENTER || Key == KEY_ESC))
        {
            if (Original)
            {
                if (Key == KEY_ESC)
                    RtlStringCbCopyA(Edit, Length, Original);
                FrLdrTempFree(Original, 'eUiB');
            }
            MachSetInputTimer(0);
            UiKeepFirmwareScreen = KeepFirmwareScreen;
            UiProgressBar.Show = ShowProgress;
            BootUiBackground(BootUiHeight);
            BootUiPresent();
            return Key == KEY_ENTER;
        }
        if (Extended)
        {
            if (Key == KEY_UP && Offset)
                --Offset;
            if (Key == KEY_DOWN && Offset + Visible < Lines)
                ++Offset;
            if (!Edit)
                continue;
            if (Key == KEY_LEFT && Cursor)
                --Cursor;
            if (Key == KEY_RIGHT && Edit[Cursor])
                ++Cursor;
            if (Key == KEY_HOME)
                Cursor = 0;
            if (Key == KEY_END)
                Cursor = (ULONG)strlen(Edit);
            if (Key == KEY_DELETE && Edit[Cursor])
                memmove(Edit + Cursor, Edit + Cursor + 1, strlen(Edit + Cursor));
        }
        else if (Edit)
        {
            if (Key == KEY_BACKSPACE && Cursor)
            {
                --Cursor;
                memmove(Edit + Cursor, Edit + Cursor + 1, strlen(Edit + Cursor));
            }
            else if (Key >= 32 && Key < 127 && strlen(Edit) + 1 < Length)
            {
                memmove(Edit + Cursor + 1, Edit + Cursor, strlen(Edit + Cursor) + 1);
                Edit[Cursor++] = (CHAR)Key;
            }
        }
    }
}

static VOID
BootUiMessageBox(PCSTR Message)
{
    BootUiDialog(Message, NULL, 0);
}

static BOOLEAN
BootUiEditBox(PCSTR Message, PCHAR Edit, ULONG Length)
{
    return BootUiDialog(Message, Edit, Length);
}

static VOID
BootUiSetProgressBarText(PCSTR Text)
{
    RtlStringCbCopyA(BootUiProgressText, sizeof(BootUiProgressText), Text);
}

static VOID
BootUiTickProgressBar(ULONG Progress)
{
    ULONG Width = min(720, BootUiWidth - 64);
    ULONG Left = (BootUiWidth - Width) / 2;
    ULONG Top = BootUiHeight - 100;
    BootUiRect(0, Top - 20, BootUiWidth, 120, 0, 0, 255);
    BootUiCentered(Top, BootUiProgressText, max(14, BootUiFontSize - 3), BOOTUI_MUTED);
    BootUiRect(Left, Top + 44, Width, 10, 5, 0x23314A, 255);
    BootUiRect(Left, Top + 44, Width * min(Progress, 10000) / 10000, 10, 5, BOOTUI_ACCENT, 255);
    BootUiPresentRegion(Top - 20, 120);
}

static VOID
BootUiDrawProgressBar(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom, PCSTR Text)
{
    UiKeepFirmwareScreen = TRUE;
    BootUiBackground(BootUiHeight);
    BootUiPresent();
    UiInitProgressBar(Left, Top, Right, Bottom, Text);
}

static VOID
BootUiDrawProgressBarCenter(PCSTR Text)
{
    BootUiDrawProgressBar(2, UiScreenHeight - 5, UiScreenWidth - 3, UiScreenHeight - 2, Text);
}

static UCHAR
BootUiTextToColor(PCSTR Text)
{
    static const PCSTR Names[] =
    {
        "Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown", "Gray",
        "DarkGray", "LightBlue", "LightGreen", "LightCyan", "LightRed", "LightMagenta", "Yellow", "White"
    };
    ULONG I;
    for (I = 0; I < RTL_NUMBER_OF(Names); ++I)
        if (!_stricmp(Text, Names[I]))
            return (UCHAR)I;
    return COLOR_GRAY;
}

static UCHAR
BootUiTextToFillStyle(PCSTR Text)
{
    return ' ';
}

static VOID
BootUiFadeInBackdrop(VOID)
{
    BootUiBackground(BootUiHeight);
    BootUiPresent();
}

static VOID
BootUiRenderMenu(PUI_MENU_INFO Menu, LONG TimeOut, BOOLEAN CanEscape, BOOTUI_MENU_LAYOUT* Layout)
{
    ULONG I, Count = 0, Total, Rank = 0, First, Visible, Row = 0;
    ULONG RowHeight = BootUiFontSize + 30;
    ULONG Width = min(BootUiWidth - 64, max(440, BootUiWidth * 2 / 5));
    ULONG Left = (BootUiWidth - Width) / 2, Top, TextWidth, Icon, Size;
    CHAR Status[320];
    for (I = 0; I < Menu->MenuItemCount; ++I)
    {
        if (!Menu->MenuItemList[I])
            continue;
        if (I == Menu->SelectedMenuItem)
            Rank = Count;
        ++Count;
        TextWidth = BootUiTextWidth(Menu->MenuItemList[I], BootUiFontSize, MAXULONG);
        Width = min(BootUiWidth - 64, max(Width, TextWidth + 96));
    }
    Total = Count;
    Left = (BootUiWidth - Width) / 2;
    Visible = min(Count, max(1, (BootUiHeight - (BootUiHeight >= 600 ? 260 : 200)) / RowHeight));
    First = Rank >= Visible ? Rank - Visible + 1 : 0;
    if (First + Visible > Count)
        First = Count - Visible;
    Top = (BootUiHeight - Visible * RowHeight) / 2;
    Size = max(14, BootUiFontSize - 5);
    BootUiBackground(BootUiHeight);
    BootUiBrand();
    BootUiInputLabel();
    if (Layout)
    {
        Layout->Left = Left;
        Layout->Top = Top;
        Layout->Width = Width;
        Layout->RowHeight = RowHeight;
        Layout->First = First;
        Layout->Visible = Visible;
    }
    BootUiCentered(Top - 52, Menu->MenuHeader, Size, BOOTUI_MUTED);
    Count = 0;
    for (I = 0; I < Menu->MenuItemCount; ++I)
    {
        PCSTR Item = Menu->MenuItemList[I];
        ULONG Y, TextInset;
        if (!Item)
            continue;
        if (Count++ < First)
            continue;
        if (Row == Visible)
            break;
        Y = Top + Row++ * RowHeight;
        if (I == Menu->SelectedMenuItem)
            BootUiRect(Left, Y + 3, Width, RowHeight - 6, 9, 0xFFFFFF, 35);
        Icon = CanEscape ? 1 : 0;
        if (!strcmp(Item, "Startup options") || !strcmp(Item, "Settings"))
            Icon = 1;
        if (!strncmp(Item, "Back", 4))
            Icon = 2;
        if (Menu->MenuItemIcons && Menu->MenuItemIcons[I] == UiMenuIconLiberNT)
        {
            BootUiLogo(Left + 20, Y + (RowHeight - 21) / 2, 2);
            TextInset = 64;
        }
        else if (Icon)
        {
            BootUiIcon(Left + 20, Y + (RowHeight - 24) / 2, Icon);
            TextInset = 64;
        }
        else
            TextInset = 20;
        BootUiText(Left + TextInset, Y + (RowHeight - BootUiFontSize) / 2 - 2,
                   Item, BootUiFontSize, I == Menu->SelectedMenuItem ? BOOTUI_TEXT : 0xC6D1D5,
                   Width - TextInset - 20, MAXULONG);
    }
    if (Total > Visible)
    {
        RtlStringCbPrintfA(Status, sizeof(Status), "%lu / %lu", Rank + 1,
                          Total);
        BootUiCentered(Top + Visible * RowHeight + 12, Status, Size, BOOTUI_MUTED);
    }
    if (TimeOut > 0)
    {
        PCSTR Item = Menu->MenuItemList[Menu->SelectedMenuItem];
        PCSTR Verb = !CanEscape ? "Starting" : "Selecting";
        if (!strcmp(Item, "Startup options") || !strcmp(Item, "Settings") ||
            !strcmp(Item, "Recovery options"))
            Verb = "Opening";
        if (!strncmp(Item, "Boot ", 5))
            RtlStringCbPrintfA(Status, sizeof(Status), "%s in %ld seconds", Item, TimeOut);
        else
            RtlStringCbPrintfA(Status, sizeof(Status), "%s %s in %ld seconds", Verb, Item, TimeOut);
        BootUiCentered(BootUiHeight - 104, Status, Size, BOOTUI_ACCENT);
    }
    else if (BootUiStatus[0])
        BootUiCentered(BootUiHeight - 104, BootUiStatus, Size, BOOTUI_ACCENT);
    if (Menu->MenuFooter)
        BootUiCentered(BootUiHeight - 74, Menu->MenuFooter, Size, BOOTUI_MUTED);
    BootUiCentered(BootUiHeight - 40,
                   BootUiTabletMode ? "Volume up / down  Select     Wait 5 seconds to choose" :
                   CanEscape ? "Tap to choose     Up / Down  Select     Enter  Choose     Esc  Back" :
                               "Tap to start     Up / Down  Select     Enter  Start",
                   Size, BOOTUI_MUTED);
    BootUiCursor();
    BootUiPresent();
}

static VOID
BootUiDrawMenu(PUI_MENU_INFO Menu)
{
    BootUiRenderMenu(Menu, -1, TRUE, NULL);
}

static VOID
BootUiDrawCountdown(LONG TimeOut)
{
    CHAR Text[80];
    ULONG Size = max(14, BootUiFontSize - 3);
    BootUiBackground(BootUiHeight);
    RtlStringCbPrintfA(Text, sizeof(Text), "Starting in %ld %s", TimeOut,
                       TimeOut == 1 ? "second" : "seconds");
    BootUiCentered(BootUiHeight - 100, Text, Size, BOOTUI_TEXT);
    BootUiCentered(BootUiHeight - 56, "Press a key, a volume button, or tap to open the boot menu", Size, BOOTUI_MUTED);
    BootUiPresent();
}

static ULONG
BootUiMenuHit(PUI_MENU_INFO Menu, BOOTUI_MENU_LAYOUT* Layout, ULONG X, ULONG Y)
{
    ULONG I, Rank = 0, Target;
    if (X < Layout->Left || X >= Layout->Left + Layout->Width ||
        Y < Layout->Top || Y >= Layout->Top + Layout->Visible * Layout->RowHeight)
        return MAXULONG;
    Target = Layout->First + (Y - Layout->Top) / Layout->RowHeight;
    for (I = 0; I < Menu->MenuItemCount; ++I)
        if (Menu->MenuItemList[I] && Rank++ == Target)
            return I;
    return MAXULONG;
}

static VOID
BootUiMoveSelection(PUI_MENU_INFO Menu, BOOLEAN Up)
{
    ULONG I = Menu->SelectedMenuItem;
    do
        I = Up ? (I ? I - 1 : Menu->MenuItemCount - 1) : (I + 1) % Menu->MenuItemCount;
    while (!Menu->MenuItemList[I]);
    Menu->SelectedMenuItem = I;
}

static BOOLEAN
BootUiDisplayMenu(PCSTR Header, PCSTR Footer, PCSTR Items[], ULONG Count,
                  ULONG Default, LONG TimeOut, PULONG Selected, BOOLEAN CanEscape,
                  const UI_MENU_ICON* Icons,
                  UiMenuKeyPressFilterCallback Filter, PVOID Context)
{
    UI_MENU_INFO Menu = {0};
    BOOTUI_MENU_LAYOUT Layout = {0};
    MACH_POINTER_STATE Previous;
    ULONG Key, I, Second, Hit, Pressed = MAXULONG;
    LONG DragY = 0;
    BOOLEAN Extended, Redraw = TRUE, Timer, Dragged = FALSE, Result = TRUE;
    if (!Count || !Items)
        return FALSE;
    if (Default >= Count || !Items[Default])
    {
        for (Default = 0; Default < Count && !Items[Default]; ++Default)
            ;
        if (Default == Count)
            return FALSE;
    }
    Menu.MenuHeader = Header;
    Menu.MenuFooter = Footer;
    Menu.MenuItemList = Items;
    Menu.MenuItemIcons = Icons;
    Menu.MenuItemCount = Count;
    Menu.SelectedMenuItem = Default;
    Menu.Context = Context;
    UiProgressBar.Show = FALSE;
    if (TimeOut < 0)
        UiKeepFirmwareScreen = FALSE;
    Second = ArcGetTime()->Second;
    Timer = MachSetInputTimer(TimeOut > 0 ? 1000 : 0);
    for (;;)
    {
        if (Redraw)
        {
            if (UiKeepFirmwareScreen)
                BootUiDrawCountdown(TimeOut);
            else
                BootUiRenderMenu(&Menu, TimeOut, CanEscape, &Layout);
            Redraw = FALSE;
        }
        if (MachConsKbHit())
        {
            Key = BootUiReadKey(&Extended);
            if (Extended && (Key == KEY_VOLUME_UP || Key == KEY_VOLUME_DOWN))
            {
                BootUiTabletMode = TRUE;
                TimeOut = 5;
                if (!UiKeepFirmwareScreen)
                    BootUiMoveSelection(&Menu, Key == KEY_VOLUME_UP);
            }
            else
            {
                BootUiTabletMode = FALSE;
                TimeOut = -1;
            }
            Timer = MachSetInputTimer(TimeOut > 0 ? 1000 : 0);
            Second = ArcGetTime()->Second;
            Redraw = TRUE;
            Pressed = MAXULONG;
            if (UiKeepFirmwareScreen)
            {
                UiKeepFirmwareScreen = FALSE;
                continue;
            }
            if (Filter && Extended && Filter(Key, Menu.SelectedMenuItem, Context))
            {
                TimeOut = -1;
                MachSetInputTimer(0);
                continue;
            }
            if (!Extended && Key == KEY_ENTER)
                break;
            if (!Extended && Key == KEY_ESC && CanEscape)
            {
                Result = FALSE;
                break;
            }
            if (!Extended)
                continue;
            if (Key == KEY_HOME || Key == KEY_END)
            {
                I = Key == KEY_HOME ? 0 : Count - 1;
                while (!Items[I])
                    I = Key == KEY_HOME ? I + 1 : I - 1;
                Menu.SelectedMenuItem = I;
            }
            else if (Key == KEY_UP || Key == KEY_DOWN)
                BootUiMoveSelection(&Menu, Key == KEY_UP);
        }
        else if (BootUiReadPointer(&Previous))
        {
            if (UiKeepFirmwareScreen)
            {
                if (BootUiPointer.LeftButton || BootUiPointer.RightButton)
                {
                    UiKeepFirmwareScreen = FALSE;
                    TimeOut = -1;
                    MachSetInputTimer(0);
                    Redraw = TRUE;
                }
                goto CheckTimeout;
            }
            Redraw = TRUE;
            TimeOut = -1;
            MachSetInputTimer(0);
            if (BootUiPointer.Absolute && !BootUiPointer.LeftButton && Previous.LeftButton)
                Hit = BootUiMenuHit(&Menu, &Layout, Previous.X, Previous.Y);
            else
                Hit = BootUiMenuHit(&Menu, &Layout, BootUiPointer.X, BootUiPointer.Y);
            if (BootUiPointer.RightButton && !Previous.RightButton && CanEscape)
            {
                Result = FALSE;
                break;
            }
            if (BootUiPointer.Wheel)
                BootUiMoveSelection(&Menu, BootUiPointer.Wheel > 0);
            else if (!BootUiPointer.Absolute && Hit != MAXULONG)
                Menu.SelectedMenuItem = Hit;
            if (BootUiPointer.LeftButton && !Previous.LeftButton)
            {
                Pressed = Hit;
                DragY = BootUiPointer.Y;
                Dragged = FALSE;
                if (Hit != MAXULONG)
                    Menu.SelectedMenuItem = Hit;
            }
            else if (BootUiPointer.Absolute && BootUiPointer.LeftButton && Pressed != MAXULONG &&
                     labs((LONG)BootUiPointer.Y - DragY) >= (LONG)Layout.RowHeight)
            {
                BootUiMoveSelection(&Menu, (LONG)BootUiPointer.Y > DragY);
                DragY = BootUiPointer.Y;
                Dragged = TRUE;
            }
            else if (!BootUiPointer.LeftButton && Previous.LeftButton)
            {
                if (!Dragged && Pressed != MAXULONG && Hit == Pressed)
                    break;
                Pressed = MAXULONG;
            }
        }
CheckTimeout:
        if (!TimeOut)
            break;
        if (TimeOut > 0 &&
                 (Timer ? MachInputTimerExpired() : ArcGetTime()->Second != Second))
        {
            Second = ArcGetTime()->Second;
            --TimeOut;
            Timer = MachSetInputTimer(TimeOut > 0 ? 1000 : 0);
            Redraw = TRUE;
        }
        MachHwIdle();
    }
    MachSetInputTimer(0);
    if (Result && Selected)
        *Selected = Menu.SelectedMenuItem;
    return Result;
}

const UIVTBL FbGuiVtbl =
{
    BootUiInitialize,
    BootUiUnInitialize,
    BootUiDrawBackdrop,
    BootUiFillArea,
    BootUiDrawShadow,
    BootUiDrawBox,
    BootUiDrawText,
    BootUiDrawText2,
    BootUiDrawCenteredText,
    BootUiDrawStatusText,
    BootUiNoOperation,
    BootUiMessageBox,
    BootUiMessageBoxCritical,
    BootUiDrawProgressBarCenter,
    BootUiDrawProgressBar,
    BootUiSetProgressBarText,
    BootUiTickProgressBar,
    BootUiEditBox,
    BootUiTextToColor,
    BootUiTextToFillStyle,
    BootUiFadeInBackdrop,
    BootUiNoOperation,
    BootUiDisplayMenu,
    BootUiDrawMenu,
};
