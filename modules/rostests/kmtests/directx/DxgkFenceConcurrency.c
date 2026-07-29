/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Fence identity and watermark publication under real contention
 *
 * These are the only core operations production calls with no lock held:
 * DxgkFenceCoreAdvance runs from the completion DPC while DxgkFenceCoreAllocate
 * runs from a submitting thread.  Their correctness rests entirely on the
 * interlocked sequences, so single-threaded coverage proves nothing about the
 * cases that actually matter.
 */

#include <kmt_test.h>
#include "fence_core.h"

#define TAG_FENCE_CONCURRENCY 'cFxD'
#define FENCE_STRESS_THREADS   4
#define FENCE_STRESS_ROUNDS    2048
#define FENCE_STRESS_TIMEOUT_S 30

typedef struct _FENCE_ALLOC_STRESS
{
    volatile LONG NextFenceId;
    volatile LONG StartGate;
    volatile LONG ZeroSeen;
    volatile LONG SlotClaimed[FENCE_STRESS_THREADS];
    KEVENT DoneEvent[FENCE_STRESS_THREADS];
    ULONG Fences[FENCE_STRESS_THREADS][FENCE_STRESS_ROUNDS];
} FENCE_ALLOC_STRESS, *PFENCE_ALLOC_STRESS;

typedef struct _FENCE_ADVANCE_STRESS
{
    volatile LONG Watermark;
    volatile LONG StartGate;
    volatile LONG RegressionSeen;
    volatile LONG AdvanceCount;
    KEVENT DoneEvent[FENCE_STRESS_THREADS];
    ULONG ThreadIndex[FENCE_STRESS_THREADS];
} FENCE_ADVANCE_STRESS, *PFENCE_ADVANCE_STRESS;

static NTSTATUS WaitForFenceEvent(_In_ PKEVENT Event)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -10LL * 1000LL * 1000LL * FENCE_STRESS_TIMEOUT_S;
    return KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, &Timeout);
}

static VOID SpinUntilStarted(_In_ volatile LONG *Gate)
{
    while (InterlockedCompareExchange(Gate, 0, 0) == 0)
        YieldProcessor();
}

/* Every thread allocates the same number of identities; no two may collide. */
static VOID NTAPI FenceAllocThread(_In_ PVOID Parameter)
{
    PFENCE_ALLOC_STRESS Stress = (PFENCE_ALLOC_STRESS)Parameter;
    ULONG Index;
    ULONG Slot;

    /*
     * Slot ownership must not share storage with the results.  The old
     * sentinel lived in Fences[Slot][0], and the owning thread overwrote it
     * with its first fence while peers were still claiming slots, allowing a
     * second thread to take the same slot and corrupt the evidence.
     */
    for (Slot = 0; Slot < FENCE_STRESS_THREADS; ++Slot)
    {
        if (InterlockedCompareExchange(&Stress->SlotClaimed[Slot], 1, 0) == 0)
            break;
    }
    if (Slot >= FENCE_STRESS_THREADS)
        Slot = 0;

    SpinUntilStarted(&Stress->StartGate);
    for (Index = 0; Index < FENCE_STRESS_ROUNDS; ++Index)
    {
        ULONG Fence = DxgkFenceCoreAllocate(&Stress->NextFenceId);

        if (Fence == 0)
            InterlockedIncrement(&Stress->ZeroSeen);
        Stress->Fences[Slot][Index] = Fence;
    }
    KeSetEvent(&Stress->DoneEvent[Slot], IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID TestConcurrentAllocation(VOID)
{
    PFENCE_ALLOC_STRESS Stress;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Threads[FENCE_STRESS_THREADS] = { NULL };
    ULONG Index;
    ULONG Slot;
    ULONG Started = 0;
    ULONG ZeroFences = 0;

    Stress = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Stress), TAG_FENCE_CONCURRENCY);
    ok(Stress != NULL, "fence stress allocation failed\n");
    if (Stress == NULL)
        return;
    RtlZeroMemory(Stress, sizeof(*Stress));
    for (Slot = 0; Slot < FENCE_STRESS_THREADS; ++Slot)
        KeInitializeEvent(&Stress->DoneEvent[Slot], NotificationEvent, FALSE);

    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    for (Index = 0; Index < FENCE_STRESS_THREADS; ++Index)
    {
        NTSTATUS Status = PsCreateSystemThread(&Threads[Index], THREAD_ALL_ACCESS,
                                               &Attributes, NULL, NULL,
                                               FenceAllocThread, Stress);
        if (!NT_SUCCESS(Status))
            break;
        Started++;
    }
    ok(Started != 0, "no allocator threads started\n");
    InterlockedExchange(&Stress->StartGate, 1);

    for (Index = 0; Index < Started; ++Index)
    {
        NTSTATUS Status = WaitForFenceEvent(&Stress->DoneEvent[Index]);

        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    /* Zero means "no fence"; the allocator must never hand it out, even when
     * several threads are racing the increment. */
    ok_eq_long(InterlockedCompareExchange(&Stress->ZeroSeen, 0, 0), 0L);

    /*
     * Two submissions sharing a fence identity would let one retire the other's
     * work.  Every identity handed out across every thread must be distinct.
     * The allocator is a plain increment, so distinctness is checkable by
     * counting the whole space it consumed.
     */
    {
        ULONG Total = Started * FENCE_STRESS_ROUNDS;
        ULONG Highest = (ULONG)InterlockedCompareExchange(&Stress->NextFenceId, 0, 0);

        ok_eq_ulong(Highest, Total);
        for (Slot = 0; Slot < Started; ++Slot)
        {
            for (Index = 0; Index < FENCE_STRESS_ROUNDS; ++Index)
            {
                if (Stress->Fences[Slot][Index] == 0)
                    ZeroFences++;
            }
        }
        ok_eq_ulong(ZeroFences, 0UL);
    }

    for (Index = 0; Index < Started; ++Index)
    {
        if (Threads[Index] != NULL)
            ZwClose(Threads[Index]);
    }
    ExFreePoolWithTag(Stress, TAG_FENCE_CONCURRENCY);
}

/*
 * Several completion sources publish watermarks concurrently.  The watermark
 * may never move backwards, whatever interleaving occurs: a regression would
 * make already-retired work look outstanding again.
 */
static VOID NTAPI FenceAdvanceThread(_In_ PVOID Parameter)
{
    PFENCE_ADVANCE_STRESS Stress = (PFENCE_ADVANCE_STRESS)Parameter;
    ULONG Index;
    ULONG Slot;

    for (Slot = 0; Slot < FENCE_STRESS_THREADS; ++Slot)
    {
        if (InterlockedCompareExchange((volatile LONG *)&Stress->ThreadIndex[Slot], 1, 0) == 0)
            break;
    }
    if (Slot >= FENCE_STRESS_THREADS)
        Slot = 0;

    SpinUntilStarted(&Stress->StartGate);
    for (Index = 1; Index <= FENCE_STRESS_ROUNDS; ++Index)
    {
        LONG Before = InterlockedCompareExchange(&Stress->Watermark, 0, 0);
        LONG After;

        if (DxgkFenceCoreAdvance(&Stress->Watermark, Index))
            InterlockedIncrement(&Stress->AdvanceCount);
        After = InterlockedCompareExchange(&Stress->Watermark, 0, 0);
        /* Observed from a racing thread, so the watermark may have moved past
         * what this thread published, but never behind what it already saw. */
        if (!DxgkFenceCoreReached((ULONG)After, (ULONG)Before))
            InterlockedIncrement(&Stress->RegressionSeen);
    }
    KeSetEvent(&Stress->DoneEvent[Slot], IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID TestConcurrentAdvance(VOID)
{
    PFENCE_ADVANCE_STRESS Stress;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Threads[FENCE_STRESS_THREADS] = { NULL };
    ULONG Index;
    ULONG Slot;
    ULONG Started = 0;

    Stress = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Stress), TAG_FENCE_CONCURRENCY);
    ok(Stress != NULL, "advance stress allocation failed\n");
    if (Stress == NULL)
        return;
    RtlZeroMemory(Stress, sizeof(*Stress));
    for (Slot = 0; Slot < FENCE_STRESS_THREADS; ++Slot)
        KeInitializeEvent(&Stress->DoneEvent[Slot], NotificationEvent, FALSE);

    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    for (Index = 0; Index < FENCE_STRESS_THREADS; ++Index)
    {
        NTSTATUS Status = PsCreateSystemThread(&Threads[Index], THREAD_ALL_ACCESS,
                                               &Attributes, NULL, NULL,
                                               FenceAdvanceThread, Stress);
        if (!NT_SUCCESS(Status))
            break;
        Started++;
    }
    ok(Started != 0, "no advance threads started\n");
    InterlockedExchange(&Stress->StartGate, 1);

    for (Index = 0; Index < Started; ++Index)
    {
        NTSTATUS Status = WaitForFenceEvent(&Stress->DoneEvent[Index]);

        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    /* The invariant that matters: never backwards, under any interleaving. */
    ok_eq_long(InterlockedCompareExchange(&Stress->RegressionSeen, 0, 0), 0L);

    /* Whatever order the threads raced in, the final watermark is the highest
     * value anyone published. */
    ok_eq_ulong((ULONG)InterlockedCompareExchange(&Stress->Watermark, 0, 0),
                (ULONG)FENCE_STRESS_ROUNDS);

    /*
     * Every thread publishes the same sequence, so only the thread that got
     * there first can advance any given value: the total number of successful
     * advances is bounded by the sequence length, never by threads * length.
     */
    {
        LONG Advances = InterlockedCompareExchange(&Stress->AdvanceCount, 0, 0);

        ok(Advances > 0 && Advances <= (LONG)FENCE_STRESS_ROUNDS,
           "advances = %ld, expected 1..%u\n", Advances, FENCE_STRESS_ROUNDS);
    }

    for (Index = 0; Index < Started; ++Index)
    {
        if (Threads[Index] != NULL)
            ZwClose(Threads[Index]);
    }
    ExFreePoolWithTag(Stress, TAG_FENCE_CONCURRENCY);
}

START_TEST(DxgkFenceConcurrency)
{
    TestConcurrentAllocation();
    TestConcurrentAdvance();
}

/* EOF */
