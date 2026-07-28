/*
 * PROJECT:     ReactOS VMX Hypervisor Launcher (rosl.exe)
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Terminal bridge - PTY I/O with VT pass-through
 * COPYRIGHT:   Copyright 2025-2026 Ahmed Arif
 *
 * This module implements the host-side terminal for PTY-based console I/O.
 * Most VT/ANSI escape sequences pass through, but DEC private mode toggles
 * that ReactOS prints literally are consumed locally.
 */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <process.h>

/* DbgPrint from ntdll */
ULONG __cdecl DbgPrint(PCSTR Format, ...);

/* ---- IOCTL codes (user-mode copies) ------------------------------------- */

#define ROSV_IOCTL_TYPE     0x8000

#define ROSV_IOCTL_PTY_READ     CTL_CODE(ROSV_IOCTL_TYPE, 0x822, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_WRITE    CTL_CODE(ROSV_IOCTL_TYPE, 0x823, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_RESIZE   CTL_CODE(ROSV_IOCTL_TYPE, 0x824, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_SIGNAL   CTL_CODE(ROSV_IOCTL_TYPE, 0x827, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* ---- PTY IOCTL structures (user-mode copies) ---------------------------- */

typedef struct {
    ULONG PtyIndex;
    ULONG DataLength;
    UCHAR Data[1];
} ROSV_PTY_IO_REQUEST;

typedef struct {
    ULONG BytesTransferred;
    LONG  Status;   /* NTSTATUS */
    UCHAR Data[1];
} ROSV_PTY_IO_RESULT;

typedef struct {
    USHORT ws_row;
    USHORT ws_col;
    USHORT ws_xpixel;
    USHORT ws_ypixel;
} ROSV_WINSIZE;

typedef struct {
    ULONG PtyIndex;
    ROSV_WINSIZE Winsize;
} ROSV_PTY_RESIZE_REQUEST;

typedef struct {
    ULONG PtyIndex;
    ULONG Signal;
} ROSV_PTY_SIGNAL_REQUEST;

/* Linux signal numbers */
#define ROSV_SIGINT     2
#define ROSV_SIGQUIT    3
#define ROSV_SIGTSTP    20

/* ---- Terminal context --------------------------------------------------- */

#define TERM_IO_BUF_SIZE    4096
#define TERM_OUTPUT_POLL_MS 10

typedef struct _TERMINAL_CONTEXT {
    /* Device handle (shared with main rosl code) */
    HANDLE hDevice;
    ULONG  PtyIndex;

    /* Console handles */
    HANDLE hStdin;
    HANDLE hStdout;

    /* Saved console modes for restore */
    DWORD  OrigInputMode;
    DWORD  OrigOutputMode;
    UINT   OrigInputCP;
    UINT   OrigOutputCP;
    BOOL   InputModeValid;
    BOOL   OutputModeValid;
    BOOL   InputCPValid;
    BOOL   OutputCPValid;

    /* Thread handles */
    HANDLE hInputThread;
    HANDLE hOutputThread;

    /* Minimal VT state needed to translate host keys correctly */
    BOOL   CursorKeysApplication;
    UCHAR  VtSeqBuf[16];
    DWORD  VtSeqLen;

    /* Shutdown flag */
    volatile BOOL Running;
} TERMINAL_CONTEXT, *PTERMINAL_CONTEXT;

/* Single global terminal context */
static TERMINAL_CONTEXT g_Term;

/* ---- Helpers ------------------------------------------------------------ */

static void TermLog(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    DbgPrint("rosl-term: %s", buf);
}

static BOOL TermIoctl(DWORD code, void *in, DWORD inSz, void *out, DWORD outSz, DWORD *ret)
{
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(g_Term.hDevice, code, in, inSz, out, outSz, &bytes, NULL);
    if (ret) *ret = bytes;
    return ok;
}

static BOOL TermIsVtFinalByte(UCHAR byte)
{
    return (byte >= 0x40 && byte <= 0x7E);
}

static void TermApplyVtSequence(const UCHAR *seq, DWORD length)
{
    if (length == 5 && memcmp(seq, "\x1b[?1h", 5) == 0)
    {
        g_Term.CursorKeysApplication = TRUE;
    }
    else if (length == 5 && memcmp(seq, "\x1b[?1l", 5) == 0)
    {
        g_Term.CursorKeysApplication = FALSE;
    }
}

static BOOL TermShouldSuppressVtSequence(const UCHAR *seq, DWORD length)
{
    if (length < 4 ||
        seq == NULL ||
        seq[0] != '\x1b' ||
        seq[1] != '[' ||
        seq[2] != '?')
    {
        return FALSE;
    }

    return TermIsVtFinalByte(seq[length - 1]);
}

static DWORD TermFlushPendingVtSequence(UCHAR *data, DWORD outLen, DWORD outSize)
{
    if (g_Term.VtSeqLen == 0)
        return outLen;

    if (outLen + g_Term.VtSeqLen <= outSize)
    {
        memcpy(data + outLen, g_Term.VtSeqBuf, g_Term.VtSeqLen);
        outLen += g_Term.VtSeqLen;
    }

    g_Term.VtSeqLen = 0;
    return outLen;
}

static DWORD TermFeedOutputVtState(UCHAR *data, DWORD length)
{
    DWORD i;
    DWORD outLen = 0;

    if (data == NULL || length == 0)
        return 0;

    for (i = 0; i < length; i++)
    {
        UCHAR byte = data[i];

        if (g_Term.VtSeqLen == 0)
        {
            if (byte == '\x1b')
            {
                g_Term.VtSeqBuf[0] = byte;
                g_Term.VtSeqLen = 1;
            }
            else
            {
                data[outLen++] = byte;
            }
            continue;
        }

        if (g_Term.VtSeqLen >= sizeof(g_Term.VtSeqBuf))
        {
            outLen = TermFlushPendingVtSequence(data, outLen, length);
            if (byte == '\x1b')
            {
                g_Term.VtSeqBuf[0] = byte;
                g_Term.VtSeqLen = 1;
            }
            else
            {
                data[outLen++] = byte;
            }
            continue;
        }

        g_Term.VtSeqBuf[g_Term.VtSeqLen++] = byte;

        if (g_Term.VtSeqLen == 2)
        {
            if (byte == '[' || byte == 'O')
                continue;

            outLen = TermFlushPendingVtSequence(data, outLen, length);
            continue;
        }

        if (g_Term.VtSeqBuf[1] == '[')
        {
            if (TermIsVtFinalByte(byte))
            {
                TermApplyVtSequence(g_Term.VtSeqBuf, g_Term.VtSeqLen);
                if (!TermShouldSuppressVtSequence(g_Term.VtSeqBuf, g_Term.VtSeqLen))
                    outLen = TermFlushPendingVtSequence(data, outLen, length);
                else
                    g_Term.VtSeqLen = 0;
            }
        }
        else if (g_Term.VtSeqBuf[1] == 'O')
        {
            if (TermIsVtFinalByte(byte))
                outLen = TermFlushPendingVtSequence(data, outLen, length);
        }
        else
        {
            outLen = TermFlushPendingVtSequence(data, outLen, length);
        }
    }

    return outLen;
}

/* ---- PTY write helper --------------------------------------------------- */

static BOOL TermPtyWrite(const UCHAR *data, DWORD len)
{
    UCHAR reqBuf[sizeof(ROSV_PTY_IO_REQUEST) + TERM_IO_BUF_SIZE];
    ROSV_PTY_IO_REQUEST *req = (ROSV_PTY_IO_REQUEST *)reqBuf;
    DWORD ret;

    if (data == NULL && len != 0)
        return FALSE;
    if (len == 0) return TRUE;
    if (len > TERM_IO_BUF_SIZE) len = TERM_IO_BUF_SIZE;

    req->PtyIndex = g_Term.PtyIndex;
    req->DataLength = len;
    memcpy(req->Data, data, len);

    return TermIoctl(ROSV_IOCTL_PTY_WRITE, req,
                     (DWORD)(offsetof(ROSV_PTY_IO_REQUEST, Data) + len),
                     NULL, 0, &ret);
}

/* ---- PTY read helper ---------------------------------------------------- */

static DWORD TermPtyRead(UCHAR *buf, DWORD bufSize)
{
    UCHAR resBuf[sizeof(ROSV_PTY_IO_RESULT) + TERM_IO_BUF_SIZE];
    ROSV_PTY_IO_RESULT *res = (ROSV_PTY_IO_RESULT *)resBuf;
    ROSV_PTY_IO_REQUEST req;
    DWORD ret;
    DWORD toCopy;

    if (buf == NULL || bufSize == 0)
        return 0;

    req.PtyIndex = g_Term.PtyIndex;
    req.DataLength = (bufSize < TERM_IO_BUF_SIZE) ? bufSize : TERM_IO_BUF_SIZE;

    if (!TermIoctl(ROSV_IOCTL_PTY_READ, &req, sizeof(req),
                   resBuf, sizeof(resBuf), &ret))
    {
        return 0;
    }

    if (ret <= offsetof(ROSV_PTY_IO_RESULT, Data))
        return 0;

    if (res->BytesTransferred == 0)
        return 0;

    toCopy = res->BytesTransferred;
    if (toCopy > bufSize) toCopy = bufSize;
    memcpy(buf, res->Data, toCopy);
    return toCopy;
}

/* ---- PTY signal helper -------------------------------------------------- */

static BOOL TermPtySignal(ULONG signal)
{
    ROSV_PTY_SIGNAL_REQUEST req;
    DWORD ret;

    req.PtyIndex = g_Term.PtyIndex;
    req.Signal = signal;

    TermLog("Sending signal %lu to PTY %lu\n", signal, g_Term.PtyIndex);
    return TermIoctl(ROSV_IOCTL_PTY_SIGNAL, &req, sizeof(req), NULL, 0, &ret);
}

/* ---- PTY resize helper -------------------------------------------------- */

static BOOL TermPtyResize(USHORT rows, USHORT cols)
{
    ROSV_PTY_RESIZE_REQUEST req;
    DWORD ret;

    req.PtyIndex = g_Term.PtyIndex;
    req.Winsize.ws_row = rows;
    req.Winsize.ws_col = cols;
    req.Winsize.ws_xpixel = 0;
    req.Winsize.ws_ypixel = 0;

    TermLog("Resizing PTY %lu to %ux%u\n", g_Term.PtyIndex, cols, rows);
    return TermIoctl(ROSV_IOCTL_PTY_RESIZE, &req, sizeof(req), NULL, 0, &ret);
}

/* ---- Key event translation ---------------------------------------------- */

/*
 * Translate a Win32 KEY_EVENT_RECORD into raw bytes for the guest.
 * Returns the number of bytes written to outBuf (0 if event should be ignored).
 * Special keys are mapped to standard VT/xterm escape sequences.
 */
static DWORD TranslateKeyEvent(const KEY_EVENT_RECORD *ke, UCHAR *outBuf, DWORD outBufSize)
{
    DWORD ctrlState;
    BOOL alt;
    WORD vk;
    WCHAR wch;
    int utf8Len;

    if (ke == NULL || outBuf == NULL || outBufSize == 0)
        return 0;

    ctrlState = ke->dwControlKeyState;
    alt = (ctrlState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
    vk = ke->wVirtualKeyCode;
    wch = ke->uChar.UnicodeChar;

    /* If there's a printable/control character, use it directly */
    if (wch != 0)
    {
        if (wch <= 0x1F || wch == 0x7F)
        {
            if (alt && outBufSize >= 2)
            {
                outBuf[0] = '\x1b';
                outBuf[1] = (UCHAR)wch;
                return 2;
            }

            outBuf[0] = (UCHAR)wch;
            return 1;
        }

        utf8Len = WideCharToMultiByte(CP_UTF8,
                                      0,
                                      &wch,
                                      1,
                                      (LPSTR)(alt ? outBuf + 1 : outBuf),
                                      (int)(alt ? outBufSize - 1 : outBufSize),
                                      NULL,
                                      NULL);
        if (utf8Len <= 0)
            return 0;

        if (alt)
        {
            if (outBufSize < (DWORD)(utf8Len + 1))
                return 0;
            outBuf[0] = '\x1b';
            return (DWORD)utf8Len + 1;
        }

        return (DWORD)utf8Len;
    }

    /* Special key VT sequences */
    switch (vk)
    {
        case VK_UP:
            if (outBufSize >= 3) { outBuf[0]='\x1b'; outBuf[1]=g_Term.CursorKeysApplication ? 'O' : '['; outBuf[2]='A'; return 3; }
            break;
        case VK_DOWN:
            if (outBufSize >= 3) { outBuf[0]='\x1b'; outBuf[1]=g_Term.CursorKeysApplication ? 'O' : '['; outBuf[2]='B'; return 3; }
            break;
        case VK_RIGHT:
            if (outBufSize >= 3) { outBuf[0]='\x1b'; outBuf[1]=g_Term.CursorKeysApplication ? 'O' : '['; outBuf[2]='C'; return 3; }
            break;
        case VK_LEFT:
            if (outBufSize >= 3) { outBuf[0]='\x1b'; outBuf[1]=g_Term.CursorKeysApplication ? 'O' : '['; outBuf[2]='D'; return 3; }
            break;
        case VK_HOME:
            if (outBufSize >= 3) { outBuf[0]='\x1b'; outBuf[1]=g_Term.CursorKeysApplication ? 'O' : '['; outBuf[2]='H'; return 3; }
            break;
        case VK_END:
            if (outBufSize >= 3) { outBuf[0]='\x1b'; outBuf[1]=g_Term.CursorKeysApplication ? 'O' : '['; outBuf[2]='F'; return 3; }
            break;
        case VK_INSERT:
            if (outBufSize >= 4) { outBuf[0]='\x1b'; outBuf[1]='['; outBuf[2]='2'; outBuf[3]='~'; return 4; }
            break;
        case VK_DELETE:
            if (outBufSize >= 4) { outBuf[0]='\x1b'; outBuf[1]='['; outBuf[2]='3'; outBuf[3]='~'; return 4; }
            break;
        case VK_PRIOR: /* Page Up */
            if (outBufSize >= 4) { outBuf[0]='\x1b'; outBuf[1]='['; outBuf[2]='5'; outBuf[3]='~'; return 4; }
            break;
        case VK_NEXT:  /* Page Down */
            if (outBufSize >= 4) { outBuf[0]='\x1b'; outBuf[1]='['; outBuf[2]='6'; outBuf[3]='~'; return 4; }
            break;
        case VK_F1:  if (outBufSize >= 3) { outBuf[0]='\x1b'; outBuf[1]='O'; outBuf[2]='P'; return 3; } break;
        case VK_F2:  if (outBufSize >= 3) { outBuf[0]='\x1b'; outBuf[1]='O'; outBuf[2]='Q'; return 3; } break;
        case VK_F3:  if (outBufSize >= 3) { outBuf[0]='\x1b'; outBuf[1]='O'; outBuf[2]='R'; return 3; } break;
        case VK_F4:  if (outBufSize >= 3) { outBuf[0]='\x1b'; outBuf[1]='O'; outBuf[2]='S'; return 3; } break;
        case VK_F5:  if (outBufSize >= 5) { outBuf[0]='\x1b'; outBuf[1]='['; outBuf[2]='1'; outBuf[3]='5'; outBuf[4]='~'; return 5; } break;
        case VK_F6:  if (outBufSize >= 5) { outBuf[0]='\x1b'; outBuf[1]='['; outBuf[2]='1'; outBuf[3]='7'; outBuf[4]='~'; return 5; } break;
        case VK_F7:  if (outBufSize >= 5) { outBuf[0]='\x1b'; outBuf[1]='['; outBuf[2]='1'; outBuf[3]='8'; outBuf[4]='~'; return 5; } break;
        case VK_F8:  if (outBufSize >= 5) { outBuf[0]='\x1b'; outBuf[1]='['; outBuf[2]='1'; outBuf[3]='9'; outBuf[4]='~'; return 5; } break;
        case VK_F9:  if (outBufSize >= 5) { outBuf[0]='\x1b'; outBuf[1]='['; outBuf[2]='2'; outBuf[3]='0'; outBuf[4]='~'; return 5; } break;
        case VK_F10: if (outBufSize >= 5) { outBuf[0]='\x1b'; outBuf[1]='['; outBuf[2]='2'; outBuf[3]='1'; outBuf[4]='~'; return 5; } break;
        case VK_F11: if (outBufSize >= 5) { outBuf[0]='\x1b'; outBuf[1]='['; outBuf[2]='2'; outBuf[3]='3'; outBuf[4]='~'; return 5; } break;
        case VK_F12: if (outBufSize >= 5) { outBuf[0]='\x1b'; outBuf[1]='['; outBuf[2]='2'; outBuf[3]='4'; outBuf[4]='~'; return 5; } break;
    }

    return 0; /* Unhandled key */
}

/* ---- Input thread (host -> guest) --------------------------------------- */

static unsigned __stdcall TerminalInputThreadProc(void *param)
{
    INPUT_RECORD records[64];
    DWORD numRead, i;
    UCHAR sendBuf[256];
    DWORD sendLen;
    DWORD waitResult;

    (void)param;

    TermLog("Input thread started\n");

    while (g_Term.Running)
    {
        /* Wait for console input events (blocking, with 100ms timeout) */
        waitResult = WaitForSingleObject(g_Term.hStdin, 100);
        if (waitResult != WAIT_OBJECT_0)
            continue;

        if (!g_Term.Running)
            break;

        if (!ReadConsoleInputW(g_Term.hStdin, records, 64, &numRead))
        {
            TermLog("ReadConsoleInput failed (err=%lu)\n", GetLastError());
            continue;
        }

        sendLen = 0;

        for (i = 0; i < numRead && g_Term.Running; i++)
        {
            if (records[i].EventType == KEY_EVENT &&
                records[i].Event.KeyEvent.bKeyDown)
            {
                KEY_EVENT_RECORD *ke = &records[i].Event.KeyEvent;
                DWORD ctrlState = ke->dwControlKeyState;
                BOOL ctrl = (ctrlState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
                BOOL alt = (ctrlState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
                WCHAR ch = ke->uChar.UnicodeChar;
                UCHAR keyBuf[8];
                DWORD keyLen;

                /* Intercept Ctrl+C -> SIGINT */
                if (ctrl && !alt && ke->wVirtualKeyCode == 'C')
                {
                    /* Flush any pending bytes first */
                    if (sendLen > 0)
                    {
                        TermPtyWrite(sendBuf, sendLen);
                        sendLen = 0;
                    }
                    TermPtySignal(ROSV_SIGINT);
                    continue;
                }

                /* Intercept Ctrl+Z -> SIGTSTP */
                if (ctrl && !alt && ke->wVirtualKeyCode == 'Z')
                {
                    if (sendLen > 0)
                    {
                        TermPtyWrite(sendBuf, sendLen);
                        sendLen = 0;
                    }
                    TermPtySignal(ROSV_SIGTSTP);
                    continue;
                }

                /* Intercept Ctrl+\ -> SIGQUIT */
                if (ctrl && !alt && (ch == 28 || ke->wVirtualKeyCode == VK_OEM_5))
                {
                    if (sendLen > 0)
                    {
                        TermPtyWrite(sendBuf, sendLen);
                        sendLen = 0;
                    }
                    TermPtySignal(ROSV_SIGQUIT);
                    continue;
                }

                /* Translate the key event */
                keyLen = TranslateKeyEvent(ke, keyBuf, sizeof(keyBuf));

                if (keyLen > 0)
                {
                    /* If adding would overflow send buffer, flush first */
                    if (sendLen + keyLen > sizeof(sendBuf))
                    {
                        TermPtyWrite(sendBuf, sendLen);
                        sendLen = 0;
                    }
                    memcpy(sendBuf + sendLen, keyBuf, keyLen);
                    sendLen += keyLen;
                }
            }
            else if (records[i].EventType == WINDOW_BUFFER_SIZE_EVENT)
            {
                COORD newSize = records[i].Event.WindowBufferSizeEvent.dwSize;

                /* Flush pending input */
                if (sendLen > 0)
                {
                    TermPtyWrite(sendBuf, sendLen);
                    sendLen = 0;
                }

                TermPtyResize((USHORT)newSize.Y, (USHORT)newSize.X);
            }
        }

        /* Send accumulated input bytes */
        if (sendLen > 0)
        {
            TermPtyWrite(sendBuf, sendLen);
        }
    }

    TermLog("Input thread exiting\n");
    return 0;
}

/* ---- Output thread (guest -> host) -------------------------------------- */

static unsigned __stdcall TerminalOutputThreadProc(void *param)
{
    UCHAR readBuf[TERM_IO_BUF_SIZE];
    DWORD bytesRead;
    DWORD written;
    DWORD emptyPolls = 0;

    (void)param;

    TermLog("Output thread started\n");

    while (g_Term.Running)
    {
        bytesRead = TermPtyRead(readBuf, sizeof(readBuf));

        if (bytesRead > 0)
        {
            bytesRead = TermFeedOutputVtState(readBuf, bytesRead);
            if (bytesRead > 0)
                WriteFile(g_Term.hStdout, readBuf, bytesRead, &written, NULL);
            emptyPolls = 0;
        }
        else
        {
            /* No data available, sleep before polling again.
             * Use adaptive polling: short interval when active,
             * back off slightly when idle. */
            emptyPolls++;
            if (emptyPolls < 10)
                Sleep(TERM_OUTPUT_POLL_MS);
            else if (emptyPolls < 100)
                Sleep(TERM_OUTPUT_POLL_MS * 2);
            else
                Sleep(TERM_OUTPUT_POLL_MS * 5);
        }
    }

    /* Drain any remaining output after shutdown signal */
    for (;;)
    {
        bytesRead = TermPtyRead(readBuf, sizeof(readBuf));
        if (bytesRead == 0)
            break;
        bytesRead = TermFeedOutputVtState(readBuf, bytesRead);
        if (bytesRead > 0)
            WriteFile(g_Term.hStdout, readBuf, bytesRead, &written, NULL);
    }

    TermLog("Output thread exiting\n");
    return 0;
}

/* ---- Public API --------------------------------------------------------- */

/*
 * TerminalInitialize - Set up the Windows Console for raw VT pass-through
 * and prepare the terminal context.
 *
 * hDevice:  Handle to the rosv driver (for PTY IOCTLs)
 * ptyIndex: Index of the allocated PTY pair
 */
BOOL TerminalInitialize(HANDLE hDevice, ULONG ptyIndex)
{
    DWORD inputMode, outputMode;

    memset(&g_Term, 0, sizeof(g_Term));
    g_Term.hDevice = hDevice;
    g_Term.PtyIndex = ptyIndex;
    g_Term.Running = TRUE;
    g_Term.OrigInputCP = 0;
    g_Term.OrigOutputCP = 0;

    /* Get console handles */
    g_Term.hStdin = GetStdHandle(STD_INPUT_HANDLE);
    g_Term.hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (g_Term.hStdin == INVALID_HANDLE_VALUE || g_Term.hStdout == INVALID_HANDLE_VALUE)
    {
        printf("[FAIL] Cannot get console handles (err=%lu)\n", GetLastError());
        return FALSE;
    }

    /* Save and configure input mode */
    g_Term.InputModeValid = GetConsoleMode(g_Term.hStdin, &g_Term.OrigInputMode);
    g_Term.InputCPValid = (GetConsoleCP() != 0);
    if (g_Term.InputCPValid)
    {
        g_Term.OrigInputCP = GetConsoleCP();
        if (!SetConsoleCP(CP_UTF8))
        {
            printf("[WARN] SetConsoleCP(CP_UTF8) failed (err=%lu)\n", GetLastError());
            g_Term.InputCPValid = FALSE;
        }
    }
    if (g_Term.InputModeValid)
    {
        inputMode = ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
#ifdef ENABLE_QUICK_EDIT_MODE
        inputMode &= ~ENABLE_QUICK_EDIT_MODE;
#endif
#ifdef ENABLE_INSERT_MODE
        inputMode &= ~ENABLE_INSERT_MODE;
#endif
        inputMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;

        if (!SetConsoleMode(g_Term.hStdin, inputMode))
        {
            printf("[WARN] SetConsoleMode(input) failed (err=%lu), VT input may not work\n",
                   GetLastError());
            /* Fall back to raw mode without VT input */
            inputMode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
            SetConsoleMode(g_Term.hStdin, inputMode);
        }

        /* Remove pre-existing mouse/window events from the input queue. */
        FlushConsoleInputBuffer(g_Term.hStdin);
    }
    else
    {
        printf("[WARN] GetConsoleMode(input) failed (err=%lu), stdin may be redirected\n",
               GetLastError());
    }

    /* Save and configure output mode */
    g_Term.OutputModeValid = GetConsoleMode(g_Term.hStdout, &g_Term.OrigOutputMode);
    g_Term.OutputCPValid = (GetConsoleOutputCP() != 0);
    if (g_Term.OutputCPValid)
    {
        g_Term.OrigOutputCP = GetConsoleOutputCP();
        if (!SetConsoleOutputCP(CP_UTF8))
        {
            printf("[WARN] SetConsoleOutputCP(CP_UTF8) failed (err=%lu)\n", GetLastError());
            g_Term.OutputCPValid = FALSE;
        }
    }
    if (g_Term.OutputModeValid)
    {
        outputMode = ENABLE_PROCESSED_OUTPUT |
                     ENABLE_WRAP_AT_EOL_OUTPUT |
                     ENABLE_VIRTUAL_TERMINAL_PROCESSING;
#ifdef DISABLE_NEWLINE_AUTO_RETURN
        outputMode |= DISABLE_NEWLINE_AUTO_RETURN;
#endif

        if (!SetConsoleMode(g_Term.hStdout, outputMode))
        {
            printf("[WARN] SetConsoleMode(output) failed (err=%lu)\n", GetLastError());
        }
    }

    TermLog("Terminal initialized (PTY %lu)\n", ptyIndex);
    return TRUE;
}

/*
 * TerminalDestroy - Restore original console modes and clean up.
 */
void TerminalDestroy(void)
{
    g_Term.Running = FALSE;

    /* Wait for threads to finish */
    if (g_Term.hInputThread)
    {
        WaitForSingleObject(g_Term.hInputThread, 2000);
        CloseHandle(g_Term.hInputThread);
        g_Term.hInputThread = NULL;
    }

    if (g_Term.hOutputThread)
    {
        WaitForSingleObject(g_Term.hOutputThread, 2000);
        CloseHandle(g_Term.hOutputThread);
        g_Term.hOutputThread = NULL;
    }

    /* Restore console modes */
    if (g_Term.InputModeValid)
    {
        SetConsoleMode(g_Term.hStdin, g_Term.OrigInputMode);
    }
    if (g_Term.InputCPValid)
    {
        SetConsoleCP(g_Term.OrigInputCP);
    }
    if (g_Term.OutputModeValid)
    {
        SetConsoleMode(g_Term.hStdout, g_Term.OrigOutputMode);
    }
    if (g_Term.OutputCPValid)
    {
        SetConsoleOutputCP(g_Term.OrigOutputCP);
    }

    TermLog("Terminal destroyed\n");
}

/*
 * TerminalRun - Start the input and output threads.
 * Blocks until g_Term.Running is set to FALSE (by TerminalStop or TerminalDestroy).
 */
BOOL TerminalRun(void)
{
    g_Term.hInputThread = (HANDLE)_beginthreadex(
        NULL, 0, TerminalInputThreadProc, NULL, 0, NULL);
    if (!g_Term.hInputThread)
    {
        printf("[FAIL] Failed to create input thread (err=%lu)\n", GetLastError());
        return FALSE;
    }

    g_Term.hOutputThread = (HANDLE)_beginthreadex(
        NULL, 0, TerminalOutputThreadProc, NULL, 0, NULL);
    if (!g_Term.hOutputThread)
    {
        printf("[FAIL] Failed to create output thread (err=%lu)\n", GetLastError());
        g_Term.Running = FALSE;
        WaitForSingleObject(g_Term.hInputThread, 2000);
        CloseHandle(g_Term.hInputThread);
        g_Term.hInputThread = NULL;
        return FALSE;
    }

    TermLog("Terminal I/O threads running\n");
    return TRUE;
}

/*
 * TerminalStop - Signal both threads to stop. Does not block.
 */
void TerminalStop(void)
{
    g_Term.Running = FALSE;
}

/*
 * TerminalIsRunning - Check if the terminal bridge is still active.
 */
BOOL TerminalIsRunning(void)
{
    return g_Term.Running;
}

/*
 * TerminalWaitForExit - Block until both I/O threads exit.
 * timeout: milliseconds to wait, or INFINITE.
 */
DWORD TerminalWaitForExit(DWORD timeout)
{
    HANDLE threads[2];
    DWORD count = 0;

    if (g_Term.hOutputThread) threads[count++] = g_Term.hOutputThread;
    if (g_Term.hInputThread)  threads[count++] = g_Term.hInputThread;

    if (count == 0)
        return WAIT_OBJECT_0;

    return WaitForMultipleObjects(count, threads, TRUE, timeout);
}

/*
 * TerminalGetSize - Query current console window size.
 */
BOOL TerminalGetSize(USHORT *rows, USHORT *cols)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hOut = g_Term.hStdout;

    /* Can be called before TerminalInitialize, so fall back to GetStdHandle */
    if (!hOut || hOut == INVALID_HANDLE_VALUE)
        hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    if (!hOut || hOut == INVALID_HANDLE_VALUE ||
        !GetConsoleScreenBufferInfo(hOut, &csbi))
    {
        TermLog("GetConsoleScreenBufferInfo failed (err=%lu)\n", GetLastError());
        *rows = 24;
        *cols = 80;
        return FALSE;
    }

    *rows = (USHORT)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
    *cols = (USHORT)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
    return TRUE;
}
