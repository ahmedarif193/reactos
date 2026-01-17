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
    KSPIN_LOCK StoredBefore;
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
#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64 CRITICAL: Use LDAXR/STXR (Load-Acquire Exclusive / Store Exclusive)
     * for proper memory ordering. This ensures:
     * 1. LDAXR provides acquire semantics - all subsequent loads/stores happen after lock acquisition
     * 2. The exclusive monitor tracks the lock address for atomic compare-and-swap
     * 3. STXR provides atomicity for the lock acquisition
     *
     * IMPORTANT: We use 64-bit operations (%x registers) because KSPIN_LOCK is 64-bit
     * on ARM64, and in DBG builds the lock is overwritten with the thread pointer.
     * Using 32-bit operations would leave the upper 32 bits non-zero after release,
     * causing deadlocks.
     *
     * The generic InterlockedBitTestAndSet does NOT provide these guarantees on ARM64.
     */
    {
        ULONG tmp;
        KSPIN_LOCK current;
        KSPIN_LOCK *LockPtr = SpinLock;

        __asm__ __volatile__(
            "1: ldaxr   %[current], [%[lockptr]]    \n"  // current = *SpinLock (Load-Acquire Exclusive 64-bit)
            "   cbnz    %[current], 2f              \n"  // if (current != 0) goto spin_wait
            "   mov     %[current], #1              \n"  // current = 1 (reuse register for store value)
            "   stxr    %w[tmp], %[current], [%[lockptr]] \n"  // *SpinLock = 1 (Store Exclusive 64-bit, status in w[tmp])
            "   cbnz    %w[tmp], 1b                 \n"  // if (store failed) retry from ldaxr
            "   b       3f                          \n"  // success, exit
            "2: wfe                                 \n"  // spin_wait: Wait For Event (low-power spin)
            "   ldaxr   %[current], [%[lockptr]]    \n"  // Re-check if unlocked (64-bit)
            "   cbnz    %[current], 2b              \n"  // Still locked, keep spinning
            "   b       1b                          \n"  // Unlocked, try to acquire
            "3:                                     \n"  // success
            : [tmp] "=&r" (tmp), [current] "=&r" (current)
            : [lockptr] "r" (LockPtr)
            : "memory"
        );
    }
#else
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
#endif /* _M_ARM64 */
#endif /* CONFIG_SMP */

#if DBG
    /* Track ownership in debug builds to satisfy release checks. */
    *SpinLock = Owner;
#endif

    /* Add an explicit memory barrier to prevent the compiler from reordering
       memory accesses across the borders of spinlocks */
    KeMemoryBarrierWithoutFence();
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
#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64 CRITICAL: Use STLR (Store-Release) for spinlock release.
     *
     * This ensures:
     * 1. All loads/stores before the unlock are visible to other CPUs
     * 2. Release semantics - memory operations before STLR cannot be reordered after it
     * 3. SEV (Send Event) wakes up CPUs waiting on WFE in the acquire path
     *
     * IMPORTANT: We use 64-bit STLR because KSPIN_LOCK is 64-bit on ARM64.
     * In DBG builds, the lock contains a 64-bit thread pointer, so we MUST
     * zero all 64 bits. Using 32-bit STLR would leave the upper 32 bits non-zero,
     * causing deadlocks.
     *
     * The generic InterlockedAnd64 does NOT provide release semantics on ARM64.
     */
    {
        KSPIN_LOCK *LockPtr = SpinLock;
        KSPIN_LOCK zero = 0;

        __asm__ __volatile__(
            "   stlr    %[zero], [%[lockptr]]       \n"  // *SpinLock = 0 (Store-Release 64-bit)
            "   sev                                 \n"  // Send Event (wake waiting CPUs)
            :
            : [zero] "r" (zero), [lockptr] "r" (LockPtr)
            : "memory"
        );
    }
#else
    #ifdef _WIN64
        InterlockedAnd64((PLONG64)SpinLock, 0);
    #else
        InterlockedAnd((PLONG)SpinLock, 0);
    #endif
#endif /* _M_ARM64 */
#endif /* CONFIG_SMP || DBG */

    /*
     * Note: KeMemoryBarrierWithoutFence is not needed on ARM64 because
     * STLR already provides the necessary memory barrier with release semantics.
     * For other architectures, keep the barrier for safety.
     */
#if !defined(_M_ARM64) && !defined(__aarch64__)
    /* Add an explicit memory barrier to prevent the compiler from reordering
       memory accesses across the borders of spinlocks */
    KeMemoryBarrierWithoutFence();
#endif
}
