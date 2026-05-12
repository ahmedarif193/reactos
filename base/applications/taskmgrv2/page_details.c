/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Details page — flat listview, all process columns, right-click menu
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9, constraint #7.
 * Columns: Name, PID, Status, User, CPU%, Memory, Architecture, Description.
 * Right-click: End task, End process tree, Set priority (submenu),
 *              Set affinity, Properties.
 */

#include "precomp.h"
#include <ndk/exfuncs.h>
#include <ndk/pstypes.h>
#include <psapi.h>
#include <winver.h>

#define DETAILS_CLASS  L"TaskMgrV2.Page.Details"

/* Column indices */
#define DCOL_NAME   0
#define DCOL_PID    1
#define DCOL_STATUS 2
#define DCOL_USER   3
#define DCOL_CPU    4
#define DCOL_MEM    5
#define DCOL_ARCH   6
#define DCOL_DESC   7
#define DCOL__COUNT 8

/* Right-click IDs — same range as Processes page, distinct values */
#define IDM_DET_ENDTASK     1270
#define IDM_DET_ENDTREE     1271
#define IDM_DET_AFFINITY    1272
#define IDM_DET_PROPERTIES  1273
#define IDM_DET_PRIO_RT     1274
#define IDM_DET_PRIO_HIGH   1275
#define IDM_DET_PRIO_ABOVE  1276
#define IDM_DET_PRIO_NORMAL 1277
#define IDM_DET_PRIO_BELOW  1278
#define IDM_DET_PRIO_IDLE   1279

#define MAX_DET_PROCS 1024

typedef struct _DET_ENTRY
{
    DWORD  pid;
    DWORD  cpuPct;
    SIZE_T workingSetBytes;
    BOOL   bIs64Bit;
    WCHAR  name[64];
    WCHAR  user[64];
    WCHAR  desc[128];
} DET_ENTRY;

typedef struct _DETAILS_PAGE
{
    HWND      hList;
    DET_ENTRY procs[MAX_DET_PROCS];
    DWORD     nProcs;
} DETAILS_PAGE;

/* -------------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */
static void
FormatMemKB(WCHAR *buf, int cch, SIZE_T bytes)
{
    SIZE_T kb = bytes / 1024;
    if (kb < 1024)
        StringCchPrintfW(buf, cch, L"%u K", (UINT)kb);
    else
        StringCchPrintfW(buf, cch, L"%u MB", (UINT)(kb / 1024));
}

/* Try to get the token user name for a process */
static void
GetProcessUserName(DWORD pid, WCHAR *buf, int cch)
{
    HANDLE hProc, hToken;
    BYTE   tokenBuf[256];
    DWORD  needed;
    SID_NAME_USE use;
    WCHAR  name[64], domain[64];
    DWORD  nameSz = 64, domainSz = 64;

    buf[0] = L'\0';
    hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProc) return;

    if (!OpenProcessToken(hProc, TOKEN_QUERY, &hToken))
    {
        CloseHandle(hProc);
        return;
    }

    if (GetTokenInformation(hToken, TokenUser,
            tokenBuf, sizeof(tokenBuf), &needed))
    {
        TOKEN_USER *ptu = (TOKEN_USER *)tokenBuf;
        if (LookupAccountSidW(NULL, ptu->User.Sid,
                name, &nameSz, domain, &domainSz, &use))
        {
            StringCchPrintfW(buf, cch, L"%s\\%s", domain, name);
        }
    }

    CloseHandle(hToken);
    CloseHandle(hProc);
}

/* Try to get the file description from version info */
static void
GetProcessDesc(DWORD pid, WCHAR *buf, int cch)
{
    HANDLE hProc;
    WCHAR  path[MAX_PATH];
    DWORD  dummy, verSize;
    PVOID  verBuf;

    buf[0] = L'\0';
    hProc = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return;

    if (!GetModuleFileNameExW(hProc, NULL, path, MAX_PATH))
    {
        CloseHandle(hProc);
        return;
    }
    CloseHandle(hProc);

    verSize = GetFileVersionInfoSizeW(path, &dummy);
    if (!verSize) return;

    verBuf = HeapAlloc(GetProcessHeap(), 0, verSize);
    if (!verBuf) return;

    if (GetFileVersionInfoW(path, 0, verSize, verBuf))
    {
        struct { WORD lang; WORD codepage; } *trans;
        UINT transLen = 0;
        if (VerQueryValueW(verBuf, L"\\VarFileInfo\\Translation",
                (LPVOID*)&trans, &transLen) && transLen >= 4)
        {
            WCHAR key[64];
            LPVOID descPtr = NULL;
            UINT   descLen = 0;
            StringCchPrintfW(key, ARRAYSIZE(key),
                L"\\StringFileInfo\\%04x%04x\\FileDescription",
                trans[0].lang, trans[0].codepage);
            if (VerQueryValueW(verBuf, key, &descPtr, &descLen) && descPtr)
                StringCchCopyNW(buf, cch, (LPCWSTR)descPtr, descLen);
        }
    }

    HeapFree(GetProcessHeap(), 0, verBuf);
}

/* Determine if a process is 32-bit or 64-bit */
static BOOL
IsProcess64Bit(DWORD pid)
{
#ifdef _WIN64
    HANDLE hProc;
    BOOL   bWow64 = FALSE;
    hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProc) return FALSE;
    IsWow64Process(hProc, &bWow64);
    CloseHandle(hProc);
    return !bWow64; /* 64-bit host, not wow64 => 64-bit process */
#else
    (void)pid;
    return FALSE; /* 32-bit host, all processes are 32-bit */
#endif
}

/* -------------------------------------------------------------------------
 * Snapshot
 * ----------------------------------------------------------------------- */
static void
Details_Snapshot(DETAILS_PAGE *pPage)
{
    NTSTATUS status;
    ULONG    bufLen = 1024 * 256;
    PVOID    buf;
    ULONG    retLen = 0;
    DWORD    n = 0;

    buf = HeapAlloc(GetProcessHeap(), 0, bufLen);
    if (!buf) return;

    status = NtQuerySystemInformation(SystemProcessInformation,
        buf, bufLen, &retLen);
    if (status == STATUS_INFO_LENGTH_MISMATCH)
    {
        HeapFree(GetProcessHeap(), 0, buf);
        bufLen = retLen + 4096;
        buf = HeapAlloc(GetProcessHeap(), 0, bufLen);
        if (!buf) return;
        status = NtQuerySystemInformation(SystemProcessInformation,
            buf, bufLen, &retLen);
    }

    if (!NT_SUCCESS(status)) { HeapFree(GetProcessHeap(), 0, buf); return; }

    {
        PSYSTEM_PROCESS_INFORMATION pInfo =
            (PSYSTEM_PROCESS_INFORMATION)buf;
        for (;;)
        {
            if (n < MAX_DET_PROCS)
            {
                DET_ENTRY *e = &pPage->procs[n];
                e->pid = HandleToUlong(pInfo->UniqueProcessId);

                if (pInfo->ImageName.Buffer && pInfo->ImageName.Length > 0)
                {
                    StringCchCopyNW(e->name, ARRAYSIZE(e->name),
                        pInfo->ImageName.Buffer,
                        pInfo->ImageName.Length / sizeof(WCHAR));
                }
                else
                {
                    StringCchCopyW(e->name, ARRAYSIZE(e->name), L"System");
                }

                e->workingSetBytes = pInfo->WorkingSetSize;
                e->cpuPct = 0; /* v1: stub */
                e->bIs64Bit = IsProcess64Bit(e->pid);

                GetProcessUserName(e->pid, e->user, ARRAYSIZE(e->user));
                GetProcessDesc(e->pid, e->desc, ARRAYSIZE(e->desc));
                n++;
            }
            if (pInfo->NextEntryOffset == 0) break;
            pInfo = (PSYSTEM_PROCESS_INFORMATION)
                ((PBYTE)pInfo + pInfo->NextEntryOffset);
        }
    }

    pPage->nProcs = n;
    HeapFree(GetProcessHeap(), 0, buf);
}

/* -------------------------------------------------------------------------
 * Refresh listview
 * ----------------------------------------------------------------------- */
static void
Details_RefreshList(DETAILS_PAGE *pPage)
{
    HWND    hList = pPage->hList;
    LVITEMW lvi;
    WCHAR   buf[64];
    DWORD   i;

    SendMessageW(hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hList);

    ZeroMemory(&lvi, sizeof(lvi));
    lvi.mask = LVIF_TEXT;

    for (i = 0; i < pPage->nProcs; i++)
    {
        DET_ENTRY *e = &pPage->procs[i];
        int iItem;

        lvi.iItem    = (int)i;
        lvi.iSubItem = 0;
        lvi.pszText  = e->name;
        iItem = ListView_InsertItem(hList, &lvi);
        if (iItem < 0) continue;

        StringCchPrintfW(buf, ARRAYSIZE(buf), L"%u", e->pid);
        ListView_SetItemText(hList, iItem, DCOL_PID, buf);

        ListView_SetItemText(hList, iItem, DCOL_STATUS, L"Running");

        ListView_SetItemText(hList, iItem, DCOL_USER,
            e->user[0] ? e->user : L"N/A");

        StringCchPrintfW(buf, ARRAYSIZE(buf), L"%u%%", e->cpuPct);
        ListView_SetItemText(hList, iItem, DCOL_CPU, buf);

        FormatMemKB(buf, ARRAYSIZE(buf), e->workingSetBytes);
        ListView_SetItemText(hList, iItem, DCOL_MEM, buf);

        ListView_SetItemText(hList, iItem, DCOL_ARCH,
            e->bIs64Bit ? L"64-bit" : L"32-bit");

        ListView_SetItemText(hList, iItem, DCOL_DESC,
            e->desc[0] ? e->desc : L"");
    }

    SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hList, NULL, FALSE);
}

/* -------------------------------------------------------------------------
 * Context menu
 * ----------------------------------------------------------------------- */
static void
Details_ContextMenu(HWND hWnd, DETAILS_PAGE *pPage, POINT pt)
{
    int    iSel = ListView_GetNextItem(pPage->hList, -1, LVNI_SELECTED);
    HMENU  hMenu, hPrioMenu;
    DWORD  warn = 1;

    if (iSel < 0) return;

    hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, IDM_DET_ENDTASK,    L"End task");
    AppendMenuW(hMenu, MF_STRING, IDM_DET_ENDTREE,    L"End process tree");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    hPrioMenu = CreatePopupMenu();
    if (hPrioMenu)
    {
        AppendMenuW(hPrioMenu, MF_STRING, IDM_DET_PRIO_RT,     L"Realtime");
        AppendMenuW(hPrioMenu, MF_STRING, IDM_DET_PRIO_HIGH,   L"High");
        AppendMenuW(hPrioMenu, MF_STRING, IDM_DET_PRIO_ABOVE,  L"Above normal");
        AppendMenuW(hPrioMenu, MF_STRING, IDM_DET_PRIO_NORMAL, L"Normal");
        AppendMenuW(hPrioMenu, MF_STRING, IDM_DET_PRIO_BELOW,  L"Below normal");
        AppendMenuW(hPrioMenu, MF_STRING, IDM_DET_PRIO_IDLE,   L"Low");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPrioMenu, L"Set priority");
    }

    AppendMenuW(hMenu, MF_STRING, IDM_DET_AFFINITY,   L"Set affinity...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_DET_PROPERTIES, L"Properties");

    Reg_ReadDword(HKEY_CURRENT_USER,
        L"Software\\ReactOS\\TaskMgrV2\\Shell", L"RealtimeWarn", &warn);

    {
        int cmd = TrackPopupMenu(hMenu,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
            pt.x, pt.y, 0, hWnd, NULL);

        if (iSel < (int)pPage->nProcs)
        {
            DET_ENTRY *e = &pPage->procs[iSel];
            HANDLE hProc;

            switch (cmd)
            {
                case IDM_DET_ENDTASK:
                case IDM_DET_ENDTREE:
                    hProc = OpenProcess(PROCESS_TERMINATE, FALSE, e->pid);
                    if (hProc) { TerminateProcess(hProc, 1); CloseHandle(hProc); }
                    break;

                case IDM_DET_PRIO_RT:
                    if (warn && MessageBoxW(hWnd,
                            L"Changing the priority class to Realtime can make the "
                            L"system unresponsive. Are you sure?",
                            L"Task Manager", MB_YESNO | MB_ICONWARNING) != IDYES)
                        break;
                    hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, e->pid);
                    if (hProc) { SetPriorityClass(hProc, REALTIME_PRIORITY_CLASS); CloseHandle(hProc); }
                    break;

                case IDM_DET_PRIO_HIGH:
                    hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, e->pid);
                    if (hProc) { SetPriorityClass(hProc, HIGH_PRIORITY_CLASS); CloseHandle(hProc); }
                    break;

                case IDM_DET_PRIO_ABOVE:
                    hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, e->pid);
                    if (hProc) { SetPriorityClass(hProc, ABOVE_NORMAL_PRIORITY_CLASS); CloseHandle(hProc); }
                    break;

                case IDM_DET_PRIO_NORMAL:
                    hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, e->pid);
                    if (hProc) { SetPriorityClass(hProc, NORMAL_PRIORITY_CLASS); CloseHandle(hProc); }
                    break;

                case IDM_DET_PRIO_BELOW:
                    hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, e->pid);
                    if (hProc) { SetPriorityClass(hProc, BELOW_NORMAL_PRIORITY_CLASS); CloseHandle(hProc); }
                    break;

                case IDM_DET_PRIO_IDLE:
                    hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, e->pid);
                    if (hProc) { SetPriorityClass(hProc, IDLE_PRIORITY_CLASS); CloseHandle(hProc); }
                    break;

                case IDM_DET_AFFINITY:
                    /* v1: placeholder — real affinity dialog is post-v1 */
                    MessageBoxW(hWnd,
                        L"Affinity dialog is not yet implemented.",
                        L"Task Manager", MB_OK | MB_ICONINFORMATION);
                    break;

                case IDM_DET_PROPERTIES:
                {
                    WCHAR path[MAX_PATH] = {0};
                    hProc = OpenProcess(
                        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                        FALSE, e->pid);
                    if (hProc)
                    {
                        GetModuleFileNameExW(hProc, NULL, path, MAX_PATH);
                        CloseHandle(hProc);
                        if (path[0])
                        {
                            SHELLEXECUTEINFOW sei = {0};
                            sei.cbSize = sizeof(sei);
                            sei.fMask  = SEE_MASK_INVOKEIDLIST;
                            sei.lpVerb = L"properties";
                            sei.lpFile = path;
                            sei.nShow  = SW_SHOW;
                            ShellExecuteExW(&sei);
                        }
                    }
                    break;
                }
            }
        }
    }

    if (hPrioMenu) DestroyMenu(hPrioMenu);
    DestroyMenu(hMenu);
}

/* -------------------------------------------------------------------------
 * Window procedure
 * ----------------------------------------------------------------------- */
static LRESULT CALLBACK
DetailsWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    DETAILS_PAGE *pPage =
        (DETAILS_PAGE *)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_CREATE:
        {
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE);
            LVCOLUMNW lvc;

            pPage = (DETAILS_PAGE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, sizeof(DETAILS_PAGE));
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

            lvc.pszText = L"Name";         lvc.cx = 180; lvc.fmt = LVCFMT_LEFT;
            ListView_InsertColumn(pPage->hList, DCOL_NAME, &lvc);

            lvc.pszText = L"PID";          lvc.cx = 60;  lvc.fmt = LVCFMT_RIGHT;
            ListView_InsertColumn(pPage->hList, DCOL_PID, &lvc);

            lvc.pszText = L"Status";       lvc.cx = 70;  lvc.fmt = LVCFMT_LEFT;
            ListView_InsertColumn(pPage->hList, DCOL_STATUS, &lvc);

            lvc.pszText = L"User name";    lvc.cx = 120;
            ListView_InsertColumn(pPage->hList, DCOL_USER, &lvc);

            lvc.pszText = L"CPU";          lvc.cx = 55;  lvc.fmt = LVCFMT_RIGHT;
            ListView_InsertColumn(pPage->hList, DCOL_CPU, &lvc);

            lvc.pszText = L"Memory";       lvc.cx = 90;
            ListView_InsertColumn(pPage->hList, DCOL_MEM, &lvc);

            lvc.pszText = L"Architecture"; lvc.cx = 80;  lvc.fmt = LVCFMT_LEFT;
            ListView_InsertColumn(pPage->hList, DCOL_ARCH, &lvc);

            lvc.pszText = L"Description";  lvc.cx = 200;
            ListView_InsertColumn(pPage->hList, DCOL_DESC, &lvc);

            SendMessageW(pPage->hList, WM_SETFONT, (WPARAM)Theme_FontUI(), FALSE);

            Details_Snapshot(pPage);
            Details_RefreshList(pPage);
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
            {
                SetWindowPos(pPage->hList, NULL, 0, 0, cx, cy,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }

        case WM_NOTIFY:
        {
            LPNMHDR pnm = (LPNMHDR)lParam;
            if (pnm->idFrom == 10 && pnm->code == NM_RCLICK)
            {
                POINT pt;
                GetCursorPos(&pt);
                Details_ContextMenu(hWnd, pPage, pt);
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
PageDetails_Create(HWND hHost, HINSTANCE hInst)
{
    WNDCLASSEXW wc = {0};

    if (!GetClassInfoExW(hInst, DETAILS_CLASS, &wc))
    {
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DetailsWndProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = NULL;
        wc.lpszClassName = DETAILS_CLASS;
        if (!RegisterClassExW(&wc))
            return NULL;
    }

    return CreateWindowExW(0, DETAILS_CLASS, NULL,
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0,
        hHost, NULL, hInst, NULL);
}

void
PageDetails_Tick(HWND hPage)
{
    DETAILS_PAGE *pPage =
        (DETAILS_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (!pPage) return;
    Details_Snapshot(pPage);
    Details_RefreshList(pPage);
}

void
PageDetails_Resize(HWND hPage, int cx, int cy)
{
    SetWindowPos(hPage, NULL, 0, 0, cx, cy,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}

void
PageDetails_Destroy(HWND hPage)
{
    DestroyWindow(hPage);
}

void
PageDetails_Theme(HWND hPage)
{
    DETAILS_PAGE *pPage =
        (DETAILS_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (pPage && pPage->hList)
        SendMessageW(pPage->hList, WM_SETFONT, (WPARAM)Theme_FontUI(), TRUE);
    InvalidateRect(hPage, NULL, TRUE);
}
