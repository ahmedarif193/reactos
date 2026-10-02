/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC spin locks
 */

#include <ntoskrnl.h>

C_ASSERT(sizeof(KSPIN_LOCK) == sizeof(PVOID));

static
PKSPIN_LOCK_QUEUE
KiPpcGetLockQueue(_In_ KSPIN_LOCK_QUEUE_NUMBER Number)
{
    PKSPIN_LOCK_QUEUE Queue;

    ASSERT((ULONG)Number < LockQueueMaximumLock);
    Queue = &KeGetCurrentPrcb()->LockQueue[Number];
    ASSERT(Queue->Lock != NULL);
    return Queue;
}

KIRQL
NTAPI
KeAcquireSpinLockRaiseToDpc(_Inout_ PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(SpinLock);
    return OldIrql;
}

KIRQL
NTAPI
KeAcquireSpinLockRaiseToSynch(_Inout_ PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    KeRaiseIrql(SYNCH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(SpinLock);
    return OldIrql;
}

VOID
NTAPI
KeReleaseSpinLock(_Inout_ PKSPIN_LOCK SpinLock, _In_ KIRQL OldIrql)
{
    KeReleaseSpinLockFromDpcLevel(SpinLock);
    KeLowerIrql(OldIrql);
}

KIRQL
FASTCALL
KeAcquireQueuedSpinLock(_In_ KSPIN_LOCK_QUEUE_NUMBER Number)
{
    KIRQL OldIrql;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KeAcquireQueuedSpinLockAtDpcLevel(KiPpcGetLockQueue(Number));
    return OldIrql;
}

VOID
FASTCALL
KeReleaseQueuedSpinLock(_In_ KSPIN_LOCK_QUEUE_NUMBER Number, _In_ KIRQL OldIrql)
{
    KeReleaseQueuedSpinLockFromDpcLevel(KiPpcGetLockQueue(Number));
    KeLowerIrql(OldIrql);
}

KIRQL
FASTCALL
KeAcquireQueuedSpinLockRaiseToSynch(_In_ KSPIN_LOCK_QUEUE_NUMBER Number)
{
    KIRQL OldIrql;

    KeRaiseIrql(SYNCH_LEVEL, &OldIrql);
    KeAcquireQueuedSpinLockAtDpcLevel(KiPpcGetLockQueue(Number));
    return OldIrql;
}

static
BOOLEAN
KiPpcTryToAcquireQueuedSpinLock(
    _In_ KSPIN_LOCK_QUEUE_NUMBER Number,
    _In_ KIRQL NewIrql,
    _Out_ PKIRQL OldIrql)
{
    KIRQL PreviousIrql;

    KeRaiseIrql(NewIrql, &PreviousIrql);
    if (!KxTryToAcquireQueuedSpinLock(KiPpcGetLockQueue(Number)))
    {
        KeLowerIrql(PreviousIrql);
        return FALSE;
    }

    *OldIrql = PreviousIrql;
    return TRUE;
}

LOGICAL
FASTCALL
KeTryToAcquireQueuedSpinLock(
    _In_ KSPIN_LOCK_QUEUE_NUMBER Number,
    _Out_ PKIRQL OldIrql)
{
    return KiPpcTryToAcquireQueuedSpinLock(Number, DISPATCH_LEVEL, OldIrql);
}

BOOLEAN
FASTCALL
KeTryToAcquireQueuedSpinLockRaiseToSynch(
    _In_ KSPIN_LOCK_QUEUE_NUMBER Number,
    _Out_ PKIRQL OldIrql)
{
    return KiPpcTryToAcquireQueuedSpinLock(Number, SYNCH_LEVEL, OldIrql);
}

VOID
FASTCALL
KeAcquireInStackQueuedSpinLock(_Inout_ PKSPIN_LOCK SpinLock, _Out_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    KeRaiseIrql(DISPATCH_LEVEL, &LockHandle->OldIrql);
    KeAcquireInStackQueuedSpinLockAtDpcLevel(SpinLock, LockHandle);
}

VOID
FASTCALL
KeAcquireInStackQueuedSpinLockRaiseToSynch(_Inout_ PKSPIN_LOCK SpinLock, _Out_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    KeRaiseIrql(SYNCH_LEVEL, &LockHandle->OldIrql);
    KeAcquireInStackQueuedSpinLockAtDpcLevel(SpinLock, LockHandle);
}

VOID
FASTCALL
KeReleaseInStackQueuedSpinLock(_Inout_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    KeReleaseInStackQueuedSpinLockFromDpcLevel(LockHandle);
    KeLowerIrql(LockHandle->OldIrql);
}

BOOLEAN
NTAPI
KeSynchronizeExecution(
    _Inout_ PKINTERRUPT Interrupt,
    _In_ PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
    _In_opt_ PVOID SynchronizeContext)
{
    BOOLEAN Result;
    KIRQL OldIrql;

    OldIrql = KfRaiseIrql(Interrupt->SynchronizeIrql);
    KeAcquireSpinLockAtDpcLevel(Interrupt->ActualLock);
    Result = SynchronizeRoutine(SynchronizeContext);
    KeReleaseSpinLockFromDpcLevel(Interrupt->ActualLock);
    KeLowerIrql(OldIrql);
    return Result;
}
