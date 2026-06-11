/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 spin-lock primitives
 *
 * KeAcquireSpinLock / KeReleaseSpinLock round-trip,
 * KeAcquireInStackQueuedSpinLock / KeReleaseInStackQueuedSpinLock,
 * verifies Prcb->LockQueue[] sizing.
 */

#include <kmt_test.h>

VOID Test_KeArm64SpinLock(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static VOID Arm64SpinLockCheck(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    KSPIN_LOCK Lock;
    KIRQL OldIrql;
    KLOCK_QUEUE_HANDLE Handle;

    ok(Prcb != NULL, "Prcb NULL\n");

    /* KSPIN_LOCK acquire/release. */
    KeInitializeSpinLock(&Lock);
    ok_eq_ulonglong((ULONGLONG)Lock, 0ULL);

    KeAcquireSpinLock(&Lock, &OldIrql);
    ok_eq_uint(OldIrql, PASSIVE_LEVEL);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    KeReleaseSpinLock(&Lock, OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);
    /* After release lock is back to 0. */
    ok_eq_ulonglong((ULONGLONG)Lock, 0ULL);

    /* In-stack queued spin lock round-trip. */
    KeAcquireInStackQueuedSpinLock(&Lock, &Handle);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    KeReleaseInStackQueuedSpinLock(&Handle);
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);

    /* Acquire/release at DPC level (no IRQL change). */
    {
        KIRQL Old;
        KeRaiseIrql(DISPATCH_LEVEL, &Old);
        KeAcquireSpinLockAtDpcLevel(&Lock);
        ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
        KeReleaseSpinLockFromDpcLevel(&Lock);
        ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
        KeLowerIrql(Old);
    }

    /* Prcb->LockQueue array sized per LockQueueMaximumLock. */
    if (Prcb)
    {
        ok_eq_size(sizeof(Prcb->LockQueue) / sizeof(Prcb->LockQueue[0]),
                   (SIZE_T)LockQueueMaximumLock);
        /* On ARM64 LockQueueMaximumLock = LockQueueUnusedSpare16 + 1 = 17. */
        ok_eq_uint(LockQueueMaximumLock, 17);
    }

    /* KSPIN_LOCK is ULONG_PTR on ARM64. */
    ok_eq_size(sizeof(KSPIN_LOCK), (SIZE_T)8);

    /* KSPIN_LOCK_QUEUE is 16 bytes (Next + Lock). */
    ok_eq_size(sizeof(KSPIN_LOCK_QUEUE), (SIZE_T)16);

    dump_trace("[arm64][KeArm64SpinLock] LockQueueMaximumLock=%u sizeof(KSPIN_LOCK_QUEUE)=%Iu\n",
               LockQueueMaximumLock, (SIZE_T)sizeof(KSPIN_LOCK_QUEUE));
}

#endif /* _M_ARM64 */

START_TEST(KeArm64SpinLock)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64SpinLock is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64SpinLock] enter\n");
    Arm64SpinLockCheck();
#endif
}
