/*
 * PROJECT:         ReactOS Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Executive shared/exclusive spin locks
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS *********************************************************/

static
VOID
ExpValidateExecutiveSpinLock(
    _In_ PEX_SPIN_LOCK SpinLock)
{
    if (((ULONG_PTR)SpinLock & (sizeof(LONG) - 1)) != 0)
    {
        KeBugCheckEx(0x1F6, 0, (ULONG_PTR)SpinLock, sizeof(LONG), 0);
    }
}

static
VOID
ExpAcquireExecutiveSpinLockExclusive(
    _Inout_ PEX_SPIN_LOCK SpinLock)
{
    while (InterlockedCompareExchange((PLONG)SpinLock, -1, 0) != 0)
    {
        YieldProcessor();
    }
}

static
VOID
ExpAcquireExecutiveSpinLockShared(
    _Inout_ PEX_SPIN_LOCK SpinLock)
{
    LONG Value;

    for (;;)
    {
        Value = ReadAcquire((PLONG)SpinLock);
        if (Value >= 0 &&
            InterlockedCompareExchange((PLONG)SpinLock, Value + 1, Value) == Value)
        {
            return;
        }

        YieldProcessor();
    }
}

/* PUBLIC FUNCTIONS **********************************************************/

KIRQL
NTAPI
ExAcquireSpinLockExclusive(
    _Inout_ PEX_SPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    ExpValidateExecutiveSpinLock(SpinLock);
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    ExpAcquireExecutiveSpinLockExclusive(SpinLock);
    return OldIrql;
}

KIRQL
NTAPI
ExAcquireSpinLockShared(
    _Inout_ PEX_SPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    ExpValidateExecutiveSpinLock(SpinLock);
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    ExpAcquireExecutiveSpinLockShared(SpinLock);
    return OldIrql;
}

VOID
NTAPI
ExAcquireSpinLockSharedAtDpcLevel(
    _Inout_ PEX_SPIN_LOCK SpinLock)
{
    ExpValidateExecutiveSpinLock(SpinLock);
    ExpAcquireExecutiveSpinLockShared(SpinLock);
}

VOID
NTAPI
ExAcquireSpinLockExclusiveAtDpcLevel(
    _Inout_ PEX_SPIN_LOCK SpinLock)
{
    ExpValidateExecutiveSpinLock(SpinLock);
    ExpAcquireExecutiveSpinLockExclusive(SpinLock);
}

LOGICAL
NTAPI
ExTryConvertSharedSpinLockExclusive(
    _Inout_ PEX_SPIN_LOCK SpinLock)
{
    ExpValidateExecutiveSpinLock(SpinLock);
    return InterlockedCompareExchange((PLONG)SpinLock, -1, 1) == 1;
}

LOGICAL
NTAPI
ExTryAcquireSpinLockSharedAtDpcLevel(
    _Inout_ PEX_SPIN_LOCK SpinLock)
{
    LONG Value;

    ExpValidateExecutiveSpinLock(SpinLock);
    Value = ReadAcquire((PLONG)SpinLock);
    while (Value >= 0)
    {
        LONG ActualValue;

        ActualValue = InterlockedCompareExchange((PLONG)SpinLock,
                                                 Value + 1,
                                                 Value);
        if (ActualValue == Value)
            return TRUE;
        Value = ActualValue;
    }
    return FALSE;
}

LOGICAL
NTAPI
ExTryAcquireSpinLockExclusiveAtDpcLevel(
    _Inout_ PEX_SPIN_LOCK SpinLock)
{
    ExpValidateExecutiveSpinLock(SpinLock);
    return InterlockedCompareExchange((PLONG)SpinLock, -1, 0) == 0;
}

VOID
NTAPI
ExReleaseSpinLockExclusive(
    _Inout_ PEX_SPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql)
{
    InterlockedExchange((PLONG)SpinLock, 0);
    KeLowerIrql(OldIrql);
}

VOID
NTAPI
ExReleaseSpinLockExclusiveFromDpcLevel(
    _Inout_ PEX_SPIN_LOCK SpinLock)
{
    InterlockedExchange((PLONG)SpinLock, 0);
}

VOID
NTAPI
ExReleaseSpinLockShared(
    _Inout_ PEX_SPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql)
{
    ASSERT(ReadAcquire((PLONG)SpinLock) > 0);
    InterlockedDecrement((PLONG)SpinLock);
    KeLowerIrql(OldIrql);
}

VOID
NTAPI
ExReleaseSpinLockSharedFromDpcLevel(
    _Inout_ PEX_SPIN_LOCK SpinLock)
{
    ASSERT(ReadAcquire((PLONG)SpinLock) > 0);
    InterlockedDecrement((PLONG)SpinLock);
}
