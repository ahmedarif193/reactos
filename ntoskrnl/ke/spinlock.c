/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/spinlock.c
 * PURPOSE:         Spinlock and Queued Spinlock Support
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * Queued spinlock flag bits encoded in the low bits of KSPIN_LOCK_QUEUE.Lock.
 * Match LOCK_QUEUE_WAIT / LOCK_QUEUE_OWNER from the public xdk headers.
 */
#define LQ_WAIT     LOCK_QUEUE_WAIT      /* (1) waiter is spinning */
#define LQ_OWN      LOCK_QUEUE_OWNER     /* (2) entry owns the lock */
#define LQ_FLAGS    (LQ_WAIT | LQ_OWN)

/* PRIVATE FUNCTIONS *********************************************************/

/*
 * NT-style queued (MCS) spinlock acquire / release.
 *
 * The KSPIN_LOCK acts as the queue's tail pointer (NULL == free, else
 * pointer to the last enqueued KSPIN_LOCK_QUEUE entry).
 * Each KSPIN_LOCK_QUEUE has:
 *   - Next: pointer to the next waiter (NULL if last)
 *   - Lock: pointer to the actual KSPIN_LOCK; low bits encode WAIT/OWN state
 *
 * Idle-state invariant: between acquire/release calls, LockHandle->Lock
 * points at the real KSPIN_LOCK with no flag bits set, and LockHandle->Next
 * is NULL. The per-CPU LockQueue entries (Prcb->LockQueue[N]) are initialized
 * once in KiInitSystem and reused across many acquire/release cycles, so
 * Release MUST leave the entry in idle state (Lock = clean address, Next = NULL).
 *
 * The same algorithm is correct on UP and SMP: on UP, contention is impossible
 * because IRQL is raised, so the InterlockedExchange always returns NULL and
 * the wait loop is never entered. The atomics are slightly more expensive than
 * a plain memory barrier but correctness is identical.
 */

_IRQL_requires_min_(DISPATCH_LEVEL)
_Acquires_nonreentrant_lock_(*LockHandle->Lock)
_Acquires_exclusive_lock_(*LockHandle->Lock)
VOID
FASTCALL
KeAcquireQueuedSpinLockAtDpcLevel(_Inout_ PKSPIN_LOCK_QUEUE LockHandle)
{
    PKSPIN_LOCK SpinLock = LockHandle->Lock;
    PKSPIN_LOCK_QUEUE Tail;

#if DBG
    /* Must be at DISPATCH_LEVEL or above */
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL,
                     (ULONG_PTR)SpinLock,
                     KeGetCurrentIrql(),
                     0,
                     0);
    }
#endif

    ASSERT(SpinLock != NULL);
    /* Recursive acquire on the same per-CPU entry shows up as stale flags */
    ASSERT(((ULONG_PTR)SpinLock & LQ_FLAGS) == 0);

    LockHandle->Next = NULL;

    /* Atomically install ourselves as the new queue tail.
     * Tail receives the previous tail (NULL means lock was free). */
    Tail = (PKSPIN_LOCK_QUEUE)
           InterlockedExchangePointer((PVOID volatile *)SpinLock,
                                       LockHandle);

    if (Tail == NULL)
    {
        /* Lock was free; we own it. Encode owner bit on our entry. */
        LockHandle->Lock = (PKSPIN_LOCK)((ULONG_PTR)SpinLock | LQ_OWN);
        return;
    }

    /* Lock held; mark our entry as waiting before linking in. */
    LockHandle->Lock = (PKSPIN_LOCK)((ULONG_PTR)SpinLock | LQ_WAIT);

    /* Ensure WAIT flag is visible before predecessor sees our pointer. */
    KeMemoryBarrier();

    Tail->Next = LockHandle;

    /* Spin until predecessor's release atomically flips WAIT off / OWN on. */
    while ((ULONG_PTR)LockHandle->Lock & LQ_WAIT)
    {
        YieldProcessor();
    }
}

_IRQL_requires_min_(DISPATCH_LEVEL)
_Releases_nonreentrant_lock_(*LockHandle->Lock)
_Releases_exclusive_lock_(*LockHandle->Lock)
VOID
FASTCALL
KeReleaseQueuedSpinLockFromDpcLevel(_Inout_ PKSPIN_LOCK_QUEUE LockHandle)
{
    PKSPIN_LOCK SpinLock;
    PKSPIN_LOCK_QUEUE Successor;

#if DBG
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL,
                     (ULONG_PTR)LockHandle->Lock,
                     KeGetCurrentIrql(),
                     0,
                     0);
    }
#endif

    ASSERT(((ULONG_PTR)LockHandle->Lock & LQ_OWN) != 0);

    /* Strip flag bits to recover the real KSPIN_LOCK pointer. */
    SpinLock = (PKSPIN_LOCK)((ULONG_PTR)LockHandle->Lock & ~LQ_FLAGS);
    ASSERT(SpinLock != NULL);

    /* Clear OWN flag from our entry; restore idle-state Lock value. */
    LockHandle->Lock = SpinLock;

    Successor = LockHandle->Next;
    if (Successor == NULL)
    {
        /* No visible successor: try to atomically clear the queue tail. */
        if (InterlockedCompareExchangePointer((PVOID volatile *)SpinLock,
                                               NULL,
                                               LockHandle) == LockHandle)
        {
            /* We were the tail; lock fully released. Entry stays idle. */
            return;
        }

        /* Successor is mid-enqueue; wait for them to publish the link. */
        while ((Successor = LockHandle->Next) == NULL)
        {
            YieldProcessor();
        }
    }

    /* Hand off the lock by atomically clearing successor's WAIT and
     * setting OWN. Successor is currently spinning on (Lock & LQ_WAIT).
     * XOR (LQ_WAIT | LQ_OWN) flips both bits in one atomic op,
     * race-free against the spin. */
#ifdef _WIN64
    InterlockedXor64((LONG64 volatile *)&Successor->Lock,
                      LQ_OWN | LQ_WAIT);
#else
    InterlockedXor((LONG volatile *)&Successor->Lock,
                    LQ_OWN | LQ_WAIT);
#endif

    /* Restore our entry to idle: Lock already holds the clean address;
     * just clear the now-stale successor pointer. */
    LockHandle->Next = NULL;
}

/*
 * Non-blocking try-acquire variant of the queued spinlock.
 * Returns TRUE on success, FALSE if the lock was already held.
 * On success the entry is marked OWN so the matching release works correctly.
 */
_IRQL_requires_min_(DISPATCH_LEVEL)
BOOLEAN
FASTCALL
KeTryToAcquireQueuedSpinLockAtDpcLevel(_Inout_ PKSPIN_LOCK_QUEUE LockHandle)
{
    PKSPIN_LOCK SpinLock = LockHandle->Lock;

#if DBG
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL,
                     (ULONG_PTR)SpinLock,
                     KeGetCurrentIrql(),
                     0,
                     0);
    }
#endif

    ASSERT(SpinLock != NULL);
    ASSERT(((ULONG_PTR)SpinLock & LQ_FLAGS) == 0);

    LockHandle->Next = NULL;

    /* Only succeed if the queue tail is NULL (lock free). */
    if (InterlockedCompareExchangePointer((PVOID volatile *)SpinLock,
                                           LockHandle,
                                           NULL) != NULL)
    {
        return FALSE;
    }

    LockHandle->Lock = (PKSPIN_LOCK)((ULONG_PTR)SpinLock | LQ_OWN);
    return TRUE;
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
KIRQL
NTAPI
KeAcquireInterruptSpinLock(IN PKINTERRUPT Interrupt)
{
    KIRQL OldIrql;

    /* Raise IRQL */
    KeRaiseIrql(Interrupt->SynchronizeIrql, &OldIrql);

    /* Acquire spinlock on MP */
    KeAcquireSpinLockAtDpcLevel(Interrupt->ActualLock);
    return OldIrql;
}

/*
 * @implemented
 */
VOID
NTAPI
KeReleaseInterruptSpinLock(IN PKINTERRUPT Interrupt,
                           IN KIRQL OldIrql)
{
    /* Release lock on MP */
    KeReleaseSpinLockFromDpcLevel(Interrupt->ActualLock);

    /* Lower IRQL */
    KeLowerIrql(OldIrql);
}

/*
 * @implemented
 */
VOID
NTAPI
_KeInitializeSpinLock(IN PKSPIN_LOCK SpinLock)
{
    /* Clear it */
    *SpinLock = 0;
}

/*
 * @implemented
 */
#undef KeAcquireSpinLockAtDpcLevel
VOID
NTAPI
KeAcquireSpinLockAtDpcLevel(IN PKSPIN_LOCK SpinLock)
{
    /* Make sure we are at DPC or above! */
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        /* We aren't -- bugcheck */
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL,
                     (ULONG_PTR)SpinLock,
                     KeGetCurrentIrql(),
                     0,
                     0);
    }

    /* Do the inlined function */
    KxAcquireSpinLock(SpinLock);
}

/*
 * @implemented
 */
#undef KeReleaseSpinLockFromDpcLevel
VOID
NTAPI
KeReleaseSpinLockFromDpcLevel(IN PKSPIN_LOCK SpinLock)
{
    /* Make sure we are at DPC or above! */
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        /* We aren't -- bugcheck */
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL,
                     (ULONG_PTR)SpinLock,
                     KeGetCurrentIrql(),
                     0,
                     0);
    }

    /* Do the inlined function */
    KxReleaseSpinLock(SpinLock);
}

/*
 * @implemented
 */
VOID
FASTCALL
KefAcquireSpinLockAtDpcLevel(IN PKSPIN_LOCK SpinLock)
{
    /* Make sure we are at DPC or above! */
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        /* We aren't -- bugcheck */
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL,
                     (ULONG_PTR)SpinLock,
                     KeGetCurrentIrql(),
                     0,
                     0);
    }

    /* Do the inlined function */
    KxAcquireSpinLock(SpinLock);
}

/*
 * @implemented
 */
VOID
FASTCALL
KefReleaseSpinLockFromDpcLevel(IN PKSPIN_LOCK SpinLock)
{
    /* Make sure we are at DPC or above! */
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        /* We aren't -- bugcheck */
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL,
                     (ULONG_PTR)SpinLock,
                     KeGetCurrentIrql(),
                     0,
                     0);
    }

    /* Do the inlined function */
    KxReleaseSpinLock(SpinLock);
}

/*
 * @implemented
 */
VOID
FASTCALL
KiAcquireSpinLock(IN PKSPIN_LOCK SpinLock)
{
    /* Do the inlined function */
    KxAcquireSpinLock(SpinLock);
}

/*
 * @implemented
 */
VOID
FASTCALL
KiReleaseSpinLock(IN PKSPIN_LOCK SpinLock)
{
    /* Do the inlined function */
    KxReleaseSpinLock(SpinLock);
}

/*
 * @implemented
 */
BOOLEAN
FASTCALL
KeTryToAcquireSpinLockAtDpcLevel(IN OUT PKSPIN_LOCK SpinLock)
{
#if DBG
    /* Make sure we are at DPC or above! */
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        /* We aren't -- bugcheck */
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL,
                     (ULONG_PTR)SpinLock,
                     KeGetCurrentIrql(),
                     0,
                     0);
    }

    /* Make sure that we don't own the lock already */
    if (((KSPIN_LOCK)KeGetCurrentThread() | 1) == *SpinLock)
    {
        /* We do, bugcheck! */
        KeBugCheckEx(SPIN_LOCK_ALREADY_OWNED, (ULONG_PTR)SpinLock, 0, 0, 0);
    }
#endif

#ifdef CONFIG_SMP
    /* Check if it's already acquired */
    if (!(*SpinLock))
    {
        /* Try to acquire it */
        if (InterlockedBitTestAndSet((PLONG)SpinLock, 0))
        {
            /* Someone else acquired it */
            return FALSE;
        }
    }
    else
    {
        /* It was already acquired */
        return FALSE;
    }
#endif

#if DBG
    /* On debug builds, we OR in the KTHREAD */
    *SpinLock = (ULONG_PTR)KeGetCurrentThread() | 1;
#endif

    /* All is well, return TRUE */
    return TRUE;
}

/*
 * @implemented
 */
VOID
FASTCALL
KeAcquireInStackQueuedSpinLockAtDpcLevel(IN PKSPIN_LOCK SpinLock,
                                         IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    /* Set up the per-acquire queue entry, then run the queued-lock acquire. */
    LockHandle->LockQueue.Next = NULL;
    LockHandle->LockQueue.Lock = SpinLock;
    KeAcquireQueuedSpinLockAtDpcLevel(&LockHandle->LockQueue);
}

/*
 * @implemented
 */
VOID
FASTCALL
KeReleaseInStackQueuedSpinLockFromDpcLevel(IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    KeReleaseQueuedSpinLockFromDpcLevel(&LockHandle->LockQueue);
}

/*
 * @unimplemented
 */
KIRQL
FASTCALL
KeAcquireSpinLockForDpc(IN PKSPIN_LOCK SpinLock)
{
    UNIMPLEMENTED;
    return 0;
}

/*
 * @unimplemented
 */
VOID
FASTCALL
KeReleaseSpinLockForDpc(IN PKSPIN_LOCK SpinLock,
                        IN KIRQL OldIrql)
{
    UNIMPLEMENTED;
}

/*
 * @implemented
 */
VOID
FASTCALL
KeAcquireInStackQueuedSpinLockForDpc(IN PKSPIN_LOCK SpinLock,
                                     IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    LockHandle->OldIrql = KeGetCurrentIrql();
    if (LockHandle->OldIrql >= DISPATCH_LEVEL)
        KeAcquireInStackQueuedSpinLockAtDpcLevel(SpinLock, LockHandle);
    else
        KeAcquireInStackQueuedSpinLock(SpinLock, LockHandle);
}

/*
 * @implemented
 */
VOID
FASTCALL
KeReleaseInStackQueuedSpinLockForDpc(IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    if (LockHandle->OldIrql >= DISPATCH_LEVEL)
        KeReleaseInStackQueuedSpinLockFromDpcLevel(LockHandle);
    else
        KeReleaseInStackQueuedSpinLock(LockHandle);

}

/*
 * @implemented
 */
BOOLEAN
FASTCALL
KeTestSpinLock(IN PKSPIN_LOCK SpinLock)
{
    /* Test this spinlock */
    if (*SpinLock)
    {
        /* Spinlock is busy, yield execution */
        YieldProcessor();

        /* Return busy flag */
        return FALSE;
    }

    /* Spinlock appears to be free */
    return TRUE;
}

#ifdef _M_IX86
VOID
NTAPI
Kii386SpinOnSpinLock(PKSPIN_LOCK SpinLock, ULONG Flags)
{
    // FIXME: Handle flags
    UNREFERENCED_PARAMETER(Flags);

    /* Spin until it's unlocked */
    while (*(volatile KSPIN_LOCK *)SpinLock & 1)
    {
        // FIXME: Check for timeout

        /* Yield and keep looping */
        YieldProcessor();
    }
}
#endif
