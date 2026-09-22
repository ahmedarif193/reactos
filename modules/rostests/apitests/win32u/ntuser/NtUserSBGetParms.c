/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for NtUserSBGetParms
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../win32nt.h"

START_TEST(NtUserSBGetParms)
{
    SETSCROLLBARINFO SetInfo;
    SCROLLINFO Info;
    SBDATA Data;
    HWND hWnd;

    hWnd = CreateWindowExW(0, L"STATIC", L"", WS_POPUP | WS_VSCROLL, 0, 0, 100, 100,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hWnd) return;

    ZeroMemory(&SetInfo, sizeof(SetInfo));
    SetInfo.nTrackPos = 42;
    ok(NtUserSetScrollBarInfo(hWnd, OBJID_VSCROLL, &SetInfo), "NtUserSetScrollBarInfo failed\n");
    ok(!NtUserSetScrollBarInfo(hWnd, 12345, &SetInfo), "NtUserSetScrollBarInfo accepted a bad object\n");

    ZeroMemory(&Info, sizeof(Info));
    Data.posMin = 3;
    Data.posMax = 90;
    Data.page = 7;
    Data.pos = 11;
    Info.cbSize = sizeof(Info);
    Info.fMask = SIF_ALL;
    ok(NtUserSBGetParms(hWnd, SB_VERT, &Data, &Info), "NtUserSBGetParms failed\n");
    ok_int(Info.nMin, 3);
    ok_int(Info.nMax, 90);
    ok_int(Info.nPage, 7);
    ok_int(Info.nPos, 11);
    ok_int(Info.nTrackPos, 42);

    DestroyWindow(hWnd);
}
