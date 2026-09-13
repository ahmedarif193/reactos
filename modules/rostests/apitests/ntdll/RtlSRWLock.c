/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests slim reader/writer lock wait-block handoff contracts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define STRESS_THREADS 8
#define STRESS_MILLISECONDS 3000
#define QUEUED_READERS 6
#define JOIN_TIMEOUT_MS 30000

typedef struct _SRW_STRESS_CONTEXT
{
    RTL_SRWLOCK Lock;
    volatile LONG Readers;
    volatile LONG Writers;
    volatile LONG Stop;
    volatile LONG Failures;
    volatile LONG Iterations;
} SRW_STRESS_CONTEXT, *PSRW_STRESS_CONTEXT;

typedef struct _SRW_QUEUED_CONTEXT
{
    RTL_SRWLOCK Lock;
    volatile LONG Entered;
    volatile LONG Failures;
    HANDLE Release;
} SRW_QUEUED_CONTEXT, *PSRW_QUEUED_CONTEXT;

/*
 * Every acquirer must leave the lock only through its own wait block hand-
 * off.  A releaser that publishes an uncontended lock word before it has
 * finished walking the wait chain lets waiters return while their stack-
 * allocated wait entries are still referenced.  This stress mix exercises
 * shared chains queued behind exclusive owners and exclusive waiters queued
 * behind shared owners; a use-after-return shows up as an invariant failure,
 * a crash, or a lost wakeup (a thread that never finishes).
 */
static
DWORD
WINAPI
StressThread(
    _In_ LPVOID Parameter)
{
    PSRW_STRESS_CONTEXT Context = Parameter;
    ULONG Seed = GetCurrentThreadId() * 2654435761u + 1;
    volatile LONG Spin;

    while (InterlockedCompareExchange(&Context->Stop, 0, 0) == 0)
    {
        Seed = Seed * 1103515245u + 12345u;
        if (((Seed >> 16) & 0xFF) < 160)
        {
            RtlAcquireSRWLockShared(&Context->Lock);
            InterlockedIncrement(&Context->Readers);
            if (InterlockedCompareExchange(&Context->Writers, 0, 0) != 0)
                InterlockedIncrement(&Context->Failures);
            for (Spin = 0; Spin < 20; Spin++)
                ;
            InterlockedDecrement(&Context->Readers);
            RtlReleaseSRWLockShared(&Context->Lock);
        }
        else
        {
            RtlAcquireSRWLockExclusive(&Context->Lock);
            if (InterlockedIncrement(&Context->Writers) != 1 ||
                InterlockedCompareExchange(&Context->Readers, 0, 0) != 0)
            {
                InterlockedIncrement(&Context->Failures);
            }
            for (Spin = 0; Spin < 50; Spin++)
                ;
            InterlockedDecrement(&Context->Writers);
            RtlReleaseSRWLockExclusive(&Context->Lock);
        }
        InterlockedIncrement(&Context->Iterations);
    }
    return 0;
}

static
VOID
TestStress(VOID)
{
    SRW_STRESS_CONTEXT Context;
    HANDLE Threads[STRESS_THREADS];
    ULONG Index;
    DWORD WaitStatus;

    RtlZeroMemory(&Context, sizeof(Context));
    RtlInitializeSRWLock(&Context.Lock);

    for (Index = 0; Index < STRESS_THREADS; Index++)
    {
        Threads[Index] = CreateThread(NULL, 0, StressThread, &Context, 0, NULL);
        ok(Threads[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
    }

    Sleep(STRESS_MILLISECONDS);
    InterlockedExchange(&Context.Stop, 1);

    WaitStatus = WaitForMultipleObjects(STRESS_THREADS, Threads, TRUE, JOIN_TIMEOUT_MS);
    ok(WaitStatus == WAIT_OBJECT_0,
       "Stress threads did not finish (wait=%lu); a waiter lost its wakeup\n",
       WaitStatus);
    ok(Context.Failures == 0, "Mutual exclusion violated %ld times\n", Context.Failures);
    ok(Context.Iterations > 0, "No iterations completed\n");
    ok(Context.Lock.Ptr == NULL, "Lock word not idle after stress: %p\n", Context.Lock.Ptr);
    trace("SRW stress: %ld iterations on %u threads\n", Context.Iterations, STRESS_THREADS);

    for (Index = 0; Index < STRESS_THREADS; Index++)
    {
        if (Threads[Index] != NULL)
        {
            if (WaitStatus != WAIT_OBJECT_0)
                TerminateThread(Threads[Index], 0);
            CloseHandle(Threads[Index]);
        }
    }
}

static
DWORD
WINAPI
QueuedReader(
    _In_ LPVOID Parameter)
{
    PSRW_QUEUED_CONTEXT Context = Parameter;

    RtlAcquireSRWLockShared(&Context->Lock);
    InterlockedIncrement(&Context->Entered);
    /* Hold the shared lock until every reader has been admitted so the
       whole chain is released by one exclusive release. */
    if (WaitForSingleObject(Context->Release, JOIN_TIMEOUT_MS) != WAIT_OBJECT_0)
        InterlockedIncrement(&Context->Failures);
    RtlReleaseSRWLockShared(&Context->Lock);
    return 0;
}

/*
 * Queue several shared acquirers behind an exclusive owner.  The exclusive
 * release must admit the whole shared chain, and the lock must return to
 * idle after the readers leave.
 */
static
VOID
TestQueuedSharedChain(VOID)
{
    SRW_QUEUED_CONTEXT Context;
    HANDLE Threads[QUEUED_READERS];
    ULONG Index;
    ULONG Start;
    DWORD WaitStatus;

    RtlZeroMemory(&Context, sizeof(Context));
    RtlInitializeSRWLock(&Context.Lock);
    Context.Release = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Context.Release != NULL, "CreateEvent failed with %lu\n", GetLastError());

    RtlAcquireSRWLockExclusive(&Context.Lock);
    for (Index = 0; Index < QUEUED_READERS; Index++)
    {
        Threads[Index] = CreateThread(NULL, 0, QueuedReader, &Context, 0, NULL);
        ok(Threads[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
    }

    /* Give the readers time to queue up on the wait chain. */
    Sleep(200);
    ok(Context.Entered == 0, "%ld readers entered while the lock was exclusive\n", Context.Entered);
    ok(Context.Lock.Ptr != NULL, "Readers did not queue on the lock word\n");

    RtlReleaseSRWLockExclusive(&Context.Lock);

    Start = GetTickCount();
    while (InterlockedCompareExchange(&Context.Entered, 0, 0) < QUEUED_READERS &&
           GetTickCount() - Start < JOIN_TIMEOUT_MS)
    {
        Sleep(1);
    }
    ok(Context.Entered == QUEUED_READERS,
       "Only %ld of %u queued readers were admitted\n", Context.Entered, QUEUED_READERS);

    SetEvent(Context.Release);
    WaitStatus = WaitForMultipleObjects(QUEUED_READERS, Threads, TRUE, JOIN_TIMEOUT_MS);
    ok(WaitStatus == WAIT_OBJECT_0, "Queued readers did not finish (wait=%lu)\n", WaitStatus);
    ok(Context.Failures == 0, "%ld readers timed out holding the lock\n", Context.Failures);
    ok(Context.Lock.Ptr == NULL, "Lock word not idle after chain release: %p\n", Context.Lock.Ptr);

    for (Index = 0; Index < QUEUED_READERS; Index++)
    {
        if (Threads[Index] != NULL)
        {
            if (WaitStatus != WAIT_OBJECT_0)
                TerminateThread(Threads[Index], 0);
            CloseHandle(Threads[Index]);
        }
    }
    CloseHandle(Context.Release);
}

/*
 * The try-acquire routines must encode the shared count exactly like the
 * blocking routines so that the matching release returns the lock to idle.
 */
static
VOID
TestTryAcquireEncoding(VOID)
{
    RTL_SRWLOCK Lock;

    RtlInitializeSRWLock(&Lock);

    ok(RtlTryAcquireSRWLockShared(&Lock), "First shared try-acquire failed\n");
    ok(RtlTryAcquireSRWLockShared(&Lock), "Second shared try-acquire failed\n");
    ok(!RtlTryAcquireSRWLockExclusive(&Lock), "Exclusive try-acquire succeeded on a shared lock\n");
    RtlReleaseSRWLockShared(&Lock);
    ok(Lock.Ptr != NULL, "Lock idle with one shared owner remaining\n");
    RtlReleaseSRWLockShared(&Lock);
    ok(Lock.Ptr == NULL, "Lock not idle after releasing both shared owners: %p\n", Lock.Ptr);

    ok(RtlTryAcquireSRWLockExclusive(&Lock), "Exclusive try-acquire failed on an idle lock\n");
    ok(!RtlTryAcquireSRWLockShared(&Lock), "Shared try-acquire succeeded on an exclusive lock\n");
    ok(!RtlTryAcquireSRWLockExclusive(&Lock), "Exclusive try-acquire succeeded twice\n");
    RtlReleaseSRWLockExclusive(&Lock);
    ok(Lock.Ptr == NULL, "Lock not idle after exclusive release: %p\n", Lock.Ptr);

    RtlAcquireSRWLockShared(&Lock);
    ok(RtlTryAcquireSRWLockShared(&Lock), "Shared try-acquire failed on a shared lock\n");
    RtlReleaseSRWLockShared(&Lock);
    RtlReleaseSRWLockShared(&Lock);
    ok(Lock.Ptr == NULL, "Lock not idle after mixed shared release: %p\n", Lock.Ptr);
}

typedef struct _SRW_PRIORITY_CONTEXT
{
    RTL_SRWLOCK Lock;
    HANDLE Held;
    HANDLE Release;
    HANDLE Entering;
    LONG Entered;
    BOOL SharedOwner;
    BOOL SharedWaiter;
} SRW_PRIORITY_CONTEXT, *PSRW_PRIORITY_CONTEXT;

static
DWORD
WINAPI
PriorityOwner(
    _In_ LPVOID Parameter)
{
    PSRW_PRIORITY_CONTEXT Context = Parameter;

    if (Context->SharedOwner)
        RtlAcquireSRWLockShared(&Context->Lock);
    else
        RtlAcquireSRWLockExclusive(&Context->Lock);
    SetEvent(Context->Held);
    WaitForSingleObject(Context->Release, INFINITE);
    if (Context->SharedOwner)
        RtlReleaseSRWLockShared(&Context->Lock);
    else
        RtlReleaseSRWLockExclusive(&Context->Lock);
    return 0;
}

static
DWORD
WINAPI
PriorityWaiter(
    _In_ LPVOID Parameter)
{
    PSRW_PRIORITY_CONTEXT Context = Parameter;

    if (InterlockedIncrement(&Context->Entered) == 2)
        SetEvent(Context->Entering);
    if (Context->SharedWaiter)
        RtlAcquireSRWLockShared(&Context->Lock);
    else
        RtlAcquireSRWLockExclusive(&Context->Lock);
    if (Context->SharedWaiter)
        RtlReleaseSRWLockShared(&Context->Lock);
    else
        RtlReleaseSRWLockExclusive(&Context->Lock);
    return 0;
}

static
VOID
TestPriorityProgress(VOID)
{
    DWORD_PTR ProcessMask, SystemMask, Cpu;
    ULONG Mode;
    BOOL Success;
    INT OldPriority;

    Success = GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask);
    ok(Success, "GetProcessAffinityMask failed with %lu\n", GetLastError());
    if (!Success)
        return;
    Cpu = ProcessMask & (0 - ProcessMask);
    OldPriority = GetThreadPriority(GetCurrentThread());
    Success = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    ok(Success, "Setting supervisor priority failed with %lu\n", GetLastError());
    if (!Success)
        return;

    for (Mode = 0; Mode < 3; ++Mode)
    {
        SRW_PRIORITY_CONTEXT Context = {0};
        HANDLE Threads[3] = {NULL, NULL, NULL};
        DWORD WaitStatus = WAIT_FAILED;
        ULONG Index;

        Context.SharedOwner = Mode == 1;
        Context.SharedWaiter = Mode == 2;
        Context.Held = CreateEventW(NULL, TRUE, FALSE, NULL);
        Context.Release = CreateEventW(NULL, TRUE, FALSE, NULL);
        Context.Entering = CreateEventW(NULL, TRUE, FALSE, NULL);
        Threads[0] = CreateThread(NULL, 0, PriorityOwner, &Context, CREATE_SUSPENDED, NULL);
        Threads[1] = CreateThread(NULL, 0, PriorityWaiter, &Context, CREATE_SUSPENDED, NULL);
        Threads[2] = CreateThread(NULL, 0, PriorityWaiter, &Context, CREATE_SUSPENDED, NULL);
        Success = Context.Held && Context.Release && Context.Entering && Threads[0] && Threads[1] && Threads[2];
        ok(Success, "Priority test setup failed with %lu\n", GetLastError());
        if (!Success)
            goto Cleanup;

        Success = SetThreadAffinityMask(Threads[0], Cpu) != 0 && SetThreadAffinityMask(Threads[1], Cpu) != 0 && SetThreadAffinityMask(Threads[2], Cpu) != 0;
        ok(Success, "SetThreadAffinityMask failed with %lu\n", GetLastError());
        if (!Success)
            goto Cleanup;
        Success = SetThreadPriority(Threads[0], THREAD_PRIORITY_LOWEST) && SetThreadPriority(Threads[1], THREAD_PRIORITY_HIGHEST) && SetThreadPriority(Threads[2], THREAD_PRIORITY_HIGHEST);
        ok(Success, "SetThreadPriority failed with %lu\n", GetLastError());
        if (!Success)
            goto Cleanup;
        Success = SetThreadPriorityBoost(Threads[0], TRUE) && SetThreadPriorityBoost(Threads[1], TRUE) && SetThreadPriorityBoost(Threads[2], TRUE);
        ok(Success, "SetThreadPriorityBoost failed with %lu\n", GetLastError());
        if (!Success)
            goto Cleanup;

        ResumeThread(Threads[0]);
        WaitStatus = WaitForSingleObject(Context.Held, JOIN_TIMEOUT_MS);
        ok(WaitStatus == WAIT_OBJECT_0, "Owner did not acquire the lock: %lu\n", WaitStatus);
        if (WaitStatus != WAIT_OBJECT_0)
            goto Cleanup;
        ResumeThread(Threads[1]);
        ResumeThread(Threads[2]);
        WaitStatus = WaitForSingleObject(Context.Entering, JOIN_TIMEOUT_MS);
        ok(WaitStatus == WAIT_OBJECT_0, "Waiter did not start: %lu\n", WaitStatus);
        if (WaitStatus != WAIT_OBJECT_0)
            goto Cleanup;

        Sleep(100);
        SetEvent(Context.Release);
        WaitStatus = WaitForMultipleObjects(3, Threads, TRUE, 2000);
        ok(WaitStatus == WAIT_OBJECT_0, "Higher-priority waiter starved the %s owner (%s waiter, wait=%lu)\n", Context.SharedOwner ? "shared" : "exclusive", Context.SharedWaiter ? "shared" : "exclusive", WaitStatus);
        if (WaitStatus != WAIT_OBJECT_0)
        {
            SetThreadPriority(Threads[0], THREAD_PRIORITY_TIME_CRITICAL);
            WaitStatus = WaitForMultipleObjects(3, Threads, TRUE, JOIN_TIMEOUT_MS);
            ok(WaitStatus == WAIT_OBJECT_0, "Priority test threads failed to finish after recovery: %lu\n", WaitStatus);
        }

Cleanup:
        for (Index = 0; Index < 3; ++Index)
        {
            if (Threads[Index])
            {
                if (WaitForSingleObject(Threads[Index], 0) != WAIT_OBJECT_0)
                    TerminateThread(Threads[Index], 0);
                CloseHandle(Threads[Index]);
            }
        }
        if (Context.Held)
            CloseHandle(Context.Held);
        if (Context.Release)
            CloseHandle(Context.Release);
        if (Context.Entering)
            CloseHandle(Context.Entering);
    }
    SetThreadPriority(GetCurrentThread(), OldPriority);
}

START_TEST(RtlSRWLock)
{
    TestTryAcquireEncoding();
    TestQueuedSharedChain();
    TestStress();
    TestPriorityProgress();
}
