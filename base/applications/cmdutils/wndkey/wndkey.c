#include <windows.h>
#include <stdlib.h>
#include <wchar.h>

static void ClickCenter(HWND hWnd)
{
    RECT rc;
    INPUT in[3];
    LONG sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);

    if (!GetClientRect(hWnd, &rc)) return;
    MapWindowPoints(hWnd, NULL, (LPPOINT)&rc, 2);
    ZeroMemory(in, sizeof(in));
    in[0].type = INPUT_MOUSE;
    in[0].mi.dx = ((rc.left + rc.right) / 2) * 65535 / (sw - 1);
    in[0].mi.dy = ((rc.top + rc.bottom) / 2) * 65535 / (sh - 1);
    in[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    in[1] = in[0];
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE;
    in[2] = in[0];
    in[2].mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE;
    SendInput(3, in, sizeof(INPUT));
}

static void PressKey(WORD vk)
{
    INPUT in[2];

    ZeroMemory(in, sizeof(in));
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[0].ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

int wmain(int argc, wchar_t **argv)
{
    LPCWSTR title = NULL;
    WORD vk = 0;
    DWORD every = 2000, duration = 300000, settle = 3000, start;
    BOOL click = FALSE, activated = FALSE;
    HWND hWnd;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!wcscmp(argv[i], L"-click")) click = TRUE;
        else if (!wcscmp(argv[i], L"-key") && i + 1 < argc) vk = (WORD)wcstoul(argv[++i], NULL, 0);
        else if (!wcscmp(argv[i], L"-every") && i + 1 < argc) every = wcstoul(argv[++i], NULL, 0);
        else if (!wcscmp(argv[i], L"-for") && i + 1 < argc) duration = wcstoul(argv[++i], NULL, 0);
        else if (!wcscmp(argv[i], L"-settle") && i + 1 < argc) settle = wcstoul(argv[++i], NULL, 0);
        else title = argv[i];
    }
    if (!title)
    {
        wprintf(L"usage: wndkey <title> [-click] [-key VK] [-every ms] [-for ms] [-settle ms]\n");
        return 1;
    }

    start = GetTickCount();
    while (GetTickCount() - start < duration)
    {
        hWnd = FindWindowW(NULL, title);
        if (hWnd && IsWindowVisible(hWnd))
        {
            if (!activated)
            {
                Sleep(settle);
                if (click) ClickCenter(hWnd);
                SetForegroundWindow(hWnd);
                activated = TRUE;
            }
            else if (GetForegroundWindow() == hWnd && vk)
            {
                PressKey(vk);
            }
        }
        Sleep(every);
    }
    return 0;
}
