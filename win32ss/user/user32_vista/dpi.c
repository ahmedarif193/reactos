/*
 * PROJECT:     ReactOS Kernel - Vista+ APIs
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     DPI functions for user32 and user32_vista.
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <cbialo2@outlook.com>
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>

HDC APIENTRY
NtUserGetDC(HWND hWnd);

ULONG APIENTRY
NtUserGetProcessDpiAwarenessContext(HANDLE hProcess);

UINT WINAPI
GetDpiForSystem(VOID);

#define NDEBUG
#include <debug.h>

/* NTUSER DPI context encoding, shared with the Wine win32u pair:
   bits 0-3 awareness, 4-7 version, 8-16 DPI, high bits flags. */
#define NTUSER_DPI_CONTEXT_GET_AWARENESS(ctx)  ((ctx) & 0x0f)
#define NTUSER_DPI_CONTEXT_GET_VERSION(ctx)    (((ctx) & 0xf0) >> 4)
#define NTUSER_DPI_CONTEXT_GET_DPI(ctx)        (((ctx) & 0x1ff00) >> 8)
#define NTUSER_DPI_CONTEXT_GET_FLAGS(ctx)      ((ctx) & 0xfffe0000)
#define NTUSER_DPI_CONTEXT_FLAG_GDISCALED      0x40000000
#define NTUSER_DPI_CONTEXT_FLAG_PROCESS        0x80000000
#define NTUSER_DPI_CONTEXT_FLAG_VALID_MASK \
    (NTUSER_DPI_CONTEXT_FLAG_PROCESS | NTUSER_DPI_CONTEXT_FLAG_GDISCALED)

#define NTUSER_DPI_UNAWARE              0x00006010
#define NTUSER_DPI_UNAWARE_GDISCALED    0x40006010
#define NTUSER_DPI_PER_MONITOR_AWARE    0x00000012
#define NTUSER_DPI_PER_MONITOR_AWARE_V2 0x00000022

static DWORD ThreadDpiContextTlsIndex = TLS_OUT_OF_INDEXES;

static DWORD
GetThreadDpiContextTlsIndex(VOID)
{
    if (ThreadDpiContextTlsIndex == TLS_OUT_OF_INDEXES)
    {
        DWORD Index = TlsAlloc();
        if (Index != TLS_OUT_OF_INDEXES &&
            InterlockedCompareExchange((LONG volatile *)&ThreadDpiContextTlsIndex,
                                       (LONG)Index,
                                       (LONG)TLS_OUT_OF_INDEXES) != (LONG)TLS_OUT_OF_INDEXES)
        {
            /* Another thread won the race */
            TlsFree(Index);
        }
    }
    return ThreadDpiContextTlsIndex;
}

/* Map the public abstract handles onto concrete NTUSER context values */
static UINT
GetNtUserDpiContext(
    _In_ DPI_AWARENESS_CONTEXT dpiContext,
    _In_ UINT SystemDpi)
{
    switch ((ULONG_PTR)dpiContext)
    {
        case (ULONG_PTR)DPI_AWARENESS_CONTEXT_UNAWARE:
            return NTUSER_DPI_UNAWARE;
        case (ULONG_PTR)DPI_AWARENESS_CONTEXT_SYSTEM_AWARE:
            return 0x11 | (SystemDpi << 8);
        case (ULONG_PTR)DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE:
            return NTUSER_DPI_PER_MONITOR_AWARE;
        case (ULONG_PTR)DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2:
            return NTUSER_DPI_PER_MONITOR_AWARE_V2;
        case (ULONG_PTR)DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED:
            return NTUSER_DPI_UNAWARE_GDISCALED;
    }
    return (UINT)(ULONG_PTR)dpiContext;
}

static BOOL
IsValidNtUserDpiContext(
    _In_ UINT Context,
    _In_ UINT SystemDpi)
{
    switch (NTUSER_DPI_CONTEXT_GET_AWARENESS(Context))
    {
        case DPI_AWARENESS_UNAWARE:
            if (NTUSER_DPI_CONTEXT_GET_FLAGS(Context) & ~NTUSER_DPI_CONTEXT_FLAG_VALID_MASK) return FALSE;
            if (NTUSER_DPI_CONTEXT_GET_VERSION(Context) != 1) return FALSE;
            return NTUSER_DPI_CONTEXT_GET_DPI(Context) == 96;

        case DPI_AWARENESS_SYSTEM_AWARE:
            if (NTUSER_DPI_CONTEXT_GET_FLAGS(Context) & ~NTUSER_DPI_CONTEXT_FLAG_VALID_MASK) return FALSE;
            if (NTUSER_DPI_CONTEXT_GET_FLAGS(Context) & NTUSER_DPI_CONTEXT_FLAG_GDISCALED) return FALSE;
            if (NTUSER_DPI_CONTEXT_GET_VERSION(Context) != 1) return FALSE;
            return !SystemDpi || NTUSER_DPI_CONTEXT_GET_DPI(Context) == SystemDpi;

        case DPI_AWARENESS_PER_MONITOR_AWARE:
            if (NTUSER_DPI_CONTEXT_GET_FLAGS(Context) & ~NTUSER_DPI_CONTEXT_FLAG_VALID_MASK) return FALSE;
            if (NTUSER_DPI_CONTEXT_GET_FLAGS(Context) & NTUSER_DPI_CONTEXT_FLAG_GDISCALED) return FALSE;
            if (NTUSER_DPI_CONTEXT_GET_VERSION(Context) != 1 &&
                NTUSER_DPI_CONTEXT_GET_VERSION(Context) != 2) return FALSE;
            return NTUSER_DPI_CONTEXT_GET_DPI(Context) == 0;
    }
    return FALSE;
}

static UINT
GetThreadNtUserDpiContext(VOID)
{
    DWORD Index = GetThreadDpiContextTlsIndex();
    UINT Context = 0;

    if (Index != TLS_OUT_OF_INDEXES)
        Context = (UINT)(ULONG_PTR)TlsGetValue(Index);
    if (!Context)
        Context = NtUserGetProcessDpiAwarenessContext(GetCurrentProcess()) |
                  NTUSER_DPI_CONTEXT_FLAG_PROCESS;
    return Context;
}

DPI_AWARENESS_CONTEXT
WINAPI
GetThreadDpiAwarenessContext(VOID)
{
    return (DPI_AWARENESS_CONTEXT)(ULONG_PTR)GetThreadNtUserDpiContext();
}

DPI_AWARENESS_CONTEXT
WINAPI
SetThreadDpiAwarenessContext(
    _In_ DPI_AWARENESS_CONTEXT dpiContext)
{
    UINT SystemDpi = GetDpiForSystem();
    UINT Context = GetNtUserDpiContext(dpiContext, SystemDpi);
    DWORD Index;
    UINT Prev;

    if (!IsValidNtUserDpiContext(Context, SystemDpi))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    Index = GetThreadDpiContextTlsIndex();
    if (Index == TLS_OUT_OF_INDEXES)
        return 0;

    Prev = GetThreadNtUserDpiContext();

    /* Setting an inherit-from-process context clears the thread override */
    if (NTUSER_DPI_CONTEXT_GET_FLAGS(Context) & NTUSER_DPI_CONTEXT_FLAG_PROCESS)
        TlsSetValue(Index, NULL);
    else
        TlsSetValue(Index, (PVOID)(ULONG_PTR)Context);

    return (DPI_AWARENESS_CONTEXT)(ULONG_PTR)Prev;
}

/*
 * @stub
 */
UINT
WINAPI
GetDpiForSystem(VOID)
{
    HDC hDC;
    UINT Dpi;
    hDC = NtUserGetDC(NULL);
    Dpi = GetDeviceCaps(hDC, LOGPIXELSY);
    ReleaseDC(NULL, hDC);
    return Dpi;
}

/*
 * @stub
 */
UINT
WINAPI
GetDpiForWindow(
    _In_ HWND hWnd)
{
    UNIMPLEMENTED_ONCE;
    UNREFERENCED_PARAMETER(hWnd);
    return GetDpiForSystem();
}

/*
 * @stub
 */
BOOL
WINAPI
IsProcessDPIAware(VOID)
{
    UNIMPLEMENTED;
    return FALSE;
}

/*
 * @stub
 */
BOOL
WINAPI
SetProcessDPIAware(VOID)
{
    UNIMPLEMENTED;
    return FALSE;
}

/*
 * @stub
 */
BOOL
WINAPI
SetProcessDpiAwarenessContext(
    _In_ DPI_AWARENESS_CONTEXT context)
{
    UNIMPLEMENTED;
    return FALSE;
}

/*
 * @stub
 */
BOOL
WINAPI
GetProcessDpiAwarenessInternal(
    _In_  HANDLE process,
    _Out_ DPI_AWARENESS *awareness)
{
    UNIMPLEMENTED;
    return FALSE;
}

/*
 * @stub
 */
BOOL
WINAPI
SetProcessDpiAwarenessInternal(
    _In_ DPI_AWARENESS awareness)
{
    UNIMPLEMENTED;
    return FALSE;
}

/*
 * @stub
 */
BOOL
WINAPI
GetDpiForMonitorInternal(
    _In_  HMONITOR monitor,
    _In_  UINT type,
    _Out_ UINT *x,
    _Out_ UINT *y)
{
    UNIMPLEMENTED;
    return FALSE;
}

/*
 * @stub
 */
BOOL
WINAPI
LogicalToPhysicalPoint(
    _In_ HWND hwnd, 
    _Inout_ POINT *point )
{
    UNIMPLEMENTED;
    return TRUE;
}
