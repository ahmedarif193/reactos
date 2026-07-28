/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     PTY/PTS emulation - terminal device pairs for guest I/O
 * COPYRIGHT:   Copyright 2025-2026 Ahmed Arif
 *
 * Implements Unix-style pseudo-terminal pairs (master/slave) with full
 * termios processing. The master side is exposed to the host via IOCTLs;
 * the slave side is presented to the guest kernel.
 *
 * Data flow:
 *   Host writes -> RosvPtyMasterWrite -> termios input processing -> InputBuf -> guest reads
 *   Guest writes -> RosvPtySlaveWrite -> termios output processing -> OutputBuf -> host reads
 */

#include <rosv/rosv.h>
#include <rosv/pty.h>
#include <rosv/virtio_console.h>

/* ---- Helpers ------------------------------------------------------------ */

/* Ring buffer put: returns TRUE if byte was stored, FALSE if full */
static BOOLEAN
PtyInputPut(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ UCHAR Byte)
{
    if (Pty->InputCount >= ROSV_PTY_INPUT_SIZE)
        return FALSE;

    Pty->InputBuf[Pty->InputHead] = Byte;
    Pty->InputHead = (Pty->InputHead + 1) % ROSV_PTY_INPUT_SIZE;
    Pty->InputCount++;
    return TRUE;
}

/* Ring buffer get from input: returns TRUE if byte was read, FALSE if empty */
static BOOLEAN
PtyInputGet(
    _Inout_ PROSV_PTY_STATE Pty,
    _Out_ PUCHAR Byte)
{
    if (Pty->InputCount == 0)
        return FALSE;

    *Byte = Pty->InputBuf[Pty->InputTail];
    Pty->InputTail = (Pty->InputTail + 1) % ROSV_PTY_INPUT_SIZE;
    Pty->InputCount--;
    return TRUE;
}

static VOID
PtyInputUngetRange(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    KIRQL OldIrql;
    ULONG i;

    if (Length == 0)
        return;

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);

    for (i = Length; i > 0 && Pty->InputCount < ROSV_PTY_INPUT_SIZE; i--)
    {
        Pty->InputTail = (Pty->InputTail + ROSV_PTY_INPUT_SIZE - 1) % ROSV_PTY_INPUT_SIZE;
        Pty->InputBuf[Pty->InputTail] = Data[i - 1];
        Pty->InputCount++;
    }

    KeReleaseSpinLock(&Pty->Lock, OldIrql);
}

static ULONG
PtyGetOutputFreeSpace(
    _Inout_ PROSV_PTY_STATE Pty)
{
    KIRQL OldIrql;
    ULONG FreeSpace;

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);
    FreeSpace = ROSV_PTY_OUTPUT_SIZE - Pty->OutputCount;
    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    return FreeSpace;
}

/*
 * Recalculate OutputTail from the minimum of all active reader cursors.
 * OutputCount is the distance from OutputTail to OutputHead.
 * Must be called under Pty->Lock.
 */
static VOID
PtyRecalcOutputTail(
    _Inout_ PROSV_PTY_STATE Pty)
{
    ULONG64 MinCursor = Pty->OutputHead;
    ULONG i;
    BOOLEAN FoundActive = FALSE;

    for (i = 0; i < ROSV_PTY_MAX_READERS; i++)
    {
        if (Pty->Readers[i].Active)
        {
            if (!FoundActive || Pty->Readers[i].ReadCursor < MinCursor)
            {
                MinCursor = Pty->Readers[i].ReadCursor;
            }
            FoundActive = TRUE;
        }
    }

    Pty->OutputTail = MinCursor;
    if (FoundActive)
    {
        Pty->OutputCount = (ULONG)(Pty->OutputHead - Pty->OutputTail);
    }
    else
    {
        /* No active readers: nothing to retain */
        Pty->OutputTail = Pty->OutputHead;
        Pty->OutputCount = 0;
    }
}

/* Ring buffer put to output: returns TRUE if byte was stored, FALSE if full.
 * The buffer is full when OutputHead would collide with the slowest reader. */
static BOOLEAN
PtyOutputPut(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ UCHAR Byte)
{
    if (Pty->OutputCount >= ROSV_PTY_OUTPUT_SIZE)
        return FALSE;

    Pty->OutputBuf[Pty->OutputHead % ROSV_PTY_OUTPUT_SIZE] = Byte;
    Pty->OutputHead++;
    Pty->OutputCount++;
    return TRUE;
}

/* Per-reader get from output: returns TRUE if byte was read for this reader.
 * Does NOT advance the global OutputTail -- only advances this reader's cursor. */
static BOOLEAN
PtyOutputGetForReader(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ ULONG ReaderIndex,
    _Out_ PUCHAR Byte)
{
    PROSV_PTY_READER Reader;

    if (ReaderIndex >= ROSV_PTY_MAX_READERS)
        return FALSE;

    Reader = &Pty->Readers[ReaderIndex];
    if (!Reader->Active)
        return FALSE;

    /* Nothing to read if cursor is at write head */
    if (Reader->ReadCursor == Pty->OutputHead)
        return FALSE;

    *Byte = Pty->OutputBuf[Reader->ReadCursor % ROSV_PTY_OUTPUT_SIZE];
    Reader->ReadCursor++;
    return TRUE;
}

/* Echo a byte to the output buffer (for ECHO flag) */
static VOID
PtyEcho(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ UCHAR Ch)
{
    UCHAR OutBuf[4];
    ULONG OutLen;

    /* Process echo through output flags so CR/LF mapping applies */
    OutLen = RosvPtyTermiosProcessOutput(Pty, Ch, OutBuf, sizeof(OutBuf));
    for (ULONG i = 0; i < OutLen; i++)
    {
        PtyOutputPut(Pty, OutBuf[i]);
    }
}

/* Echo a control character as ^X */
static VOID
PtyEchoCtrl(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ UCHAR Ch)
{
    PtyEcho(Pty, '^');
    PtyEcho(Pty, (UCHAR)(Ch + '@'));
}

/* Erase one character with backspace-space-backspace */
static VOID
PtyEchoErase(
    _Inout_ PROSV_PTY_STATE Pty)
{
    PtyOutputPut(Pty, '\b');
    PtyOutputPut(Pty, ' ');
    PtyOutputPut(Pty, '\b');
}

/* ---- Termios defaults --------------------------------------------------- */

VOID
RosvPtyTermiosSetDefaults(
    _Out_ PROSV_TERMIOS Termios)
{
    RtlZeroMemory(Termios, sizeof(*Termios));

    /* Input flags: map CR to NL, enable XON/XOFF */
    Termios->c_iflag = ROSV_ICRNL | ROSV_IXON;

    /* Output flags: post-processing, NL -> CR+NL */
    Termios->c_oflag = ROSV_OPOST | ROSV_ONLCR;

    /* Control flags: 8-bit chars, receiver enabled, ignore modem */
    Termios->c_cflag = ROSV_CS8 | ROSV_CREAD | ROSV_CLOCAL;

    /* Local flags: echo, canonical mode, signals, erase/kill echo, extensions */
    Termios->c_lflag = ROSV_ECHO | ROSV_ECHOE | ROSV_ECHOK |
                       ROSV_ICANON | ROSV_ISIG | ROSV_IEXTEN;

    /* Standard control characters */
    Termios->c_cc[ROSV_VINTR]    = ROSV_CTRL('C');
    Termios->c_cc[ROSV_VQUIT]    = ROSV_CTRL('\\');
    Termios->c_cc[ROSV_VERASE]   = 0x7F;   /* DEL */
    Termios->c_cc[ROSV_VKILL]    = ROSV_CTRL('U');
    Termios->c_cc[ROSV_VEOF]     = ROSV_CTRL('D');
    Termios->c_cc[ROSV_VTIME]    = 0;
    Termios->c_cc[ROSV_VMIN]     = 1;
    Termios->c_cc[ROSV_VSTART]   = ROSV_CTRL('Q');
    Termios->c_cc[ROSV_VSTOP]    = ROSV_CTRL('S');
    Termios->c_cc[ROSV_VSUSP]    = ROSV_CTRL('Z');
    Termios->c_cc[ROSV_VEOL]     = 0;
    Termios->c_cc[ROSV_VREPRINT] = ROSV_CTRL('R');
    Termios->c_cc[ROSV_VDISCARD] = ROSV_CTRL('O');
    Termios->c_cc[ROSV_VWERASE]  = ROSV_CTRL('W');
    Termios->c_cc[ROSV_VLNEXT]   = ROSV_CTRL('V');
    Termios->c_cc[ROSV_VEOL2]    = 0;

    /* Default baud rate: 38400 (standard for PTYs) */
    Termios->c_ispeed = 38400;
    Termios->c_ospeed = 38400;

    ROSV_TRACE("PTY termios defaults set: iflag=0x%X oflag=0x%X cflag=0x%X lflag=0x%X",
               Termios->c_iflag, Termios->c_oflag,
               Termios->c_cflag, Termios->c_lflag);
}

/* ---- Termios input processing ------------------------------------------- */

/*
 * RosvPtyTermiosProcessInput - Process one input byte through termios flags.
 *
 * Handles:
 *   - ISIG: signal generation (^C -> SIGINT, ^\ -> SIGQUIT, ^Z -> SIGTSTP)
 *   - ISTRIP: strip bit 7
 *   - IGNCR: ignore CR
 *   - ICRNL: map CR -> NL
 *   - INLCR: map NL -> CR
 *   - IUCLC: map uppercase to lowercase
 *   - IXON/IXOFF: XON/XOFF flow control
 *   - ICANON: canonical line editing (VERASE, VKILL, VEOF)
 *   - ECHO: echo processed character to output
 *
 * Returns: number of bytes placed into InputBuf (0 if consumed/buffered, 1+ if delivered).
 * In ICANON mode, bytes accumulate in LineBuf until a line delimiter
 * (NL, EOF, EOL), then the entire line is copied to InputBuf.
 */
ULONG
RosvPtyTermiosProcessInput(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ UCHAR Ch)
{
    PROSV_TERMIOS T = &Pty->Termios;
    ULONG BytesQueued = 0;
    BOOLEAN IsCanon = (T->c_lflag & ROSV_ICANON) != 0;
    BOOLEAN DoEcho = (T->c_lflag & ROSV_ECHO) != 0;

    /* ISTRIP: strip high bit */
    if (T->c_iflag & ROSV_ISTRIP)
        Ch &= 0x7F;

    /* IXON: XON/XOFF output flow control */
    if (T->c_iflag & ROSV_IXON)
    {
        if (Ch == T->c_cc[ROSV_VSTOP] && T->c_cc[ROSV_VSTOP] != 0)
        {
            Pty->OutputStopped = TRUE;
            ROSV_TRACE("PTY %u: XOFF received, output stopped", Pty->PtyIndex);
            return 0;
        }
        if (Ch == T->c_cc[ROSV_VSTART] && T->c_cc[ROSV_VSTART] != 0)
        {
            Pty->OutputStopped = FALSE;
            ROSV_TRACE("PTY %u: XON received, output resumed", Pty->PtyIndex);
            return 0;
        }
        /* IXANY: any character restarts output */
        if ((T->c_iflag & ROSV_IXANY) && Pty->OutputStopped)
        {
            Pty->OutputStopped = FALSE;
            ROSV_TRACE("PTY %u: IXANY - output resumed by char 0x%02X", Pty->PtyIndex, Ch);
        }
    }

    /* ISIG: signal-generating characters */
    if (T->c_lflag & ROSV_ISIG)
    {
        ULONG Signal = 0;

        if (Ch == T->c_cc[ROSV_VINTR] && T->c_cc[ROSV_VINTR] != 0)
            Signal = ROSV_SIGINT;
        else if (Ch == T->c_cc[ROSV_VQUIT] && T->c_cc[ROSV_VQUIT] != 0)
            Signal = ROSV_SIGQUIT;
        else if (Ch == T->c_cc[ROSV_VSUSP] && T->c_cc[ROSV_VSUSP] != 0)
            Signal = ROSV_SIGTSTP;

        if (Signal != 0)
        {
            Pty->PendingSignal = Signal;
            ROSV_TRACE("PTY %u: signal %u generated by char 0x%02X", Pty->PtyIndex, Signal, Ch);

            /* Flush input/output unless NOFLSH */
            if (!(T->c_lflag & ROSV_NOFLSH))
            {
                Pty->InputHead = Pty->InputTail = Pty->InputCount = 0;
                Pty->LineLen = 0;
                ROSV_TRACE("PTY %u: input flushed (NOFLSH not set)", Pty->PtyIndex);
            }

            /* Echo the signal character */
            if (DoEcho)
            {
                if (T->c_lflag & ROSV_ECHOCTL)
                    PtyEchoCtrl(Pty, Ch);
                PtyEcho(Pty, '\n');
            }

            return 0;
        }
    }

    /* IGNCR: discard CR */
    if ((T->c_iflag & ROSV_IGNCR) && Ch == '\r')
        return 0;

    /* ICRNL: map CR to NL */
    if ((T->c_iflag & ROSV_ICRNL) && Ch == '\r')
        Ch = '\n';

    /* INLCR: map NL to CR */
    if ((T->c_iflag & ROSV_INLCR) && Ch == '\n')
        Ch = '\r';

    /* IUCLC: map uppercase to lowercase */
    if ((T->c_iflag & ROSV_IUCLC) && Ch >= 'A' && Ch <= 'Z')
        Ch = Ch + ('a' - 'A');

    /* ---- Canonical mode line editing ---- */
    if (IsCanon)
    {
        /* VERASE: erase last character */
        if (Ch == T->c_cc[ROSV_VERASE] && T->c_cc[ROSV_VERASE] != 0)
        {
            if (Pty->LineLen > 0)
            {
                UCHAR ErasedCh = Pty->LineBuf[Pty->LineLen - 1];
                Pty->LineLen--;

                if (DoEcho && (T->c_lflag & ROSV_ECHOE))
                {
                    /* Erase a control char echo (^X = 2 columns) */
                    if (ErasedCh < 0x20 && (T->c_lflag & ROSV_ECHOCTL))
                    {
                        PtyEchoErase(Pty);
                        PtyEchoErase(Pty);
                    }
                    else
                    {
                        PtyEchoErase(Pty);
                    }
                }
            }
            return 0;
        }

        /* VKILL: erase entire line */
        if (Ch == T->c_cc[ROSV_VKILL] && T->c_cc[ROSV_VKILL] != 0)
        {
            if (DoEcho && (T->c_lflag & ROSV_ECHOK))
            {
                /* Erase each character visually */
                while (Pty->LineLen > 0)
                {
                    UCHAR ErasedCh = Pty->LineBuf[Pty->LineLen - 1];
                    Pty->LineLen--;
                    if (ErasedCh < 0x20 && (T->c_lflag & ROSV_ECHOCTL))
                    {
                        PtyEchoErase(Pty);
                        PtyEchoErase(Pty);
                    }
                    else
                    {
                        PtyEchoErase(Pty);
                    }
                }
            }
            Pty->LineLen = 0;
            return 0;
        }

        /* VWERASE: erase last word (IEXTEN) */
        if ((T->c_lflag & ROSV_IEXTEN) &&
            Ch == T->c_cc[ROSV_VWERASE] && T->c_cc[ROSV_VWERASE] != 0)
        {
            /* Skip trailing spaces */
            while (Pty->LineLen > 0 && Pty->LineBuf[Pty->LineLen - 1] == ' ')
            {
                Pty->LineLen--;
                if (DoEcho && (T->c_lflag & ROSV_ECHOE))
                    PtyEchoErase(Pty);
            }
            /* Erase word characters */
            while (Pty->LineLen > 0 && Pty->LineBuf[Pty->LineLen - 1] != ' ')
            {
                UCHAR ErasedCh = Pty->LineBuf[Pty->LineLen - 1];
                Pty->LineLen--;
                if (DoEcho && (T->c_lflag & ROSV_ECHOE))
                {
                    if (ErasedCh < 0x20 && (T->c_lflag & ROSV_ECHOCTL))
                    {
                        PtyEchoErase(Pty);
                        PtyEchoErase(Pty);
                    }
                    else
                    {
                        PtyEchoErase(Pty);
                    }
                }
            }
            return 0;
        }

        /* VEOF: end-of-file; deliver current line without the EOF char */
        if (Ch == T->c_cc[ROSV_VEOF] && T->c_cc[ROSV_VEOF] != 0)
        {
            for (ULONG i = 0; i < Pty->LineLen; i++)
            {
                if (PtyInputPut(Pty, Pty->LineBuf[i]))
                    BytesQueued++;
            }
            ROSV_TRACE("PTY %u: EOF, delivered %u bytes from line buf", Pty->PtyIndex, BytesQueued);
            Pty->LineLen = 0;
            return BytesQueued;
        }

        /* Line delimiter: NL or EOL or EOL2 -> deliver line including delimiter */
        if (Ch == '\n' ||
            (Ch == T->c_cc[ROSV_VEOL] && T->c_cc[ROSV_VEOL] != 0) ||
            (Ch == T->c_cc[ROSV_VEOL2] && T->c_cc[ROSV_VEOL2] != 0))
        {
            /* Echo the newline */
            if (DoEcho || (T->c_lflag & ROSV_ECHONL))
                PtyEcho(Pty, '\n');

            /* Add the delimiter to the line */
            if (Pty->LineLen < ROSV_PTY_LINE_MAX)
                Pty->LineBuf[Pty->LineLen++] = Ch;

            /* Deliver the complete line to InputBuf */
            for (ULONG i = 0; i < Pty->LineLen; i++)
            {
                if (PtyInputPut(Pty, Pty->LineBuf[i]))
                    BytesQueued++;
            }
            Pty->LineLen = 0;
            return BytesQueued;
        }

        /* Regular character: buffer in LineBuf */
        if (Pty->LineLen < ROSV_PTY_LINE_MAX)
        {
            Pty->LineBuf[Pty->LineLen++] = Ch;

            /* Echo */
            if (DoEcho)
            {
                if (Ch < 0x20 && Ch != '\t' && (T->c_lflag & ROSV_ECHOCTL))
                    PtyEchoCtrl(Pty, Ch);
                else
                    PtyEcho(Pty, Ch);
            }
        }
        else
        {
            ROSV_WARN("PTY %u: canonical line buffer full (%u), discarding char 0x%02X",
                      Pty->PtyIndex, Pty->LineLen, Ch);
        }

        return 0;
    }

    /* ---- Raw mode: deliver byte directly to InputBuf ---- */
    if (DoEcho)
    {
        if (Ch < 0x20 && Ch != '\t' && Ch != '\n' && (T->c_lflag & ROSV_ECHOCTL))
            PtyEchoCtrl(Pty, Ch);
        else
            PtyEcho(Pty, Ch);
    }

    if (PtyInputPut(Pty, Ch))
        BytesQueued = 1;
    else
        ROSV_WARN("PTY %u: input buffer full, dropping char 0x%02X", Pty->PtyIndex, Ch);

    return BytesQueued;
}

/* ---- Termios output processing ------------------------------------------ */

/*
 * RosvPtyTermiosProcessOutput - Process one output byte through termios flags.
 *
 * Handles:
 *   - OPOST: enable output processing
 *   - ONLCR: map NL -> CR+NL
 *   - OCRNL: map CR -> NL
 *   - ONOCR: don't output CR at column 0 (not tracked, simplified)
 *   - ONLRET: NL performs CR function
 *   - OLCUC: map lowercase to uppercase
 *
 * Returns: number of bytes written to OutBuf.
 */
ULONG
RosvPtyTermiosProcessOutput(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ UCHAR Ch,
    _Out_writes_(MaxOut) UCHAR *OutBuf,
    _In_ ULONG MaxOut)
{
    PROSV_TERMIOS T = &Pty->Termios;

    if (MaxOut == 0)
        return 0;

    /* No output processing? Pass through. */
    if (!(T->c_oflag & ROSV_OPOST))
    {
        OutBuf[0] = Ch;
        return 1;
    }

    /* OLCUC: map lowercase to uppercase */
    if ((T->c_oflag & ROSV_OLCUC) && Ch >= 'a' && Ch <= 'z')
        Ch = Ch - ('a' - 'A');

    /* ONLCR: NL -> CR+NL */
    if ((T->c_oflag & ROSV_ONLCR) && Ch == '\n')
    {
        if (MaxOut >= 2)
        {
            OutBuf[0] = '\r';
            OutBuf[1] = '\n';
            return 2;
        }
        OutBuf[0] = '\n';
        return 1;
    }

    /* OCRNL: CR -> NL */
    if ((T->c_oflag & ROSV_OCRNL) && Ch == '\r')
    {
        OutBuf[0] = '\n';
        return 1;
    }

    OutBuf[0] = Ch;
    return 1;
}

/* ---- PTY Manager -------------------------------------------------------- */

NTSTATUS
RosvPtyManagerInitialize(
    _Out_ PROSV_PTY_MANAGER Manager)
{
    ROSV_TRACE("RosvPtyManagerInitialize: initializing PTY manager (%u max PTYs)",
               ROSV_PTY_MAX);

    RtlZeroMemory(Manager, sizeof(*Manager));
    KeInitializeSpinLock(&Manager->Lock);

    ROSV_TRACE("RosvPtyManagerInitialize: PTY manager ready");
    return STATUS_SUCCESS;
}

VOID
RosvPtyManagerDestroy(
    _Inout_ PROSV_PTY_MANAGER Manager)
{
    ULONG i;

    ROSV_TRACE("RosvPtyManagerDestroy: destroying PTY manager (allocated=%u)",
               Manager->AllocatedCount);

    for (i = 0; i < ROSV_PTY_MAX; i++)
    {
        if (Manager->Ptys[i].Flags & RosvPtyFlagAllocated)
        {
            ROSV_TRACE("RosvPtyManagerDestroy: cleaning up PTY %u", i);
            Manager->Ptys[i].Flags = RosvPtyFlagNone;
        }
    }

    Manager->AllocatedCount = 0;
    ROSV_TRACE("RosvPtyManagerDestroy: done");
}

/* ---- PTY Create/Destroy ------------------------------------------------- */

NTSTATUS
RosvPtyCreate(
    _Inout_ PROSV_PTY_MANAGER Manager,
    _In_ USHORT InitialRows,
    _In_ USHORT InitialCols,
    _Out_ PULONG PtyIndex,
    _Out_ PULONG ReaderIndex)
{
    ULONG i;
    PROSV_PTY_STATE Pty;
    KIRQL OldIrql;

    if (Manager == NULL || PtyIndex == NULL || ReaderIndex == NULL)
    {
        ROSV_ERR("RosvPtyCreate: invalid parameters (Manager=%p PtyIndex=%p ReaderIndex=%p)",
                 Manager, PtyIndex, ReaderIndex);
        return STATUS_INVALID_PARAMETER;
    }

    *PtyIndex = (ULONG)-1;
    *ReaderIndex = (ULONG)-1;

    ROSV_TRACE("RosvPtyCreate: rows=%u cols=%u (allocated=%u/%u)",
               InitialRows, InitialCols, Manager->AllocatedCount, ROSV_PTY_MAX);

    KeAcquireSpinLock(&Manager->Lock, &OldIrql);

    if (Manager->AllocatedCount >= ROSV_PTY_MAX)
    {
        KeReleaseSpinLock(&Manager->Lock, OldIrql);
        ROSV_ERR("RosvPtyCreate: no free PTY slots (max=%u)", ROSV_PTY_MAX);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Find first free slot */
    for (i = 0; i < ROSV_PTY_MAX; i++)
    {
        if (!(Manager->Ptys[i].Flags & RosvPtyFlagAllocated))
            break;
    }

    if (i >= ROSV_PTY_MAX)
    {
        KeReleaseSpinLock(&Manager->Lock, OldIrql);
        ROSV_ERR("RosvPtyCreate: no free slot found despite count=%u",
                 Manager->AllocatedCount);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Pty = &Manager->Ptys[i];
    RtlZeroMemory(Pty, sizeof(*Pty));

    Pty->PtyIndex = i;
    Pty->Flags = RosvPtyFlagAllocated | RosvPtyFlagMasterOpen;
    Pty->BoundVconPort = (ULONG)-1;
    Pty->BoundVconState = NULL;

    /* Set default terminal size */
    Pty->Winsize.ws_row = (InitialRows != 0) ? InitialRows : 24;
    Pty->Winsize.ws_col = (InitialCols != 0) ? InitialCols : 80;

    /* Set default termios */
    RosvPtyTermiosSetDefaults(&Pty->Termios);

    /* Initialize lock */
    KeInitializeSpinLock(&Pty->Lock);

    /* Allocate reader slot 0 for the creator */
    Pty->Readers[0].Active = TRUE;
    Pty->Readers[0].ReadCursor = 0;
    Pty->ActiveReaderCount = 1;

    Manager->AllocatedCount++;

    KeReleaseSpinLock(&Manager->Lock, OldIrql);

    *PtyIndex = i;
    *ReaderIndex = 0;
    ROSV_TRACE("RosvPtyCreate: allocated PTY %u (%ux%u), reader=0, total=%u",
               i, Pty->Winsize.ws_col, Pty->Winsize.ws_row,
               Manager->AllocatedCount);

    return STATUS_SUCCESS;
}

/*
 * RosvPtyAttachReader - Attach a new reader to an existing PTY.
 *
 * The new reader's cursor starts at the current OutputHead, so it
 * sees only new output from this point forward.
 */
NTSTATUS
RosvPtyAttachReader(
    _Inout_ PROSV_PTY_MANAGER Manager,
    _In_ ULONG PtyIndex,
    _Out_ PULONG ReaderIndex)
{
    PROSV_PTY_STATE Pty;
    KIRQL OldIrql;
    ULONG i;

    if (Manager == NULL || ReaderIndex == NULL)
    {
        ROSV_ERR("RosvPtyAttachReader: invalid parameters (Manager=%p ReaderIndex=%p)",
                 Manager, ReaderIndex);
        return STATUS_INVALID_PARAMETER;
    }

    *ReaderIndex = (ULONG)-1;

    if (PtyIndex >= ROSV_PTY_MAX)
    {
        ROSV_ERR("RosvPtyAttachReader: PtyIndex %u out of range", PtyIndex);
        return STATUS_INVALID_PARAMETER;
    }

    Pty = &Manager->Ptys[PtyIndex];

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        KeReleaseSpinLock(&Pty->Lock, OldIrql);
        ROSV_ERR("RosvPtyAttachReader: PTY %u not allocated", PtyIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    /* Find a free reader slot */
    for (i = 0; i < ROSV_PTY_MAX_READERS; i++)
    {
        if (!Pty->Readers[i].Active)
            break;
    }

    if (i >= ROSV_PTY_MAX_READERS)
    {
        KeReleaseSpinLock(&Pty->Lock, OldIrql);
        ROSV_ERR("RosvPtyAttachReader: PTY %u no free reader slots (max=%u)",
                 PtyIndex, ROSV_PTY_MAX_READERS);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Start at current write head -- see only new output */
    Pty->Readers[i].Active = TRUE;
    Pty->Readers[i].ReadCursor = Pty->OutputHead;
    Pty->ActiveReaderCount++;

    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    *ReaderIndex = i;
    ROSV_TRACE("RosvPtyAttachReader: PTY %u attached reader %u (cursor=%llu, active=%u)",
               PtyIndex, i, (unsigned long long)Pty->OutputHead, Pty->ActiveReaderCount);

    return STATUS_SUCCESS;
}

/*
 * RosvPtyDetachReader - Detach a reader from a PTY.
 *
 * Frees the reader slot and recalculates OutputTail so that
 * buffer space held only by this reader can be reclaimed.
 */
NTSTATUS
RosvPtyDetachReader(
    _Inout_ PROSV_PTY_MANAGER Manager,
    _In_ ULONG PtyIndex,
    _In_ ULONG ReaderIndex)
{
    PROSV_PTY_STATE Pty;
    KIRQL OldIrql;

    if (Manager == NULL)
    {
        ROSV_ERR("RosvPtyDetachReader: Manager is NULL");
        return STATUS_INVALID_PARAMETER;
    }

    if (PtyIndex >= ROSV_PTY_MAX)
    {
        ROSV_ERR("RosvPtyDetachReader: PtyIndex %u out of range", PtyIndex);
        return STATUS_INVALID_PARAMETER;
    }

    if (ReaderIndex >= ROSV_PTY_MAX_READERS)
    {
        ROSV_ERR("RosvPtyDetachReader: ReaderIndex %u out of range", ReaderIndex);
        return STATUS_INVALID_PARAMETER;
    }

    Pty = &Manager->Ptys[PtyIndex];

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);

    if (!Pty->Readers[ReaderIndex].Active)
    {
        KeReleaseSpinLock(&Pty->Lock, OldIrql);
        ROSV_ERR("RosvPtyDetachReader: PTY %u reader %u not active", PtyIndex, ReaderIndex);
        return STATUS_INVALID_PARAMETER;
    }

    Pty->Readers[ReaderIndex].Active = FALSE;
    Pty->Readers[ReaderIndex].ReadCursor = 0;
    Pty->ActiveReaderCount--;

    /* Recalculate tail now that this reader is gone */
    PtyRecalcOutputTail(Pty);

    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    ROSV_TRACE("RosvPtyDetachReader: PTY %u detached reader %u (active=%u)",
               PtyIndex, ReaderIndex, Pty->ActiveReaderCount);

    return STATUS_SUCCESS;
}

NTSTATUS
RosvPtyDestroy(
    _Inout_ PROSV_PTY_MANAGER Manager,
    _In_ ULONG PtyIndex)
{
    PROSV_PTY_STATE Pty;
    KIRQL OldIrql;

    if (Manager == NULL)
    {
        ROSV_ERR("RosvPtyDestroy: Manager is NULL");
        return STATUS_INVALID_PARAMETER;
    }

    ROSV_TRACE("RosvPtyDestroy: PtyIndex=%u", PtyIndex);

    if (PtyIndex >= ROSV_PTY_MAX)
    {
        ROSV_ERR("RosvPtyDestroy: index %u out of range (max=%u)", PtyIndex, ROSV_PTY_MAX);
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Manager->Lock, &OldIrql);

    Pty = &Manager->Ptys[PtyIndex];

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        KeReleaseSpinLock(&Manager->Lock, OldIrql);
        ROSV_ERR("RosvPtyDestroy: PTY %u is not allocated", PtyIndex);
        return STATUS_INVALID_PARAMETER;
    }

    ROSV_TRACE("RosvPtyDestroy: PTY %u stats: written=%llu read=%llu input=%u output=%u",
               PtyIndex, Pty->BytesWrittenToGuest, Pty->BytesReadFromGuest,
               Pty->InputCount, Pty->OutputCount);

    Pty->Flags = RosvPtyFlagNone;
    Manager->AllocatedCount--;

    KeReleaseSpinLock(&Manager->Lock, OldIrql);

    ROSV_TRACE("RosvPtyDestroy: PTY %u destroyed, remaining=%u",
               PtyIndex, Manager->AllocatedCount);
    return STATUS_SUCCESS;
}

/* ---- Master side I/O ---------------------------------------------------- */

/*
 * RosvPtyMasterWrite - Host writes input data for the guest.
 *
 * Data flows through termios input processing before reaching
 * the guest-visible InputBuf.
 */
NTSTATUS
RosvPtyMasterWrite(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_reads_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PULONG BytesWritten)
{
    KIRQL OldIrql;
    ULONG i;
    ULONG TotalQueued = 0;

    if (Pty == NULL || BytesWritten == NULL || (Length != 0 && Data == NULL))
    {
        ROSV_ERR("RosvPtyMasterWrite: invalid parameters (Pty=%p Data=%p Len=%u BytesWritten=%p)",
                 Pty, Data, Length, BytesWritten);
        return STATUS_INVALID_PARAMETER;
    }

    *BytesWritten = 0;

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        ROSV_ERR("RosvPtyMasterWrite: PTY %u not allocated", Pty->PtyIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (Length == 0)
    {
        ROSV_TRACE("RosvPtyMasterWrite: PTY %u zero-length write", Pty->PtyIndex);
        return STATUS_SUCCESS;
    }

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);

    for (i = 0; i < Length; i++)
    {
        /* Check if input buffer has space before processing */
        if (Pty->InputCount >= ROSV_PTY_INPUT_SIZE)
        {
            ROSV_WARN("RosvPtyMasterWrite: PTY %u input buffer full at byte %u/%u",
                      Pty->PtyIndex, i, Length);
            break;
        }

        TotalQueued += RosvPtyTermiosProcessInput(Pty, Data[i]);
    }

    Pty->BytesWrittenToGuest += TotalQueued;

    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    *BytesWritten = i;
    ROSV_TRACE("RosvPtyMasterWrite: PTY %u accepted %u/%u bytes, queued %u to guest",
               Pty->PtyIndex, i, Length, TotalQueued);

    return STATUS_SUCCESS;
}

/*
 * RosvPtyMasterRead - Host reads output data from the guest.
 *
 * Reads from the OutputBuf using a specific reader's cursor.
 * Each reader independently advances through the shared buffer.
 * After reading, OutputTail is recalculated from all active readers.
 */
NTSTATUS
RosvPtyMasterRead(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ ULONG ReaderIndex,
    _Out_writes_(BufferSize) UCHAR *Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesRead)
{
    KIRQL OldIrql;
    ULONG Count = 0;
    UCHAR Byte;

    if (Pty == NULL || BytesRead == NULL || (BufferSize != 0 && Buffer == NULL))
    {
        ROSV_ERR("RosvPtyMasterRead: invalid parameters (Pty=%p Buffer=%p Size=%u BytesRead=%p)",
                 Pty, Buffer, BufferSize, BytesRead);
        return STATUS_INVALID_PARAMETER;
    }

    *BytesRead = 0;

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        ROSV_ERR("RosvPtyMasterRead: PTY %u not allocated", Pty->PtyIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (ReaderIndex >= ROSV_PTY_MAX_READERS)
    {
        ROSV_ERR("RosvPtyMasterRead: PTY %u reader %u out of range",
                 Pty->PtyIndex, ReaderIndex);
        return STATUS_INVALID_PARAMETER;
    }

    if (BufferSize == 0)
    {
        ROSV_TRACE("RosvPtyMasterRead: PTY %u reader %u zero-length read",
                   Pty->PtyIndex, ReaderIndex);
        return STATUS_SUCCESS;
    }

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);

    if (!Pty->Readers[ReaderIndex].Active)
    {
        KeReleaseSpinLock(&Pty->Lock, OldIrql);
        ROSV_ERR("RosvPtyMasterRead: PTY %u reader %u not active",
                 Pty->PtyIndex, ReaderIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    while (Count < BufferSize && PtyOutputGetForReader(Pty, ReaderIndex, &Byte))
    {
        Buffer[Count++] = Byte;
    }

    /* Recalculate global OutputTail after this reader advanced */
    if (Count > 0)
    {
        PtyRecalcOutputTail(Pty);
    }

    Pty->BytesReadFromGuest += Count;

    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    *BytesRead = Count;

    if (Count > 0)
    {
        ROSV_TRACE("RosvPtyMasterRead: PTY %u reader %u read %u bytes (output remaining=%u)",
                   Pty->PtyIndex, ReaderIndex, Count, Pty->OutputCount);
    }

    return STATUS_SUCCESS;
}

/* ---- Slave side I/O ----------------------------------------------------- */

/*
 * RosvPtySlaveWrite - Guest writes output data.
 *
 * Data flows through termios output processing into the OutputBuf
 * where it can be read by the host via RosvPtyMasterRead.
 */
NTSTATUS
RosvPtySlaveWrite(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_reads_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PULONG BytesWritten)
{
    KIRQL OldIrql;
    ULONG i;
    ULONG TotalWritten = 0;
    UCHAR OutBuf[4];
    ULONG OutLen;

    if (Pty == NULL || BytesWritten == NULL || (Length != 0 && Data == NULL))
    {
        ROSV_ERR("RosvPtySlaveWrite: invalid parameters (Pty=%p Data=%p Len=%u BytesWritten=%p)",
                 Pty, Data, Length, BytesWritten);
        return STATUS_INVALID_PARAMETER;
    }

    *BytesWritten = 0;

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        ROSV_ERR("RosvPtySlaveWrite: PTY %u not allocated", Pty->PtyIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (Length == 0)
    {
        ROSV_TRACE("RosvPtySlaveWrite: PTY %u zero-length write", Pty->PtyIndex);
        return STATUS_SUCCESS;
    }

    if (Pty->OutputStopped)
    {
        ROSV_TRACE("RosvPtySlaveWrite: PTY %u output stopped (XOFF), deferring %u bytes",
                   Pty->PtyIndex, Length);
        return STATUS_SUCCESS;
    }

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);

    for (i = 0; i < Length; i++)
    {
        if (Pty->OutputCount >= ROSV_PTY_OUTPUT_SIZE)
        {
            ROSV_WARN("RosvPtySlaveWrite: PTY %u output buffer full at byte %u/%u",
                      Pty->PtyIndex, i, Length);
            break;
        }

        OutLen = RosvPtyTermiosProcessOutput(Pty, Data[i], OutBuf, sizeof(OutBuf));
        for (ULONG j = 0; j < OutLen; j++)
        {
            if (!PtyOutputPut(Pty, OutBuf[j]))
            {
                ROSV_WARN("RosvPtySlaveWrite: PTY %u output buffer full during expansion",
                          Pty->PtyIndex);
                goto Done;
            }
        }
        TotalWritten++;
    }

Done:
    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    *BytesWritten = TotalWritten;
    ROSV_TRACE("RosvPtySlaveWrite: PTY %u wrote %u/%u bytes (output count=%u)",
               Pty->PtyIndex, TotalWritten, Length, Pty->OutputCount);

    return STATUS_SUCCESS;
}

/*
 * RosvPtySlaveWriteRaw - Write raw bytes to PTY OutputBuf, bypassing OPOST.
 *
 * Used by the UART->PTY splice: guest serial output has already been
 * formatted by the guest kernel's tty layer, so no double-processing.
 */
NTSTATUS
RosvPtySlaveWriteRaw(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_reads_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PULONG BytesWritten)
{
    KIRQL OldIrql;
    ULONG i;

    if (Pty == NULL || BytesWritten == NULL || (Length != 0 && Data == NULL))
    {
        ROSV_ERR("RosvPtySlaveWriteRaw: invalid parameters (Pty=%p Data=%p Len=%u BytesWritten=%p)",
                 Pty, Data, Length, BytesWritten);
        return STATUS_INVALID_PARAMETER;
    }

    *BytesWritten = 0;

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        ROSV_ERR("RosvPtySlaveWriteRaw: PTY %u not allocated", Pty->PtyIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (Length == 0)
        return STATUS_SUCCESS;

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);

    for (i = 0; i < Length; i++)
    {
        if (!PtyOutputPut(Pty, Data[i]))
            break;
    }

    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    *BytesWritten = i;
    return STATUS_SUCCESS;
}

/*
 * RosvPtySlaveRead - Guest reads input data.
 *
 * Reads from the InputBuf which contains termios-processed input
 * written by the host via RosvPtyMasterWrite.
 */
NTSTATUS
RosvPtySlaveRead(
    _Inout_ PROSV_PTY_STATE Pty,
    _Out_writes_(BufferSize) UCHAR *Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesRead)
{
    KIRQL OldIrql;
    ULONG Count = 0;
    UCHAR Byte;

    if (Pty == NULL || BytesRead == NULL || (BufferSize != 0 && Buffer == NULL))
    {
        ROSV_ERR("RosvPtySlaveRead: invalid parameters (Pty=%p Buffer=%p Size=%u BytesRead=%p)",
                 Pty, Buffer, BufferSize, BytesRead);
        return STATUS_INVALID_PARAMETER;
    }

    *BytesRead = 0;

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        ROSV_ERR("RosvPtySlaveRead: PTY %u not allocated", Pty->PtyIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (BufferSize == 0)
    {
        ROSV_TRACE("RosvPtySlaveRead: PTY %u zero-length read", Pty->PtyIndex);
        return STATUS_SUCCESS;
    }

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);

    while (Count < BufferSize && PtyInputGet(Pty, &Byte))
    {
        Buffer[Count++] = Byte;

        /* In canonical mode, stop at newline/EOL */
        if ((Pty->Termios.c_lflag & ROSV_ICANON) && Byte == '\n')
            break;
    }

    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    *BytesRead = Count;

    if (Count >= 16 || (Count > 0 && Pty->InputCount == 0))
    {
        ROSV_TRACE("RosvPtySlaveRead: PTY %u read %u bytes (input remaining=%u)",
                   Pty->PtyIndex, Count, Pty->InputCount);
    }

    return STATUS_SUCCESS;
}

/* ---- Termios and Winsize ------------------------------------------------ */

NTSTATUS
RosvPtySetTermios(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ ULONG Action,
    _In_ const ROSV_TERMIOS *Termios)
{
    KIRQL OldIrql;

    if (Pty == NULL || Termios == NULL)
    {
        ROSV_ERR("RosvPtySetTermios: invalid parameters (Pty=%p Termios=%p)",
                 Pty, Termios);
        return STATUS_INVALID_PARAMETER;
    }

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        ROSV_ERR("RosvPtySetTermios: PTY %u not allocated", Pty->PtyIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (Action > ROSV_TCSAFLUSH)
    {
        ROSV_ERR("RosvPtySetTermios: PTY %u invalid action %u", Pty->PtyIndex, Action);
        return STATUS_INVALID_PARAMETER;
    }

    ROSV_TRACE("RosvPtySetTermios: PTY %u action=%u iflag=0x%X oflag=0x%X cflag=0x%X lflag=0x%X",
               Pty->PtyIndex, Action,
               Termios->c_iflag, Termios->c_oflag,
               Termios->c_cflag, Termios->c_lflag);

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);

    /* TCSAFLUSH: flush input queue before applying */
    if (Action == ROSV_TCSAFLUSH)
    {
        Pty->InputHead = Pty->InputTail = Pty->InputCount = 0;
        Pty->LineLen = 0;
        ROSV_TRACE("RosvPtySetTermios: PTY %u input flushed (TCSAFLUSH)", Pty->PtyIndex);
    }

    RtlCopyMemory(&Pty->Termios, Termios, sizeof(ROSV_TERMIOS));

    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    return STATUS_SUCCESS;
}

NTSTATUS
RosvPtyGetTermios(
    _In_ PROSV_PTY_STATE Pty,
    _Out_ PROSV_TERMIOS Termios)
{
    KIRQL OldIrql;

    if (Pty == NULL || Termios == NULL)
    {
        ROSV_ERR("RosvPtyGetTermios: invalid parameters (Pty=%p Termios=%p)",
                 Pty, Termios);
        return STATUS_INVALID_PARAMETER;
    }

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        ROSV_ERR("RosvPtyGetTermios: PTY %u not allocated", Pty->PtyIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);
    RtlCopyMemory(Termios, &Pty->Termios, sizeof(ROSV_TERMIOS));
    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    ROSV_TRACE("RosvPtyGetTermios: PTY %u iflag=0x%X oflag=0x%X cflag=0x%X lflag=0x%X",
               Pty->PtyIndex,
               Termios->c_iflag, Termios->c_oflag,
               Termios->c_cflag, Termios->c_lflag);

    return STATUS_SUCCESS;
}

NTSTATUS
RosvPtySetWinsize(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ const ROSV_WINSIZE *Winsize)
{
    KIRQL OldIrql;
    BOOLEAN Changed;

    if (Pty == NULL || Winsize == NULL)
    {
        ROSV_ERR("RosvPtySetWinsize: invalid parameters (Pty=%p Winsize=%p)",
                 Pty, Winsize);
        return STATUS_INVALID_PARAMETER;
    }

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        ROSV_ERR("RosvPtySetWinsize: PTY %u not allocated", Pty->PtyIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    ROSV_TRACE("RosvPtySetWinsize: PTY %u new size %ux%u (was %ux%u)",
               Pty->PtyIndex, Winsize->ws_col, Winsize->ws_row,
               Pty->Winsize.ws_col, Pty->Winsize.ws_row);

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);

    Changed = (Pty->Winsize.ws_row != Winsize->ws_row ||
               Pty->Winsize.ws_col != Winsize->ws_col);

    RtlCopyMemory(&Pty->Winsize, Winsize, sizeof(ROSV_WINSIZE));

    if (Changed)
    {
        Pty->PendingSignal = ROSV_SIGWINCH;
        ROSV_TRACE("RosvPtySetWinsize: PTY %u SIGWINCH queued", Pty->PtyIndex);
    }

    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    return STATUS_SUCCESS;
}

NTSTATUS
RosvPtyGetWinsize(
    _In_ PROSV_PTY_STATE Pty,
    _Out_ PROSV_WINSIZE Winsize)
{
    KIRQL OldIrql;

    if (Pty == NULL || Winsize == NULL)
    {
        ROSV_ERR("RosvPtyGetWinsize: invalid parameters (Pty=%p Winsize=%p)",
                 Pty, Winsize);
        return STATUS_INVALID_PARAMETER;
    }

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        ROSV_ERR("RosvPtyGetWinsize: PTY %u not allocated", Pty->PtyIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);
    RtlCopyMemory(Winsize, &Pty->Winsize, sizeof(ROSV_WINSIZE));
    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    ROSV_TRACE("RosvPtyGetWinsize: PTY %u %ux%u",
               Pty->PtyIndex, Winsize->ws_col, Winsize->ws_row);

    return STATUS_SUCCESS;
}

/* ---- Flush -------------------------------------------------------------- */

NTSTATUS
RosvPtyFlush(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ ULONG Queue)
{
    KIRQL OldIrql;

    if (Pty == NULL)
    {
        ROSV_ERR("RosvPtyFlush: Pty is NULL");
        return STATUS_INVALID_PARAMETER;
    }

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        ROSV_ERR("RosvPtyFlush: PTY %u not allocated", Pty->PtyIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (Queue > ROSV_TCIOFLUSH)
    {
        ROSV_ERR("RosvPtyFlush: PTY %u invalid queue selector %u", Pty->PtyIndex, Queue);
        return STATUS_INVALID_PARAMETER;
    }

    ROSV_TRACE("RosvPtyFlush: PTY %u queue=%u (input=%u output=%u)",
               Pty->PtyIndex, Queue, Pty->InputCount, Pty->OutputCount);

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);

    if (Queue == ROSV_TCIFLUSH || Queue == ROSV_TCIOFLUSH)
    {
        Pty->InputHead = Pty->InputTail = Pty->InputCount = 0;
        Pty->LineLen = 0;
        ROSV_TRACE("RosvPtyFlush: PTY %u input flushed", Pty->PtyIndex);
    }

    if (Queue == ROSV_TCOFLUSH || Queue == ROSV_TCIOFLUSH)
    {
        ULONG j;
        Pty->OutputHead = Pty->OutputTail = Pty->OutputCount = 0;
        for (j = 0; j < ROSV_PTY_MAX_READERS; j++)
        {
            if (Pty->Readers[j].Active)
                Pty->Readers[j].ReadCursor = 0;
        }
        ROSV_TRACE("RosvPtyFlush: PTY %u output flushed (all reader cursors reset)", Pty->PtyIndex);
    }

    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    return STATUS_SUCCESS;
}

/* ---- Signal ------------------------------------------------------------- */

NTSTATUS
RosvPtySendSignal(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ ULONG Signal)
{
    KIRQL OldIrql;

    if (Pty == NULL)
    {
        ROSV_ERR("RosvPtySendSignal: Pty is NULL");
        return STATUS_INVALID_PARAMETER;
    }

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        ROSV_ERR("RosvPtySendSignal: PTY %u not allocated", Pty->PtyIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    ROSV_TRACE("RosvPtySendSignal: PTY %u signal=%u (previous pending=%u)",
               Pty->PtyIndex, Signal, Pty->PendingSignal);

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);
    Pty->PendingSignal = Signal;
    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    return STATUS_SUCCESS;
}

/* ---- Info --------------------------------------------------------------- */

NTSTATUS
RosvPtyGetInfo(
    _In_ PROSV_PTY_STATE Pty,
    _Out_ PROSV_PTY_INFO Info)
{
    KIRQL OldIrql;

    if (Pty == NULL || Info == NULL)
    {
        ROSV_ERR("RosvPtyGetInfo: invalid parameters (Pty=%p Info=%p)",
                 Pty, Info);
        return STATUS_INVALID_PARAMETER;
    }

    if (!(Pty->Flags & RosvPtyFlagAllocated))
    {
        ROSV_ERR("RosvPtyGetInfo: PTY %u not allocated", Pty->PtyIndex);
        return STATUS_INVALID_DEVICE_STATE;
    }

    KeAcquireSpinLock(&Pty->Lock, &OldIrql);

    Info->PtyIndex = Pty->PtyIndex;
    Info->Flags = Pty->Flags;
    RtlCopyMemory(&Info->Winsize, &Pty->Winsize, sizeof(ROSV_WINSIZE));
    Info->SessionId = Pty->SessionId;
    Info->ForegroundPgid = Pty->ForegroundPgid;
    Info->BytesWrittenToGuest = Pty->BytesWrittenToGuest;
    Info->BytesReadFromGuest = Pty->BytesReadFromGuest;

    KeReleaseSpinLock(&Pty->Lock, OldIrql);

    ROSV_TRACE("RosvPtyGetInfo: PTY %u flags=0x%X size=%ux%u written=%llu read=%llu",
               Info->PtyIndex, Info->Flags,
               Info->Winsize.ws_col, Info->Winsize.ws_row,
               Info->BytesWrittenToGuest, Info->BytesReadFromGuest);

    return STATUS_SUCCESS;
}

/* ---- PTY-Vcon bridge ---------------------------------------------------- */

/*
 * RosvPtyPumpToVcon - Drain PTY InputBuf into bound vcon port TxBuf.
 *
 * Called after PTY_WRITE to push host keystrokes through to the guest
 * via the bound virtio-console port. The PTY's termios input processing
 * has already been applied by RosvPtyMasterWrite.
 */
VOID
RosvPtyPumpToVcon(
    _Inout_ PROSV_PTY_STATE Pty)
{
    UCHAR DrainBuf[256];
    ULONG TotalDrained = 0;

    if (Pty == NULL || Pty->BoundVconPort == (ULONG)-1 || Pty->BoundVconState == NULL)
        return;

    for (;;)
    {
        ULONG Available;
        ULONG ToRead;
        ULONG DrainRead = 0;
        ULONG Written = 0;
        NTSTATUS Status;

        Available = RosvVirtioConPortQueryTxSpace(Pty->BoundVconState, Pty->BoundVconPort);
        if (Available == 0)
            break;

        ToRead = sizeof(DrainBuf);
        if (ToRead > Available)
            ToRead = Available;

        /* Read from PTY InputBuf (slave read side) */
        Status = RosvPtySlaveRead(Pty, DrainBuf, ToRead, &DrainRead);
        if (!NT_SUCCESS(Status) || DrainRead == 0)
            break;

        /* Write to vcon port TxBuf (host → guest) */
        Status = RosvVirtioConPortWrite(Pty->BoundVconState, Pty->BoundVconPort,
                                        DrainBuf, DrainRead, &Written);

        TotalDrained += Written;

        if (!NT_SUCCESS(Status) || Written < DrainRead)
        {
            if (Written < DrainRead)
            {
                PtyInputUngetRange(Pty, DrainBuf + Written, DrainRead - Written);
            }
            ROSV_TRACE("RosvPtyPumpToVcon: PTY %u vcon port %u TxBuf full (wrote %u/%u)",
                       Pty->PtyIndex, Pty->BoundVconPort, Written, DrainRead);
            break;
        }
    }

    if (TotalDrained > 0)
    {
        ROSV_TRACE("RosvPtyPumpToVcon: PTY %u → vcon port %u: drained %u bytes",
                   Pty->PtyIndex, Pty->BoundVconPort, TotalDrained);
    }
}

/*
 * RosvVconPumpToPty - Drain vcon port RxBuf into PTY OutputBuf.
 *
 * Called when the guest writes to a virtio-console port that is bound
 * to a PTY. The guest output bytes are pushed raw into the PTY's
 * OutputBuf (bypassing OPOST since the guest application already
 * formatted the output). The host client reads via PTY_READ.
 */
VOID
RosvVconPumpToPty(
    _Inout_ PROSV_PTY_STATE Pty)
{
    UCHAR DrainBuf[256];
    ULONG TotalDrained = 0;

    if (Pty == NULL || Pty->BoundVconPort == (ULONG)-1 || Pty->BoundVconState == NULL)
        return;

    for (;;)
    {
        ULONG Available;
        ULONG ToRead;
        ULONG DrainRead = 0;
        ULONG Written = 0;
        NTSTATUS Status;

        Available = PtyGetOutputFreeSpace(Pty);
        if (Available == 0)
            break;

        ToRead = sizeof(DrainBuf);
        if (ToRead > Available)
            ToRead = Available;

        Status = RosvVirtioConPortRead(Pty->BoundVconState, Pty->BoundVconPort,
                                       DrainBuf, ToRead, &DrainRead);
        if (!NT_SUCCESS(Status))
            break;
        if (DrainRead == 0)
            break;

        /* Push raw bytes into PTY OutputBuf (guest → host) */
        Status = RosvPtySlaveWriteRaw(Pty, DrainBuf, DrainRead, &Written);
        if (!NT_SUCCESS(Status))
        {
            RosvVirtioConPortUnread(Pty->BoundVconState, Pty->BoundVconPort,
                                    DrainBuf, DrainRead, NULL);
            break;
        }

        if (Written < DrainRead)
        {
            RosvVirtioConPortUnread(Pty->BoundVconState, Pty->BoundVconPort,
                                    DrainBuf + Written, DrainRead - Written, NULL);
        }

        TotalDrained += Written;

        if (Written > 0)
        {
            RosvVirtioConKickTx(Pty->BoundVconState, Pty->BoundVconPort);
        }

        if (Written < DrainRead || DrainRead < ToRead)
            break;
    }

    if (TotalDrained > 0)
    {
        ROSV_TRACE("RosvVconPumpToPty: vcon port %u → PTY %u: drained %u bytes",
                   Pty->BoundVconPort, Pty->PtyIndex, TotalDrained);
    }
}

/*
 * RosvPtyPumpAllVconBound - Pump all vcon-bound PTYs bidirectionally.
 *
 * Called from the VMX exit handler to keep data flowing between
 * vcon ports and their bound PTYs during guest execution.
 */
VOID
RosvPtyPumpAllVconBound(
    _Inout_ PROSV_PTY_MANAGER Manager)
{
    ULONG i;

    if (Manager == NULL)
        return;

    for (i = 0; i < ROSV_PTY_MAX; i++)
    {
        PROSV_PTY_STATE Pty = &Manager->Ptys[i];

        if ((Pty->Flags & RosvPtyFlagAllocated) &&
            Pty->BoundVconPort != (ULONG)-1)
        {
            RosvVconPumpToPty(Pty);     /* guest→host: vcon RxBuf → PTY OutputBuf */
            RosvPtyPumpToVcon(Pty);     /* host→guest: PTY InputBuf → vcon TxBuf */
        }
    }
}
