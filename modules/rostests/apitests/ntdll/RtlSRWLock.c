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

START_TEST(RtlSRWLock)
{
    TestTryAcquireEncoding();
    TestQueuedSharedChain();
    TestStress();
}
