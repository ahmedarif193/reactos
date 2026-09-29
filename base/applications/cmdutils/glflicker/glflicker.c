/*
 * PROJECT:     LiberNT GL presentation tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Drive window changes over a running GL client to expose presentation flicker
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#define GEARS_X 300
#define GEARS_Y 200
#define GEARS_SIZE 400

static HWND Gears;
static HWND Cover;
static HWND Other;

static VOID
Mark(const char *Format, ...)
{
    CHAR Buffer[256];
    va_list Arguments;
    int Length;

    Length = _snprintf(Buffer, sizeof(Buffer) - 2, "GLFLICKER_");
    va_start(Arguments, Format);
    _vsnprintf(Buffer + Length, sizeof(Buffer) - Length - 2, Format, Arguments);
    va_end(Arguments);
    Buffer[sizeof(Buffer) - 2] = '\0';
    strcat(Buffer, "\n");
    OutputDebugStringA(Buffer);
}

static VOID
Step(const char *Name)
{
    Mark("STEP name=%s tick=%lu", Name, GetTickCount());
}

static VOID
Pump(DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    MSG Message;

    do
    {
        while (PeekMessageA(&Message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageA(&Message);
        }
        Sleep(5);
    } while (GetTickCount() - Start < Milliseconds);
}

static LRESULT CALLBACK
WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    return DefWindowProcA(Window, Message, WParam, LParam);
}

static HWND
CreatePlainWindow(DWORD ExStyle, DWORD Style, int X, int Y, int Width, int Height)
{
    return CreateWindowExA(ExStyle, "GlFlickerWindow", "glflicker", Style,
                           X, Y, Width, Height, NULL, NULL,
                           GetModuleHandleA(NULL), NULL);
}

int
main(int argc, char **argv)
{
    CHAR Directory[MAX_PATH];
    CHAR CommandLine[MAX_PATH * 2];
    STARTUPINFOA StartupInfo;
    PROCESS_INFORMATION Process;
    WNDCLASSA Class;
    RECT Rect;
    DWORD Start;
    int CenterX, CenterY, Index;

    if (argc > 2)
    {
        printf("usage: glflicker [gl-client-path]\n");
        return 1;
    }
    if (argc == 2)
    {
        _snprintf(CommandLine, sizeof(CommandLine), "\"%s\" -geometry %dx%d+%d+%d",
                  argv[1], GEARS_SIZE, GEARS_SIZE, GEARS_X, GEARS_Y);
    }
    else
    {
        GetSystemDirectoryA(Directory, sizeof(Directory));
        _snprintf(CommandLine, sizeof(CommandLine), "\"%s\\glgears.exe\" -geometry %dx%d+%d+%d",
                  Directory, GEARS_SIZE, GEARS_SIZE, GEARS_X, GEARS_Y);
    }
    CommandLine[sizeof(CommandLine) - 1] = '\0';

    ZeroMemory(&Class, sizeof(Class));
    Class.lpfnWndProc = WindowProc;
    Class.hInstance = GetModuleHandleA(NULL);
    Class.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    Class.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    Class.lpszClassName = "GlFlickerWindow";
    RegisterClassA(&Class);

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    Mark("BEGIN client=%s", CommandLine);
    if (!CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &Process))
    {
        Mark("ERROR create_process=%lu", GetLastError());
        return 1;
    }
    CloseHandle(Process.hThread);

    Start = GetTickCount();
    while (!(Gears = FindWindowA("wglgears", NULL)) && GetTickCount() - Start < 20000)
        Sleep(50);
    if (!Gears)
    {
        Mark("ERROR no_window");
        TerminateProcess(Process.hProcess, 1);
        return 1;
    }
    GetWindowRect(Gears, &Rect);
    CenterX = (Rect.left + Rect.right) / 2;
    CenterY = (Rect.top + Rect.bottom) / 2;
    Mark("WINDOW left=%ld top=%ld right=%ld bottom=%ld", Rect.left, Rect.top, Rect.right, Rect.bottom);
    SetCursorPos(1800, 900);
    Pump(3000);

    Step("idle");
    Pump(3000);

    Step("cursor_over");
    Start = GetTickCount();
    for (Index = 0; GetTickCount() - Start < 3000; Index++)
    {
        SetCursorPos(CenterX + (int)(80.0 * cos(Index * 0.1)), CenterY + (int)(80.0 * sin(Index * 0.1)));
        Pump(16);
    }

    Step("cursor_border");
    Start = GetTickCount();
    for (Index = 0; GetTickCount() - Start < 4000; Index++)
    {
        SetCursorPos((Index & 1) ? Rect.right - 3 : Rect.right - 40, CenterY);
        Pump(100);
    }

    Step("cursor_away");
    SetCursorPos(1800, 900);
    Pump(2000);

    Step("overlap_on");
    Cover = CreatePlainWindow(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, WS_POPUP | WS_BORDER,
                              CenterX, CenterY, 180, 150);
    ShowWindow(Cover, SW_SHOWNOACTIVATE);
    Pump(2000);
    Step("overlap_off");
    DestroyWindow(Cover);
    Pump(2000);

    Step("move");
    for (Index = 0; Index < 60; Index++)
    {
        SetWindowPos(Gears, NULL, Rect.left + Index * 4, Rect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        Pump(16);
    }
    SetWindowPos(Gears, NULL, Rect.left, Rect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    Pump(2000);

    Step("focus_other");
    Other = CreatePlainWindow(0, WS_OVERLAPPEDWINDOW, 1100, 200, 400, 300);
    ShowWindow(Other, SW_SHOW);
    SetForegroundWindow(Other);
    Pump(2000);
    Step("focus_back");
    SetForegroundWindow(Gears);
    Pump(2000);
    DestroyWindow(Other);
    Pump(1000);

    Step("minimize");
    ShowWindow(Gears, SW_MINIMIZE);
    Pump(1500);
    Step("restore");
    ShowWindow(Gears, SW_RESTORE);
    Pump(2500);

    Step("resize");
    SetWindowPos(Gears, NULL, 0, 0, Rect.right - Rect.left + 120, Rect.bottom - Rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    Pump(2000);
    SetWindowPos(Gears, NULL, 0, 0, Rect.right - Rect.left, Rect.bottom - Rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    Pump(2000);

    Step("close");
    PostMessageA(Gears, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(Process.hProcess, 5000) != WAIT_OBJECT_0)
        TerminateProcess(Process.hProcess, 1);
    CloseHandle(Process.hProcess);
    Mark("END tick=%lu", GetTickCount());
    return 0;
}
