/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64-on-ARM64 network-provider loader parity test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winnetwk.h>
#include <stdarg.h>
#include <stdio.h>

typedef DWORD (WINAPI *PWNET_OPEN_ENUM_W)(DWORD Scope, DWORD Type, DWORD Usage, LPNETRESOURCEW Resource, LPHANDLE Enum);
typedef DWORD (WINAPI *PWNET_CLOSE_ENUM)(HANDLE Enum);
typedef DWORD (WINAPI *PWNET_GET_CONNECTION_W)(LPCWSTR LocalName, LPWSTR RemoteName, LPDWORD Length);

static int
log_result(PCSTR Format, ...)
{
    CHAR Buffer[512];
    va_list Arguments;
    int Length;

    va_start(Arguments, Format);
    Length = vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);
    va_end(Arguments);

    Buffer[sizeof(Buffer) - 1] = ANSI_NULL;
    fputs(Buffer, stdout);
    OutputDebugStringA(Buffer);
    return Length;
}

#define printf log_result

int
main(void)
{
    HMODULE Mpr;
    PWNET_OPEN_ENUM_W WNetOpenEnumWDynamic;
    PWNET_CLOSE_ENUM WNetCloseEnumDynamic;
    PWNET_GET_CONNECTION_W WNetGetConnectionWDynamic;
    WCHAR RemoteName[256];
    DWORD Length, OpenResult, CloseResult, ConnectionResult;
    HANDLE Enum;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_MPR_PROVIDER_TEST_BEGIN\n");

    Mpr = LoadLibraryW(L"mpr.dll");
    if (!Mpr)
    {
        printf("FAIL LoadLibrary mpr error=%lu\n", GetLastError());
        return 1;
    }

    WNetOpenEnumWDynamic = (PWNET_OPEN_ENUM_W)GetProcAddress(Mpr, "WNetOpenEnumW");
    WNetCloseEnumDynamic = (PWNET_CLOSE_ENUM)GetProcAddress(Mpr, "WNetCloseEnum");
    WNetGetConnectionWDynamic = (PWNET_GET_CONNECTION_W)GetProcAddress(Mpr, "WNetGetConnectionW");
    if (!WNetOpenEnumWDynamic || !WNetCloseEnumDynamic || !WNetGetConnectionWDynamic)
    {
        printf("FAIL GetProcAddress open=%p close=%p connection=%p error=%lu\n", WNetOpenEnumWDynamic, WNetCloseEnumDynamic, WNetGetConnectionWDynamic, GetLastError());
        return 2;
    }

    Enum = NULL;
    SetLastError(0xdeadbeef);
    OpenResult = WNetOpenEnumWDynamic(RESOURCE_GLOBALNET, RESOURCETYPE_ANY, 0, NULL, &Enum);
    printf("CALL WNetOpenEnumW result=%lu error=%lu handle=%p\n", OpenResult, GetLastError(), Enum);
    if (OpenResult == NO_ERROR)
    {
        CloseResult = WNetCloseEnumDynamic(Enum);
        printf("CALL WNetCloseEnum result=%lu\n", CloseResult);
    }

    RemoteName[0] = UNICODE_NULL;
    Length = ARRAYSIZE(RemoteName);
    SetLastError(0xdeadbeef);
    ConnectionResult = WNetGetConnectionWDynamic(L"D:", RemoteName, &Length);
    printf("CALL WNetGetConnectionW result=%lu error=%lu length=%lu remote=%ls\n", ConnectionResult, GetLastError(), Length, RemoteName);

    printf("CHPE_MPR_PROVIDER_TEST_PASS\n");
    return 0;
}
