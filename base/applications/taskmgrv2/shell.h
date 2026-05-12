#pragma once

/* -----------------------------------------------------------------------
 * shell.h — Outer top-level window, sidebar nav rail, content host,
 *            hamburger toggle, page registry, refresh timer, message pump.
 * ----------------------------------------------------------------------- */

HWND           Shell_GetMainWnd(void);
HWND           Shell_GetContentHost(void);
void           Shell_NavigateTo(MODERN_PAGE_ID id);
MODERN_PAGE_ID Shell_GetCurrentPage(void);
void           Shell_RegisterPage(MODERN_PAGE_ID id, const PAGE_VTBL *vtbl);
void           Shell_RequestRefresh(void);       /* posts WM_TIMER */
DWORD          Shell_GetRefreshIntervalMs(void); /* default 1000 */
void           Shell_SetRefreshIntervalMs(DWORD ms); /* clamped 250..5000 */
BOOL           Shell_IsDarkMode(void);
void           Shell_SetDarkMode(BOOL bDark);
BOOL           Shell_IsSidebarExpanded(void);
void           Shell_SetSidebarExpanded(BOOL bExpanded);

/* Called once from TmgrV2_Run. Owns the main window class registration,
 * page registration, message pump, and shutdown. Returns the wParam of
 * the message loop. */
int            Shell_Main(HINSTANCE hInst, LPWSTR lpCmdLine, int nCmdShow);
