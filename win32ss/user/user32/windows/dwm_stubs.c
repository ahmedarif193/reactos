/*
 * PROJECT:     ReactOS user32.dll
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Undocumented USER32 APIs required by DWM (Desktop Window Manager)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * These are internal/undocumented USER32 APIs that Windows Vista+ DWM
 * imports.  They provide the bridge between the DWM compositor process
 * and the win32k window manager kernel.
 *
 * APIs implemented here:
 *   DwmStartRedirection    - Enable window surface redirection for composition
 *   DwmStopRedirection     - Disable window surface redirection
 *   RegisterSessionPort    - Register the DWM's LPC API port with win32k
 *   UnregisterSessionPort  - Unregister the DWM's LPC API port
 *   CheckDesktopByThreadId - Verify desktop association for a thread
 *   IsThreadDesktopComposited - Check if current thread's desktop is composited
 *   GhostWindowFromHungWindow - Get ghost window for a hung window
 *   HungWindowFromGhostWindow - Get hung window from its ghost
 *   RegisterGhostWindow    - Register a window as a ghost replacement
 *   RegisterFrostWindow    - Register a window as a "frost" overlay
 *   RegisterErrorReportingDialog - Register error reporting dialog window
 *   GetWindowCompositionAttribute - Get DWM composition attribute for a window
 *   GetWindowCompositionInfo - Get extended composition info for a window
 */

#include <user32.h>

WINE_DEFAULT_DEBUG_CHANNEL(dwm);

/*
 * DwmStartRedirection
 *
 * Called by dwm.exe to enable window surface redirection.  When redirection
 * is active, all top-level windows render to off-screen surfaces instead of
 * directly to the screen framebuffer.  The DWM compositor then composites
 * these surfaces.
 *
 * Calls NtUserDwmStartRedirection in win32k.
 */
BOOL
WINAPI
DwmStartRedirection(
    _Out_ PVOID pRedirectionInfo)
{
    TRACE("DwmStartRedirection(%p)\n", pRedirectionInfo);
    return NtUserDwmStartRedirection(pRedirectionInfo);
}

/*
 * DwmStopRedirection
 *
 * Called by dwm.exe during shutdown to disable surface redirection.
 * Windows revert to direct screen rendering.
 */
BOOL
WINAPI
DwmStopRedirection(VOID)
{
    TRACE("DwmStopRedirection()\n");
    return NtUserDwmStopRedirection();
}

/*
 * RegisterSessionPort
 *
 * Registers the DWM's LPC API port with win32k so that the kernel
 * knows where to send DWM notifications (window created, destroyed,
 * moved, resized, etc.).
 *
 * The port name follows the pattern:
 *   \BaseNamedObjects\Dwm-XXXX-ApiPort-XXXX
 */
BOOL
WINAPI
RegisterSessionPort(
    _In_ HANDLE hPort)
{
    TRACE("RegisterSessionPort(%p)\n", hPort);
    return NtUserRegisterSessionPort(hPort);
}

/*
 * UnregisterSessionPort
 *
 * Unregisters the DWM's LPC API port from win32k.
 * Called during DWM shutdown.
 */
BOOL
WINAPI
UnregisterSessionPort(VOID)
{
    TRACE("UnregisterSessionPort()\n");
    return NtUserUnregisterSessionPort();
}

/*
 * CheckDesktopByThreadId
 *
 * Verifies that a thread is associated with the expected desktop.
 * DWM uses this to ensure it is operating on the correct desktop.
 */
BOOL
WINAPI
CheckDesktopByThreadId(
    _In_ DWORD dwThreadId)
{
    TRACE("CheckDesktopByThreadId(%lu)\n", dwThreadId);
    return NtUserCheckDesktopByThreadId(dwThreadId);
}

/*
 * IsThreadDesktopComposited
 *
 * Returns whether the current thread's desktop has DWM composition
 * enabled.  dwmapi!DwmIsCompositionEnabled calls this internally.
 *
 * This is not a separate syscall in Vista; it queries state from the
 * shared DESKTOPINFO visible in user-mode.
 */
BOOL
WINAPI
IsThreadDesktopComposited(VOID)
{
    TRACE("IsThreadDesktopComposited()\n");
    /* Phase 1: Composition is not yet fully active.
     * Phase 2: Check DESKTOPINFO flags in the shared section. */
    return FALSE;
}

/*
 * GhostWindowFromHungWindow
 *
 * Given a hung (not-responding) window handle, returns the handle
 * of the ghost replacement window that DWM created for it.
 * Returns NULL if no ghost exists.
 */
HWND
WINAPI
GhostWindowFromHungWindow(
    _In_ HWND hwndHung)
{
    TRACE("GhostWindowFromHungWindow(%p)\n", hwndHung);

    if (!hwndHung)
        return NULL;

    return NtUserGhostWindowFromHungWindow(hwndHung);
}

/*
 * HungWindowFromGhostWindow
 *
 * Given a ghost window handle, returns the original hung window
 * that it is replacing.
 */
HWND
WINAPI
HungWindowFromGhostWindow(
    _In_ HWND hwndGhost)
{
    TRACE("HungWindowFromGhostWindow(%p)\n", hwndGhost);

    if (!hwndGhost)
        return NULL;

    return NtUserHungWindowFromGhostWindow(hwndGhost);
}

/*
 * RegisterGhostWindow
 *
 * Registers a window as a ghost replacement for a hung window.
 * DWM creates ghost windows to show a visual representation of
 * the hung application's last painted state.
 *
 * Not a separate syscall in Vista; handled in user-mode.
 */
BOOL
WINAPI
RegisterGhostWindow(
    _In_ HWND hwndGhost)
{
    TRACE("RegisterGhostWindow(%p)\n", hwndGhost);

    if (!hwndGhost || !IsWindow(hwndGhost))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * RegisterFrostWindow
 *
 * Registers a window as a "frost" overlay.  Frost windows are
 * semi-transparent overlays that DWM uses during certain transitions
 * (e.g., the fade effect when a window stops responding).
 *
 * Not a separate syscall in Vista; handled in user-mode.
 */
BOOL
WINAPI
RegisterFrostWindow(
    _In_ HWND hwndFrost)
{
    TRACE("RegisterFrostWindow(%p)\n", hwndFrost);

    if (!hwndFrost || !IsWindow(hwndFrost))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * RegisterErrorReportingDialog
 *
 * Registers a window as an error reporting dialog.  This allows DWM
 * to give the dialog special Z-order treatment (always on top of the
 * ghost window for the crashed application).
 */
BOOL
WINAPI
RegisterErrorReportingDialog(
    _In_ HWND hwndDialog,
    _In_ DWORD dwFlags)
{
    TRACE("RegisterErrorReportingDialog(%p, %lu)\n", hwndDialog, dwFlags);
    return NtUserRegisterErrorReportingDialog(hwndDialog, dwFlags);
}

/*
 * Window Composition Attribute data structure
 * Used by Get/SetWindowCompositionAttribute.
 */
typedef struct _WINDOWCOMPOSITIONATTRIBDATA
{
    DWORD   Attrib;
    PVOID   pvData;
    SIZE_T  cbData;
} WINDOWCOMPOSITIONATTRIBDATA, *PWINDOWCOMPOSITIONATTRIBDATA;

/*
 * GetWindowCompositionAttribute
 *
 * Retrieves a DWM composition attribute for a window.  This is the
 * read counterpart to SetWindowCompositionAttribute.
 *
 * Attributes include:
 *   WCA_NCRENDERING_ENABLED (1) - Non-client rendering state
 *   WCA_ACCENT_POLICY (19) - Accent/blur policy
 *   WCA_CLOAKED (14) - Window cloaked state
 */
BOOL
WINAPI
GetWindowCompositionAttribute(
    _In_ HWND hwnd,
    _Inout_ PWINDOWCOMPOSITIONATTRIBDATA pAttrData)
{
    TRACE("GetWindowCompositionAttribute(%p, %p)\n", hwnd, pAttrData);

    if (!hwnd || !pAttrData)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!IsWindow(hwnd))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }

    if (!pAttrData->pvData)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    switch (pAttrData->Attrib)
    {
    case 1:  /* WCA_NCRENDERING_ENABLED */
        if (pAttrData->cbData < sizeof(BOOL))
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        *(BOOL *)pAttrData->pvData = FALSE;
        return TRUE;

    case 5:  /* WCA_CAPTION_BUTTON_BOUNDS */
        if (pAttrData->cbData < sizeof(RECT))
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        SetRectEmpty((RECT *)pAttrData->pvData);
        return TRUE;

    case 14: /* WCA_CLOAKED */
        if (pAttrData->cbData < sizeof(DWORD))
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        *(DWORD *)pAttrData->pvData = 0;
        return TRUE;

    default:
        break;
    }

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * GetWindowCompositionInfo
 *
 * Retrieves extended composition information for a window.
 * Used by dwmredir.dll to query per-window composition state
 * (redirection surfaces, DX shared handles, etc.).
 */
BOOL
WINAPI
GetWindowCompositionInfo(
    _In_ HWND hwnd,
    _Out_ PVOID pCompInfo)
{
    TRACE("GetWindowCompositionInfo(%p, %p)\n", hwnd, pCompInfo);

    if (!hwnd || !pCompInfo)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!IsWindow(hwnd))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
