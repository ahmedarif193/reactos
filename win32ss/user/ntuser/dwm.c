/*
 * PROJECT:     ReactOS Win32k subsystem
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DWM (Desktop Window Manager) support syscalls
 * FILE:        win32ss/user/ntuser/dwm.c
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * NtUser syscalls for DWM composition support. These are the kernel-side
 * handlers for the undocumented USER32 DWM APIs that dwm.exe imports.
 *
 * Vista+ syscalls implemented here:
 *   NtUserDwmStartRedirection   - Enable window surface redirection
 *   NtUserDwmStopRedirection    - Disable window surface redirection
 *   NtUserDwmGetDxRgn           - Get DirectX dirty region for a window
 *   NtUserDwmHintDxUpdate       - Hint that a DX surface has been updated
 *   NtUserRegisterSessionPort   - Register the DWM's LPC API port
 *   NtUserUnregisterSessionPort - Unregister the DWM's LPC API port
 *   NtUserCheckDesktopByThreadId - Verify desktop association for a thread
 *   NtUserGhostWindowFromHungWindow - Get ghost window for a hung window
 *   NtUserHungWindowFromGhostWindow - Get hung window from its ghost
 *   NtUserRegisterErrorReportingDialog - Register error dialog window
 */

#include <win32k.h>

DBG_DEFAULT_CHANNEL(UserMisc);

/* Forward declarations for ghost.c internal functions */
HWND FASTCALL UserGhostWindowFromHungWindow(HWND hwndHung);
HWND FASTCALL UserHungWindowFromGhostWindow(HWND hwndGhost);

/* GLOBALS *******************************************************************/

/*
 * DWM session state. In Windows, these live in the PROCESSINFO of the DWM
 * process and the DESKTOP structure. For Phase 1, we use simple globals.
 */
static HANDLE  gpDwmSessionPort = NULL;    /* DWM's registered LPC port */
static BOOLEAN gbDwmRedirectionActive = FALSE; /* Whether redirection is on */

/* FUNCTIONS *****************************************************************/

/*
 * NtUserDwmStartRedirection
 *
 * Called by dwm.exe (via user32!DwmStartRedirection) to enable window
 * surface redirection. When active, top-level windows render to off-screen
 * surfaces that the DWM composites.
 *
 * pRedirectionInfo: User-mode buffer that receives redirection state.
 *                   Phase 1 zeroes it out; Phase 2 fills it with real data.
 */
BOOL
APIENTRY
NtUserDwmStartRedirection(
    _Out_ PVOID pRedirectionInfo)
{
    TRACE("NtUserDwmStartRedirection(%p)\n", pRedirectionInfo);

    UserEnterExclusive();

    if (!pRedirectionInfo)
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        goto Exit;
    }

    _SEH2_TRY
    {
        /* Probe the user-mode buffer. The redirection info structure
         * is at least pointer-sized. For Phase 1, just zero 8 bytes. */
        ProbeForWrite(pRedirectionInfo, sizeof(PVOID), 1);
        RtlZeroMemory(pRedirectionInfo, sizeof(PVOID));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        _SEH2_YIELD(goto Exit);
    }
    _SEH2_END;

    EngSetLastError(ERROR_CALL_NOT_IMPLEMENTED);

Exit:
    UserLeave();
    return FALSE;
}

/*
 * NtUserDwmStopRedirection
 *
 * Called by dwm.exe during shutdown to disable surface redirection.
 * Windows revert to direct screen rendering.
 */
BOOL
APIENTRY
NtUserDwmStopRedirection(VOID)
{
    TRACE("NtUserDwmStopRedirection()\n");

    UserEnterExclusive();

    gbDwmRedirectionActive = FALSE;

    UserLeave();
    return TRUE;
}

/*
 * NtUserDwmGetDxRgn
 *
 * Retrieves the DirectX dirty region for a window. The DWM uses this to
 * determine which parts of a redirected window's DX surface have changed
 * since the last composition pass.
 *
 * hwnd:    Target window
 * hrgn:    Region handle to receive the dirty region
 * dwFlags: Query flags
 */
BOOL
APIENTRY
NtUserDwmGetDxRgn(
    _In_ HWND hwnd,
    _In_ HANDLE hrgn,
    _In_ DWORD dwFlags)
{
    PWND pWnd;

    TRACE("NtUserDwmGetDxRgn(%p, %p, 0x%lx)\n", hwnd, hrgn, dwFlags);

    UserEnterExclusive();

    pWnd = UserGetWindowObject(hwnd);
    if (!pWnd || !hrgn || dwFlags)
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        UserLeave();
        return FALSE;
    }

    EngSetLastError(ERROR_CALL_NOT_IMPLEMENTED);

    UserLeave();
    return FALSE;
}

/*
 * NtUserDwmHintDxUpdate
 *
 * Hints to win32k that a DirectX surface associated with a window has
 * been updated. The DWM calls this after a D3D present to let the
 * kernel know it should schedule a composition pass.
 *
 * hwnd:    Window whose DX surface was updated
 * dwFlags: Hint flags
 */
BOOL
APIENTRY
NtUserDwmHintDxUpdate(
    _In_ HWND hwnd,
    _In_ DWORD dwFlags)
{
    PWND pWnd;

    TRACE("NtUserDwmHintDxUpdate(%p, 0x%lx)\n", hwnd, dwFlags);

    UserEnterExclusive();

    pWnd = UserGetWindowObject(hwnd);
    if (!pWnd || dwFlags)
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        UserLeave();
        return FALSE;
    }

    EngSetLastError(gbDwmRedirectionActive ? ERROR_CALL_NOT_IMPLEMENTED :
                                             ERROR_INVALID_FUNCTION);
    UserLeave();
    return FALSE;
}

/*
 * NtUserRegisterSessionPort
 *
 * Registers the DWM's LPC API port with win32k so that the kernel
 * can send DWM notifications (window created, destroyed, moved, etc.).
 *
 * The port name follows: \BaseNamedObjects\Dwm-XXXX-ApiPort-XXXX
 *
 * hPort: Handle to the DWM's LPC port
 */
BOOL
APIENTRY
NtUserRegisterSessionPort(
    _In_ HANDLE hPort)
{
    TRACE("NtUserRegisterSessionPort(%p)\n", hPort);

    UserEnterExclusive();

    if (!hPort)
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        UserLeave();
        return FALSE;
    }

    /*
     * Do not cache an unreferenced user handle here. Until this path stores
     * a referenced kernel object with owner/session validation, report the
     * unsupported registration explicitly.
     */
    EngSetLastError(ERROR_CALL_NOT_IMPLEMENTED);

    UserLeave();
    return FALSE;
}

/*
 * NtUserUnregisterSessionPort
 *
 * Unregisters the DWM's LPC API port from win32k.
 * Called during DWM shutdown.
 */
BOOL
APIENTRY
NtUserUnregisterSessionPort(VOID)
{
    TRACE("NtUserUnregisterSessionPort()\n");

    UserEnterExclusive();

    gpDwmSessionPort = NULL;
    gbDwmRedirectionActive = FALSE;

    UserLeave();
    return TRUE;
}

/*
 * NtUserCheckDesktopByThreadId
 *
 * Verifies that a thread is associated with the expected desktop.
 * DWM uses this to ensure it is operating on the correct desktop
 * (important for multi-desktop and Secure Attention Sequence scenarios).
 *
 * dwThreadId: Thread ID to check
 */
BOOL
APIENTRY
NtUserCheckDesktopByThreadId(
    _In_ DWORD dwThreadId)
{
    PTHREADINFO pti;
    BOOL Ret = FALSE;

    TRACE("NtUserCheckDesktopByThreadId(%lu)\n", dwThreadId);

    UserEnterExclusive();

    if (!dwThreadId)
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        goto Exit;
    }

    pti = IntTID2PTI(ULongToHandle(dwThreadId));
    if (pti && pti->rpdesk)
    {
        Ret = TRUE;
    }
    else
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
    }

Exit:
    UserLeave();
    return Ret;
}

/*
 * NtUserGhostWindowFromHungWindow
 *
 * Given a hung (not-responding) window handle, returns the handle of the
 * ghost replacement window. Delegates to the existing ghost.c internals.
 */
HWND
APIENTRY
NtUserGhostWindowFromHungWindow(
    _In_ HWND hwndHung)
{
    HWND Ret;

    TRACE("NtUserGhostWindowFromHungWindow(%p)\n", hwndHung);

    UserEnterExclusive();

    Ret = UserGhostWindowFromHungWindow(hwndHung);

    UserLeave();
    return Ret;
}

/*
 * NtUserHungWindowFromGhostWindow
 *
 * Given a ghost window handle, returns the original hung window
 * that it is replacing. Delegates to the existing ghost.c internals.
 */
HWND
APIENTRY
NtUserHungWindowFromGhostWindow(
    _In_ HWND hwndGhost)
{
    HWND Ret;

    TRACE("NtUserHungWindowFromGhostWindow(%p)\n", hwndGhost);

    UserEnterExclusive();

    Ret = UserHungWindowFromGhostWindow(hwndGhost);

    UserLeave();
    return Ret;
}

/*
 * NtUserRegisterErrorReportingDialog
 *
 * Registers a window as an error reporting dialog. This allows DWM
 * to give the dialog special Z-order treatment (always on top of the
 * ghost window for the crashed application).
 *
 * hwndDialog: Dialog window handle
 * dwFlags:    Registration flags
 */
BOOL
APIENTRY
NtUserRegisterErrorReportingDialog(
    _In_ HWND hwndDialog,
    _In_ DWORD dwFlags)
{
    PWND pWnd;
    BOOL Ret = FALSE;

    TRACE("NtUserRegisterErrorReportingDialog(%p, 0x%lx)\n", hwndDialog, dwFlags);

    UserEnterExclusive();

    pWnd = UserGetWindowObject(hwndDialog);
    if (!pWnd)
    {
        goto Exit;
    }

    EngSetLastError(ERROR_CALL_NOT_IMPLEMENTED);

Exit:
    UserLeave();
    return Ret;
}
