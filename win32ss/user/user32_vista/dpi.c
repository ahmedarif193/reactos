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

ULONG APIENTRY
NtUserSetProcessDpiAwarenessContext(ULONG DpiContext, ULONG Flags);

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

#define NTUSER_MDT_EFFECTIVE_DPI        0
#define NTUSER_MDT_ANGULAR_DPI          1
#define NTUSER_MDT_RAW_DPI              2

#define NTUSER_DPI_UNAWARE              0x00006010
#define NTUSER_DPI_SYSTEM_AWARE         0x00000011
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

BOOL
WINAPI
IsProcessDPIAware(VOID)
{
    UINT Context = GetThreadNtUserDpiContext();
    return NTUSER_DPI_CONTEXT_GET_AWARENESS(Context) != DPI_AWARENESS_UNAWARE;
}

BOOL
WINAPI
SetProcessDPIAware(VOID)
{
    UINT SystemDpi = GetDpiForSystem();
    NtUserSetProcessDpiAwarenessContext(NTUSER_DPI_SYSTEM_AWARE | (SystemDpi << 8), 0);
    return TRUE;
}

BOOL
WINAPI
SetProcessDpiAwarenessContext(
    _In_ DPI_AWARENESS_CONTEXT context)
{
    UINT SystemDpi = GetDpiForSystem();
    UINT Context = GetNtUserDpiContext(context, SystemDpi);

    if (!IsValidNtUserDpiContext(Context, SystemDpi))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Context &= ~NTUSER_DPI_CONTEXT_FLAG_PROCESS;
    return NtUserSetProcessDpiAwarenessContext(Context, 0) != 0;
}

BOOL
WINAPI
GetProcessDpiAwarenessInternal(
    _In_opt_ HANDLE process,
    _Out_ DPI_AWARENESS *awareness)
{
    ULONG Context;

    if (awareness == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (process == NULL)
        process = GetCurrentProcess();

    *awareness = DPI_AWARENESS_INVALID;
    Context = NtUserGetProcessDpiAwarenessContext(process);
    if (Context == 0)
    {
        if (GetLastError() == ERROR_INVALID_HANDLE)
            SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *awareness = (DPI_AWARENESS)NTUSER_DPI_CONTEXT_GET_AWARENESS(Context);
    return TRUE;
}

BOOL
WINAPI
SetProcessDpiAwarenessInternal(
    _In_ DPI_AWARENESS awareness)
{
    UINT SystemDpi = GetDpiForSystem();
    UINT Context;

    switch (awareness)
    {
        case DPI_AWARENESS_UNAWARE:
            Context = NTUSER_DPI_UNAWARE;
            break;
        case DPI_AWARENESS_SYSTEM_AWARE:
            Context = NTUSER_DPI_SYSTEM_AWARE | (SystemDpi << 8);
            break;
        case DPI_AWARENESS_PER_MONITOR_AWARE:
            Context = NTUSER_DPI_PER_MONITOR_AWARE;
            break;
        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }

    return NtUserSetProcessDpiAwarenessContext(Context, 0) != 0;
}

BOOL
WINAPI
GetDpiForMonitorInternal(
    _In_  HMONITOR monitor,
    _In_  UINT type,
    _Out_ UINT *x,
    _Out_ UINT *y)
{
    MONITORINFOEXW MonitorInfo;
    DPI_AWARENESS Awareness;
    HDC MonitorDC;
    UINT DpiX, DpiY;

    if (type > NTUSER_MDT_RAW_DPI)
    {
        SetLastError(ERROR_BAD_ARGUMENTS);
        return FALSE;
    }

    if (x == NULL || y == NULL)
    {
        SetLastError(ERROR_INVALID_ADDRESS);
        return FALSE;
    }

    ZeroMemory(&MonitorInfo, sizeof(MonitorInfo));
    MonitorInfo.cbSize = sizeof(MonitorInfo);
    if (!GetMonitorInfoW(monitor, (LPMONITORINFO)&MonitorInfo))
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    Awareness = (DPI_AWARENESS)NTUSER_DPI_CONTEXT_GET_AWARENESS(GetThreadNtUserDpiContext());
    if (Awareness == DPI_AWARENESS_UNAWARE)
    {
        *x = USER_DEFAULT_SCREEN_DPI;
        *y = USER_DEFAULT_SCREEN_DPI;
        return TRUE;
    }
    if (Awareness == DPI_AWARENESS_SYSTEM_AWARE)
    {
        *x = GetDpiForSystem();
        *y = *x;
        return TRUE;
    }

    if (type == NTUSER_MDT_RAW_DPI)
    {
        *x = 0;
        *y = 0;
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }

    MonitorDC = CreateDCW(L"DISPLAY", MonitorInfo.szDevice, NULL, NULL);
    if (MonitorDC == NULL)
    {
        *x = 0;
        *y = 0;
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }

    DpiX = GetDeviceCaps(MonitorDC, LOGPIXELSX);
    DpiY = GetDeviceCaps(MonitorDC, LOGPIXELSY);
    DeleteDC(MonitorDC);

    if (DpiX == 0) DpiX = GetDpiForSystem();
    if (DpiY == 0) DpiY = GetDpiForSystem();

    *x = DpiX;
    *y = DpiY;
    return TRUE;
}

BOOL
WINAPI
IsValidDpiAwarenessContext(
    _In_ DPI_AWARENESS_CONTEXT context)
{
    UINT SystemDpi = GetDpiForSystem();
    return IsValidNtUserDpiContext(GetNtUserDpiContext(context, SystemDpi), SystemDpi);
}

BOOL
WINAPI
AreDpiAwarenessContextsEqual(
    _In_ DPI_AWARENESS_CONTEXT context1,
    _In_ DPI_AWARENESS_CONTEXT context2)
{
    UINT SystemDpi = GetDpiForSystem();
    UINT Context1 = GetNtUserDpiContext(context1, SystemDpi);
    UINT Context2 = GetNtUserDpiContext(context2, SystemDpi);

    if (!Context1 || !Context2) return FALSE;
    if (!IsValidNtUserDpiContext(Context1, SystemDpi) || !IsValidNtUserDpiContext(Context2, SystemDpi)) return FALSE;
    return (Context1 & ~NTUSER_DPI_CONTEXT_FLAG_PROCESS) == (Context2 & ~NTUSER_DPI_CONTEXT_FLAG_PROCESS);
}

DPI_AWARENESS
WINAPI
GetAwarenessFromDpiAwarenessContext(
    _In_ DPI_AWARENESS_CONTEXT context)
{
    UINT Context = GetNtUserDpiContext(context, 0);

    if (!IsValidNtUserDpiContext(Context, 0)) return DPI_AWARENESS_INVALID;
    return (DPI_AWARENESS)NTUSER_DPI_CONTEXT_GET_AWARENESS(Context);
}

DPI_AWARENESS_CONTEXT
WINAPI
GetWindowDpiAwarenessContext(
    _In_ HWND hwnd)
{
    DWORD ProcessId;
    DWORD ThreadId;
    HANDLE Process;
    ULONG Context;

    if (!IsWindow(hwnd))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    ThreadId = GetWindowThreadProcessId(hwnd, &ProcessId);
    if (ThreadId == GetCurrentThreadId())
    {
        Context = (ULONG)(ULONG_PTR)GetThreadDpiAwarenessContext();
        return (DPI_AWARENESS_CONTEXT)(ULONG_PTR)(Context & ~NTUSER_DPI_CONTEXT_FLAG_PROCESS);
    }

    Process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, ProcessId);
    if (Process == NULL) return NULL;
    Context = NtUserGetProcessDpiAwarenessContext(Process);
    CloseHandle(Process);
    return (DPI_AWARENESS_CONTEXT)(ULONG_PTR)Context;
}

static INT
ScaleDpiValue(
    _In_ INT Value,
    _In_ UINT SourceDpi,
    _In_ UINT TargetDpi)
{
    LONGLONG Magnitude;

    if (SourceDpi == 0)
        SourceDpi = USER_DEFAULT_SCREEN_DPI;

    if (Value >= 0)
        return (INT)(((LONGLONG)Value * TargetDpi + SourceDpi / 2) / SourceDpi);

    Magnitude = -(LONGLONG)Value;
    return -(INT)((Magnitude * TargetDpi + SourceDpi / 2) / SourceDpi);
}

static VOID
ScaleLogFontForDpi(
    _Inout_ PLOGFONTW LogFont,
    _In_ UINT SourceDpi,
    _In_ UINT TargetDpi)
{
    LogFont->lfHeight = ScaleDpiValue(LogFont->lfHeight, SourceDpi, TargetDpi);
    LogFont->lfWidth = ScaleDpiValue(LogFont->lfWidth, SourceDpi, TargetDpi);
}

static BOOL
GetNonClientMetricsForDpi(
    _Out_ PNONCLIENTMETRICSW Metrics,
    _In_ UINT Dpi)
{
    UINT SystemDpi = GetDpiForSystem();

    Metrics->cbSize = sizeof(*Metrics);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(*Metrics), Metrics, 0))
        return FALSE;

    Metrics->iBorderWidth = ScaleDpiValue(Metrics->iBorderWidth, SystemDpi, Dpi);
    Metrics->iScrollWidth = ScaleDpiValue(Metrics->iScrollWidth, SystemDpi, Dpi);
    Metrics->iScrollHeight = ScaleDpiValue(Metrics->iScrollHeight, SystemDpi, Dpi);
    Metrics->iCaptionWidth = ScaleDpiValue(Metrics->iCaptionWidth, SystemDpi, Dpi);
    Metrics->iCaptionHeight = ScaleDpiValue(Metrics->iCaptionHeight, SystemDpi, Dpi);
    Metrics->iSmCaptionWidth = ScaleDpiValue(Metrics->iSmCaptionWidth, SystemDpi, Dpi);
    Metrics->iSmCaptionHeight = ScaleDpiValue(Metrics->iSmCaptionHeight, SystemDpi, Dpi);
    Metrics->iMenuWidth = ScaleDpiValue(Metrics->iMenuWidth, SystemDpi, Dpi);
    Metrics->iMenuHeight = ScaleDpiValue(Metrics->iMenuHeight, SystemDpi, Dpi);
    /* ReactOS paints and reports thick frames without a separate padded border. */
    Metrics->iPaddedBorderWidth = 0;
    ScaleLogFontForDpi(&Metrics->lfCaptionFont, SystemDpi, Dpi);
    ScaleLogFontForDpi(&Metrics->lfSmCaptionFont, SystemDpi, Dpi);
    ScaleLogFontForDpi(&Metrics->lfMenuFont, SystemDpi, Dpi);
    ScaleLogFontForDpi(&Metrics->lfStatusFont, SystemDpi, Dpi);
    ScaleLogFontForDpi(&Metrics->lfMessageFont, SystemDpi, Dpi);
    return TRUE;
}

static BOOL
GetIconMetricsForDpi(
    _Out_ PICONMETRICSW Metrics,
    _In_ UINT Dpi)
{
    UINT SystemDpi = GetDpiForSystem();

    Metrics->cbSize = sizeof(*Metrics);
    if (!SystemParametersInfoW(SPI_GETICONMETRICS, sizeof(*Metrics), Metrics, 0))
        return FALSE;

    Metrics->iHorzSpacing = ScaleDpiValue(Metrics->iHorzSpacing, SystemDpi, Dpi);
    Metrics->iVertSpacing = ScaleDpiValue(Metrics->iVertSpacing, SystemDpi, Dpi);
    ScaleLogFontForDpi(&Metrics->lfFont, SystemDpi, Dpi);
    return TRUE;
}

BOOL
WINAPI
SystemParametersInfoForDpi(
    _In_ UINT action,
    _In_ UINT val,
    _Inout_ PVOID ptr,
    _In_ UINT winini,
    _In_ UINT dpi)
{
    UINT SystemDpi;

    UNREFERENCED_PARAMETER(winini);

    if (ptr == NULL || dpi == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    switch (action)
    {
        case SPI_GETNONCLIENTMETRICS:
            if (val != sizeof(NONCLIENTMETRICSW) || ((PNONCLIENTMETRICSW)ptr)->cbSize != sizeof(NONCLIENTMETRICSW)) break;
            return GetNonClientMetricsForDpi((PNONCLIENTMETRICSW)ptr, dpi);

        case SPI_GETICONMETRICS:
            if (val != sizeof(ICONMETRICSW) || ((PICONMETRICSW)ptr)->cbSize != sizeof(ICONMETRICSW)) break;
            return GetIconMetricsForDpi((PICONMETRICSW)ptr, dpi);

        case SPI_GETICONTITLELOGFONT:
            if (val != sizeof(LOGFONTW)) break;
            if (!SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(LOGFONTW), ptr, 0)) return FALSE;
            SystemDpi = GetDpiForSystem();
            ScaleLogFontForDpi((PLOGFONTW)ptr, SystemDpi, dpi);
            return TRUE;
    }

    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

int
WINAPI
GetSystemMetricsForDpi(
    _In_ int nIndex,
    _In_ UINT dpi)
{
    NONCLIENTMETRICSW Metrics;
    ICONMETRICSW IconMetrics;
    UINT Value;

    if (dpi == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    switch (nIndex)
    {
        case SM_CXVSCROLL:
        case SM_CYHSCROLL:
            if (!GetNonClientMetricsForDpi(&Metrics, dpi)) return 0;
            return max(Metrics.iScrollWidth, 8);

        case SM_CYCAPTION:
            if (!GetNonClientMetricsForDpi(&Metrics, dpi)) return 0;
            return Metrics.iCaptionHeight + 1;

        case SM_CYVTHUMB:
        case SM_CXHTHUMB:
        case SM_CYVSCROLL:
        case SM_CXHSCROLL:
            if (!GetNonClientMetricsForDpi(&Metrics, dpi)) return 0;
            return max(Metrics.iScrollHeight, 8);

        case SM_CXICON:
        case SM_CYICON:
            return ScaleDpiValue(32, USER_DEFAULT_SCREEN_DPI, dpi);

        case SM_CXCURSOR:
        case SM_CYCURSOR:
            Value = ScaleDpiValue(32, USER_DEFAULT_SCREEN_DPI, dpi);
            if (Value >= 64) return 64;
            if (Value >= 48) return 48;
            return 32;

        case SM_CYMENU:
            if (!GetNonClientMetricsForDpi(&Metrics, dpi)) return 0;
            return Metrics.iMenuHeight + 1;

        case SM_CXSIZE:
            if (!GetNonClientMetricsForDpi(&Metrics, dpi)) return 0;
            return max(Metrics.iCaptionWidth, 8);

        case SM_CYSIZE:
            if (!GetNonClientMetricsForDpi(&Metrics, dpi)) return 0;
            return Metrics.iCaptionHeight;

        case SM_CXFRAME:
        case SM_CYFRAME:
            if (!GetNonClientMetricsForDpi(&Metrics, dpi)) return 0;
            return max(Metrics.iBorderWidth, 1) + 3;

        case SM_CXICONSPACING:
        case SM_CYICONSPACING:
            if (!GetIconMetricsForDpi(&IconMetrics, dpi)) return 0;
            return nIndex == SM_CXICONSPACING ? IconMetrics.iHorzSpacing : IconMetrics.iVertSpacing;

        case SM_CXSMICON:
        case SM_CYSMICON:
            return ScaleDpiValue(16, USER_DEFAULT_SCREEN_DPI, dpi) & ~1;

        case SM_CYSMCAPTION:
            if (!GetNonClientMetricsForDpi(&Metrics, dpi)) return 0;
            return Metrics.iSmCaptionHeight + 1;

        case SM_CXSMSIZE:
            if (!GetNonClientMetricsForDpi(&Metrics, dpi)) return 0;
            return Metrics.iSmCaptionWidth;

        case SM_CYSMSIZE:
            if (!GetNonClientMetricsForDpi(&Metrics, dpi)) return 0;
            return Metrics.iSmCaptionHeight;

        case SM_CXMENUSIZE:
            if (!GetNonClientMetricsForDpi(&Metrics, dpi)) return 0;
            return Metrics.iMenuWidth;

        case SM_CYMENUSIZE:
            if (!GetNonClientMetricsForDpi(&Metrics, dpi)) return 0;
            return Metrics.iMenuHeight;

        case SM_CXMENUCHECK:
        case SM_CYMENUCHECK:
        {
            HDC hDC;
            HFONT Font, OldFont;
            TEXTMETRICW TextMetrics;

            if (!GetNonClientMetricsForDpi(&Metrics, dpi)) return 0;

            hDC = NtUserGetDC(NULL);
            Font = CreateFontIndirectW(&Metrics.lfMenuFont);
            if (hDC == NULL || Font == NULL)
            {
                if (Font != NULL) DeleteObject(Font);
                if (hDC != NULL) ReleaseDC(NULL, hDC);
                return 13;
            }

            OldFont = SelectObject(hDC, Font);
            if (OldFont == NULL || !GetTextMetricsW(hDC, &TextMetrics))
                Value = 13;
            else
                Value = (TextMetrics.tmHeight + TextMetrics.tmExternalLeading - 1) | 1;
            if (OldFont != NULL) SelectObject(hDC, OldFont);
            DeleteObject(Font);
            ReleaseDC(NULL, hDC);
            return Value;
        }
    }

    return GetSystemMetrics(nIndex);
}

BOOL
WINAPI
AdjustWindowRectExForDpi(
    _Inout_ LPRECT lpRect,
    _In_ DWORD dwStyle,
    _In_ BOOL bMenu,
    _In_ DWORD dwExStyle,
    _In_ UINT dpi)
{
    NONCLIENTMETRICSW Metrics;
    INT Adjust = 0;

    if (lpRect == NULL || dpi == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!GetNonClientMetricsForDpi(&Metrics, dpi))
        return FALSE;

    if ((dwExStyle & (WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME)) == WS_EX_STATICEDGE)
        Adjust = 1;
    else if ((dwExStyle & WS_EX_DLGMODALFRAME) || (dwStyle & (WS_THICKFRAME | WS_DLGFRAME)))
        Adjust = 2;

    if (dwStyle & WS_THICKFRAME)
        Adjust += Metrics.iBorderWidth + Metrics.iPaddedBorderWidth;

    if ((dwStyle & (WS_BORDER | WS_DLGFRAME)) || (dwExStyle & WS_EX_DLGMODALFRAME))
        ++Adjust;

    InflateRect(lpRect, Adjust, Adjust);

    if ((dwStyle & WS_CAPTION) == WS_CAPTION)
    {
        if (dwExStyle & WS_EX_TOOLWINDOW)
            lpRect->top -= Metrics.iSmCaptionHeight + 1;
        else
            lpRect->top -= Metrics.iCaptionHeight + 1;
    }
    if (bMenu)
        lpRect->top -= Metrics.iMenuHeight + 1;

    if (dwExStyle & WS_EX_CLIENTEDGE)
        InflateRect(lpRect, GetSystemMetrics(SM_CXEDGE), GetSystemMetrics(SM_CYEDGE));

    return TRUE;
}

BOOL
WINAPI
EnableNonClientDpiScaling(
    _In_ HWND hwnd)
{
    if (!IsWindow(hwnd))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
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

static BOOL
IntValidatePerMonitorDpiPoint(
    _In_ HWND hwnd,
    _In_ const POINT *point)
{
    RECT Rect;

    if (!point)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!GetWindowRect(hwnd, &Rect)) return FALSE;
    return point->x >= Rect.left && point->y >= Rect.top && point->x <= Rect.right && point->y <= Rect.bottom;
}

/* ReactOS currently uses physical coordinates for all DPI awareness modes. */
BOOL
WINAPI
LogicalToPhysicalPointForPerMonitorDPI(
    _In_ HWND hwnd,
    _Inout_ POINT *point)
{
    return IntValidatePerMonitorDpiPoint(hwnd, point);
}

/* ReactOS currently uses physical coordinates for all DPI awareness modes. */
BOOL
WINAPI
PhysicalToLogicalPointForPerMonitorDPI(
    _In_ HWND hwnd,
    _Inout_ POINT *point)
{
    return IntValidatePerMonitorDpiPoint(hwnd, point);
}
