/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Torture tests for GetOsSafeBootMode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

typedef BOOL (WINAPI *FN_GetOsSafeBootMode)(PDWORD);

static FN_GetOsSafeBootMode pGetOsSafeBootMode;
static const char *pszHostDll;

static DWORD
ReadSafeBootOptionValue(_Out_ PBOOL pbPresent)
{
    HKEY hKey;
    DWORD dwValue = 0, cbData = sizeof(dwValue), dwType = 0;
    LONG err;

    *pbPresent = FALSE;

    err = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                        L"SYSTEM\\CurrentControlSet\\Control\\SafeBoot\\Option",
                        0, KEY_QUERY_VALUE, &hKey);
    if (err != ERROR_SUCCESS)
        return 0;

    err = RegQueryValueExW(hKey, L"OptionValue", NULL, &dwType, (LPBYTE)&dwValue, &cbData);
    RegCloseKey(hKey);

    if (err != ERROR_SUCCESS || dwType != REG_DWORD)
        return 0;

    *pbPresent = TRUE;
    return dwValue;
}

static void
Resolve(void)
{
    static const struct
    {
        const WCHAR *pszDll;
        const char *pszName;
    } Hosts[] =
    {
        { L"kernelbase.dll",                     "kernelbase.dll" },
        { L"api-ms-win-core-sysinfo-l1-2-1.dll", "api-ms-win-core-sysinfo-l1-2-1" },
        { L"kernel32.dll",                       "kernel32.dll" },
    };
    UINT i;

    for (i = 0; i < ARRAYSIZE(Hosts); i++)
    {
        HMODULE hMod = LoadLibraryW(Hosts[i].pszDll);
        FARPROC fp;

        if (!hMod)
            continue;

        fp = GetProcAddress(hMod, "GetOsSafeBootMode");
        if (fp)
        {
            pGetOsSafeBootMode = (FN_GetOsSafeBootMode)fp;
            pszHostDll = Hosts[i].pszName;
            trace("GetOsSafeBootMode resolved from %s\n", pszHostDll);
            return;
        }
        FreeLibrary(hMod);
    }
}

static void
Test_BadPointers(void)
{
    BOOL ret;

    SetLastError(0xdeadbeef);
    ret = TRUE;
    _SEH2_TRY
    {
        ret = pGetOsSafeBootMode(NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("NULL raised 0x%08lx\n", _SEH2_GetExceptionCode());
        ret = FALSE;
    }
    _SEH2_END;
    ok(!ret, "GetOsSafeBootMode(NULL) must not report success\n");

    ret = TRUE;
    _SEH2_TRY
    {
        ret = pGetOsSafeBootMode((PDWORD)InvalidPointer);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("bogus pointer raised 0x%08lx\n", _SEH2_GetExceptionCode());
        ret = FALSE;
    }
    _SEH2_END;
    ok(!ret, "a bogus pointer must not report success\n");
}

static void
Test_Value(void)
{
    DWORD dwFlags, dwOptionValue;
    BOOL bPresent;

    dwFlags = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ok(pGetOsSafeBootMode(&dwFlags), "GetOsSafeBootMode failed, error %lu\n", GetLastError());
    ok(dwFlags != 0xdeadbeef, "GetOsSafeBootMode must write the output\n");
    trace("safe boot mode = %lu\n", dwFlags);
    ok(dwFlags <= 3, "unexpected safe boot mode %lu\n", dwFlags);

    dwOptionValue = ReadSafeBootOptionValue(&bPresent);
    if (bPresent)
    {
        ok(dwFlags == dwOptionValue,
           "GetOsSafeBootMode returned %lu but SafeBoot\\Option\\OptionValue is %lu\n",
           dwFlags, dwOptionValue);
    }
    else
    {
        ok(dwFlags == 0,
           "no SafeBoot OptionValue present, so the mode must be 0, got %lu\n", dwFlags);
    }

    ok((*(BOOLEAN *)(ULONG_PTR)(KUSER_SHARED_DATA_UMPTR + 0x2EC) != 0) == (dwFlags != 0),
       "KUSER_SHARED_DATA.SafeBootMode disagrees with GetOsSafeBootMode (%lu)\n", dwFlags);
}

static void
Test_Stability(void)
{
    DWORD dwFirst = 0, dwFlags;
    UINT i;
    LONG Bad = 0;

    ok(pGetOsSafeBootMode(&dwFirst), "first call failed, error %lu\n", GetLastError());

    for (i = 0; i < 20000; i++)
    {
        dwFlags = 0xdeadbeef;
        if (!pGetOsSafeBootMode(&dwFlags) || dwFlags != dwFirst)
        {
            Bad++;
            break;
        }
    }
    ok(Bad == 0, "the safe boot mode drifted after %u calls\n", i);
}

static DWORD WINAPI
HammerThread(LPVOID pv)
{
    LONG *pFailures = (LONG *)pv;
    DWORD dwFlags;
    UINT i;

    for (i = 0; i < 10000; i++)
    {
        dwFlags = 0xdeadbeef;
        if (!pGetOsSafeBootMode(&dwFlags) || dwFlags == 0xdeadbeef)
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
        hThreads[i] = CreateThread(NULL, 0, HammerThread, &Failures, 0, NULL);

    WaitForMultipleObjects(ARRAYSIZE(hThreads), hThreads, TRUE, 60000);
    for (i = 0; i < ARRAYSIZE(hThreads); i++)
    {
        if (hThreads[i])
            CloseHandle(hThreads[i]);
    }
    ok(Failures == 0, "%ld threaded iterations misbehaved\n", Failures);
}

START_TEST(GetOsSafeBootMode)
{
    Resolve();
    ok(pGetOsSafeBootMode != NULL,
       "GetOsSafeBootMode is exported by neither kernelbase, the sysinfo apiset, nor kernel32\n");
    if (!pGetOsSafeBootMode)
        return;

    Test_BadPointers();
    Test_Value();
    Test_Stability();
    Test_Threaded();
}
