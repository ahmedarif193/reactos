/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Startup page — Run registry keys + Startup folder enumeration
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9, constraint #10.
 * Sources:
 *   HKCU\Software\Microsoft\Windows\CurrentVersion\Run
 *   HKLM\Software\Microsoft\Windows\CurrentVersion\Run
 *   HKCU\Software\Microsoft\Windows\CurrentVersion\RunOnce (listed but not modified)
 *   Startup folder (SHGetSpecialFolderPathW with CSIDL_STARTUP)
 *
 * Columns: Name, Publisher, Status (Enabled/Disabled), Startup impact.
 * Right-click: Enable / Disable (toggle via disabled Run keys registry path).
 *
 * "Startup impact" heuristic:
 *   - Startup folder item: Low
 *   - HKLM Run entry: High (system-wide)
 *   - HKCU Run entry: Medium
 */

#include "precomp.h"
#include <shlobj.h>

#define STARTUP_CLASS  L"TaskMgrV2.Page.Startup"

/* Startup item sources */
typedef enum _STARTUP_SOURCE
{
    SRC_HKCU_RUN = 0,
    SRC_HKLM_RUN,
    SRC_STARTUP_FOLDER,
    SRC__COUNT
} STARTUP_SOURCE;

/* Startup impact level */
typedef enum _STARTUP_IMPACT
{
    IMPACT_LOW = 0,
    IMPACT_MEDIUM,
    IMPACT_HIGH
} STARTUP_IMPACT;

/* Column indices */
#define STCOL_NAME      0
#define STCOL_PUBLISHER 1
#define STCOL_STATUS    2
#define STCOL_IMPACT    3
#define STCOL__COUNT    4

/* Context menu IDs */
#define IDM_ST_ENABLE   1295
#define IDM_ST_DISABLE  1296

#define MAX_STARTUP 256

/* Registry key for disabled HKCU startup entries */
#define DISABLED_RUN_HKCU \
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"
#define DISABLED_RUN_HKLM \
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32"

typedef struct _STARTUP_ENTRY
{
    WCHAR          name[128];
    WCHAR          command[MAX_PATH];
    WCHAR          publisher[128];
    BOOL           bEnabled;
    STARTUP_SOURCE source;
    STARTUP_IMPACT impact;
} STARTUP_ENTRY;

typedef struct _STARTUP_PAGE
{
    HWND          hList;
    STARTUP_ENTRY items[MAX_STARTUP];
    DWORD         nItems;
} STARTUP_PAGE;

/* -------------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */
static LPCWSTR
ImpactStr(STARTUP_IMPACT impact)
{
    switch (impact)
    {
        case IMPACT_LOW:    return L"Low";
        case IMPACT_MEDIUM: return L"Medium";
        case IMPACT_HIGH:   return L"High";
        default:            return L"Unknown";
    }
}

/* Extract publisher from a command path via version info */
static void
GetPublisherFromCmd(LPCWSTR cmd, WCHAR *buf, int cch)
{
    WCHAR   path[MAX_PATH];
    DWORD   dummy, verSize;
    PVOID   verBuf;
    LPCWSTR p;

    buf[0] = L'\0';

    /* Strip leading quote if present */
    p = cmd;
    if (*p == L'"') p++;
    StringCchCopyNW(path, MAX_PATH, p, MAX_PATH - 1);
    /* Trim at the next quote or first space after .exe */
    {
        WCHAR *q = path;
        while (*q && *q != L'"')
        {
            if (*q == L' ' &&
                q > path + 4 &&
                _wcsicmp(q - 4, L".exe") > 0)
            {
                /* don't truncate inside "Program Files" */
                break;
            }
            q++;
        }
        if (*q == L'"') *q = L'\0';
    }

    verSize = GetFileVersionInfoSizeW(path, &dummy);
    if (!verSize) return;

    verBuf = HeapAlloc(GetProcessHeap(), 0, verSize);
    if (!verBuf) return;

    if (GetFileVersionInfoW(path, 0, verSize, verBuf))
    {
        struct { WORD lang; WORD codepage; } *trans;
        UINT transLen = 0;
        if (VerQueryValueW(verBuf, L"\\VarFileInfo\\Translation",
                (LPVOID *)&trans, &transLen) && transLen >= 4)
        {
            WCHAR key[64];
            LPVOID valPtr = NULL;
            UINT   valLen = 0;
            StringCchPrintfW(key, ARRAYSIZE(key),
                L"\\StringFileInfo\\%04x%04x\\CompanyName",
                trans[0].lang, trans[0].codepage);
            if (VerQueryValueW(verBuf, key, &valPtr, &valLen) && valPtr)
                StringCchCopyNW(buf, cch, (LPCWSTR)valPtr, valLen);
        }
    }

    HeapFree(GetProcessHeap(), 0, verBuf);
}

/* -------------------------------------------------------------------------
 * Enumerate a Run registry key
 * ----------------------------------------------------------------------- */
static void
EnumRunKey(HKEY hRoot, LPCWSTR subkey, STARTUP_SOURCE src,
    STARTUP_IMPACT impact, STARTUP_ENTRY *items, DWORD *pN, DWORD maxN)
{
    HKEY   hKey;
    DWORD  idx = 0;
    WCHAR  valName[128];
    WCHAR  valData[MAX_PATH];
    DWORD  nameLen, dataLen, type;

    if (RegOpenKeyExW(hRoot, subkey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return;

    while (*pN < maxN)
    {
        nameLen = ARRAYSIZE(valName);
        dataLen = sizeof(valData);
        if (RegEnumValueW(hKey, idx, valName, &nameLen,
                NULL, &type, (LPBYTE)valData, &dataLen) != ERROR_SUCCESS)
            break;

        if (type == REG_SZ || type == REG_EXPAND_SZ)
        {
            STARTUP_ENTRY *e = &items[*pN];
            StringCchCopyW(e->name, ARRAYSIZE(e->name), valName);
            StringCchCopyW(e->command, ARRAYSIZE(e->command), valData);
            e->source   = src;
            e->impact   = impact;
            e->bEnabled = TRUE; /* will check disabled key below */
            GetPublisherFromCmd(e->command, e->publisher,
                ARRAYSIZE(e->publisher));
            (*pN)++;
        }

        idx++;
    }

    RegCloseKey(hKey);
}

/* -------------------------------------------------------------------------
 * Enumerate Startup folder
 * ----------------------------------------------------------------------- */
static void
EnumStartupFolder(STARTUP_ENTRY *items, DWORD *pN, DWORD maxN)
{
    WCHAR   folderPath[MAX_PATH];
    WCHAR   searchPath[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE  hFind;

    if (!SHGetSpecialFolderPathW(NULL, folderPath, CSIDL_STARTUP, FALSE))
        return;

    StringCchCopyW(searchPath, ARRAYSIZE(searchPath), folderPath);
    StringCchCatW(searchPath, ARRAYSIZE(searchPath), L"\\*");

    hFind = FindFirstFileW(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (*pN >= maxN) break;

        {
            STARTUP_ENTRY *e = &items[*pN];
            /* Use filename without extension as Name */
            StringCchCopyW(e->name, ARRAYSIZE(e->name), fd.cFileName);
            StringCchPrintfW(e->command, ARRAYSIZE(e->command),
                L"%s\\%s", folderPath, fd.cFileName);
            e->source   = SRC_STARTUP_FOLDER;
            e->impact   = IMPACT_LOW;
            e->bEnabled = TRUE;
            GetPublisherFromCmd(e->command, e->publisher,
                ARRAYSIZE(e->publisher));
            (*pN)++;
        }
    }
    while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

/* -------------------------------------------------------------------------
 * Check which entries are disabled via Explorer\StartupApproved\Run
 * The disabled blob has a specific binary format: first byte 0x03 = disabled,
 * 0x02 = enabled. We check only the first byte.
 * ----------------------------------------------------------------------- */
static void
MarkDisabledEntries(STARTUP_ENTRY *items, DWORD n)
{
    static const LPCWSTR keys[SRC__COUNT] =
    {
        DISABLED_RUN_HKCU,   /* SRC_HKCU_RUN */
        DISABLED_RUN_HKLM,   /* SRC_HKLM_RUN */
        NULL                 /* SRC_STARTUP_FOLDER — handled differently */
    };
    static const HKEY   roots[SRC__COUNT] =
    {
        HKEY_CURRENT_USER,
        HKEY_LOCAL_MACHINE,
        HKEY_CURRENT_USER   /* unused */
    };

    DWORD  i;
    HKEY   hApproved[2] = {NULL, NULL};
    int    k;

    for (k = 0; k < 2; k++)
    {
        RegOpenKeyExW(roots[k], keys[k], 0, KEY_READ, &hApproved[k]);
    }

    for (i = 0; i < n; i++)
    {
        STARTUP_ENTRY *e = &items[i];
        if (e->source == SRC_STARTUP_FOLDER) continue;

        {
            HKEY  hK = hApproved[e->source == SRC_HKCU_RUN ? 0 : 1];
            BYTE  blob[12];
            DWORD blobLen = sizeof(blob);
            DWORD type    = 0;

            if (hK && RegQueryValueExW(hK, e->name, NULL,
                    &type, blob, &blobLen) == ERROR_SUCCESS)
            {
                /* byte[0]: 0x03 = disabled, 0x02 = enabled */
                if (blobLen >= 1 && blob[0] == 0x03)
                    e->bEnabled = FALSE;
            }
        }
    }

    for (k = 0; k < 2; k++)
        if (hApproved[k]) RegCloseKey(hApproved[k]);
}

/* -------------------------------------------------------------------------
 * Snapshot
 * ----------------------------------------------------------------------- */
static void
Startup_Snapshot(STARTUP_PAGE *pPage)
{
    DWORD n = 0;

    EnumRunKey(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        SRC_HKCU_RUN, IMPACT_MEDIUM,
        pPage->items, &n, MAX_STARTUP);

    EnumRunKey(HKEY_LOCAL_MACHINE,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        SRC_HKLM_RUN, IMPACT_HIGH,
        pPage->items, &n, MAX_STARTUP);

    EnumStartupFolder(pPage->items, &n, MAX_STARTUP);

    MarkDisabledEntries(pPage->items, n);

    pPage->nItems = n;
}

/* -------------------------------------------------------------------------
 * Refresh listview
 * ----------------------------------------------------------------------- */
static void
Startup_RefreshList(STARTUP_PAGE *pPage)
{
    HWND    hList = pPage->hList;
    LVITEMW lvi;
    DWORD   i;

    SendMessageW(hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hList);

    ZeroMemory(&lvi, sizeof(lvi));
    lvi.mask = LVIF_TEXT;

    for (i = 0; i < pPage->nItems; i++)
    {
        STARTUP_ENTRY *e = &pPage->items[i];
        int iItem;

        lvi.iItem    = (int)i;
        lvi.iSubItem = 0;
        lvi.pszText  = e->name;
        iItem = ListView_InsertItem(hList, &lvi);
        if (iItem < 0) continue;

        ListView_SetItemText(hList, iItem, STCOL_PUBLISHER,
            e->publisher[0] ? e->publisher : L"Unknown");

        ListView_SetItemText(hList, iItem, STCOL_STATUS,
            e->bEnabled ? L"Enabled" : L"Disabled");

        ListView_SetItemText(hList, iItem, STCOL_IMPACT,
            ImpactStr(e->impact));
    }

    SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hList, NULL, FALSE);
}

/* -------------------------------------------------------------------------
 * Enable / Disable a startup entry
 * We write to Explorer\StartupApproved\Run: 0x03... = disable, 0x02... = enable.
 * ----------------------------------------------------------------------- */
static void
Startup_SetEnabled(STARTUP_ENTRY *e, BOOL bEnable)
{
    static const LPCWSTR regKeys[2] =
    {
        DISABLED_RUN_HKCU,
        DISABLED_RUN_HKLM
    };
    static const HKEY roots[2] =
    {
        HKEY_CURRENT_USER,
        HKEY_LOCAL_MACHINE
    };
    BYTE  blob[12] = {0};
    HKEY  hKey;
    int   idx;

    if (e->source == SRC_STARTUP_FOLDER)
    {
        /* Startup folder items: rename with .disabled extension */
        WCHAR newPath[MAX_PATH];
        if (bEnable)
        {
            /* If command ends in .disabled, rename back */
            size_t len = wcslen(e->command);
            if (len > 9 &&
                _wcsicmp(e->command + len - 9, L".disabled") == 0)
            {
                StringCchCopyW(newPath, ARRAYSIZE(newPath), e->command);
                newPath[len - 9] = L'\0';
                MoveFileW(e->command, newPath);
            }
        }
        else
        {
            StringCchPrintfW(newPath, ARRAYSIZE(newPath),
                L"%s.disabled", e->command);
            MoveFileW(e->command, newPath);
        }
        return;
    }

    idx = (e->source == SRC_HKCU_RUN) ? 0 : 1;

    if (RegCreateKeyExW(roots[idx], regKeys[idx],
            0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return;

    /* Build the 12-byte blob. byte[0] = 0x02 (enabled) or 0x03 (disabled).
     * Remaining 11 bytes are typically a timestamp — zero is fine. */
    blob[0] = bEnable ? 0x02 : 0x03;

    RegSetValueExW(hKey, e->name, 0, REG_BINARY, blob, 12);
    RegCloseKey(hKey);
    e->bEnabled = bEnable;
}

/* -------------------------------------------------------------------------
 * Context menu
 * ----------------------------------------------------------------------- */
static void
Startup_ContextMenu(HWND hWnd, STARTUP_PAGE *pPage, POINT pt)
{
    int   iSel = ListView_GetNextItem(pPage->hList, -1, LVNI_SELECTED);
    HMENU hMenu;

    if (iSel < 0 || iSel >= (int)pPage->nItems) return;

    hMenu = CreatePopupMenu();
    if (!hMenu) return;

    {
        STARTUP_ENTRY *e = &pPage->items[iSel];
        AppendMenuW(hMenu, MF_STRING | (e->bEnabled  ? MF_GRAYED : 0),
            IDM_ST_ENABLE,  L"Enable");
        AppendMenuW(hMenu, MF_STRING | (!e->bEnabled ? MF_GRAYED : 0),
            IDM_ST_DISABLE, L"Disable");

        {
            int cmd = TrackPopupMenu(hMenu,
                TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                pt.x, pt.y, 0, hWnd, NULL);
            switch (cmd)
            {
                case IDM_ST_ENABLE:
                    Startup_SetEnabled(e, TRUE);
                    break;
                case IDM_ST_DISABLE:
                    Startup_SetEnabled(e, FALSE);
                    break;
            }
        }
    }

    DestroyMenu(hMenu);
    /* Refresh item text after change */
    {
        STARTUP_ENTRY *e = &pPage->items[iSel];
        ListView_SetItemText(pPage->hList, iSel, STCOL_STATUS,
            e->bEnabled ? L"Enabled" : L"Disabled");
    }
}

/* -------------------------------------------------------------------------
 * Window procedure
 * ----------------------------------------------------------------------- */
static LRESULT CALLBACK
StartupWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    STARTUP_PAGE *pPage =
        (STARTUP_PAGE *)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_CREATE:
        {
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE);
            LVCOLUMNW lvc;

            pPage = (STARTUP_PAGE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, sizeof(STARTUP_PAGE));
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

            lvc.pszText = L"Name";           lvc.cx = 200; lvc.fmt = LVCFMT_LEFT;
            ListView_InsertColumn(pPage->hList, STCOL_NAME, &lvc);

            lvc.pszText = L"Publisher";      lvc.cx = 180;
            ListView_InsertColumn(pPage->hList, STCOL_PUBLISHER, &lvc);

            lvc.pszText = L"Status";         lvc.cx = 90;
            ListView_InsertColumn(pPage->hList, STCOL_STATUS, &lvc);

            lvc.pszText = L"Startup impact"; lvc.cx = 110;
            ListView_InsertColumn(pPage->hList, STCOL_IMPACT, &lvc);

            SendMessageW(pPage->hList, WM_SETFONT, (WPARAM)Theme_FontUI(), FALSE);

            Startup_Snapshot(pPage);
            Startup_RefreshList(pPage);
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
                Startup_ContextMenu(hWnd, pPage, pt);
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
PageStartup_Create(HWND hHost, HINSTANCE hInst)
{
    WNDCLASSEXW wc = {0};

    if (!GetClassInfoExW(hInst, STARTUP_CLASS, &wc))
    {
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = StartupWndProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = NULL;
        wc.lpszClassName = STARTUP_CLASS;
        if (!RegisterClassExW(&wc))
            return NULL;
    }

    return CreateWindowExW(0, STARTUP_CLASS, NULL,
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0,
        hHost, NULL, hInst, NULL);
}

void
PageStartup_Tick(HWND hPage)
{
    STARTUP_PAGE *pPage =
        (STARTUP_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (!pPage) return;
    Startup_Snapshot(pPage);
    Startup_RefreshList(pPage);
}

void
PageStartup_Resize(HWND hPage, int cx, int cy)
{
    SetWindowPos(hPage, NULL, 0, 0, cx, cy,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}

void
PageStartup_Destroy(HWND hPage)
{
    DestroyWindow(hPage);
}

void
PageStartup_Theme(HWND hPage)
{
    STARTUP_PAGE *pPage =
        (STARTUP_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (pPage && pPage->hList)
        SendMessageW(pPage->hList, WM_SETFONT, (WPARAM)Theme_FontUI(), TRUE);
    InvalidateRect(hPage, NULL, TRUE);
}
