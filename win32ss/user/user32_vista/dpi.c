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

INT APIENTRY
NtGdiGetDeviceCaps(HDC hDC, INT Index);

NTSYSAPI LONG WINAPI
RtlQueryActivationContextApplicationSettings(
    DWORD Flags,
    HANDLE ActivationContext,
    const WCHAR *Namespace,
    const WCHAR *SettingName,
    WCHAR *Buffer,
    SIZE_T BufferSize,
    SIZE_T *Written);

ULONG APIENTRY
NtUserGetProcessDpiAwarenessContext(HANDLE hProcess);

UINT WINAPI
GetDpiForSystem(VOID);

ULONG APIENTRY
NtUserGetThreadDpiAwarenessContext(VOID);

ULONG APIENTRY
NtUserGetWindowDpiAwarenessContext(HWND hWnd);

ULONG APIENTRY
NtUserSetProcessDpiAwarenessContext(ULONG DpiContext, ULONG Flags);

ULONG APIENTRY
NtUserSetThreadDpiAwarenessContext(ULONG DpiContext);

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

static BOOL
IsDpiManifestSpace(_In_ WCHAR Character)
{
    return Character == L' ' || Character == L'\t' || Character == L'\r' || Character == L'\n';
}

static DPI_AWARENESS_CONTEXT
GetManifestDpiContext(_In_reads_(Length) const WCHAR *Value, _In_ SIZE_T Length)
{
    WCHAR Name[32];

    while (Length && IsDpiManifestSpace(*Value))
    {
        ++Value;
        --Length;
    }
    while (Length && IsDpiManifestSpace(Value[Length - 1]))
        --Length;

    if (!Length || Length >= ARRAYSIZE(Name))
        return NULL;

    RtlCopyMemory(Name, Value, Length * sizeof(*Name));
    Name[Length] = UNICODE_NULL;

    if (!lstrcmpiW(Name, L"unaware"))
        return DPI_AWARENESS_CONTEXT_UNAWARE;
    if (!lstrcmpiW(Name, L"system"))
        return DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
    if (!lstrcmpiW(Name, L"permonitor"))
        return DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;
    if (!lstrcmpiW(Name, L"permonitorv2"))
        return DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;

    return NULL;
}

VOID
WINAPI
User32InitializeDpiAwareness(VOID)
{
    static const WCHAR Namespace2005[] = L"http://schemas.microsoft.com/SMI/2005/WindowsSettings";
    static const WCHAR Namespace2016[] = L"http://schemas.microsoft.com/SMI/2016/WindowsSettings";
    WCHAR Buffer[256];
    WCHAR *Start, *End;
    DPI_AWARENESS_CONTEXT Context;

    if (RtlQueryActivationContextApplicationSettings(
            0,
            NULL,
            Namespace2016,
            L"dpiAwareness",
            Buffer,
            ARRAYSIZE(Buffer),
            NULL) >= 0)
    {
        for (Start = Buffer; *Start; Start = *End ? End + 1 : End)
        {
            End = Start;
            while (*End && *End != L',')
                ++End;

            Context = GetManifestDpiContext(Start, End - Start);
            if (Context)
            {
                SetProcessDpiAwarenessContext(Context);
                return;
            }

            if (!*End)
                break;
        }
        return;
    }

    if (RtlQueryActivationContextApplicationSettings(
            0,
            NULL,
            Namespace2005,
            L"dpiAware",
            Buffer,
            ARRAYSIZE(Buffer),
            NULL) < 0)
    {
        return;
    }

    if (!lstrcmpiW(Buffer, L"true"))
        Context = DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
    else if (!lstrcmpiW(Buffer, L"true/pm") || !lstrcmpiW(Buffer, L"per monitor"))
        Context = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;
    else
        Context = DPI_AWARENESS_CONTEXT_UNAWARE;

    SetProcessDpiAwarenessContext(Context);
}

static UINT
GetRawSystemDpi(VOID)
{
    HDC hDC;
    UINT Dpi = USER_DEFAULT_SCREEN_DPI;

    hDC = NtUserGetDC(NULL);
    if (hDC)
    {
        /* Bypass the caller-aware GDI32 view and read the physical PDEV cap. */
        Dpi = NtGdiGetDeviceCaps(hDC, LOGPIXELSY);
        ReleaseDC(NULL, hDC);
    }

    return Dpi ? Dpi : USER_DEFAULT_SCREEN_DPI;
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
    return NtUserGetThreadDpiAwarenessContext();
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
    UINT SystemDpi = GetRawSystemDpi();
    UINT Context = GetNtUserDpiContext(dpiContext, SystemDpi);

    if (!IsValidNtUserDpiContext(Context, SystemDpi))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    return (DPI_AWARENESS_CONTEXT)(ULONG_PTR)
        NtUserSetThreadDpiAwarenessContext(Context);
}

BOOL
WINAPI
AreDpiAwarenessContextsEqual(_In_ DPI_AWARENESS_CONTEXT Context1, _In_ DPI_AWARENESS_CONTEXT Context2)
{
    UINT Value1 = GetNtUserDpiContext(Context1, GetRawSystemDpi());
    UINT Value2 = GetNtUserDpiContext(Context2, GetRawSystemDpi());

    if (!IsValidNtUserDpiContext(Value1, 0) || !IsValidNtUserDpiContext(Value2, 0))
    {
        return FALSE;
    }

    return (Value1 & ~NTUSER_DPI_CONTEXT_FLAG_PROCESS) == (Value2 & ~NTUSER_DPI_CONTEXT_FLAG_PROCESS);
}

DPI_AWARENESS
WINAPI
GetAwarenessFromDpiAwarenessContext(_In_ DPI_AWARENESS_CONTEXT Context)
{
    UINT Value = GetNtUserDpiContext(Context, GetRawSystemDpi());

    if (!IsValidNtUserDpiContext(Value, 0))
        return DPI_AWARENESS_INVALID;

    return (DPI_AWARENESS)NTUSER_DPI_CONTEXT_GET_AWARENESS(Value);
}

UINT
WINAPI
GetDpiFromDpiAwarenessContext(_In_ DPI_AWARENESS_CONTEXT Context)
{
    UINT Value = GetNtUserDpiContext(Context, GetRawSystemDpi());
    return NTUSER_DPI_CONTEXT_GET_DPI(Value);
}

BOOL
WINAPI
IsValidDpiAwarenessContext(_In_ DPI_AWARENESS_CONTEXT Context)
{
    return IsValidNtUserDpiContext(GetNtUserDpiContext(Context, GetRawSystemDpi()), 0);
}

BOOL
WINAPI
IsProcessDPIAware(VOID)
{
    UINT Context = GetThreadNtUserDpiContext();

    return NTUSER_DPI_CONTEXT_GET_AWARENESS(Context) != DPI_AWARENESS_UNAWARE;
}

UINT
WINAPI
GetDpiForSystem(VOID)
{
    if (!IsProcessDPIAware())
        return USER_DEFAULT_SCREEN_DPI;

    return GetRawSystemDpi();
}

DPI_AWARENESS_CONTEXT
WINAPI
GetDpiAwarenessContextForProcess(_In_opt_ HANDLE Process)
{
    ULONG Context;

    if (!Process)
        Process = GetCurrentProcess();

    Context = NtUserGetProcessDpiAwarenessContext(Process);
    return Context ? (DPI_AWARENESS_CONTEXT)(ULONG_PTR)Context : NULL;
}

DPI_AWARENESS_CONTEXT
WINAPI
GetWindowDpiAwarenessContext(_In_ HWND hWnd)
{
    ULONG Context = NtUserGetWindowDpiAwarenessContext(hWnd);
    return Context ? (DPI_AWARENESS_CONTEXT)(ULONG_PTR)Context : NULL;
}

UINT
WINAPI
GetDpiForWindow(
    _In_ HWND hWnd)
{
    DPI_AWARENESS_CONTEXT Context = GetWindowDpiAwarenessContext(hWnd);

    if (!Context)
        return 0;

    if (GetAwarenessFromDpiAwarenessContext(Context) == DPI_AWARENESS_UNAWARE)
        return USER_DEFAULT_SCREEN_DPI;

    return GetRawSystemDpi();
}

BOOL
WINAPI
SetProcessDpiAwarenessContext(_In_ DPI_AWARENESS_CONTEXT Context)
{
    UINT SystemDpi = GetRawSystemDpi();
    UINT Value = GetNtUserDpiContext(Context, SystemDpi);

    if (!IsValidNtUserDpiContext(Value, SystemDpi))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return !!NtUserSetProcessDpiAwarenessContext(Value, 0);
}

BOOL
WINAPI
SetProcessDPIAware(VOID)
{
    return SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
}

BOOL
WINAPI
GetProcessDpiAwarenessInternal(_In_opt_ HANDLE Process, _Out_ DPI_AWARENESS *Awareness)
{
    ULONG Context;

    if (!Awareness)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!Process)
        Process = GetCurrentProcess();

    *Awareness = DPI_AWARENESS_INVALID;
    Context = NtUserGetProcessDpiAwarenessContext(Process);
    if (!Context)
    {
        if (GetLastError() == ERROR_INVALID_HANDLE)
            SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *Awareness = (DPI_AWARENESS)NTUSER_DPI_CONTEXT_GET_AWARENESS(Context);
    return TRUE;
}

BOOL
WINAPI
SetProcessDpiAwarenessInternal(_In_ DPI_AWARENESS Awareness)
{
    static const DPI_AWARENESS_CONTEXT Contexts[] =
    {
        DPI_AWARENESS_CONTEXT_UNAWARE,
        DPI_AWARENESS_CONTEXT_SYSTEM_AWARE,
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
    };

    if (Awareness < DPI_AWARENESS_UNAWARE || Awareness > DPI_AWARENESS_PER_MONITOR_AWARE)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return SetProcessDpiAwarenessContext(Contexts[Awareness]);
}

BOOL
WINAPI
GetDpiForMonitorInternal(_In_ HMONITOR Monitor, _In_ UINT Type, _Out_ UINT *DpiX, _Out_ UINT *DpiY)
{
    MONITORINFO MonitorInfo = { sizeof(MonitorInfo) };
    DPI_AWARENESS Awareness;
    UINT Dpi;

    if (Type > 2)
    {
        SetLastError(ERROR_BAD_ARGUMENTS);
        return FALSE;
    }

    if (!DpiX || !DpiY)
    {
        SetLastError(ERROR_INVALID_ADDRESS);
        return FALSE;
    }

    if (!GetMonitorInfoW(Monitor, &MonitorInfo))
    {
        SetLastError(ERROR_INVALID_MONITOR_HANDLE);
        return FALSE;
    }

    /*
     * ReactOS currently has one logical DPI for the desktop. Raw and angular
     * monitor DPI require EDID physical dimensions, which GOP does not expose.
     */
    Awareness = GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext());
    Dpi = (Awareness == DPI_AWARENESS_UNAWARE) ? USER_DEFAULT_SCREEN_DPI : GetRawSystemDpi();
    *DpiX = Dpi;
    *DpiY = Dpi;
    return TRUE;
}

static INT
ScaleDpiValue(INT Value, UINT SourceDpi, UINT TargetDpi)
{
    LONGLONG Magnitude;

    if (!SourceDpi)
        SourceDpi = USER_DEFAULT_SCREEN_DPI;

    if (Value >= 0)
        return (INT)(((LONGLONG)Value * TargetDpi + SourceDpi / 2) / SourceDpi);

    Magnitude = -(LONGLONG)Value;
    return -(INT)((Magnitude * TargetDpi + SourceDpi / 2) / SourceDpi);
}

static VOID
ScaleLogFontForDpi(_Inout_ PLOGFONTW LogFont, UINT SourceDpi, UINT TargetDpi)
{
    LogFont->lfHeight = ScaleDpiValue(LogFont->lfHeight, SourceDpi, TargetDpi);
    LogFont->lfWidth = ScaleDpiValue(LogFont->lfWidth, SourceDpi, TargetDpi);
}

BOOL
WINAPI
SystemParametersInfoForDpi(_In_ UINT Action, _In_ UINT Param, _Inout_opt_ PVOID Data, _In_ UINT Flags, _In_ UINT Dpi)
{
    UINT SystemDpi = GetRawSystemDpi();

    if (!Data || !Dpi)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    switch (Action)
    {
        case SPI_GETICONTITLELOGFONT:
        {
            PLOGFONTW LogFont = Data;

            if (!SystemParametersInfoW(Action, Param, Data, Flags))
                return FALSE;
            ScaleLogFontForDpi(LogFont, SystemDpi, Dpi);
            return TRUE;
        }

        case SPI_GETNONCLIENTMETRICS:
        {
            PNONCLIENTMETRICSW Metrics = Data;
            NONCLIENTMETRICSW LocalMetrics;
            UINT Size = Metrics->cbSize;

            if (Size != sizeof(*Metrics) && Size != FIELD_OFFSET(NONCLIENTMETRICSW, iPaddedBorderWidth))
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }

            LocalMetrics.cbSize = sizeof(LocalMetrics);
            if (!SystemParametersInfoW(Action, sizeof(LocalMetrics), &LocalMetrics, Flags))
            {
                return FALSE;
            }

            LocalMetrics.iBorderWidth = ScaleDpiValue(LocalMetrics.iBorderWidth, SystemDpi, Dpi);
            LocalMetrics.iScrollWidth = ScaleDpiValue(LocalMetrics.iScrollWidth, SystemDpi, Dpi);
            LocalMetrics.iScrollHeight = ScaleDpiValue(LocalMetrics.iScrollHeight, SystemDpi, Dpi);
            LocalMetrics.iCaptionWidth = ScaleDpiValue(LocalMetrics.iCaptionWidth, SystemDpi, Dpi);
            LocalMetrics.iCaptionHeight = ScaleDpiValue(LocalMetrics.iCaptionHeight, SystemDpi, Dpi);
            LocalMetrics.iSmCaptionWidth = ScaleDpiValue(LocalMetrics.iSmCaptionWidth, SystemDpi, Dpi);
            LocalMetrics.iSmCaptionHeight = ScaleDpiValue(LocalMetrics.iSmCaptionHeight, SystemDpi, Dpi);
            LocalMetrics.iMenuWidth = ScaleDpiValue(LocalMetrics.iMenuWidth, SystemDpi, Dpi);
            LocalMetrics.iMenuHeight = ScaleDpiValue(LocalMetrics.iMenuHeight, SystemDpi, Dpi);
#if (WINVER >= 0x0600)
            LocalMetrics.iPaddedBorderWidth = ScaleDpiValue(LocalMetrics.iPaddedBorderWidth, SystemDpi, Dpi);
#endif
            ScaleLogFontForDpi(&LocalMetrics.lfCaptionFont, SystemDpi, Dpi);
            ScaleLogFontForDpi(&LocalMetrics.lfSmCaptionFont, SystemDpi, Dpi);
            ScaleLogFontForDpi(&LocalMetrics.lfMenuFont, SystemDpi, Dpi);
            ScaleLogFontForDpi(&LocalMetrics.lfStatusFont, SystemDpi, Dpi);
            ScaleLogFontForDpi(&LocalMetrics.lfMessageFont, SystemDpi, Dpi);
            LocalMetrics.cbSize = Size;
            CopyMemory(Metrics, &LocalMetrics, Size);
            return TRUE;
        }

        case SPI_GETICONMETRICS:
        {
            PICONMETRICSW Metrics = Data;
            ICONMETRICSW LocalMetrics;

            if (Metrics->cbSize != sizeof(*Metrics))
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }

            LocalMetrics.cbSize = sizeof(LocalMetrics);
            if (!SystemParametersInfoW(Action, sizeof(LocalMetrics), &LocalMetrics, Flags))
            {
                return FALSE;
            }

            LocalMetrics.iHorzSpacing = ScaleDpiValue(LocalMetrics.iHorzSpacing, SystemDpi, Dpi);
            LocalMetrics.iVertSpacing = ScaleDpiValue(LocalMetrics.iVertSpacing, SystemDpi, Dpi);
            ScaleLogFontForDpi(&LocalMetrics.lfFont, SystemDpi, Dpi);
            *Metrics = LocalMetrics;
            return TRUE;
        }
    }

    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

INT
WINAPI
GetSystemMetricsForDpi(_In_ INT Index, _In_ UINT Dpi)
{
    NONCLIENTMETRICSW Metrics;
    ICONMETRICSW IconMetrics;
    UINT Value;

    if (!Dpi)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    switch (Index)
    {
        case SM_CXVSCROLL:
        case SM_CYHSCROLL:
            Metrics.cbSize = sizeof(Metrics);
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
            {
                return 0;
            }
            return max(Metrics.iScrollWidth, 8);

        case SM_CYCAPTION:
            Metrics.cbSize = sizeof(Metrics);
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
            {
                return 0;
            }
            return Metrics.iCaptionHeight + 1;

        case SM_CYVTHUMB:
        case SM_CXHTHUMB:
        case SM_CYVSCROLL:
        case SM_CXHSCROLL:
            Metrics.cbSize = sizeof(Metrics);
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
            {
                return 0;
            }
            return max(Metrics.iScrollHeight, 8);

        case SM_CXICON:
        case SM_CYICON:
            return ScaleDpiValue(32, USER_DEFAULT_SCREEN_DPI, Dpi);

        case SM_CXCURSOR:
        case SM_CYCURSOR:
            Value = ScaleDpiValue(32, USER_DEFAULT_SCREEN_DPI, Dpi);
            if (Value >= 64)
                return 64;
            if (Value >= 48)
                return 48;
            return 32;

        case SM_CYMENU:
            Metrics.cbSize = sizeof(Metrics);
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
            {
                return 0;
            }
            return Metrics.iMenuHeight + 1;

        case SM_CXSIZE:
            Metrics.cbSize = sizeof(Metrics);
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
            {
                return 0;
            }
            return max(Metrics.iCaptionWidth, 8);

        case SM_CYSIZE:
            Metrics.cbSize = sizeof(Metrics);
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
            {
                return 0;
            }
            return Metrics.iCaptionHeight;

        case SM_CXFRAME:
        case SM_CYFRAME:
            Metrics.cbSize = sizeof(Metrics);
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
            {
                return 0;
            }
            return max(Metrics.iBorderWidth, 1) + 3;

        case SM_CXICONSPACING:
        case SM_CYICONSPACING:
            IconMetrics.cbSize = sizeof(IconMetrics);
            if (!SystemParametersInfoForDpi(SPI_GETICONMETRICS, sizeof(IconMetrics), &IconMetrics, 0, Dpi))
            {
                return 0;
            }
            return (Index == SM_CXICONSPACING) ? IconMetrics.iHorzSpacing : IconMetrics.iVertSpacing;

        case SM_CXSMICON:
        case SM_CYSMICON:
            return ScaleDpiValue(16, USER_DEFAULT_SCREEN_DPI, Dpi) & ~1;

        case SM_CYSMCAPTION:
            Metrics.cbSize = sizeof(Metrics);
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
            {
                return 0;
            }
            return Metrics.iSmCaptionHeight + 1;

        case SM_CXSMSIZE:
            Metrics.cbSize = sizeof(Metrics);
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
            {
                return 0;
            }
            return Metrics.iSmCaptionWidth;

        case SM_CYSMSIZE:
            Metrics.cbSize = sizeof(Metrics);
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
            {
                return 0;
            }
            return Metrics.iSmCaptionHeight;

        case SM_CXMENUSIZE:
            Metrics.cbSize = sizeof(Metrics);
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
            {
                return 0;
            }
            return Metrics.iMenuWidth;

        case SM_CYMENUSIZE:
            Metrics.cbSize = sizeof(Metrics);
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
            {
                return 0;
            }
            return Metrics.iMenuHeight;

        case SM_CXMENUCHECK:
        case SM_CYMENUCHECK:
        {
            HDC hDC;
            HFONT Font, OldFont;
            TEXTMETRICW TextMetrics;

            Metrics.cbSize = sizeof(Metrics);
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
            {
                return 0;
            }

            hDC = NtUserGetDC(NULL);
            Font = CreateFontIndirectW(&Metrics.lfMenuFont);
            if (!hDC || !Font)
            {
                if (Font)
                    DeleteObject(Font);
                if (hDC)
                    ReleaseDC(NULL, hDC);
                return 13;
            }

            OldFont = SelectObject(hDC, Font);
            if (!OldFont || !GetTextMetricsW(hDC, &TextMetrics))
                Value = 13;
            else
                Value = (TextMetrics.tmHeight + TextMetrics.tmExternalLeading - 1) | 1;
            if (OldFont)
                SelectObject(hDC, OldFont);
            DeleteObject(Font);
            ReleaseDC(NULL, hDC);
            return Value;
        }
    }

    return GetSystemMetrics(Index);
}

BOOL
WINAPI
AdjustWindowRectExForDpi(_Inout_ LPRECT Rect, _In_ DWORD Style, _In_ BOOL Menu, _In_ DWORD ExStyle, _In_ UINT Dpi)
{
    NONCLIENTMETRICSW Metrics;
    INT Adjust = 0;

    if (!Rect || !Dpi)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Metrics.cbSize = sizeof(Metrics);
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0, Dpi))
    {
        return FALSE;
    }

    if ((ExStyle & (WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME)) == WS_EX_STATICEDGE)
    {
        Adjust = 1;
    }
    else if ((ExStyle & WS_EX_DLGMODALFRAME) || (Style & (WS_THICKFRAME | WS_DLGFRAME)))
    {
        Adjust = 2;
    }

    if (Style & WS_THICKFRAME)
        Adjust += Metrics.iBorderWidth + Metrics.iPaddedBorderWidth;

    if ((Style & (WS_BORDER | WS_DLGFRAME)) || (ExStyle & WS_EX_DLGMODALFRAME))
    {
        ++Adjust;
    }

    InflateRect(Rect, Adjust, Adjust);

    if ((Style & WS_CAPTION) == WS_CAPTION)
    {
        Rect->top -= (ExStyle & WS_EX_TOOLWINDOW) ? Metrics.iSmCaptionHeight + 1 : Metrics.iCaptionHeight + 1;
    }
    if (Menu)
        Rect->top -= Metrics.iMenuHeight + 1;

    if (ExStyle & WS_EX_CLIENTEDGE)
    {
        InflateRect(Rect, GetSystemMetrics(SM_CXEDGE), GetSystemMetrics(SM_CYEDGE));
    }

    return TRUE;
}

BOOL
WINAPI
LogicalToPhysicalPoint(
    _In_ HWND hWnd,
    _Inout_ POINT *Point)
{
    if (!Point || !IsWindow(hWnd))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    /* ReactOS currently uses one physical coordinate space per desktop. */
    return TRUE;
}

BOOL
WINAPI
EnableNonClientDpiScaling(
    _In_ HWND hWnd)
{
    /* Per-monitor non-client scaling is not implemented yet. */
    UNREFERENCED_PARAMETER(hWnd);
    return TRUE;
}
