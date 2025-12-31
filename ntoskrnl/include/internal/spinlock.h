/*
* PROJECT:         ReactOS Kernel
* LICENSE:         GPL - See COPYING in the top level directory
* FILE:            ntoskrnl/include/internal/spinlock.h
* PURPOSE:         Internal Inlined Functions for spinlocks, shared with HAL
* PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
*/

#if defined(_M_IX86)
VOID
NTAPI
Kii386SpinOnSpinLock(PKSPIN_LOCK SpinLock, ULONG Flags);
#endif

//
// Spinlock Acquisition at IRQL >= DISPATCH_LEVEL
//
_Acquires_nonreentrant_lock_(SpinLock)
FORCEINLINE
VOID
KxAcquireSpinLock(
#if defined(CONFIG_SMP) || DBG
    _Inout_
#else
    _Unreferenced_parameter_
#endif
    PKSPIN_LOCK SpinLock)
{
#if DBG
    KSPIN_LOCK StoredBefore, StoredAfter;
    KSPIN_LOCK Owner = (KSPIN_LOCK)KeGetCurrentThread() | 1;

    /* Validate spinlock pointer */
    if (SpinLock == NULL)
    {
        /* Critical error: NULL spinlock pointer */
        KeBugCheckEx(SPIN_LOCK_INIT_FAILURE, (ULONG_PTR)SpinLock, 0, 0, 0);
    }

    StoredBefore = *SpinLock;

    /* Make sure that we don't own the lock already */
    if (Owner == StoredBefore)
    {
        /* We do, bugcheck! */
        KeBugCheckEx(SPIN_LOCK_ALREADY_OWNED, (ULONG_PTR)SpinLock, 0, 0, 0);
    }
#endif

#ifdef CONFIG_SMP
    /* Try to acquire the lock */
    while (InterlockedBitTestAndSet((PLONG)SpinLock, 0))
    {
#if defined(_M_IX86) && DBG
        /* On x86 debug builds, we use a much slower but useful routine */
        Kii386SpinOnSpinLock(SpinLock, 5);
#else
        /* It's locked... spin until it's unlocked */
        while (*(volatile KSPIN_LOCK *)SpinLock & 1)
        {
                /* Yield and keep looping */
                YieldProcessor();
        }
#endif
    }
#endif

    /* Add an explicit memory barrier to prevent the compiler from reordering
       memory accesses across the borders of spinlocks */
    KeMemoryBarrierWithoutFence();

#if DBG
    /* On debug builds, we OR in the KTHREAD */
    *SpinLock = (KSPIN_LOCK)KeGetCurrentThread() | 1;
    StoredAfter = *SpinLock;
#endif
}

//
// Spinlock Release at IRQL >= DISPATCH_LEVEL
//
_Releases_nonreentrant_lock_(SpinLock)
FORCEINLINE
VOID
KxReleaseSpinLock(
#if defined(CONFIG_SMP) || DBG
    _Inout_
#else
    _Unreferenced_parameter_
#endif
    PKSPIN_LOCK SpinLock)
{
#if DBG
    KSPIN_LOCK Owner = (KSPIN_LOCK)KeGetCurrentThread() | 1;
    KSPIN_LOCK Stored;
    BOOLEAN Mismatch;

    /* Validate spinlock pointer */
    if (SpinLock == NULL)
    {
        /* Critical error: NULL spinlock pointer */
        KeBugCheckEx(SPIN_LOCK_INIT_FAILURE, (ULONG_PTR)SpinLock, (ULONG_PTR)Owner, 0, 1);
    }

    Stored = *SpinLock;
    Mismatch = (Owner != Stored);

    /* Make sure that the threads match */
    if (Mismatch)
    {
        /* They don't, bugcheck */
        KeBugCheckEx(SPIN_LOCK_NOT_OWNED, (ULONG_PTR)SpinLock, (ULONG_PTR)Owner, (ULONG_PTR)Stored, 0);
    }
#endif

#if defined(CONFIG_SMP) || DBG
    /* Clear the lock  */
#ifdef _WIN64
    InterlockedAnd64((PLONG64)SpinLock, 0);
#else
    InterlockedAnd((PLONG)SpinLock, 0);
#endif
#endif

    /* Add an explicit memory barrier to prevent the compiler from reordering
       memory accesses across the borders of spinlocks */
    KeMemoryBarrierWithoutFence();
}
