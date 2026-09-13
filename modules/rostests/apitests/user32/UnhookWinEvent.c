/*
 * PROJECT:     ReactOS API tests
 * PURPOSE:     Queued WinEvent delivery during hook removal
 */

#include "precomp.h"

static ULONG EventCalls;
static LONG LastChild;
static BOOL RemoveDuringCallback;

static VOID
DrainWinEvents(VOID)
{
    MSG Message;

    while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
}

static VOID CALLBACK
QueuedEventProc(HWINEVENTHOOK Hook, DWORD Event, HWND Window, LONG Object, LONG Child, DWORD Thread, DWORD Time)
{
    ++EventCalls;
    LastChild = Child;
    if (RemoveDuringCallback)
    {
        RemoveDuringCallback = FALSE;
        ok(UnhookWinEvent(Hook), "UnhookWinEvent in callback failed: %lu\n", GetLastError());
        DrainWinEvents();
    }
}

static HWINEVENTHOOK
CreateQueuedEventHook(VOID)
{
    return SetWinEventHook(EVENT_OBJECT_VALUECHANGE, EVENT_OBJECT_VALUECHANGE, NULL, QueuedEventProc, GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
}

START_TEST(UnhookWinEvent)
{
    HWND Window;
    HWINEVENTHOOK Hook;
    ULONG Index;

    Window = CreateWindowExW(0, L"STATIC", L"Queued WinEvent test", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(Window != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!Window)
        return;
    DrainWinEvents();

    EventCalls = 0;
    Hook = CreateQueuedEventHook();
    ok(Hook != NULL, "SetWinEventHook failed: %lu\n", GetLastError());
    if (!Hook)
        goto Cleanup;
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, Window, OBJID_CLIENT, 1);
    ok(EventCalls == 0, "Out-of-context callback ran synchronously\n");
    DrainWinEvents();
    ok(EventCalls == 1 && LastChild == 1, "Live hook received %lu calls, child %ld\n", EventCalls, LastChild);
    ok(UnhookWinEvent(Hook), "UnhookWinEvent failed: %lu\n", GetLastError());

    EventCalls = 0;
    Hook = CreateQueuedEventHook();
    ok(Hook != NULL, "SetWinEventHook failed: %lu\n", GetLastError());
    if (!Hook)
        goto Cleanup;
    for (Index = 0; Index < 16; ++Index)
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, Window, OBJID_CLIENT, 2);
    ok(UnhookWinEvent(Hook), "UnhookWinEvent with queued events failed: %lu\n", GetLastError());
    trace("WINEVENT_UNHOOKED_PENDING\n");
    DrainWinEvents();
    ok(EventCalls == 0, "Removed hook received %lu queued calls\n", EventCalls);

    EventCalls = 0;
    Hook = CreateQueuedEventHook();
    ok(Hook != NULL, "SetWinEventHook failed: %lu\n", GetLastError());
    if (!Hook)
        goto Cleanup;
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, Window, OBJID_CLIENT, 3);
    ok(UnhookWinEvent(Hook), "UnhookWinEvent before replacement failed: %lu\n", GetLastError());
    Hook = CreateQueuedEventHook();
    ok(Hook != NULL, "Replacement SetWinEventHook failed: %lu\n", GetLastError());
    if (!Hook)
        goto Cleanup;
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, Window, OBJID_CLIENT, 4);
    DrainWinEvents();
    ok(EventCalls == 1 && LastChild == 4, "Replacement hook received %lu calls, child %ld\n", EventCalls, LastChild);
    ok(UnhookWinEvent(Hook), "UnhookWinEvent replacement failed: %lu\n", GetLastError());

    EventCalls = 0;
    Hook = CreateQueuedEventHook();
    ok(Hook != NULL, "SetWinEventHook failed: %lu\n", GetLastError());
    if (!Hook)
        goto Cleanup;
    RemoveDuringCallback = TRUE;
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, Window, OBJID_CLIENT, 5);
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, Window, OBJID_CLIENT, 6);
    DrainWinEvents();
    ok(EventCalls == 1 && LastChild == 5, "Self-removing hook received %lu calls, child %ld\n", EventCalls, LastChild);
    if (RemoveDuringCallback)
    {
        RemoveDuringCallback = FALSE;
        UnhookWinEvent(Hook);
    }

Cleanup:
    DestroyWindow(Window);
}
