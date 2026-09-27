/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         Test for NtUserEnumDisplayMonitors
 * PROGRAMMERS:
 */

#include "../win32nt.h"

static ULONG gMonitorCount;
static HMONITOR ghMonitor;
static HDC ghdcMonitor;
static RECT grcMonitor;
static LPARAM gData;
static BOOL gContinue;

static
BOOL
CALLBACK
MonitorEnumProc(
    HMONITOR hMonitor,
    HDC hdcMonitor,
    LPRECT lprcMonitor,
    LPARAM dwData)
{
    gMonitorCount++;
    if (gMonitorCount == 1)
    {
        ghMonitor = hMonitor;
        ghdcMonitor = hdcMonitor;
        grcMonitor = *lprcMonitor;
        gData = dwData;
    }
    return gContinue;
}

START_TEST(NtUserEnumDisplayMonitors)
{
    MONITORINFO Info;
    BOOL ret;

    gMonitorCount = 0;
    gContinue = TRUE;
    ret = NtUserEnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0x1234);
    ok_int(ret, TRUE);
    ok(gMonitorCount > 0, "gMonitorCount is %lu\n", gMonitorCount);
    ok_ptr(ghdcMonitor, NULL);
    ok_long((LONG)gData, 0x1234);

    Info.cbSize = sizeof(Info);
    ok(GetMonitorInfoW(ghMonitor, &Info), "GetMonitorInfoW(%p) failed\n", ghMonitor);
    ok((Info.dwFlags & MONITORINFOF_PRIMARY) != 0, "The first monitor is not the primary one\n");
    ok(EqualRect(&Info.rcMonitor, &grcMonitor), "Monitor rectangle mismatch\n");

    gMonitorCount = 0;
    gContinue = FALSE;
    ret = NtUserEnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
    ok_int(ret, FALSE);
    ok_long(gMonitorCount, 1);
}
