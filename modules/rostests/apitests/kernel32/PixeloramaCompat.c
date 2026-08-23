/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pixelorama loader and compatibility API coverage
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#ifdef PIXELORAMA_COMPAT_STANDALONE
#include <apitest.h>
#include <windows.h>
#else
#include "precomp.h"
#endif

#include <winuser.h>

#ifndef POWER_REQUEST_CONTEXT_VERSION
#define POWER_REQUEST_CONTEXT_VERSION 0
#define POWER_REQUEST_CONTEXT_SIMPLE_STRING 0x00000001
typedef enum _POWER_REQUEST_TYPE
{
    PowerRequestDisplayRequired,
    PowerRequestSystemRequired,
    PowerRequestAwayModeRequired
} POWER_REQUEST_TYPE;
#endif

typedef DLL_DIRECTORY_COOKIE (WINAPI *PADD_DLL_DIRECTORY)(PCWSTR);
typedef BOOL (WINAPI *PREMOVE_DLL_DIRECTORY)(DLL_DIRECTORY_COOKIE);
typedef HANDLE (WINAPI *PPOWER_CREATE_REQUEST)(PREASON_CONTEXT);
typedef BOOL (WINAPI *PPOWER_CHANGE_REQUEST)(HANDLE, POWER_REQUEST_TYPE);
typedef BOOL (WINAPI *PPOINT_FOR_PER_MONITOR_DPI)(HWND, POINT *);

static void
TestDllDirectory(
    PADD_DLL_DIRECTORY pAddDllDirectory,
    PREMOVE_DLL_DIRECTORY pRemoveDllDirectory)
{
    WCHAR TempPath[MAX_PATH], Directory[MAX_PATH], Source[MAX_PATH], Target[MAX_PATH];
    DLL_DIRECTORY_COOKIE Cookie;
    HMODULE Module;
    UINT Length;
    BOOL Success;

    Length = GetTempPathW(ARRAYSIZE(TempPath), TempPath);
    ok(Length && Length < ARRAYSIZE(TempPath), "GetTempPathW failed: %lu\n", GetLastError());
    if (!Length || Length >= ARRAYSIZE(TempPath)) return;

    Success = GetTempFileNameW(TempPath, L"pxr", 0, Directory);
    ok(Success, "GetTempFileNameW failed: %lu\n", GetLastError());
    if (!Success) return;
    DeleteFileW(Directory);
    Success = CreateDirectoryW(Directory, NULL);
    ok(Success, "CreateDirectoryW failed: %lu\n", GetLastError());
    if (!Success) return;

    Length = GetSystemDirectoryW(Source, ARRAYSIZE(Source));
    ok(Length && Length < ARRAYSIZE(Source) - 12, "GetSystemDirectoryW failed: %lu\n", GetLastError());
    if (!Length || Length >= ARRAYSIZE(Source) - 12) goto CleanupDirectory;
    lstrcatW(Source, L"\\version.dll");
    lstrcpyW(Target, Directory);
    lstrcatW(Target, L"\\pixelorama-probe.dll");
    Success = CopyFileW(Source, Target, FALSE);
    ok(Success, "CopyFileW failed: %lu\n", GetLastError());
    if (!Success) goto CleanupDirectory;

    SetLastError(0xdeadbeef);
    Cookie = pAddDllDirectory(Directory);
    ok(Cookie != NULL, "AddDllDirectory failed: %lu\n", GetLastError());
    if (!Cookie) goto CleanupFile;

    SetLastError(0xdeadbeef);
    Module = LoadLibraryExW(L"pixelorama-probe.dll", NULL, LOAD_LIBRARY_SEARCH_USER_DIRS);
    ok(Module != NULL, "LoadLibraryExW from an added directory failed: %lu\n", GetLastError());
    if (Module) FreeLibrary(Module);

    SetLastError(0xdeadbeef);
    Success = pRemoveDllDirectory(Cookie);
    ok(Success, "RemoveDllDirectory failed: %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Module = LoadLibraryExW(L"pixelorama-probe.dll", NULL, LOAD_LIBRARY_SEARCH_USER_DIRS);
    ok(Module == NULL, "LoadLibraryExW still found a removed directory\n");
    if (Module) FreeLibrary(Module);

CleanupFile:
    DeleteFileW(Target);
CleanupDirectory:
    RemoveDirectoryW(Directory);
}

static void
TestPowerRequests(
    PPOWER_CREATE_REQUEST pPowerCreateRequest,
    PPOWER_CHANGE_REQUEST pPowerSetRequest,
    PPOWER_CHANGE_REQUEST pPowerClearRequest)
{
    REASON_CONTEXT Context = {0};
    HANDLE Request;
    BOOL Success;

    Context.Version = POWER_REQUEST_CONTEXT_VERSION;
    Context.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    Context.Reason.SimpleReasonString = L"ReactOS Pixelorama compatibility test";

    SetLastError(0xdeadbeef);
    Request = pPowerCreateRequest(&Context);
    ok(Request != NULL && Request != INVALID_HANDLE_VALUE, "PowerCreateRequest failed: handle %p, error %lu\n", Request, GetLastError());
    if (Request == NULL || Request == INVALID_HANDLE_VALUE) return;

    SetLastError(0xdeadbeef);
    Success = pPowerSetRequest(Request, PowerRequestDisplayRequired);
    ok(Success, "PowerSetRequest(DisplayRequired) failed: %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Success = pPowerClearRequest(Request, PowerRequestDisplayRequired);
    ok(Success, "PowerClearRequest(DisplayRequired) failed: %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Success = pPowerSetRequest(Request, PowerRequestSystemRequired);
    ok(Success, "PowerSetRequest(SystemRequired) failed: %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Success = pPowerClearRequest(Request, PowerRequestSystemRequired);
    ok(Success, "PowerClearRequest(SystemRequired) failed: %lu\n", GetLastError());

    CloseHandle(Request);
}

static void
TestDpiPoints(
    PPOINT_FOR_PER_MONITOR_DPI pLogicalToPhysicalPointForPerMonitorDPI,
    PPOINT_FOR_PER_MONITOR_DPI pPhysicalToLogicalPointForPerMonitorDPI)
{
    HWND Desktop;
    RECT Rect;
    POINT Original, Point;
    BOOL Success;

    Desktop = GetDesktopWindow();
    Success = GetWindowRect(Desktop, &Rect);
    ok(Success, "GetWindowRect failed: %lu\n", GetLastError());
    if (!Success) return;

    Original.x = Rect.left + (Rect.right - Rect.left) / 2;
    Original.y = Rect.top + (Rect.bottom - Rect.top) / 2;
    Point = Original;
    Success = pPhysicalToLogicalPointForPerMonitorDPI(Desktop, &Point);
    ok(Success, "PhysicalToLogicalPointForPerMonitorDPI failed: %lu\n", GetLastError());
    if (!Success) return;

    Success = pLogicalToPhysicalPointForPerMonitorDPI(Desktop, &Point);
    ok(Success, "LogicalToPhysicalPointForPerMonitorDPI failed: %lu\n", GetLastError());
    ok(Point.x == Original.x && Point.y == Original.y, "DPI round trip returned %ld,%ld instead of %ld,%ld\n", Point.x, Point.y, Original.x, Original.y);
}

START_TEST(PixeloramaCompat)
{
    HMODULE Kernel32, Shlwapi, User32;
    PADD_DLL_DIRECTORY pAddDllDirectory;
    PREMOVE_DLL_DIRECTORY pRemoveDllDirectory;
    PPOWER_CREATE_REQUEST pPowerCreateRequest;
    PPOWER_CHANGE_REQUEST pPowerSetRequest, pPowerClearRequest;
    PPOINT_FOR_PER_MONITOR_DPI pLogicalToPhysicalPointForPerMonitorDPI;
    PPOINT_FOR_PER_MONITOR_DPI pPhysicalToLogicalPointForPerMonitorDPI;

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    Shlwapi = LoadLibraryW(L"shlwapi.dll");
    User32 = GetModuleHandleW(L"user32.dll");
    pAddDllDirectory = (PADD_DLL_DIRECTORY)GetProcAddress(Kernel32, "AddDllDirectory");
    pRemoveDllDirectory = (PREMOVE_DLL_DIRECTORY)GetProcAddress(Kernel32, "RemoveDllDirectory");
    pPowerCreateRequest = (PPOWER_CREATE_REQUEST)GetProcAddress(Kernel32, "PowerCreateRequest");
    pPowerSetRequest = (PPOWER_CHANGE_REQUEST)GetProcAddress(Kernel32, "PowerSetRequest");
    pPowerClearRequest = (PPOWER_CHANGE_REQUEST)GetProcAddress(Kernel32, "PowerClearRequest");
    pLogicalToPhysicalPointForPerMonitorDPI = (PPOINT_FOR_PER_MONITOR_DPI)GetProcAddress(User32, "LogicalToPhysicalPointForPerMonitorDPI");
    pPhysicalToLogicalPointForPerMonitorDPI = (PPOINT_FOR_PER_MONITOR_DPI)GetProcAddress(User32, "PhysicalToLogicalPointForPerMonitorDPI");

    ok(pAddDllDirectory != NULL, "KERNEL32!AddDllDirectory is not exported\n");
    ok(pRemoveDllDirectory != NULL, "KERNEL32!RemoveDllDirectory is not exported\n");
    ok(pPowerCreateRequest != NULL, "KERNEL32!PowerCreateRequest is not exported\n");
    ok(pPowerSetRequest != NULL, "KERNEL32!PowerSetRequest is not exported\n");
    ok(pPowerClearRequest != NULL, "KERNEL32!PowerClearRequest is not exported\n");
    ok(Shlwapi != NULL, "LoadLibraryW(shlwapi.dll) failed: %lu\n", GetLastError());
    if (Shlwapi) ok(GetProcAddress(Shlwapi, "QISearch") != NULL, "SHLWAPI!QISearch is not exported by name\n");
    ok(pLogicalToPhysicalPointForPerMonitorDPI != NULL, "USER32!LogicalToPhysicalPointForPerMonitorDPI is not exported\n");
    ok(pPhysicalToLogicalPointForPerMonitorDPI != NULL, "USER32!PhysicalToLogicalPointForPerMonitorDPI is not exported\n");

    if (pAddDllDirectory && pRemoveDllDirectory) TestDllDirectory(pAddDllDirectory, pRemoveDllDirectory);
    if (pPowerCreateRequest && pPowerSetRequest && pPowerClearRequest) TestPowerRequests(pPowerCreateRequest, pPowerSetRequest, pPowerClearRequest);
    if (pLogicalToPhysicalPointForPerMonitorDPI && pPhysicalToLogicalPointForPerMonitorDPI) TestDpiPoints(pLogicalToPhysicalPointForPerMonitorDPI, pPhysicalToLogicalPointForPerMonitorDPI);
    if (Shlwapi) FreeLibrary(Shlwapi);
}
