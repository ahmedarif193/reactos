/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/condrv/vt.c
 * PURPOSE:         Virtual Terminal processing (phase 1 implementation)
 */

/* INCLUDES *******************************************************************/

#include <consrv.h>
#include "../conoutput.h"
#include "../coninput.h"

#include <limits.h>

NTSTATUS NTAPI ConDrvScrollConsoleScreenBuffer(PCONSOLE Console,
                                             PTEXTMODE_SCREEN_BUFFER Buffer,
                                             BOOLEAN Unicode,
                                             PSMALL_RECT ScrollRectangle,
                                             BOOLEAN UseClipRectangle,
                                             PSMALL_RECT ClipRectangle OPTIONAL,
                                             PCOORD DestinationOrigin,
                                             CHAR_INFO FillChar);

NTSTATUS NTAPI ConDrvWriteConsoleInput(PCONSOLE Console,
                                       PCONSOLE_INPUT_BUFFER InputBuffer,
                                       BOOLEAN AppendToEnd,
                                       PINPUT_RECORD InputRecord,
                                       ULONG NumEventsToWrite,
                                       PULONG NumEventsWritten OPTIONAL);

VOID NTAPI ConDrvVtInitializeBuffer(PTEXTMODE_SCREEN_BUFFER ScreenBuffer);

#define VT_MAX_PARAMS 16

#define FG_ATTR_MASK (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define BG_ATTR_MASK (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY)

#define VT_PRIVMODE_MOUSE_BUTTON_TRACKING   0x00000001 /* CSI ?1002 h/l */
#define VT_PRIVMODE_MOUSE_SGR_EXTENDED       0x00000002 /* CSI ?1006 h/l */
#define VT_PRIVMODE_BRACKETED_PASTE          0x00000004 /* CSI ?2004 h/l */
#define VT_PRIVMODE_META_SENDS_ESCAPE        0x00000008 /* CSI ?1036 h/l */
#define VT_PRIVMODE_ALTERNATE_SCREEN_BUFFER  0x00000010 /* CSI ?1049 h/l */

#define VT_CHARSET_SLOT_G0                   0
#define VT_CHARSET_SLOT_G1                   1
#define VT_CHARSET_SLOT_G2                   2
#define VT_CHARSET_SLOT_G3                   3
#define VT_CHARSET_SLOT_INVALID              0xFF

typedef enum _VT_CHARSET_ID
{
    VtCharsetAscii      = 0,
    VtCharsetDecSpecial = 1,
} VT_CHARSET_ID;

static const WCHAR VtDecSpecialMap[0x1F] =
{
    0x25C6, /* ` */
    0x2592, /* a */
    0x2409, /* b */
    0x240C, /* c */
    0x240D, /* d */
    0x240A, /* e */
    0x00B0, /* f */
    0x00B1, /* g */
    0x2424, /* h */
    0x240B, /* i */
    0x2518, /* j */
    0x2510, /* k */
    0x250C, /* l */
    0x2514, /* m */
    0x253C, /* n */
    0x23BA, /* o */
    0x23BB, /* p */
    0x2500, /* q */
    0x23BC, /* r */
    0x23BD, /* s */
    0x251C, /* t */
    0x2524, /* u */
    0x2534, /* v */
    0x252C, /* w */
    0x2502, /* x */
    0x2264, /* y */
    0x2265, /* z */
    0x03C0, /* { */
    0x2260, /* | */
    0x00A3, /* } */
    0x00B7, /* ~ */
};

static VT_CHARSET_ID
VtGetCharsetSlot(PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                 UCHAR Slot)
{
    switch (Slot)
    {
        case VT_CHARSET_SLOT_G0:
            return (VT_CHARSET_ID)ScreenBuffer->VtState.G0Charset;

        case VT_CHARSET_SLOT_G1:
            return (VT_CHARSET_ID)ScreenBuffer->VtState.G1Charset;

        case VT_CHARSET_SLOT_G2:
            return (VT_CHARSET_ID)ScreenBuffer->VtState.G2Charset;

        case VT_CHARSET_SLOT_G3:
            return (VT_CHARSET_ID)ScreenBuffer->VtState.G3Charset;

        default:
            return VtCharsetAscii;
    }
}

static VT_CHARSET_ID
VtGetActiveCharset(PTEXTMODE_SCREEN_BUFFER ScreenBuffer)
{
    return VtGetCharsetSlot(ScreenBuffer, ScreenBuffer->VtState.ActiveCharset);
}

static VOID
VtSetActiveCharset(PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                   UCHAR Slot)
{
    if (Slot > VT_CHARSET_SLOT_G3)
        Slot = VT_CHARSET_SLOT_G0;

    ScreenBuffer->VtState.ActiveCharset = Slot;
}

static BOOLEAN
VtDesignateCharset(PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                   UCHAR Slot,
                   WCHAR Designator)
{
    VT_CHARSET_ID Charset;

    switch (Designator)
    {
        case L'0':
            Charset = VtCharsetDecSpecial;
            break;

        case L'B':
        case L'U':
        case L'K':
        default:
            Charset = VtCharsetAscii;
            break;
    }

    switch (Slot)
    {
        case VT_CHARSET_SLOT_G0:
            ScreenBuffer->VtState.G0Charset = (UCHAR)Charset;
            return TRUE;

        case VT_CHARSET_SLOT_G1:
            ScreenBuffer->VtState.G1Charset = (UCHAR)Charset;
            return TRUE;

        case VT_CHARSET_SLOT_G2:
            ScreenBuffer->VtState.G2Charset = (UCHAR)Charset;
            return TRUE;

        case VT_CHARSET_SLOT_G3:
            ScreenBuffer->VtState.G3Charset = (UCHAR)Charset;
            return TRUE;

        default:
            return FALSE;
    }
}

static WCHAR
VtTranslateGlyphSlot(PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                     UCHAR Slot,
                     WCHAR Ch)
{
    if (Ch < 0x20 || Ch > 0x7E)
        return Ch;

    if (VtGetCharsetSlot(ScreenBuffer, Slot) != VtCharsetDecSpecial)
        return Ch;

    if (Ch >= 0x60 && Ch <= 0x7E)
    {
        WCHAR Mapped = VtDecSpecialMap[Ch - 0x60];
        return Mapped ? Mapped : Ch;
    }

    return Ch;
}

static VOID
VtSaveCursorState(PTEXTMODE_SCREEN_BUFFER ScreenBuffer)
{
    ScreenBuffer->VtState.SavedCursorPos = ScreenBuffer->CursorPosition;
    ScreenBuffer->VtState.SavedAttributes = ScreenBuffer->VtState.CurrentAttributes;
    ScreenBuffer->VtState.SavedFgColor = ScreenBuffer->VtState.CurrentFgColor;
    ScreenBuffer->VtState.SavedBgColor = ScreenBuffer->VtState.CurrentBgColor;
    ScreenBuffer->VtState.SavedUseRgbForeground = ScreenBuffer->VtState.UseRgbForeground;
    ScreenBuffer->VtState.SavedUseRgbBackground = ScreenBuffer->VtState.UseRgbBackground;
    ScreenBuffer->VtState.SavedG0Charset = ScreenBuffer->VtState.G0Charset;
    ScreenBuffer->VtState.SavedG1Charset = ScreenBuffer->VtState.G1Charset;
    ScreenBuffer->VtState.SavedG2Charset = ScreenBuffer->VtState.G2Charset;
    ScreenBuffer->VtState.SavedG3Charset = ScreenBuffer->VtState.G3Charset;
    ScreenBuffer->VtState.SavedActiveCharset = ScreenBuffer->VtState.ActiveCharset;
    ScreenBuffer->VtState.LastCharValid = FALSE;
    ScreenBuffer->VtState.LastWrittenChar = L' ';
    ScreenBuffer->VtState.CursorSaved = TRUE;
}

static VOID
VtRestoreCursorState(PCONSOLE Console,
                     PTEXTMODE_SCREEN_BUFFER ScreenBuffer)
{
    if (!ScreenBuffer->VtState.CursorSaved)
        return;

    ConDrvSetConsoleCursorPosition(Console,
                                   ScreenBuffer,
                                   &ScreenBuffer->VtState.SavedCursorPos);
    ScreenBuffer->VtState.CurrentAttributes = ScreenBuffer->VtState.SavedAttributes;
    ScreenBuffer->VtState.CurrentFgColor = ScreenBuffer->VtState.SavedFgColor;
    ScreenBuffer->VtState.CurrentBgColor = ScreenBuffer->VtState.SavedBgColor;
    ScreenBuffer->VtState.UseRgbForeground = ScreenBuffer->VtState.SavedUseRgbForeground;
    ScreenBuffer->VtState.UseRgbBackground = ScreenBuffer->VtState.SavedUseRgbBackground;
    ScreenBuffer->VtState.G0Charset = ScreenBuffer->VtState.SavedG0Charset;
    ScreenBuffer->VtState.G1Charset = ScreenBuffer->VtState.SavedG1Charset;
    ScreenBuffer->VtState.G2Charset = ScreenBuffer->VtState.SavedG2Charset;
    ScreenBuffer->VtState.G3Charset = ScreenBuffer->VtState.SavedG3Charset;
    VtSetActiveCharset(ScreenBuffer, ScreenBuffer->VtState.SavedActiveCharset);
    ScreenBuffer->VtState.PendingSingleShift = VT_CHARSET_SLOT_INVALID;
}

static BOOLEAN
VtEnableAlternateScreen(PCONSOLE Console,
                        PTEXTMODE_SCREEN_BUFFER PrimaryBuffer)
{
    TEXTMODE_BUFFER_INFO AltInfo;
    PCONSOLE_SCREEN_BUFFER NewBuffer = NULL;
    PTEXTMODE_SCREEN_BUFFER AltBuffer;
    NTSTATUS Status;

    if (PrimaryBuffer->VtState.AlternateBuffer != NULL)
    {
        /* Already active, ensure the console is pointing at it. */
        if (Console->ActiveBuffer != (PCONSOLE_SCREEN_BUFFER)PrimaryBuffer->VtState.AlternateBuffer)
        {
            ConDrvSetConsoleActiveScreenBuffer(Console,
                                               (PCONSOLE_SCREEN_BUFFER)PrimaryBuffer->VtState.AlternateBuffer);
        }
        return TRUE;
    }

    PrimaryBuffer->VtState.PrimaryCursorInfo = PrimaryBuffer->CursorInfo;
    PrimaryBuffer->VtState.PrimaryCursorPos = PrimaryBuffer->CursorPosition;
    PrimaryBuffer->VtState.PrimaryViewOrigin = PrimaryBuffer->ViewOrigin;
    PrimaryBuffer->VtState.PrimaryVirtualY = PrimaryBuffer->VirtualY;
    AltInfo.ScreenBufferSize = PrimaryBuffer->ViewSize;
    AltInfo.ScreenBufferSize.X = max(AltInfo.ScreenBufferSize.X, 1);
    AltInfo.ScreenBufferSize.Y = max(AltInfo.ScreenBufferSize.Y, 1);
    AltInfo.ViewSize         = AltInfo.ScreenBufferSize;
    AltInfo.ScreenAttrib     = PrimaryBuffer->ScreenDefaultAttrib;
    AltInfo.PopupAttrib      = PrimaryBuffer->PopupDefaultAttrib;
    AltInfo.CursorSize       = PrimaryBuffer->CursorInfo.dwSize;
    AltInfo.IsCursorVisible  = PrimaryBuffer->CursorInfo.bVisible;

    Status = ConDrvCreateScreenBuffer(&NewBuffer,
                                      Console,
                                      NULL,
                                      CONSOLE_TEXTMODE_BUFFER,
                                      &AltInfo);
    if (!NT_SUCCESS(Status) || NewBuffer == NULL)
    {
        PrimaryBuffer->VtState.PrimaryBuffer = NULL;
        return FALSE;
    }

    AltBuffer = (PTEXTMODE_SCREEN_BUFFER)NewBuffer;
    AltBuffer->VtState.PrimaryBuffer = PrimaryBuffer;
    AltBuffer->VtState.PrivateModes = PrimaryBuffer->VtState.PrivateModes | VT_PRIVMODE_ALTERNATE_SCREEN_BUFFER;
    AltBuffer->VtState.Active = PrimaryBuffer->VtState.Active;
    AltBuffer->VtState.CurrentAttributes = PrimaryBuffer->VtState.CurrentAttributes;
    AltBuffer->VtState.SavedAttributes = PrimaryBuffer->VtState.SavedAttributes;
    AltBuffer->VtState.UseRgbForeground = PrimaryBuffer->VtState.UseRgbForeground;
    AltBuffer->VtState.UseRgbBackground = PrimaryBuffer->VtState.UseRgbBackground;
    AltBuffer->VtState.CurrentFgColor = PrimaryBuffer->VtState.CurrentFgColor;
    AltBuffer->VtState.CurrentBgColor = PrimaryBuffer->VtState.CurrentBgColor;
    AltBuffer->VtState.SavedFgColor = PrimaryBuffer->VtState.SavedFgColor;
    AltBuffer->VtState.SavedBgColor = PrimaryBuffer->VtState.SavedBgColor;
    AltBuffer->VtState.SavedUseRgbForeground = PrimaryBuffer->VtState.SavedUseRgbForeground;
    AltBuffer->VtState.SavedUseRgbBackground = PrimaryBuffer->VtState.SavedUseRgbBackground;
    AltBuffer->VtState.G0Charset = PrimaryBuffer->VtState.G0Charset;
    AltBuffer->VtState.G1Charset = PrimaryBuffer->VtState.G1Charset;
    AltBuffer->VtState.G2Charset = PrimaryBuffer->VtState.G2Charset;
    AltBuffer->VtState.G3Charset = PrimaryBuffer->VtState.G3Charset;
    AltBuffer->VtState.ActiveCharset = PrimaryBuffer->VtState.ActiveCharset;
    AltBuffer->VtState.SavedG0Charset = PrimaryBuffer->VtState.SavedG0Charset;
    AltBuffer->VtState.SavedG1Charset = PrimaryBuffer->VtState.SavedG1Charset;
    AltBuffer->VtState.SavedG2Charset = PrimaryBuffer->VtState.SavedG2Charset;
    AltBuffer->VtState.SavedG3Charset = PrimaryBuffer->VtState.SavedG3Charset;
    AltBuffer->VtState.SavedActiveCharset = PrimaryBuffer->VtState.SavedActiveCharset;
    AltBuffer->VtState.PendingSingleShift = PrimaryBuffer->VtState.PendingSingleShift;
    AltBuffer->VtState.DefaultCursorInfo = PrimaryBuffer->VtState.DefaultCursorInfo;
    AltBuffer->VtState.CurrentCursorStyle = PrimaryBuffer->VtState.CurrentCursorStyle;
    AltBuffer->VtState.ScrollTop = min(PrimaryBuffer->VtState.ScrollTop,
                                       (SHORT)max(0, AltBuffer->ScreenBufferSize.Y - 1));
    AltBuffer->VtState.HyperlinkActive = PrimaryBuffer->VtState.HyperlinkActive;
    AltBuffer->VtState.HyperlinkUri.Buffer = NULL;
    AltBuffer->VtState.HyperlinkUri.Length = 0;
    AltBuffer->VtState.HyperlinkUri.MaximumLength = 0;
    if (PrimaryBuffer->VtState.HyperlinkActive &&
        PrimaryBuffer->VtState.HyperlinkUri.Buffer &&
        PrimaryBuffer->VtState.HyperlinkUri.Length > 0)
    {
        ULONG Bytes = PrimaryBuffer->VtState.HyperlinkUri.Length + sizeof(WCHAR);
        PWCHAR Copy = ConsoleAllocHeap(0, Bytes);
        if (Copy)
        {
            RtlCopyMemory(Copy, PrimaryBuffer->VtState.HyperlinkUri.Buffer,
                          PrimaryBuffer->VtState.HyperlinkUri.Length);
            Copy[PrimaryBuffer->VtState.HyperlinkUri.Length / sizeof(WCHAR)] = UNICODE_NULL;
            AltBuffer->VtState.HyperlinkUri.Buffer = Copy;
            AltBuffer->VtState.HyperlinkUri.Length = PrimaryBuffer->VtState.HyperlinkUri.Length;
            AltBuffer->VtState.HyperlinkUri.MaximumLength = (USHORT)Bytes;
        }
        else
        {
            AltBuffer->VtState.HyperlinkActive = FALSE;
        }
    }

    AltBuffer->VtState.ScrollBottom = min(PrimaryBuffer->VtState.ScrollBottom,
                                          (SHORT)max(0, AltBuffer->ScreenBufferSize.Y - 1));
    if (AltBuffer->VtState.ScrollBottom < AltBuffer->VtState.ScrollTop)
    {
        AltBuffer->VtState.ScrollTop = 0;
        AltBuffer->VtState.ScrollBottom = max(0, AltBuffer->ScreenBufferSize.Y - 1);
    }

    PrimaryBuffer->VtState.AlternateBuffer = AltBuffer;
    PrimaryBuffer->VtState.PrivateModes |= VT_PRIVMODE_ALTERNATE_SCREEN_BUFFER;

    ConDrvSetConsoleActiveScreenBuffer(Console, NewBuffer);
    ConDrvSetConsoleCursorInfo(Console, AltBuffer, &AltBuffer->CursorInfo);
    return TRUE;
}

static BOOLEAN
VtDisableAlternateScreen(PCONSOLE Console,
                         PTEXTMODE_SCREEN_BUFFER ActiveBuffer)
{
    PTEXTMODE_SCREEN_BUFFER PrimaryBuffer;

    if (ActiveBuffer->VtState.PrimaryBuffer == NULL)
    {
        /* No alternate screen in use. */
        if (ActiveBuffer->VtState.AlternateBuffer != NULL)
        {
            PTEXTMODE_SCREEN_BUFFER AltBuffer = ActiveBuffer->VtState.AlternateBuffer;
            ActiveBuffer->VtState.AlternateBuffer = NULL;
            AltBuffer->VtState.PrimaryBuffer = NULL;
            ActiveBuffer->VtState.PrivateModes &= ~VT_PRIVMODE_ALTERNATE_SCREEN_BUFFER;
            ConDrvDeleteScreenBuffer((PCONSOLE_SCREEN_BUFFER)AltBuffer);
        }
        return TRUE;
    }

    PrimaryBuffer = ActiveBuffer->VtState.PrimaryBuffer;

    /* Restore the saved attributes on the primary buffer before switching back. */
    PrimaryBuffer->CursorInfo = PrimaryBuffer->VtState.PrimaryCursorInfo;
    PrimaryBuffer->CursorPosition = PrimaryBuffer->VtState.PrimaryCursorPos;
    PrimaryBuffer->ViewOrigin = PrimaryBuffer->VtState.PrimaryViewOrigin;
    PrimaryBuffer->VirtualY = PrimaryBuffer->VtState.PrimaryVirtualY;

    PrimaryBuffer->VtState.PrivateModes &= ~VT_PRIVMODE_ALTERNATE_SCREEN_BUFFER;
    PrimaryBuffer->VtState.AlternateBuffer = NULL;

    ActiveBuffer->VtState.PrimaryBuffer = NULL;
    ActiveBuffer->VtState.PrivateModes &= ~VT_PRIVMODE_ALTERNATE_SCREEN_BUFFER;

    ConDrvVtInvalidateBufferRgb(ActiveBuffer);

    ConDrvSetConsoleActiveScreenBuffer(Console, (PCONSOLE_SCREEN_BUFFER)PrimaryBuffer);
    ConDrvSetConsoleCursorInfo(Console, PrimaryBuffer, &PrimaryBuffer->CursorInfo);
    ConDrvSetConsoleCursorPosition(Console, PrimaryBuffer, &PrimaryBuffer->CursorPosition);

    return TRUE;
}

static COLORREF
VtGetPaletteColor(PCONSOLE Console, UCHAR Index)
{
    PCONSRV_CONSOLE Cons = (PCONSRV_CONSOLE)Console;
    if (!Cons) return 0;
    return Cons->Colors[Index & 0x0F];
}

static VOID
VtClearHyperlink(PTEXTMODE_SCREEN_BUFFER ScreenBuffer)
{
    if (!ScreenBuffer)
        return;

    if (ScreenBuffer->VtState.HyperlinkUri.Buffer)
    {
        ConsoleFreeHeap(ScreenBuffer->VtState.HyperlinkUri.Buffer);
        ScreenBuffer->VtState.HyperlinkUri.Buffer = NULL;
    }

    ScreenBuffer->VtState.HyperlinkUri.Length = 0;
    ScreenBuffer->VtState.HyperlinkUri.MaximumLength = 0;
    ScreenBuffer->VtState.HyperlinkActive = FALSE;
}

VOID
NTAPI
ConDrvVtInvalidateBufferRgb(PTEXTMODE_SCREEN_BUFFER ScreenBuffer)
{
    SIZE_T CellCount;
    PCONSOLE Console;

    if (!ScreenBuffer)
        return;

    CellCount = (SIZE_T)ScreenBuffer->ScreenBufferSize.X * ScreenBuffer->ScreenBufferSize.Y;

    if (ScreenBuffer->FgColors)
        RtlFillMemory(ScreenBuffer->FgColors, CellCount * sizeof(COLORREF), 0xFF);
    if (ScreenBuffer->BgColors)
        RtlFillMemory(ScreenBuffer->BgColors, CellCount * sizeof(COLORREF), 0xFF);

    ScreenBuffer->VtState.Active = FALSE;
    ScreenBuffer->VtState.CursorSaved = FALSE;
    ScreenBuffer->VtState.UseRgbForeground = FALSE;
    ScreenBuffer->VtState.UseRgbBackground = FALSE;
    ScreenBuffer->VtState.SavedUseRgbForeground = FALSE;
    ScreenBuffer->VtState.SavedUseRgbBackground = FALSE;
    ScreenBuffer->VtState.PrivateModes = 0;
    ScreenBuffer->VtState.ScrollTop = 0;
    ScreenBuffer->VtState.ScrollBottom = max(0, ScreenBuffer->ScreenBufferSize.Y - 1);
    VtClearHyperlink(ScreenBuffer);

    Console = ScreenBuffer->Header.Console;
    if (Console)
    {
        COLORREF fg = VtGetPaletteColor(Console, ScreenBuffer->VtState.CurrentAttributes & 0x0F);
        COLORREF bg = VtGetPaletteColor(Console, (ScreenBuffer->VtState.CurrentAttributes >> 4) & 0x0F);
        ScreenBuffer->VtState.CurrentFgColor = fg;
        ScreenBuffer->VtState.CurrentBgColor = bg;
        ScreenBuffer->VtState.SavedFgColor = fg;
        ScreenBuffer->VtState.SavedBgColor = bg;
    }
}

static UCHAR
VtFindNearestPaletteIndex(PCONSOLE Console, COLORREF Color)
{
    PCONSRV_CONSOLE Cons = (PCONSRV_CONSOLE)Console;
    UCHAR best = 0;
    ULONG bestDiff = ULONG_MAX;
    if (!Cons) return 0;
    for (UCHAR idx = 0; idx < 16; ++idx)
    {
        COLORREF ref = Cons->Colors[idx];
        LONG dr = (LONG)GetRValue(Color) - (LONG)GetRValue(ref);
        LONG dg = (LONG)GetGValue(Color) - (LONG)GetGValue(ref);
        LONG db = (LONG)GetBValue(Color) - (LONG)GetBValue(ref);
        ULONG diff = (ULONG)(dr * dr + dg * dg + db * db);
        if (diff < bestDiff)
        {
            bestDiff = diff;
            best = idx;
        }
    }
    return best;
}

static COLORREF
VtColorFromXtermIndex(ULONG Index)
{
    if (Index < 16)
        return RGB(((Index & 1) ? 0x80 : 0x00) + ((Index & 8) ? 0x80 : 0x00),
                   ((Index & 2) ? 0x80 : 0x00) + ((Index & 8) ? 0x80 : 0x00),
                   ((Index & 4) ? 0x80 : 0x00) + ((Index & 8) ? 0x80 : 0x00));

    if (Index >= 16 && Index <= 231)
    {
        Index -= 16;
        ULONG r = (Index / 36) % 6;
        ULONG g = (Index / 6) % 6;
        ULONG b = Index % 6;
        r = r ? r * 40 + 55 : 0;
        g = g ? g * 40 + 55 : 0;
        b = b ? b * 40 + 55 : 0;
        return RGB((BYTE)r, (BYTE)g, (BYTE)b);
    }

    if (Index >= 232 && Index <= 255)
    {
        BYTE shade = (BYTE)((Index - 232) * 10 + 8);
        return RGB(shade, shade, shade);
    }

    return 0;
}

static SIZE_T
VtAppendNumber(WCHAR *Buffer, SIZE_T BufferLength, ULONG Number)
{
    WCHAR Temp[12];
    SIZE_T Count = 0;
    SIZE_T Written = 0;

    if (BufferLength == 0)
        return 0;

    do
    {
        Temp[Count++] = (WCHAR)(L'0' + (Number % 10));
        Number /= 10;
    } while (Number != 0 && Count < ARRAYSIZE(Temp));

    while (Count > 0 && Written < BufferLength)
    {
        Buffer[Written++] = Temp[--Count];
    }

    return Written;
}

static VOID
VtSendInputResponse(PCONSOLE Console,
                    const WCHAR *Response,
                    SIZE_T Length)
{
    PINPUT_RECORD Records;
    SIZE_T Index;
    ULONG Total;

    if (Console == NULL || Response == NULL || Length == 0)
        return;

    Total = (ULONG)(Length * 2);
    Records = ConsoleAllocHeap(0, Total * sizeof(INPUT_RECORD));
    if (!Records)
        return;

    for (Index = 0; Index < Length; ++Index)
    {
        PINPUT_RECORD Down = &Records[Index * 2];
        PINPUT_RECORD Up = &Records[Index * 2 + 1];

        RtlZeroMemory(Down, sizeof(INPUT_RECORD));
        Down->EventType = KEY_EVENT;
        Down->Event.KeyEvent.bKeyDown = TRUE;
        Down->Event.KeyEvent.wRepeatCount = 1;
        Down->Event.KeyEvent.uChar.UnicodeChar = Response[Index];

        *Up = *Down;
        Up->Event.KeyEvent.bKeyDown = FALSE;
    }

    ConDrvWriteConsoleInput(Console,
                            &Console->InputBuffer,
                            TRUE,
                            Records,
                            Total,
                            NULL);

    ConsoleFreeHeap(Records);
}

static SIZE_T
VtAppendHex(WCHAR *Buffer, SIZE_T BufferLength, ULONG Value, ULONG Digits)
{
    SIZE_T Written = 0;
    LONG Shift;

    if (Digits == 0 || BufferLength == 0)
        return 0;

    for (Shift = (LONG)(Digits - 1); Shift >= 0 && Written < BufferLength; --Shift)
    {
        ULONG Nibble = (Value >> (Shift * 4)) & 0xF;
        Buffer[Written++] = (WCHAR)(Nibble < 10 ? (L'0' + Nibble) : (L'A' + (Nibble - 10)));
    }

    return Written;
}

static SIZE_T
VtAppendOscRgb(WCHAR *Buffer, SIZE_T BufferLength, COLORREF Color)
{
    SIZE_T Written = 0;
    ULONG Components[3];
    ULONG Index;

    if (BufferLength < 4)
        return 0;

    Buffer[Written++] = L'r';
    Buffer[Written++] = L'g';
    Buffer[Written++] = L'b';
    Buffer[Written++] = L':';

    Components[0] = (ULONG)GetRValue(Color) * 257u;
    Components[1] = (ULONG)GetGValue(Color) * 257u;
    Components[2] = (ULONG)GetBValue(Color) * 257u;

    for (Index = 0; Index < ARRAYSIZE(Components) && Written < BufferLength; ++Index)
    {
        Written += VtAppendHex(Buffer + Written,
                               BufferLength - Written,
                               Components[Index],
                               4);

        if (Index + 1 < ARRAYSIZE(Components) && Written < BufferLength)
            Buffer[Written++] = L'/';
    }

    return Written;
}

static VOID
VtSendOscColorResponse(PCONSOLE Console,
                       ULONG Parameter,
                       COLORREF Color)
{
    WCHAR Response[64];
    SIZE_T Len = 0;

    Response[Len++] = L'\x1b';
    Response[Len++] = L']';
    Len += VtAppendNumber(Response + Len, ARRAYSIZE(Response) - Len, Parameter);
    if (Len < ARRAYSIZE(Response))
        Response[Len++] = L';';
    Len += VtAppendOscRgb(Response + Len, ARRAYSIZE(Response) - Len, Color);
    if (Len < ARRAYSIZE(Response))
        Response[Len++] = L'\x07';

    VtSendInputResponse(Console, Response, Len);
}

static VOID
VtApplyRgbToRegion(PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                  SHORT Left,
                  SHORT Top,
                  SHORT Right,
                  SHORT Bottom)
{
    SHORT Width = ScreenBuffer->ScreenBufferSize.X;
    SHORT Height = ScreenBuffer->ScreenBufferSize.Y;
    SHORT x, y;
    COLORREF Fg = ScreenBuffer->VtState.UseRgbForeground ? ScreenBuffer->VtState.CurrentFgColor : CLR_INVALID;
    COLORREF Bg = ScreenBuffer->VtState.UseRgbBackground ? ScreenBuffer->VtState.CurrentBgColor : CLR_INVALID;

    if (Width <= 0 || Height <= 0)
        return;

    Left = max(Left, 0);
    Top = max(Top, 0);
    Right = min(Right, (SHORT)(Width - 1));
    Bottom = min(Bottom, (SHORT)(Height - 1));

    if (Left > Right || Top > Bottom)
        return;

    for (y = Top; y <= Bottom; ++y)
    {
        for (x = Left; x <= Right; ++x)
        {
            ConioSetCellFgColor(ScreenBuffer, x, y, Fg);
            ConioSetCellBgColor(ScreenBuffer, x, y, Bg);
        }
    }
}

static VOID
VtFillRectWithCurrentAttr(PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                          SHORT Left,
                          SHORT Top,
                          SHORT Right,
                          SHORT Bottom)
{
    SHORT Width = ScreenBuffer->ScreenBufferSize.X;
    SHORT Height = ScreenBuffer->ScreenBufferSize.Y;
    SHORT X, Y;
    USHORT Attr = ScreenBuffer->VtState.CurrentAttributes;
    COLORREF Fg = ScreenBuffer->VtState.UseRgbForeground ? ScreenBuffer->VtState.CurrentFgColor : CLR_INVALID;
    COLORREF Bg = ScreenBuffer->VtState.UseRgbBackground ? ScreenBuffer->VtState.CurrentBgColor : CLR_INVALID;

    if (Width <= 0 || Height <= 0)
        return;

    Left = max(Left, 0);
    Top = max(Top, 0);
    Right = min(Right, (SHORT)(Width - 1));
    Bottom = min(Bottom, (SHORT)(Height - 1));

    if (Left > Right || Top > Bottom)
        return;

    for (Y = Top; Y <= Bottom; ++Y)
    {
        PCHAR_INFO Ptr = ConioCoordToPointer(ScreenBuffer, Left, Y);
        for (X = Left; X <= Right; ++X, ++Ptr)
        {
            Ptr->Char.UnicodeChar = L' ';
            Ptr->Attributes = Attr;
            ConioSetCellFgColor(ScreenBuffer, X, Y, Fg);
            ConioSetCellBgColor(ScreenBuffer, X, Y, Bg);
        }
    }
}

static VOID
VtInsertCharacters(PCONSOLE Console,
                   PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                   ULONG Count)
{
    SHORT Width = ScreenBuffer->ScreenBufferSize.X;
    SHORT Y = ScreenBuffer->CursorPosition.Y;
    SHORT X = ScreenBuffer->CursorPosition.X;
    ULONG Available;
    ULONG CellsToMove;
    ULONG RowIndex;
    SHORT FillCount;

    if (Count == 0)
        Count = 1;

    if (Width <= 0)
        return;

    if (Y < 0 || Y >= ScreenBuffer->ScreenBufferSize.Y)
        return;

    if (X < 0 || X >= Width)
        return;

    Available = (ULONG)(Width - X);
    if (Available == 0)
        return;

    if (Count > Available)
        Count = Available;

    FillCount = (SHORT)Count;

    CellsToMove = Available - Count;
    if (CellsToMove > 0)
    {
        PCHAR_INFO Row = ConioCoordToPointer(ScreenBuffer, 0, Y);
        RowIndex = ConioCoordToIndex(ScreenBuffer, 0, Y);
        RtlMoveMemory(Row + X + Count,
                      Row + X,
                      CellsToMove * sizeof(CHAR_INFO));

        if (ScreenBuffer->FgColors)
        {
            RtlMoveMemory(ScreenBuffer->FgColors + RowIndex + X + Count,
                          ScreenBuffer->FgColors + RowIndex + X,
                          CellsToMove * sizeof(COLORREF));
        }

        if (ScreenBuffer->BgColors)
        {
            RtlMoveMemory(ScreenBuffer->BgColors + RowIndex + X + Count,
                          ScreenBuffer->BgColors + RowIndex + X,
                          CellsToMove * sizeof(COLORREF));
        }
    }

    VtFillRectWithCurrentAttr(ScreenBuffer,
                              X,
                              Y,
                              (SHORT)(X + FillCount - 1),
                              Y);

    {
        SMALL_RECT Region;
        Region.Left = X;
        Region.Top = Y;
        Region.Right = Width > 0 ? Width - 1 : 0;
        Region.Bottom = Y;
        TermDrawRegion(Console, &Region);
    }
}

static VOID
VtDeleteCharacters(PCONSOLE Console,
                   PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                   ULONG Count)
{
    SHORT Width = ScreenBuffer->ScreenBufferSize.X;
    SHORT Y = ScreenBuffer->CursorPosition.Y;
    SHORT X = ScreenBuffer->CursorPosition.X;
    ULONG Available;
    ULONG CellsToMove;
    ULONG RowIndex;
    SHORT TailStart;
    SHORT DeleteCount;

    if (Count == 0)
        Count = 1;

    if (Width <= 0)
        return;

    if (Y < 0 || Y >= ScreenBuffer->ScreenBufferSize.Y)
        return;

    if (X < 0 || X >= Width)
        return;

    Available = (ULONG)(Width - X);
    if (Available == 0)
        return;

    if (Count > Available)
        Count = Available;

    DeleteCount = (SHORT)Count;

    CellsToMove = Available - Count;
    if (CellsToMove > 0)
    {
        PCHAR_INFO Row = ConioCoordToPointer(ScreenBuffer, 0, Y);
        RowIndex = ConioCoordToIndex(ScreenBuffer, 0, Y);
        RtlMoveMemory(Row + X,
                      Row + X + Count,
                      CellsToMove * sizeof(CHAR_INFO));

        if (ScreenBuffer->FgColors)
        {
            RtlMoveMemory(ScreenBuffer->FgColors + RowIndex + X,
                          ScreenBuffer->FgColors + RowIndex + X + Count,
                          CellsToMove * sizeof(COLORREF));
        }

        if (ScreenBuffer->BgColors)
        {
            RtlMoveMemory(ScreenBuffer->BgColors + RowIndex + X,
                          ScreenBuffer->BgColors + RowIndex + X + Count,
                          CellsToMove * sizeof(COLORREF));
        }
    }

    TailStart = (SHORT)(Width - DeleteCount);
    VtFillRectWithCurrentAttr(ScreenBuffer,
                              TailStart,
                              Y,
                              Width > 0 ? Width - 1 : 0,
                              Y);

    {
        SMALL_RECT Region;
        Region.Left = X;
        Region.Top = Y;
        Region.Right = Width > 0 ? Width - 1 : 0;
        Region.Bottom = Y;
        TermDrawRegion(Console, &Region);
    }
}

static VOID
VtEraseCharacters(PCONSOLE Console,
                  PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                  ULONG Count)
{
    SHORT Width = ScreenBuffer->ScreenBufferSize.X;
    SHORT Y = ScreenBuffer->CursorPosition.Y;
    SHORT X = ScreenBuffer->CursorPosition.X;

    if (Count == 0)
        Count = 1;

    if (Width <= 0)
        return;

    if (Y < 0 || Y >= ScreenBuffer->ScreenBufferSize.Y)
        return;

    if (X < 0 || X >= Width)
        return;

    if ((ULONG)(Width - X) < Count)
        Count = (ULONG)(Width - X);

    if (Count == 0)
        return;

    {
        SHORT EraseCount = (SHORT)Count;
        VtFillRectWithCurrentAttr(ScreenBuffer,
                                  X,
                                  Y,
                                  (SHORT)(X + EraseCount - 1),
                                  Y);

        {
            SMALL_RECT Region;
            Region.Left = X;
            Region.Top = Y;
            Region.Right = (SHORT)(X + EraseCount - 1);
            Region.Bottom = Y;
            TermDrawRegion(Console, &Region);
        }
    }
}

static VOID
VtRepeatCharacter(PCONSOLE Console,
                  PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                  ULONG Count)
{
    WCHAR Chunk[64];
    ULONG Remaining;

    if (!Console || !ScreenBuffer)
        return;

    if (!ScreenBuffer->VtState.LastCharValid)
        return;

    if (Count == 0)
        Count = 1;

    Remaining = Count;

    while (Remaining > 0)
    {
        ULONG Emit = (Remaining > ARRAYSIZE(Chunk)) ? ARRAYSIZE(Chunk) : Remaining;
        ULONG i;

        for (i = 0; i < Emit; ++i)
            Chunk[i] = ScreenBuffer->VtState.LastWrittenChar;

        if (!NT_SUCCESS(VtFlushText(Console, ScreenBuffer, Chunk, Emit)))
            break;

        Remaining -= Emit;
    }
}

static VOID
VtInsertLines(PCONSOLE Console,
              PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
              ULONG Count)
{
    SHORT Width = ScreenBuffer->ScreenBufferSize.X;
    SHORT CursorY = ScreenBuffer->CursorPosition.Y;
    SHORT Top = ScreenBuffer->VtState.ScrollTop;
    SHORT Bottom = ScreenBuffer->VtState.ScrollBottom;
    SHORT LinesAvailable;
    SHORT Line;

    if (Count == 0)
        Count = 1;

    if (Width <= 0)
        return;

    if (Top < 0)
        Top = 0;
    if (Bottom >= ScreenBuffer->ScreenBufferSize.Y)
        Bottom = ScreenBuffer->ScreenBufferSize.Y - 1;

    if (CursorY < Top || CursorY > Bottom)
        return;

    LinesAvailable = Bottom - CursorY + 1;
    if (LinesAvailable <= 0)
        return;

    if ((ULONG)LinesAvailable < Count)
        Count = LinesAvailable;

    {
        SHORT InsertCount = (SHORT)Count;

        if (InsertCount <= 0)
            return;

        if (InsertCount < LinesAvailable)
        {
            for (Line = Bottom - InsertCount; Line >= CursorY; --Line)
            {
                PCHAR_INFO Src = ConioCoordToPointer(ScreenBuffer, 0, Line);
                PCHAR_INFO Dst = ConioCoordToPointer(ScreenBuffer, 0, Line + InsertCount);
                RtlMoveMemory(Dst, Src, Width * sizeof(CHAR_INFO));

                if (ScreenBuffer->FgColors)
                {
                    ULONG SrcIndex = ConioCoordToIndex(ScreenBuffer, 0, Line);
                    ULONG DstIndex = ConioCoordToIndex(ScreenBuffer, 0, Line + InsertCount);
                    RtlMoveMemory(ScreenBuffer->FgColors + DstIndex,
                                  ScreenBuffer->FgColors + SrcIndex,
                                  Width * sizeof(COLORREF));
                }

                if (ScreenBuffer->BgColors)
                {
                    ULONG SrcIndex = ConioCoordToIndex(ScreenBuffer, 0, Line);
                    ULONG DstIndex = ConioCoordToIndex(ScreenBuffer, 0, Line + InsertCount);
                    RtlMoveMemory(ScreenBuffer->BgColors + DstIndex,
                                  ScreenBuffer->BgColors + SrcIndex,
                                  Width * sizeof(COLORREF));
                }
            }
        }

        VtFillRectWithCurrentAttr(ScreenBuffer,
                                  0,
                                  CursorY,
                                  Width > 0 ? Width - 1 : 0,
                                  (SHORT)(CursorY + InsertCount - 1));

        {
            SMALL_RECT Region;
            Region.Left = 0;
            Region.Right = Width > 0 ? Width - 1 : 0;
            Region.Top = CursorY;
            Region.Bottom = Bottom;
            TermDrawRegion(Console, &Region);
        }
    }
}

static VOID
VtDeleteLines(PCONSOLE Console,
              PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
              ULONG Count)
{
    SHORT Width = ScreenBuffer->ScreenBufferSize.X;
    SHORT CursorY = ScreenBuffer->CursorPosition.Y;
    SHORT Top = ScreenBuffer->VtState.ScrollTop;
    SHORT Bottom = ScreenBuffer->VtState.ScrollBottom;
    SHORT LinesAvailable;
    SHORT Line;

    if (Count == 0)
        Count = 1;

    if (Width <= 0)
        return;

    if (Top < 0)
        Top = 0;
    if (Bottom >= ScreenBuffer->ScreenBufferSize.Y)
        Bottom = ScreenBuffer->ScreenBufferSize.Y - 1;

    if (CursorY < Top || CursorY > Bottom)
        return;

    LinesAvailable = Bottom - CursorY + 1;
    if (LinesAvailable <= 0)
        return;

    if ((ULONG)LinesAvailable < Count)
        Count = LinesAvailable;

    {
        SHORT DeleteCount = (SHORT)Count;

        if (DeleteCount <= 0)
            return;

        if (DeleteCount < LinesAvailable)
        {
            for (Line = CursorY; Line <= Bottom - DeleteCount; ++Line)
            {
                PCHAR_INFO Src = ConioCoordToPointer(ScreenBuffer, 0, Line + DeleteCount);
                PCHAR_INFO Dst = ConioCoordToPointer(ScreenBuffer, 0, Line);
                RtlMoveMemory(Dst, Src, Width * sizeof(CHAR_INFO));

                if (ScreenBuffer->FgColors)
                {
                    ULONG SrcIndex = ConioCoordToIndex(ScreenBuffer, 0, Line + DeleteCount);
                    ULONG DstIndex = ConioCoordToIndex(ScreenBuffer, 0, Line);
                    RtlMoveMemory(ScreenBuffer->FgColors + DstIndex,
                                  ScreenBuffer->FgColors + SrcIndex,
                                  Width * sizeof(COLORREF));
                }

                if (ScreenBuffer->BgColors)
                {
                    ULONG SrcIndex = ConioCoordToIndex(ScreenBuffer, 0, Line + DeleteCount);
                    ULONG DstIndex = ConioCoordToIndex(ScreenBuffer, 0, Line);
                    RtlMoveMemory(ScreenBuffer->BgColors + DstIndex,
                                  ScreenBuffer->BgColors + SrcIndex,
                                  Width * sizeof(COLORREF));
                }
            }
        }

        VtFillRectWithCurrentAttr(ScreenBuffer,
                                  0,
                                  (SHORT)(Bottom - DeleteCount + 1),
                                  Width > 0 ? Width - 1 : 0,
                                  Bottom);

        {
            SMALL_RECT Region;
            Region.Left = 0;
            Region.Right = Width > 0 ? Width - 1 : 0;
            Region.Top = CursorY;
            Region.Bottom = Bottom;
            TermDrawRegion(Console, &Region);
        }
    }
}

static VOID
VtScrollRegionUp(PCONSOLE Console,
                 PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                 ULONG Lines)
{
    SHORT Top = ScreenBuffer->VtState.ScrollTop;
    SHORT Bottom = ScreenBuffer->VtState.ScrollBottom;
    SHORT Height = Bottom - Top + 1;
    SMALL_RECT ScrollRect;
    COORD DestOrigin;
    CHAR_INFO FillChar;

    if (Height <= 0)
        return;

    if (Lines == 0)
        Lines = 1;
    if (Lines > (ULONG)Height)
        Lines = Height;

    ScrollRect.Left = 0;
    ScrollRect.Top = Top;
    ScrollRect.Right = ScreenBuffer->ScreenBufferSize.X - 1;
    ScrollRect.Bottom = Bottom;

    DestOrigin.X = 0;
    DestOrigin.Y = (SHORT)(Top - (SHORT)Lines);

    FillChar.Char.UnicodeChar = L' ';
    FillChar.Attributes = ScreenBuffer->VtState.CurrentAttributes;

    if (!NT_SUCCESS(ConDrvScrollConsoleScreenBuffer(Console,
                                                    ScreenBuffer,
                                                    TRUE,
                                                    &ScrollRect,
                                                    FALSE,
                                                    NULL,
                                                    &DestOrigin,
                                                    FillChar)))
    {
        return;
    }

    VtApplyRgbToRegion(ScreenBuffer,
                       0,
                       Bottom - (SHORT)Lines + 1,
                       ScreenBuffer->ScreenBufferSize.X - 1,
                       Bottom);
}

static VOID
VtScrollRegionDown(PCONSOLE Console,
                 PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                 ULONG Lines)
{
    SHORT Top = ScreenBuffer->VtState.ScrollTop;
    SHORT Bottom = ScreenBuffer->VtState.ScrollBottom;
    SHORT Height = Bottom - Top + 1;
    SMALL_RECT ScrollRect;
    COORD DestOrigin;
    CHAR_INFO FillChar;

    if (Height <= 0)
        return;

    if (Lines == 0)
        Lines = 1;
    if (Lines > (ULONG)Height)
        Lines = Height;

    ScrollRect.Left = 0;
    ScrollRect.Top = Top;
    ScrollRect.Right = ScreenBuffer->ScreenBufferSize.X - 1;
    ScrollRect.Bottom = Bottom;

    DestOrigin.X = 0;
    DestOrigin.Y = (SHORT)(Top + (SHORT)Lines);

    FillChar.Char.UnicodeChar = L' ';
    FillChar.Attributes = ScreenBuffer->VtState.CurrentAttributes;

    if (!NT_SUCCESS(ConDrvScrollConsoleScreenBuffer(Console,
                                                    ScreenBuffer,
                                                    TRUE,
                                                    &ScrollRect,
                                                    FALSE,
                                                    NULL,
                                                    &DestOrigin,
                                                    FillChar)))
    {
        return;
    }

    VtApplyRgbToRegion(ScreenBuffer,
                       0,
                       Top,
                       ScreenBuffer->ScreenBufferSize.X - 1,
                       Top + (SHORT)Lines - 1);
}

static VOID
VtScrollLeft(PCONSOLE Console,
             PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
             ULONG Count)
{
    SHORT Top = ScreenBuffer->VtState.ScrollTop;
    SHORT Bottom = ScreenBuffer->VtState.ScrollBottom;
    SHORT Width = ScreenBuffer->ScreenBufferSize.X;
    SHORT Line;
    USHORT Attr = ScreenBuffer->VtState.CurrentAttributes;

    if (!Console || !ScreenBuffer)
        return;

    if (Width <= 0)
        return;

    if (Count == 0)
        Count = 1;
    if (Count > (ULONG)Width)
        Count = Width;

    if (Top < 0)
        Top = 0;
    if (Bottom >= ScreenBuffer->ScreenBufferSize.Y)
        Bottom = ScreenBuffer->ScreenBufferSize.Y - 1;
    if (Bottom < Top)
        return;

    for (Line = Top; Line <= Bottom; ++Line)
    {
        PCHAR_INFO Row = ConioCoordToPointer(ScreenBuffer, 0, Line);
        ULONG RowIndex = ConioCoordToIndex(ScreenBuffer, 0, Line);
        SHORT Shift = (SHORT)Count;

        if (Shift >= Width)
        {
            VtFillRectWithCurrentAttr(ScreenBuffer, 0, Line, Width > 0 ? Width - 1 : 0, Line);
            continue;
        }

        RtlMoveMemory(Row, Row + Shift, (Width - Shift) * sizeof(CHAR_INFO));

        if (ScreenBuffer->FgColors)
        {
            RtlMoveMemory(ScreenBuffer->FgColors + RowIndex,
                          ScreenBuffer->FgColors + RowIndex + Shift,
                          (Width - Shift) * sizeof(COLORREF));
        }

        if (ScreenBuffer->BgColors)
        {
            RtlMoveMemory(ScreenBuffer->BgColors + RowIndex,
                          ScreenBuffer->BgColors + RowIndex + Shift,
                          (Width - Shift) * sizeof(COLORREF));
        }

        {
            SHORT x;
            for (x = Width - Shift; x < Width; ++x)
            {
                PCHAR_INFO Cell = Row + x;
                Cell->Char.UnicodeChar = L' ';
                Cell->Attributes = Attr;
                ConioSetCellFgColor(ScreenBuffer, x, Line, ScreenBuffer->VtState.UseRgbForeground ? ScreenBuffer->VtState.CurrentFgColor : CLR_INVALID);
                ConioSetCellBgColor(ScreenBuffer, x, Line, ScreenBuffer->VtState.UseRgbBackground ? ScreenBuffer->VtState.CurrentBgColor : CLR_INVALID);
            }
        }
    }

    if (Width > 0)
    {
        SMALL_RECT Region;
        Region.Left = 0;
        Region.Right = Width - 1;
        Region.Top = Top;
        Region.Bottom = Bottom;
        TermDrawRegion(Console, &Region);
    }
}

static VOID
VtScrollRight(PCONSOLE Console,
              PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
              ULONG Count)
{
    SHORT Top = ScreenBuffer->VtState.ScrollTop;
    SHORT Bottom = ScreenBuffer->VtState.ScrollBottom;
    SHORT Width = ScreenBuffer->ScreenBufferSize.X;
    SHORT Line;
    USHORT Attr = ScreenBuffer->VtState.CurrentAttributes;

    if (!Console || !ScreenBuffer)
        return;

    if (Width <= 0)
        return;

    if (Count == 0)
        Count = 1;
    if (Count > (ULONG)Width)
        Count = Width;

    if (Top < 0)
        Top = 0;
    if (Bottom >= ScreenBuffer->ScreenBufferSize.Y)
        Bottom = ScreenBuffer->ScreenBufferSize.Y - 1;
    if (Bottom < Top)
        return;

    for (Line = Top; Line <= Bottom; ++Line)
    {
        PCHAR_INFO Row = ConioCoordToPointer(ScreenBuffer, 0, Line);
        ULONG RowIndex = ConioCoordToIndex(ScreenBuffer, 0, Line);
        SHORT Shift = (SHORT)Count;

        if (Shift >= Width)
        {
            VtFillRectWithCurrentAttr(ScreenBuffer, 0, Line, Width > 0 ? Width - 1 : 0, Line);
            continue;
        }

        RtlMoveMemory(Row + Shift, Row, (Width - Shift) * sizeof(CHAR_INFO));

        if (ScreenBuffer->FgColors)
        {
            RtlMoveMemory(ScreenBuffer->FgColors + RowIndex + Shift,
                          ScreenBuffer->FgColors + RowIndex,
                          (Width - Shift) * sizeof(COLORREF));
        }

        if (ScreenBuffer->BgColors)
        {
            RtlMoveMemory(ScreenBuffer->BgColors + RowIndex + Shift,
                          ScreenBuffer->BgColors + RowIndex,
                          (Width - Shift) * sizeof(COLORREF));
        }

        {
            SHORT x;
            for (x = 0; x < Shift; ++x)
            {
                PCHAR_INFO Cell = Row + x;
                Cell->Char.UnicodeChar = L' ';
                Cell->Attributes = Attr;
                ConioSetCellFgColor(ScreenBuffer, x, Line, ScreenBuffer->VtState.UseRgbForeground ? ScreenBuffer->VtState.CurrentFgColor : CLR_INVALID);
                ConioSetCellBgColor(ScreenBuffer, x, Line, ScreenBuffer->VtState.UseRgbBackground ? ScreenBuffer->VtState.CurrentBgColor : CLR_INVALID);
            }
        }
    }

    if (Width > 0)
    {
        SMALL_RECT Region;
        Region.Left = 0;
        Region.Right = Width - 1;
        Region.Top = Top;
        Region.Bottom = Bottom;
        TermDrawRegion(Console, &Region);
    }
}

static BOOLEAN
VtSetConsoleTitle(PCONSOLE Console,
                  const WCHAR *Title,
                  ULONG Length)
{
    PCONSRV_CONSOLE Cons;
    PWCHAR Buffer;
    ULONG MaxChars;

    if (!Console || !Title)
        return FALSE;

    Cons = (PCONSRV_CONSOLE)Console;
    if (!Cons)
        return FALSE;

    /* Clamp to what the UNICODE_STRING can represent. */
    if (Length > (ULONG)((MAXUSHORT / sizeof(WCHAR)) - 1))
        Length = (ULONG)((MAXUSHORT / sizeof(WCHAR)) - 1);

    MaxChars = Length + 1;
    Buffer = ConsoleAllocHeap(HEAP_ZERO_MEMORY, MaxChars * sizeof(WCHAR));
    if (!Buffer)
        return FALSE;

    if (Length > 0)
        RtlCopyMemory(Buffer, Title, Length * sizeof(WCHAR));
    Buffer[Length] = UNICODE_NULL;

    if (Cons->Title.Buffer)
        ConsoleFreeHeap(Cons->Title.Buffer);

    Cons->Title.Buffer = Buffer;
    Cons->Title.Length = (USHORT)(Length * sizeof(WCHAR));
    Cons->Title.MaximumLength = (USHORT)(MaxChars * sizeof(WCHAR));

    TermChangeTitle(Cons);
    return TRUE;
}

static BOOLEAN
VtReportWindowSize(PCONSOLE Console,
                   PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                   ULONG ReplyPrefix)
{
    WCHAR Response[32];
    SIZE_T Len = 0;
    SHORT Rows;
    SHORT Cols;

    if (!Console || !ScreenBuffer)
        return FALSE;

    Rows = ScreenBuffer->ViewSize.Y;
    Cols = ScreenBuffer->ViewSize.X;
    if (Rows <= 0)
        Rows = ScreenBuffer->ScreenBufferSize.Y;
    if (Cols <= 0)
        Cols = ScreenBuffer->ScreenBufferSize.X;
    if (Rows <= 0) Rows = 1;
    if (Cols <= 0) Cols = 1;

    Response[Len++] = L'\x1b';
    Response[Len++] = L'[';
    Len += VtAppendNumber(Response + Len,
                          ARRAYSIZE(Response) - Len,
                          ReplyPrefix);
    if (Len < ARRAYSIZE(Response))
        Response[Len++] = L';';
    Len += VtAppendNumber(Response + Len,
                          ARRAYSIZE(Response) - Len,
                          (ULONG)Rows);
    if (Len < ARRAYSIZE(Response))
        Response[Len++] = L';';
    Len += VtAppendNumber(Response + Len,
                          ARRAYSIZE(Response) - Len,
                          (ULONG)Cols);
    if (Len < ARRAYSIZE(Response))
        Response[Len++] = L't';

    VtSendInputResponse(Console, Response, Len);
    return TRUE;
}

static BOOLEAN
VtResizeWindow(PCONSOLE Console,
               PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
               ULONG Rows,
               ULONG Cols)
{
    SMALL_RECT WindowRect;
    COORD Size;
    NTSTATUS Status;

    if (!Console || !ScreenBuffer)
        return FALSE;

    if (Rows == 0 || Cols == 0)
        return TRUE;

    if (Rows > SHRT_MAX) Rows = SHRT_MAX;
    if (Cols > SHRT_MAX) Cols = SHRT_MAX;

    Size.X = (SHORT)max(1UL, Cols);
    Size.Y = (SHORT)max(1UL, Rows);

    if (Size.X != ScreenBuffer->ScreenBufferSize.X ||
        Size.Y != ScreenBuffer->ScreenBufferSize.Y)
    {
        Status = ConDrvSetConsoleScreenBufferSize(Console, ScreenBuffer, &Size);
        if (!NT_SUCCESS(Status))
            return FALSE;
    }

    WindowRect.Left = 0;
    WindowRect.Top = 0;
    WindowRect.Right = (SHORT)(Size.X - 1);
    WindowRect.Bottom = (SHORT)(Size.Y - 1);

    Status = ConDrvSetConsoleWindowInfo(Console, ScreenBuffer, TRUE, &WindowRect);
    return NT_SUCCESS(Status);
}

static BOOLEAN
VtParseHexComponent(const WCHAR *Start,
                    const WCHAR *End,
                    ULONG *Value,
                    ULONG *Digits)
{
    ULONG Result = 0;
    ULONG Count = 0;

    if (!Value || !Digits)
        return FALSE;

    while (Start < End)
    {
        WCHAR Ch = *Start++;
        ULONG Nibble;

        if (Ch >= L'0' && Ch <= L'9')
            Nibble = Ch - L'0';
        else if (Ch >= L'a' && Ch <= L'f')
            Nibble = Ch - L'a' + 10;
        else if (Ch >= L'A' && Ch <= L'F')
            Nibble = Ch - L'A' + 10;
        else
            return FALSE;

        Result = (Result << 4) | Nibble;
        if (++Count > 4)
            return FALSE;
    }

    if (Count == 0)
        return FALSE;

    *Value = Result;
    *Digits = Count;
    return TRUE;
}

static BYTE
VtScaleComponent(ULONG Value, ULONG Digits)
{
    ULONG MaxValue;

    if (Digits == 0)
        return 0;

    MaxValue = (1u << (Digits * 4)) - 1u;
    if (MaxValue == 0)
        return 0;

    return (BYTE)((Value * 255u + MaxValue / 2u) / MaxValue);
}

static BOOLEAN
VtOscParseRgb(const WCHAR *Value,
              ULONG Length,
              COLORREF *Color)
{
    const WCHAR *Start = Value;
    const WCHAR *End = Value + Length;

    if (!Value || !Color)
        return FALSE;

    /* Trim surrounding whitespace */
    while (Start < End && (*Start == L' ' || *Start == L'\t'))
        ++Start;
    while (Start < End && (End[-1] == L' ' || End[-1] == L'\t'))
        --End;

    if (Start >= End)
        return FALSE;

    if (*Start == L'#')
    {
        ULONG DigitsPerComponent;
        ULONG ComponentLength = (ULONG)(End - Start - 1);
        ULONG RValue, GValue, BValue;
        ULONG Digits;
        const WCHAR *Ptr;

        if (ComponentLength == 0 || (ComponentLength % 3) != 0)
            return FALSE;

        DigitsPerComponent = ComponentLength / 3;
        Ptr = Start + 1;

        if (!VtParseHexComponent(Ptr, Ptr + DigitsPerComponent, &RValue, &Digits))
            return FALSE;
        Ptr += DigitsPerComponent;

        if (!VtParseHexComponent(Ptr, Ptr + DigitsPerComponent, &GValue, &Digits))
            return FALSE;
        Ptr += DigitsPerComponent;

        if (!VtParseHexComponent(Ptr, Ptr + DigitsPerComponent, &BValue, &Digits))
            return FALSE;

        *Color = RGB(VtScaleComponent(RValue, DigitsPerComponent),
                     VtScaleComponent(GValue, DigitsPerComponent),
                     VtScaleComponent(BValue, DigitsPerComponent));
        return TRUE;
    }

    if ((End - Start) > 4 && Start[0] == L'r' && Start[1] == L'g' && Start[2] == L'b' && Start[3] == L':')
    {
        const WCHAR *Ptr = Start + 4;
        ULONG Values[3];
        ULONG Digits;
        ULONG Index = 0;
        const WCHAR *SegmentStart;

        while (Index < 3 && Ptr <= End)
        {
            SegmentStart = Ptr;
            while (Ptr < End && *Ptr != L'/')
                ++Ptr;

            if (!VtParseHexComponent(SegmentStart, Ptr, &Values[Index], &Digits))
                return FALSE;

            Values[Index] = VtScaleComponent(Values[Index], Digits);
            ++Index;

            if (Ptr < End)
                ++Ptr; /* Skip '/' */
        }

        if (Index != 3)
            return FALSE;

        *Color = RGB((BYTE)Values[0], (BYTE)Values[1], (BYTE)Values[2]);
        return TRUE;
    }

    return FALSE;
}

static VOID
VtUpdatePaletteHandle(PCONSOLE Console,
                      PTEXTMODE_SCREEN_BUFFER Buffer)
{
    PCONSRV_CONSOLE Cons = (PCONSRV_CONSOLE)Console;
    PALETTEENTRY Entries[16];
    UINT Index;

    if (!Cons || !Buffer || !Buffer->PaletteHandle)
        return;

    for (Index = 0; Index < ARRAYSIZE(Entries); ++Index)
    {
        COLORREF Color = Cons->Colors[Index];
        Entries[Index].peRed   = GetRValue(Color);
        Entries[Index].peGreen = GetGValue(Color);
        Entries[Index].peBlue  = GetBValue(Color);
        Entries[Index].peFlags = 0;
    }

    SetPaletteEntries(Buffer->PaletteHandle, 0, ARRAYSIZE(Entries), Entries);
}

static VOID
VtRefreshPaletteForBuffer(PCONSOLE Console,
                          PTEXTMODE_SCREEN_BUFFER Buffer)
{
    if (!Console || !Buffer)
        return;

    if (!Buffer->VtState.UseRgbForeground)
    {
        Buffer->VtState.CurrentFgColor = VtGetPaletteColor(Console, Buffer->VtState.CurrentAttributes & 0x0F);
    }

    if (!Buffer->VtState.UseRgbBackground)
    {
        Buffer->VtState.CurrentBgColor = VtGetPaletteColor(Console, (Buffer->VtState.CurrentAttributes >> 4) & 0x0F);
    }

    if (!Buffer->VtState.SavedUseRgbForeground)
    {
        Buffer->VtState.SavedFgColor = VtGetPaletteColor(Console, Buffer->VtState.SavedAttributes & 0x0F);
    }

    if (!Buffer->VtState.SavedUseRgbBackground)
    {
        Buffer->VtState.SavedBgColor = VtGetPaletteColor(Console, (Buffer->VtState.SavedAttributes >> 4) & 0x0F);
    }

    VtUpdatePaletteHandle(Console, Buffer);
}

static VOID
VtApplyPaletteChange(PCONSOLE Console)
{
    PCONSOLE_SCREEN_BUFFER Active;

    if (!Console)
        return;

    Active = Console->ActiveBuffer;
    if (Active && GetType(Active) == TEXTMODE_BUFFER)
    {
        PTEXTMODE_SCREEN_BUFFER TextActive = (PTEXTMODE_SCREEN_BUFFER)Active;
        SMALL_RECT Region;

        TermSetPalette(Console, TextActive->PaletteHandle, TextActive->PaletteUsage);

        if (TextActive->ScreenBufferSize.X > 0 && TextActive->ScreenBufferSize.Y > 0)
        {
            Region.Left = 0;
            Region.Top = 0;
            Region.Right = TextActive->ScreenBufferSize.X - 1;
            Region.Bottom = TextActive->ScreenBufferSize.Y - 1;
            TermDrawRegion(Console, &Region);
        }
    }
}

static BOOLEAN
VtHandleOscPalette(PCONSOLE Console,
                   PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                   const WCHAR *Parameters,
                   ULONG Length)
{
    const WCHAR *Ptr = Parameters;
    const WCHAR *End = Parameters + Length;
    BOOLEAN Updated = FALSE;
    PCONSRV_CONSOLE Cons = (PCONSRV_CONSOLE)Console;

    if (!Console || !Cons)
        return TRUE;

    while (Ptr < End)
    {
        ULONG Index = 0;
        BOOLEAN HaveDigits = FALSE;

        while (Ptr < End && (*Ptr == L';' || *Ptr == L' ' || *Ptr == L'\t'))
            ++Ptr;

        if (Ptr >= End)
            break;

        while (Ptr < End && *Ptr != L';')
        {
            if (*Ptr >= L'0' && *Ptr <= L'9')
            {
                Index = Index * 10 + (ULONG)(*Ptr - L'0');
                HaveDigits = TRUE;
            }
            else if (*Ptr != L' ' && *Ptr != L'\t')
            {
                HaveDigits = FALSE;
            }
            ++Ptr;
        }

        if (!HaveDigits)
        {
            if (Ptr < End)
                ++Ptr;
            continue;
        }

        if (Ptr >= End)
            break;

        ++Ptr; /* Skip ';' */
        const WCHAR *ColorStart = Ptr;
        while (Ptr < End && *Ptr != L';')
            ++Ptr;

        if (ColorStart < Ptr && Index < 16)
        {
            COLORREF Parsed;

            if (VtOscParseRgb(ColorStart, (ULONG)(Ptr - ColorStart), &Parsed))
            {
                Cons->Colors[Index] = Parsed;
                Updated = TRUE;
            }
        }
    }

    if (Updated)
    {
        VtRefreshPaletteForBuffer(Console, ScreenBuffer);
        if (ScreenBuffer && ScreenBuffer->VtState.PrimaryBuffer)
            VtRefreshPaletteForBuffer(Console, ScreenBuffer->VtState.PrimaryBuffer);
        if (ScreenBuffer && ScreenBuffer->VtState.AlternateBuffer)
            VtRefreshPaletteForBuffer(Console, ScreenBuffer->VtState.AlternateBuffer);

        VtApplyPaletteChange(Console);
    }

    return Updated;
}

static BOOLEAN
VtHandleOscDefaultColor(PCONSOLE Console,
                        PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                        ULONG Parameter,
                        const WCHAR *Value,
                        ULONG Length)
{
    PCONSRV_CONSOLE Cons = (PCONSRV_CONSOLE)Console;
    UCHAR Index;
    COLORREF Parsed;

    if (!Cons || !ScreenBuffer || !Value)
        return FALSE;

    while (Length > 0 && (*Value == L' ' || *Value == L'\t'))
    {
        ++Value;
        --Length;
    }

    if (Parameter == 10)
        Index = (UCHAR)(ScreenBuffer->ScreenDefaultAttrib & 0x0F);
    else if (Parameter == 11)
        Index = (UCHAR)((ScreenBuffer->ScreenDefaultAttrib >> 4) & 0x0F);
    else
        return FALSE;

    if (Length > 0 && *Value == L'?')
    {
        VtSendOscColorResponse(Console, Parameter, Cons->Colors[Index]);
        return FALSE;
    }

    if (!VtOscParseRgb(Value, Length, &Parsed))
        return FALSE;

    Cons->Colors[Index] = Parsed;
    return TRUE;
}

static BOOLEAN
VtHandleOscHyperlink(PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                     const WCHAR *Parameters,
                     ULONG Length)
{
    const WCHAR *Ptr = Parameters;
    const WCHAR *End = Parameters + Length;
    const WCHAR *UriStart;
    ULONG UriLength = 0;

    if (!ScreenBuffer)
        return TRUE;

    while (Ptr < End && *Ptr != L';')
        ++Ptr;

    if (Ptr < End)
        ++Ptr;
    else
        Ptr = End;

    UriStart = Ptr;
    if (UriStart < End)
        UriLength = (ULONG)(End - UriStart);

    VtClearHyperlink(ScreenBuffer);

    if (UriLength == 0)
        return TRUE;

    if (UriLength >= (ULONG)((USHRT_MAX / sizeof(WCHAR)) - 1))
        return TRUE;

    PWCHAR Buffer = ConsoleAllocHeap(0, (UriLength + 1) * sizeof(WCHAR));
    if (!Buffer)
        return TRUE;

    RtlCopyMemory(Buffer, UriStart, UriLength * sizeof(WCHAR));
    Buffer[UriLength] = UNICODE_NULL;

    ScreenBuffer->VtState.HyperlinkUri.Buffer = Buffer;
    ScreenBuffer->VtState.HyperlinkUri.Length = (USHORT)(UriLength * sizeof(WCHAR));
    ScreenBuffer->VtState.HyperlinkUri.MaximumLength = (USHORT)((UriLength + 1) * sizeof(WCHAR));
    ScreenBuffer->VtState.HyperlinkActive = TRUE;
    return TRUE;
}

static BOOLEAN
VtHandleOscClipboard(PCONSOLE Console,
                     const WCHAR *Parameters,
                     ULONG Length)
{
    UNREFERENCED_PARAMETER(Console);
    UNREFERENCED_PARAMETER(Parameters);
    UNREFERENCED_PARAMETER(Length);
    return TRUE;
}

static BOOLEAN
VtHandleOscSequence(PCONSOLE Console,
                    PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                    const WCHAR *Sequence,
                    ULONG Length)
{
    const WCHAR *End = Sequence + Length;
    const WCHAR *ParamEnd = Sequence;
    ULONG Parameter = 0;
    BOOLEAN HaveParameter = FALSE;

    while (ParamEnd < End && *ParamEnd != L';')
        ++ParamEnd;

    for (const WCHAR *Ptr = Sequence; Ptr < ParamEnd; ++Ptr)
    {
        if (*Ptr >= L'0' && *Ptr <= L'9')
        {
            Parameter = Parameter * 10 + (ULONG)(*Ptr - L'0');
            HaveParameter = TRUE;
        }
        else if (*Ptr != L' ' && *Ptr != L'\t')
        {
            return TRUE;
        }
    }

    if (!HaveParameter)
        Parameter = 0;

    if (ParamEnd < End)
        ++ParamEnd;

    switch (Parameter)
    {
        case 0:
        case 1:
        case 2:
            if (ParamEnd <= End)
            {
                VtSetConsoleTitle(Console, ParamEnd, (ULONG)(End - ParamEnd));
            }
            return TRUE;

        case 4:
            VtHandleOscPalette(Console, ScreenBuffer, ParamEnd, (ULONG)(End - ParamEnd));
            return TRUE;

        case 10:
        case 11:
        {
            BOOLEAN Updated = FALSE;

            if (ParamEnd <= End)
            {
                Updated = VtHandleOscDefaultColor(Console,
                                                   ScreenBuffer,
                                                   Parameter,
                                                   ParamEnd,
                                                   (ULONG)(End - ParamEnd));
            }

            if (Updated)
            {
                VtRefreshPaletteForBuffer(Console, ScreenBuffer);
                if (ScreenBuffer && ScreenBuffer->VtState.PrimaryBuffer)
                    VtRefreshPaletteForBuffer(Console, ScreenBuffer->VtState.PrimaryBuffer);
                if (ScreenBuffer && ScreenBuffer->VtState.AlternateBuffer)
                    VtRefreshPaletteForBuffer(Console, ScreenBuffer->VtState.AlternateBuffer);

                VtApplyPaletteChange(Console);
            }

            return TRUE;
        }

        case 8:
            return VtHandleOscHyperlink(ScreenBuffer, ParamEnd, (ULONG)(End - ParamEnd));

        case 52:
            return VtHandleOscClipboard(Console, ParamEnd, (ULONG)(End - ParamEnd));

        default:
            return TRUE;
    }

    return TRUE;
}

static NTSTATUS
VtFlushText(PCONSOLE Console,
           PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
           PCWSTR Text,
           ULONG Length)
{
    NTSTATUS Status;
    USHORT OriginalAttrib;
    WCHAR StackBuffer[128];
    WCHAR *Translated;
    UCHAR pending;

    if (Length == 0) return STATUS_SUCCESS;

    OriginalAttrib = ScreenBuffer->ScreenDefaultAttrib;
    ScreenBuffer->ScreenDefaultAttrib = ScreenBuffer->VtState.CurrentAttributes;

    if ((ScreenBuffer->VtState.PendingSingleShift == VT_CHARSET_SLOT_INVALID) &&
        (VtGetActiveCharset(ScreenBuffer) != VtCharsetDecSpecial))
    {
        Status = TermWriteStream(Console,
                                 ScreenBuffer,
                                 (PWCHAR)Text,
                                 Length,
                                 TRUE);
        ScreenBuffer->ScreenDefaultAttrib = OriginalAttrib;
        return Status;
    }

    Translated = StackBuffer;

    if (Length > ARRAYSIZE(StackBuffer))
    {
        Translated = ConsoleAllocHeap(0, Length * sizeof(WCHAR));
        if (!Translated)
        {
            ScreenBuffer->ScreenDefaultAttrib = OriginalAttrib;
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    pending = ScreenBuffer->VtState.PendingSingleShift;

    for (ULONG i = 0; i < Length; ++i)
    {
        UCHAR slot = (pending != VT_CHARSET_SLOT_INVALID) ? pending
                                                          : ScreenBuffer->VtState.ActiveCharset;
        Translated[i] = VtTranslateGlyphSlot(ScreenBuffer, slot, Text[i]);

        if (pending != VT_CHARSET_SLOT_INVALID)
            pending = VT_CHARSET_SLOT_INVALID;
    }

    ScreenBuffer->VtState.PendingSingleShift = pending;

    Status = TermWriteStream(Console,
                             ScreenBuffer,
                             Translated,
                             Length,
                             TRUE);

    if (NT_SUCCESS(Status) && Length > 0)
    {
        for (ULONG i = Length; i > 0; --i)
        {
            WCHAR ch = Translated[i - 1];
            if (ch >= L' ' && ch != 0x7F)
            {
                ScreenBuffer->VtState.LastWrittenChar = ch;
                ScreenBuffer->VtState.LastCharValid = TRUE;
                break;
            }
        }
    }

    if (Translated != StackBuffer)
        ConsoleFreeHeap(Translated);

    ScreenBuffer->ScreenDefaultAttrib = OriginalAttrib;
    return Status;
}

static const USHORT VtAnsiFgMap[8] =
{
    0,
    FOREGROUND_RED,
    FOREGROUND_GREEN,
    FOREGROUND_RED | FOREGROUND_GREEN,
    FOREGROUND_BLUE,
    FOREGROUND_RED | FOREGROUND_BLUE,
    FOREGROUND_GREEN | FOREGROUND_BLUE,
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE
};

static const USHORT VtAnsiBgMap[8] =
{
    0,
    BACKGROUND_RED,
    BACKGROUND_GREEN,
    BACKGROUND_RED | BACKGROUND_GREEN,
    BACKGROUND_BLUE,
    BACKGROUND_RED | BACKGROUND_BLUE,
    BACKGROUND_GREEN | BACKGROUND_BLUE,
    BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE
};

static USHORT
VtIndexToFgAttr(UCHAR Index)
{
    USHORT Attr = 0;
    if (Index & 0x1) Attr |= FOREGROUND_BLUE;
    if (Index & 0x2) Attr |= FOREGROUND_GREEN;
    if (Index & 0x4) Attr |= FOREGROUND_RED;
    if (Index & 0x8) Attr |= FOREGROUND_INTENSITY;
    return Attr;
}

static USHORT
VtIndexToBgAttr(UCHAR Index)
{
    USHORT Attr = 0;
    if (Index & 0x1) Attr |= BACKGROUND_BLUE;
    if (Index & 0x2) Attr |= BACKGROUND_GREEN;
    if (Index & 0x4) Attr |= BACKGROUND_RED;
    if (Index & 0x8) Attr |= BACKGROUND_INTENSITY;
    return Attr;
}

static VOID
VtApplySgr(PCONSOLE Console,
           PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
           const ULONG *Params,
           ULONG Count)
{
    USHORT Attr = ScreenBuffer->VtState.CurrentAttributes;
    const USHORT DefaultAttr = ScreenBuffer->VtState.SavedAttributes;

    if (Count == 0)
    {
        Attr = DefaultAttr;
        ScreenBuffer->VtState.UseRgbForeground = FALSE;
        ScreenBuffer->VtState.UseRgbBackground = FALSE;
        ScreenBuffer->VtState.CurrentFgColor = VtGetPaletteColor(Console, Attr & 0x0F);
        ScreenBuffer->VtState.CurrentBgColor = VtGetPaletteColor(Console, (Attr >> 4) & 0x0F);
        ScreenBuffer->VtState.CurrentAttributes = Attr;
        return;
    }

    for (ULONG i = 0; i < Count; ++i)
    {
        ULONG Code = Params[i];
        switch (Code)
        {
            case 0:
                Attr = DefaultAttr;
                ScreenBuffer->VtState.UseRgbForeground = FALSE;
                ScreenBuffer->VtState.UseRgbBackground = FALSE;
                break;

            case 1:
                Attr |= FOREGROUND_INTENSITY;
                break;

            case 22:
                Attr &= ~FOREGROUND_INTENSITY;
                break;

            case 30: case 31: case 32: case 33:
            case 34: case 35: case 36: case 37:
            {
                UCHAR idx = (UCHAR)(Code - 30);
                Attr &= ~FG_ATTR_MASK;
                Attr |= VtIndexToFgAttr(idx);
                ScreenBuffer->VtState.UseRgbForeground = FALSE;
                break;
            }

            case 39:
                Attr &= ~FG_ATTR_MASK;
                Attr |= (DefaultAttr & FG_ATTR_MASK);
                ScreenBuffer->VtState.UseRgbForeground = FALSE;
                break;

            case 40: case 41: case 42: case 43:
            case 44: case 45: case 46: case 47:
            {
                UCHAR idx = (UCHAR)(Code - 40);
                Attr &= ~BG_ATTR_MASK;
                Attr |= VtIndexToBgAttr(idx);
                ScreenBuffer->VtState.UseRgbBackground = FALSE;
                break;
            }

            case 49:
                Attr &= ~BG_ATTR_MASK;
                Attr |= (DefaultAttr & BG_ATTR_MASK);
                ScreenBuffer->VtState.UseRgbBackground = FALSE;
                break;

            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
            {
                UCHAR idx = (UCHAR)((Code - 90) | 0x08);
                Attr &= ~FG_ATTR_MASK;
                Attr |= VtIndexToFgAttr(idx);
                ScreenBuffer->VtState.UseRgbForeground = FALSE;
                break;
            }

            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
            {
                UCHAR idx = (UCHAR)((Code - 100) | 0x08);
                Attr &= ~BG_ATTR_MASK;
                Attr |= VtIndexToBgAttr(idx);
                ScreenBuffer->VtState.UseRgbBackground = FALSE;
                break;
            }

            case 38:
            case 48:
            {
                BOOL Foreground = (Code == 38);
                if (i + 1 < Count)
                {
                    ULONG mode = Params[++i];
                    if (mode == 2 && (i + 3) < Count)
                    {
                        ULONG r = Params[++i] & 0xFF;
                        ULONG g = Params[++i] & 0xFF;
                        ULONG b = Params[++i] & 0xFF;
                        COLORREF color = RGB((BYTE)r, (BYTE)g, (BYTE)b);
                        UCHAR nearest = VtFindNearestPaletteIndex(Console, color);
                        if (Foreground)
                        {
                            Attr &= ~FG_ATTR_MASK;
                            Attr |= VtIndexToFgAttr(nearest);
                            ScreenBuffer->VtState.UseRgbForeground = TRUE;
                            ScreenBuffer->VtState.CurrentFgColor = color;
                        }
                        else
                        {
                            Attr &= ~BG_ATTR_MASK;
                            Attr |= VtIndexToBgAttr(nearest);
                            ScreenBuffer->VtState.UseRgbBackground = TRUE;
                            ScreenBuffer->VtState.CurrentBgColor = color;
                        }
                        continue;
                    }
                    else if (mode == 5 && (i + 1) < Count)
                    {
                        ULONG idx = Params[++i];
                        COLORREF color = VtColorFromXtermIndex(idx);
                        if (idx < 16)
                        {
                            if (Foreground)
                            {
                                Attr &= ~FG_ATTR_MASK;
                                Attr |= VtIndexToFgAttr((UCHAR)idx);
                                ScreenBuffer->VtState.UseRgbForeground = FALSE;
                            }
                            else
                            {
                                Attr &= ~BG_ATTR_MASK;
                                Attr |= VtIndexToBgAttr((UCHAR)idx);
                                ScreenBuffer->VtState.UseRgbBackground = FALSE;
                            }
                        }
                        else
                        {
                            UCHAR nearest = VtFindNearestPaletteIndex(Console, color);
                            if (Foreground)
                            {
                                Attr &= ~FG_ATTR_MASK;
                                Attr |= VtIndexToFgAttr(nearest);
                                ScreenBuffer->VtState.UseRgbForeground = TRUE;
                                ScreenBuffer->VtState.CurrentFgColor = color;
                            }
                            else
                            {
                                Attr &= ~BG_ATTR_MASK;
                                Attr |= VtIndexToBgAttr(nearest);
                                ScreenBuffer->VtState.UseRgbBackground = TRUE;
                                ScreenBuffer->VtState.CurrentBgColor = color;
                            }
                        }
                        continue;
                    }
                }

                if (Foreground)
                    ScreenBuffer->VtState.UseRgbForeground = FALSE;
                else
                    ScreenBuffer->VtState.UseRgbBackground = FALSE;
                break;
            }

            default:
                break;
        }
    }

    ScreenBuffer->VtState.CurrentAttributes = Attr;

    if (!ScreenBuffer->VtState.UseRgbForeground)
        ScreenBuffer->VtState.CurrentFgColor = VtGetPaletteColor(Console, Attr & 0x0F);
    if (!ScreenBuffer->VtState.UseRgbBackground)
        ScreenBuffer->VtState.CurrentBgColor = VtGetPaletteColor(Console, (Attr >> 4) & 0x0F);
}


static VOID
VtEraseLine(PCONSOLE Console,
            PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
            ULONG Mode)
{
    SHORT X;
    SHORT Y = ScreenBuffer->CursorPosition.Y;
    USHORT Attr = ScreenBuffer->VtState.CurrentAttributes;
    SMALL_RECT Region;
    PCHAR_INFO Ptr;
    SHORT StartX, EndX;

    if (Y < 0 || Y >= ScreenBuffer->ScreenBufferSize.Y)
        return;

    switch (Mode)
    {
        case 1:
            StartX = 0;
            EndX = ScreenBuffer->CursorPosition.X;
            break;

        case 2:
            StartX = 0;
            EndX = ScreenBuffer->ScreenBufferSize.X - 1;
            break;

        case 0:
        default:
            StartX = ScreenBuffer->CursorPosition.X;
            EndX = ScreenBuffer->ScreenBufferSize.X - 1;
            break;
    }

    StartX = max(StartX, 0);
    EndX = min(EndX, (SHORT)(ScreenBuffer->ScreenBufferSize.X - 1));
    if (EndX < StartX)
        return;

    Ptr = ConioCoordToPointer(ScreenBuffer, StartX, Y);

    for (X = StartX; X <= EndX; ++X, ++Ptr)
    {
        Ptr->Char.UnicodeChar = L' ';
        Ptr->Attributes = Attr;
        ConioSetCellFgColor(ScreenBuffer, X, Y,
                            ScreenBuffer->VtState.UseRgbForeground ? ScreenBuffer->VtState.CurrentFgColor : CLR_INVALID);
        ConioSetCellBgColor(ScreenBuffer, X, Y,
                            ScreenBuffer->VtState.UseRgbBackground ? ScreenBuffer->VtState.CurrentBgColor : CLR_INVALID);
    }

    Region.Left   = StartX;
    Region.Right  = EndX;
    Region.Top    = Y;
    Region.Bottom = Y;
    TermDrawRegion(Console, &Region);
}

static VOID
VtDecaln(PCONSOLE Console,
         PTEXTMODE_SCREEN_BUFFER ScreenBuffer)
{
    SHORT Y, X;
    USHORT Attr;
    SMALL_RECT Region;
    COORD Origin;
    COLORREF Fg, Bg;

    if (!Console || !ScreenBuffer)
        return;

    Attr = ScreenBuffer->VtState.CurrentAttributes;
    Fg = ScreenBuffer->VtState.UseRgbForeground ? ScreenBuffer->VtState.CurrentFgColor : CLR_INVALID;
    Bg = ScreenBuffer->VtState.UseRgbBackground ? ScreenBuffer->VtState.CurrentBgColor : CLR_INVALID;

    for (Y = 0; Y < ScreenBuffer->ScreenBufferSize.Y; ++Y)
    {
        PCHAR_INFO Ptr = ConioCoordToPointer(ScreenBuffer, 0, Y);
        for (X = 0; X < ScreenBuffer->ScreenBufferSize.X; ++X, ++Ptr)
        {
            Ptr->Char.UnicodeChar = L'E';
            Ptr->Attributes = Attr;
            ConioSetCellFgColor(ScreenBuffer, X, Y, Fg);
            ConioSetCellBgColor(ScreenBuffer, X, Y, Bg);
        }
    }

    Region.Left   = 0;
    Region.Top    = 0;
    Region.Right  = ScreenBuffer->ScreenBufferSize.X > 0 ? ScreenBuffer->ScreenBufferSize.X - 1 : 0;
    Region.Bottom = ScreenBuffer->ScreenBufferSize.Y > 0 ? ScreenBuffer->ScreenBufferSize.Y - 1 : 0;
    TermDrawRegion(Console, &Region);

    Origin.X = 0;
    Origin.Y = 0;
    ConDrvSetConsoleCursorPosition(Console, ScreenBuffer, &Origin);
}

static VOID
VtEraseDisplay(PCONSOLE Console,
               PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
               ULONG Mode)
{
    SHORT X, Y;
    USHORT Attr = ScreenBuffer->VtState.CurrentAttributes;
    SMALL_RECT Region;
    COORD Origin;

    switch (Mode)
    {
        case 1:
        case 0:
        {
            SHORT StartY, EndY;
            SHORT StartX = 0;
            SHORT EndX = ScreenBuffer->ScreenBufferSize.X - 1;

            if (Mode == 0)
            {
                StartY = ScreenBuffer->CursorPosition.Y;
                EndY = ScreenBuffer->ScreenBufferSize.Y - 1;
            }
            else
            {
                StartY = 0;
                EndY = ScreenBuffer->CursorPosition.Y;
            }

            StartY = max(StartY, 0);
            EndY = min(EndY, (SHORT)(ScreenBuffer->ScreenBufferSize.Y - 1));
            if (EndY < StartY)
                return;

            for (Y = StartY; Y <= EndY; ++Y)
            {
                SHORT LocalStartX = StartX;
                SHORT LocalEndX = EndX;

                if (Mode == 0 && Y == StartY)
                    LocalStartX = max(ScreenBuffer->CursorPosition.X, 0);
                else if (Mode == 1 && Y == EndY)
                    LocalEndX = min(ScreenBuffer->CursorPosition.X, (SHORT)(ScreenBuffer->ScreenBufferSize.X - 1));

                if (LocalEndX < LocalStartX)
                    continue;

                PCHAR_INFO Ptr = ConioCoordToPointer(ScreenBuffer, LocalStartX, Y);
                for (X = LocalStartX; X <= LocalEndX; ++X, ++Ptr)
                {
                    Ptr->Char.UnicodeChar = L' ';
                    Ptr->Attributes = Attr;
                    ConioSetCellFgColor(ScreenBuffer, X, Y,
                                        ScreenBuffer->VtState.UseRgbForeground ? ScreenBuffer->VtState.CurrentFgColor : CLR_INVALID);
                    ConioSetCellBgColor(ScreenBuffer, X, Y,
                                        ScreenBuffer->VtState.UseRgbBackground ? ScreenBuffer->VtState.CurrentBgColor : CLR_INVALID);
                }
            }

            Region.Left   = 0;
            Region.Right  = ScreenBuffer->ScreenBufferSize.X - 1;
            Region.Top    = StartY;
            Region.Bottom = EndY;
            TermDrawRegion(Console, &Region);
            break;
        }

        case 2:
        case 3:
            for (Y = 0; Y < ScreenBuffer->ScreenBufferSize.Y; ++Y)
            {
                PCHAR_INFO Ptr = ConioCoordToPointer(ScreenBuffer, 0, Y);
                for (X = 0; X < ScreenBuffer->ScreenBufferSize.X; ++X, ++Ptr)
                {
                    Ptr->Char.UnicodeChar = L' ';
                    Ptr->Attributes = Attr;
                    ConioSetCellFgColor(ScreenBuffer, X, Y,
                                        ScreenBuffer->VtState.UseRgbForeground ? ScreenBuffer->VtState.CurrentFgColor : CLR_INVALID);
                    ConioSetCellBgColor(ScreenBuffer, X, Y,
                                        ScreenBuffer->VtState.UseRgbBackground ? ScreenBuffer->VtState.CurrentBgColor : CLR_INVALID);
                }
            }

            Region.Left   = 0;
            Region.Top    = 0;
            Region.Right  = ScreenBuffer->ScreenBufferSize.X - 1;
            Region.Bottom = ScreenBuffer->ScreenBufferSize.Y - 1;
            TermDrawRegion(Console, &Region);

            Origin.X = Origin.Y = 0;
            ConDrvSetConsoleCursorPosition(Console, ScreenBuffer, &Origin);

            if (Mode == 3)
            {
                ScreenBuffer->VirtualY = 0;
                ScreenBuffer->ViewOrigin.Y = 0;
            }
            break;
    }
}

static VOID
VtSoftReset(PCONSOLE Console,
            PTEXTMODE_SCREEN_BUFFER ScreenBuffer)
{
    PTEXTMODE_SCREEN_BUFFER Target;
    BOOLEAN WasActive;
    COORD Origin;

    if (!Console || !ScreenBuffer)
        return;

    if (ScreenBuffer->VtState.PrimaryBuffer != NULL ||
        ScreenBuffer->VtState.AlternateBuffer != NULL)
    {
        VtDisableAlternateScreen(Console, ScreenBuffer);
        Target = (PTEXTMODE_SCREEN_BUFFER)Console->ActiveBuffer;
    }
    else
    {
        Target = ScreenBuffer;
    }

    if (!Target)
        return;

    WasActive = Target->VtState.Active;
    VtClearHyperlink(Target);
    ConDrvVtInitializeBuffer(Target);
    Target->VtState.Active = WasActive ? TRUE : FALSE;

    VtEraseDisplay(Console, Target, 2);

    Origin.X = 0;
    Origin.Y = 0;
    ConDrvSetConsoleCursorPosition(Console, Target, &Origin);
    ConDrvSetConsoleCursorInfo(Console, Target, &Target->CursorInfo);
    Target->VtState.LastCharValid = FALSE;
}

VOID
NTAPI
ConDrvVtInitializeBuffer(PTEXTMODE_SCREEN_BUFFER ScreenBuffer)
{
    COORD Zero = {0, 0};

    if (!ScreenBuffer) return;

    ScreenBuffer->VtState.Active = FALSE;
    ScreenBuffer->VtState.CursorSaved = FALSE;
    ScreenBuffer->VtState.SavedCursorPos = Zero;
    ScreenBuffer->VtState.SavedAttributes = ScreenBuffer->ScreenDefaultAttrib;
    ScreenBuffer->VtState.CurrentAttributes = ScreenBuffer->ScreenDefaultAttrib;
    ScreenBuffer->VtState.UseRgbForeground = FALSE;
    ScreenBuffer->VtState.UseRgbBackground = FALSE;
    ScreenBuffer->VtState.CurrentFgColor = VtGetPaletteColor(ScreenBuffer->Header.Console, ScreenBuffer->ScreenDefaultAttrib & 0x0F);
    ScreenBuffer->VtState.CurrentBgColor = VtGetPaletteColor(ScreenBuffer->Header.Console, (ScreenBuffer->ScreenDefaultAttrib >> 4) & 0x0F);
    ScreenBuffer->VtState.SavedFgColor = ScreenBuffer->VtState.CurrentFgColor;
    ScreenBuffer->VtState.SavedBgColor = ScreenBuffer->VtState.CurrentBgColor;
    ScreenBuffer->VtState.SavedUseRgbForeground = FALSE;
    ScreenBuffer->VtState.SavedUseRgbBackground = FALSE;
    ScreenBuffer->VtState.PrivateModes = 0;
    ScreenBuffer->VtState.AlternateBuffer = NULL;
    ScreenBuffer->VtState.ScrollTop = 0;
    ScreenBuffer->VtState.ScrollBottom = max(0, ScreenBuffer->ScreenBufferSize.Y - 1);
    ScreenBuffer->VtState.PrimaryBuffer = NULL;
    ScreenBuffer->VtState.PrimaryCursorInfo = ScreenBuffer->CursorInfo;
    ScreenBuffer->VtState.PrimaryCursorPos = Zero;
    ScreenBuffer->VtState.PrimaryViewOrigin = Zero;
    ScreenBuffer->VtState.PrimaryVirtualY = 0;
    ScreenBuffer->VtState.DefaultCursorInfo = ScreenBuffer->CursorInfo;
    ScreenBuffer->VtState.CurrentCursorStyle = 0;
    ScreenBuffer->VtState.HyperlinkActive = FALSE;
    ScreenBuffer->VtState.HyperlinkUri.Buffer = NULL;
    ScreenBuffer->VtState.HyperlinkUri.Length = 0;
    ScreenBuffer->VtState.HyperlinkUri.MaximumLength = 0;
    ScreenBuffer->VtState.G0Charset = VtCharsetAscii;
    ScreenBuffer->VtState.G1Charset = VtCharsetDecSpecial;
    ScreenBuffer->VtState.G2Charset = VtCharsetAscii;
    ScreenBuffer->VtState.G3Charset = VtCharsetAscii;
    ScreenBuffer->VtState.ActiveCharset = VT_CHARSET_SLOT_G0;
    ScreenBuffer->VtState.PendingSingleShift = VT_CHARSET_SLOT_INVALID;
    ScreenBuffer->VtState.SavedG0Charset = ScreenBuffer->VtState.G0Charset;
    ScreenBuffer->VtState.SavedG1Charset = ScreenBuffer->VtState.G1Charset;
    ScreenBuffer->VtState.SavedG2Charset = ScreenBuffer->VtState.G2Charset;
    ScreenBuffer->VtState.SavedG3Charset = ScreenBuffer->VtState.G3Charset;
    ScreenBuffer->VtState.SavedActiveCharset = ScreenBuffer->VtState.ActiveCharset;
}

static VOID
VtHandleCursorPosition(PCONSOLE Console,
                       PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                       const ULONG *Params,
                       ULONG Count)
{
    COORD Position;
    SHORT Row, Col;

    Row = (Count >= 1) ? (SHORT)max(1UL, Params[0]) : 1;
    Col = (Count >= 2) ? (SHORT)max(1UL, Params[1]) : 1;

    Row = min(Row, ScreenBuffer->ScreenBufferSize.Y);
    Col = min(Col, ScreenBuffer->ScreenBufferSize.X);

    Position.Y = Row - 1;
    Position.X = Col - 1;

    ConDrvSetConsoleCursorPosition(Console, ScreenBuffer, &Position);
}

static BOOLEAN
VtApplyCursorStyle(PCONSOLE Console,
                   PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                   ULONG Style)
{
    CONSOLE_CURSOR_INFO CursorInfo;

    if (Style == 0)
    {
        CursorInfo = ScreenBuffer->VtState.DefaultCursorInfo;
    }
    else
    {
        CursorInfo = ScreenBuffer->CursorInfo;

        switch (Style)
        {
            case 1: /* blinking block (default) */
            case 2: /* steady block */
                CursorInfo.dwSize = 100;
                break;

            case 3: /* blinking underline */
            case 4: /* steady underline */
                CursorInfo.dwSize = 15;
                break;

            case 5: /* blinking bar */
            case 6: /* steady bar */
                CursorInfo.dwSize = 25;
                break;

            default:
                return FALSE;
        }
    }

    if (!NT_SUCCESS(ConDrvSetConsoleCursorInfo(Console,
                                               ScreenBuffer,
                                               &CursorInfo)))
    {
        return FALSE;
    }

    ScreenBuffer->VtState.CurrentCursorStyle = (UCHAR)Style;

    return TRUE;
}

static BOOLEAN
VtHandleDecPrivateMode(PCONSOLE Console,
                       PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                       ULONG Mode,
                       BOOLEAN Enable)
{
    ULONG Mask = 0;

    switch (Mode)
    {
        case 25: /* DECTCEM – show/hide the text cursor */
        {
            CONSOLE_CURSOR_INFO CursorInfo = ScreenBuffer->CursorInfo;
            CursorInfo.bVisible = Enable ? TRUE : FALSE;

            return NT_SUCCESS(ConDrvSetConsoleCursorInfo(Console,
                                                         ScreenBuffer,
                                                         &CursorInfo));
        }

        case 1002:
            Mask = VT_PRIVMODE_MOUSE_BUTTON_TRACKING;
            break;

        case 1006:
            Mask = VT_PRIVMODE_MOUSE_SGR_EXTENDED;
            break;

        case 2004:
            Mask = VT_PRIVMODE_BRACKETED_PASTE;
            break;

        case 1036:
            Mask = VT_PRIVMODE_META_SENDS_ESCAPE;
            break;

        case 1049:
            if (Enable)
                return VtEnableAlternateScreen(Console, ScreenBuffer);
            return VtDisableAlternateScreen(Console, ScreenBuffer);

        default:
            return FALSE;
    }

    if (Enable)
        ScreenBuffer->VtState.PrivateModes |= Mask;
    else
        ScreenBuffer->VtState.PrivateModes &= ~Mask;

    /*
     * These modes currently have no observable effect in the console server,
     * but they should not leak into the legacy renderer. Track them so we can
     * implement the behaviours in later roadmap steps.
     */
    return TRUE;
}

static BOOLEAN
VtHandleDeviceAttributes(PCONSOLE Console,
                         WCHAR PrivateIndicator)
{
    WCHAR Response[32];
    SIZE_T Len = 0;

    if (!Console)
        return FALSE;

    Response[Len++] = L'\x1b';
    Response[Len++] = L'[';

    switch (PrivateIndicator)
    {
        case 0:
        case L'?':
            if (Len >= ARRAYSIZE(Response))
                return FALSE;
            Response[Len++] = L'?';
            Len += VtAppendNumber(Response + Len,
                                  ARRAYSIZE(Response) - Len,
                                  1);
            if (Len >= ARRAYSIZE(Response))
                return FALSE;
            Response[Len++] = L';';
            Len += VtAppendNumber(Response + Len,
                                  ARRAYSIZE(Response) - Len,
                                  0);
            break;

        case L'>':
            if (Len >= ARRAYSIZE(Response))
                return FALSE;
            Response[Len++] = L'>';
            Len += VtAppendNumber(Response + Len,
                                  ARRAYSIZE(Response) - Len,
                                  0);
            if (Len >= ARRAYSIZE(Response))
                return FALSE;
            Response[Len++] = L';';
            Len += VtAppendNumber(Response + Len,
                                  ARRAYSIZE(Response) - Len,
                                  136);
            if (Len >= ARRAYSIZE(Response))
                return FALSE;
            Response[Len++] = L';';
            Len += VtAppendNumber(Response + Len,
                                  ARRAYSIZE(Response) - Len,
                                  0);
            break;

        default:
            return FALSE;
    }

    if (Len >= ARRAYSIZE(Response))
        return FALSE;

    Response[Len++] = L'c';

    VtSendInputResponse(Console, Response, Len);
    return TRUE;
}

static BOOLEAN
VtHandlePrivateCsiSequence(PCONSOLE Console,
                           PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                           WCHAR PrivateIndicator,
                           WCHAR Final,
                           WCHAR Intermediate,
                           const ULONG *Params,
                           ULONG Count)
{
    UNREFERENCED_PARAMETER(Intermediate);

    switch (Final)
    {
        case L'h':
        case L'l':
        {
            BOOLEAN Enable = (Final == L'h');
            BOOLEAN Handled = FALSE;
            ULONG i;

            for (i = 0; i < Count; ++i)
            {
                if (VtHandleDecPrivateMode(Console, ScreenBuffer, Params[i], Enable))
                    Handled = TRUE;
            }

            return Handled;
        }

        case L'c':
            return VtHandleDeviceAttributes(Console, PrivateIndicator);

        default:
            return FALSE;
    }
}

static BOOLEAN
VtHandleCsiSequence(PCONSOLE Console,
                    PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                    WCHAR Final,
                    WCHAR Intermediate,
                    const ULONG *Params,
                    ULONG Count)
{
    switch (Final)
    {
        case L'm':
            VtApplySgr(Console, ScreenBuffer, Params, Count);
            return TRUE;

        case L'p':
            if (Intermediate == L'!')
            {
                VtSoftReset(Console, ScreenBuffer);
                return TRUE;
            }
            return FALSE;

        case L'H':
        case L'f':
            VtHandleCursorPosition(Console, ScreenBuffer, Params, Count);
            return TRUE;

        case L'J':
            VtEraseDisplay(Console, ScreenBuffer, (Count >= 1) ? Params[0] : 0);
            return TRUE;

        case L'K':
            VtEraseLine(Console, ScreenBuffer, (Count >= 1) ? Params[0] : 0);
            return TRUE;

        case L'@':
            if (Intermediate == L' ')
            {
                ULONG Columns = (Count >= 1 && Params[0] != 0) ? Params[0] : 1;
                VtScrollLeft(Console, ScreenBuffer, Columns);
                return TRUE;
            }
            VtInsertCharacters(Console, ScreenBuffer, (Count >= 1) ? Params[0] : 1);
            return TRUE;
        case L'A':
            if (Intermediate == L' ')
            {
                ULONG Columns = (Count >= 1 && Params[0] != 0) ? Params[0] : 1;
                VtScrollRight(Console, ScreenBuffer, Columns);
                return TRUE;
            }
            return FALSE;

        case L'P':
            VtDeleteCharacters(Console, ScreenBuffer, (Count >= 1) ? Params[0] : 1);
            return TRUE;

        case L'X':
            VtEraseCharacters(Console, ScreenBuffer, (Count >= 1) ? Params[0] : 1);
            return TRUE;

        case L'b':
        {
            ULONG Repeat = (Count >= 1 && Params[0] != 0) ? Params[0] : 1;
            VtRepeatCharacter(Console, ScreenBuffer, Repeat);
            return TRUE;
        }

        case L'L':
            VtInsertLines(Console, ScreenBuffer, (Count >= 1) ? Params[0] : 1);
            return TRUE;

        case L'M':
            VtDeleteLines(Console, ScreenBuffer, (Count >= 1) ? Params[0] : 1);
            return TRUE;

        case L'c':
            if (Intermediate == 0)
                return VtHandleDeviceAttributes(Console, 0);
            return FALSE;

        case L't':
        {
            ULONG Action = (Count >= 1) ? Params[0] : 0;

            switch (Action)
            {
                case 8:
                    if (Count >= 3)
                    {
                        ULONG Rows = Params[1];
                        ULONG Cols = Params[2];
                        VtResizeWindow(Console, ScreenBuffer, Rows, Cols);
                        return TRUE;
                    }
                    return TRUE;

                case 18:
                    return VtReportWindowSize(Console, ScreenBuffer, 8);

                case 19:
                    return VtReportWindowSize(Console, ScreenBuffer, 4);

                default:
                    return FALSE;
            }
        }

        case L'n':
        {
            ULONG Query = (Count >= 1) ? Params[0] : 0;
            WCHAR Response[32];
            SIZE_T Len = 0;

            switch (Query)
            {
                case 5:
                    Response[Len++] = L'\x1b';
                    Response[Len++] = L'[';
                    Response[Len++] = L'0';
                    Response[Len++] = L'n';
                    break;

                case 6:
                {
                    SHORT Row = ScreenBuffer->CursorPosition.Y - ScreenBuffer->ViewOrigin.Y;
                    SHORT Col = ScreenBuffer->CursorPosition.X - ScreenBuffer->ViewOrigin.X;

                    if (Row < 0) Row = 0;
                    if (Col < 0) Col = 0;

                    Response[Len++] = L'\x1b';
                    Response[Len++] = L'[';
                    Len += VtAppendNumber(Response + Len,
                                          ARRAYSIZE(Response) - Len,
                                          (ULONG)Row + 1);
                    if (Len < ARRAYSIZE(Response))
                        Response[Len++] = L';';
                    Len += VtAppendNumber(Response + Len,
                                          ARRAYSIZE(Response) - Len,
                                          (ULONG)Col + 1);
                    if (Len < ARRAYSIZE(Response))
                        Response[Len++] = L'R';
                    break;
                }

                default:
                    return FALSE;
            }

            VtSendInputResponse(Console, Response, Len);
            return TRUE;
        }

        case L'S':
        {
            ULONG Lines = (Count >= 1 && Params[0] != 0) ? Params[0] : 1;
            VtScrollRegionUp(Console, ScreenBuffer, Lines);
            return TRUE;
        }

        case L'T':
        {
            ULONG Lines = (Count >= 1 && Params[0] != 0) ? Params[0] : 1;
            VtScrollRegionDown(Console, ScreenBuffer, Lines);
            return TRUE;
        }

        case L'r':
        {
            SHORT Top = 0;
            SHORT Bottom = ScreenBuffer->ScreenBufferSize.Y > 0 ? ScreenBuffer->ScreenBufferSize.Y - 1 : 0;

            if (Count >= 1 && Params[0] != 0)
                Top = (SHORT)max(0L, (LONG)Params[0] - 1);
            if (Count >= 2 && Params[1] != 0)
                Bottom = (SHORT)min((LONG)Bottom, (LONG)Params[1] - 1);

            if (Top < 0)
                Top = 0;
            if (Bottom >= ScreenBuffer->ScreenBufferSize.Y)
                Bottom = ScreenBuffer->ScreenBufferSize.Y - 1;

            if (Bottom < Top)
            {
                Top = 0;
                Bottom = ScreenBuffer->ScreenBufferSize.Y > 0 ? ScreenBuffer->ScreenBufferSize.Y - 1 : 0;
            }

            ScreenBuffer->VtState.ScrollTop = Top;
            ScreenBuffer->VtState.ScrollBottom = Bottom;

            {
                COORD Home;
                Home.X = 0;
                Home.Y = Top;
                ConDrvSetConsoleCursorPosition(Console, ScreenBuffer, &Home);
            }
            return TRUE;
        }

        case L'q':
            if (Intermediate == L' ')
            {
                ULONG Style = (Count >= 1) ? Params[0] : 0;
                if (VtApplyCursorStyle(Console, ScreenBuffer, Style))
                    return TRUE;
            }
            return FALSE;

        default:
            return FALSE;
    }
}

static BOOLEAN
VtHandleEscapeSequence(PCONSOLE Console,
                       PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                       WCHAR Indicator,
                       PCWSTR Buffer,
                       ULONG Length,
                       PULONG Position)
{
    switch (Indicator)
    {
        case L's':
        case L'7':
            VtSaveCursorState(ScreenBuffer);
            return TRUE;

        case L'u':
        case L'8':
            VtRestoreCursorState(Console, ScreenBuffer);
            return TRUE;

        case L'c':
            VtSoftReset(Console, ScreenBuffer);
            return TRUE;

        case L'#':
        {
            WCHAR Selector;

            if (!Position || *Position >= Length)
                return FALSE;

            Selector = Buffer[*Position];
            if (Selector == L'8')
            {
                (*Position)++;
                VtDecaln(Console, ScreenBuffer);
                return TRUE;
            }

            return FALSE;
        }

        case L'(':
        case L')':
        case L'*':
        case L'+':
        case L'-':
        case L'.':
        case L'/':
        {
            UCHAR Slot;

            if (!Position || *Position >= Length)
                return FALSE;

            switch (Indicator)
            {
                case L'(': Slot = VT_CHARSET_SLOT_G0; break;
                case L')':
                case L'-': Slot = VT_CHARSET_SLOT_G1; break;
                case L'*':
                case L'.': Slot = VT_CHARSET_SLOT_G2; break;
                case L'+':
                case L'/': Slot = VT_CHARSET_SLOT_G3; break;
                default: return FALSE;
            }

            if (VtDesignateCharset(ScreenBuffer, Slot, Buffer[*Position]))
            {
                (*Position)++;
                return TRUE;
            }

            return FALSE;
        }

        case L'N':
            ScreenBuffer->VtState.PendingSingleShift = VT_CHARSET_SLOT_G2;
            return TRUE;

        case L'O':
            ScreenBuffer->VtState.PendingSingleShift = VT_CHARSET_SLOT_G3;
            return TRUE;

        case L'n':
            VtSetActiveCharset(ScreenBuffer, VT_CHARSET_SLOT_G2);
            return TRUE;

        case L'o':
            VtSetActiveCharset(ScreenBuffer, VT_CHARSET_SLOT_G3);
            return TRUE;

        default:
            return FALSE;
    }
}

NTSTATUS
NTAPI
ConDrvVtWriteConsole(PCONSOLE Console,
                     PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                     PCWSTR Buffer,
                     ULONG Length,
                     PULONG NumCharsProcessed,
                     PBOOLEAN Handled)
{
    ULONG Pos = 0;
    ULONG SegmentStart = 0;
    ULONG Processed = 0;
    ULONG Params[VT_MAX_PARAMS];
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN AnyHandled = FALSE;
    
    if (!Console || !ScreenBuffer || !Buffer)
    {
        if (NumCharsProcessed) *NumCharsProcessed = 0;
        if (Handled) *Handled = FALSE;
        return STATUS_INVALID_PARAMETER;
    }

    ScreenBuffer->VtState.Active = TRUE;

    while (Pos < Length && NT_SUCCESS(Status))
    {
        WCHAR Ch = Buffer[Pos];

        if (Ch == L'' || Ch == L'')
        {
            Status = VtFlushText(Console,
                                 ScreenBuffer,
                                 Buffer + SegmentStart,
                                 Pos - SegmentStart);
            if (!NT_SUCCESS(Status))
                break;

            VtSetActiveCharset(ScreenBuffer,
                               (Ch == L'\x0e') ? VT_CHARSET_SLOT_G1
                                                 : VT_CHARSET_SLOT_G0);
            Pos++;
            SegmentStart = Pos;
            AnyHandled = TRUE;
            continue;
        }

        if (Ch == L'')
        {
            ULONG EscStart = Pos;

            if (Pos + 1 >= Length)
                break; /* Incomplete sequence, emit as literal later */

            Status = VtFlushText(Console,
                                 ScreenBuffer,
                                 Buffer + SegmentStart,
                                 EscStart - SegmentStart);
            if (!NT_SUCCESS(Status))
                break;

            Pos++;
            Ch = Buffer[Pos++];

            if (Ch == L'[')
            {
                WCHAR Final = 0;
                ULONG Count = 0;
                ULONG Current = 0;
                BOOLEAN HaveCurrent = FALSE;
                WCHAR PrivateIndicator = 0;
                WCHAR Intermediate = 0;

                if (Pos < Length && (Buffer[Pos] == L'?' || Buffer[Pos] == L'>' || Buffer[Pos] == L'<' || Buffer[Pos] == L'='))
                {
                    PrivateIndicator = Buffer[Pos];
                    Pos++;
                }

                while (Pos < Length)
                {
                    WCHAR C = Buffer[Pos];
                    if (C >= L'0' && C <= L'9')
                    {
                        HaveCurrent = TRUE;
                        Current = Current * 10 + (C - L'0');
                        Pos++;
                        continue;
                    }
                    if (C == L';')
                    {
                        if (Count < VT_MAX_PARAMS)
                            Params[Count] = HaveCurrent ? Current : 0;
                        Count++;
                        Current = 0;
                        HaveCurrent = FALSE;
                        Pos++;
                        continue;
                    }

                    if (C >= L' ' && C <= L'/')
                    {
                        Intermediate = C;
                        Pos++;
                        continue;
                    }

                    Final = C;
                    Pos++;
                    break;
                }

                if (Final == 0)
                {
                    /* Incomplete sequence, emit literally */
                    Status = VtFlushText(Console,
                                         ScreenBuffer,
                                         Buffer + EscStart,
                                         Pos - EscStart);
                    SegmentStart = Pos;
                    continue;
                }

                if (HaveCurrent || Count == 0)
                {
                    if (Count < VT_MAX_PARAMS)
                        Params[Count] = HaveCurrent ? Current : 0;
                    Count++;
                }

                if ((PrivateIndicator != 0 &&
                     VtHandlePrivateCsiSequence(Console, ScreenBuffer, PrivateIndicator, Final, Intermediate, Params, Count)) ||
                    (PrivateIndicator == 0 &&
                     VtHandleCsiSequence(Console, ScreenBuffer, Final, Intermediate, Params, Count)))
                {
                    AnyHandled = TRUE;
                    ScreenBuffer = (PTEXTMODE_SCREEN_BUFFER)Console->ActiveBuffer;
                    SegmentStart = Pos;
                    continue;
                }

                /* Unsupported CSI sequence, emit literally */
                Status = VtFlushText(Console,
                                     ScreenBuffer,
                                     Buffer + EscStart,
                                     Pos - EscStart);
                SegmentStart = Pos;
                continue;
            }
            else if (Ch == L']')
            {
                ULONG OscStart = Pos;
                ULONG SearchPos = Pos;
                ULONG TerminatorLen = 0;

                while (SearchPos < Length)
                {
                    WCHAR C = Buffer[SearchPos];
                    if (C == L'\a')
                    {
                        TerminatorLen = 1;
                        break;
                    }
                    if (C == L'\u001b' && (SearchPos + 1) < Length && Buffer[SearchPos + 1] == L'\\')
                    {
                        TerminatorLen = 2;
                        break;
                    }
                    SearchPos++;
                }

                if (TerminatorLen == 0)
                {
                    Status = VtFlushText(Console,
                                         ScreenBuffer,
                                         Buffer + EscStart,
                                         Pos - EscStart);
                    SegmentStart = Pos;
                    continue;
                }

                if (VtHandleOscSequence(Console,
                                        ScreenBuffer,
                                        Buffer + OscStart,
                                        SearchPos - OscStart))
                {
                    AnyHandled = TRUE;
                    Pos = SearchPos + TerminatorLen;
                    ScreenBuffer = (PTEXTMODE_SCREEN_BUFFER)Console->ActiveBuffer;
                    SegmentStart = Pos;
                    continue;
                }

                Status = VtFlushText(Console,
                                     ScreenBuffer,
                                     Buffer + EscStart,
                                     (SearchPos + TerminatorLen) - EscStart);
                Pos = SearchPos + TerminatorLen;
                SegmentStart = Pos;
                continue;
            }
            else
            {
                ULONG NewPos = Pos;
                if (VtHandleEscapeSequence(Console,
                                           ScreenBuffer,
                                           Ch,
                                           Buffer,
                                           Length,
                                           &NewPos))
                {
                    AnyHandled = TRUE;
                    Pos = NewPos;
                    ScreenBuffer = (PTEXTMODE_SCREEN_BUFFER)Console->ActiveBuffer;
                    SegmentStart = Pos;
                    continue;
                }

                /* Unsupported ESC sequence, emit literally */
                Status = VtFlushText(Console,
                                     ScreenBuffer,
                                     Buffer + EscStart,
                                     Pos - EscStart);
                SegmentStart = Pos;
                continue;
            }
        }

        Pos++;
    }

    if (NT_SUCCESS(Status))
    {
        Status = VtFlushText(Console,
                             ScreenBuffer,
                             Buffer + SegmentStart,
                             Length - SegmentStart);
        if (NT_SUCCESS(Status))
        {
            Processed = Length;
            AnyHandled = TRUE;
        }
    }

    if (!NT_SUCCESS(Status))
        AnyHandled = FALSE;

    if (NumCharsProcessed) *NumCharsProcessed = Processed;
    if (Handled) *Handled = AnyHandled;

    return Status;
}
