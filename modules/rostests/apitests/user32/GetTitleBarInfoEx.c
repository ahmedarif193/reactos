/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Tests WM_GETTITLEBARINFOEX pointer marshalling and geometry
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"

typedef struct _TEST_TITLEBARINFOEX
{
    DWORD cbSize;
    RECT rcTitleBar;
    DWORD rgstate[CCHILDREN_TITLEBAR + 1];
    RECT rgrect[CCHILDREN_TITLEBAR + 1];
} TEST_TITLEBARINFOEX;

static const char TestClassName[] = "GetTitleBarInfoExTestClass";

static void
MakeWindowTitle(
    _Out_writes_(Length) char *Buffer,
    _In_ SIZE_T Length,
    _In_ DWORD ParentProcessId)
{
    _snprintf(Buffer, Length, "GetTitleBarInfoEx-%lu", ParentProcessId);
    Buffer[Length - 1] = '\0';
}

static LRESULT CALLBACK
TitleBarWindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, message, wParam, lParam);
}

static void
RunChildProcess(
    _In_ DWORD ParentProcessId)
{
    WNDCLASSA WindowClass;
    char WindowTitle[64];
    MSG Message;
    HWND hwnd;

    ZeroMemory(&WindowClass, sizeof(WindowClass));
    WindowClass.lpfnWndProc = TitleBarWindowProc;
    WindowClass.hInstance = GetModuleHandleA(NULL);
    WindowClass.lpszClassName = TestClassName;
    if (!RegisterClassA(&WindowClass)) ExitProcess(2);

    MakeWindowTitle(WindowTitle, _countof(WindowTitle), ParentProcessId);
    hwnd = CreateWindowExA(WS_EX_CONTEXTHELP, TestClassName, WindowTitle, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 100, 100, 320, 200, NULL, NULL, WindowClass.hInstance, NULL);
    if (hwnd == NULL) ExitProcess(3);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    while (GetMessageA(&Message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Message);
        DispatchMessageA(&Message);
    }
}

static void
TestCrossProcessTitleBarInfo(void)
{
    TEST_TITLEBARINFOEX Info;
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOA StartupInfo;
    char Executable[MAX_PATH];
    char CommandLine[MAX_PATH + 128];
    char WindowTitle[64];
    DWORD_PTR MessageResult;
    DWORD ExitCode;
    DWORD WaitResult;
    DWORD Error;
    LRESULT SendResult;
    BOOL Result;
    HWND hwnd;
    UINT Index;

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));
    StartupInfo.cb = sizeof(StartupInfo);

    if (!GetModuleFileNameA(NULL, Executable, _countof(Executable)))
    {
        skip("GetModuleFileNameA failed, error %lu\n", GetLastError());
        return;
    }

    MakeWindowTitle(WindowTitle, _countof(WindowTitle), GetCurrentProcessId());
    sprintf(CommandLine, "\"%s\" GetTitleBarInfoEx child %lu", Executable, GetCurrentProcessId());
    Result = CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo);
    ok(Result, "CreateProcessA failed, error %lu\n", GetLastError());
    if (!Result) return;

    hwnd = NULL;
    for (Index = 0; Index < 500 && hwnd == NULL; ++Index)
    {
        hwnd = FindWindowA(TestClassName, WindowTitle);
        if (hwnd == NULL && WaitForSingleObject(ProcessInfo.hProcess, 0) == WAIT_OBJECT_0) break;
        if (hwnd == NULL) Sleep(10);
    }

    ok(hwnd != NULL, "The child window was not created\n");
    if (hwnd != NULL)
    {
        memset(&Info, 0xcc, sizeof(Info));
        Info.cbSize = sizeof(Info);
        MessageResult = 0;
        SetLastError(0xdeadbeef);
        SendResult = SendMessageTimeoutA(hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&Info, SMTO_ABORTIFHUNG, 5000, &MessageResult);
        ok(SendResult != 0, "SendMessageTimeoutA failed, error %lu\n", GetLastError());
        ok(MessageResult != 0, "WM_GETTITLEBARINFOEX returned %Iu\n", MessageResult);
        if (SendResult != 0 && MessageResult != 0)
        {
            ok(Info.cbSize == sizeof(Info), "cbSize changed to %lu\n", Info.cbSize);
            ok(Info.rcTitleBar.right > Info.rcTitleBar.left && Info.rcTitleBar.bottom > Info.rcTitleBar.top, "The title-bar rectangle is empty: %ld,%ld-%ld,%ld\n", Info.rcTitleBar.left, Info.rcTitleBar.top, Info.rcTitleBar.right, Info.rcTitleBar.bottom);
            ok(!(Info.rgstate[4] & STATE_SYSTEM_INVISIBLE), "The context-help button is marked invisible, state %#lx\n", Info.rgstate[4]);
            ok(Info.rgrect[4].right > Info.rgrect[4].left && Info.rgrect[4].bottom > Info.rgrect[4].top, "The context-help rectangle is empty: %ld,%ld-%ld,%ld\n", Info.rgrect[4].left, Info.rgrect[4].top, Info.rgrect[4].right, Info.rgrect[4].bottom);
            ok(Info.rgrect[5].right > Info.rgrect[5].left && Info.rgrect[5].bottom > Info.rgrect[5].top, "The close-button rectangle is empty: %ld,%ld-%ld,%ld\n", Info.rgrect[5].left, Info.rgrect[5].top, Info.rgrect[5].right, Info.rgrect[5].bottom);
            ok(Info.rgrect[4].right <= Info.rgrect[5].left, "The context-help rectangle does not precede the close button\n");
        }

        memset(&Info, 0, sizeof(Info));
        Info.cbSize = sizeof(Info);
        SetLastError(0xdeadbeef);
        Result = PostMessageA(hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&Info);
        Error = GetLastError();
        ok(!Result, "PostMessageA unexpectedly accepted a pointer-bearing message\n");
        ok(Error == ERROR_MESSAGE_SYNC_ONLY, "PostMessageA returned error %lu, expected ERROR_MESSAGE_SYNC_ONLY\n", Error);

        SetLastError(0xdeadbeef);
        Result = SendNotifyMessageA(hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&Info);
        Error = GetLastError();
        ok(!Result, "SendNotifyMessageA unexpectedly accepted a pointer-bearing message\n");
        ok(Error == ERROR_MESSAGE_SYNC_ONLY, "SendNotifyMessageA returned error %lu, expected ERROR_MESSAGE_SYNC_ONLY\n", Error);

        PostMessageA(hwnd, WM_CLOSE, 0, 0);
    }

    WaitResult = WaitForSingleObject(ProcessInfo.hProcess, 5000);
    if (WaitResult == WAIT_TIMEOUT)
    {
        TerminateProcess(ProcessInfo.hProcess, 4);
        WaitForSingleObject(ProcessInfo.hProcess, 5000);
    }
    ok(WaitResult == WAIT_OBJECT_0, "The child process did not exit normally, wait result %#lx\n", WaitResult);
    if (GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode)) ok(ExitCode == 0, "The child process exited with code %lu\n", ExitCode);
    else ok(FALSE, "GetExitCodeProcess failed, error %lu\n", GetLastError());
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
}

START_TEST(GetTitleBarInfoEx)
{
    char **Arguments;
    int ArgumentCount;

    ArgumentCount = winetest_get_mainargs(&Arguments);
    if (ArgumentCount >= 4 && !strcmp(Arguments[2], "child"))
    {
        RunChildProcess(strtoul(Arguments[3], NULL, 10));
        return;
    }

    TestCrossProcessTitleBarInfo();
}
