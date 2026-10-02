/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Open Firmware console (serial terminal) input and output
 */

#include <freeldr.h>

UCHAR MachDefaultTextColor = COLOR_GRAY;

/* Bytes read from the firmware but not yet consumed. */
static UCHAR OfwInputQueue[16];
static ULONG OfwInputHead, OfwInputTail;
/* Second half of an extended key: (KEY_EXTENDED, scan code). */
static BOOLEAN OfwExtendedPending;
static UCHAR OfwExtendedCode;

VOID
OfwConsPutChar(int Ch)
{
    CHAR Buffer[2];
    ULONG Length = 0;

    if (OfwStdout == OFW_INVALID)
        return;

    /* Terminals need CR LF. */
    if (Ch == '\n')
        Buffer[Length++] = '\r';
    Buffer[Length++] = (CHAR)Ch;
    OfwWrite(OfwStdout, Buffer, Length);
}

VOID
OfwConsWriteString(_In_z_ PCSTR String)
{
    while (*String)
        OfwConsPutChar(*String++);
}

static
BOOLEAN
OfwPollByte(VOID)
{
    UCHAR Byte;

    if (((OfwInputTail + 1) % sizeof(OfwInputQueue)) == OfwInputHead)
        return TRUE;
    if (OfwStdin == OFW_INVALID || OfwRead(OfwStdin, &Byte, 1) != 1)
        return FALSE;
    OfwInputQueue[OfwInputTail] = Byte;
    OfwInputTail = (OfwInputTail + 1) % sizeof(OfwInputQueue);
    return TRUE;
}

static
BOOLEAN
OfwPeekByte(_In_ ULONG Index, _Out_ PUCHAR Byte, _In_ ULONG WaitMs)
{
    ULONG Start = OfwMilliseconds();

    for (;;)
    {
        ULONG Count = (OfwInputTail + sizeof(OfwInputQueue) - OfwInputHead) % sizeof(OfwInputQueue);

        if (Index < Count)
        {
            *Byte = OfwInputQueue[(OfwInputHead + Index) % sizeof(OfwInputQueue)];
            return TRUE;
        }
        if (!OfwPollByte() && (OfwMilliseconds() - Start) >= WaitMs)
            return FALSE;
    }
}

static
VOID
OfwDropBytes(_In_ ULONG Count)
{
    OfwInputHead = (OfwInputHead + Count) % sizeof(OfwInputQueue);
}

BOOLEAN
OfwConsKbHit(VOID)
{
    if (OfwExtendedPending || OfwInputHead != OfwInputTail)
        return TRUE;
    return OfwPollByte();
}

/* Decode a VT100/xterm escape sequence starting at the queue head. */
static
UCHAR
OfwDecodeEscape(VOID)
{
    UCHAR Introducer, Final, Digit;
    ULONG Number = 0, Used = 2;

    if (!OfwPeekByte(1, &Introducer, 30) || (Introducer != '[' && Introducer != 'O'))
    {
        OfwDropBytes(1);
        return 0;
    }

    for (;;)
    {
        if (!OfwPeekByte(Used, &Final, 30))
        {
            OfwDropBytes(Used);
            return 0;
        }
        Used++;
        if (Final < '0' || Final > '9')
            break;
        Digit = Final - '0';
        Number = Number * 10 + Digit;
    }
    OfwDropBytes(Used);

    switch (Final)
    {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        case 'P': return KEY_F1;
        case 'Q': return KEY_F2;
        case 'R': return KEY_F3;
        case 'S': return KEY_F4;
        case '~':
            switch (Number)
            {
                case 1: return KEY_HOME;
                case 3: return KEY_DELETE;
                case 4: return KEY_END;
                case 15: return KEY_F5;
                case 17: return KEY_F6;
                case 18: return KEY_F7;
                case 19: return KEY_F8;
                case 20: return KEY_F9;
                case 21: return KEY_F10;
            }
            break;
    }
    return 0;
}

int
OfwConsGetCh(VOID)
{
    UCHAR Byte, Scan;

    if (OfwExtendedPending)
    {
        OfwExtendedPending = FALSE;
        return OfwExtendedCode;
    }

    while (!OfwPeekByte(0, &Byte, 0))
        NOTHING;

    if (Byte == KEY_ESC)
    {
        UCHAR Next;

        /* A lone ESC is the escape key; ESC [ or ESC O starts a sequence. */
        if (!OfwPeekByte(1, &Next, 30))
        {
            OfwDropBytes(1);
            return KEY_ESC;
        }
        Scan = OfwDecodeEscape();
        if (Scan == 0)
            return KEY_ESC;
        OfwExtendedPending = TRUE;
        OfwExtendedCode = Scan;
        return KEY_EXTENDED;
    }

    OfwDropBytes(1);
    if (Byte == 0x7F)
        return KEY_BACKSPACE;
    if (Byte == '\n')
        return KEY_ENTER;
    return Byte;
}
