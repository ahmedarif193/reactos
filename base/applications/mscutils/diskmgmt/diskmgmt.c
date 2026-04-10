/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Program entrypoint.
 */

#include "precomp.h"

HINSTANCE hInstance;
HANDLE ProcessHeap;

int WINAPI
wWinMain(HINSTANCE hThisInstance,
         HINSTANCE hPrevInstance,
         LPWSTR lpCmdLine,
         int nCmdShow)
{
    INITCOMMONCONTROLSEX icex;
    HWND hMainWnd;
    HACCEL hAccel;
    MSG Msg;
    WCHAR szAppName[64];
    int Ret = 1;

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    hInstance = hThisInstance;
    ProcessHeap = GetProcessHeap();

    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    if (LoadStringW(hInstance,
                    IDS_APPNAME,
                    szAppName,
                    ARRAYSIZE(szAppName)) == 0)
    {
        return Ret;
    }

    hAccel = LoadAcceleratorsW(hInstance,
                               MAKEINTRESOURCEW(IDA_DISKMGMT));

    if (!InitMainWindowImpl())
        return Ret;

    hMainWnd = CreateMainWindow(szAppName,
                                nCmdShow);
    if (hMainWnd != NULL)
    {
        while (GetMessageW(&Msg, NULL, 0, 0) > 0)
        {
            if (hAccel == NULL ||
                !TranslateAcceleratorW(hMainWnd, hAccel, &Msg))
            {
                TranslateMessage(&Msg);
                DispatchMessageW(&Msg);
            }
        }

        Ret = (int)Msg.wParam;
    }

    UninitMainWindowImpl();
    return Ret;
}
