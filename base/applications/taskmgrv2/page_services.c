/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Services page — SCM enumeration via OpenSCManagerW + EnumServicesStatusExW
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9, constraint #8.
 * Columns: Name, PID, Description, Status, Group.
 * Right-click: Start, Stop, Restart, Open Services Console.
 */

#include "precomp.h"
#include <winsvc.h>

#define SERVICES_CLASS  L"TaskMgrV2.Page.Services"

/* Column indices */
#define SCOL_NAME   0
#define SCOL_PID    1
#define SCOL_DESC   2
#define SCOL_STATUS 3
#define SCOL_GROUP  4
#define SCOL__COUNT 5

/* Context menu IDs */
#define IDM_SVC_START    1290
#define IDM_SVC_STOP     1291
#define IDM_SVC_RESTART  1292
#define IDM_SVC_CONSOLE  1293

#define MAX_SERVICES 1024

typedef struct _SVC_ENTRY
{
    WCHAR  name[256];
    WCHAR  displayName[256];
    WCHAR  desc[256];
    WCHAR  group[64];
    DWORD  pid;
    DWORD  state;       /* SERVICE_* running state */
    BOOL   bCanStop;
    BOOL   bCanStart;
} SVC_ENTRY;

typedef struct _SERVICES_PAGE
{
    HWND      hList;
    SVC_ENTRY svcs[MAX_SERVICES];
    DWORD     nSvcs;
} SERVICES_PAGE;

/* -------------------------------------------------------------------------
 * Map service state to string
 * ----------------------------------------------------------------------- */
static LPCWSTR
SvcStateStr(DWORD state)
{
    switch (state)
    {
        case SERVICE_STOPPED:          return L"Stopped";
        case SERVICE_START_PENDING:    return L"Starting";
        case SERVICE_STOP_PENDING:     return L"Stopping";
        case SERVICE_RUNNING:          return L"Running";
        case SERVICE_CONTINUE_PENDING: return L"Resuming";
        case SERVICE_PAUSE_PENDING:    return L"Pausing";
        case SERVICE_PAUSED:           return L"Paused";
        default:                       return L"Unknown";
    }
}

/* -------------------------------------------------------------------------
 * Snapshot: enumerate all services via SCM
 * ----------------------------------------------------------------------- */
static void
Services_Snapshot(SERVICES_PAGE *pPage)
{
    SC_HANDLE hSCM;
    DWORD     bytesNeeded = 0, resumeHandle = 0;
    DWORD     n = 0, i;
    LPBYTE    buf = NULL;
    DWORD     bufLen = 0;
    BOOL      ok;
    LPENUM_SERVICE_STATUS_PROCESSW pEnum;
    DWORD     count = 0;

    pPage->nSvcs = 0;

    hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!hSCM) return;

    /* First call to get required buffer size */
    ok = EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO,
        SERVICE_WIN32 | SERVICE_DRIVER,
        SERVICE_STATE_ALL,
        NULL, 0, &bytesNeeded, &count, &resumeHandle, NULL);

    if (!ok && GetLastError() == ERROR_MORE_DATA)
    {
        bufLen = bytesNeeded + 4096;
        buf = (LPBYTE)HeapAlloc(GetProcessHeap(), 0, bufLen);
        if (buf)
        {
            resumeHandle = 0;
            ok = EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO,
                SERVICE_WIN32 | SERVICE_DRIVER,
                SERVICE_STATE_ALL,
                buf, bufLen, &bytesNeeded, &count, &resumeHandle, NULL);
        }
    }

    if (ok && buf && count > 0)
    {
        pEnum = (LPENUM_SERVICE_STATUS_PROCESSW)buf;
        for (i = 0; i < count && n < MAX_SERVICES; i++, pEnum++)
        {
            SVC_ENTRY *e = &pPage->svcs[n];

            StringCchCopyW(e->name, ARRAYSIZE(e->name),
                pEnum->lpServiceName);
            StringCchCopyW(e->displayName, ARRAYSIZE(e->displayName),
                pEnum->lpDisplayName);

            e->pid    = pEnum->ServiceStatusProcess.dwProcessId;
            e->state  = pEnum->ServiceStatusProcess.dwCurrentState;
            e->bCanStop  = (pEnum->ServiceStatusProcess.dwControlsAccepted &
                            SERVICE_ACCEPT_STOP) != 0;
            e->bCanStart = (e->state == SERVICE_STOPPED);

            /* Get description and load order group from service config */
            {
                SC_HANDLE hSvc = OpenServiceW(hSCM, e->name,
                    SERVICE_QUERY_CONFIG);
                if (hSvc)
                {
                    BYTE cfgBuf[4096];
                    DWORD needed = 0;
                    if (QueryServiceConfigW(hSvc,
                            (LPQUERY_SERVICE_CONFIGW)cfgBuf,
                            sizeof(cfgBuf), &needed))
                    {
                        LPQUERY_SERVICE_CONFIGW cfg =
                            (LPQUERY_SERVICE_CONFIGW)cfgBuf;
                        if (cfg->lpLoadOrderGroup)
                            StringCchCopyW(e->group, ARRAYSIZE(e->group),
                                cfg->lpLoadOrderGroup);
                        else
                            e->group[0] = L'\0';
                    }

                    /* Description */
                    {
                        SERVICE_DESCRIPTIONW svcDesc;
                        BYTE descBuf[1024];
                        DWORD descNeeded = 0;
                        if (QueryServiceConfig2W(hSvc,
                                SERVICE_CONFIG_DESCRIPTION,
                                descBuf, sizeof(descBuf), &descNeeded))
                        {
                            SERVICE_DESCRIPTIONW *sd =
                                (SERVICE_DESCRIPTIONW *)descBuf;
                            if (sd->lpDescription)
                                StringCchCopyW(e->desc, ARRAYSIZE(e->desc),
                                    sd->lpDescription);
                            else
                                e->desc[0] = L'\0';
                        }
                        else
                        {
                            e->desc[0] = L'\0';
                        }
                        (void)svcDesc;
                    }

                    CloseServiceHandle(hSvc);
                }
                else
                {
                    e->group[0] = L'\0';
                    e->desc[0]  = L'\0';
                }
            }

            n++;
        }
    }

    pPage->nSvcs = n;

    if (buf) HeapFree(GetProcessHeap(), 0, buf);
    CloseServiceHandle(hSCM);
}

/* -------------------------------------------------------------------------
 * Refresh listview
 * ----------------------------------------------------------------------- */
static void
Services_RefreshList(SERVICES_PAGE *pPage)
{
    HWND    hList = pPage->hList;
    LVITEMW lvi;
    WCHAR   buf[64];
    DWORD   i;

    SendMessageW(hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hList);

    ZeroMemory(&lvi, sizeof(lvi));
    lvi.mask = LVIF_TEXT;

    for (i = 0; i < pPage->nSvcs; i++)
    {
        SVC_ENTRY *e = &pPage->svcs[i];
        int iItem;

        lvi.iItem    = (int)i;
        lvi.iSubItem = 0;
        lvi.pszText  = e->name;
        iItem = ListView_InsertItem(hList, &lvi);
        if (iItem < 0) continue;

        if (e->pid != 0)
            StringCchPrintfW(buf, ARRAYSIZE(buf), L"%u", e->pid);
        else
            buf[0] = L'\0';
        ListView_SetItemText(hList, iItem, SCOL_PID, buf);

        ListView_SetItemText(hList, iItem, SCOL_DESC,
            e->desc[0] ? e->desc : L"");

        ListView_SetItemText(hList, iItem, SCOL_STATUS,
            SvcStateStr(e->state));

        ListView_SetItemText(hList, iItem, SCOL_GROUP,
            e->group[0] ? e->group : L"");
    }

    SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hList, NULL, FALSE);
}

/* -------------------------------------------------------------------------
 * Start / stop helpers
 * ----------------------------------------------------------------------- */
static void
Services_Start(LPCWSTR svcName)
{
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return;
    SC_HANDLE hSvc = OpenServiceW(hSCM, svcName,
        SERVICE_START | SERVICE_QUERY_STATUS);
    if (hSvc)
    {
        StartServiceW(hSvc, 0, NULL);
        CloseServiceHandle(hSvc);
    }
    CloseServiceHandle(hSCM);
}

static void
Services_Stop(LPCWSTR svcName)
{
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return;
    SC_HANDLE hSvc = OpenServiceW(hSCM, svcName,
        SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (hSvc)
    {
        SERVICE_STATUS ss;
        ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);
        CloseServiceHandle(hSvc);
    }
    CloseServiceHandle(hSCM);
}

/* -------------------------------------------------------------------------
 * Context menu
 * ----------------------------------------------------------------------- */
static void
Services_ContextMenu(HWND hWnd, SERVICES_PAGE *pPage, POINT pt)
{
    int   iSel = ListView_GetNextItem(pPage->hList, -1, LVNI_SELECTED);
    HMENU hMenu;

    if (iSel < 0 || iSel >= (int)pPage->nSvcs) return;
    SVC_ENTRY *e = &pPage->svcs[iSel];

    hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING | (e->bCanStart ? 0 : MF_GRAYED),
        IDM_SVC_START,   L"Start");
    AppendMenuW(hMenu, MF_STRING | (e->bCanStop  ? 0 : MF_GRAYED),
        IDM_SVC_STOP,    L"Stop");
    AppendMenuW(hMenu, MF_STRING | (e->bCanStop  ? 0 : MF_GRAYED),
        IDM_SVC_RESTART, L"Restart");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_SVC_CONSOLE, L"Open Services console");

    {
        int cmd = TrackPopupMenu(hMenu,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
            pt.x, pt.y, 0, hWnd, NULL);

        switch (cmd)
        {
            case IDM_SVC_START:
                Services_Start(e->name);
                break;

            case IDM_SVC_STOP:
                Services_Stop(e->name);
                break;

            case IDM_SVC_RESTART:
                Services_Stop(e->name);
                /* Give the service a moment to stop before restarting */
                Sleep(500);
                Services_Start(e->name);
                break;

            case IDM_SVC_CONSOLE:
                ShellExecuteW(hWnd, L"open", L"services.msc",
                    NULL, NULL, SW_SHOWNORMAL);
                break;
        }
    }

    DestroyMenu(hMenu);
}

/* -------------------------------------------------------------------------
 * Window procedure
 * ----------------------------------------------------------------------- */
static LRESULT CALLBACK
ServicesWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SERVICES_PAGE *pPage =
        (SERVICES_PAGE *)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_CREATE:
        {
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE);
            LVCOLUMNW lvc;

            pPage = (SERVICES_PAGE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, sizeof(SERVICES_PAGE));
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

            lvc.pszText = L"Name";        lvc.cx = 180; lvc.fmt = LVCFMT_LEFT;
            ListView_InsertColumn(pPage->hList, SCOL_NAME, &lvc);

            lvc.pszText = L"PID";         lvc.cx = 60;  lvc.fmt = LVCFMT_RIGHT;
            ListView_InsertColumn(pPage->hList, SCOL_PID, &lvc);

            lvc.pszText = L"Description"; lvc.cx = 220; lvc.fmt = LVCFMT_LEFT;
            ListView_InsertColumn(pPage->hList, SCOL_DESC, &lvc);

            lvc.pszText = L"Status";      lvc.cx = 80;
            ListView_InsertColumn(pPage->hList, SCOL_STATUS, &lvc);

            lvc.pszText = L"Group";       lvc.cx = 120;
            ListView_InsertColumn(pPage->hList, SCOL_GROUP, &lvc);

            SendMessageW(pPage->hList, WM_SETFONT, (WPARAM)Theme_FontUI(), FALSE);

            Services_Snapshot(pPage);
            Services_RefreshList(pPage);
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

        case WM_NOTIFY:
        {
            LPNMHDR pnm = (LPNMHDR)lParam;
            if (pnm->idFrom == 10 && pnm->code == NM_RCLICK)
            {
                POINT pt;
                GetCursorPos(&pt);
                Services_ContextMenu(hWnd, pPage, pt);
            }
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
PageServices_Create(HWND hHost, HINSTANCE hInst)
{
    WNDCLASSEXW wc = {0};

    if (!GetClassInfoExW(hInst, SERVICES_CLASS, &wc))
    {
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ServicesWndProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = NULL;
        wc.lpszClassName = SERVICES_CLASS;
        if (!RegisterClassExW(&wc))
            return NULL;
    }

    return CreateWindowExW(0, SERVICES_CLASS, NULL,
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0,
        hHost, NULL, hInst, NULL);
}

void
PageServices_Tick(HWND hPage)
{
    SERVICES_PAGE *pPage =
        (SERVICES_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (!pPage) return;
    Services_Snapshot(pPage);
    Services_RefreshList(pPage);
}

void
PageServices_Resize(HWND hPage, int cx, int cy)
{
    SetWindowPos(hPage, NULL, 0, 0, cx, cy,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}

void
PageServices_Destroy(HWND hPage)
{
    DestroyWindow(hPage);
}

void
PageServices_Theme(HWND hPage)
{
    SERVICES_PAGE *pPage =
        (SERVICES_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (pPage && pPage->hList)
        SendMessageW(pPage->hList, WM_SETFONT, (WPARAM)Theme_FontUI(), TRUE);
    InvalidateRect(hPage, NULL, TRUE);
}
