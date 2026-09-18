/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Concurrent affinity updates must return a serial sequence of masks
 */

#include <kmt_test.h>

VOID Test_KeArm64AffinityRace(VOID);

#define AFFINITY_ROUNDS 32768

typedef struct _AFFINITY_RACE
{
    PKTHREAD Target;
    KEVENT Ready;
    KEVENT Quit;
    volatile LONG Phase;
    volatile LONG Stop;
    volatile LONG StopTarget;
    volatile LONG Done[2];
    KAFFINITY Previous[2];
    ULONG Mode;
    ULONG FinalCpu;
    ULONG WrongCpu;
    ULONG64 Iterations;
    ULONG64 ActiveIterations;
} AFFINITY_RACE;

typedef struct _AFFINITY_WRITER
{
    AFFINITY_RACE *Race;
    ULONG Index;
} AFFINITY_WRITER;

static VOID NTAPI
AffinityTarget(PVOID Parameter)
{
    AFFINITY_RACE *Race = Parameter;
    KAFFINITY PreviousAffinity = 0;

    if (Race->Mode)
    {
        KeSetPriorityThread(KeGetCurrentThread(), 8);
        if (Race->Mode == 2)
            PreviousAffinity = KeSetSystemAffinityThreadEx(8);
        KeSetEvent(&Race->Ready, IO_NO_INCREMENT, FALSE);
        while (!Race->StopTarget)
        {
            LONG Phase = Race->Phase;
            Race->Iterations++;
            if (Phase && (Race->Done[0] != Phase || Race->Done[1] != Phase))
                Race->ActiveIterations++;
            if (Race->Mode == 2 && KeGetCurrentProcessorNumber() != 3)
                Race->WrongCpu++;
            YieldProcessor();
        }
    }
    else
    {
        KeSetEvent(&Race->Ready, IO_NO_INCREMENT, TRUE);
    }
    KeWaitForSingleObject(&Race->Quit, Executive, KernelMode, FALSE, NULL);
    if (Race->Mode == 2)
        KeRevertToUserAffinityThreadEx(PreviousAffinity);
    Race->FinalCpu = KeGetCurrentProcessorNumber();
}

static VOID NTAPI
AffinityWriter(PVOID Parameter)
{
    AFFINITY_WRITER *Writer = Parameter;
    AFFINITY_RACE *Race = Writer->Race;
    KAFFINITY PreviousAffinity;
    LONG Phase = 0, Next;

    PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << (Writer->Index + 1));
    KeSetPriorityThread(KeGetCurrentThread(), 8);
    while (!Race->Stop)
    {
        Next = Race->Phase;
        if (Next == Phase)
        {
            YieldProcessor();
            continue;
        }
        Race->Previous[Writer->Index] = KeSetAffinityThread(Race->Target, (KAFFINITY)1 << (Writer->Index + 1));
        InterlockedExchange(&Race->Done[Writer->Index], Next);
        Phase = Next;
    }
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

static VOID
CheckAffinityRace(ULONG Mode)
{
    AFFINITY_RACE Race = {0};
    AFFINITY_WRITER Writers[2];
    PKTHREAD Threads[2] = {NULL, NULL};
    KAFFINITY PreviousAffinity, ResetMask, FinalMask;
    KAFFINITY FirstPrevious[2] = {0, 0}, FirstFinal = 0;
    LARGE_INTEGER Frequency;
    LONGLONG Start, RunUntil, Deadline;
    ULONG Index, Round, Completed = 0, Errors = 0, DuplicatePrevious = 0;
    BOOLEAN Valid;

    Race.Mode = Mode;
    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    ResetMask = KeQueryActiveProcessors();
    KeInitializeEvent(&Race.Ready, NotificationEvent, FALSE);
    KeInitializeEvent(&Race.Quit, NotificationEvent, FALSE);
    Race.Target = KmtStartThread(AffinityTarget, &Race);
    if (!Race.Target) goto Cleanup;
    KeWaitForSingleObject(&Race.Ready, Executive, KernelMode, FALSE, NULL);
    KeSetAffinityThread(Race.Target, ResetMask);
    for (Index = 0; Index < RTL_NUMBER_OF(Threads); Index++)
    {
        Writers[Index].Race = &Race;
        Writers[Index].Index = Index;
        Threads[Index] = KmtStartThread(AffinityWriter, &Writers[Index]);
        if (!Threads[Index]) goto Cleanup;
    }

    KeQueryPerformanceCounter(&Frequency);
    Start = KeQueryPerformanceCounter(NULL).QuadPart;
    RunUntil = Start + Frequency.QuadPart;
    Deadline = Start + 30 * Frequency.QuadPart;
    /* Running targets must get a timeslice even when the writers finish quickly. */
    for (Round = 1; Round <= AFFINITY_ROUNDS ||
                    (Mode && KeQueryPerformanceCounter(NULL).QuadPart < RunUntil); Round++)
    {
        InterlockedExchange(&Race.Phase, (LONG)Round);
        while ((Race.Done[0] != (LONG)Round || Race.Done[1] != (LONG)Round) &&
               KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
            YieldProcessor();
        if (Race.Done[0] != (LONG)Round || Race.Done[1] != (LONG)Round) break;
        KeMemoryBarrier();
        FinalMask = KeSetAffinityThread(Race.Target, ResetMask);
        Valid = (Race.Previous[0] == ResetMask && Race.Previous[1] == 2 && FinalMask == 4) ||
                (Race.Previous[1] == ResetMask && Race.Previous[0] == 4 && FinalMask == 2);
        if (!Valid)
        {
            if (!Errors)
            {
                FirstPrevious[0] = Race.Previous[0];
                FirstPrevious[1] = Race.Previous[1];
                FirstFinal = FinalMask;
            }
            Errors++;
        }
        if (Race.Previous[0] == Race.Previous[1]) DuplicatePrevious++;
        Completed++;
    }

Cleanup:
    InterlockedExchange(&Race.Stop, 1);
    InterlockedExchange(&Race.StopTarget, 1);
    for (Index = 0; Index < RTL_NUMBER_OF(Threads); Index++)
        if (Threads[Index]) KmtFinishThread(Threads[Index], NULL);
    if (Race.Target)
    {
        KeSetAffinityThread(Race.Target, 2);
        KmtFinishThread(Race.Target, &Race.Quit);
        ok_eq_ulong(Race.FinalCpu, 1);
    }
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    ok(Completed >= AFFINITY_ROUNDS, "Only %lu affinity rounds\n", Completed);
    ok_eq_ulong(Errors, 0);
    ok_eq_ulong(DuplicatePrevious, 0);
    if (Mode)
        ok(Race.ActiveIterations > 0, "Target did not execute during concurrent affinity updates\n");
    if (Mode == 2)
        ok_eq_ulong(Race.WrongCpu, 0);
    trace("AFFINITY_RACE mode=%lu rounds=%lu errors=%lu duplicate_previous=%lu first_previous=0x%Ix,0x%Ix first_final=0x%Ix active_iterations=%I64u wrong_cpu=%lu final_cpu=%lu\n",
          Mode, Completed, Errors, DuplicatePrevious, FirstPrevious[0], FirstPrevious[1], FirstFinal,
          Race.ActiveIterations, Race.WrongCpu, Race.FinalCpu);
}

START_TEST(KeArm64AffinityRace)
{
    if (skip(KeNumberProcessors >= 3, "Three processors required\n")) return;
    CheckAffinityRace(0);
    CheckAffinityRace(1);
    if (!skip(KeNumberProcessors >= 4, "Four processors required for system-affinity override\n"))
        CheckAffinityRace(2);
}
