/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Spin lock support for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#undef KeAcquireSpinLock
#undef KeReleaseSpinLock

extern BOOLEAN ExpArm64PoolBootstrapMode;
VOID KiArm64BootStageLog(_In_z_ PCSTR Stage);

#if DBG && (defined(_M_ARM64) || defined(__aarch64__))
VOID
KiArm64DebugLogSpinAcquire(
    _In_ PKSPIN_LOCK Lock,
    _In_ KSPIN_LOCK Owner,
    _In_ KSPIN_LOCK StoredBefore,
    _In_ KSPIN_LOCK StoredAfter)
{
    CHAR Buf[192];

    if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                      sizeof(Buf),
                                      "[arm64] KxAcquireSpinLock: lock=%p owner=%p stored_before=%p stored_after=%p",
                                      Lock,
                                      (PVOID)Owner,
                                      (PVOID)StoredBefore,
                                      (PVOID)StoredAfter)))
    {
        KiArm64BootStageLog(Buf);
    }
}

VOID
KiArm64DebugLogSpinRelease(
    _In_ PKSPIN_LOCK Lock,
    _In_ KSPIN_LOCK Owner,
    _In_ KSPIN_LOCK Stored,
    _In_ BOOLEAN Mismatch)
{
    CHAR Buf[192];

    if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                      sizeof(Buf),
                                      "[arm64] KxReleaseSpinLock: lock=%p owner=%p stored=%p mismatch=%lu",
                                      Lock,
                                      (PVOID)Owner,
                                      (PVOID)Stored,
                                      (ULONG)Mismatch)))
    {
        KiArm64BootStageLog(Buf);
    }
}
#endif
VOID KiArm64BootStageLog(_In_z_ PCSTR Stage);

KIRQL
FASTCALL
KfAcquireSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    OldIrql = KfRaiseIrql(DISPATCH_LEVEL);
    KxAcquireSpinLock(SpinLock);
    return OldIrql;
}

VOID
FASTCALL
KfReleaseSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql)
{
    CHAR Buf[128];
    KIRQL CurIrql = KeGetCurrentIrql();

    if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                      sizeof(Buf),
                                      "[arm64] KfReleaseSpinLock: entry Lock=%p OldIrql=%lu CurIrql=%lu",
                                      SpinLock,
                                      (ULONG)OldIrql,
                                      (ULONG)CurIrql)))
    {
        KiArm64BootStageLog(Buf);
    }

    KxReleaseSpinLock(SpinLock);

    CurIrql = KeGetCurrentIrql();
    if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                      sizeof(Buf),
                                      "[arm64] KfReleaseSpinLock: after KxReleaseSpinLock CurIrql=%lu",
                                      (ULONG)CurIrql)))
    {
        KiArm64BootStageLog(Buf);
    }

    if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                      sizeof(Buf),
                                      "[arm64] KfReleaseSpinLock: before KfLowerIrql NewIrql=%lu CurIrql=%lu",
                                      (ULONG)OldIrql,
                                      (ULONG)CurIrql)))
    {
        KiArm64BootStageLog(Buf);
    }

    KfLowerIrql(OldIrql);

    CurIrql = KeGetCurrentIrql();
    if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                      sizeof(Buf),
                                      "[arm64] KfReleaseSpinLock: after KfLowerIrql CurIrql=%lu",
                                      (ULONG)CurIrql)))
    {
        KiArm64BootStageLog(Buf);
    }
}

VOID
NTAPI
KeAcquireSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _Out_ PKIRQL OldIrql)
{
    *OldIrql = KfAcquireSpinLock(SpinLock);
}

VOID
NTAPI
KeReleaseSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql)
{
    KfReleaseSpinLock(SpinLock, OldIrql);
}

KIRQL
FASTCALL
KeAcquireSpinLockRaiseToDpc(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KxAcquireSpinLock(SpinLock);
    return OldIrql;
}

KIRQL
FASTCALL
KeAcquireSpinLockRaiseToSynch(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    KeRaiseIrql(SYNCH_LEVEL, &OldIrql);
    KxAcquireSpinLock(SpinLock);
    return OldIrql;
}
KIRQL
FASTCALL
KeAcquireQueuedSpinLock(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber)
{
    KIRQL OldIrql;

#if defined(_M_ARM64) || defined(__aarch64__)
    if (ExpArm64PoolBootstrapMode && LockNumber == LockQueueMmNonPagedPoolLock)
    {
        return PASSIVE_LEVEL;
    }
#endif

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KxAcquireSpinLock(KeGetCurrentPrcb()->LockQueue[LockNumber].Lock);
    return OldIrql;
}

KIRQL
FASTCALL
KeAcquireQueuedSpinLockRaiseToSynch(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber)
{
    KIRQL OldIrql;

    KeRaiseIrql(SYNCH_LEVEL, &OldIrql);
    KxAcquireSpinLock(KeGetCurrentPrcb()->LockQueue[LockNumber].Lock);
    return OldIrql;
}

VOID
FASTCALL
KeReleaseQueuedSpinLock(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber,
    _In_ KIRQL OldIrql)
{
#if defined(_M_ARM64) || defined(__aarch64__)
    if (ExpArm64PoolBootstrapMode && LockNumber == LockQueueMmNonPagedPoolLock)
    {
        return;
    }
#endif

    KxReleaseSpinLock(KeGetCurrentPrcb()->LockQueue[LockNumber].Lock);
    KeLowerIrql(OldIrql);
}

VOID
FASTCALL
KeAcquireInStackQueuedSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _Inout_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    LockHandle->LockQueue.Next = NULL;
    LockHandle->LockQueue.Lock = SpinLock;

    KeRaiseIrql(DISPATCH_LEVEL, &LockHandle->OldIrql);
    KxAcquireSpinLock(LockHandle->LockQueue.Lock);
}

VOID
FASTCALL
KeAcquireInStackQueuedSpinLockRaiseToSynch(
    _Inout_ PKSPIN_LOCK SpinLock,
    _Inout_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    LockHandle->LockQueue.Next = NULL;
    LockHandle->LockQueue.Lock = SpinLock;

    KeRaiseIrql(SYNCH_LEVEL, &LockHandle->OldIrql);
    KxAcquireSpinLock(LockHandle->LockQueue.Lock);
}

VOID
FASTCALL
KeReleaseInStackQueuedSpinLock(
    _Inout_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    KxReleaseSpinLock(LockHandle->LockQueue.Lock);
    KeLowerIrql(LockHandle->OldIrql);
}

LOGICAL
FASTCALL
KeTryToAcquireQueuedSpinLock(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber,
    _Out_ PKIRQL OldIrql)
{
    KeRaiseIrql(DISPATCH_LEVEL, OldIrql);

#ifdef CONFIG_SMP
    return KeTryToAcquireSpinLockAtDpcLevel(
               KeGetCurrentPrcb()->LockQueue[LockNumber].Lock);
#else
    KeMemoryBarrierWithoutFence();
    return TRUE;
#endif
}

BOOLEAN
FASTCALL
KeTryToAcquireQueuedSpinLockRaiseToSynch(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber,
    _Out_ PKIRQL OldIrql)
{
    KeRaiseIrql(SYNCH_LEVEL, OldIrql);

#ifdef CONFIG_SMP
    return KeTryToAcquireSpinLockAtDpcLevel(
               KeGetCurrentPrcb()->LockQueue[LockNumber].Lock);
#else
    KeMemoryBarrierWithoutFence();
    return TRUE;
#endif
}
