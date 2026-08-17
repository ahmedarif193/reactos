/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Portable AMD64-on-ARM64 condition-variable handoff parity probe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#define HANDOFF_ROUNDS 256
#define HANDOFF_TIMEOUT_MS 1000
#define TEST_TIMEOUT_MS 5000

typedef VOID (WINAPI *PINITIALIZE_CONDITION_VARIABLE)(PCONDITION_VARIABLE);
typedef VOID (WINAPI *PWAKE_ALL_CONDITION_VARIABLE)(PCONDITION_VARIABLE);
typedef BOOL (WINAPI *PSLEEP_CONDITION_VARIABLE_SRW)(PCONDITION_VARIABLE, PSRWLOCK, DWORD, ULONG);
typedef VOID (WINAPI *PINITIALIZE_SRW_LOCK)(PSRWLOCK);
typedef VOID (WINAPI *PACQUIRE_SRW_LOCK_EXCLUSIVE)(PSRWLOCK);
typedef VOID (WINAPI *PRELEASE_SRW_LOCK_EXCLUSIVE)(PSRWLOCK);

static PWAKE_ALL_CONDITION_VARIABLE WakeAllConditionVariablePtr;
static PSLEEP_CONDITION_VARIABLE_SRW SleepConditionVariableSRWPtr;
static PACQUIRE_SRW_LOCK_EXCLUSIVE AcquireSRWLockExclusivePtr;
static PRELEASE_SRW_LOCK_EXCLUSIVE ReleaseSRWLockExclusivePtr;
static CONDITION_VARIABLE HandoffCondition;
static SRWLOCK HandoffLock;
static volatile LONG HandoffGeneration;
static volatile LONG HandoffReady;
static volatile LONG HandoffCompleted;
static volatile LONG HandoffTimeouts;
static volatile LONG HandoffFailures;
static volatile LONG HandoffStop;

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

static BOOL
wait_for_value(
    volatile LONG *Value,
    LONG Expected)
{
    DWORD Start = GetTickCount();

    while (InterlockedCompareExchange(Value, 0, 0) < Expected)
    {
        if (GetTickCount() - Start >= TEST_TIMEOUT_MS)
            return FALSE;

        Sleep(0);
    }

    return TRUE;
}

static DWORD WINAPI
handoff_waiter(PVOID Parameter)
{
    LONG Round;

    UNREFERENCED_PARAMETER(Parameter);

    for (Round = 1; Round <= HANDOFF_ROUNDS; ++Round)
    {
        BOOL Result;

        AcquireSRWLockExclusivePtr(&HandoffLock);
        InterlockedExchange(&HandoffReady, Round);

        while (InterlockedCompareExchange(&HandoffGeneration, 0, 0) < Round && !InterlockedCompareExchange(&HandoffStop, 0, 0))
        {
            Result = SleepConditionVariableSRWPtr(&HandoffCondition, &HandoffLock, HANDOFF_TIMEOUT_MS, 0);
            if (!Result && GetLastError() == ERROR_TIMEOUT)
                InterlockedIncrement(&HandoffTimeouts);
            else if (!Result)
                InterlockedIncrement(&HandoffFailures);
        }

        InterlockedExchange(&HandoffCompleted, Round);
        ReleaseSRWLockExclusivePtr(&HandoffLock);

        if (InterlockedCompareExchange(&HandoffStop, 0, 0))
            break;
    }

    return 0;
}

int
main(void)
{
    PINITIALIZE_CONDITION_VARIABLE InitializeConditionVariablePtr;
    PINITIALIZE_SRW_LOCK InitializeSRWLockPtr;
    HMODULE Kernel32;
    HANDLE Thread;
    DWORD WaitStatus;
    LONG Round;
    INT Failed;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_CONDVAR_HANDOFF_BEGIN\n");

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    InitializeConditionVariablePtr = Kernel32 ? (PINITIALIZE_CONDITION_VARIABLE)GetProcAddress(Kernel32, "InitializeConditionVariable") : NULL;
    WakeAllConditionVariablePtr = Kernel32 ? (PWAKE_ALL_CONDITION_VARIABLE)GetProcAddress(Kernel32, "WakeAllConditionVariable") : NULL;
    SleepConditionVariableSRWPtr = Kernel32 ? (PSLEEP_CONDITION_VARIABLE_SRW)GetProcAddress(Kernel32, "SleepConditionVariableSRW") : NULL;
    InitializeSRWLockPtr = Kernel32 ? (PINITIALIZE_SRW_LOCK)GetProcAddress(Kernel32, "InitializeSRWLock") : NULL;
    AcquireSRWLockExclusivePtr = Kernel32 ? (PACQUIRE_SRW_LOCK_EXCLUSIVE)GetProcAddress(Kernel32, "AcquireSRWLockExclusive") : NULL;
    ReleaseSRWLockExclusivePtr = Kernel32 ? (PRELEASE_SRW_LOCK_EXCLUSIVE)GetProcAddress(Kernel32, "ReleaseSRWLockExclusive") : NULL;
    if (!InitializeConditionVariablePtr || !WakeAllConditionVariablePtr || !SleepConditionVariableSRWPtr || !InitializeSRWLockPtr || !AcquireSRWLockExclusivePtr || !ReleaseSRWLockExclusivePtr)
    {
        printf("CHPE_CONDVAR_HANDOFF_FAIL exports kernel32=%p error=%lu\n", Kernel32, GetLastError());
        return 2;
    }

    InitializeConditionVariablePtr(&HandoffCondition);
    InitializeSRWLockPtr(&HandoffLock);
    HandoffGeneration = 0;
    HandoffReady = 0;
    HandoffCompleted = 0;
    HandoffTimeouts = 0;
    HandoffFailures = 0;
    HandoffStop = 0;

    Thread = CreateThread(NULL, 0, handoff_waiter, NULL, 0, NULL);
    if (!Thread)
    {
        printf("CHPE_CONDVAR_HANDOFF_FAIL thread error=%lu\n", GetLastError());
        return 3;
    }

    for (Round = 1; Round <= HANDOFF_ROUNDS; ++Round)
    {
        if (!wait_for_value(&HandoffReady, Round))
            break;

        AcquireSRWLockExclusivePtr(&HandoffLock);
        InterlockedExchange(&HandoffGeneration, Round);
        WakeAllConditionVariablePtr(&HandoffCondition);
        ReleaseSRWLockExclusivePtr(&HandoffLock);

        if (!wait_for_value(&HandoffCompleted, Round))
            break;
    }

    if (Round <= HANDOFF_ROUNDS)
    {
        InterlockedExchange(&HandoffStop, 1);
        AcquireSRWLockExclusivePtr(&HandoffLock);
        InterlockedExchange(&HandoffGeneration, HANDOFF_ROUNDS);
        WakeAllConditionVariablePtr(&HandoffCondition);
        ReleaseSRWLockExclusivePtr(&HandoffLock);
    }

    WaitStatus = WaitForSingleObject(Thread, TEST_TIMEOUT_MS);
    Failed = WaitStatus != WAIT_OBJECT_0 || HandoffFailures != 0 || HandoffTimeouts != 0 || HandoffCompleted != HANDOFF_ROUNDS;
    printf("CONDVAR rounds=%ld completed=%ld timeouts=%ld failures=%ld wait=0x%08lx result=%d\n", HANDOFF_ROUNDS, HandoffCompleted, HandoffTimeouts, HandoffFailures, WaitStatus, Failed);
    CloseHandle(Thread);

    printf("CHPE_CONDVAR_HANDOFF_%s\n", Failed ? "FAIL" : "PASS");
    return Failed;
}
