/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <windows.h>
#include <wtsapi32.h>
#include <wine/test.h>
#ifdef __REACTOS__
#include <reactos/wtssession.h>
#endif

START_TEST(notification)
{
    HWND window;
    BOOL ret;
#ifdef __REACTOS__
    DWORD session;
#endif

    window = CreateWindowExW(0, L"static", L"Session notification test", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, NULL, NULL, NULL);
    ok(window != NULL, "CreateWindow: %lu\n", GetLastError());
    if (!window) return;

    ret = WTSRegisterSessionNotification(NULL, NOTIFY_FOR_THIS_SESSION);
    ok(!ret, "NULL window accepted\n");
    ret = WTSRegisterSessionNotification(window, 2);
    ok(!ret, "Invalid flags accepted\n");
    ret = WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION);
    ok(ret, "Register: %lu\n", GetLastError());
    ret = WTSRegisterSessionNotificationEx(WTS_CURRENT_SERVER_HANDLE, window, NOTIFY_FOR_ALL_SESSIONS);
    ok(ret, "Duplicate register: %lu\n", GetLastError());
    ret = WTSUnRegisterSessionNotification(window);
    ok(ret, "First unregister: %lu\n", GetLastError());
    ret = WTSUnRegisterSessionNotificationEx(WTS_CURRENT_SERVER_HANDLE, window);
    ok(ret, "Second unregister: %lu\n", GetLastError());
    ret = WTSUnRegisterSessionNotification(window);
    ok(!ret, "Unregistered window accepted\n");
    ret = WTSRegisterSessionNotification(window, NOTIFY_FOR_ALL_SESSIONS);
    ok(ret, "Register after unregister: %lu\n", GetLastError());
#ifdef __REACTOS__
    ret = ProcessIdToSessionId(GetCurrentProcessId(), &session);
    ok(ret, "ProcessIdToSessionId: %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ret = NtUserCallTwoParam(WTS_SESSION_LOCK, session, ROS_WTS_NOTIFY);
    ok(!ret && GetLastError() == ERROR_ACCESS_DENIED,
       "Unprivileged session publisher returned %d, %lu\n", ret, GetLastError());
#endif
    DestroyWindow(window);
    ret = WTSUnRegisterSessionNotification(window);
    ok(!ret, "Destroyed window accepted\n");
    window = CreateWindowExW(0, L"static", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    ok(window != NULL, "Second CreateWindow: %lu\n", GetLastError());
    if (window)
    {
        ret = WTSUnRegisterSessionNotification(window);
        ok(!ret, "New window inherited an old registration\n");
        ret = WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION);
        ok(ret, "New window register: %lu\n", GetLastError());
        ret = WTSUnRegisterSessionNotification(window);
        ok(ret, "New window unregister: %lu\n", GetLastError());
        DestroyWindow(window);
    }
}
