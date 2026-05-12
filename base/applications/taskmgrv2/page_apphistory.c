/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     App history page — placeholder, no data layer
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9, constraint #11, decision #5.
 * Body: one centered STATIC label in Theme_FontUI() / clrTextDim.
 * Tick callback: NULL (passed as NULL to PAGE_VTBL.pfnTick).
 */

#include "precomp.h"

#define APPHISTORY_CLASS  L"TaskMgrV2.Page.AppHistory"
#define IDC_AH_LABEL      50

/* -------------------------------------------------------------------------
 * Window procedure
 * ----------------------------------------------------------------------- */
static LRESULT CALLBACK
AppHistoryWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_CREATE:
        {
            /* Create a centered static label */
            HWND hLabel = CreateWindowExW(0, L"STATIC",
                L"Application history is not available on this system.",
                WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                0, 0, 0, 0,
                hWnd, (HMENU)(INT_PTR)IDC_AH_LABEL,
                (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), NULL);
            if (hLabel)
            {
                SendMessageW(hLabel, WM_SETFONT, (WPARAM)Theme_FontUI(), TRUE);
            }
            return 0;
        }

        case WM_SIZE:
        {
            int cx = LOWORD(lParam);
            int cy = HIWORD(lParam);
            HWND hLabel = GetDlgItem(hWnd, IDC_AH_LABEL);
            if (hLabel)
            {
                SetWindowPos(hLabel, NULL, 0, 0, cx, cy,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = (HDC)wParam;
            const MODERN_THEME *t = Theme_Current();
            SetTextColor(hdc, t->clrTextDim);
            SetBkColor(hdc, t->clrBg);
            return (LRESULT)Theme_BrushBg();
        }

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
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

/* -------------------------------------------------------------------------
 * Page vtbl implementation
 * ----------------------------------------------------------------------- */
HWND
PageAppHistory_Create(HWND hHost, HINSTANCE hInst)
{
    WNDCLASSEXW wc = {0};

    /* Register class once */
    if (!GetClassInfoExW(hInst, APPHISTORY_CLASS, &wc))
    {
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = AppHistoryWndProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = NULL; /* handled in WM_ERASEBKGND */
        wc.lpszClassName = APPHISTORY_CLASS;
        if (!RegisterClassExW(&wc))
            return NULL;
    }

    return CreateWindowExW(0, APPHISTORY_CLASS, NULL,
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0,
        hHost, NULL, hInst, NULL);
}

void
PageAppHistory_Tick(HWND hPage)
{
    /* No-op — static placeholder, no data fetch. */
    (void)hPage;
}

void
PageAppHistory_Resize(HWND hPage, int cx, int cy)
{
    SetWindowPos(hPage, NULL, 0, 0, cx, cy,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}

void
PageAppHistory_Destroy(HWND hPage)
{
    DestroyWindow(hPage);
}

void
PageAppHistory_Theme(HWND hPage)
{
    /* Repaint so WM_CTLCOLORSTATIC picks up the new theme colors. */
    InvalidateRect(hPage, NULL, TRUE);
    /* Also update the font on the label in case DPI changed */
    HWND hLabel = GetDlgItem(hPage, IDC_AH_LABEL);
    if (hLabel)
    {
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)Theme_FontUI(), TRUE);
    }
}
