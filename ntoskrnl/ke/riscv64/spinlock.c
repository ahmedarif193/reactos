/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V spin-lock IRQL wrappers
 */

#include <ntoskrnl.h>

C_ASSERT(sizeof(KSPIN_LOCK) == sizeof(PVOID));

static
PKSPIN_LOCK_QUEUE
KiRiscvGetLockQueue(_In_ KSPIN_LOCK_QUEUE_NUMBER Number)
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
    KeAcquireQueuedSpinLockAtDpcLevel(KiRiscvGetLockQueue(Number));
    return OldIrql;
}

KIRQL
FASTCALL
KeAcquireQueuedSpinLockRaiseToSynch(_In_ KSPIN_LOCK_QUEUE_NUMBER Number)
{
    KIRQL OldIrql;
    KeRaiseIrql(SYNCH_LEVEL, &OldIrql);
    KeAcquireQueuedSpinLockAtDpcLevel(KiRiscvGetLockQueue(Number));
    return OldIrql;
}

LOGICAL
FASTCALL
KeTryToAcquireQueuedSpinLock(_In_ KSPIN_LOCK_QUEUE_NUMBER Number, _Out_ PKIRQL OldIrql)
{
    KIRQL PreviousIrql;

    KeRaiseIrql(DISPATCH_LEVEL, &PreviousIrql);
    if (!KxTryToAcquireQueuedSpinLock(KiRiscvGetLockQueue(Number)))
    {
        KeLowerIrql(PreviousIrql);
        return FALSE;
    }

    *OldIrql = PreviousIrql;
    return TRUE;
}

/* As on the other architectures, IRQL stays at SYNCH_LEVEL when the lock is
 * not acquired. */
BOOLEAN
FASTCALL
KeTryToAcquireQueuedSpinLockRaiseToSynch(_In_ KSPIN_LOCK_QUEUE_NUMBER Number, _Out_ PKIRQL OldIrql)
{
    KeRaiseIrql(SYNCH_LEVEL, OldIrql);
    return KxTryToAcquireQueuedSpinLock(KiRiscvGetLockQueue(Number));
}

VOID
FASTCALL
KeReleaseQueuedSpinLock(_In_ KSPIN_LOCK_QUEUE_NUMBER Number, _In_ KIRQL OldIrql)
{
    KeReleaseQueuedSpinLockFromDpcLevel(KiRiscvGetLockQueue(Number));
    KeLowerIrql(OldIrql);
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
