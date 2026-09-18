/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Clock-level interrupt reentry into an ARM64 IPI broadcast
 */

#include <kmt_test.h>

VOID Test_KeArm64IpiPreemption(VOID);

typedef struct _IPI_PREEMPTION
{
    ULONG TimerCpu;
    volatile LONG OuterCalls;
    volatile LONG InnerCalls;
    volatile LONG IsrCalls;
    volatile LONG Complete;
    volatile LONG BadIrql;
    volatile LONG BadCpu;
    ULONG_PTR InnerResult;
} IPI_PREEMPTION;

static VOID
ArmPhysicalTimer(VOID)
{
    __asm__ __volatile__("msr cntp_tval_el0, %0\n\tmsr cntp_ctl_el0, %1\n\tisb"
                         :: "r"((ULONG64)0), "r"((ULONG64)1) : "memory");
}

static ULONG_PTR NTAPI
InnerBroadcast(ULONG_PTR Argument)
{
    IPI_PREEMPTION *State = (IPI_PREEMPTION *)Argument;

    if (KeGetCurrentIrql() != IPI_LEVEL) InterlockedIncrement(&State->BadIrql);
    InterlockedIncrement(&State->InnerCalls);
    return 0x1f1;
}

static ULONG_PTR NTAPI
OuterBroadcast(ULONG_PTR Argument)
{
    IPI_PREEMPTION *State = (IPI_PREEMPTION *)Argument;

    if (KeGetCurrentIrql() != IPI_LEVEL) InterlockedIncrement(&State->BadIrql);
    InterlockedIncrement(&State->OuterCalls);
    if (KeGetCurrentProcessorNumber() == State->TimerCpu) ArmPhysicalTimer();
    return 0x0f1;
}

static BOOLEAN NTAPI
PhysicalTimerIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    IPI_PREEMPTION *State = Context;

    __asm__ __volatile__("msr cntp_ctl_el0, %0\n\tisb" :: "r"((ULONG64)0) : "memory");
    if (KeGetCurrentIrql() != CLOCK_LEVEL) InterlockedIncrement(&State->BadIrql);
    if (KeGetCurrentProcessorNumber() != State->TimerCpu ||
        (UCHAR)Interrupt->Number != State->TimerCpu)
        InterlockedIncrement(&State->BadCpu);
    InterlockedIncrement(&State->IsrCalls);
    State->InnerResult = KeIpiGenericCall(InnerBroadcast, (ULONG_PTR)State);
    InterlockedExchange(&State->Complete, 1);
    return TRUE;
}

static BOOLEAN
WaitForTimer(IPI_PREEMPTION *State)
{
    LARGE_INTEGER Frequency;
    LONGLONG Start = KeQueryPerformanceCounter(&Frequency).QuadPart;

    while (!InterlockedCompareExchange(&State->Complete, 0, 0))
    {
        if (KeQueryPerformanceCounter(NULL).QuadPart - Start >= Frequency.QuadPart)
            return FALSE;
        YieldProcessor();
    }
    return TRUE;
}

static VOID
TestProcessor(ULONG Cpu)
{
    static const KIRQL Levels[] = { PASSIVE_LEVEL, DISPATCH_LEVEL, SYNCH_LEVEL, CLOCK_LEVEL };
    IPI_PREEMPTION State = {0};
    KINTERRUPT Interrupt;
    KIRQL OldIrql, ReturnedIrql;
    KAFFINITY PreviousAffinity;
    ULONG64 SavedControl, SavedDeadline;
    ULONG ControllerCpu = (Cpu + 1) % KeNumberProcessors;
    ULONG Round, Index, CompletedRounds = 0;
    LONG OwnerErrors = 0, IrqlErrors = 0;
    ULONG_PTR Result;
    BOOLEAN Completed, Connected;

    PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << Cpu);
    __asm__ __volatile__("mrs %0, cntp_ctl_el0" : "=r"(SavedControl));
    __asm__ __volatile__("mrs %0, cntp_cval_el0" : "=r"(SavedDeadline));
    if (skip(!(SavedControl & 1), "Physical timer is already in use on CPU %lu\n", Cpu))
    {
        KeRevertToUserAffinityThreadEx(PreviousAffinity);
        return;
    }

    State.TimerCpu = Cpu;
    KeSetSystemAffinityThread((KAFFINITY)1 << ControllerCpu);
    KeInitializeInterrupt(&Interrupt, PhysicalTimerIsr, &State, NULL,
                          30, CLOCK_LEVEL, CLOCK_LEVEL, LevelSensitive, FALSE, (CHAR)Cpu, FALSE);
    Connected = KeConnectInterrupt(&Interrupt);
    ok_eq_ulong(KeGetCurrentProcessorNumber(), ControllerCpu);
    if (skip(Connected, "Physical timer vector is already connected on CPU %lu\n", Cpu))
    {
        KeRevertToUserAffinityThreadEx(PreviousAffinity);
        return;
    }

    KeSetSystemAffinityThread((KAFFINITY)1 << Cpu);
    trace("IPI_PREEMPTION cpu=%lu: broadcast from physical timer ISR\n", Cpu);
    ArmPhysicalTimer();
    Completed = WaitForTimer(&State);
    ok(Completed, "Physical timer control case timed out\n");
    ok_eq_long(State.IsrCalls, 1);
    ok_eq_long(State.InnerCalls, KeNumberProcessors);
    ok_eq_ulongptr(State.InnerResult, 0x1f1);
    ok_eq_long(State.BadIrql, 0);
    ok_eq_long(State.BadCpu, 0);
    OwnerErrors += State.BadCpu;
    IrqlErrors += State.BadIrql;
    if (!Completed) goto Cleanup;

    for (Round = 0; Round < 32; Round++)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(Levels); Index++)
        {
            RtlZeroMemory(&State, sizeof(State));
            State.TimerCpu = Cpu;
            KeRaiseIrql(Levels[Index], &OldIrql);
            Result = KeIpiGenericCall(OuterBroadcast, (ULONG_PTR)&State);
            ReturnedIrql = KeGetCurrentIrql();
            KeLowerIrql(OldIrql);
            Completed = WaitForTimer(&State);
            ok(Completed, "Timer reentry timed out: round %lu IRQL %u\n", Round, Levels[Index]);
            ok_eq_ulongptr(Result, 0x0f1);
            ok_eq_uint(ReturnedIrql, Levels[Index]);
            ok_eq_long(State.IsrCalls, 1);
            ok_eq_long(State.OuterCalls, KeNumberProcessors);
            ok_eq_long(State.InnerCalls, KeNumberProcessors);
            ok_eq_ulongptr(State.InnerResult, 0x1f1);
            ok_eq_long(State.BadIrql, 0);
            ok_eq_long(State.BadCpu, 0);
            OwnerErrors += State.BadCpu;
            IrqlErrors += State.BadIrql;
            if (!Completed) goto Cleanup;
            CompletedRounds++;
        }
    }

Cleanup:
    __asm__ __volatile__("msr cntp_ctl_el0, %0\n\tisb" :: "r"((ULONG64)0) : "memory");
    KeSetSystemAffinityThread((KAFFINITY)1 << ControllerCpu);
    Connected = KeDisconnectInterrupt(&Interrupt);
    ok_eq_bool(Connected, TRUE);
    ok_eq_ulong(KeGetCurrentProcessorNumber(), ControllerCpu);
    KeSetSystemAffinityThread((KAFFINITY)1 << Cpu);
    __asm__ __volatile__("msr cntp_cval_el0, %0\n\tmsr cntp_ctl_el0, %1\n\tisb"
                         :: "r"(SavedDeadline), "r"(SavedControl) : "memory");
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    ok_eq_ulong(CompletedRounds, 128);
    trace("PPI_REENTRY cpu=%lu controller=%lu rounds=%lu owner_errors=%ld irql_errors=%ld\n",
          Cpu, ControllerCpu, CompletedRounds, OwnerErrors, IrqlErrors);
}

typedef struct _PPI_BANKS
{
    ULONG64 SavedControl[MAXIMUM_PROCESSORS];
    ULONG64 SavedDeadline[MAXIMUM_PROCESSORS];
    volatile LONG Calls[MAXIMUM_PROCESSORS];
    volatile LONG InUse;
    volatile LONG BadOwner;
    volatile LONG BadIrql;
    KAFFINITY ArmMask;
} PPI_BANKS;

static ULONG_PTR NTAPI
SavePhysicalTimers(ULONG_PTR Argument)
{
    PPI_BANKS *State = (PPI_BANKS *)Argument;
    ULONG Cpu = KeGetCurrentProcessorNumber();

    __asm__ __volatile__("mrs %0, cntp_ctl_el0" : "=r"(State->SavedControl[Cpu]));
    __asm__ __volatile__("mrs %0, cntp_cval_el0" : "=r"(State->SavedDeadline[Cpu]));
    if (State->SavedControl[Cpu] & 1) InterlockedIncrement(&State->InUse);
    return 0;
}

static ULONG_PTR NTAPI
ArmPhysicalTimers(ULONG_PTR Argument)
{
    PPI_BANKS *State = (PPI_BANKS *)Argument;

    if (State->ArmMask & ((KAFFINITY)1 << KeGetCurrentProcessorNumber()))
        ArmPhysicalTimer();
    return 0;
}

static ULONG_PTR NTAPI
RestorePhysicalTimers(ULONG_PTR Argument)
{
    PPI_BANKS *State = (PPI_BANKS *)Argument;
    ULONG Cpu = KeGetCurrentProcessorNumber();

    __asm__ __volatile__("msr cntp_ctl_el0, %0\n\tisb" :: "r"((ULONG64)0) : "memory");
    __asm__ __volatile__("msr cntp_cval_el0, %0\n\tmsr cntp_ctl_el0, %1\n\tisb"
                         :: "r"(State->SavedDeadline[Cpu]), "r"(State->SavedControl[Cpu]) : "memory");
    return 0;
}

static BOOLEAN NTAPI
BankedTimerIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    PPI_BANKS *State = Context;
    ULONG Cpu = KeGetCurrentProcessorNumber();

    __asm__ __volatile__("msr cntp_ctl_el0, %0\n\tisb" :: "r"((ULONG64)0) : "memory");
    if ((UCHAR)Interrupt->Number != Cpu) InterlockedIncrement(&State->BadOwner);
    if (KeGetCurrentIrql() != CLOCK_LEVEL) InterlockedIncrement(&State->BadIrql);
    InterlockedIncrement(&State->Calls[Cpu]);
    return TRUE;
}

static BOOLEAN
WaitForBanks(PPI_BANKS *State, LONG Expected)
{
    LARGE_INTEGER Frequency;
    LONGLONG Start = KeQueryPerformanceCounter(&Frequency).QuadPart;
    ULONG Cpu;

    for (;;)
    {
        for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
        {
            if ((State->ArmMask & ((KAFFINITY)1 << Cpu)) &&
                State->Calls[Cpu] < Expected)
                break;
        }
        if (Cpu == (ULONG)KeNumberProcessors) return TRUE;
        if (KeQueryPerformanceCounter(NULL).QuadPart - Start >= Frequency.QuadPart)
            return FALSE;
        YieldProcessor();
    }
}

static VOID
TestBankLifetime(VOID)
{
    PPI_BANKS State = {0};
    PKINTERRUPT First = NULL, Others = NULL;
    KAFFINITY PreviousAffinity, ActiveMask = KeQueryActiveProcessors();
    ULONG Cpu, Phase, Round, Completed = 0;
    NTSTATUS Status;
    BOOLEAN Delivered;

    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    KeIpiGenericCall(SavePhysicalTimers, (ULONG_PTR)&State);
    if (skip(State.InUse == 0, "Physical timer already active on %ld processors\n", State.InUse))
        goto Done;

    Status = IoConnectInterrupt(&First, BankedTimerIsr, &State, NULL, 30,
                                CLOCK_LEVEL, CLOCK_LEVEL, LevelSensitive, FALSE, 1, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = IoConnectInterrupt(&Others, BankedTimerIsr, &State, NULL, 30,
                                CLOCK_LEVEL, CLOCK_LEVEL, LevelSensitive, FALSE, ActiveMask & ~(KAFFINITY)1, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Cleanup;

    for (Phase = 0; Phase < 2; Phase++)
    {
        State.ArmMask = Phase ? ActiveMask & ~(KAFFINITY)1 : ActiveMask;
        if (Phase)
        {
            IoDisconnectInterrupt(First);
            First = NULL;
        }
        for (Round = 1; Round <= 64; Round++)
        {
            KeIpiGenericCall(ArmPhysicalTimers, (ULONG_PTR)&State);
            Delivered = WaitForBanks(&State, (LONG)(Phase * 64 + Round));
            ok(Delivered, "PPI delivery timed out: phase %lu round %lu\n", Phase, Round);
            if (!Delivered) goto Cleanup;
            for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
                ok_eq_long(State.Calls[Cpu], Cpu == 0 && Phase ? 64 : (LONG)(Phase * 64 + Round));
            Completed++;
        }
    }

Cleanup:
    KeIpiGenericCall(RestorePhysicalTimers, (ULONG_PTR)&State);
    if (Others) IoDisconnectInterrupt(Others);
    if (First) IoDisconnectInterrupt(First);
    ok_eq_ulong(Completed, 128);
    ok_eq_long(State.BadOwner, 0);
    ok_eq_long(State.BadIrql, 0);
    for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
        trace("PPI_BANK cpu=%lu calls=%ld expected=%u\n", Cpu, State.Calls[Cpu], Cpu ? 128 : 64);
    trace("PPI_BANK_LIFETIME rounds=%lu owner_errors=%ld irql_errors=%ld\n",
          Completed, State.BadOwner, State.BadIrql);
Done:
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

typedef VOID (NTAPI *PKMT_DISABLE_INTERRUPT)(ULONG, KIRQL);
typedef BOOLEAN (NTAPI *PKMT_ENABLE_INTERRUPT)(ULONG, KIRQL, KINTERRUPT_MODE);

typedef struct _PPI_MASK_STATE
{
    ULONG Cpu;
    volatile LONG Calls;
    volatile LONG BadOwner;
} PPI_MASK_STATE;

static BOOLEAN NTAPI
MaskedTimerIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    PPI_MASK_STATE *State = Context;
    ULONG64 Control;

    __asm__ __volatile__("mrs %0, cntp_ctl_el0" : "=r"(Control));
    if ((Control & 5) != 5) return FALSE;
    __asm__ __volatile__("msr cntp_ctl_el0, %0\n\tisb" :: "r"((ULONG64)0) : "memory");
    if ((UCHAR)Interrupt->Number != State->Cpu ||
        KeGetCurrentProcessorNumber() != State->Cpu)
        InterlockedIncrement(&State->BadOwner);
    InterlockedIncrement(&State->Calls);
    return TRUE;
}

static VOID
StallForTimer(VOID)
{
    LARGE_INTEGER Frequency;
    LONGLONG Start = KeQueryPerformanceCounter(&Frequency).QuadPart;

    while (KeQueryPerformanceCounter(NULL).QuadPart - Start < Frequency.QuadPart / 5000)
        YieldProcessor();
}

static VOID
TestDisabledPending(ULONG Cpu, PKMT_DISABLE_INTERRUPT Disable, PKMT_ENABLE_INTERRUPT Enable)
{
    PPI_MASK_STATE State = {0};
    KINTERRUPT Interrupt;
    KAFFINITY PreviousAffinity;
    KIRQL OldIrql;
    ULONG64 SavedControl, SavedDeadline, PendingControl;
    LONG ControlCalls, HighCalls, DisabledCalls, EnabledCalls;
    BOOLEAN Connected, Enabled;

    PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << Cpu);
    __asm__ __volatile__("mrs %0, cntp_ctl_el0" : "=r"(SavedControl));
    __asm__ __volatile__("mrs %0, cntp_cval_el0" : "=r"(SavedDeadline));
    if (skip(!(SavedControl & 1), "Physical timer is active on CPU %lu\n", Cpu)) goto Done;
    State.Cpu = Cpu;
    KeInitializeInterrupt(&Interrupt, MaskedTimerIsr, &State, NULL,
                          30, CLOCK_LEVEL, CLOCK_LEVEL, LevelSensitive, FALSE, (CHAR)Cpu, FALSE);
    Connected = KeConnectInterrupt(&Interrupt);
    if (skip(Connected, "Physical timer vector is occupied on CPU %lu\n", Cpu)) goto Done;

    ArmPhysicalTimer();
    StallForTimer();
    ControlCalls = State.Calls;
    State.Calls = 0;
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    ArmPhysicalTimer();
    StallForTimer();
    __asm__ __volatile__("mrs %0, cntp_ctl_el0" : "=r"(PendingControl));
    HighCalls = State.Calls;
    Disable(30, CLOCK_LEVEL);
    KeLowerIrql(OldIrql);
    StallForTimer();
    DisabledCalls = State.Calls;

    /* Quiesce the source and drain any owned pending IRQ before rearming. */
    __asm__ __volatile__("msr cntp_ctl_el0, %0\n\tisb" :: "r"((ULONG64)0) : "memory");
    Enabled = Enable(30, CLOCK_LEVEL, LevelSensitive);
    StallForTimer();
    State.Calls = 0;
    ArmPhysicalTimer();
    StallForTimer();
    EnabledCalls = State.Calls;
    __asm__ __volatile__("msr cntp_ctl_el0, %0\n\tisb" :: "r"((ULONG64)0) : "memory");
    Connected = KeDisconnectInterrupt(&Interrupt);
    __asm__ __volatile__("msr cntp_cval_el0, %0\n\tmsr cntp_ctl_el0, %1\n\tisb"
                         :: "r"(SavedDeadline), "r"(SavedControl) : "memory");

    ok_eq_long(ControlCalls, 1);
    ok_eq_long(HighCalls, 0);
    ok_eq_ulong((ULONG)PendingControl & 5, 5);
    ok_eq_long(DisabledCalls, 0);
    ok_eq_bool(Enabled, TRUE);
    ok_eq_long(EnabledCalls, 1);
    ok_eq_bool(Connected, TRUE);
    ok_eq_long(State.BadOwner, 0);
    trace("PPI_DISABLE cpu=%lu control=%ld while_high=%ld pending=%u after_disable=%ld after_reenable=%ld owner_errors=%ld\n",
          Cpu, ControlCalls, HighCalls, (ULONG)((PendingControl & 5) == 5),
          DisabledCalls, EnabledCalls, State.BadOwner);
Done:
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

START_TEST(KeArm64IpiPreemption)
{
    PKMT_DISABLE_INTERRUPT Disable;
    PKMT_ENABLE_INTERRUPT Enable;
    ULONG Cpu;

    if (skip(KeNumberProcessors >= 2, "SMP required\n")) return;
    for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
        TestProcessor(Cpu);
    TestBankLifetime();
    Disable = (PKMT_DISABLE_INTERRUPT)KmtGetSystemRoutineAddress(L"HalDisableSystemInterrupt");
    Enable = (PKMT_ENABLE_INTERRUPT)KmtGetSystemRoutineAddress(L"HalEnableSystemInterrupt");
    if (!skip(Disable != NULL && Enable != NULL, "HAL interrupt mask routines unavailable\n"))
        for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
            TestDisabledPending(Cpu, Disable, Enable);
}
