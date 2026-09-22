/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests sending messages that carry buffers larger than a stack page
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define LARGE_CHARS 50000

static HANDLE ReadyEvent;
static HWND ReceiverWindow;
static LONG SetTextLength;
static BOOL SetTextMatches;

static LRESULT CALLBACK
ReceiverProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    SIZE_T i, Count;

    switch (Msg)
    {
        case WM_SETTEXT:
        {
            PCWSTR Text = (PCWSTR)lParam;

            Count = wcslen(Text);
            SetTextLength = (LONG)Count;
            SetTextMatches = TRUE;
            for (i = 0; i < Count; i++)
            {
                if (Text[i] != (WCHAR)(L'A' + (i % 26)))
                {
                    SetTextMatches = FALSE;
                    break;
                }
            }
            return TRUE;
        }

        case WM_GETTEXT:
        {
            PWSTR Buffer = (PWSTR)lParam;

            if (!wParam)
                return 0;
            Count = wParam - 1;
            for (i = 0; i < Count; i++)
                Buffer[i] = (WCHAR)(L'a' + (i % 26));
            Buffer[Count] = UNICODE_NULL;
            return Count;
        }
    }

    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static DWORD WINAPI
ReceiverThread(PVOID Parameter)
{
    MSG Msg;

    ReceiverWindow = CreateWindowExW(0, L"LargeMessageReceiver", NULL, WS_POPUP,
                                     0, 0, 10, 10, NULL, NULL, GetModuleHandleW(NULL), NULL);
    SetEvent(ReadyEvent);
    if (!ReceiverWindow)
        return 1;

    while (GetMessageW(&Msg, NULL, 0, 0) > 0)
        DispatchMessageW(&Msg);

    return 0;
}

static void
TestWindow(HWND hWnd, PCWSTR Text, PWSTR Buffer, PCSTR Name)
{
    LRESULT Result;
    SIZE_T i;
    BOOL Matches;

    SetTextLength = 0;
    SetTextMatches = FALSE;
    Result = SendMessageW(hWnd, WM_SETTEXT, 0, (LPARAM)Text);
    ok(Result == TRUE, "%s: WM_SETTEXT returned %Id\n", Name, (INT_PTR)Result);
    ok(SetTextLength == LARGE_CHARS, "%s: received %ld characters\n", Name, SetTextLength);
    ok(SetTextMatches, "%s: received text does not match\n", Name);

    ZeroMemory(Buffer, (LARGE_CHARS + 1) * sizeof(WCHAR));
    Result = SendMessageW(hWnd, WM_GETTEXT, LARGE_CHARS + 1, (LPARAM)Buffer);
    ok(Result == LARGE_CHARS, "%s: WM_GETTEXT returned %Id\n", Name, (INT_PTR)Result);

    Matches = TRUE;
    for (i = 0; i < LARGE_CHARS; i++)
    {
        if (Buffer[i] != (WCHAR)(L'a' + (i % 26)))
        {
            Matches = FALSE;
            break;
        }
    }
    ok(Matches, "%s: returned text does not match at %Iu\n", Name, (UINT_PTR)i);
    ok(Buffer[LARGE_CHARS] == UNICODE_NULL, "%s: returned text is not terminated\n", Name);
}

START_TEST(LargeMessageBuffers)
{
    WNDCLASSW Class = { 0 };
    PWSTR Text, Buffer;
    HANDLE Thread;
    DWORD ThreadId;
    HWND LocalWindow;
    SIZE_T i;

    Class.lpfnWndProc = ReceiverProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"LargeMessageReceiver";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed: %lu\n", GetLastError());

    Text = HeapAlloc(GetProcessHeap(), 0, (LARGE_CHARS + 1) * sizeof(WCHAR));
    Buffer = HeapAlloc(GetProcessHeap(), 0, (LARGE_CHARS + 1) * sizeof(WCHAR));
    ok(Text && Buffer, "HeapAlloc failed\n");
    if (!Text || !Buffer)
        return;

    for (i = 0; i < LARGE_CHARS; i++)
        Text[i] = (WCHAR)(L'A' + (i % 26));
    Text[LARGE_CHARS] = UNICODE_NULL;

    ReadyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Thread = CreateThread(NULL, 0, ReceiverThread, NULL, 0, &ThreadId);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (Thread)
    {
        ok(WaitForSingleObject(ReadyEvent, 10000) == WAIT_OBJECT_0, "Receiver thread did not start\n");
        ok(ReceiverWindow != NULL, "Receiver window was not created\n");
        if (ReceiverWindow)
        {
            TestWindow(ReceiverWindow, Text, Buffer, "cross-thread");
            SendMessageW(ReceiverWindow, WM_CLOSE, 0, 0);
        }
        PostThreadMessageW(ThreadId, WM_QUIT, 0, 0);
        ok(WaitForSingleObject(Thread, 10000) == WAIT_OBJECT_0, "Receiver thread did not exit\n");
        CloseHandle(Thread);
    }

    LocalWindow = CreateWindowExW(0, L"LargeMessageReceiver", NULL, WS_POPUP,
                                  0, 0, 10, 10, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(LocalWindow != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (LocalWindow)
    {
        TestWindow(LocalWindow, Text, Buffer, "same-thread");
        DestroyWindow(LocalWindow);
    }

    CloseHandle(ReadyEvent);
    HeapFree(GetProcessHeap(), 0, Buffer);
    HeapFree(GetProcessHeap(), 0, Text);
    UnregisterClassW(L"LargeMessageReceiver", GetModuleHandleW(NULL));
}
