/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "precomp.h"

START_TEST(ClipboardAccess)
{
    HWINSTA original, station, restricted = NULL;
    DWORD sequence;
    BOOL ret;

    original = GetProcessWindowStation();
    station = CreateWindowStationW(NULL, 0, WINSTA_ALL_ACCESS, NULL);
    ok(station != NULL, "CreateWindowStation: %lu\n", GetLastError());
    if (!station) return;
    ret = DuplicateHandle(GetCurrentProcess(), station, GetCurrentProcess(),
                          (HANDLE *)&restricted, WINSTA_ENUMDESKTOPS, FALSE, 0);
    ok(ret, "Duplicate restricted station: %lu\n", GetLastError());
    if (!ret) goto done;
    ret = SetProcessWindowStation(restricted);
    ok(ret, "Set restricted station: %lu\n", GetLastError());
    if (!ret) goto done;
    sequence = GetClipboardSequenceNumber();
    ok(sequence == 0, "Restricted sequence leaked: %lu\n", sequence);
    SetLastError(0xdeadbeef);
    ret = OpenClipboard(NULL);
    ok(!ret && GetLastError() == ERROR_ACCESS_DENIED,
       "Restricted OpenClipboard: %d, %lu\n", ret, GetLastError());
    if (ret) CloseClipboard();
    ret = SetProcessWindowStation(station);
    ok(ret, "Set unrestricted station: %lu\n", GetLastError());
    if (ret)
    {
        ret = OpenClipboard(NULL);
        ok(ret, "Unrestricted OpenClipboard: %lu\n", GetLastError());
        if (ret)
        {
            ret = EmptyClipboard();
            ok(ret, "EmptyClipboard: %lu\n", GetLastError());
            sequence = GetClipboardSequenceNumber();
            ok(sequence != 0, "Unrestricted sequence unavailable\n");
            CloseClipboard();
        }
    }
done:
    ret = SetProcessWindowStation(original);
    ok(ret, "Restore original station: %lu\n", GetLastError());
    if (restricted) CloseWindowStation(restricted);
    CloseWindowStation(station);
}
