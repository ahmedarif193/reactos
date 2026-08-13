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

#if defined(_M_AMD64) || defined(_M_ARM64)
FORCEINLINE
ULONG_PTR
KxLoadAcquirePointer(
    _In_ PVOID const volatile *Address)
{
#ifdef _M_ARM64
    ULONG_PTR Value;

    __asm__ __volatile__("ldar %0, [%1]"
                         : "=&r"(Value)
                         : "r"(Address)
                         : "memory");
    return Value;
#else
    return (ULONG_PTR)ReadPointerAcquire(Address);
#endif
}

FORCEINLINE
VOID
KxStoreReleasePointer(
    _Out_ PVOID volatile *Address,
    _In_ PVOID Value)
{
#ifdef _M_ARM64
    __asm__ __volatile__("stlr %1, [%0]"
                         :
                         : "r"(Address), "r"(Value)
                         : "memory");
#else
    WritePointerRelease(Address, Value);
#endif
}
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
    /* Make sure that we don't own the lock already */
    if (((KSPIN_LOCK)KeGetCurrentThread() | 1) == *SpinLock)
    {
        /* We do, bugcheck! */
        KeBugCheckEx(SPIN_LOCK_ALREADY_OWNED,
                     (ULONG_PTR)SpinLock,
                     (ULONG_PTR)_ReturnAddress(),
                     (ULONG_PTR)KeGetCurrentThread(),
                     (ULONG_PTR)KeGetCurrentIrql());
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
    /* Make sure that the threads match */
    if (((KSPIN_LOCK)KeGetCurrentThread() | 1) != *SpinLock)
    {
        /* They don't, bugcheck */
        KeBugCheckEx(SPIN_LOCK_NOT_OWNED, (ULONG_PTR)SpinLock, 0, 0, 0);
    }
#endif

#if defined(CONFIG_SMP) || DBG
    /* Publish the unlocked state with release ordering. */
#ifdef _M_ARM64
    __asm__ __volatile__("stlr xzr, [%0]" :: "r"(SpinLock) : "memory");
#elif defined(_M_AMD64)
    WriteULongPtrRelease((PULONG_PTR)SpinLock, 0);
#elif defined(_WIN64)
    InterlockedAnd64((PLONG64)SpinLock, 0);
#else
    InterlockedAnd((PLONG)SpinLock, 0);
#endif
#endif

    /* Add an explicit memory barrier to prevent the compiler from reordering
       memory accesses across the borders of spinlocks */
    KeMemoryBarrierWithoutFence();
}

#if defined(_M_AMD64) || defined(_M_ARM64)

#define KX_LOCK_QUEUE_WAIT  ((ULONG_PTR)LOCK_QUEUE_WAIT)
#define KX_LOCK_QUEUE_OWNER ((ULONG_PTR)LOCK_QUEUE_OWNER)
#define KX_LOCK_QUEUE_FLAGS (KX_LOCK_QUEUE_WAIT | KX_LOCK_QUEUE_OWNER)

/*
 * Queued spinlocks use the KSPIN_LOCK as an MCS queue tail. Each acquisition
 * supplies a distinct KSPIN_LOCK_QUEUE node. The low bits of the node's Lock
 * pointer describe whether that node is waiting or owns the lock; release
 * restores the pointer before the node can be reused.
 */
FORCEINLINE
VOID
KxAcquireQueuedSpinLock(
    _Inout_ PKSPIN_LOCK_QUEUE LockQueue)
{
#if defined(CONFIG_SMP) || DBG
    PKSPIN_LOCK SpinLock;
    PKSPIN_LOCK_QUEUE Predecessor;
    ULONG_PTR QueueState;

    SpinLock = LockQueue->Lock;
    ASSERT(SpinLock != NULL);
    ASSERT(((ULONG_PTR)SpinLock & KX_LOCK_QUEUE_FLAGS) == 0);

    LockQueue->Next = NULL;
    LockQueue->Lock = (PKSPIN_LOCK)((ULONG_PTR)SpinLock | KX_LOCK_QUEUE_WAIT);

    Predecessor = (PKSPIN_LOCK_QUEUE)InterlockedExchangePointer(
        (PVOID volatile *)SpinLock,
        LockQueue);
    if (Predecessor == NULL)
    {
        LockQueue->Lock = (PKSPIN_LOCK)((ULONG_PTR)SpinLock | KX_LOCK_QUEUE_OWNER);
        return;
    }

    KxStoreReleasePointer((PVOID volatile *)&Predecessor->Next, LockQueue);

    do
    {
        QueueState = KxLoadAcquirePointer((PVOID const volatile *)&LockQueue->Lock);
        if (!(QueueState & KX_LOCK_QUEUE_WAIT))
            break;
        YieldProcessor();
    } while (TRUE);

    ASSERT((QueueState & KX_LOCK_QUEUE_OWNER) != 0);
#else
    UNREFERENCED_PARAMETER(LockQueue);
    KeMemoryBarrierWithoutFence();
#endif
}

FORCEINLINE
VOID
KxReleaseQueuedSpinLock(
    _Inout_ PKSPIN_LOCK_QUEUE LockQueue)
{
#if defined(CONFIG_SMP) || DBG
    PKSPIN_LOCK SpinLock;
    PKSPIN_LOCK_QUEUE Successor;

    ASSERT(((ULONG_PTR)LockQueue->Lock & KX_LOCK_QUEUE_OWNER) != 0);

    SpinLock = (PKSPIN_LOCK)((ULONG_PTR)LockQueue->Lock & ~KX_LOCK_QUEUE_FLAGS);
    ASSERT(SpinLock != NULL);

    Successor = (PKSPIN_LOCK_QUEUE)KxLoadAcquirePointer(
        (PVOID const volatile *)&LockQueue->Next);
    if (Successor == NULL)
    {
        if (InterlockedCompareExchangePointer((PVOID volatile *)SpinLock,
                                              NULL,
                                              LockQueue) == LockQueue)
        {
            LockQueue->Lock = SpinLock;
            LockQueue->Next = NULL;
            return;
        }

        do
        {
            Successor = (PKSPIN_LOCK_QUEUE)KxLoadAcquirePointer(
                (PVOID const volatile *)&LockQueue->Next);
            if (Successor != NULL)
                break;
            YieldProcessor();
        } while (TRUE);
    }
    ASSERT(((ULONG_PTR)Successor->Lock & ~KX_LOCK_QUEUE_FLAGS) ==
           (ULONG_PTR)SpinLock);
    KxStoreReleasePointer(
        (PVOID volatile *)&Successor->Lock,
        (PVOID)((ULONG_PTR)SpinLock | KX_LOCK_QUEUE_OWNER));

    LockQueue->Next = NULL;
    LockQueue->Lock = SpinLock;
#else
    UNREFERENCED_PARAMETER(LockQueue);
    KeMemoryBarrierWithoutFence();
#endif
}

FORCEINLINE
BOOLEAN
KxTryToAcquireQueuedSpinLock(
    _Inout_ PKSPIN_LOCK_QUEUE LockQueue)
{
#if defined(CONFIG_SMP) || DBG
    PKSPIN_LOCK SpinLock;
    ULONG_PTR QueueState;

    QueueState = (ULONG_PTR)LockQueue->Lock;
    ASSERT(QueueState != 0);
    if ((QueueState == 0) || (QueueState & KX_LOCK_QUEUE_FLAGS))
    {
        ASSERT(FALSE);
        return FALSE;
    }
    SpinLock = (PKSPIN_LOCK)QueueState;

    LockQueue->Next = NULL;
    if (InterlockedCompareExchangePointer((PVOID volatile *)SpinLock,
                                          LockQueue,
                                          NULL) != NULL)
    {
        return FALSE;
    }

    LockQueue->Lock = (PKSPIN_LOCK)((ULONG_PTR)SpinLock | KX_LOCK_QUEUE_OWNER);
    return TRUE;
#else
    UNREFERENCED_PARAMETER(LockQueue);
    KeMemoryBarrierWithoutFence();
    return TRUE;
#endif
}

#endif /* defined(_M_AMD64) || defined(_M_ARM64) */
