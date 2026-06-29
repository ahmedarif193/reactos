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
#elif defined(_M_AMD64)
    /*
     * AMD64 SMP: Use 64-bit InterlockedCompareExchange for spinlock acquisition.
     *
     * On amd64, KSPIN_LOCK is a 64-bit ULONG_PTR. In DBG builds, the lock is
     * overwritten with the full 64-bit thread pointer (Owner = Thread | 1).
     * The release path uses InterlockedAnd64 to zero all 64 bits.
     *
     * Using InterlockedBitTestAndSet((PLONG)SpinLock, 0) is a 32-bit LOCK BTS
     * that only operates on the lower 32 bits. While architecturally safe on
     * x86-64 (strong memory model, cache-line locking), the mismatch between
     * the 32-bit acquire and 64-bit release creates a race window in DBG builds:
     *
     *   1. CPU A releases: InterlockedAnd64 -> *SpinLock = 0 (all 64 bits)
     *   2. CPU B acquires: 32-bit LOCK BTS sets bit 0 -> *SpinLock = 1 (lower 32)
     *   3. CPU B DBG writes: *SpinLock = ThreadB | 1 (full 64-bit)
     *   4. CPU A tries to re-acquire, DBG reads *SpinLock -> sees ThreadB | 1
     *   5. CPU A's DBG check passes (ThreadA != ThreadB)
     *   6. CPU A spins on InterlockedBitTestAndSet (32-bit) which checks only
     *      bit 0 of lower 32 bits, but the full 64-bit value has upper bits set
     *
     * The 32-bit BTS can also lose a race with the 64-bit AND release if the
     * operations happen on different halves of the same cache line in quick
     * succession on different cores, leading to SPIN_LOCK_ALREADY_OWNED when
     * the DBG ownership check sees a stale value from the local store buffer.
     *
     * Fix: Use a full 64-bit InterlockedCompareExchange64 (LOCK CMPXCHG16B on
     * older CPUs, LOCK CMPXCHG8B equivalent for 64-bit) to atomically transition
     * the lock from 0 to 1. This ensures the acquire and release operations use
     * the same operand size, eliminating any potential 32/64-bit interaction issues.
     */
    while (InterlockedCompareExchange64((PLONG64)SpinLock, 1, 0) != 0)
    {
        /* It's locked... spin until it's unlocked */
        while (*(volatile KSPIN_LOCK *)SpinLock != 0)
        {
            /* Yield and keep looping */
            YieldProcessor();
        }
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
#endif /* _M_ARM64 / _M_AMD64 */
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
