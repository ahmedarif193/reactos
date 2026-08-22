/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SetWaitableTimerEx
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PSET_WAITABLE_TIMER_EX)(HANDLE, const LARGE_INTEGER *, LONG, PTIMERAPCROUTINE, LPVOID, PREASON_CONTEXT, ULONG);

static LONG ApcCount;

static VOID CALLBACK TimerApc(LPVOID Context, DWORD TimerLowValue, DWORD TimerHighValue)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(TimerLowValue);
    UNREFERENCED_PARAMETER(TimerHighValue);
    InterlockedIncrement(&ApcCount);
}

static VOID CheckCall(PSET_WAITABLE_TIMER_EX SetTimerEx, const char *Name, HANDLE Timer, const LARGE_INTEGER *DueTime, LONG Period, PREASON_CONTEXT WakeContext, ULONG TolerableDelay, BOOL ExpectedResult, DWORD ExpectedError)
{
    BOOL Result;

    SetLastError(0xdeadbeef);
    Result = SetTimerEx(Timer, DueTime, Period, NULL, NULL, WakeContext, TolerableDelay);
    ok(Result == ExpectedResult, "%s returned %u, expected %u\n", Name, Result, ExpectedResult);
    ok_long(GetLastError(), ExpectedError);
}

START_TEST(SetWaitableTimerEx)
{
    PSET_WAITABLE_TIMER_EX SetTimerEx;
    PSET_WAITABLE_TIMER_EX KernelBaseSetTimerEx;
    REASON_CONTEXT Reason;
    LARGE_INTEGER DueTime;
    HMODULE Kernel32;
    HMODULE KernelBase;
    HANDLE Timer;
    DWORD Wait;
    BOOL Result;

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    SetTimerEx = (PSET_WAITABLE_TIMER_EX)(PVOID)GetProcAddress(Kernel32, "SetWaitableTimerEx");
    ok(SetTimerEx != NULL, "SetWaitableTimerEx is not exported by kernel32\n");
    if (!SetTimerEx) return;

    KernelBase = LoadLibraryW(L"kernelbase.dll");
    ok(KernelBase != NULL, "Failed to load kernelbase.dll: %lu\n", GetLastError());
    KernelBaseSetTimerEx = KernelBase ? (PSET_WAITABLE_TIMER_EX)(PVOID)GetProcAddress(KernelBase, "SetWaitableTimerEx") : NULL;
    ok(KernelBaseSetTimerEx != NULL, "SetWaitableTimerEx is not exported by kernelbase\n");

    Timer = CreateWaitableTimerW(NULL, TRUE, NULL);
    ok(Timer != NULL, "Failed to create timer: %lu\n", GetLastError());
    if (!Timer) goto Cleanup;

    DueTime.QuadPart = -100000;
    CheckCall(SetTimerEx, "zero tolerance", Timer, &DueTime, 0, NULL, 0, TRUE, ERROR_SUCCESS);
    Wait = WaitForSingleObject(Timer, 2000);
    ok_long(Wait, WAIT_OBJECT_0);

    CancelWaitableTimer(Timer);
    DueTime.QuadPart = -100000;
    CheckCall(SetTimerEx, "nonzero tolerance", Timer, &DueTime, 0, NULL, 250, TRUE, ERROR_SUCCESS);
    Wait = WaitForSingleObject(Timer, 2000);
    ok_long(Wait, WAIT_OBJECT_0);

    CancelWaitableTimer(Timer);
    ZeroMemory(&Reason, sizeof(Reason));
    Reason.Version = POWER_REQUEST_CONTEXT_VERSION;
    Reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    Reason.Reason.SimpleReasonString = L"ReactOS timer parity";
    DueTime.QuadPart = -100000;
    CheckCall(SetTimerEx, "simple wake reason", Timer, &DueTime, 0, &Reason, 0, TRUE, ERROR_NOT_SUPPORTED);
    Wait = WaitForSingleObject(Timer, 2000);
    ok_long(Wait, WAIT_OBJECT_0);

    CancelWaitableTimer(Timer);
    ApcCount = 0;
    DueTime.QuadPart = -100000;
    SetLastError(0xdeadbeef);
    Result = SetTimerEx(Timer, &DueTime, 0, TimerApc, (PVOID)(ULONG_PTR)0x1234, NULL, 0);
    ok(Result, "APC timer failed: %lu\n", GetLastError());
    ok_long(GetLastError(), ERROR_SUCCESS);
    Wait = SleepEx(2000, TRUE);
    ok_long(Wait, WAIT_IO_COMPLETION);
    ok_long(ApcCount, 1);

    DueTime.QuadPart = -100000;
    CheckCall(SetTimerEx, "negative period", Timer, &DueTime, -1, NULL, 0, FALSE, ERROR_INVALID_PARAMETER);
    CheckCall(SetTimerEx, "invalid handle", (HANDLE)(ULONG_PTR)0x1234, &DueTime, 0, NULL, 0, FALSE, ERROR_INVALID_HANDLE);
    CheckCall(SetTimerEx, "null due time", Timer, NULL, 0, NULL, 0, FALSE, ERROR_INVALID_PARAMETER);

    ZeroMemory(&Reason, sizeof(Reason));
    Reason.Version = POWER_REQUEST_CONTEXT_VERSION + 1;
    Reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    Reason.Reason.SimpleReasonString = L"invalid version";
    CheckCall(SetTimerEx, "invalid reason version", Timer, &DueTime, 0, &Reason, 0, FALSE, ERROR_INVALID_PARAMETER);

    ZeroMemory(&Reason, sizeof(Reason));
    Reason.Version = POWER_REQUEST_CONTEXT_VERSION;
    Reason.Flags = 0x40000000;
    CheckCall(SetTimerEx, "invalid reason flags", Timer, &DueTime, 0, &Reason, 0, FALSE, ERROR_INVALID_PARAMETER);

    ZeroMemory(&Reason, sizeof(Reason));
    Reason.Version = POWER_REQUEST_CONTEXT_VERSION;
    CheckCall(SetTimerEx, "zero reason flags", Timer, &DueTime, 0, &Reason, 0, FALSE, ERROR_INVALID_PARAMETER);

    Reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    CheckCall(SetTimerEx, "null simple reason", Timer, &DueTime, 0, &Reason, 0, FALSE, ERROR_INVALID_PARAMETER);
    Reason.Reason.SimpleReasonString = L"";
    CheckCall(SetTimerEx, "empty simple reason", Timer, &DueTime, 0, &Reason, 0, FALSE, ERROR_INVALID_PARAMETER);
    Reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING | POWER_REQUEST_CONTEXT_DETAILED_STRING;
    Reason.Reason.SimpleReasonString = L"both flags";
    CheckCall(SetTimerEx, "combined reason flags", Timer, &DueTime, 0, &Reason, 0, FALSE, ERROR_INVALID_PARAMETER);

    ZeroMemory(&Reason, sizeof(Reason));
    Reason.Version = POWER_REQUEST_CONTEXT_VERSION;
    Reason.Flags = POWER_REQUEST_CONTEXT_DETAILED_STRING;
    CheckCall(SetTimerEx, "empty detailed reason", Timer, &DueTime, 0, &Reason, 0, TRUE, ERROR_NOT_SUPPORTED);
    CheckCall(SetTimerEx, "maximum tolerance", Timer, &DueTime, 0, NULL, ~0u, TRUE, ERROR_SUCCESS);

    if (KernelBaseSetTimerEx)
    {
        CancelWaitableTimer(Timer);
        DueTime.QuadPart = -100000;
        CheckCall(KernelBaseSetTimerEx, "kernelbase forward", Timer, &DueTime, 0, NULL, 0, TRUE, ERROR_SUCCESS);
        Wait = WaitForSingleObject(Timer, 2000);
        ok_long(Wait, WAIT_OBJECT_0);
    }

    CancelWaitableTimer(Timer);
    CloseHandle(Timer);

Cleanup:
    if (KernelBase) FreeLibrary(KernelBase);
}
