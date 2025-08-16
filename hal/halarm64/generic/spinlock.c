/*
 * PROJECT:     ReactOS ARM64 HAL
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Spinlock Support for ARM64
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <hal.h>

/* Undefine the macros that might interfere */
#undef KfRaiseIrql
#undef KfLowerIrql

/* Forward declarations for HAL-internal functions */
KIRQL FASTCALL KfRaiseIrql(IN KIRQL NewIrql);
VOID FASTCALL KfLowerIrql(IN KIRQL OldIrql);

/* FUNCTIONS *****************************************************************/

KIRQL
FASTCALL
KeAcquireSpinLockRaiseToSynch(
    IN PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;
    
    /* Raise IRQL to SYNCH_LEVEL */
    OldIrql = KfRaiseIrql(SYNCH_LEVEL);
    
    /* Acquire spinlock */
    while (_InterlockedCompareExchange64((PLONG64)SpinLock, 1, 0) != 0)
    {
        /* Spin */
        YieldProcessor();
    }
    
    return OldIrql;
}

KIRQL
FASTCALL
KfAcquireSpinLock(
    IN PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;
    
    /* Raise IRQL to DISPATCH_LEVEL */
    OldIrql = KfRaiseIrql(DISPATCH_LEVEL);
    
    /* Acquire spinlock */
    while (_InterlockedCompareExchange64((PLONG64)SpinLock, 1, 0) != 0)
    {
        /* Spin */
        YieldProcessor();
    }
    
    return OldIrql;
}

VOID
FASTCALL
KfReleaseSpinLock(
    IN PKSPIN_LOCK SpinLock,
    IN KIRQL OldIrql)
{
    /* Release spinlock */
    _InterlockedExchange64((PLONG64)SpinLock, 0);
    
    /* Lower IRQL */
    KfLowerIrql(OldIrql);
}

VOID
FASTCALL
KeAcquireInStackQueuedSpinLock(
    IN PKSPIN_LOCK SpinLock,
    IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    /* Simplified implementation */
    LockHandle->OldIrql = KfAcquireSpinLock(SpinLock);
    LockHandle->LockQueue.Lock = SpinLock;
}

VOID
FASTCALL
KeReleaseInStackQueuedSpinLock(
    IN PKLOCK_QUEUE_HANDLE LockHandle)
{
    /* Simplified implementation */
    KfReleaseSpinLock(LockHandle->LockQueue.Lock, LockHandle->OldIrql);
}

BOOLEAN
FASTCALL
KeTryToAcquireSpinLockAtDpcLevel(
    IN PKSPIN_LOCK SpinLock)
{
    /* Try to acquire spinlock */
    return (_InterlockedCompareExchange64((PLONG64)SpinLock, 1, 0) == 0);
}

/* Fast Mutex Support */

VOID
FASTCALL
ExAcquireFastMutex(
    IN PFAST_MUTEX FastMutex)
{
    KIRQL OldIrql;
    
    /* Raise IRQL to APC_LEVEL */
    OldIrql = KfRaiseIrql(APC_LEVEL);
    
    /* Save the old IRQL */
    FastMutex->OldIrql = OldIrql;
    
    /* Acquire the mutex */
    while (_InterlockedCompareExchange(&FastMutex->Count, 0, 1) != 1)
    {
        /* Spin */
        YieldProcessor();
    }
}

VOID
FASTCALL
ExReleaseFastMutex(
    IN PFAST_MUTEX FastMutex)
{
    KIRQL OldIrql;
    
    /* Get the old IRQL */
    OldIrql = FastMutex->OldIrql;
    
    /* Release the mutex */
    _InterlockedExchange(&FastMutex->Count, 1);
    
    /* Lower IRQL */
    KfLowerIrql(OldIrql);
}

BOOLEAN
FASTCALL
ExTryToAcquireFastMutex(
    IN PFAST_MUTEX FastMutex)
{
    KIRQL OldIrql;
    
    /* Raise IRQL to APC_LEVEL */
    OldIrql = KfRaiseIrql(APC_LEVEL);
    
    /* Try to acquire the mutex */
    if (_InterlockedCompareExchange(&FastMutex->Count, 0, 1) == 1)
    {
        /* Success - save the old IRQL */
        FastMutex->OldIrql = OldIrql;
        return TRUE;
    }
    else
    {
        /* Failed - restore IRQL */
        KfLowerIrql(OldIrql);
        return FALSE;
    }
}

/* EOF */