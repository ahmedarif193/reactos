/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 spin-lock primitives
 *
 * KeAcquireSpinLock / KeReleaseSpinLock round-trip,
 * KeAcquireInStackQueuedSpinLock / KeReleaseInStackQueuedSpinLock,
 * verifies Prcb->LockQueue[] sizing.
 */

#include <kmt_test.h>

VOID Test_KeArm64SpinLock(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static VOID Arm64SpinLockCheck(VOID)
{
    KAFFINITY PreviousAffinity;
    PKPRCB Prcb;
    KSPIN_LOCK Lock;
    KIRQL OldIrql;
    KLOCK_QUEUE_HANDLE Handle;

    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    Prcb = KeGetCurrentPrcb();
    ok(Prcb != NULL, "Prcb NULL\n");

    /* KSPIN_LOCK acquire/release. */
    KeInitializeSpinLock(&Lock);
    ok_eq_ulonglong((ULONGLONG)Lock, 0ULL);

    KeAcquireSpinLock(&Lock, &OldIrql);
    ok_eq_uint(OldIrql, PASSIVE_LEVEL);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    KeReleaseSpinLock(&Lock, OldIrql);
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);
    /* After release lock is back to 0. */
    ok_eq_ulonglong((ULONGLONG)Lock, 0ULL);

    /* In-stack queued spin lock round-trip. */
    KeAcquireInStackQueuedSpinLock(&Lock, &Handle);
    ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
    KeReleaseInStackQueuedSpinLock(&Handle);
    ok_eq_uint(KeGetCurrentIrql(), PASSIVE_LEVEL);

    /* Acquire/release at DPC level (no IRQL change). */
    {
        KIRQL Old;
        KeRaiseIrql(DISPATCH_LEVEL, &Old);
        KeAcquireSpinLockAtDpcLevel(&Lock);
        ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
        KeReleaseSpinLockFromDpcLevel(&Lock);
        ok_eq_uint(KeGetCurrentIrql(), DISPATCH_LEVEL);
        KeLowerIrql(Old);
    }

    /* Prcb->LockQueue array sized per LockQueueMaximumLock. */
    if (Prcb)
    {
        PKSPIN_LOCK_QUEUE SpareQueue = &Prcb->LockQueue[LockQueueUnusedSpare16];
        PKSPIN_LOCK SavedLock = SpareQueue->Lock;
        PKSPIN_LOCK_QUEUE SavedNext = SpareQueue->Next;

        ok_eq_size(sizeof(Prcb->LockQueue) / sizeof(Prcb->LockQueue[0]),
                   (SIZE_T)LockQueueMaximumLock);
        /* On ARM64 LockQueueMaximumLock = LockQueueUnusedSpare16 + 1 = 17. */
        ok_eq_uint(LockQueueMaximumLock, 17);

        /* A failed queued try must restore IRQL and leave OldIrql untouched. */
        if ((SavedLock == NULL) && (SavedNext == NULL))
        {
            LOGICAL Acquired;
            KIRQL ObservedIrql;

            Lock = 1;
            SpareQueue->Lock = &Lock;
            OldIrql = 0x55;
            Acquired = KeTryToAcquireQueuedSpinLock(LockQueueUnusedSpare16, &OldIrql);
            ObservedIrql = KeGetCurrentIrql();

            if (ObservedIrql != PASSIVE_LEVEL)
            {
                KeLowerIrql(PASSIVE_LEVEL);
            }

            SpareQueue->Next = SavedNext;
            SpareQueue->Lock = SavedLock;

            ok_eq_bool(Acquired, FALSE);
            ok_eq_uint(ObservedIrql, PASSIVE_LEVEL);
            ok_eq_uint(OldIrql, 0x55);
        }
        else
        {
            skip(FALSE, "Unused lock queue is already in use\n");
        }
    }

    /* KSPIN_LOCK is ULONG_PTR on ARM64. */
    ok_eq_size(sizeof(KSPIN_LOCK), (SIZE_T)8);

    /* KSPIN_LOCK_QUEUE is 16 bytes (Next + Lock). */
    ok_eq_size(sizeof(KSPIN_LOCK_QUEUE), (SIZE_T)16);

    dump_trace("[arm64][KeArm64SpinLock] LockQueueMaximumLock=%u sizeof(KSPIN_LOCK_QUEUE)=%Iu\n",
               LockQueueMaximumLock, (SIZE_T)sizeof(KSPIN_LOCK_QUEUE));
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

#define LOCK_STRESS_ROUNDS 4096
#define LOCK_STRESS_WORDS 32

typedef struct _LOCK_STRESS_SHARED
{
    KSPIN_LOCK Lock;
    KEVENT Go;
    ULONG Mode;
    volatile ULONG Owner;
    volatile ULONG Sequence;
    volatile ULONG64 Payload[LOCK_STRESS_WORDS];
} LOCK_STRESS_SHARED;

typedef struct _LOCK_STRESS_WORKER
{
    LOCK_STRESS_SHARED *Shared;
    KEVENT Ready;
    ULONG Cpu;
    ULONG Rounds;
    ULONG DataErrors;
    ULONG OwnerErrors;
    ULONG StateErrors;
} LOCK_STRESS_WORKER;

static ULONG64
LockPayload(ULONG Sequence, ULONG Index)
{
    return ((ULONG64)Sequence << 32) ^ (0xC3A594867102E8DFULL + Index);
}

static VOID NTAPI
LockStressThread(PVOID Parameter)
{
    LOCK_STRESS_WORKER *Worker = Parameter;
    LOCK_STRESS_SHARED *Shared = Worker->Shared;
    KAFFINITY PreviousAffinity;
    KLOCK_QUEUE_HANDLE Handle;
    KIRQL OldIrql, ExpectedIrql;
    ULONG Round, Index, Sequence;

    PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << Worker->Cpu);
    KeSetPriorityThread(KeGetCurrentThread(), 8);
    KeSetEvent(&Worker->Ready, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(&Shared->Go, Executive, KernelMode, FALSE, NULL);
    ExpectedIrql = Shared->Mode == 1 || Shared->Mode == 3 ? SYNCH_LEVEL : DISPATCH_LEVEL;

    for (Round = 0; Round < LOCK_STRESS_ROUNDS; Round++)
    {
        switch (Shared->Mode)
        {
            case 0:
                KeAcquireSpinLock(&Shared->Lock, &OldIrql);
                break;
            case 1:
                OldIrql = KeAcquireSpinLockRaiseToSynch(&Shared->Lock);
                break;
            case 2:
                KeAcquireInStackQueuedSpinLock(&Shared->Lock, &Handle);
                OldIrql = Handle.OldIrql;
                break;
            case 3:
                KeAcquireInStackQueuedSpinLockRaiseToSynch(&Shared->Lock, &Handle);
                OldIrql = Handle.OldIrql;
                break;
            case 4:
                KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
                KeAcquireSpinLockAtDpcLevel(&Shared->Lock);
                break;
            default:
                KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
                KeAcquireInStackQueuedSpinLockAtDpcLevel(&Shared->Lock, &Handle);
                break;
        }

        /* No interlocked operation may hide missing lock memory ordering. */
        Sequence = Shared->Sequence;
        for (Index = 0; Index < LOCK_STRESS_WORDS; Index++)
        {
            if (Shared->Payload[Index] != LockPayload(Sequence, Index))
                Worker->DataErrors++;
        }
        if (Shared->Owner != MAXULONG) Worker->OwnerErrors++;
        Shared->Owner = Worker->Cpu;
        if (KeGetCurrentProcessorNumber() != Worker->Cpu ||
            KeGetCurrentIrql() != ExpectedIrql || OldIrql != PASSIVE_LEVEL ||
            !KmtAreInterruptsEnabled())
        {
            Worker->StateErrors++;
        }
        for (Index = 0; Index < LOCK_STRESS_WORDS; Index++)
            Shared->Payload[Index] = LockPayload(Sequence + 1, Index);
        if (Shared->Owner != Worker->Cpu) Worker->OwnerErrors++;
        Shared->Owner = MAXULONG;
        Shared->Sequence = Sequence + 1;

        switch (Shared->Mode)
        {
            case 0:
            case 1:
                KeReleaseSpinLock(&Shared->Lock, OldIrql);
                break;
            case 2:
            case 3:
                KeReleaseInStackQueuedSpinLock(&Handle);
                break;
            case 4:
                KeReleaseSpinLockFromDpcLevel(&Shared->Lock);
                KeLowerIrql(OldIrql);
                break;
            default:
                KeReleaseInStackQueuedSpinLockFromDpcLevel(&Handle);
                KeLowerIrql(OldIrql);
                break;
        }
        if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
            KeGetCurrentProcessorNumber() != Worker->Cpu ||
            !KeGetCurrentThread()->SystemAffinityActive || !KmtAreInterruptsEnabled())
        {
            Worker->StateErrors++;
        }
        Worker->Rounds++;
    }
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

static VOID
Arm64SpinLockContention(ULONG Mode)
{
    LOCK_STRESS_SHARED Shared = {0};
    LOCK_STRESS_WORKER *Workers;
    PKTHREAD Threads[MAXIMUM_PROCESSORS];
    ULONG Cpu, Index, Created = 0, Count = KeNumberProcessors;
    ULONG DataErrors = 0, OwnerErrors = 0, StateErrors = 0, Rounds = 0;

    if (skip(Count >= 2, "Two processors required\n")) return;
    Workers = ExAllocatePoolWithTag(NonPagedPool, Count * sizeof(*Workers), 'slDK');
    ok(Workers != NULL, "Lock stress allocation failed\n");
    if (!Workers) return;
    RtlZeroMemory(Workers, Count * sizeof(*Workers));
    KeInitializeSpinLock(&Shared.Lock);
    KeInitializeEvent(&Shared.Go, NotificationEvent, FALSE);
    Shared.Mode = Mode;
    Shared.Owner = MAXULONG;
    for (Index = 0; Index < LOCK_STRESS_WORDS; Index++)
        Shared.Payload[Index] = LockPayload(0, Index);
    for (Cpu = 0; Cpu < Count; Cpu++)
    {
        Workers[Cpu].Shared = &Shared;
        Workers[Cpu].Cpu = Cpu;
        KeInitializeEvent(&Workers[Cpu].Ready, NotificationEvent, FALSE);
        Threads[Cpu] = KmtStartThread(LockStressThread, &Workers[Cpu]);
        if (!Threads[Cpu]) break;
        Created++;
    }
    for (Cpu = 0; Cpu < Created; Cpu++)
        KeWaitForSingleObject(&Workers[Cpu].Ready, Executive, KernelMode, FALSE, NULL);
    KeSetEvent(&Shared.Go, IO_NO_INCREMENT, FALSE);
    for (Cpu = 0; Cpu < Created; Cpu++)
    {
        KmtFinishThread(Threads[Cpu], NULL);
        ok_eq_ulong(Workers[Cpu].Rounds, LOCK_STRESS_ROUNDS);
        ok_eq_ulong(Workers[Cpu].DataErrors, 0);
        ok_eq_ulong(Workers[Cpu].OwnerErrors, 0);
        ok_eq_ulong(Workers[Cpu].StateErrors, 0);
        Rounds += Workers[Cpu].Rounds;
        DataErrors += Workers[Cpu].DataErrors;
        OwnerErrors += Workers[Cpu].OwnerErrors;
        StateErrors += Workers[Cpu].StateErrors;
    }
    ok_eq_ulong(Created, Count);
    ok_eq_ulong(Shared.Sequence, Count * LOCK_STRESS_ROUNDS);
    ok_eq_ulong(Shared.Owner, MAXULONG);
    ok_eq_ulonglong(Shared.Lock, 0);
    for (Index = 0; Index < LOCK_STRESS_WORDS; Index++)
        ok_eq_ulonglong(Shared.Payload[Index], LockPayload(Shared.Sequence, Index));
    trace("SPINLOCK_STRESS mode=%lu cpus=%lu rounds=%lu sequence=%lu data_errors=%lu owner_errors=%lu state_errors=%lu\n",
          Mode, Created, Rounds, Shared.Sequence, DataErrors, OwnerErrors, StateErrors);
    ExFreePoolWithTag(Workers, 'slDK');
}

#endif /* _M_ARM64 */

START_TEST(KeArm64SpinLock)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64SpinLock is ARM64-only\n");
#else
    ULONG Mode;

    dump_trace("[arm64][KeArm64SpinLock] enter\n");
    Arm64SpinLockCheck();
    for (Mode = 0; Mode < 6; Mode++) Arm64SpinLockContention(Mode);
#endif
}
