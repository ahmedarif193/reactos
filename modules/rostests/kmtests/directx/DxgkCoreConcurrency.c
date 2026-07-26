/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     The lock-guarded cores under real multi-threaded contention
 *
 * Every core header says "all of these run under one caller-owned lock".  That
 * is a claim about what the caller must do, and it is only meaningful if the
 * lock is genuinely sufficient: the ledgers must balance exactly after
 * concurrent callers hammer them, with no lost count and no leaked slot.
 */

#include <kmt_test.h>
#include "vidmm_core.h"
#include "residency_core.h"
#include "scheduler_core.h"

#define TAG_CORE_CONCURRENCY 'cCxD'
#define CORE_STRESS_THREADS  3
#define CORE_STRESS_ROUNDS   512
#define CORE_STRESS_TIMEOUT_S 30
#define CORE_STRESS_POOL     64
#define CORE_SEGMENT_SIZE    (16ULL * 1024ULL * 1024ULL)
#define CORE_PAGE            0x1000ULL

typedef struct _CORE_STRESS
{
    KSPIN_LOCK Lock;
    DXGMMS2_VIDMM_CORE VidMm;
    DXGMMS2_VIDMM_RANGE RangePool[CORE_STRESS_POOL];
    DXGK_RESIDENCY_REFS Refs;
    DXGMMS2_SCHED_CORE Sched;
    DXGMMS2_SCHED_PACKET Packets[DXGMMS2_SCHED_MAX_PACKETS];
    volatile LONG StartGate;
    volatile LONG SlotClaim;
    volatile LONG ReserveOk;
    volatile LONG ReserveFail;
    volatile LONG ReleaseOk;
    volatile LONG AcquireOk;
    volatile LONG ReleaseRefOk;
    volatile LONG Anomalies;
    KEVENT DoneEvent[CORE_STRESS_THREADS];
} CORE_STRESS, *PCORE_STRESS;

static NTSTATUS WaitForCoreEvent(_In_ PKEVENT Event)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -10LL * 1000LL * 1000LL * CORE_STRESS_TIMEOUT_S;
    return KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, &Timeout);
}

static ULONG ClaimSlot(_Inout_ PCORE_STRESS Stress)
{
    LONG Slot = InterlockedIncrement(&Stress->SlotClaim) - 1;

    return (ULONG)((Slot < 0 || Slot >= CORE_STRESS_THREADS) ? 0 : Slot);
}

/*
 * Each thread reserves and releases placements under its own cookie, and
 * acquires/releases residency references, all under the one documented lock.
 * The counters must balance exactly when every thread has finished.
 */
static VOID NTAPI CoreStressThread(_In_ PVOID Parameter)
{
    PCORE_STRESS Stress = (PCORE_STRESS)Parameter;
    ULONG Slot = ClaimSlot(Stress);
    ULONGLONG Cookie = 0x9000ULL + Slot;
    ULONG Round;

    while (InterlockedCompareExchange(&Stress->StartGate, 0, 0) == 0)
        YieldProcessor();

    for (Round = 0; Round < CORE_STRESS_ROUNDS; ++Round)
    {
        DXGMMS2_VIDMM_RESERVE_INFO_V1 Info;
        ULONGLONG Offset = 0;
        NTSTATUS Status;
        KIRQL OldIrql;

        RtlZeroMemory(&Info, sizeof(Info));
        Info.Size = CORE_PAGE;
        Info.Alignment = CORE_PAGE;
        Info.OwnerCookie = Cookie;

        KeAcquireSpinLock(&Stress->Lock, &OldIrql);
        Status = Dxgmms2VidMmCoreReserve(&Stress->VidMm, 0, &Info, &Offset);
        KeReleaseSpinLock(&Stress->Lock, OldIrql);

        if (NT_SUCCESS(Status))
        {
            InterlockedIncrement(&Stress->ReserveOk);
            /* A placement must never be handed out past the segment. */
            if (Offset >= CORE_SEGMENT_SIZE)
                InterlockedIncrement(&Stress->Anomalies);

            KeAcquireSpinLock(&Stress->Lock, &OldIrql);
            Status = Dxgmms2VidMmCoreRelease(&Stress->VidMm, 0, Cookie);
            KeReleaseSpinLock(&Stress->Lock, OldIrql);
            if (NT_SUCCESS(Status))
                InterlockedIncrement(&Stress->ReleaseOk);
            else
                InterlockedIncrement(&Stress->Anomalies);
        }
        else
        {
            InterlockedIncrement(&Stress->ReserveFail);
        }

        KeAcquireSpinLock(&Stress->Lock, &OldIrql);
        Status = DxgkResidencyCoreAcquire(&Stress->Refs, Cookie);
        KeReleaseSpinLock(&Stress->Lock, OldIrql);
        if (NT_SUCCESS(Status))
        {
            InterlockedIncrement(&Stress->AcquireOk);
            KeAcquireSpinLock(&Stress->Lock, &OldIrql);
            Status = DxgkResidencyCoreRelease(&Stress->Refs, Cookie);
            KeReleaseSpinLock(&Stress->Lock, OldIrql);
            if (NT_SUCCESS(Status))
                InterlockedIncrement(&Stress->ReleaseRefOk);
            else
                InterlockedIncrement(&Stress->Anomalies);
        }
        else
        {
            InterlockedIncrement(&Stress->Anomalies);
        }
    }

    KeSetEvent(&Stress->DoneEvent[Slot], IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID TestLedgersBalance(VOID)
{
    PCORE_STRESS Stress;
    DXGMMS2_VIDMM_SEGMENT_DESC_V1 Desc;
    DXGMMS2_VIDMM_SEGMENT_STATUS_V1 SegStatus;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Threads[CORE_STRESS_THREADS] = { NULL };
    NTSTATUS Status;
    ULONG Index;
    ULONG Started = 0;

    Stress = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Stress), TAG_CORE_CONCURRENCY);
    ok(Stress != NULL, "core stress allocation failed\n");
    if (Stress == NULL)
        return;
    RtlZeroMemory(Stress, sizeof(*Stress));
    KeInitializeSpinLock(&Stress->Lock);
    for (Index = 0; Index < CORE_STRESS_THREADS; ++Index)
        KeInitializeEvent(&Stress->DoneEvent[Index], NotificationEvent, FALSE);

    Dxgmms2VidMmCoreInitialize(&Stress->VidMm, Stress->RangePool, CORE_STRESS_POOL);
    Status = Dxgmms2VidMmCoreStart(&Stress->VidMm, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    RtlZeroMemory(&Desc, sizeof(Desc));
    Desc.Size = CORE_SEGMENT_SIZE;
    Status = Dxgmms2VidMmCoreSetSegment(&Stress->VidMm, 0, &Desc);
    ok_eq_hex(Status, STATUS_SUCCESS);
    DxgkResidencyCoreRefsInitialize(&Stress->Refs, 0x1ULL, FALSE);

    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    for (Index = 0; Index < CORE_STRESS_THREADS; ++Index)
    {
        Status = PsCreateSystemThread(&Threads[Index], THREAD_ALL_ACCESS, &Attributes,
                                      NULL, NULL, CoreStressThread, Stress);
        if (!NT_SUCCESS(Status))
            break;
        Started++;
    }
    ok(Started != 0, "no core stress threads started\n");
    InterlockedExchange(&Stress->StartGate, 1);

    for (Index = 0; Index < Started; ++Index)
    {
        Status = WaitForCoreEvent(&Stress->DoneEvent[Index]);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    /* No operation may report an impossible outcome under contention. */
    ok_eq_long(InterlockedCompareExchange(&Stress->Anomalies, 0, 0), 0L);

    /* Every successful reserve was released, so the segment ledger is empty
     * and its used size is exactly zero — not merely small. */
    ok_eq_long(InterlockedCompareExchange(&Stress->ReserveOk, 0, 0),
               InterlockedCompareExchange(&Stress->ReleaseOk, 0, 0));
    RtlZeroMemory(&SegStatus, sizeof(SegStatus));
    Status = Dxgmms2VidMmCoreQuerySegment(&Stress->VidMm, 0, &SegStatus);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(SegStatus.UsedSize, 0ULL);
    ok_eq_ulong(SegStatus.PlacementCount, 0UL);
    ok_eq_ulong(Stress->VidMm.LiveRangeCount, 0UL);
    /* Overflow records taken under contention must all have been freed. */
    ok_eq_ulong(Stress->VidMm.OverflowRangeCount, 0UL);

    /* Residency references balance to zero, so the allocation is evictable. */
    ok_eq_long(InterlockedCompareExchange(&Stress->AcquireOk, 0, 0),
               InterlockedCompareExchange(&Stress->ReleaseRefOk, 0, 0));
    ok_eq_ulong(Stress->Refs.TotalCount, 0UL);
    ok_bool_true(DxgkResidencyCoreIsEvictable(&Stress->Refs), "fully released");

    Dxgmms2VidMmCoreStop(&Stress->VidMm);
    for (Index = 0; Index < Started; ++Index)
    {
        if (Threads[Index] != NULL)
            ZwClose(Threads[Index]);
    }
    ExFreePoolWithTag(Stress, TAG_CORE_CONCURRENCY);
}

/*
 * The scheduler's claim protocol is the part most exposed to a race: two
 * threads kicking the same engine must not both obtain a claim, or the same
 * packet is dispatched twice.
 */
typedef struct _SCHED_STRESS
{
    KSPIN_LOCK Lock;
    DXGMMS2_SCHED_CORE Core;
    DXGMMS2_SCHED_PACKET Packets[64];
    volatile LONG StartGate;
    volatile LONG SlotClaim;
    volatile LONG ClaimsWon;
    volatile LONG DoubleClaims;
    volatile LONG Dispatched;
    KEVENT DoneEvent[CORE_STRESS_THREADS];
} SCHED_STRESS, *PSCHED_STRESS;

static VOID NTAPI SchedClaimThread(_In_ PVOID Parameter)
{
    PSCHED_STRESS Stress = (PSCHED_STRESS)Parameter;
    LONG SlotIndex = InterlockedIncrement(&Stress->SlotClaim) - 1;
    ULONG Slot = (ULONG)((SlotIndex < 0 || SlotIndex >= CORE_STRESS_THREADS) ? 0 : SlotIndex);
    ULONG Round;

    while (InterlockedCompareExchange(&Stress->StartGate, 0, 0) == 0)
        YieldProcessor();

    for (Round = 0; Round < CORE_STRESS_ROUNDS; ++Round)
    {
        DXGMMS2_SCHEDULER_CLAIM_V1 Claim;
        PDXGMMS2_SCHED_PACKET Failed = NULL;
        KIRQL OldIrql;
        BOOLEAN Won;

        RtlZeroMemory(&Claim, sizeof(Claim));
        Claim.Size = DXGMMS2_SCHEDULER_CLAIM_V1_SIZE;
        Claim.Version = DXGMMS2_SCHEDULER_VERSION_1;

        KeAcquireSpinLock(&Stress->Lock, &OldIrql);
        Won = Dxgmms2SchedCoreClaim(&Stress->Core, 0, &Claim);
        KeReleaseSpinLock(&Stress->Lock, OldIrql);

        if (!Won)
            continue;
        InterlockedIncrement(&Stress->ClaimsWon);

        /* Only one thread may hold a claim at a time; if a second gets one
         * while this is outstanding, the same packet dispatches twice. */
        if (InterlockedIncrement(&Stress->Dispatched) != 1)
            InterlockedIncrement(&Stress->DoubleClaims);
        InterlockedDecrement(&Stress->Dispatched);

        {
            PDXGMMS2_SCHED_PACKET Retired[8];

            KeAcquireSpinLock(&Stress->Lock, &OldIrql);
            (VOID)Dxgmms2SchedCorePublishDispatch(&Stress->Core, 0, Claim.ClaimToken);
            (VOID)Dxgmms2SchedCoreCompleteDispatch(&Stress->Core, 0, Claim.ClaimToken,
                                                   STATUS_SUCCESS, &Failed);
            /* A committed packet stays queued until its fence retires it, so
             * retire it here or the head never advances and no other thread
             * can ever win a claim. */
            (VOID)Dxgmms2SchedCoreNotifyCompletion(&Stress->Core, 0, Claim.SubmissionFenceId,
                                                   Retired, RTL_NUMBER_OF(Retired));
            KeReleaseSpinLock(&Stress->Lock, OldIrql);
        }
    }

    KeSetEvent(&Stress->DoneEvent[Slot], IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID TestClaimIsExclusive(VOID)
{
    PSCHED_STRESS Stress;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Threads[CORE_STRESS_THREADS] = { NULL };
    NTSTATUS Status;
    ULONG Index;
    ULONG Started = 0;
    ULONG Admitted = 0;

    Stress = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Stress), TAG_CORE_CONCURRENCY);
    ok(Stress != NULL, "sched stress allocation failed\n");
    if (Stress == NULL)
        return;
    RtlZeroMemory(Stress, sizeof(*Stress));
    KeInitializeSpinLock(&Stress->Lock);
    for (Index = 0; Index < CORE_STRESS_THREADS; ++Index)
        KeInitializeEvent(&Stress->DoneEvent[Index], NotificationEvent, FALSE);

    Dxgmms2SchedCoreInitialize(&Stress->Core);
    Status = Dxgmms2SchedCoreStart(&Stress->Core, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);

    for (Index = 0; Index < RTL_NUMBER_OF(Stress->Packets); ++Index)
    {
        DXGMMS2_SCHEDULER_ADMIT_INFO_V1 Info;
        ULONG FenceId = 0;

        RtlZeroMemory(&Info, sizeof(Info));
        Info.Size = DXGMMS2_SCHEDULER_ADMIT_INFO_V1_SIZE;
        Info.Version = DXGMMS2_SCHEDULER_VERSION_1;
        Info.PacketCookie = (ULONGLONG)(ULONG_PTR)&Stress->Packets[Index];
        Info.OwnerCookie = 0x77ULL;
        RtlZeroMemory(&Stress->Packets[Index], sizeof(Stress->Packets[Index]));
        InitializeListHead(&Stress->Packets[Index].Entry);
        if (!NT_SUCCESS(Dxgmms2SchedCoreAdmit(&Stress->Core, &Info,
                                              &Stress->Packets[Index], &FenceId)))
            break;
        Admitted++;
    }
    ok(Admitted != 0, "no packets admitted\n");

    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    for (Index = 0; Index < CORE_STRESS_THREADS; ++Index)
    {
        Status = PsCreateSystemThread(&Threads[Index], THREAD_ALL_ACCESS, &Attributes,
                                      NULL, NULL, SchedClaimThread, Stress);
        if (!NT_SUCCESS(Status))
            break;
        Started++;
    }
    ok(Started != 0, "no claim threads started\n");
    InterlockedExchange(&Stress->StartGate, 1);

    for (Index = 0; Index < Started; ++Index)
    {
        Status = WaitForCoreEvent(&Stress->DoneEvent[Index]);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    /* The claim serialises dispatch: never two holders at once. */
    ok_eq_long(InterlockedCompareExchange(&Stress->DoubleClaims, 0, 0), 0L);

    /* Each admitted packet is claimed exactly once, so the total claims won
     * across every thread equals the number admitted — no packet dispatched
     * twice, and none left behind. */
    ok_eq_long(InterlockedCompareExchange(&Stress->ClaimsWon, 0, 0), (LONG)Admitted);

    for (Index = 0; Index < Started; ++Index)
    {
        if (Threads[Index] != NULL)
            ZwClose(Threads[Index]);
    }
    ExFreePoolWithTag(Stress, TAG_CORE_CONCURRENCY);
}

START_TEST(DxgkCoreConcurrency)
{
    TestLedgersBalance();
    TestClaimIsExclusive();
}

/* EOF */
