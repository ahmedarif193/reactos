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
#include <wingdi.h>
#include <winuser.h>

HDC APIENTRY
NtUserGetDC(HWND hWnd);

#define NDEBUG
#include <debug.h>

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

/*
 * @implemented
 */
BOOL
WINAPI
EnableNonClientDpiScaling(
    _In_ HWND hwnd)
{
    /* ReactOS does not perform per-monitor non-client DPI scaling, so there is
     * nothing to enable. Report success so callers that invoke this from their
     * WM_NCCREATE handler (e.g. GLFW) proceed with window creation instead of
     * calling through a NULL import. */
    UNREFERENCED_PARAMETER(hwnd);
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
AdjustWindowRectExForDpi(
    _Inout_ LPRECT lpRect,
    _In_ DWORD dwStyle,
    _In_ BOOL bMenu,
    _In_ DWORD dwExStyle,
    _In_ UINT dpi)
{
    /* ReactOS renders at the system DPI (96), so the DPI-scaled frame metrics
     * are identical to the non-scaled ones; defer to AdjustWindowRectEx. */
    UNREFERENCED_PARAMETER(dpi);
    return AdjustWindowRectEx(lpRect, dwStyle, bMenu, dwExStyle);
}

/*
 * @implemented
 */
int
WINAPI
GetSystemMetricsForDpi(
    _In_ int nIndex,
    _In_ UINT dpi)
{
    /* At the system DPI (96) the per-DPI metric equals the plain one. */
    UNREFERENCED_PARAMETER(dpi);
    return GetSystemMetrics(nIndex);
}
