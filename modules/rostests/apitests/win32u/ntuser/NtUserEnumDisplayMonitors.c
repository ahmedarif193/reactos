/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         Test for NtUserEnumDisplayMonitors
 * PROGRAMMERS:
 */

#include "../win32nt.h"

START_TEST(NtUserEnumDisplayMonitors)
{
    HMONITOR Monitors[8];
    MONITORINFO Info;
    RECT Rects[8];
    INT Count, Filled, Index;

    Count = NtUserEnumDisplayMonitors(NULL, NULL, NULL, NULL, 0);
    ok(Count > 0, "Monitor count is %d\n", Count);
    if (Count <= 0) return;

    Filled = NtUserEnumDisplayMonitors(NULL, NULL, Monitors, Rects, RTL_NUMBER_OF(Monitors));
    ok_int(Filled, min(Count, (INT)RTL_NUMBER_OF(Monitors)));
    for (Index = 0; Index < Filled; ++Index)
    {
        Info.cbSize = sizeof(Info);
        ok(GetMonitorInfoW(Monitors[Index], &Info), "GetMonitorInfoW(%p) failed\n", Monitors[Index]);
        ok(EqualRect(&Info.rcMonitor, &Rects[Index]), "Monitor %d rectangle mismatch\n", Index);
    }
}
