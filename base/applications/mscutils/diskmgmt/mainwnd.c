/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main window implementation.
 */

#include "precomp.h"

static const WCHAR szMainWndClass[] = L"DiskMgmtWndClass";

BOOL
InitMainWindowImpl(VOID)
{
    WNDCLASSEXW wc;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DmMainWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_DISKMGMT));
    wc.hIconSm = (HICON)LoadImageW(hInstance,
                                   MAKEINTRESOURCEW(IDI_DISKMGMT),
                                   IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON),
                                   LR_SHARED);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDM_DISKMGMT);
    wc.lpszClassName = szMainWndClass;
    if (!RegisterClassExW(&wc))
        return FALSE;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    return DmMainWndRegisterDiskViewClass(hInstance);
}

VOID
UninitMainWindowImpl(VOID)
{
    DmMainWndUnregisterDiskViewClass(hInstance);
    UnregisterClassW(szMainWndClass, hInstance);
}

HWND
CreateMainWindow(
    _In_z_ LPCTSTR lpCaption,
    _In_ int nCmdShow)
{
    PMAIN_WND_INFO Info;
    HWND hwnd;

    Info = (PMAIN_WND_INFO)HeapAlloc(ProcessHeap,
                                     HEAP_ZERO_MEMORY,
                                     sizeof(*Info));
    if (Info == NULL)
        return NULL;

    Info->nCmdShow = nCmdShow;

    hwnd = CreateWindowExW(WS_EX_WINDOWEDGE,
                           szMainWndClass,
                           lpCaption,
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                           CW_USEDEFAULT,
                           CW_USEDEFAULT,
                           980,
                           680,
                           NULL,
                           NULL,
                           hInstance,
                           Info);
    if (hwnd == NULL)
    {
        HeapFree(ProcessHeap, 0, Info);
        return NULL;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    return hwnd;
}

BOOL
StatusBarLoadString(
    _In_ HWND hStatusBar,
    _In_ INT PartId,
    _In_ HINSTANCE hInst,
    _In_ UINT uID)
{
    WCHAR szText[128];

    if (LoadStringW(hInst, uID, szText, ARRAYSIZE(szText)) == 0)
        return FALSE;

    return (SendMessageW(hStatusBar,
                         SB_SETTEXTW,
                         (WPARAM)PartId,
                         (LPARAM)szText) != 0);
}

VOID
ResourceMessageBox(
    _In_opt_ HWND hWnd,
    _In_ UINT uType,
    _In_ UINT uTextId)
{
    WCHAR szText[256];

    if (LoadStringW(hInstance,
                    uTextId,
                    szText,
                    ARRAYSIZE(szText)) == 0)
    {
        szText[0] = UNICODE_NULL;
    }

    MessageBoxW(hWnd,
                szText,
                L"Disk Management",
                uType);
}
