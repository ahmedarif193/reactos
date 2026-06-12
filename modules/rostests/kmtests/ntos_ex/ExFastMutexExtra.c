/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite extended FAST_MUTEX coverage
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestAcquireRelease(VOID)
{
    FAST_MUTEX Mutex;
    ULONG i;

    ExInitializeFastMutex(&Mutex);
    ok_eq_long(Mutex.Count, 1L);
    ok_eq_pointer(Mutex.Owner, NULL);

    for (i = 0; i < 32; i++)
    {
        ExAcquireFastMutex(&Mutex);
        ok_eq_long(Mutex.Count, 0L);
        ok_eq_pointer(Mutex.Owner, KeGetCurrentThread());
        ok_eq_uint(KeGetCurrentIrql(), APC_LEVEL);
        ExReleaseFastMutex(&Mutex);
        ok_eq_long(Mutex.Count, 1L);
        ok_eq_pointer(Mutex.Owner, NULL);
        ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);
    }
}

static
VOID
TestTryAcquire(VOID)
{
    FAST_MUTEX Mutex;

    ExInitializeFastMutex(&Mutex);

    ok_bool_true(ExTryToAcquireFastMutex(&Mutex), "try acquire on free");
    ok_eq_long(Mutex.Count, 0L);
    ok_eq_pointer(Mutex.Owner, KeGetCurrentThread());
    ok_bool_false(ExTryToAcquireFastMutex(&Mutex), "try acquire on held");
    ok_eq_long(Mutex.Count, 0L);
    ExReleaseFastMutex(&Mutex);
    ok_eq_long(Mutex.Count, 1L);
}

static
VOID
TestUnsafe(VOID)
{
    FAST_MUTEX Mutex;

    ExInitializeFastMutex(&Mutex);

    KeEnterCriticalRegion();
    ExAcquireFastMutexUnsafe(&Mutex);
    ok_eq_long(Mutex.Count, 0L);
    ok_eq_pointer(Mutex.Owner, KeGetCurrentThread());
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);
    ExReleaseFastMutexUnsafe(&Mutex);
    ok_eq_long(Mutex.Count, 1L);
    KeLeaveCriticalRegion();
}

START_TEST(ExFastMutexExtra)
{
    TestAcquireRelease();
    TestTryAcquire();
    TestUnsafe();
}
