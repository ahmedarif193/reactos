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
#include "process_device_core.h"
#include "process_lifetime_core.h"

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
    DXGK_RESIDENCY_BUDGET Budget;
    DXGMMS2_SCHED_CORE Sched;
    DXGMMS2_SCHED_PACKET Packets[DXGMMS2_SCHED_MAX_PACKETS];
    volatile LONG StartGate;
    volatile LONG SlotClaim;
    volatile LONG ReserveOk;
    volatile LONG ReserveFail;
    volatile LONG ReleaseOk;
    volatile LONG AcquireOk;
    volatile LONG ReleaseRefOk;
    volatile LONG BudgetChargeOk;
    volatile LONG BudgetChargeFail;
    volatile LONG BudgetReleaseOk;
    ULONGLONG MaximumBudgetUsage;
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

        {
            ULONGLONG BytesToTrim = 0;

            KeAcquireSpinLock(&Stress->Lock, &OldIrql);
            Status = DxgkResidencyCoreBudgetTryCharge(
                         &Stress->Budget,
                         CORE_PAGE,
                         TRUE,
                         &BytesToTrim);
            if (NT_SUCCESS(Status) &&
                Stress->Budget.CurrentUsage > Stress->MaximumBudgetUsage)
            {
                Stress->MaximumBudgetUsage =
                    Stress->Budget.CurrentUsage;
            }
            KeReleaseSpinLock(&Stress->Lock, OldIrql);
            if (NT_SUCCESS(Status))
            {
                InterlockedIncrement(&Stress->BudgetChargeOk);
                YieldProcessor();
                KeAcquireSpinLock(&Stress->Lock, &OldIrql);
                Status = DxgkResidencyCoreBudgetRelease(
                             &Stress->Budget,
                             CORE_PAGE);
                KeReleaseSpinLock(&Stress->Lock, OldIrql);
                if (NT_SUCCESS(Status))
                    InterlockedIncrement(&Stress->BudgetReleaseOk);
                else
                    InterlockedIncrement(&Stress->Anomalies);
            }
            else if (Status == STATUS_GRAPHICS_NO_VIDEO_MEMORY)
            {
                InterlockedIncrement(&Stress->BudgetChargeFail);
                if (BytesToTrim != CORE_PAGE)
                    InterlockedIncrement(&Stress->Anomalies);
            }
            else
            {
                InterlockedIncrement(&Stress->Anomalies);
            }
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
    Status = DxgkResidencyCoreBudgetInitialize(
                 &Stress->Budget,
                 CORE_PAGE,
                 2 * CORE_PAGE,
                 0);
    ok_eq_hex(Status, STATUS_SUCCESS);

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

    /* Concurrent budget admissions never cross the hard cap, and every
     * successful placement charge is released exactly once. */
    ok_eq_long(InterlockedCompareExchange(&Stress->BudgetChargeOk, 0, 0),
               InterlockedCompareExchange(&Stress->BudgetReleaseOk, 0, 0));
    ok_eq_ulonglong(Stress->Budget.CurrentUsage, 0ULL);
    ok(Stress->MaximumBudgetUsage <= Stress->Budget.Maximum,
       "maximum observed budget usage %I64u exceeds %I64u\n",
       Stress->MaximumBudgetUsage,
       Stress->Budget.Maximum);

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

typedef struct _PROCESS_DEVICE_TEST
{
    DXGK_PROCESS_DEVICE_LINK Link;
    BOOLEAN Referenceable;
    ULONG References;
} PROCESS_DEVICE_TEST, *PPROCESS_DEVICE_TEST;

static BOOLEAN
TryReferenceProcessDevice(
    _In_ PVOID Object,
    _In_opt_ PVOID Context)
{
    PPROCESS_DEVICE_TEST Device = (PPROCESS_DEVICE_TEST)Object;

    UNREFERENCED_PARAMETER(Context);
    if (!Device->Referenceable)
        return FALSE;
    Device->References++;
    return TRUE;
}

static VOID
TestProcessPagingDeviceLifetime(VOID)
{
    LIST_ENTRY ProcessDevices;
    PROCESS_DEVICE_TEST First;
    PROCESS_DEVICE_TEST Second;
    PVOID SelectedDevice;
    HANDLE SelectedHandle;
    BOOLEAN Selected;

    InitializeListHead(&ProcessDevices);
    RtlZeroMemory(&First, sizeof(First));
    RtlZeroMemory(&Second, sizeof(Second));
    DxgkProcessDeviceLinkInitialize(&First.Link, &First);
    DxgkProcessDeviceLinkInitialize(&Second.Link, &Second);

    ok_bool_true(DxgkProcessDeviceLinkAttach(&ProcessDevices,
                                             &First.Link,
                                             (HANDLE)(ULONG_PTR)0x1111),
                 "attach first");
    ok_bool_true(DxgkProcessDeviceLinkAttach(&ProcessDevices,
                                             &Second.Link,
                                             (HANDLE)(ULONG_PTR)0x2222),
                 "attach second");

    /*
     * A teardown-closing first device must be skipped, not returned as a raw
     * stale handle.  Selection falls through to the next device only after its
     * callback has acquired a real lifetime reference.
     */
    First.Referenceable = FALSE;
    Second.Referenceable = TRUE;
    Selected = DxgkProcessDeviceTryReference(&ProcessDevices,
                                             TryReferenceProcessDevice,
                                             NULL,
                                             &SelectedDevice,
                                             &SelectedHandle);
    ok_bool_true(Selected, "select live fallback");
    ok_eq_pointer(SelectedDevice, &Second);
    ok_eq_pointer(SelectedHandle, (HANDLE)(ULONG_PTR)0x2222);
    ok_eq_ulong(First.References, 0);
    ok_eq_ulong(Second.References, 1);

    /* Detaching the old first device removes both membership and its handle. */
    ok_bool_true(DxgkProcessDeviceLinkDetach(&First.Link), "detach first");
    ok_bool_false(DxgkProcessDeviceLinkDetach(&First.Link), "detach first twice");

    Second.Referenceable = FALSE;
    SelectedDevice = (PVOID)(ULONG_PTR)0xBAD;
    SelectedHandle = (HANDLE)(ULONG_PTR)0xBAD;
    Selected = DxgkProcessDeviceTryReference(&ProcessDevices,
                                             TryReferenceProcessDevice,
                                             NULL,
                                             &SelectedDevice,
                                             &SelectedHandle);
    ok_bool_false(Selected, "no referenceable device");
    ok_eq_pointer(SelectedDevice, NULL);
    ok_eq_pointer(SelectedHandle, NULL);

    ok_bool_true(DxgkProcessDeviceLinkDetach(&Second.Link), "detach second");
    ok_bool_true(IsListEmpty(&ProcessDevices), "process list drained");
}

#define PROCESS_LIFETIME_TEST_THREADS 3

typedef enum _PROCESS_LIFETIME_TEST_MODE
{
    ProcessLifetimeTestCreating,
    ProcessLifetimeTestDestroying
} PROCESS_LIFETIME_TEST_MODE;

typedef struct _PROCESS_LIFETIME_TEST
{
    KSPIN_LOCK Lock;
    DXGK_PROCESS_LIFETIME_CORE Core;
    PROCESS_LIFETIME_TEST_MODE Mode;
    LONG ExpectedThreads;
    volatile LONG StartGate;
    volatile LONG NextSlot;
    volatile LONG PinnedThreads;
    volatile LONG ObservedThreads;
    volatile LONG Anomalies;
    volatile LONG BeginDestroyCount;
    volatile LONG FreeCount;
    KEVENT AllPinnedEvent;
    KEVENT AllObservedEvent;
    KEVENT TransitionEvent;
    KEVENT ReleaseEvent;
    KEVENT DoneEvent[PROCESS_LIFETIME_TEST_THREADS];
} PROCESS_LIFETIME_TEST, *PPROCESS_LIFETIME_TEST;

static VOID
NTAPI
ProcessLifetimeTestThread(
    _In_ PVOID Parameter)
{
    PPROCESS_LIFETIME_TEST Test = (PPROCESS_LIFETIME_TEST)Parameter;
    DXGK_PROCESS_LIFETIME_ACQUIRE AcquireResult;
    DXGK_PROCESS_LIFETIME_RELEASE ReleaseResult;
    DXGK_PROCESS_LIFETIME_ACQUIRE ExpectedAcquire;
    KIRQL OldIrql;
    LONG Count;
    LONG Slot;

    Slot = InterlockedIncrement(&Test->NextSlot) - 1;
    while (InterlockedCompareExchange(&Test->StartGate, 0, 0) == 0)
        YieldProcessor();

    KeAcquireSpinLock(&Test->Lock, &OldIrql);
    AcquireResult = DxgkProcessLifetimeAcquire(&Test->Core,
                                               PsGetCurrentThread());
    ExpectedAcquire =
        Test->Mode == ProcessLifetimeTestCreating ?
            DxgkProcessLifetimeWaitForCreate :
            DxgkProcessLifetimeWaitForDestroy;
    if (AcquireResult != ExpectedAcquire)
        InterlockedIncrement(&Test->Anomalies);
    KeReleaseSpinLock(&Test->Lock, OldIrql);

    Count = InterlockedIncrement(&Test->PinnedThreads);
    if (Count == Test->ExpectedThreads)
        KeSetEvent(&Test->AllPinnedEvent, IO_NO_INCREMENT, FALSE);

    (VOID)KeWaitForSingleObject(&Test->TransitionEvent,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);

    KeAcquireSpinLock(&Test->Lock, &OldIrql);
    if (Test->Mode == ProcessLifetimeTestCreating)
    {
        if (Test->Core.State != DxgkProcessLifetimeReady)
            InterlockedIncrement(&Test->Anomalies);
        ReleaseResult = DxgkProcessLifetimeReleaseNone;
    }
    else
    {
        if (Test->Core.State != DxgkProcessLifetimeDestroying)
            InterlockedIncrement(&Test->Anomalies);
        ReleaseResult = DxgkProcessLifetimeRelease(&Test->Core,
                                                   PsGetCurrentThread());
    }
    KeReleaseSpinLock(&Test->Lock, OldIrql);

    if (ReleaseResult == DxgkProcessLifetimeBeginDestroy)
        InterlockedIncrement(&Test->BeginDestroyCount);
    else if (ReleaseResult == DxgkProcessLifetimeFree)
        InterlockedIncrement(&Test->FreeCount);

    Count = InterlockedIncrement(&Test->ObservedThreads);
    if (Count == Test->ExpectedThreads)
        KeSetEvent(&Test->AllObservedEvent, IO_NO_INCREMENT, FALSE);

    if (Test->Mode == ProcessLifetimeTestCreating)
    {
        (VOID)KeWaitForSingleObject(&Test->ReleaseEvent,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
        KeAcquireSpinLock(&Test->Lock, &OldIrql);
        ReleaseResult = DxgkProcessLifetimeRelease(&Test->Core,
                                                   PsGetCurrentThread());
        if (ReleaseResult == DxgkProcessLifetimeBeginDestroy &&
            !DxgkProcessLifetimeCompleteDestroy(&Test->Core,
                                                PsGetCurrentThread()))
        {
            InterlockedIncrement(&Test->Anomalies);
        }
        KeReleaseSpinLock(&Test->Lock, OldIrql);
        if (ReleaseResult == DxgkProcessLifetimeBeginDestroy)
            InterlockedIncrement(&Test->BeginDestroyCount);
        else if (ReleaseResult == DxgkProcessLifetimeFree)
            InterlockedIncrement(&Test->FreeCount);
    }

    if (Slot >= 0 && Slot < PROCESS_LIFETIME_TEST_THREADS)
        KeSetEvent(&Test->DoneEvent[Slot], IO_NO_INCREMENT, FALSE);
    else
        InterlockedIncrement(&Test->Anomalies);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID
RunProcessLifetimeContention(
    _In_ PROCESS_LIFETIME_TEST_MODE Mode)
{
    PPROCESS_LIFETIME_TEST Test;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Threads[PROCESS_LIFETIME_TEST_THREADS] = { NULL };
    DXGK_PROCESS_LIFETIME_RELEASE ReleaseResult;
    NTSTATUS Status;
    KIRQL OldIrql;
    ULONG Index;
    ULONG Started = 0;

    Test = ExAllocatePoolWithTag(NonPagedPool,
                                 sizeof(*Test),
                                 TAG_CORE_CONCURRENCY);
    ok(Test != NULL, "process-lifetime test allocation failed\n");
    if (Test == NULL)
        return;

    RtlZeroMemory(Test, sizeof(*Test));
    KeInitializeSpinLock(&Test->Lock);
    KeInitializeEvent(&Test->AllPinnedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Test->AllObservedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Test->TransitionEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Test->ReleaseEvent, NotificationEvent, FALSE);
    for (Index = 0; Index < PROCESS_LIFETIME_TEST_THREADS; ++Index)
        KeInitializeEvent(&Test->DoneEvent[Index], NotificationEvent, FALSE);

    Test->Mode = Mode;
    DxgkProcessLifetimeInitialize(&Test->Core, PsGetCurrentThread());
    if (Mode == ProcessLifetimeTestDestroying)
    {
        DxgkProcessLifetimeCompleteCreate(&Test->Core,
                                          PsGetCurrentThread(),
                                          STATUS_SUCCESS);
        ReleaseResult = DxgkProcessLifetimeRelease(&Test->Core,
                                                   PsGetCurrentThread());
        ok_eq_long(ReleaseResult, DxgkProcessLifetimeBeginDestroy);
    }

    InitializeObjectAttributes(&Attributes,
                               NULL,
                               OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    for (Index = 0; Index < PROCESS_LIFETIME_TEST_THREADS; ++Index)
    {
        Status = PsCreateSystemThread(&Threads[Index],
                                      THREAD_ALL_ACCESS,
                                      &Attributes,
                                      NULL,
                                      NULL,
                                      ProcessLifetimeTestThread,
                                      Test);
        if (!NT_SUCCESS(Status))
            break;
        Started++;
    }
    ok(Started != 0, "no process-lifetime threads started\n");
    if (Started == 0)
        goto Cleanup;

    Test->ExpectedThreads = (LONG)Started;
    InterlockedExchange(&Test->StartGate, 1);
    Status = WaitForCoreEvent(&Test->AllPinnedEvent);
    ok_eq_hex(Status, STATUS_SUCCESS);

    KeAcquireSpinLock(&Test->Lock, &OldIrql);
    ok_eq_long(Test->Core.ReferenceCount,
               Mode == ProcessLifetimeTestCreating ?
                   (LONG)Started + 1 :
                   (LONG)Started);
    if (Mode == ProcessLifetimeTestCreating)
    {
        ok_eq_long(Test->Core.State, DxgkProcessLifetimeCreating);
        DxgkProcessLifetimeCompleteCreate(&Test->Core,
                                          PsGetCurrentThread(),
                                          STATUS_SUCCESS);
    }
    else
    {
        ok_eq_long(Test->Core.State, DxgkProcessLifetimeDestroying);
        ok_bool_false(DxgkProcessLifetimeCompleteDestroy(
                          &Test->Core,
                          PsGetCurrentThread()),
                      "destroy waiters pin storage");
    }
    KeReleaseSpinLock(&Test->Lock, OldIrql);
    KeSetEvent(&Test->TransitionEvent, IO_NO_INCREMENT, FALSE);

    Status = WaitForCoreEvent(&Test->AllObservedEvent);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Mode == ProcessLifetimeTestCreating)
    {
        /*
         * The original adapter handle may close while the concurrently opened
         * handles remain. It cannot trigger process destruction.
         */
        KeAcquireSpinLock(&Test->Lock, &OldIrql);
        ReleaseResult = DxgkProcessLifetimeRelease(&Test->Core,
                                                   PsGetCurrentThread());
        KeReleaseSpinLock(&Test->Lock, OldIrql);
        ok_eq_long(ReleaseResult, DxgkProcessLifetimeReleaseNone);
        KeSetEvent(&Test->ReleaseEvent, IO_NO_INCREMENT, FALSE);
    }

    for (Index = 0; Index < Started; ++Index)
    {
        Status = WaitForCoreEvent(&Test->DoneEvent[Index]);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    ok_eq_long(InterlockedCompareExchange(&Test->Anomalies, 0, 0), 0L);
    if (Mode == ProcessLifetimeTestCreating)
    {
        ok_eq_long(InterlockedCompareExchange(
                       &Test->BeginDestroyCount, 0, 0), 1L);
        ok_eq_long(InterlockedCompareExchange(&Test->FreeCount, 0, 0), 0L);
    }
    else
    {
        ok_eq_long(InterlockedCompareExchange(
                       &Test->BeginDestroyCount, 0, 0), 0L);
        ok_eq_long(InterlockedCompareExchange(&Test->FreeCount, 0, 0), 1L);
    }
    ok_eq_long(Test->Core.ReferenceCount, 0L);
    ok_bool_true(DxgkProcessLifetimeStorageUnpinned(&Test->Core),
                 "all process-lifetime pins drained");

Cleanup:
    for (Index = 0; Index < Started; ++Index)
    {
        if (Threads[Index] != NULL)
            ZwClose(Threads[Index]);
    }
    ExFreePoolWithTag(Test, TAG_CORE_CONCURRENCY);
}

static VOID
TestProcessLifetimeOwnership(VOID)
{
    DXGK_PROCESS_LIFETIME_CORE Core;
    DXGK_PROCESS_LIFETIME_RELEASE ReleaseResult;
    PVOID CallbackOwner = (PVOID)(ULONG_PTR)0x1111;
    PVOID OtherCaller = (PVOID)(ULONG_PTR)0x2222;

    /* Opening an adapter alone establishes and owns the process record. */
    DxgkProcessLifetimeInitialize(&Core, CallbackOwner);
    ok_eq_long(Core.ReferenceCount, 1L);
    ok_eq_long(Core.State, DxgkProcessLifetimeCreating);
    ok_eq_long(DxgkProcessLifetimeAcquire(&Core, CallbackOwner),
               DxgkProcessLifetimeRejectReentrant);
    ok_eq_long(Core.ReferenceCount, 1L);
    DxgkProcessLifetimeCompleteCreate(&Core,
                                      CallbackOwner,
                                      STATUS_SUCCESS);
    ReleaseResult = DxgkProcessLifetimeRelease(&Core, CallbackOwner);
    ok_eq_long(ReleaseResult, DxgkProcessLifetimeBeginDestroy);
    ok_eq_long(DxgkProcessLifetimeAcquire(&Core, CallbackOwner),
               DxgkProcessLifetimeRejectReentrant);
    ok_eq_long(Core.ReferenceCount, 0L);
    ok_bool_true(DxgkProcessLifetimeCompleteDestroy(&Core, CallbackOwner),
                 "open-without-device destroys on adapter close");

    /*
     * A device is an additional owner. Destroying the last device must not
     * destroy the process while its adapter handle remains open.
     */
    DxgkProcessLifetimeInitialize(&Core, CallbackOwner);
    DxgkProcessLifetimeCompleteCreate(&Core,
                                      CallbackOwner,
                                      STATUS_SUCCESS);
    ok_eq_long(DxgkProcessLifetimeAcquire(&Core, OtherCaller),
               DxgkProcessLifetimeAcquireReady);
    ReleaseResult = DxgkProcessLifetimeRelease(&Core, OtherCaller);
    ok_eq_long(ReleaseResult, DxgkProcessLifetimeReleaseNone);
    ok_eq_long(Core.State, DxgkProcessLifetimeReady);
    ReleaseResult = DxgkProcessLifetimeRelease(&Core, CallbackOwner);
    ok_eq_long(ReleaseResult, DxgkProcessLifetimeBeginDestroy);
    ok_bool_true(DxgkProcessLifetimeCompleteDestroy(&Core, CallbackOwner),
                 "device and adapter owners both released");

    /* Failed creation wakes pinned openers and frees only after the last pin. */
    DxgkProcessLifetimeInitialize(&Core, CallbackOwner);
    ok_eq_long(DxgkProcessLifetimeAcquire(&Core, OtherCaller),
               DxgkProcessLifetimeWaitForCreate);
    DxgkProcessLifetimeCompleteCreate(&Core,
                                      CallbackOwner,
                                      STATUS_DEVICE_NOT_READY);
    ReleaseResult = DxgkProcessLifetimeRelease(&Core, CallbackOwner);
    ok_eq_long(ReleaseResult, DxgkProcessLifetimeReleaseNone);
    ReleaseResult = DxgkProcessLifetimeRelease(&Core, OtherCaller);
    ok_eq_long(ReleaseResult, DxgkProcessLifetimeFree);
}

START_TEST(DxgkCoreConcurrency)
{
    TestLedgersBalance();
    TestClaimIsExclusive();
    TestProcessPagingDeviceLifetime();
    TestProcessLifetimeOwnership();
    RunProcessLifetimeContention(ProcessLifetimeTestCreating);
    RunProcessLifetimeContention(ProcessLifetimeTestDestroying);
}

/* EOF */
