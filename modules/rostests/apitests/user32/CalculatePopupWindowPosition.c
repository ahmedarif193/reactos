/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Torture tests for CalculatePopupWindowPosition
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

typedef BOOL (WINAPI *FN_CalculatePopupWindowPosition)(const POINT *, const SIZE *, UINT, RECT *, RECT *);

static FN_CalculatePopupWindowPosition pCalculatePopupWindowPosition;

static RECT g_rcMonitor;
static RECT g_rcWork;

static void
GetRectsForPoint(_In_ LONG x, _In_ LONG y, _Out_ RECT *prcMonitor, _Out_ RECT *prcWork)
{
    MONITORINFO mi;
    POINT pt;
    HMONITOR hMonitor;

    pt.x = x;
    pt.y = y;
    hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

    mi.cbSize = sizeof(mi);
    if (hMonitor && GetMonitorInfoW(hMonitor, &mi))
    {
        *prcMonitor = mi.rcMonitor;
        *prcWork = mi.rcWork;
        return;
    }

    *prcMonitor = g_rcMonitor;
    *prcWork = g_rcWork;
}

static void
GetPrimaryRects(void)
{
    MONITORINFO mi;
    POINT pt = { 0, 0 };
    HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);

    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMonitor, &mi))
    {
        SetRect(&g_rcMonitor, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        g_rcWork = g_rcMonitor;
        return;
    }

    g_rcMonitor = mi.rcMonitor;
    g_rcWork = mi.rcWork;
}

static void
ExpectRect(
    _In_ const RECT *prc,
    _In_ LONG left,
    _In_ LONG top,
    _In_ LONG right,
    _In_ LONG bottom,
    _In_ const char *pszName)
{
    ok(prc->left == left && prc->top == top && prc->right == right && prc->bottom == bottom,
       "%s: expected {%ld,%ld,%ld,%ld}, got {%ld,%ld,%ld,%ld}\n", pszName,
       left, top, right, bottom, prc->left, prc->top, prc->right, prc->bottom);
}

static BOOL
Call(_In_ LONG x, _In_ LONG y, _In_ LONG cx, _In_ LONG cy, _In_ UINT flags,
     _In_opt_ RECT *pExclude, _Out_ RECT *prc)
{
    POINT pt;
    SIZE size;

    pt.x = x;
    pt.y = y;
    size.cx = cx;
    size.cy = cy;
    SetRect(prc, 0x0BADF00D, 0x0BADF00D, 0x0BADF00D, 0x0BADF00D);
    return pCalculatePopupWindowPosition(&pt, &size, flags, pExclude, prc);
}

static void
Test_NullAndBadPointers(void)
{
    POINT pt = { 100, 100 };
    SIZE size = { 40, 40 };
    RECT rc;
    BOOL ret;
    DWORD dwErr;

    SetLastError(0xdeadbeef);
    ret = pCalculatePopupWindowPosition(NULL, &size, TPM_LEFTALIGN | TPM_TOPALIGN, NULL, &rc);
    dwErr = GetLastError();
    ok(!ret, "NULL anchorPoint should fail\n");
    ok(dwErr == 0xdeadbeef, "NULL anchorPoint must not set the last error, got %lu\n", dwErr);

    SetLastError(0xdeadbeef);
    ret = pCalculatePopupWindowPosition(&pt, NULL, TPM_LEFTALIGN | TPM_TOPALIGN, NULL, &rc);
    dwErr = GetLastError();
    ok(!ret, "NULL windowSize should fail\n");
    ok(dwErr == 0xdeadbeef, "NULL windowSize must not set the last error, got %lu\n", dwErr);

    SetLastError(0xdeadbeef);
    ret = pCalculatePopupWindowPosition(&pt, &size, TPM_LEFTALIGN | TPM_TOPALIGN, NULL, NULL);
    dwErr = GetLastError();
    ok(!ret, "NULL popupWindowPosition should fail\n");
    ok(dwErr == ERROR_INVALID_PARAMETER,
       "NULL popupWindowPosition: expected ERROR_INVALID_PARAMETER, got %lu\n", dwErr);

    SetRect(&rc, 1, 2, 3, 4);
    SetLastError(0xdeadbeef);
    ret = pCalculatePopupWindowPosition(NULL, NULL, 0, NULL, &rc);
    ok(!ret, "all-NULL should fail\n");
    ExpectRect(&rc, 1, 2, 3, 4, "output untouched on failure");

    ret = FALSE;
    _SEH2_TRY
    {
        ret = pCalculatePopupWindowPosition((const POINT *)InvalidPointer, &size,
                                            TPM_LEFTALIGN, NULL, &rc);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ret = FALSE;
        trace("bogus anchorPoint raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;
    ok(!ret, "a bogus anchorPoint must not report success\n");

    ret = FALSE;
    _SEH2_TRY
    {
        ret = pCalculatePopupWindowPosition(&pt, &size, TPM_LEFTALIGN,
                                            (RECT *)InvalidPointer, &rc);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ret = FALSE;
        trace("bogus excludeRect raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;
    ok(!ret, "a bogus excludeRect must not report success\n");
}

static void
Test_Alignment(void)
{
    RECT rc;
    LONG x = g_rcMonitor.left + (g_rcMonitor.right - g_rcMonitor.left) / 2;
    LONG y = g_rcMonitor.top + (g_rcMonitor.bottom - g_rcMonitor.top) / 2;
    const LONG cx = 60, cy = 40;

    ok(Call(x, y, cx, cy, TPM_LEFTALIGN | TPM_TOPALIGN, NULL, &rc), "LEFT|TOP failed\n");
    ExpectRect(&rc, x, y, x + cx, y + cy, "LEFT|TOP");

    ok(Call(x, y, cx, cy, TPM_RIGHTALIGN | TPM_TOPALIGN, NULL, &rc), "RIGHT|TOP failed\n");
    ExpectRect(&rc, x - cx, y, x, y + cy, "RIGHT|TOP");

    ok(Call(x, y, cx, cy, TPM_CENTERALIGN | TPM_TOPALIGN, NULL, &rc), "CENTER|TOP failed\n");
    ExpectRect(&rc, x - cx / 2, y, x - cx / 2 + cx, y + cy, "CENTER|TOP");

    ok(Call(x, y, cx, cy, TPM_LEFTALIGN | TPM_BOTTOMALIGN, NULL, &rc), "LEFT|BOTTOM failed\n");
    ExpectRect(&rc, x, y - cy, x + cx, y, "LEFT|BOTTOM");

    ok(Call(x, y, cx, cy, TPM_LEFTALIGN | TPM_VCENTERALIGN, NULL, &rc), "LEFT|VCENTER failed\n");
    ExpectRect(&rc, x, y - cy / 2, x + cx, y - cy / 2 + cy, "LEFT|VCENTER");

    ok(Call(x, y, cx, cy, 0, NULL, &rc), "flags 0 failed\n");
    ExpectRect(&rc, x, y, x + cx, y + cy, "flags 0 == LEFT|TOP");

    ok(Call(x, y, cx, cy, TPM_RIGHTALIGN | TPM_CENTERALIGN, NULL, &rc),
       "RIGHT|CENTER failed\n");
    ExpectRect(&rc, x - cx / 2, y, x - cx / 2 + cx, y + cy,
               "CENTERALIGN wins over RIGHTALIGN");

    ok(Call(x, y, cx, cy, TPM_BOTTOMALIGN | TPM_VCENTERALIGN, NULL, &rc),
       "BOTTOM|VCENTER failed\n");
    ExpectRect(&rc, x, y - cy / 2, x + cx, y - cy / 2 + cy,
               "VCENTERALIGN wins over BOTTOMALIGN");

    SetLastError(0xdeadbeef);
    ok(!Call(x, y, cx, cy, 0xFFFFFFFF & ~TPM_LAYOUTRTL, NULL, &rc),
       "every flag bit set must be rejected\n");
    ok(GetLastError() == ERROR_INVALID_FLAGS,
       "expected ERROR_INVALID_FLAGS, got %lu\n", GetLastError());
}

static void
Test_FlagBits(void)
{
    LONG x = g_rcMonitor.left + 40;
    LONG y = g_rcMonitor.top + 40;
    RECT rc;
    UINT bit;
    DWORD dwAccepted = 0;

    for (bit = 0; bit < 32; bit++)
    {
        UINT flags = 1u << bit;

        SetLastError(0xdeadbeef);
        if (Call(x, y, 40, 20, flags, NULL, &rc))
            dwAccepted |= flags;
        else
            trace("flag 0x%08x rejected, error %lu\n", flags, GetLastError());
    }

    trace("accepted flag mask = 0x%08lx\n", dwAccepted);
    ok(dwAccepted == 0x0001FDFF,
       "expected the TPM_* mask 0x0001FDFF to be accepted, got 0x%08lx\n", dwAccepted);
}

static void
Test_Placement(void)
{
    static const struct
    {
        LONG dx, dy;
        const char *pszName;
    } Offsets[] =
    {
        {    0,    0, "inside" },
        {  500,  500, "past bottom-right" },
        { -500, -500, "past top-left" },
        {  500, -500, "past top-right" },
        { -500,  500, "past bottom-left" },
    };
    const LONG cx = 80, cy = 50;
    RECT rc;
    UINT i;

    trace("monitor {%ld,%ld,%ld,%ld} work {%ld,%ld,%ld,%ld}\n",
          g_rcMonitor.left, g_rcMonitor.top, g_rcMonitor.right, g_rcMonitor.bottom,
          g_rcWork.left, g_rcWork.top, g_rcWork.right, g_rcWork.bottom);

    for (i = 0; i < ARRAYSIZE(Offsets); i++)
    {
        LONG x = g_rcMonitor.right + Offsets[i].dx;
        LONG y = g_rcMonitor.bottom + Offsets[i].dy;
        RECT rcMon, rcWrk, rcBound;
        UINT pass;

        GetRectsForPoint(x, y, &rcMon, &rcWrk);

        for (pass = 0; pass < 2; pass++)
        {
            UINT flags = TPM_LEFTALIGN | TPM_TOPALIGN | (pass ? TPM_WORKAREA : 0);

            rcBound = pass ? rcWrk : rcMon;

            ok(Call(x, y, cx, cy, flags, NULL, &rc),
               "%s%s failed, error %lu\n", Offsets[i].pszName, pass ? " (workarea)" : "",
               GetLastError());
            trace("%-20s%s anchor(%ld,%ld) bound {%ld,%ld,%ld,%ld} -> {%ld,%ld,%ld,%ld}\n",
                  Offsets[i].pszName, pass ? " workarea" : "        ", x, y,
                  rcBound.left, rcBound.top, rcBound.right, rcBound.bottom,
                  rc.left, rc.top, rc.right, rc.bottom);

            ok(rc.right - rc.left == cx && rc.bottom - rc.top == cy,
               "%s: size must be preserved, got %ldx%ld\n", Offsets[i].pszName,
               rc.right - rc.left, rc.bottom - rc.top);

            if (cx <= rcBound.right - rcBound.left)
            {
                ok(rc.left >= rcBound.left && rc.right <= rcBound.right,
                   "%s: x %ld..%ld escapes the bound %ld..%ld\n", Offsets[i].pszName,
                   rc.left, rc.right, rcBound.left, rcBound.right);
            }
            if (cy <= rcBound.bottom - rcBound.top)
            {
                ok(rc.top >= rcBound.top && rc.bottom <= rcBound.bottom,
                   "%s: y %ld..%ld escapes the bound %ld..%ld\n", Offsets[i].pszName,
                   rc.top, rc.bottom, rcBound.top, rcBound.bottom);
            }
        }
    }
}

static void
Test_Determinism(void)
{
    RECT rcFirst, rc;
    LONG x = g_rcMonitor.left + 17;
    LONG y = g_rcMonitor.top + 23;
    UINT i;

    ok(Call(x, y, 70, 30, TPM_LEFTALIGN | TPM_TOPALIGN, NULL, &rcFirst), "first call failed\n");

    for (i = 0; i < 2000; i++)
    {
        if (!Call(x, y, 70, 30, TPM_LEFTALIGN | TPM_TOPALIGN, NULL, &rc))
        {
            ok(FALSE, "iteration %u failed, error %lu\n", i, GetLastError());
            return;
        }
        if (!EqualRect(&rc, &rcFirst))
        {
            ok(FALSE, "iteration %u drifted to {%ld,%ld,%ld,%ld}\n",
               i, rc.left, rc.top, rc.right, rc.bottom);
            return;
        }
    }
    ok(TRUE, "2000 identical calls stayed deterministic\n");
}

static void
Test_ExtremeSizes(void)
{
    static const LONG Sizes[] = { 0, 1, -1, -1000, 0x7FFF, 0xFFFF, 0x7FFFFFFF, (LONG)0x80000000 };
    RECT rc;
    UINT i, j;
    LONG x = g_rcMonitor.left + 5;
    LONG y = g_rcMonitor.top + 5;

    for (i = 0; i < ARRAYSIZE(Sizes); i++)
    {
        for (j = 0; j < ARRAYSIZE(Sizes); j++)
        {
            BOOL ret = FALSE;

            _SEH2_TRY
            {
                ret = Call(x, y, Sizes[i], Sizes[j], TPM_LEFTALIGN | TPM_TOPALIGN, NULL, &rc);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                ok(FALSE, "size %ldx%ld raised 0x%08lx\n",
                   Sizes[i], Sizes[j], _SEH2_GetExceptionCode());
                ret = FALSE;
            }
            _SEH2_END;

            if (ret)
            {
                trace("size %11ld x %11ld -> {%ld,%ld,%ld,%ld}\n",
                      Sizes[i], Sizes[j], rc.left, rc.top, rc.right, rc.bottom);
            }
            else
            {
                trace("size %11ld x %11ld -> FALSE (error %lu)\n",
                      Sizes[i], Sizes[j], GetLastError());
            }
        }
    }
    ok(TRUE, "extreme sizes survived\n");
}

static void
Test_ExtremeAnchors(void)
{
    static const LONG Coords[] =
    {
        0, 1, -1, 0x7FFF, -0x8000, 0x7FFFFFFF, (LONG)0x80000000, 0x40000000
    };
    RECT rc;
    UINT i, j;

    for (i = 0; i < ARRAYSIZE(Coords); i++)
    {
        for (j = 0; j < ARRAYSIZE(Coords); j++)
        {
            BOOL ret = FALSE;

            _SEH2_TRY
            {
                ret = Call(Coords[i], Coords[j], 40, 20, TPM_LEFTALIGN | TPM_TOPALIGN, NULL, &rc);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                ok(FALSE, "anchor %ld,%ld raised 0x%08lx\n",
                   Coords[i], Coords[j], _SEH2_GetExceptionCode());
                ret = FALSE;
            }
            _SEH2_END;

            if (ret)
            {
                ok(rc.right - rc.left == 40 && rc.bottom - rc.top == 20,
                   "anchor %ld,%ld: size drifted to %ldx%ld\n",
                   Coords[i], Coords[j], rc.right - rc.left, rc.bottom - rc.top);
            }
        }
    }
    ok(TRUE, "extreme anchors survived\n");
}

static void
Test_ExcludeRect(void)
{
    RECT rcExclude, rcHit, rc;
    LONG x = g_rcMonitor.left + (g_rcMonitor.right - g_rcMonitor.left) / 2;
    LONG y = g_rcMonitor.top + (g_rcMonitor.bottom - g_rcMonitor.top) / 2;
    const LONG cx = 60, cy = 40;

    SetRect(&rcExclude, x - 20, y - 20, x + 20, y + 20);
    ok(Call(x, y, cx, cy, TPM_LEFTALIGN | TPM_TOPALIGN, &rcExclude, &rc),
       "exclude (horizontal) failed\n");
    trace("exclude {%ld,%ld,%ld,%ld} horizontal -> {%ld,%ld,%ld,%ld}\n",
          rcExclude.left, rcExclude.top, rcExclude.right, rcExclude.bottom,
          rc.left, rc.top, rc.right, rc.bottom);
    ok(!IntersectRect(&rcHit, &rc, &rcExclude),
       "popup {%ld,%ld,%ld,%ld} overlaps exclude {%ld,%ld,%ld,%ld}\n",
       rc.left, rc.top, rc.right, rc.bottom,
       rcExclude.left, rcExclude.top, rcExclude.right, rcExclude.bottom);

    ok(Call(x, y, cx, cy, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL, &rcExclude, &rc),
       "exclude (vertical) failed\n");
    ok(!IntersectRect(&rcHit, &rc, &rcExclude), "vertical popup overlaps the exclude rect\n");

    SetRect(&rcExclude, g_rcMonitor.left - 400, g_rcMonitor.top - 400,
            g_rcMonitor.left - 300, g_rcMonitor.top - 300);
    ok(Call(x, y, cx, cy, TPM_LEFTALIGN | TPM_TOPALIGN, &rcExclude, &rc),
       "off-screen exclude failed\n");
    ExpectRect(&rc, x, y, x + cx, y + cy, "off-screen exclude is ignored");

    SetRect(&rcExclude, 0, 0, 0, 0);
    ok(Call(x, y, cx, cy, TPM_LEFTALIGN | TPM_TOPALIGN, &rcExclude, &rc),
       "empty exclude failed\n");
    ExpectRect(&rc, x, y, x + cx, y + cy, "empty exclude is ignored");

    SetRect(&rcExclude, x + 100, y + 100, x - 100, y - 100);
    ok(Call(x, y, cx, cy, TPM_LEFTALIGN | TPM_TOPALIGN, &rcExclude, &rc),
       "inverted exclude failed\n");
    trace("inverted exclude -> {%ld,%ld,%ld,%ld}\n", rc.left, rc.top, rc.right, rc.bottom);

    SetRect(&rcExclude, g_rcMonitor.left - 10000, g_rcMonitor.top - 10000,
            g_rcMonitor.right + 10000, g_rcMonitor.bottom + 10000);
    ok(Call(x, y, cx, cy, TPM_LEFTALIGN | TPM_TOPALIGN, &rcExclude, &rc),
       "exclude covering everything failed\n");
    trace("exclude covers the world -> {%ld,%ld,%ld,%ld}\n",
          rc.left, rc.top, rc.right, rc.bottom);
    ok(rc.right - rc.left == cx && rc.bottom - rc.top == cy,
       "size must survive an unsatisfiable exclude rect\n");
}

static void
Test_AliasedOutput(void)
{
    RECT rc;
    POINT pt;
    SIZE size;
    LONG x = g_rcMonitor.left + 33;
    LONG y = g_rcMonitor.top + 44;

    pt.x = x;
    pt.y = y;
    size.cx = 50;
    size.cy = 60;
    SetRect(&rc, x - 5, y - 5, x + 5, y + 5);

    ok(pCalculatePopupWindowPosition(&pt, &size, TPM_LEFTALIGN | TPM_TOPALIGN, &rc, &rc),
       "excludeRect aliased with the output failed\n");
    trace("aliased exclude/output -> {%ld,%ld,%ld,%ld}\n", rc.left, rc.top, rc.right, rc.bottom);
    ok(rc.right - rc.left == size.cx && rc.bottom - rc.top == size.cy,
       "aliasing must still produce the requested size, got %ldx%ld\n",
       rc.right - rc.left, rc.bottom - rc.top);
}

static DWORD WINAPI
HammerThread(LPVOID pv)
{
    LONG *pFailures = (LONG *)pv;
    RECT rc;
    UINT i;

    for (i = 0; i < 5000; i++)
    {
        LONG x = g_rcMonitor.left + (LONG)(i % 500) - 100;
        LONG y = g_rcMonitor.top + (LONG)(i % 400) - 100;

        if (!Call(x, y, 40 + (LONG)(i % 30), 20 + (LONG)(i % 15),
                  TPM_LEFTALIGN | TPM_TOPALIGN, NULL, &rc))
        {
            InterlockedIncrement(pFailures);
            break;
        }
        if (rc.right < rc.left || rc.bottom < rc.top)
        {
            InterlockedIncrement(pFailures);
            break;
        }
    }
    return 0;
}

static void
Test_Threaded(void)
{
    HANDLE hThreads[4];
    LONG Failures = 0;
    UINT i;

    for (i = 0; i < ARRAYSIZE(hThreads); i++)
    {
        hThreads[i] = CreateThread(NULL, 0, HammerThread, &Failures, 0, NULL);
        ok(hThreads[i] != NULL, "CreateThread %u failed, error %lu\n", i, GetLastError());
    }

    WaitForMultipleObjects(ARRAYSIZE(hThreads), hThreads, TRUE, 60000);
    for (i = 0; i < ARRAYSIZE(hThreads); i++)
    {
        if (hThreads[i])
            CloseHandle(hThreads[i]);
    }

    ok(Failures == 0, "%ld thread iterations misbehaved\n", Failures);
}

START_TEST(CalculatePopupWindowPosition)
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");

    pCalculatePopupWindowPosition =
        (FN_CalculatePopupWindowPosition)GetProcAddress(hUser32, "CalculatePopupWindowPosition");
    ok(pCalculatePopupWindowPosition != NULL, "user32!CalculatePopupWindowPosition is missing\n");
    if (!pCalculatePopupWindowPosition)
        return;

    GetPrimaryRects();

    Test_NullAndBadPointers();
    Test_Alignment();
    Test_FlagBits();
    Test_Placement();
    Test_ExcludeRect();
    Test_AliasedOutput();
    Test_ExtremeSizes();
    Test_ExtremeAnchors();
    Test_Determinism();
    Test_Threaded();
}
