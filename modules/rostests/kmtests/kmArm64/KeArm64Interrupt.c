/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 single and shared ISR synchronization IRQL
 */

#include <kmt_test.h>
#include <ndk/halfuncs.h>

VOID Test_KeArm64Interrupt(VOID);

typedef BOOLEAN (NTAPI *PKMT_DISPATCH_SECONDARY)(ULONG, ULONG_PTR, PVOID);

typedef struct _KMT_ISR_CONTEXT
{
    ULONG Calls;
    ULONG Cpu;
    ULONG OwnerCpu;
    KIRQL Irql;
} KMT_ISR_CONTEXT;

static BOOLEAN NTAPI
RecordIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    KMT_ISR_CONTEXT *Record = Context;
    Record->Calls++;
    Record->Cpu = KeGetCurrentProcessorNumber();
    Record->OwnerCpu = (UCHAR)Interrupt->Number;
    Record->Irql = KeGetCurrentIrql();
    return FALSE;
}

static ULONG_PTR NTAPI
ReadInterruptCount(ULONG_PTR Argument)
{
    PULONG Counts = (PULONG)Argument;
    if (Counts != NULL)
        Counts[KeGetCurrentProcessorNumber()] = KeGetCurrentPrcb()->InterruptCount;
    return 0;
}

static VOID
TestIpiCounts(VOID)
{
    ULONG Before[MAXIMUM_PROCESSORS], After[MAXIMUM_PROCESSORS];
    ULONG Cpu, Round;

    if (KeNumberProcessors < 2) return;
    KeSetSystemAffinityThread(1);
    KeIpiGenericCall(ReadInterruptCount, (ULONG_PTR)Before);
    for (Round = 0; Round < 64; Round++)
        KeIpiGenericCall(ReadInterruptCount, 0);
    KeIpiGenericCall(ReadInterruptCount, (ULONG_PTR)After);
    KeRevertToUserAffinityThread();
    for (Cpu = 1; Cpu < (ULONG)KeNumberProcessors; Cpu++)
        ok(After[Cpu] - Before[Cpu] >= 65,
           "CPU %lu serviced 65 IPIs but counted %lu interrupts\n",
           Cpu, After[Cpu] - Before[Cpu]);
}

typedef struct _KMT_ISR_DISCONNECT
{
    PKMT_DISPATCH_SECONDARY Dispatch;
    ULONG Vector;
    ULONG NestedVector;
    KIRQL Irql;
    LONGLONG HoldTicks;
    volatile LONG Entered;
    volatile LONG Finished;
    BOOLEAN Dispatched;
} KMT_ISR_DISCONNECT;

static BOOLEAN NTAPI
HoldIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    KMT_ISR_DISCONNECT *State = Context;
    LONGLONG Start = KeQueryPerformanceCounter(NULL).QuadPart;

    UNREFERENCED_PARAMETER(Interrupt);
    InterlockedExchange(&State->Entered, 1);
    if (State->NestedVector) State->Dispatch(State->NestedVector, 0, NULL);
    while (KeQueryPerformanceCounter(NULL).QuadPart - Start < State->HoldTicks)
        YieldProcessor();
    InterlockedExchange(&State->Finished, 1);
    return TRUE;
}

static VOID NTAPI
DispatchWorker(PVOID Context)
{
    KMT_ISR_DISCONNECT *State = Context;
    KIRQL OldIrql;

    KeSetSystemAffinityThread(2);
    KeRaiseIrql(State->Irql, &OldIrql);
    State->Dispatched = State->Dispatch(State->Vector, 0, NULL);
    KeLowerIrql(OldIrql);
    KeRevertToUserAffinityThread();
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID
TestDisconnect(PKMT_DISPATCH_SECONDARY Dispatch, ULONG Vector, KIRQL Irql, ULONG Mode)
{
    KMT_ISR_DISCONNECT State, NestedState, *WaitState = &State;
    KMT_ISR_CONTEXT Record;
    KINTERRUPT Interrupt, OtherInterrupt;
    LARGE_INTEGER Frequency, Delay;
    LONGLONG Start;
    HANDLE Thread;
    NTSTATUS Status;
    ULONG Gsiv, NestedVector;
    KIRQL NestedIrql;
    KAFFINITY Affinity;
    BOOLEAN Result;

    if (KeNumberProcessors < 2) return;
    trace("Disconnect case %lu (single, shared head, shared tail, nested)\n", Mode);
    RtlZeroMemory(&State, sizeof(State));
    RtlZeroMemory(&OtherInterrupt, sizeof(OtherInterrupt));
    RtlZeroMemory(&Record, sizeof(Record));
    State.Dispatch = Dispatch;
    State.Vector = Vector;
    State.Irql = Irql;
    KeQueryPerformanceCounter(&Frequency);
    State.HoldTicks = Frequency.QuadPart / 20;
    KeInitializeInterrupt(&Interrupt, HoldIsr, &State, NULL,
                          Vector, Irql, Irql, LevelSensitive, TRUE, 1, FALSE);
    if (Mode == 1 || Mode == 2)
        KeInitializeInterrupt(&OtherInterrupt, RecordIsr, &Record, NULL,
                              Vector, Irql, Irql, LevelSensitive, TRUE, 1, FALSE);
    else if (Mode == 3)
    {
        Status = HalAllocateGsivForSecondaryInterrupt("kmtest-nested", sizeof("kmtest-nested") - 1, &Gsiv);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status)) return;
        NestedVector = HalGetInterruptVector(Internal, 0, Gsiv, 0, &NestedIrql, &Affinity);
        ok(NestedVector != 0 && NestedIrql <= CLOCK_LEVEL, "Invalid nested vector %lu IRQL %u\n", NestedVector, NestedIrql);
        if (!NestedVector || NestedIrql > CLOCK_LEVEL) return;
        NestedState = State;
        NestedState.Vector = NestedVector;
        WaitState = &NestedState;
        State.NestedVector = NestedVector;
        State.HoldTicks = 0;
        KeInitializeInterrupt(&OtherInterrupt, HoldIsr, &NestedState, NULL,
                              NestedVector, NestedIrql, CLOCK_LEVEL, LevelSensitive, FALSE, 1, FALSE);
    }
    if ((Mode == 2 || Mode == 3) && !KeConnectInterrupt(&OtherInterrupt))
    {
        ok(FALSE, "Cannot connect the first shared/nested ISR\n");
        return;
    }
    if (!KeConnectInterrupt(&Interrupt))
    {
        ok(FALSE, "Cannot connect the disconnect-test ISR\n");
        if (OtherInterrupt.Connected) KeDisconnectInterrupt(&OtherInterrupt);
        return;
    }
    if (Mode == 1 && !KeConnectInterrupt(&OtherInterrupt))
    {
        ok(FALSE, "Cannot connect the second shared ISR\n");
        KeDisconnectInterrupt(&Interrupt);
        return;
    }

    KeSetSystemAffinityThread(1);
    Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                 DispatchWorker, &State);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Start = KeQueryPerformanceCounter(NULL).QuadPart;
        Delay.QuadPart = -10000;
        while (!InterlockedCompareExchange(&WaitState->Entered, 0, 0) &&
               KeQueryPerformanceCounter(NULL).QuadPart - Start < Frequency.QuadPart)
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);

        ok_eq_long(WaitState->Entered, 1);
        ok_eq_long(State.Finished, 0);
        Result = KeDisconnectInterrupt(&Interrupt);
        ok_eq_bool(Result, TRUE);
        ok(State.Finished == 1, "Disconnect returned while the ISR was still running\n");
        Status = ZwWaitForSingleObject(Thread, FALSE, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_bool(State.Dispatched, TRUE);
        ZwClose(Thread);
        KeSetSystemAffinityThread(2);
        if (Mode == 1 || Mode == 2)
        {
            Record.Calls = 0;
            Result = Dispatch(Vector, 0, NULL);
            ok_eq_bool(Result, TRUE);
            ok_eq_ulong(Record.Calls, 1);
        }
        else
        {
            Result = Dispatch(Vector, 0, NULL);
            ok_eq_bool(Result, FALSE);
        }
        Result = KeDisconnectInterrupt(&Interrupt);
        ok_eq_bool(Result, FALSE);
    }
    else
    {
        KeDisconnectInterrupt(&Interrupt);
    }
    if (OtherInterrupt.Connected) KeDisconnectInterrupt(&OtherInterrupt);
    Result = Dispatch(Vector, 0, NULL);
    ok_eq_bool(Result, FALSE);
    KeRevertToUserAffinityThread();
}

static VOID
TestFailedConnection(PKMT_DISPATCH_SECONDARY Dispatch)
{
    KINTERRUPT Interrupt;
    KMT_ISR_CONTEXT Record = {0};
    BOOLEAN Connected, Dispatched;

    /* INTID 1020 is reserved; the HAL rejects it without enabling hardware. */
    KeInitializeInterrupt(&Interrupt, RecordIsr, &Record, NULL, 1020,
                          DISPATCH_LEVEL + 1, DISPATCH_LEVEL + 1,
                          LevelSensitive, FALSE, 0, FALSE);
    Connected = KeConnectInterrupt(&Interrupt);
    ok_eq_bool(Connected, FALSE);
    ok_eq_bool(Interrupt.Connected, FALSE);
    Dispatched = Dispatch(Interrupt.Vector, 0, NULL);
    ok_eq_bool(Dispatched, FALSE);
    ok_eq_ulong(Record.Calls, 0);
    trace("INTERRUPT_CONNECT_FAILURE result=%u connected=%u dispatched=%u calls=%lu\n",
          Connected, Interrupt.Connected, Dispatched, Record.Calls);
    if (Interrupt.Connected) KeDisconnectInterrupt(&Interrupt);
    ok_eq_bool(Dispatch(Interrupt.Vector, 0, NULL), FALSE);
}

typedef struct _KMT_INTERRUPT_ISOLATION
{
    KMT_ISR_DISCONNECT DispatchState;
    volatile LONG Release;
    volatile LONG TimedOut;
} KMT_INTERRUPT_ISOLATION;

static BOOLEAN NTAPI
UnrelatedIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    KMT_INTERRUPT_ISOLATION *State = Context;
    LONGLONG Start = KeQueryPerformanceCounter(NULL).QuadPart;

    UNREFERENCED_PARAMETER(Interrupt);
    InterlockedExchange(&State->DispatchState.Entered, 1);
    while (!ReadAcquire(&State->Release))
    {
        if (KeQueryPerformanceCounter(NULL).QuadPart - Start >= State->DispatchState.HoldTicks)
        {
            InterlockedExchange(&State->TimedOut, 1);
            break;
        }
        YieldProcessor();
    }
    return TRUE;
}

static VOID
TestIndependentInterruptChains(PKMT_DISPATCH_SECONDARY Dispatch, ULONG Vector, KIRQL Irql)
{
    KMT_INTERRUPT_ISOLATION State = {0};
    KMT_ISR_CONTEXT Record = {0};
    KINTERRUPT Local, Shared, OtherCpu;
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Frequency, Delay;
    LONGLONG Start;
    HANDLE Thread;
    NTSTATUS Status;

    if (skip(KeNumberProcessors >= 2, "Two processors required for independent interrupt chains\n")) return;
    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    State.DispatchState.Dispatch = Dispatch;
    State.DispatchState.Vector = Vector;
    State.DispatchState.Irql = Irql;
    KeQueryPerformanceCounter(&Frequency);
    State.DispatchState.HoldTicks = 2 * Frequency.QuadPart;
    KeInitializeInterrupt(&Local, RecordIsr, &Record, NULL,
                          Vector, Irql, Irql, LevelSensitive, TRUE, 0, FALSE);
    KeInitializeInterrupt(&Shared, RecordIsr, &Record, NULL,
                          Vector, Irql, Irql, LevelSensitive, TRUE, 0, FALSE);
    KeInitializeInterrupt(&OtherCpu, UnrelatedIsr, &State, NULL,
                          Vector, Irql, Irql, LevelSensitive, FALSE, 1, FALSE);
    ok_eq_bool(KeConnectInterrupt(&Local), TRUE);
    ok_eq_bool(KeConnectInterrupt(&OtherCpu), TRUE);
    if (!Local.Connected || !OtherCpu.Connected) goto Cleanup;

    Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                 DispatchWorker, &State.DispatchState);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Start = KeQueryPerformanceCounter(NULL).QuadPart;
    Delay.QuadPart = -10000;
    while (!ReadAcquire(&State.DispatchState.Entered) &&
           KeQueryPerformanceCounter(NULL).QuadPart - Start < Frequency.QuadPart)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    ok_eq_long(State.DispatchState.Entered, 1);

    /* An ISR using CPU 1's table must not hold up edits to CPU 0's table. */
    ok_eq_bool(KeConnectInterrupt(&Shared), TRUE);
    ok_eq_bool(KeDisconnectInterrupt(&Shared), TRUE);
    ok_eq_bool(KeDisconnectInterrupt(&Local), TRUE);
    InterlockedExchange(&State.Release, 1);
    Status = ZwWaitForSingleObject(Thread, FALSE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(Thread);
    ok_eq_bool(State.DispatchState.Dispatched, TRUE);
    ok_eq_long(State.TimedOut, 0);
    trace("INTERRUPT_CHAIN_ISOLATION unrelated_isr_timed_out=%ld\n", State.TimedOut);

Cleanup:
    if (Shared.Connected) KeDisconnectInterrupt(&Shared);
    if (Local.Connected) KeDisconnectInterrupt(&Local);
    if (OtherCpu.Connected) KeDisconnectInterrupt(&OtherCpu);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

#define CONNECT_ROUNDS 2048

typedef struct _CONNECT_RACE
{
    KINTERRUPT Interrupt;
    KMT_ISR_CONTEXT Record;
    volatile LONG Phase;
    volatile LONG Done[2];
    KEVENT Completed[2];
    volatile LONG Stop;
    BOOLEAN Result[2];
} CONNECT_RACE;

typedef struct _CONNECT_WRITER
{
    CONNECT_RACE *Race;
    ULONG Index;
} CONNECT_WRITER;

static VOID NTAPI
ConnectWorker(PVOID Parameter)
{
    CONNECT_WRITER *Writer = Parameter;
    CONNECT_RACE *Race = Writer->Race;
    KAFFINITY PreviousAffinity;
    LONG Phase = 0, Next;

    PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << (Writer->Index + 1));
    while (!Race->Stop)
    {
        Next = Race->Phase;
        if (Next == Phase)
        {
            YieldProcessor();
            continue;
        }
        Race->Result[Writer->Index] = KeConnectInterrupt(&Race->Interrupt);
        InterlockedExchange(&Race->Done[Writer->Index], Next);
        KeSetEvent(&Race->Completed[Writer->Index], IO_NO_INCREMENT, FALSE);
        Phase = Next;
    }
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

static VOID
TestConcurrentConnection(PKMT_DISPATCH_SECONDARY Dispatch, ULONG Vector, KIRQL Irql)
{
    CONNECT_RACE Race = {0};
    CONNECT_WRITER Writers[2];
    PKTHREAD Threads[2] = {NULL, NULL};
    PVOID Events[2];
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Timeout;
    ULONGLONG Deadline, Now;
    NTSTATUS Status;
    ULONG Index, Round, Completed = 0, LostState = 0, ReturnErrors = 0;
    ULONG DispatchErrors = 0, DisconnectErrors = 0;
    BOOLEAN Dispatched;

    if (skip(KeNumberProcessors >= 3, "Three processors required for connect race\n")) return;
    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    KeInitializeInterrupt(&Race.Interrupt, RecordIsr, &Race.Record, NULL,
                          Vector, Irql, Irql, LevelSensitive, FALSE, 0, FALSE);
    for (Index = 0; Index < RTL_NUMBER_OF(Threads); Index++)
    {
        KeInitializeEvent(&Race.Completed[Index], NotificationEvent, FALSE);
        Events[Index] = &Race.Completed[Index];
        Writers[Index].Race = &Race;
        Writers[Index].Index = Index;
        Threads[Index] = KmtStartThread(ConnectWorker, &Writers[Index]);
        if (!Threads[Index]) goto Cleanup;
    }

    Deadline = KeQueryInterruptTime() + 30 * 10000000ULL;
    for (Round = 1; Round <= CONNECT_ROUNDS; Round++)
    {
        Race.Record.Calls = 0;
        for (Index = 0; Index < RTL_NUMBER_OF(Events); Index++)
            KeClearEvent(&Race.Completed[Index]);
        InterlockedExchange(&Race.Phase, (LONG)Round);
        /* The writers may migrate here to update this CPU's interrupt table. */
        Now = KeQueryInterruptTime();
        if (Now >= Deadline) break;
        Timeout.QuadPart = -(LONGLONG)(Deadline - Now);
        Status = KeWaitForMultipleObjects(RTL_NUMBER_OF(Events), Events, WaitAll,
                                          Executive, KernelMode, FALSE, &Timeout, NULL);
        if (Status != STATUS_SUCCESS) break;
        if (Race.Done[0] != (LONG)Round || Race.Done[1] != (LONG)Round) break;
        KeMemoryBarrier();
        if (!Race.Result[0] || !Race.Result[1]) ReturnErrors++;
        Dispatched = Dispatch(Vector, 0, NULL);
        if (!Dispatched || Race.Record.Calls != 1) DispatchErrors++;
        if (!Race.Interrupt.Connected)
        {
            LostState++;
            /* Repair only the test-owned flag so a failed round can be detached. */
            if (Dispatched && Race.Record.Calls == 1) Race.Interrupt.Connected = TRUE;
        }
        if (!KeDisconnectInterrupt(&Race.Interrupt)) DisconnectErrors++;
        if (Dispatch(Vector, 0, NULL)) DisconnectErrors++;
        Completed++;
    }

Cleanup:
    InterlockedExchange(&Race.Stop, 1);
    for (Index = 0; Index < RTL_NUMBER_OF(Threads); Index++)
        if (Threads[Index]) KmtFinishThread(Threads[Index], NULL);
    Race.Record.Calls = 0;
    if (Dispatch(Vector, 0, NULL) && Race.Record.Calls == 1)
        Race.Interrupt.Connected = TRUE;
    if (Race.Interrupt.Connected) KeDisconnectInterrupt(&Race.Interrupt);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    ok_eq_ulong(Completed, CONNECT_ROUNDS);
    ok_eq_ulong(LostState, 0);
    ok_eq_ulong(ReturnErrors, 0);
    ok_eq_ulong(DispatchErrors, 0);
    ok_eq_ulong(DisconnectErrors, 0);
    trace("INTERRUPT_CONNECT_RACE rounds=%lu lost_state=%lu return_errors=%lu dispatch_errors=%lu disconnect_errors=%lu\n",
          Completed, LostState, ReturnErrors, DispatchErrors, DisconnectErrors);
}

static VOID
TestIoAffinity(PKMT_DISPATCH_SECONDARY Dispatch, ULONG Vector, KIRQL Irql,
               KAFFINITY Mask, BOOLEAN Shared)
{
    PKINTERRUPT Interrupt = NULL;
    KMT_ISR_CONTEXT Record = {0};
    KAFFINITY PreviousAffinity;
    NTSTATUS Status;
    BOOLEAN Dispatched;
    ULONG Cpu, Expected, Calls = 0, Outside = 0, BadOwner = 0;

    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    Status = IoConnectInterrupt(&Interrupt, RecordIsr, &Record, NULL, Vector,
                                Irql, Irql, LevelSensitive, Shared, Mask, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
        {
            KeSetSystemAffinityThread((KAFFINITY)1 << Cpu);
            Record.Calls = 0;
            Dispatched = Dispatch(Vector, 0, NULL);
            Expected = (Mask & ((KAFFINITY)1 << Cpu)) != 0;
            ok_eq_bool(Dispatched, Expected != 0);
            ok_eq_ulong(Record.Calls, Expected);
            Calls += Record.Calls;
            if (!Expected) Outside += Record.Calls;
            if (Record.Calls && Record.OwnerCpu != Cpu) BadOwner++;
        }
        KeSetSystemAffinityThread(1);
        IoDisconnectInterrupt(Interrupt);
    }
    for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
    {
        KeSetSystemAffinityThread((KAFFINITY)1 << Cpu);
        ok_eq_bool(Dispatch(Vector, 0, NULL), FALSE);
    }
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    ok_eq_ulong(Outside, 0);
    ok_eq_ulong(BadOwner, 0);
    trace("IO_INTERRUPT_AFFINITY mask=0x%Ix shared=%u status=0x%08lx calls=%lu outside=%lu bad_owner=%lu\n",
          Mask, Shared, Status, Calls, Outside, BadOwner);
}

START_TEST(KeArm64Interrupt)
{
    HAL_SECONDARY_INTERRUPT_INFORMATION Information;
    PKMT_DISPATCH_SECONDARY Dispatch;
    KINTERRUPT Interrupts[2];
    KMT_ISR_CONTEXT Records[2];
    ULONG Gsiv, Vector, Cpu, Count, Index;
    KIRQL Irql, OldIrql, ReturnedIrql;
    KAFFINITY Affinity;
    NTSTATUS Status;
    BOOLEAN Dispatched;

    Dispatch = (PKMT_DISPATCH_SECONDARY)KmtGetSystemRoutineAddress(L"KeDispatchSecondaryInterrupt");
    if (skip(Dispatch != NULL, "Secondary interrupt dispatcher unavailable\n")) return;
    Status = HalQuerySystemInformation(HalSecondaryInterruptInformation, sizeof(Information), &Information, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = HalAllocateGsivForSecondaryInterrupt("kmtest-irql", sizeof("kmtest-irql") - 1, &Gsiv);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Vector = HalGetInterruptVector(Internal, 0, Gsiv, 0, &Irql, &Affinity);
    ok(Vector != 0 && Irql > DISPATCH_LEVEL && Irql < CLOCK_LEVEL,
       "Secondary vector %lu IRQL %u\n", Vector, Irql);
    if (Vector == 0 || Irql <= DISPATCH_LEVEL || Irql >= CLOCK_LEVEL) return;

    for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
    {
        KeSetSystemAffinityThread((KAFFINITY)1 << Cpu);
        RtlZeroMemory(Records, sizeof(Records));
        for (Index = 0; Index < 2; Index++)
            KeInitializeInterrupt(&Interrupts[Index], RecordIsr, &Records[Index], NULL,
                                  Vector, Irql, CLOCK_LEVEL, LevelSensitive, TRUE, (CHAR)Cpu, FALSE);
        if (!KeConnectInterrupt(&Interrupts[0]))
        {
            ok(FALSE, "Cannot connect the first ISR on CPU %lu\n", Cpu);
            KeRevertToUserAffinityThread();
            break;
        }
        for (Count = 1; Count <= 2; Count++)
        {
            if (Count == 2 && !KeConnectInterrupt(&Interrupts[1]))
            {
                ok(FALSE, "Cannot connect the shared ISR on CPU %lu\n", Cpu);
                break;
            }
            RtlZeroMemory(Records, sizeof(Records));
            KeRaiseIrql(Irql, &OldIrql);
            Dispatched = Dispatch(Vector, 0, NULL);
            ReturnedIrql = KeGetCurrentIrql();
            KeLowerIrql(OldIrql);
            ok_eq_bool(Dispatched, TRUE);
            ok_eq_uint(ReturnedIrql, Irql);
            for (Index = 0; Index < Count; Index++)
            {
                ok_eq_ulong(Records[Index].Calls, 1);
                ok_eq_ulong(Records[Index].Cpu, Cpu);
                ok_eq_ulong(Records[Index].OwnerCpu, Cpu);
                ok(Records[Index].Irql == CLOCK_LEVEL,
                   "%lu ISR(s), cpu %lu, ISR %lu ran at IRQL %u instead of %u\n",
                   Count, Cpu, Index, Records[Index].Irql, CLOCK_LEVEL);
            }
        }
        if (Interrupts[1].Connected) KeDisconnectInterrupt(&Interrupts[1]);
        KeDisconnectInterrupt(&Interrupts[0]);
        Dispatched = Dispatch(Vector, 0, NULL);
        ok_eq_bool(Dispatched, FALSE);
        KeRevertToUserAffinityThread();
    }
    TestIpiCounts();
    for (Index = 0; Index < 4; Index++)
        TestDisconnect(Dispatch, Vector, Irql, Index);
    TestFailedConnection(Dispatch);
    TestIndependentInterruptChains(Dispatch, Vector, Irql);
    TestConcurrentConnection(Dispatch, Vector, Irql);
    TestIoAffinity(Dispatch, Vector, Irql, 1, FALSE);
    TestIoAffinity(Dispatch, Vector, Irql, KeQueryActiveProcessors(), FALSE);
    TestIoAffinity(Dispatch, Vector, Irql, 1, TRUE);
    TestIoAffinity(Dispatch, Vector, Irql, KeQueryActiveProcessors(), TRUE);
}
