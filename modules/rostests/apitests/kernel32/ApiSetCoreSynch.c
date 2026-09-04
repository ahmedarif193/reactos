/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the api-ms-win-core-synch-l1-2-0 forwarder
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

/* Binaries built against the Windows 8 or later SDK import the address-wait
 * primitives from this API set instead of from kernel32.  The AMD OpenGL ICD
 * (atio6axx.dll) is one such binary.  No file of this name is shipped: the
 * loader's API set resolution redirects the name to kernelbase, so the test
 * covers that the redirection happens and that the functions behave. */

#define API_SET_NAME L"api-ms-win-core-synch-l1-2-0.dll"

typedef BOOL (WINAPI *PWAIT_ON_ADDRESS)(volatile VOID *, PVOID, SIZE_T, DWORD);
typedef VOID (WINAPI *PWAKE_BY_ADDRESS)(PVOID);

static PWAIT_ON_ADDRESS pWaitOnAddress;
static PWAKE_BY_ADDRESS pWakeByAddressSingle;
static PWAKE_BY_ADDRESS pWakeByAddressAll;

static volatile LONG SharedValue;

static DWORD WINAPI WakeThread(LPVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    Sleep(50);
    InterlockedExchange(&SharedValue, 1);
    pWakeByAddressSingle((PVOID)&SharedValue);
    return 0;
}

START_TEST(ApiSetCoreSynch)
{
    HMODULE hApiSet, hKernelBase;
    LONG Compare;
    BOOL Success;
    DWORD Error, WaitResult;
    HANDLE Thread;

    hApiSet = LoadLibraryW(API_SET_NAME);
    if (hApiSet == NULL)
    {
        skip("%S does not resolve (error %lu)\n", API_SET_NAME, GetLastError());
        return;
    }

    pWaitOnAddress = (PWAIT_ON_ADDRESS)GetProcAddress(hApiSet, "WaitOnAddress");
    pWakeByAddressSingle = (PWAKE_BY_ADDRESS)GetProcAddress(hApiSet, "WakeByAddressSingle");
    pWakeByAddressAll = (PWAKE_BY_ADDRESS)GetProcAddress(hApiSet, "WakeByAddressAll");

    ok(pWaitOnAddress != NULL, "WaitOnAddress did not resolve\n");
    ok(pWakeByAddressSingle != NULL, "WakeByAddressSingle did not resolve\n");
    ok(pWakeByAddressAll != NULL, "WakeByAddressAll did not resolve\n");

    if (pWaitOnAddress == NULL || pWakeByAddressSingle == NULL)
    {
        FreeLibrary(hApiSet);
        return;
    }

    /* The redirection has to land on the same implementation kernelbase
     * exports, not on a private copy. */
    hKernelBase = GetModuleHandleW(L"kernelbase.dll");
    if (hKernelBase != NULL)
    {
        ok(GetProcAddress(hKernelBase, "WaitOnAddress") == (FARPROC)pWaitOnAddress,
           "WaitOnAddress did not redirect to kernelbase\n");
    }

    /* A wait whose compare value already differs returns at once. */
    InterlockedExchange(&SharedValue, 7);
    Compare = 0;
    SetLastError(0xdeadbeef);
    Success = pWaitOnAddress(&SharedValue, &Compare, sizeof(Compare), 0);
    ok(Success, "WaitOnAddress failed on an already changed value, error %lu\n",
       GetLastError());

    /* A wait on an unchanged value honours the timeout. */
    InterlockedExchange(&SharedValue, 7);
    Compare = 7;
    SetLastError(0xdeadbeef);
    Success = pWaitOnAddress(&SharedValue, &Compare, sizeof(Compare), 100);
    Error = GetLastError();
    ok(!Success, "WaitOnAddress succeeded without a wake\n");
    ok(Error == ERROR_TIMEOUT, "expected ERROR_TIMEOUT, got %lu\n", Error);

    /* A wait woken by another thread returns success. */
    InterlockedExchange(&SharedValue, 0);
    Thread = CreateThread(NULL, 0, WakeThread, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed, error %lu\n", GetLastError());
    if (Thread != NULL)
    {
        Compare = 0;
        SetLastError(0xdeadbeef);
        Success = pWaitOnAddress(&SharedValue, &Compare, sizeof(Compare), 5000);
        ok(Success, "WaitOnAddress was not woken, error %lu\n", GetLastError());
        ok(SharedValue == 1, "expected the woken value 1, got %ld\n", SharedValue);

        WaitResult = WaitForSingleObject(Thread, 5000);
        ok(WaitResult == WAIT_OBJECT_0, "the wake thread did not exit, result %lu\n",
           WaitResult);
        CloseHandle(Thread);
    }

    FreeLibrary(hApiSet);
}
