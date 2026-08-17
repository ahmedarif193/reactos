/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests condition-variable wake handoff behavior
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define HANDOFF_ROUNDS 256
#define HANDOFF_TIMEOUT_100NS (-10000000LL)
#define POLL_TIMEOUT_MS 5000

typedef VOID (NTAPI *PFN_RTL_INITIALIZE_CONDITION_VARIABLE)(PRTL_CONDITION_VARIABLE);
typedef VOID (NTAPI *PFN_RTL_WAKE_ALL_CONDITION_VARIABLE)(PRTL_CONDITION_VARIABLE);
typedef NTSTATUS (NTAPI *PFN_RTL_SLEEP_CONDITION_VARIABLE_SRW)(PRTL_CONDITION_VARIABLE, PRTL_SRWLOCK, PLARGE_INTEGER, ULONG);
typedef VOID (NTAPI *PFN_RTL_INITIALIZE_SRW_LOCK)(PRTL_SRWLOCK);
typedef VOID (NTAPI *PFN_RTL_ACQUIRE_SRW_LOCK_EXCLUSIVE)(PRTL_SRWLOCK);
typedef VOID (NTAPI *PFN_RTL_RELEASE_SRW_LOCK_EXCLUSIVE)(PRTL_SRWLOCK);

static PFN_RTL_WAKE_ALL_CONDITION_VARIABLE pRtlWakeAllConditionVariable;
static PFN_RTL_SLEEP_CONDITION_VARIABLE_SRW pRtlSleepConditionVariableSRW;
static PFN_RTL_ACQUIRE_SRW_LOCK_EXCLUSIVE pRtlAcquireSRWLockExclusive;
static PFN_RTL_RELEASE_SRW_LOCK_EXCLUSIVE pRtlReleaseSRWLockExclusive;
static RTL_CONDITION_VARIABLE HandoffCondition;
static RTL_SRWLOCK HandoffLock;
static volatile LONG HandoffGeneration;
static volatile LONG HandoffReady;
static volatile LONG HandoffCompleted;
static volatile LONG HandoffTimeouts;
static volatile LONG HandoffFailures;
static volatile LONG HandoffStop;

static
BOOLEAN
WaitForHandoffValue(
    _In_ volatile LONG *Value,
    _In_ LONG Expected)
{
    ULONG Start = GetTickCount();

    while (InterlockedCompareExchange((volatile LONG *)Value, 0, 0) < Expected)
    {
        if (GetTickCount() - Start >= POLL_TIMEOUT_MS)
            return FALSE;

        Sleep(0);
    }

    return TRUE;
}

static
DWORD
WINAPI
HandoffWaiter(
    _In_ LPVOID Parameter)
{
    LONG Round;

    UNREFERENCED_PARAMETER(Parameter);

    for (Round = 1; Round <= HANDOFF_ROUNDS; ++Round)
    {
        LARGE_INTEGER Timeout;
        NTSTATUS Status;

        pRtlAcquireSRWLockExclusive(&HandoffLock);
        InterlockedExchange(&HandoffReady, Round);

        while (InterlockedCompareExchange(&HandoffGeneration, 0, 0) < Round && !InterlockedCompareExchange(&HandoffStop, 0, 0))
        {
            Timeout.QuadPart = HANDOFF_TIMEOUT_100NS;
            Status = pRtlSleepConditionVariableSRW(&HandoffCondition, &HandoffLock, &Timeout, 0);
            if (Status == STATUS_TIMEOUT)
                InterlockedIncrement(&HandoffTimeouts);
            else if (Status != STATUS_SUCCESS)
                InterlockedIncrement(&HandoffFailures);
        }

        InterlockedExchange(&HandoffCompleted, Round);
        pRtlReleaseSRWLockExclusive(&HandoffLock);

        if (InterlockedCompareExchange(&HandoffStop, 0, 0))
            break;
    }

    return 0;
}

START_TEST(RtlConditionVariable)
{
    PFN_RTL_INITIALIZE_CONDITION_VARIABLE pRtlInitializeConditionVariable;
    PFN_RTL_INITIALIZE_SRW_LOCK pRtlInitializeSRWLock;
    HMODULE Ntdll;
    HANDLE Thread;
    DWORD WaitStatus;
    LONG Round;

    Ntdll = GetModuleHandleW(L"ntdll.dll");
    ok(Ntdll != NULL, "GetModuleHandleW failed: %lu\n", GetLastError());
    if (!Ntdll)
        return;

    pRtlInitializeConditionVariable = (PFN_RTL_INITIALIZE_CONDITION_VARIABLE)GetProcAddress(Ntdll, "RtlInitializeConditionVariable");
    pRtlWakeAllConditionVariable = (PFN_RTL_WAKE_ALL_CONDITION_VARIABLE)GetProcAddress(Ntdll, "RtlWakeAllConditionVariable");
    pRtlSleepConditionVariableSRW = (PFN_RTL_SLEEP_CONDITION_VARIABLE_SRW)GetProcAddress(Ntdll, "RtlSleepConditionVariableSRW");
    pRtlInitializeSRWLock = (PFN_RTL_INITIALIZE_SRW_LOCK)GetProcAddress(Ntdll, "RtlInitializeSRWLock");
    pRtlAcquireSRWLockExclusive = (PFN_RTL_ACQUIRE_SRW_LOCK_EXCLUSIVE)GetProcAddress(Ntdll, "RtlAcquireSRWLockExclusive");
    pRtlReleaseSRWLockExclusive = (PFN_RTL_RELEASE_SRW_LOCK_EXCLUSIVE)GetProcAddress(Ntdll, "RtlReleaseSRWLockExclusive");
    if (!pRtlInitializeConditionVariable || !pRtlWakeAllConditionVariable || !pRtlSleepConditionVariableSRW || !pRtlInitializeSRWLock || !pRtlAcquireSRWLockExclusive || !pRtlReleaseSRWLockExclusive)
    {
        skip("Condition-variable or SRW-lock exports are unavailable\n");
        return;
    }

    pRtlInitializeConditionVariable(&HandoffCondition);
    pRtlInitializeSRWLock(&HandoffLock);
    HandoffGeneration = 0;
    HandoffReady = 0;
    HandoffCompleted = 0;
    HandoffTimeouts = 0;
    HandoffFailures = 0;
    HandoffStop = 0;

    Thread = CreateThread(NULL, 0, HandoffWaiter, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        return;

    for (Round = 1; Round <= HANDOFF_ROUNDS; ++Round)
    {
        if (!WaitForHandoffValue(&HandoffReady, Round))
        {
            ok(0, "waiter did not publish round %ld\n", Round);
            break;
        }

        pRtlAcquireSRWLockExclusive(&HandoffLock);
        InterlockedExchange(&HandoffGeneration, Round);
        pRtlWakeAllConditionVariable(&HandoffCondition);
        pRtlReleaseSRWLockExclusive(&HandoffLock);

        if (!WaitForHandoffValue(&HandoffCompleted, Round))
        {
            ok(0, "waiter did not complete round %ld\n", Round);
            break;
        }
    }

    if (Round <= HANDOFF_ROUNDS)
    {
        InterlockedExchange(&HandoffStop, 1);
        pRtlAcquireSRWLockExclusive(&HandoffLock);
        InterlockedExchange(&HandoffGeneration, HANDOFF_ROUNDS);
        pRtlWakeAllConditionVariable(&HandoffCondition);
        pRtlReleaseSRWLockExclusive(&HandoffLock);
    }

    WaitStatus = WaitForSingleObject(Thread, POLL_TIMEOUT_MS);
    ok_long(WaitStatus, WAIT_OBJECT_0);
    ok_long(HandoffFailures, 0);
    ok_long(HandoffTimeouts, 0);
    ok_long(HandoffCompleted, HANDOFF_ROUNDS);
    CloseHandle(Thread);
}
