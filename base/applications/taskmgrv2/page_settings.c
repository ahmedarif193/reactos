/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Settings page — inline sidebar page with 5 control groups
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9.1, constraint #12.
 * Layout: vertical stack, 24 px outer padding, 16 px gap between groups.
 * Settings are saved immediately on change via Shell_Set* + Reg_WriteDword.
 * No Apply/OK button.
 */

#include "precomp.h"

#define SETTINGS_CLASS  L"TaskMgrV2.Page.Settings"

/* Registry subkey for shell settings */
#define REG_SHELL_KEY   L"Software\\ReactOS\\TaskMgrV2\\Shell"

/* Control layout constants (unscaled px at 96 DPI) */
#define PAD             24
#define GROUP_GAP       16
#define LABEL_H         20
#define RADIO_H         22
#define CHECK_H         22
#define COMBO_H         24
#define SECTION_LABEL_H 18

/* Page names for default-page combo (order matches MODERN_PAGE_ID enum) */
static const WCHAR * const s_pageNames[MPAGE__COUNT] =
{
    L"Processes",
    L"Performance",
    L"App history",
    L"Startup",
    L"Users",
    L"Details",
    L"Services",
    L"Settings"
};

/* -------------------------------------------------------------------------
 * Per-page state
 * ----------------------------------------------------------------------- */
typedef struct _SETTINGS_PAGE
{
    /* Theme section */
    HWND hGrpTheme;
    HWND hRadThemeLight;
    HWND hRadThemeDark;
    HWND hRadThemeSystem;

    /* Refresh interval section */
    HWND hGrpRefresh;
    HWND hRadRefreshLow;
    HWND hRadRefreshMed;
    HWND hRadRefreshHigh;

    /* Default page section */
    HWND hGrpDefPage;
    HWND hCmbDefPage;

    /* Checkboxes */
    HWND hChkUsersFullName;
    HWND hChkRealtimeWarn;

    /* About label */
    HWND hLblAbout;

    /* current client size */
    int cx;
    int cy;
} SETTINGS_PAGE;

/* Helper: create a static group label */
static HWND
CreateSectionLabel(HWND hParent, HINSTANCE hInst, LPCWSTR text, int id)
{
    HWND h = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hParent, (HMENU)(INT_PTR)id, hInst, NULL);
    if (h)
        SendMessageW(h, WM_SETFONT, (WPARAM)Theme_FontUI(), FALSE);
    return h;
}

/* Helper: create a radio button */
static HWND
CreateRadio(HWND hParent, HINSTANCE hInst, LPCWSTR text, int id, BOOL bFirst)
{
    DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON |
                  (bFirst ? WS_GROUP : 0);
    HWND h = CreateWindowExW(0, L"BUTTON", text, style,
        0, 0, 0, 0, hParent, (HMENU)(INT_PTR)id, hInst, NULL);
    if (h)
        SendMessageW(h, WM_SETFONT, (WPARAM)Theme_FontUI(), FALSE);
    return h;
}

/* Helper: create a checkbox */
static HWND
CreateCheck(HWND hParent, HINSTANCE hInst, LPCWSTR text, int id)
{
    HWND h = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hParent, (HMENU)(INT_PTR)id, hInst, NULL);
    if (h)
        SendMessageW(h, WM_SETFONT, (WPARAM)Theme_FontUI(), FALSE);
    return h;
}

/* -------------------------------------------------------------------------
 * Layout: position all controls based on current client area.
 * Called from WM_SIZE.
 * ----------------------------------------------------------------------- */
static void
Settings_Layout(HWND hWnd, SETTINGS_PAGE *pState, int cx, int cy)
{
    int x = Theme_DpiScale(PAD);
    int y = Theme_DpiScale(PAD);
    int w = cx - 2 * Theme_DpiScale(PAD);
    int gap = Theme_DpiScale(GROUP_GAP);
    int lblH = Theme_DpiScale(SECTION_LABEL_H);
    int radioH = Theme_DpiScale(RADIO_H);
    int checkH = Theme_DpiScale(CHECK_H);
    int comboH = Theme_DpiScale(COMBO_H);

    (void)cy;

    /* --- Theme group --- */
    SetWindowPos(pState->hGrpTheme, NULL, x, y, w, lblH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += lblH + Theme_DpiScale(4);

    SetWindowPos(pState->hRadThemeLight, NULL, x + Theme_DpiScale(8), y, w, radioH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += radioH;

    SetWindowPos(pState->hRadThemeDark, NULL, x + Theme_DpiScale(8), y, w, radioH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += radioH;

    SetWindowPos(pState->hRadThemeSystem, NULL, x + Theme_DpiScale(8), y, w, radioH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += radioH + gap;

    /* --- Refresh interval group --- */
    SetWindowPos(pState->hGrpRefresh, NULL, x, y, w, lblH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += lblH + Theme_DpiScale(4);

    SetWindowPos(pState->hRadRefreshLow, NULL, x + Theme_DpiScale(8), y, w, radioH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += radioH;

    SetWindowPos(pState->hRadRefreshMed, NULL, x + Theme_DpiScale(8), y, w, radioH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += radioH;

    SetWindowPos(pState->hRadRefreshHigh, NULL, x + Theme_DpiScale(8), y, w, radioH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += radioH + gap;

    /* --- Default startup page group --- */
    SetWindowPos(pState->hGrpDefPage, NULL, x, y, w, lblH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += lblH + Theme_DpiScale(4);

    SetWindowPos(pState->hCmbDefPage, NULL, x + Theme_DpiScale(8), y,
        min(w, Theme_DpiScale(200)), comboH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += comboH + gap;

    /* --- Checkboxes --- */
    SetWindowPos(pState->hChkUsersFullName, NULL, x, y, w, checkH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += checkH + Theme_DpiScale(4);

    SetWindowPos(pState->hChkRealtimeWarn, NULL, x, y, w, checkH,
        SWP_NOZORDER | SWP_NOACTIVATE);
    y += checkH + gap * 2;

    /* --- About --- */
    SetWindowPos(pState->hLblAbout, NULL, x, y, w, lblH,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

/* -------------------------------------------------------------------------
 * Load settings from registry and update control states
 * ----------------------------------------------------------------------- */
static void
Settings_LoadControls(SETTINGS_PAGE *pState)
{
    DWORD val;

    /* Theme */
    val = 0;
    Reg_ReadDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"DarkMode", &val);
    SendMessageW(pState->hRadThemeLight,  BM_SETCHECK,
        (val == 0) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(pState->hRadThemeDark,   BM_SETCHECK,
        (val == 1) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(pState->hRadThemeSystem, BM_SETCHECK,
        (val == 2) ? BST_CHECKED : BST_UNCHECKED, 0);

    /* Refresh interval */
    val = 1000;
    Reg_ReadDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"RefreshIntervalMs", &val);
    SendMessageW(pState->hRadRefreshLow,  BM_SETCHECK,
        (val >= 4000) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(pState->hRadRefreshMed,  BM_SETCHECK,
        (val > 250 && val < 4000) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(pState->hRadRefreshHigh, BM_SETCHECK,
        (val <= 250) ? BST_CHECKED : BST_UNCHECKED, 0);

    /* Default page combo: index = enum value */
    val = 1; /* MPAGE_PERFORMANCE */
    Reg_ReadDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"DefaultPage", &val);
    if (val < MPAGE__COUNT)
        SendMessageW(pState->hCmbDefPage, CB_SETCURSEL, (WPARAM)val, 0);

    /* Checkboxes */
    val = 0;
    Reg_ReadDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"UsersShowFullName", &val);
    SendMessageW(pState->hChkUsersFullName, BM_SETCHECK,
        val ? BST_CHECKED : BST_UNCHECKED, 0);

    val = 1;
    Reg_ReadDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"RealtimeWarn", &val);
    SendMessageW(pState->hChkRealtimeWarn, BM_SETCHECK,
        val ? BST_CHECKED : BST_UNCHECKED, 0);
}

/* -------------------------------------------------------------------------
 * Handle control notifications (BN_CLICKED, CBN_SELCHANGE)
 * ----------------------------------------------------------------------- */
static void
Settings_OnCommand(HWND hWnd, SETTINGS_PAGE *pState, int id, int code)
{
    DWORD val;

    switch (id)
    {
        case IDC_SETTINGS_THEME_LIGHT:
            if (code == BN_CLICKED)
            {
                Shell_SetDarkMode(FALSE);
                Reg_WriteDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"DarkMode", 0);
            }
            break;

        case IDC_SETTINGS_THEME_DARK:
            if (code == BN_CLICKED)
            {
                Shell_SetDarkMode(TRUE);
                Reg_WriteDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"DarkMode", 1);
            }
            break;

        case IDC_SETTINGS_THEME_SYSTEM:
            if (code == BN_CLICKED)
            {
                /* v1: "Use system setting" maps to light */
                Shell_SetDarkMode(FALSE);
                Reg_WriteDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"DarkMode", 2);
            }
            break;

        case IDC_SETTINGS_REFRESH_LOW:
            if (code == BN_CLICKED)
            {
                Shell_SetRefreshIntervalMs(4000);
                Reg_WriteDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"RefreshIntervalMs", 4000);
            }
            break;

        case IDC_SETTINGS_REFRESH_MED:
            if (code == BN_CLICKED)
            {
                Shell_SetRefreshIntervalMs(1000);
                Reg_WriteDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"RefreshIntervalMs", 1000);
            }
            break;

        case IDC_SETTINGS_REFRESH_HIGH:
            if (code == BN_CLICKED)
            {
                Shell_SetRefreshIntervalMs(250);
                Reg_WriteDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"RefreshIntervalMs", 250);
            }
            break;

        case IDC_SETTINGS_DEFAULTPAGE:
            if (code == CBN_SELCHANGE)
            {
                LRESULT sel = SendMessageW(pState->hCmbDefPage, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR)
                {
                    val = (DWORD)sel;
                    Reg_WriteDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"DefaultPage", val);
                }
            }
            break;

        case IDC_SETTINGS_USERS_FULLNAME:
            if (code == BN_CLICKED)
            {
                val = (SendMessageW(pState->hChkUsersFullName,
                    BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
                Reg_WriteDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"UsersShowFullName", val);
            }
            break;

        case IDC_SETTINGS_REALTIME_WARN:
            if (code == BN_CLICKED)
            {
                val = (SendMessageW(pState->hChkRealtimeWarn,
                    BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
                Reg_WriteDword(HKEY_CURRENT_USER, REG_SHELL_KEY, L"RealtimeWarn", val);
            }
            break;
    }
    (void)hWnd;
}

/* -------------------------------------------------------------------------
 * Update all fonts (called from Theme callback)
 * ----------------------------------------------------------------------- */
static void
Settings_UpdateFonts(SETTINGS_PAGE *pState)
{
    HFONT hFont = Theme_FontUI();

    SendMessageW(pState->hGrpTheme,       WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(pState->hRadThemeLight,  WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(pState->hRadThemeDark,   WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(pState->hRadThemeSystem, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(pState->hGrpRefresh,     WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(pState->hRadRefreshLow,  WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(pState->hRadRefreshMed,  WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(pState->hRadRefreshHigh, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(pState->hGrpDefPage,     WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(pState->hCmbDefPage,     WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(pState->hChkUsersFullName, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(pState->hChkRealtimeWarn,  WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(pState->hLblAbout,       WM_SETFONT, (WPARAM)hFont, TRUE);
}

/* -------------------------------------------------------------------------
 * Window procedure
 * ----------------------------------------------------------------------- */
static LRESULT CALLBACK
SettingsWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SETTINGS_PAGE *pState = (SETTINGS_PAGE *)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    int i;

    switch (uMsg)
    {
        case WM_CREATE:
        {
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE);
            pState = (SETTINGS_PAGE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, sizeof(SETTINGS_PAGE));
            if (!pState) return -1;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);

            /* --- Theme section --- */
            pState->hGrpTheme = CreateSectionLabel(hWnd, hInst, L"Theme", 1001);
            pState->hRadThemeLight  = CreateRadio(hWnd, hInst, L"Light",
                IDC_SETTINGS_THEME_LIGHT, TRUE);
            pState->hRadThemeDark   = CreateRadio(hWnd, hInst, L"Dark",
                IDC_SETTINGS_THEME_DARK, FALSE);
            pState->hRadThemeSystem = CreateRadio(hWnd, hInst, L"Use system setting",
                IDC_SETTINGS_THEME_SYSTEM, FALSE);

            /* --- Refresh interval section --- */
            pState->hGrpRefresh = CreateSectionLabel(hWnd, hInst, L"Refresh interval", 1002);
            pState->hRadRefreshLow  = CreateRadio(hWnd, hInst, L"Low (4 seconds)",
                IDC_SETTINGS_REFRESH_LOW, TRUE);
            pState->hRadRefreshMed  = CreateRadio(hWnd, hInst, L"Normal (1 second)",
                IDC_SETTINGS_REFRESH_MED, FALSE);
            pState->hRadRefreshHigh = CreateRadio(hWnd, hInst, L"High (0.25 seconds)",
                IDC_SETTINGS_REFRESH_HIGH, FALSE);

            /* --- Default startup page section --- */
            pState->hGrpDefPage = CreateSectionLabel(hWnd, hInst,
                L"Default startup page", 1003);
            pState->hCmbDefPage = CreateWindowExW(0, L"COMBOBOX", NULL,
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
                0, 0, 0, 0, hWnd,
                (HMENU)(INT_PTR)IDC_SETTINGS_DEFAULTPAGE, hInst, NULL);
            if (pState->hCmbDefPage)
            {
                SendMessageW(pState->hCmbDefPage, WM_SETFONT,
                    (WPARAM)Theme_FontUI(), FALSE);
                for (i = 0; i < MPAGE__COUNT; i++)
                {
                    SendMessageW(pState->hCmbDefPage, CB_ADDSTRING,
                        0, (LPARAM)s_pageNames[i]);
                }
            }

            /* --- Checkboxes --- */
            pState->hChkUsersFullName = CreateCheck(hWnd, hInst,
                L"Show full account name on the Users page",
                IDC_SETTINGS_USERS_FULLNAME);
            pState->hChkRealtimeWarn = CreateCheck(hWnd, hInst,
                L"Warn before changing a process to Realtime priority",
                IDC_SETTINGS_REALTIME_WARN);

            /* --- About --- */
            pState->hLblAbout = CreateWindowExW(0, L"STATIC",
                L"ReactOS Task Manager v2 \x2014 1.0.0",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)1004, hInst, NULL);
            if (pState->hLblAbout)
                SendMessageW(pState->hLblAbout, WM_SETFONT,
                    (WPARAM)Theme_FontUI(), FALSE);

            /* Load current values */
            Settings_LoadControls(pState);
            return 0;
        }

        case WM_DESTROY:
            if (pState)
            {
                HeapFree(GetProcessHeap(), 0, pState);
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
            }
            return 0;

        case WM_SIZE:
            if (pState)
            {
                pState->cx = LOWORD(lParam);
                pState->cy = HIWORD(lParam);
                Settings_Layout(hWnd, pState, pState->cx, pState->cy);
            }
            return 0;

        case WM_COMMAND:
            if (pState)
            {
                Settings_OnCommand(hWnd, pState,
                    LOWORD(wParam), HIWORD(wParam));
            }
            return 0;

        case WM_ERASEBKGND:
        {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, Theme_BrushBg());
            return 1;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, Theme_BrushBg());
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        {
            HDC hdc = (HDC)wParam;
            const MODERN_THEME *t = Theme_Current();
            SetTextColor(hdc, t->clrText);
            SetBkColor(hdc, t->clrBg);
            return (LRESULT)Theme_BrushBg();
        }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

/* -------------------------------------------------------------------------
 * Page vtbl implementation
 * ----------------------------------------------------------------------- */
HWND
PageSettings_Create(HWND hHost, HINSTANCE hInst)
{
    WNDCLASSEXW wc = {0};

    if (!GetClassInfoExW(hInst, SETTINGS_CLASS, &wc))
    {
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = SettingsWndProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = NULL;
        wc.lpszClassName = SETTINGS_CLASS;
        if (!RegisterClassExW(&wc))
            return NULL;
    }

    return CreateWindowExW(0, SETTINGS_CLASS, NULL,
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0,
        hHost, NULL, hInst, NULL);
}

void
PageSettings_Tick(HWND hPage)
{
    /* Settings page is static — no tick needed. */
    (void)hPage;
}

void
PageSettings_Resize(HWND hPage, int cx, int cy)
{
    SetWindowPos(hPage, NULL, 0, 0, cx, cy,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}

void
PageSettings_Destroy(HWND hPage)
{
    DestroyWindow(hPage);
}

void
PageSettings_Theme(HWND hPage)
{
    SETTINGS_PAGE *pState =
        (SETTINGS_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (pState)
        Settings_UpdateFonts(pState);
    InvalidateRect(hPage, NULL, TRUE);
}

/* -------------------------------------------------------------------------
 * Extra exports — called by shell when setting changes from outside
 * ----------------------------------------------------------------------- */
void
PageSettings_OnThemeChanged(HWND hPage, BOOL bDark)
{
    SETTINGS_PAGE *pState =
        (SETTINGS_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (!pState) return;
    SendMessageW(pState->hRadThemeLight,  BM_SETCHECK,
        (!bDark) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(pState->hRadThemeDark,   BM_SETCHECK,
        bDark    ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(pState->hRadThemeSystem, BM_SETCHECK, BST_UNCHECKED, 0);
}

void
PageSettings_OnRefreshChanged(HWND hPage, DWORD ms)
{
    SETTINGS_PAGE *pState =
        (SETTINGS_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (!pState) return;
    SendMessageW(pState->hRadRefreshLow,  BM_SETCHECK,
        (ms >= 4000) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(pState->hRadRefreshMed,  BM_SETCHECK,
        (ms > 250 && ms < 4000) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(pState->hRadRefreshHigh, BM_SETCHECK,
        (ms <= 250) ? BST_CHECKED : BST_UNCHECKED, 0);
}

void
PageSettings_OnDefaultPageChanged(HWND hPage, MODERN_PAGE_ID id)
{
    SETTINGS_PAGE *pState =
        (SETTINGS_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (!pState) return;
    if (id < MPAGE__COUNT)
        SendMessageW(pState->hCmbDefPage, CB_SETCURSEL, (WPARAM)id, 0);
}

void
PageSettings_OnUsersFullNameChanged(HWND hPage, BOOL bShow)
{
    SETTINGS_PAGE *pState =
        (SETTINGS_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (!pState) return;
    SendMessageW(pState->hChkUsersFullName, BM_SETCHECK,
        bShow ? BST_CHECKED : BST_UNCHECKED, 0);
}

void
PageSettings_OnRealtimeWarnChanged(HWND hPage, BOOL bWarn)
{
    SETTINGS_PAGE *pState =
        (SETTINGS_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (!pState) return;
    SendMessageW(pState->hChkRealtimeWarn, BM_SETCHECK,
        bWarn ? BST_CHECKED : BST_UNCHECKED, 0);
}
