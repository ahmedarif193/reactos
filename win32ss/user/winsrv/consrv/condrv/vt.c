/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/condrv/vt.c
 * PURPOSE:         Virtual Terminal processing (phase 1 implementation)
 */

/* INCLUDES *******************************************************************/

#include <consrv.h>
#include "../conoutput.h"

#define VT_MAX_PARAMS 16

#define FG_ATTR_MASK (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define BG_ATTR_MASK (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY)

#define VT_PRIVMODE_MOUSE_BUTTON_TRACKING   0x00000001 /* CSI ?1002 h/l */
#define VT_PRIVMODE_MOUSE_SGR_EXTENDED       0x00000002 /* CSI ?1006 h/l */
#define VT_PRIVMODE_BRACKETED_PASTE          0x00000004 /* CSI ?2004 h/l */
#define VT_PRIVMODE_META_SENDS_ESCAPE        0x00000008 /* CSI ?1036 h/l */
#define VT_PRIVMODE_ALTERNATE_SCREEN_BUFFER  0x00000010 /* CSI ?1049 h/l */

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
    AltInfo.ScreenBufferSize = PrimaryBuffer->ScreenBufferSize;
    AltInfo.ViewSize         = PrimaryBuffer->ViewSize;
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
    AltBuffer->VtState.DefaultCursorInfo = PrimaryBuffer->VtState.DefaultCursorInfo;
    AltBuffer->VtState.CurrentCursorStyle = PrimaryBuffer->VtState.CurrentCursorStyle;

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

    ConDrvSetConsoleActiveScreenBuffer(Console, (PCONSOLE_SCREEN_BUFFER)PrimaryBuffer);
    ConDrvSetConsoleCursorInfo(Console, PrimaryBuffer, &PrimaryBuffer->CursorInfo);
    ConDrvSetConsoleCursorPosition(Console, PrimaryBuffer, &PrimaryBuffer->CursorPosition);

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

    if (Length == 0) return STATUS_SUCCESS;

    OriginalAttrib = ScreenBuffer->ScreenDefaultAttrib;
    ScreenBuffer->ScreenDefaultAttrib = ScreenBuffer->VtState.CurrentAttributes;

    Status = TermWriteStream(Console,
                             ScreenBuffer,
                             (PWCHAR)Text,
                             Length,
                             TRUE);

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

static VOID
VtApplySgr(PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
           const ULONG *Params,
           ULONG Count)
{
    ULONG i;
    USHORT Attr = ScreenBuffer->VtState.CurrentAttributes;
    const USHORT DefaultAttr = ScreenBuffer->VtState.SavedAttributes;

    if (Count == 0)
    {
        Attr = DefaultAttr;
        ScreenBuffer->VtState.CurrentAttributes = Attr;
        return;
    }

    for (i = 0; i < Count; ++i)
    {
        ULONG Code = Params[i];
        switch (Code)
        {
            case 0:
                Attr = DefaultAttr;
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
                ULONG idx = Code - 30;
                Attr &= ~FG_ATTR_MASK;
                Attr |= VtAnsiFgMap[idx];
                break;
            }
            case 39:
                Attr &= ~FG_ATTR_MASK;
                Attr |= (DefaultAttr & FG_ATTR_MASK);
                break;
            case 40: case 41: case 42: case 43:
            case 44: case 45: case 46: case 47:
            {
                ULONG idx = Code - 40;
                Attr &= ~BG_ATTR_MASK;
                Attr |= VtAnsiBgMap[idx];
                break;
            }
            case 49:
                Attr &= ~BG_ATTR_MASK;
                Attr |= (DefaultAttr & BG_ATTR_MASK);
                break;
            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
            {
                ULONG idx = Code - 90;
                Attr &= ~FG_ATTR_MASK;
                Attr |= VtAnsiFgMap[idx] | FOREGROUND_INTENSITY;
                break;
            }
            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
            {
                ULONG idx = Code - 100;
                Attr &= ~BG_ATTR_MASK;
                Attr |= VtAnsiBgMap[idx] | BACKGROUND_INTENSITY;
                break;
            }
            default:
                break;
        }
    }

    ScreenBuffer->VtState.CurrentAttributes = Attr;
}

static VOID
VtEraseLineToEnd(PCONSOLE Console,
                 PTEXTMODE_SCREEN_BUFFER ScreenBuffer)
{
    SHORT X;
    SHORT Y = ScreenBuffer->CursorPosition.Y;
    USHORT Attr = ScreenBuffer->VtState.CurrentAttributes;
    SMALL_RECT Region;
    PCHAR_INFO Ptr;

    if (Y < 0 || Y >= ScreenBuffer->ScreenBufferSize.Y)
        return;

    Ptr = ConioCoordToPointer(ScreenBuffer,
                              ScreenBuffer->CursorPosition.X,
                              Y);

    for (X = ScreenBuffer->CursorPosition.X;
         X < ScreenBuffer->ScreenBufferSize.X;
         ++X, ++Ptr)
    {
        Ptr->Char.UnicodeChar = L' ';
        Ptr->Attributes = Attr;
    }

    Region.Left   = ScreenBuffer->CursorPosition.X;
    Region.Right  = ScreenBuffer->ScreenBufferSize.X - 1;
    Region.Top    = Y;
    Region.Bottom = Y;
    TermDrawRegion(Console, &Region);
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

    if (Mode != 2)
        return; /* Only support CSI 2 J for now */

    for (Y = 0; Y < ScreenBuffer->ScreenBufferSize.Y; ++Y)
    {
        PCHAR_INFO Ptr = ConioCoordToPointer(ScreenBuffer, 0, Y);
        for (X = 0; X < ScreenBuffer->ScreenBufferSize.X; ++X, ++Ptr)
        {
            Ptr->Char.UnicodeChar = L' ';
            Ptr->Attributes = Attr;
        }
    }

    Region.Left   = 0;
    Region.Top    = 0;
    Region.Right  = ScreenBuffer->ScreenBufferSize.X - 1;
    Region.Bottom = ScreenBuffer->ScreenBufferSize.Y - 1;
    TermDrawRegion(Console, &Region);

    Origin.X = Origin.Y = 0;
    ConDrvSetConsoleCursorPosition(Console, ScreenBuffer, &Origin);
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
    ScreenBuffer->VtState.PrivateModes = 0;
    ScreenBuffer->VtState.AlternateBuffer = NULL;
    ScreenBuffer->VtState.PrimaryBuffer = NULL;
    ScreenBuffer->VtState.PrimaryCursorInfo = ScreenBuffer->CursorInfo;
    ScreenBuffer->VtState.PrimaryCursorPos = Zero;
    ScreenBuffer->VtState.PrimaryViewOrigin = Zero;
    ScreenBuffer->VtState.PrimaryVirtualY = 0;
    ScreenBuffer->VtState.DefaultCursorInfo = ScreenBuffer->CursorInfo;
    ScreenBuffer->VtState.CurrentCursorStyle = 0;
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
VtHandlePrivateCsiSequence(PCONSOLE Console,
                           PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
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
            VtApplySgr(ScreenBuffer, Params, Count);
            return TRUE;

        case L'H':
        case L'f':
            VtHandleCursorPosition(Console, ScreenBuffer, Params, Count);
            return TRUE;

        case L'J':
            VtEraseDisplay(Console, ScreenBuffer, (Count >= 1) ? Params[0] : 0);
            return TRUE;

        case L'K':
            VtEraseLineToEnd(Console, ScreenBuffer);
            return TRUE;

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
                       WCHAR Final)
{
    switch (Final)
    {
        case L's':
            ScreenBuffer->VtState.SavedCursorPos = ScreenBuffer->CursorPosition;
            ScreenBuffer->VtState.SavedAttributes = ScreenBuffer->VtState.CurrentAttributes;
            ScreenBuffer->VtState.CursorSaved = TRUE;
            return TRUE;

        case L'u':
            if (ScreenBuffer->VtState.CursorSaved)
            {
                ConDrvSetConsoleCursorPosition(Console,
                                               ScreenBuffer,
                                               &ScreenBuffer->VtState.SavedCursorPos);
                ScreenBuffer->VtState.CurrentAttributes = ScreenBuffer->VtState.SavedAttributes;
            }
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
                BOOLEAN Private = FALSE;
                WCHAR Intermediate = 0;

                if (Pos < Length && (Buffer[Pos] == L'?' || Buffer[Pos] == L'>' || Buffer[Pos] == L'<' || Buffer[Pos] == L'='))
                {
                    Private = TRUE;
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

                if ((Private &&
                     VtHandlePrivateCsiSequence(Console, ScreenBuffer, Final, Intermediate, Params, Count)) ||
                    (!Private &&
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
            else
            {
                if (VtHandleEscapeSequence(Console, ScreenBuffer, Ch))
                {
                    AnyHandled = TRUE;
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
