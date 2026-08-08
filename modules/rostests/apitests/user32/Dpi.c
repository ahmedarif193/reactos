/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Tests modern DPI API semantics implemented by user32 and SHCore
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"
#include <shellscalingapi.h>

typedef BOOL (WINAPI *PGETPROCESSDPIAWARENESSINTERNAL)(HANDLE, DPI_AWARENESS *);
typedef BOOL (WINAPI *PSETPROCESSDPIAWARENESSCONTEXT)(DPI_AWARENESS_CONTEXT);
typedef BOOL (WINAPI *PGETDPIFORMONITORINTERNAL)(HMONITOR, UINT, UINT *, UINT *);
typedef BOOL (WINAPI *PISVALIDDPIAWARENESSCONTEXT)(DPI_AWARENESS_CONTEXT);
typedef UINT (WINAPI *PGETDPIFORSYSTEM)(VOID);
typedef INT (WINAPI *PGETSYSTEMMETRICSFORDPI)(INT, UINT);
typedef BOOL (WINAPI *PADJUSTWINDOWRECTEXFORDPI)(LPRECT, DWORD, BOOL, DWORD, UINT);
typedef BOOL (WINAPI *PENABLENONCLIENTDPISCALING)(HWND);
typedef HRESULT (WINAPI *PSHGETPROCESSDPIAWARENESS)(HANDLE, PROCESS_DPI_AWARENESS *);
typedef HRESULT (WINAPI *PSHSETPROCESSDPIAWARENESS)(PROCESS_DPI_AWARENESS);
typedef HRESULT (WINAPI *PSHGETDPIFORMONITOR)(HMONITOR, MONITOR_DPI_TYPE, UINT *, UINT *);

static UINT
GetFallbackSystemDpi(void)
{
    HDC hdc;
    UINT Dpi;

    hdc = GetDC(NULL);
    if (hdc == NULL) return USER_DEFAULT_SCREEN_DPI;
    Dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return Dpi ? Dpi : USER_DEFAULT_SCREEN_DPI;
}

static void
TestSizingApis(
    _In_ HMODULE User32)
{
    PGETSYSTEMMETRICSFORDPI GetSystemMetricsForDpiFn;
    PADJUSTWINDOWRECTEXFORDPI AdjustWindowRectExForDpiFn;
    PGETDPIFORSYSTEM GetDpiForSystemFn;
    RECT SourceRect, TargetRect;
    INT SourceCaption, TargetCaption;
    INT SourceFrame, TargetFrame;
    UINT SystemDpi, TargetDpi;
    BOOL Result;

    GetSystemMetricsForDpiFn = (PGETSYSTEMMETRICSFORDPI)GetProcAddress(User32, "GetSystemMetricsForDpi");
    AdjustWindowRectExForDpiFn = (PADJUSTWINDOWRECTEXFORDPI)GetProcAddress(User32, "AdjustWindowRectExForDpi");
    GetDpiForSystemFn = (PGETDPIFORSYSTEM)GetProcAddress(User32, "GetDpiForSystem");
    if (GetSystemMetricsForDpiFn == NULL)
    {
        skip("GetSystemMetricsForDpi is not exported\n");
        return;
    }

    SystemDpi = GetDpiForSystemFn != NULL ? GetDpiForSystemFn() : GetFallbackSystemDpi();
    ok(SystemDpi != 0, "The system DPI is zero\n");
    if (SystemDpi == 0) return;
    TargetDpi = SystemDpi * 2;

    SourceCaption = GetSystemMetricsForDpiFn(SM_CYCAPTION, SystemDpi);
    TargetCaption = GetSystemMetricsForDpiFn(SM_CYCAPTION, TargetDpi);
    ok(SourceCaption > 0, "GetSystemMetricsForDpi returned an invalid source caption height %d\n", SourceCaption);
    ok(TargetCaption == (SourceCaption - 1) * 2 + 1, "Caption height %d at %u DPI did not scale from %d at %u DPI\n", TargetCaption, TargetDpi, SourceCaption, SystemDpi);

    SourceFrame = GetSystemMetricsForDpiFn(SM_CXFRAME, SystemDpi);
    TargetFrame = GetSystemMetricsForDpiFn(SM_CXFRAME, TargetDpi);
    ok(SourceFrame >= 3, "GetSystemMetricsForDpi returned an invalid source frame width %d\n", SourceFrame);
    ok(TargetFrame == (SourceFrame - 3) * 2 + 3, "Frame width %d at %u DPI did not scale from %d at %u DPI\n", TargetFrame, TargetDpi, SourceFrame, SystemDpi);

    if (AdjustWindowRectExForDpiFn == NULL)
    {
        skip("AdjustWindowRectExForDpi is not exported\n");
        return;
    }

    SetRect(&SourceRect, 0, 0, 100, 100);
    TargetRect = SourceRect;
    Result = AdjustWindowRectExForDpiFn(&SourceRect, WS_CAPTION, FALSE, 0, SystemDpi);
    ok(Result, "AdjustWindowRectExForDpi failed for %u DPI, error %lu\n", SystemDpi, GetLastError());
    Result = AdjustWindowRectExForDpiFn(&TargetRect, WS_CAPTION, FALSE, 0, TargetDpi);
    ok(Result, "AdjustWindowRectExForDpi failed for %u DPI, error %lu\n", TargetDpi, GetLastError());
    ok(TargetRect.top == SourceRect.top - (TargetCaption - SourceCaption), "The target top margin %ld did not scale from %ld\n", TargetRect.top, SourceRect.top);

    SetRect(&SourceRect, 0, 0, 100, 100);
    TargetRect = SourceRect;
    Result = AdjustWindowRectExForDpiFn(&SourceRect, WS_CAPTION | WS_THICKFRAME, FALSE, 0, SystemDpi);
    ok(Result, "AdjustWindowRectExForDpi failed for a source thick frame, error %lu\n", GetLastError());
    Result = AdjustWindowRectExForDpiFn(&TargetRect, WS_CAPTION | WS_THICKFRAME, FALSE, 0, TargetDpi);
    ok(Result, "AdjustWindowRectExForDpi failed for a target thick frame, error %lu\n", GetLastError());
    ok(TargetRect.right - TargetRect.left > SourceRect.right - SourceRect.left, "The thick-frame width did not grow for %u DPI\n", TargetDpi);
    ok(TargetRect.bottom - TargetRect.top > SourceRect.bottom - SourceRect.top, "The thick-frame height did not grow for %u DPI\n", TargetDpi);
}

static void
TestDpiContexts(
    _In_ HMODULE User32,
    _In_opt_ HMODULE ShCore)
{
    PGETPROCESSDPIAWARENESSINTERNAL GetProcessDpiAwarenessInternalFn;
    PSETPROCESSDPIAWARENESSCONTEXT SetProcessDpiAwarenessContextFn;
    PISVALIDDPIAWARENESSCONTEXT IsValidDpiAwarenessContextFn;
    PSHGETPROCESSDPIAWARENESS ShGetProcessDpiAwarenessFn;
    PSHSETPROCESSDPIAWARENESS ShSetProcessDpiAwarenessFn;
    PROCESS_DPI_AWARENESS ShNullAwareness, ShCurrentAwareness;
    DPI_AWARENESS NullAwareness, CurrentAwareness;
    HRESULT ResultHr;
    DWORD Error;
    BOOL Result;

    GetProcessDpiAwarenessInternalFn = (PGETPROCESSDPIAWARENESSINTERNAL)GetProcAddress(User32, "GetProcessDpiAwarenessInternal");
    SetProcessDpiAwarenessContextFn = (PSETPROCESSDPIAWARENESSCONTEXT)GetProcAddress(User32, "SetProcessDpiAwarenessContext");
    IsValidDpiAwarenessContextFn = (PISVALIDDPIAWARENESSCONTEXT)GetProcAddress(User32, "IsValidDpiAwarenessContext");

    if (GetProcessDpiAwarenessInternalFn != NULL)
    {
        NullAwareness = DPI_AWARENESS_INVALID;
        CurrentAwareness = DPI_AWARENESS_INVALID;
        Result = GetProcessDpiAwarenessInternalFn(NULL, &NullAwareness);
        ok(Result, "GetProcessDpiAwarenessInternal(NULL) failed, error %lu\n", GetLastError());
        Result = GetProcessDpiAwarenessInternalFn(GetCurrentProcess(), &CurrentAwareness);
        ok(Result, "GetProcessDpiAwarenessInternal(current process) failed, error %lu\n", GetLastError());
        ok(NullAwareness == CurrentAwareness, "NULL returned awareness %d, current process returned %d\n", NullAwareness, CurrentAwareness);
    }
    else
    {
        skip("GetProcessDpiAwarenessInternal is not exported\n");
    }

    if (IsValidDpiAwarenessContextFn != NULL)
    {
        Result = IsValidDpiAwarenessContextFn(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
        ok(Result, "The standard system-aware context was rejected\n");
        Result = IsValidDpiAwarenessContextFn((DPI_AWARENESS_CONTEXT)(ULONG_PTR)0x6111);
        ok(!Result, "The mismatched system-aware context 0x6111 was accepted\n");
    }
    else
    {
        skip("IsValidDpiAwarenessContext is not exported\n");
    }

    ShGetProcessDpiAwarenessFn = ShCore != NULL ? (PSHGETPROCESSDPIAWARENESS)GetProcAddress(ShCore, "GetProcessDpiAwareness") : NULL;
    ShSetProcessDpiAwarenessFn = ShCore != NULL ? (PSHSETPROCESSDPIAWARENESS)GetProcAddress(ShCore, "SetProcessDpiAwareness") : NULL;
    if (ShGetProcessDpiAwarenessFn != NULL)
    {
        ShNullAwareness = (PROCESS_DPI_AWARENESS)-1;
        ShCurrentAwareness = (PROCESS_DPI_AWARENESS)-1;
        ResultHr = ShGetProcessDpiAwarenessFn(NULL, &ShNullAwareness);
        ok(ResultHr == S_OK, "SHCore GetProcessDpiAwareness(NULL) returned %#lx\n", ResultHr);
        ResultHr = ShGetProcessDpiAwarenessFn(GetCurrentProcess(), &ShCurrentAwareness);
        ok(ResultHr == S_OK, "SHCore GetProcessDpiAwareness(current process) returned %#lx\n", ResultHr);
        ok(ShNullAwareness == ShCurrentAwareness, "SHCore NULL awareness %d differs from current-process awareness %d\n", ShNullAwareness, ShCurrentAwareness);
    }
    else
    {
        skip("SHCore GetProcessDpiAwareness is not exported\n");
    }

    if (ShSetProcessDpiAwarenessFn != NULL)
    {
        ResultHr = ShSetProcessDpiAwarenessFn(PROCESS_SYSTEM_DPI_AWARE);
        if (ResultHr == S_OK) ResultHr = ShSetProcessDpiAwarenessFn(PROCESS_SYSTEM_DPI_AWARE);
        ok(ResultHr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED), "The repeated SHCore DPI setter returned %#lx instead of access denied\n", ResultHr);
    }
    else if (SetProcessDpiAwarenessContextFn != NULL)
    {
        Result = SetProcessDpiAwarenessContextFn(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
        if (Result) Result = SetProcessDpiAwarenessContextFn(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
        Error = GetLastError();
        ok(!Result, "The repeated process DPI setter succeeded\n");
        ok(Error == ERROR_ACCESS_DENIED, "The repeated process DPI setter returned error %lu\n", Error);
    }
    else
    {
        skip("No process DPI setter is exported\n");
    }

    if (SetProcessDpiAwarenessContextFn != NULL)
    {
        SetLastError(0xdeadbeef);
        Result = SetProcessDpiAwarenessContextFn(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
        Error = GetLastError();
        ok(!Result, "SetProcessDpiAwarenessContext succeeded after process awareness was fixed\n");
        ok(Error == ERROR_ACCESS_DENIED, "SetProcessDpiAwarenessContext returned error %lu instead of access denied\n", Error);
    }
}

static void
TestMonitorDpi(
    _In_ HMODULE User32,
    _In_opt_ HMODULE ShCore)
{
    PGETDPIFORMONITORINTERNAL GetDpiForMonitorInternalFn;
    PSHGETDPIFORMONITOR ShGetDpiForMonitorFn;
    HMONITOR Monitor;
    POINT Origin = { 0, 0 };
    HRESULT ResultHr;
    DWORD Error;
    UINT DpiX, DpiY;
    UINT Type;
    BOOL Result;

    GetDpiForMonitorInternalFn = (PGETDPIFORMONITORINTERNAL)GetProcAddress(User32, "GetDpiForMonitorInternal");
    ShGetDpiForMonitorFn = ShCore != NULL ? (PSHGETDPIFORMONITOR)GetProcAddress(ShCore, "GetDpiForMonitor") : NULL;
    if (GetDpiForMonitorInternalFn == NULL)
    {
        skip("GetDpiForMonitorInternal is not exported\n");
        return;
    }

    DpiX = DpiY = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    Result = GetDpiForMonitorInternalFn((HMONITOR)(ULONG_PTR)0xdeadbeef, MDT_EFFECTIVE_DPI, &DpiX, &DpiY);
    Error = GetLastError();
    ok(!Result, "GetDpiForMonitorInternal accepted an invalid monitor\n");
    ok(Error == ERROR_INVALID_HANDLE, "GetDpiForMonitorInternal returned error %lu\n", Error);

    if (ShGetDpiForMonitorFn != NULL)
    {
        ResultHr = ShGetDpiForMonitorFn((HMONITOR)(ULONG_PTR)0xdeadbeef, MDT_EFFECTIVE_DPI, &DpiX, &DpiY);
        ok(ResultHr == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE), "SHCore GetDpiForMonitor returned %#lx for an invalid monitor\n", ResultHr);
    }

    Monitor = MonitorFromPoint(Origin, MONITOR_DEFAULTTOPRIMARY);
    ok(Monitor != NULL, "MonitorFromPoint returned NULL\n");
    if (Monitor == NULL) return;

    for (Type = MDT_EFFECTIVE_DPI; Type <= MDT_RAW_DPI; ++Type)
    {
        DpiX = DpiY = 0;
        SetLastError(0xdeadbeef);
        Result = GetDpiForMonitorInternalFn(Monitor, Type, &DpiX, &DpiY);
        Error = GetLastError();
        ok(Result || (Type == MDT_RAW_DPI && Error == ERROR_NOT_SUPPORTED), "DPI type %u failed, error %lu\n", Type, Error);
        if (Result) ok(DpiX != 0 && DpiY != 0, "DPI type %u returned %u x %u\n", Type, DpiX, DpiY);
    }
}

static void
TestNonClientScaling(
    _In_ HMODULE User32)
{
    PENABLENONCLIENTDPISCALING EnableNonClientDpiScalingFn;
    DWORD Error;
    BOOL Result;

    EnableNonClientDpiScalingFn = (PENABLENONCLIENTDPISCALING)GetProcAddress(User32, "EnableNonClientDpiScaling");
    if (EnableNonClientDpiScalingFn == NULL)
    {
        skip("EnableNonClientDpiScaling is not exported\n");
        return;
    }

    SetLastError(0xdeadbeef);
    Result = EnableNonClientDpiScalingFn(NULL);
    Error = GetLastError();
    ok(!Result, "EnableNonClientDpiScaling(NULL) unexpectedly succeeded\n");
    ok(Error == ERROR_INVALID_WINDOW_HANDLE, "EnableNonClientDpiScaling(NULL) returned error %lu\n", Error);
}

START_TEST(Dpi)
{
    HMODULE User32;
    HMODULE ShCore;

    User32 = GetModuleHandleA("user32.dll");
    ok(User32 != NULL, "user32.dll is not loaded\n");
    if (User32 == NULL) return;

    ShCore = LoadLibraryA("shcore.dll");
    if (ShCore == NULL) skip("shcore.dll is not available\n");

    TestSizingApis(User32);
    TestMonitorDpi(User32, ShCore);
    TestNonClientScaling(User32);
    TestDpiContexts(User32, ShCore);

    if (ShCore != NULL) FreeLibrary(ShCore);
}
