/*
 * PROJECT:     ReactOS VMX Hypervisor Launcher tools
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Shared console log styling helpers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#include "log_style.h"

#include <stdio.h>
#include <string.h>

typedef struct _ROSL_LOG_STYLE
{
    const char *Tag;
    WORD Attr;
} ROSL_LOG_STYLE;

static const ROSL_LOG_STYLE g_LogStyles[] =
{
    { "[FAIL]",       FOREGROUND_RED | FOREGROUND_INTENSITY },
    { "[WARN]",       FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY },
    { "[OK]",         FOREGROUND_GREEN | FOREGROUND_INTENSITY },
    { "[DONE]",       FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY },
    { "[INFO]",       FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY },
    { "[STATE]",      FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY },
    { "[CPU]",        FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY },
    { "[TCP]",        FOREGROUND_GREEN | FOREGROUND_INTENSITY },
    { "[UDP]",        FOREGROUND_RED | FOREGROUND_GREEN },
    { "[DNS]",        FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY },
    { "[DHCP]",       FOREGROUND_BLUE | FOREGROUND_INTENSITY },
    { "[ICMP]",       FOREGROUND_GREEN | FOREGROUND_BLUE },
    { "[ICMP-FWD]",   FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY },
    { "[ICMP-LOCAL]", FOREGROUND_BLUE | FOREGROUND_INTENSITY },
    { "[NAT]",        FOREGROUND_BLUE | FOREGROUND_INTENSITY },
    { "[NET]",        FOREGROUND_GREEN | FOREGROUND_BLUE },
    { "[RX]",         FOREGROUND_GREEN },
    { "[TX]",         FOREGROUND_BLUE },
    { "[STAT]",       FOREGROUND_RED | FOREGROUND_BLUE },
    { "[PERF]",       FOREGROUND_RED | FOREGROUND_GREEN },
    { "[WAIT]",       FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE },
    { "[GUEST]",      FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY }
};

static INIT_ONCE g_LogInitOnce = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_LogLock;

static BOOL CALLBACK
RoslInitLogLock(
    PINIT_ONCE InitOnce,
    PVOID Parameter,
    PVOID *Context)
{
    (void)InitOnce;
    (void)Parameter;
    (void)Context;

    InitializeCriticalSection(&g_LogLock);
    return TRUE;
}

static void
RoslWriteRawStdout(
    HANDLE StdoutHandle,
    const char *Text,
    DWORD Length)
{
    DWORD Written;

    if (Text == NULL || Length == 0)
        return;

    if (StdoutHandle == INVALID_HANDLE_VALUE || StdoutHandle == NULL)
    {
        fwrite(Text, 1, Length, stdout);
        return;
    }

    while (Length > 0)
    {
        if (!WriteFile(StdoutHandle, Text, Length, &Written, NULL) || Written == 0)
            break;

        Text += Written;
        Length -= Written;
    }
}

static BOOL
RoslTryGetStyledAttr(
    const char *Text,
    SIZE_T Length,
    WORD *Attr)
{
    SIZE_T Index;

    if (Text == NULL || Length < 3 || Attr == NULL)
        return FALSE;

    for (Index = 0; Index < ARRAYSIZE(g_LogStyles); ++Index)
    {
        if (strlen(g_LogStyles[Index].Tag) == Length &&
            memcmp(Text, g_LogStyles[Index].Tag, Length) == 0)
        {
            *Attr = g_LogStyles[Index].Attr;
            return TRUE;
        }
    }

    return FALSE;
}

void
RoslWriteStyledLog(
    _In_z_ const char *Text)
{
    CONSOLE_SCREEN_BUFFER_INFO Info;
    HANDLE StdoutHandle;
    const char *Cursor;

    if (Text == NULL)
        return;

    InitOnceExecuteOnce(&g_LogInitOnce, RoslInitLogLock, NULL, NULL);
    EnterCriticalSection(&g_LogLock);

    StdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (StdoutHandle == INVALID_HANDLE_VALUE ||
        !GetConsoleScreenBufferInfo(StdoutHandle, &Info))
    {
        RoslWriteRawStdout(StdoutHandle, Text, (DWORD)strlen(Text));
        LeaveCriticalSection(&g_LogLock);
        return;
    }

    Cursor = Text;
    while (*Cursor != '\0')
    {
        const char *TagStart = strchr(Cursor, '[');

        if (TagStart == NULL)
        {
            RoslWriteRawStdout(StdoutHandle, Cursor, (DWORD)strlen(Cursor));
            break;
        }

        if (TagStart > Cursor)
        {
            RoslWriteRawStdout(StdoutHandle, Cursor, (DWORD)(TagStart - Cursor));
        }

        if (TagStart[1] == '\0')
        {
            RoslWriteRawStdout(StdoutHandle, TagStart, 1);
            break;
        }

        {
            const char *TagEnd = strchr(TagStart, ']');
            WORD TagAttr;

            if (TagEnd == NULL ||
                (TagEnd - TagStart) > 15 ||
                !RoslTryGetStyledAttr(TagStart, (SIZE_T)(TagEnd - TagStart + 1), &TagAttr))
            {
                RoslWriteRawStdout(StdoutHandle, TagStart, 1);
                Cursor = TagStart + 1;
                continue;
            }

            SetConsoleTextAttribute(StdoutHandle, TagAttr);
            RoslWriteRawStdout(StdoutHandle,
                               TagStart,
                               (DWORD)(TagEnd - TagStart + 1));
            SetConsoleTextAttribute(StdoutHandle, Info.wAttributes);
            Cursor = TagEnd + 1;
        }
    }

    LeaveCriticalSection(&g_LogLock);
}
