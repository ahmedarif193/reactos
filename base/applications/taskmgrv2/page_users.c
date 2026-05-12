/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Users page — WTSEnumerateSessionsW, columns User/Session/Status/CPU/Memory
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9, constraint #9.
 * Data source: wtsapi32 WTSEnumerateSessionsW + WTSQuerySessionInformationW.
 * Reads Shell\UsersShowFullName DWORD on every tick to decide display format.
 */

#include "precomp.h"
#include <wtsapi32.h>

#define USERS_CLASS  L"TaskMgrV2.Page.Users"

/* Column indices */
#define UCOL_USER    0
#define UCOL_SESSION 1
#define UCOL_STATUS  2
#define UCOL_CPU     3
#define UCOL_MEM     4
#define UCOL__COUNT  5

#define REG_SHELL_KEY  L"Software\\ReactOS\\TaskMgrV2\\Shell"

typedef struct _USER_ENTRY
{
    DWORD  sessionId;
    WCHAR  userName[128];
    DWORD  state;       /* WTS_CONNECTSTATE_CLASS */
} USER_ENTRY;

typedef struct _USERS_PAGE
{
    HWND       hList;
    USER_ENTRY sessions[64];
    DWORD      nSessions;
} USERS_PAGE;

/* -------------------------------------------------------------------------
 * Map WTS state to string
 * ----------------------------------------------------------------------- */
static LPCWSTR
WtsStateStr(DWORD state)
{
    switch ((WTS_CONNECTSTATE_CLASS)state)
    {
        case WTSActive:       return L"Active";
        case WTSConnected:    return L"Connected";
        case WTSConnectQuery: return L"Connect query";
        case WTSShadow:       return L"Shadow";
        case WTSDisconnected: return L"Disconnected";
        case WTSIdle:         return L"Idle";
        case WTSListen:       return L"Listen";
        case WTSReset:        return L"Reset";
        case WTSDown:         return L"Down";
        case WTSInit:         return L"Init";
        default:              return L"Unknown";
    }
}

/* -------------------------------------------------------------------------
 * Snapshot: enumerate WTS sessions
 * ----------------------------------------------------------------------- */
static void
Users_Snapshot(USERS_PAGE *pPage, BOOL bShowFullName)
{
    PWTS_SESSION_INFOW pSessions = NULL;
    DWORD count = 0, i;
    DWORD n = 0;

    pPage->nSessions = 0;

    if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE,
            0, 1, &pSessions, &count))
        return;

    for (i = 0; i < count && n < 64; i++)
    {
        PWSTR pName = NULL;
        DWORD nameBytes = 0;
        USER_ENTRY *e = &pPage->sessions[n];

        e->sessionId = pSessions[i].SessionId;
        e->state     = (DWORD)pSessions[i].State;

        /* Get the user name for this session */
        if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,
                e->sessionId,
                bShowFullName ? WTSClientName : WTSUserName,
                &pName, &nameBytes))
        {
            if (pName && pName[0])
                StringCchCopyW(e->userName, ARRAYSIZE(e->userName), pName);
            else
                StringCchCopyW(e->userName, ARRAYSIZE(e->userName),
                    pSessions[i].pWinStationName);
            WTSFreeMemory(pName);
        }
        else
        {
            StringCchCopyW(e->userName, ARRAYSIZE(e->userName),
                pSessions[i].pWinStationName);
        }

        n++;
    }

    pPage->nSessions = n;

    if (pSessions)
        WTSFreeMemory(pSessions);
}

/* -------------------------------------------------------------------------
 * Refresh listview
 * ----------------------------------------------------------------------- */
static void
Users_RefreshList(USERS_PAGE *pPage)
{
    HWND    hList = pPage->hList;
    LVITEMW lvi;
    WCHAR   buf[32];
    DWORD   i;

    SendMessageW(hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hList);

    ZeroMemory(&lvi, sizeof(lvi));
    lvi.mask = LVIF_TEXT;

    for (i = 0; i < pPage->nSessions; i++)
    {
        USER_ENTRY *e = &pPage->sessions[i];
        int iItem;

        lvi.iItem    = (int)i;
        lvi.iSubItem = 0;
        lvi.pszText  = e->userName;
        iItem = ListView_InsertItem(hList, &lvi);
        if (iItem < 0) continue;

        StringCchPrintfW(buf, ARRAYSIZE(buf), L"%u", e->sessionId);
        ListView_SetItemText(hList, iItem, UCOL_SESSION, buf);

        ListView_SetItemText(hList, iItem, UCOL_STATUS,
            WtsStateStr(e->state));

        /* CPU / Memory per-session not available without ETW in v1 */
        ListView_SetItemText(hList, iItem, UCOL_CPU, L"N/A");
        ListView_SetItemText(hList, iItem, UCOL_MEM, L"N/A");
    }

    SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hList, NULL, FALSE);
}

/* -------------------------------------------------------------------------
 * Window procedure
 * ----------------------------------------------------------------------- */
static LRESULT CALLBACK
UsersWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    USERS_PAGE *pPage =
        (USERS_PAGE *)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_CREATE:
        {
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE);
            LVCOLUMNW lvc;

            pPage = (USERS_PAGE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, sizeof(USERS_PAGE));
            if (!pPage) return -1;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pPage);

            pPage->hList = CreateWindowExW(WS_EX_CLIENTEDGE,
                WC_LISTVIEWW, NULL,
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                0, 0, 0, 0,
                hWnd, (HMENU)(INT_PTR)10, hInst, NULL);
            if (!pPage->hList) return -1;

            ListView_SetExtendedListViewStyle(pPage->hList,
                LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT |
                LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP);

            ZeroMemory(&lvc, sizeof(lvc));
            lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;

            lvc.pszText = L"User";       lvc.cx = 180; lvc.fmt = LVCFMT_LEFT;
            ListView_InsertColumn(pPage->hList, UCOL_USER, &lvc);

            lvc.pszText = L"Session ID"; lvc.cx = 90;  lvc.fmt = LVCFMT_RIGHT;
            ListView_InsertColumn(pPage->hList, UCOL_SESSION, &lvc);

            lvc.pszText = L"Status";     lvc.cx = 100; lvc.fmt = LVCFMT_LEFT;
            ListView_InsertColumn(pPage->hList, UCOL_STATUS, &lvc);

            lvc.pszText = L"CPU";        lvc.cx = 60;  lvc.fmt = LVCFMT_RIGHT;
            ListView_InsertColumn(pPage->hList, UCOL_CPU, &lvc);

            lvc.pszText = L"Memory";     lvc.cx = 90;
            ListView_InsertColumn(pPage->hList, UCOL_MEM, &lvc);

            SendMessageW(pPage->hList, WM_SETFONT, (WPARAM)Theme_FontUI(), FALSE);

            {
                DWORD showFull = 0;
                Reg_ReadDword(HKEY_CURRENT_USER, REG_SHELL_KEY,
                    L"UsersShowFullName", &showFull);
                Users_Snapshot(pPage, showFull != 0);
            }
            Users_RefreshList(pPage);
            return 0;
        }

        case WM_DESTROY:
            if (pPage)
            {
                HeapFree(GetProcessHeap(), 0, pPage);
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
            }
            return 0;

        case WM_SIZE:
        {
            int cx = LOWORD(lParam);
            int cy = HIWORD(lParam);
            if (pPage && pPage->hList)
                SetWindowPos(pPage->hList, NULL, 0, 0, cx, cy,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_ERASEBKGND:
        {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, Theme_BrushBg());
            return 1;
        }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

/* -------------------------------------------------------------------------
 * Page vtbl implementation
 * ----------------------------------------------------------------------- */
HWND
PageUsers_Create(HWND hHost, HINSTANCE hInst)
{
    WNDCLASSEXW wc = {0};

    if (!GetClassInfoExW(hInst, USERS_CLASS, &wc))
    {
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = UsersWndProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = NULL;
        wc.lpszClassName = USERS_CLASS;
        if (!RegisterClassExW(&wc))
            return NULL;
    }

    return CreateWindowExW(0, USERS_CLASS, NULL,
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0,
        hHost, NULL, hInst, NULL);
}

void
PageUsers_Tick(HWND hPage)
{
    USERS_PAGE *pPage =
        (USERS_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    DWORD showFull = 0;
    if (!pPage) return;
    Reg_ReadDword(HKEY_CURRENT_USER, REG_SHELL_KEY,
        L"UsersShowFullName", &showFull);
    Users_Snapshot(pPage, showFull != 0);
    Users_RefreshList(pPage);
}

void
PageUsers_Resize(HWND hPage, int cx, int cy)
{
    SetWindowPos(hPage, NULL, 0, 0, cx, cy,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}

void
PageUsers_Destroy(HWND hPage)
{
    DestroyWindow(hPage);
}

void
PageUsers_Theme(HWND hPage)
{
    USERS_PAGE *pPage =
        (USERS_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (pPage && pPage->hList)
        SendMessageW(pPage->hList, WM_SETFONT, (WPARAM)Theme_FontUI(), TRUE);
    InvalidateRect(hPage, NULL, TRUE);
}
