/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Tests for Shell_NotifyIconGetRect
 */

#include <apitest.h>
#include <windows.h>
#include <shellapi.h>

typedef HRESULT (WINAPI *SHELL_NOTIFYICONGETRECT)(const NOTIFYICONIDENTIFIER*, RECT*);

static const RECT g_SentinelRect = { 0x11111111, 0x22222222, 0x33333333, 0x44444444 };

static void
CheckRect(const RECT& Rect, LONG Left, LONG Top, LONG Right, LONG Bottom)
{
    ok(Rect.left == Left && Rect.top == Top && Rect.right == Right && Rect.bottom == Bottom,
       "rect is (%ld,%ld)-(%ld,%ld), expected (%ld,%ld)-(%ld,%ld)\n",
       Rect.left, Rect.top, Rect.right, Rect.bottom, Left, Top, Right, Bottom);
}

static void
CheckQueryResult(SHELL_NOTIFYICONGETRECT GetRect, const NOTIFYICONIDENTIFIER& Identifier,
                 HRESULT Expected)
{
    RECT Rect = g_SentinelRect;
    HRESULT Result = GetRect(&Identifier, &Rect);
    ok_hex(Result, Expected);
    CheckRect(Rect, 0, 0, 0, 0);
}

START_TEST(Shell_NotifyIconGetRect)
{
    HMODULE Shell32 = GetModuleHandleW(L"shell32.dll");
    SHELL_NOTIFYICONGETRECT GetRect;
    NOTIFYICONIDENTIFIER Identifier = { 0 };
    NOTIFYICONDATAW Icon = { 0 };
    RECT Rect;
    HRESULT Result;
    HWND Window;
    BOOL Added;

    GetRect = reinterpret_cast<SHELL_NOTIFYICONGETRECT>(GetProcAddress(Shell32, "Shell_NotifyIconGetRect"));
    if (!GetRect)
    {
        skip("Shell_NotifyIconGetRect is not exported\n");
        return;
    }

    Rect = g_SentinelRect;
    Result = GetRect(NULL, &Rect);
    ok_hex(Result, E_POINTER);
    CheckRect(Rect, g_SentinelRect.left, g_SentinelRect.top,
              g_SentinelRect.right, g_SentinelRect.bottom);

    Identifier.cbSize = sizeof(Identifier);
    Result = GetRect(&Identifier, NULL);
    ok_hex(Result, E_POINTER);

    Identifier.cbSize = 0;
    CheckQueryResult(GetRect, Identifier, E_INVALIDARG);
    Identifier.cbSize = sizeof(Identifier) - 1;
    CheckQueryResult(GetRect, Identifier, E_INVALIDARG);
    Identifier.cbSize = sizeof(Identifier) + 1;
    CheckQueryResult(GetRect, Identifier, E_INVALIDARG);

    Identifier.cbSize = sizeof(Identifier);
    CheckQueryResult(GetRect, Identifier, E_FAIL);

    Window = CreateWindowExW(0, L"STATIC", L"Shell_NotifyIconGetRect test", WS_OVERLAPPED,
                             0, 0, 100, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(Window != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (!Window)
        return;

    Icon.cbSize = sizeof(Icon);
    Icon.hWnd = Window;
    Icon.uID = 42;
    Icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    Icon.uCallbackMessage = WM_APP + 1;
    Icon.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    lstrcpyW(Icon.szTip, L"Shell_NotifyIconGetRect test");
    Added = Shell_NotifyIconW(NIM_ADD, &Icon);
    ok(Added, "NIM_ADD failed with %lu\n", GetLastError());

    if (Added)
    {
        Sleep(250);
        Identifier.hWnd = Window;
        Identifier.uID = Icon.uID;
        Rect = g_SentinelRect;
        Result = GetRect(&Identifier, &Rect);
        ok(Result == S_OK || Result == E_FAIL, "unexpected result 0x%08lx\n", Result);
        if (Result == S_OK)
        {
            ok(Rect.right > Rect.left && Rect.bottom > Rect.top,
               "invalid successful rect (%ld,%ld)-(%ld,%ld)\n",
               Rect.left, Rect.top, Rect.right, Rect.bottom);
        }
        else
        {
            CheckRect(Rect, 0, 0, 0, 0);
        }

        Identifier.uID++;
        CheckQueryResult(GetRect, Identifier, E_FAIL);
        Identifier.uID--;

        Icon.uFlags = NIF_STATE;
        Icon.dwStateMask = NIS_HIDDEN;
        Icon.dwState = NIS_HIDDEN;
        ok(Shell_NotifyIconW(NIM_MODIFY, &Icon), "hiding icon failed with %lu\n", GetLastError());
        Sleep(100);
        CheckQueryResult(GetRect, Identifier, E_FAIL);

        ok(Shell_NotifyIconW(NIM_DELETE, &Icon), "NIM_DELETE failed with %lu\n", GetLastError());
        CheckQueryResult(GetRect, Identifier, E_FAIL);
    }

    DestroyWindow(Window);
}
