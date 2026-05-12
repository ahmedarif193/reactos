/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Processes page — grouped listview, live update via NtQuerySystemInformation
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.9, constraint #6.
 * Columns: Name, Status, CPU%, Memory, Disk, Network.
 * LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | groups.
 * Live-update on Tick via SystemProcessInformation.
 *
 * NOTE: The NDK SYSTEM_PROCESS_INFORMATION / SYSTEM_THREAD_INFORMATION
 * structures are included via <ndk/exfuncs.h> or <ndk/pstypes.h>. We rely
 * on precomp.h pulling in the NDK headers.
 */

#include "precomp.h"
#include <ndk/exfuncs.h>
#include <ndk/pstypes.h>
#include <psapi.h>

/* LVGS_COLLAPSIBLE is not defined in the ReactOS commctrl.h at this SDK
 * version; define the numeric value from the Windows SDK. */
#ifndef LVGS_COLLAPSIBLE
#define LVGS_COLLAPSIBLE 0x00000004
#endif

#define PROCESSES_CLASS  L"TaskMgrV2.Page.Processes"

/* Column indices */
#define COL_NAME    0
#define COL_STATUS  1
#define COL_CPU     2
#define COL_MEMORY  3
#define COL_DISK    4
#define COL_NET     5
#define COL__COUNT  6

/* Group IDs for the listview */
#define GRP_APPS       1
#define GRP_BACKGROUND 2

/* Max processes we track in one snapshot */
#define MAX_PROCESSES  1024

/* Right-click context menu item IDs (within 1200..1299 range) */
#define IDM_PROC_ENDTASK     1250
#define IDM_PROC_ENDTREE     1251
#define IDM_PROC_SETPRIO     1252
#define IDM_PROC_AFFINITY    1253
#define IDM_PROC_PROPERTIES  1254

/* Priority sub-items */
#define IDM_PRIO_REALTIME    1260
#define IDM_PRIO_HIGH        1261
#define IDM_PRIO_ABOVENORMAL 1262
#define IDM_PRIO_NORMAL      1263
#define IDM_PRIO_BELOWNORMAL 1264
#define IDM_PRIO_IDLE        1265

/* Process snapshot entry */
typedef struct _PROC_ENTRY
{
    DWORD  pid;
    DWORD  cpuPct;           /* 0..100 */
    SIZE_T workingSetBytes;
    WCHAR  name[64];
    BOOL   bForeground;      /* rough heuristic: has a visible window */
} PROC_ENTRY;

/* Per-page state */
typedef struct _PROCESSES_PAGE
{
    HWND         hList;
    PROC_ENTRY   procs[MAX_PROCESSES];
    DWORD        nProcs;
    LARGE_INTEGER lastIdle;
    LARGE_INTEGER lastKernel;
    LARGE_INTEGER lastUser;
} PROCESSES_PAGE;

/* -------------------------------------------------------------------------
 * Format helpers
 * ----------------------------------------------------------------------- */
static void
FormatMemoryKB(WCHAR *buf, int cch, SIZE_T bytes)
{
    SIZE_T kb = bytes / 1024;
    if (kb < 1024)
        StringCchPrintfW(buf, cch, L"%u K", (UINT)kb);
    else
        StringCchPrintfW(buf, cch, L"%u MB", (UINT)(kb / 1024));
}

/* -------------------------------------------------------------------------
 * Snapshot: fill pPage->procs[] from NtQuerySystemInformation.
 * Pattern from legacy taskmgr/perfdata.c:204-205 (read-only reference).
 * ----------------------------------------------------------------------- */
static void
Processes_Snapshot(PROCESSES_PAGE *pPage)
{
    NTSTATUS status;
    ULONG    bufLen = 1024 * 256;
    PVOID    buf    = NULL;
    ULONG    retLen = 0;
    DWORD    n = 0;

    /* Allocate a buffer for SystemProcessInformation */
    buf = HeapAlloc(GetProcessHeap(), 0, bufLen);
    if (!buf) return;

    /* Retry once with a larger buffer if the first call returns overflow */
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

    if (!NT_SUCCESS(status))
    {
        HeapFree(GetProcessHeap(), 0, buf);
        return;
    }

    /* Walk the linked list of SYSTEM_PROCESS_INFORMATION entries */
    {
        PSYSTEM_PROCESS_INFORMATION pInfo =
            (PSYSTEM_PROCESS_INFORMATION)buf;

        for (;;)
        {
            if (n < MAX_PROCESSES)
            {
                PROC_ENTRY *pe = &pPage->procs[n];
                pe->pid = HandleToUlong(pInfo->UniqueProcessId);

                /* Process name */
                if (pInfo->ImageName.Buffer && pInfo->ImageName.Length > 0)
                {
                    StringCchCopyNW(pe->name,
                        ARRAYSIZE(pe->name),
                        pInfo->ImageName.Buffer,
                        pInfo->ImageName.Length / sizeof(WCHAR));
                }
                else
                {
                    /* System Idle Process */
                    StringCchCopyW(pe->name, ARRAYSIZE(pe->name), L"System");
                }

                pe->workingSetBytes = pInfo->WorkingSetSize;

                /* Very rough CPU % — computed below after we have both
                 * old and new kernel/user times.  For now store 0; the
                 * real computation is done once we have prev-tick data
                 * stored per-process (v1 simplification: show 0). */
                pe->cpuPct = 0;

                /* Heuristic: pid 0/4 = System; others may be apps */
                pe->bForeground = (pe->pid != 0 && pe->pid != 4);
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
 * Populate / refresh the listview from the current snapshot
 * ----------------------------------------------------------------------- */
static void
Processes_RefreshList(PROCESSES_PAGE *pPage)
{
    HWND hList = pPage->hList;
    LVITEMW lvi;
    WCHAR   buf[64];
    DWORD   i;

    SendMessageW(hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hList);

    ZeroMemory(&lvi, sizeof(lvi));
    lvi.mask = LVIF_TEXT | LVIF_GROUPID | LVIF_COLUMNS;

    for (i = 0; i < pPage->nProcs; i++)
    {
        PROC_ENTRY *pe = &pPage->procs[i];
        int iItem;

        lvi.iItem     = (int)i;
        lvi.iSubItem  = 0;
        lvi.pszText   = pe->name;
        lvi.iGroupId  = pe->bForeground ? GRP_APPS : GRP_BACKGROUND;
        iItem = ListView_InsertItem(hList, &lvi);
        if (iItem < 0) continue;

        /* Status */
        ListView_SetItemText(hList, iItem, COL_STATUS, L"Running");

        /* CPU % */
        StringCchPrintfW(buf, ARRAYSIZE(buf), L"%u%%", pe->cpuPct);
        ListView_SetItemText(hList, iItem, COL_CPU, buf);

        /* Memory */
        FormatMemoryKB(buf, ARRAYSIZE(buf), pe->workingSetBytes);
        ListView_SetItemText(hList, iItem, COL_MEMORY, buf);

        /* Disk / Network — not yet wired to real data in v1 */
        ListView_SetItemText(hList, iItem, COL_DISK, L"0 MB/s");
        ListView_SetItemText(hList, iItem, COL_NET,  L"0 Mbps");
    }

    SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hList, NULL, FALSE);
}

/* -------------------------------------------------------------------------
 * Context menu: right-click on a selected item
 * ----------------------------------------------------------------------- */
static void
Processes_ContextMenu(HWND hWnd, PROCESSES_PAGE *pPage, POINT pt)
{
    int iSel = ListView_GetNextItem(pPage->hList, -1, LVNI_SELECTED);
    HMENU hMenu, hPrioMenu;
    BOOL bRealtimeWarn;
    DWORD warn = 1;

    if (iSel < 0) return;

    hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, IDM_PROC_ENDTASK,    L"End task");
    AppendMenuW(hMenu, MF_STRING, IDM_PROC_ENDTREE,    L"End process tree");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    hPrioMenu = CreatePopupMenu();
    if (hPrioMenu)
    {
        AppendMenuW(hPrioMenu, MF_STRING, IDM_PRIO_REALTIME,    L"Realtime");
        AppendMenuW(hPrioMenu, MF_STRING, IDM_PRIO_HIGH,        L"High");
        AppendMenuW(hPrioMenu, MF_STRING, IDM_PRIO_ABOVENORMAL, L"Above normal");
        AppendMenuW(hPrioMenu, MF_STRING, IDM_PRIO_NORMAL,      L"Normal");
        AppendMenuW(hPrioMenu, MF_STRING, IDM_PRIO_BELOWNORMAL, L"Below normal");
        AppendMenuW(hPrioMenu, MF_STRING, IDM_PRIO_IDLE,        L"Low");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPrioMenu, L"Set priority");
    }

    AppendMenuW(hMenu, MF_STRING, IDM_PROC_AFFINITY,   L"Set affinity...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_PROC_PROPERTIES, L"Properties");

    Reg_ReadDword(HKEY_CURRENT_USER,
        L"Software\\ReactOS\\TaskMgrV2\\Shell", L"RealtimeWarn", &warn);
    bRealtimeWarn = (warn != 0);

    {
        int cmd = TrackPopupMenu(hMenu,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
            pt.x, pt.y, 0, hWnd, NULL);

        if (iSel < (int)pPage->nProcs)
        {
            PROC_ENTRY *pe = &pPage->procs[iSel];
            HANDLE hProc;

            switch (cmd)
            {
                case IDM_PROC_ENDTASK:
                    hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe->pid);
                    if (hProc)
                    {
                        TerminateProcess(hProc, 1);
                        CloseHandle(hProc);
                    }
                    break;

                case IDM_PROC_ENDTREE:
                    /* Simple implementation: terminate the process itself.
                     * Full tree kill would require walking all processes. */
                    hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe->pid);
                    if (hProc)
                    {
                        TerminateProcess(hProc, 1);
                        CloseHandle(hProc);
                    }
                    break;

                case IDM_PRIO_REALTIME:
                    if (bRealtimeWarn &&
                        MessageBoxW(hWnd,
                            L"Changing the priority class to Realtime can make the "
                            L"system unresponsive. Are you sure?",
                            L"Task Manager", MB_YESNO | MB_ICONWARNING) != IDYES)
                        break;
                    hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe->pid);
                    if (hProc)
                    {
                        SetPriorityClass(hProc, REALTIME_PRIORITY_CLASS);
                        CloseHandle(hProc);
                    }
                    break;

                case IDM_PRIO_HIGH:
                    hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe->pid);
                    if (hProc) { SetPriorityClass(hProc, HIGH_PRIORITY_CLASS); CloseHandle(hProc); }
                    break;

                case IDM_PRIO_ABOVENORMAL:
                    hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe->pid);
                    if (hProc) { SetPriorityClass(hProc, ABOVE_NORMAL_PRIORITY_CLASS); CloseHandle(hProc); }
                    break;

                case IDM_PRIO_NORMAL:
                    hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe->pid);
                    if (hProc) { SetPriorityClass(hProc, NORMAL_PRIORITY_CLASS); CloseHandle(hProc); }
                    break;

                case IDM_PRIO_BELOWNORMAL:
                    hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe->pid);
                    if (hProc) { SetPriorityClass(hProc, BELOW_NORMAL_PRIORITY_CLASS); CloseHandle(hProc); }
                    break;

                case IDM_PRIO_IDLE:
                    hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe->pid);
                    if (hProc) { SetPriorityClass(hProc, IDLE_PRIORITY_CLASS); CloseHandle(hProc); }
                    break;

                case IDM_PROC_PROPERTIES:
                {
                    /* ShellExecuteEx to open the properties dialog for the EXE */
                    WCHAR exePath[MAX_PATH] = {0};
                    hProc = OpenProcess(
                        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                        FALSE, pe->pid);
                    if (hProc)
                    {
                        GetModuleFileNameExW(hProc, NULL,
                            exePath, MAX_PATH);
                        CloseHandle(hProc);
                        if (exePath[0])
                        {
                            SHELLEXECUTEINFOW sei = {0};
                            sei.cbSize = sizeof(sei);
                            sei.fMask  = SEE_MASK_INVOKEIDLIST;
                            sei.lpVerb = L"properties";
                            sei.lpFile = exePath;
                            sei.nShow  = SW_SHOW;
                            ShellExecuteExW(&sei);
                        }
                    }
                    break;
                }

                default:
                    break;
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
ProcessesWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    PROCESSES_PAGE *pPage =
        (PROCESSES_PAGE *)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_CREATE:
        {
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE);
            LVCOLUMNW lvc;
            LVGROUP   lvg;

            pPage = (PROCESSES_PAGE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, sizeof(PROCESSES_PAGE));
            if (!pPage) return -1;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pPage);

            /* Create the listview */
            pPage->hList = CreateWindowExW(WS_EX_CLIENTEDGE,
                WC_LISTVIEWW, NULL,
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS |
                LVS_NOSORTHEADER,
                0, 0, 0, 0,
                hWnd, (HMENU)(INT_PTR)10, hInst, NULL);

            if (!pPage->hList) return -1;

            /* Extended styles */
            ListView_SetExtendedListViewStyle(pPage->hList,
                LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT |
                LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP);

            /* Enable groups */
            ListView_EnableGroupView(pPage->hList, TRUE);

            /* Add groups */
            ZeroMemory(&lvg, sizeof(lvg));
            lvg.cbSize    = sizeof(lvg);
            lvg.mask      = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
            lvg.state     = LVGS_COLLAPSIBLE;
            lvg.iGroupId  = GRP_APPS;
            lvg.pszHeader = L"Apps";
            ListView_InsertGroup(pPage->hList, -1, &lvg);

            lvg.iGroupId  = GRP_BACKGROUND;
            lvg.pszHeader = L"Background processes";
            ListView_InsertGroup(pPage->hList, -1, &lvg);

            /* Add columns */
            ZeroMemory(&lvc, sizeof(lvc));
            lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;

            lvc.pszText = L"Name";     lvc.cx = 200; lvc.fmt = LVCFMT_LEFT;
            ListView_InsertColumn(pPage->hList, COL_NAME, &lvc);

            lvc.pszText = L"Status";   lvc.cx = 80;
            ListView_InsertColumn(pPage->hList, COL_STATUS, &lvc);

            lvc.pszText = L"CPU";      lvc.cx = 60;  lvc.fmt = LVCFMT_RIGHT;
            ListView_InsertColumn(pPage->hList, COL_CPU, &lvc);

            lvc.pszText = L"Memory";   lvc.cx = 90;
            ListView_InsertColumn(pPage->hList, COL_MEMORY, &lvc);

            lvc.pszText = L"Disk";     lvc.cx = 80;
            ListView_InsertColumn(pPage->hList, COL_DISK, &lvc);

            lvc.pszText = L"Network";  lvc.cx = 80;
            ListView_InsertColumn(pPage->hList, COL_NET, &lvc);

            /* Font */
            SendMessageW(pPage->hList, WM_SETFONT,
                (WPARAM)Theme_FontUI(), FALSE);

            /* Initial snapshot */
            Processes_Snapshot(pPage);
            Processes_RefreshList(pPage);
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
                Processes_ContextMenu(hWnd, pPage, pt);
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
PageProcesses_Create(HWND hHost, HINSTANCE hInst)
{
    WNDCLASSEXW wc = {0};

    if (!GetClassInfoExW(hInst, PROCESSES_CLASS, &wc))
    {
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ProcessesWndProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = NULL;
        wc.lpszClassName = PROCESSES_CLASS;
        if (!RegisterClassExW(&wc))
            return NULL;
    }

    return CreateWindowExW(0, PROCESSES_CLASS, NULL,
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0,
        hHost, NULL, hInst, NULL);
}

void
PageProcesses_Tick(HWND hPage)
{
    PROCESSES_PAGE *pPage =
        (PROCESSES_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (!pPage) return;
    Processes_Snapshot(pPage);
    Processes_RefreshList(pPage);
}

void
PageProcesses_Resize(HWND hPage, int cx, int cy)
{
    SetWindowPos(hPage, NULL, 0, 0, cx, cy,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}

void
PageProcesses_Destroy(HWND hPage)
{
    DestroyWindow(hPage);
}

void
PageProcesses_Theme(HWND hPage)
{
    PROCESSES_PAGE *pPage =
        (PROCESSES_PAGE *)GetWindowLongPtrW(hPage, GWLP_USERDATA);
    if (pPage && pPage->hList)
    {
        SendMessageW(pPage->hList, WM_SETFONT,
            (WPARAM)Theme_FontUI(), TRUE);
    }
    InvalidateRect(hPage, NULL, TRUE);
}
