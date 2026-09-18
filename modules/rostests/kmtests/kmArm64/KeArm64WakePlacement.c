/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Validate wake affinity and dynamic priority under load
 */

#include <kmt_test.h>

VOID Test_KeArm64WakePlacement(VOID);

#define PLACEMENT_SAMPLES 256
#define PLACEMENT_WARMUP 32

typedef struct _WAKE_PLACEMENT
{
    KEVENT Ready;
    KEVENT Go;
    KEVENT Ack;
    KAFFINITY Affinity;
    volatile LONG Stop;
    ULONG Cpu;
    KPRIORITY Priority;
    KPRIORITY BasePriority;
    LONG PriorityDecrement;
} WAKE_PLACEMENT;

static VOID NTAPI
PlacementWorker(PVOID Parameter)
{
    WAKE_PLACEMENT *Context = Parameter;
    KAFFINITY PreviousAffinity;
    KIRQL OldIrql;
    PKTHREAD Thread = KeGetCurrentThread();

    PreviousAffinity = KeSetAffinityThread(KeGetCurrentThread(), Context->Affinity);
    for (;;)
    {
        KeSetEvent(&Context->Ready, IO_NO_INCREMENT, TRUE);
        KeWaitForSingleObject(&Context->Go, Executive, KernelMode, FALSE, NULL);
        if (Context->Stop) break;
        OldIrql = KeRaiseIrqlToDpcLevel();
        Context->Cpu = KeGetCurrentProcessorNumber();
        Context->Priority = KeQueryPriorityThread(Thread);
        Context->BasePriority = Thread->BasePriority;
        Context->PriorityDecrement = Thread->PriorityDecrement;
        KeLowerIrql(OldIrql);
        KeSetEvent(&Context->Ack, IO_NO_INCREMENT, FALSE);
    }
    KeSetAffinityThread(KeGetCurrentThread(), PreviousAffinity);
}

static VOID
CheckPlacement(KAFFINITY Affinity)
{
    WAKE_PLACEMENT Context = {0};
    PKTHREAD Worker;
    KAFFINITY PreviousAffinity;
    KPRIORITY PreviousPriority;
    LARGE_INTEGER Frequency, Timeout;
    LONGLONG Start, BusyEnd;
    ULONG Round, Count = 0;
    ULONG Timeouts = 0, WrongCpu = 0, PriorityErrors = 0;
    NTSTATUS Status;

    Context.Affinity = Affinity;
    KeInitializeEvent(&Context.Ready, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context.Go, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context.Ack, SynchronizationEvent, FALSE);
    KeQueryPerformanceCounter(&Frequency);
    Timeout.QuadPart = -10000000;
    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    PreviousPriority = KeSetPriorityThread(KeGetCurrentThread(), 16);
    Worker = KmtStartThread(PlacementWorker, &Context);
    if (!Worker) goto Cleanup;

    for (Round = 0; Round < PLACEMENT_WARMUP + PLACEMENT_SAMPLES; Round++)
    {
        Status = KeWaitForSingleObject(&Context.Ready, Executive, KernelMode, FALSE, &Timeout);
        if (Status != STATUS_SUCCESS)
        {
            Timeouts++;
            break;
        }
        KeSetPriorityThread(Worker, 8);
        Start = KeQueryPerformanceCounter(NULL).QuadPart;
        BusyEnd = Start + Frequency.QuadPart / 1000;
        KeSetEvent(&Context.Go, IO_NO_INCREMENT, FALSE);
        while (KeQueryPerformanceCounter(NULL).QuadPart < BusyEnd)
            YieldProcessor();
        Status = KeWaitForSingleObject(&Context.Ack, Executive, KernelMode, FALSE, &Timeout);
        if (Status != STATUS_SUCCESS)
        {
            Timeouts++;
            break;
        }
        /* Ready-queue starvation boosts can raise a dynamic worker above its base. */
        if (KeQueryPriorityThread(KeGetCurrentThread()) != 16 ||
            Context.BasePriority != 8 || Context.Priority < 8 || Context.Priority >= 16 ||
            Context.PriorityDecrement != Context.Priority - Context.BasePriority)
        {
            PriorityErrors++;
        }
        if (!(Affinity & ((KAFFINITY)1 << Context.Cpu))) WrongCpu++;
        if (Round < PLACEMENT_WARMUP) continue;
        Count++;
    }

    InterlockedExchange(&Context.Stop, 1);
    KmtFinishThread(Worker, &Context.Go);

Cleanup:
    KeSetPriorityThread(KeGetCurrentThread(), PreviousPriority);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    ok_eq_ulong(Count, PLACEMENT_SAMPLES);
    ok_eq_ulong(Timeouts, 0);
    ok_eq_ulong(WrongCpu, 0);
    ok_eq_ulong(PriorityErrors, 0);
}

START_TEST(KeArm64WakePlacement)
{
    KAFFINITY Active = KeQueryActiveProcessors();

    if (skip(KeNumberProcessors >= 2, "Two processors required\n")) return;
    CheckPlacement(Active & ~(KAFFINITY)1);
    CheckPlacement(Active);
}
