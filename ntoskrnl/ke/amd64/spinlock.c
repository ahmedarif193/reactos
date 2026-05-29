/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/amd64/spinlock.c
 * PURPOSE:         Spinlock and Queued Spinlock Support
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#undef KeAcquireSpinLock
#undef KeReleaseSpinLock

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
KIRQL
KeAcquireSpinLockRaiseToSynch(PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    /* Raise to sync */
    KeRaiseIrql(SYNCH_LEVEL, &OldIrql);

    /* Acquire the lock and return */
    KxAcquireSpinLock(SpinLock);
    return OldIrql;
}

/*
 * @implemented
 */
KIRQL
NTAPI
KeAcquireSpinLockRaiseToDpc(PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    /* Raise to dispatch */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    /* Acquire the lock and return */
    KxAcquireSpinLock(SpinLock);
    return OldIrql;
}

/*
 * @implemented
 */
VOID
NTAPI
KeReleaseSpinLock(PKSPIN_LOCK SpinLock,
                  KIRQL OldIrql)
{
    /* Release the lock and lower IRQL back */
    KxReleaseSpinLock(SpinLock);
    KeLowerIrql(OldIrql);
}

/*
 * @implemented
 */
KIRQL
KeAcquireQueuedSpinLock(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber)
{
    KIRQL OldIrql;

    /* Raise to dispatch and acquire the queued lock */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KeAcquireQueuedSpinLockAtDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockNumber]);
    return OldIrql;
}

/*
 * @implemented
 */
KIRQL
KeAcquireQueuedSpinLockRaiseToSynch(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber)
{
    KIRQL OldIrql;

    /* Raise to synch and acquire the queued lock */
    KeRaiseIrql(SYNCH_LEVEL, &OldIrql);
    KeAcquireQueuedSpinLockAtDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockNumber]);
    return OldIrql;
}

/*
 * @implemented
 */
VOID
KeAcquireInStackQueuedSpinLock(IN PKSPIN_LOCK SpinLock,
                               IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    /* Raise to dispatch and acquire the in-stack queued lock */
    KeRaiseIrql(DISPATCH_LEVEL, &LockHandle->OldIrql);
    KeAcquireInStackQueuedSpinLockAtDpcLevel(SpinLock, LockHandle);
}


/*
 * @implemented
 */
VOID
KeAcquireInStackQueuedSpinLockRaiseToSynch(IN PKSPIN_LOCK SpinLock,
                                           IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    /* Raise to synch and acquire the in-stack queued lock */
    KeRaiseIrql(SYNCH_LEVEL, &LockHandle->OldIrql);
    KeAcquireInStackQueuedSpinLockAtDpcLevel(SpinLock, LockHandle);
}


/*
 * @implemented
 */
VOID
KeReleaseQueuedSpinLock(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber,
                        IN KIRQL OldIrql)
{
    /* Release the queued lock and lower IRQL back */
    KeReleaseQueuedSpinLockFromDpcLevel(&KeGetCurrentPrcb()->LockQueue[LockNumber]);
    KeLowerIrql(OldIrql);
}


/*
 * @implemented
 */
VOID
KeReleaseInStackQueuedSpinLock(IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    /* Release the in-stack queued lock and lower IRQL back */
    KeReleaseInStackQueuedSpinLockFromDpcLevel(LockHandle);
    KeLowerIrql(LockHandle->OldIrql);
}


/*
 * @implemented
 *
 * Note: per Windows kernel-mode tests, the IRQL is raised regardless of
 * whether the lock is acquired. The caller is responsible for lowering it
 * via KeReleaseQueuedSpinLock on success, or KeLowerIrql on failure.
 */
BOOLEAN
KeTryToAcquireQueuedSpinLockRaiseToSynch(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber,
                                         IN PKIRQL OldIrql)
{
    KeRaiseIrql(SYNCH_LEVEL, OldIrql);
    return KeTryToAcquireQueuedSpinLockAtDpcLevel(
        &KeGetCurrentPrcb()->LockQueue[LockNumber]);
}

/*
 * @implemented
 *
 * Note: per Windows kernel-mode tests, the IRQL is raised regardless of
 * whether the lock is acquired. The caller is responsible for lowering it
 * via KeReleaseQueuedSpinLock on success, or KeLowerIrql on failure.
 */
LOGICAL
KeTryToAcquireQueuedSpinLock(IN KSPIN_LOCK_QUEUE_NUMBER LockNumber,
                             OUT PKIRQL OldIrql)
{
    KeRaiseIrql(DISPATCH_LEVEL, OldIrql);
    return KeTryToAcquireQueuedSpinLockAtDpcLevel(
        &KeGetCurrentPrcb()->LockQueue[LockNumber]);
}

/* EOF */
