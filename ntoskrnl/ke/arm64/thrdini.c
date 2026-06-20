/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/thrdini.c
 * PURPOSE:         Thread startup stubs for ARM64
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <fpstate.h>
#define NDEBUG
#include <debug.h>
#include <reactos/smpdbg.h>

/*
 * Stack layout used during context acquisition:
 *
 *   +-------------------------------+
 *   |   KTRAP_FRAME (user threads)  |
 *   +-------------------------------+
 *   | KEXCEPTION_FRAME (user only)  |
 *   +-------------------------------+
 *   |        KSTART_FRAME           |
 *   +-------------------------------+
 *   |       KSWITCH_FRAME           |  <- Thread->KernelStack
 *   +-------------------------------+
 */

typedef struct _KUINIT_FRAME
{
    KSWITCH_FRAME SwitchFrame;
    KSTART_FRAME StartFrame;
    KEXCEPTION_FRAME ExceptionFrame;
    KTRAP_FRAME TrapFrame;
} KUINIT_FRAME, *PKUINIT_FRAME;

typedef struct _KKINIT_FRAME
{
    KSWITCH_FRAME SwitchFrame;
    KSTART_FRAME StartFrame;
} KKINIT_FRAME, *PKKINIT_FRAME;

VOID
NTAPI
KiThreadStartup(VOID);

VOID
NTAPI
KiRetireDpcListInDpcStack(
    _In_ PKPRCB Prcb,
    _In_opt_ PVOID DpcStack);

static
VOID
KiArm64SetupStartFrame(
    _Inout_ PKSTART_FRAME StartFrame,
    _In_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_opt_ PVOID StartContext)
{
    RtlZeroMemory(StartFrame, sizeof(*StartFrame));
    StartFrame->StartRoutine = (ULONG64)StartRoutine;
    StartFrame->StartContext = (ULONG64)StartContext;
    StartFrame->SystemRoutine = (ULONG64)SystemRoutine;
    StartFrame->Return = 0;
}

VOID
NTAPI
KiInitializeContextThread(_Inout_ PKTHREAD Thread,
                          _In_ PKSYSTEM_ROUTINE SystemRoutine,
                          _In_ PKSTART_ROUTINE StartRoutine,
                          _In_opt_ PVOID StartContext,
                          _In_opt_ PCONTEXT ContextPointer)
{
    ULONG_PTR StackTop;
    PKSWITCH_FRAME SwitchFrame;
    PKSTART_FRAME StartFrame;

    ASSERT(Thread != NULL);
    ASSERT(SystemRoutine != NULL);

    StackTop = (ULONG_PTR)ALIGN_DOWN_POINTER_BY(Thread->InitialStack, 16);
    Thread->InitialStack = (PVOID)StackTop;

    if (ContextPointer != NULL)
    {
        PKUINIT_FRAME InitFrame;
        PKEXCEPTION_FRAME ExceptionFrame;
        PKTRAP_FRAME TrapFrame;

        {
            SIZE_T FrameSize = ALIGN_UP_BY(sizeof(*InitFrame), 16);
            InitFrame = (PKUINIT_FRAME)(StackTop - FrameSize);
        }
        RtlZeroMemory(InitFrame, sizeof(*InitFrame));

        SwitchFrame = &InitFrame->SwitchFrame;
        StartFrame = &InitFrame->StartFrame;
        ExceptionFrame = &InitFrame->ExceptionFrame;
        TrapFrame = &InitFrame->TrapFrame;

        KiArm64SetupStartFrame(StartFrame,
                               SystemRoutine,
                               StartRoutine,
                               StartContext);

        Thread->PreviousMode = UserMode;
        Thread->KernelStack = SwitchFrame;
        Thread->TrapFrame = TrapFrame;

        KeContextToTrapFrame(ContextPointer,
                             ExceptionFrame,
                             TrapFrame,
                             ContextPointer->ContextFlags | CONTEXT_CONTROL,
                             UserMode);

        TrapFrame->PreviousMode = UserMode;
        TrapFrame->ContextFromKFramesUnwound = FALSE;
        TrapFrame->DebugRegistersValid = FALSE;

        ExceptionFrame->Return = (ULONG64)KiThreadStartup;
    }
    else
    {
        PKKINIT_FRAME InitFrame;

        {
            SIZE_T FrameSize = ALIGN_UP_BY(sizeof(*InitFrame), 16);
            InitFrame = (PKKINIT_FRAME)(StackTop - FrameSize);
        }
        RtlZeroMemory(InitFrame, sizeof(*InitFrame));

        SwitchFrame = &InitFrame->SwitchFrame;
        StartFrame = &InitFrame->StartFrame;

        KiArm64SetupStartFrame(StartFrame,
                               SystemRoutine,
                               StartRoutine,
                               StartContext);

        Thread->PreviousMode = KernelMode;
        Thread->KernelStack = SwitchFrame;
        Thread->TrapFrame = NULL;
    }

    SwitchFrame->ReturnAddress = (ULONG64)KiThreadStartup;
    SwitchFrame->Lr = (ULONG64)KiThreadStartup;
    SwitchFrame->ApcBypass = APC_LEVEL;
}

/*
 * KiArm64IdleDispatchNextThread - dispatch a thread handed to this idle core.
 *
 * Returns TRUE (after KiSwapContext has returned to the idle thread) if a
 * thread was selected and switched to, FALSE if there was nothing to run.
 *
 * Called both from the main idle wait and from the DPC/timer drain branch: a
 * remote CPU can install (or replace) Prcb->NextThread while this core is busy
 * draining DPCs, and that thread must be consumed before parking in WFI.
 *
 * Must be called with interrupts disabled and at DISPATCH_LEVEL (idle context).
 */
static
BOOLEAN
KiArm64IdleDispatchNextThread(_In_ PKPRCB Prcb)
{
    PKTHREAD OldThread, NewThread;

    KiAcquirePrcbLock(Prcb);

    /* Passive consumer: don't run the scheduler here, only drain a thread that
     * KiDeferredReadyThread left on this core's local ready list (no NextThread/IPI). */
    if (!Prcb->NextThread && Prcb->ReadySummary)
    {
        PKTHREAD Ready = KiSelectReadyThread(0, Prcb);
        if (Ready != NULL)
        {
            Ready->State = Standby;
            Prcb->NextThread = Ready;
        }
    }

    if (!Prcb->NextThread)
    {
        KiReleasePrcbLock(Prcb);
        return FALSE;
    }

    OldThread = Prcb->CurrentThread;
    NewThread = Prcb->NextThread;

    /* (diag) idle-switch trace removed: serial print here perturbs the SMP race. */

    Prcb->Sleeping = FALSE;
    InterlockedAnd64((PLONG64)&KiIdleSummary, (LONG64)~Prcb->SetMember);

    Prcb->NextThread = NULL;
    Prcb->CurrentThread = NewThread;
    NewThread->State = Running;

    KiReleasePrcbLock(Prcb);

    ASSERT(OldThread != NULL);
    KiSwapContext(APC_LEVEL, OldThread);
    if (KeGetCurrentIrql() > DISPATCH_LEVEL) KeLowerIrql(DISPATCH_LEVEL);
    return TRUE;
}

DECLSPEC_NORETURN
VOID
KiIdleLoop(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    /*
     * ARM64 UP kernel: APs must NOT touch scheduler state.
     *
     * On a UP kernel (no CONFIG_SMP), spinlocks are no-ops, scheduler data
     * structures are not protected, and there is only one logical CPU as
     * far as the kernel is concerned. APs simply spin in WFI.
     *
     * On an SMP kernel (CONFIG_SMP), APs run the full idle loop below
     * so they can receive IPIs and dispatch threads.
     */
#ifndef CONFIG_SMP
    if (Prcb != NULL && Prcb->Number != 0)
    {
        __asm__ __volatile__("msr daifset, #3" ::: "memory");
        __asm__ __volatile__("isb" ::: "memory");
        for (;;)
        {
            __asm__ __volatile__("wfi" ::: "memory");
        }
    }
#endif

    __asm__ __volatile__("msr daifclr, #0x4" ::: "memory");

    /* NT contract: the idle thread runs at DISPATCH_LEVEL; normalize from the boot (HIGH_LEVEL) or AP entry state */
    if (KeGetCurrentIrql() > DISPATCH_LEVEL) KeLowerIrql(DISPATCH_LEVEL);
    if (KeGetCurrentIrql() < DISPATCH_LEVEL) KfRaiseIrql(DISPATCH_LEVEL);

    for (;;)
    {
        /* Close the interrupt window so the work check and wfi are atomic */
        _disable();

        Prcb->Sleeping = TRUE;
        __asm__ __volatile__("dmb ish" ::: "memory");

        if (Prcb->DpcData[0].DpcQueueDepth || Prcb->TimerRequest || Prcb->DeferredReadyListHead.Next || Prcb->DpcInterruptRequested)
        {
            Prcb->Sleeping = FALSE;

            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            Prcb->DpcInterruptRequested = FALSE;

            if (Prcb->DeferredReadyListHead.Next != NULL)
            {
                KIRQL DeferredIrql = KfRaiseIrql(SYNCH_LEVEL);
                KiProcessDeferredReadyList(Prcb);
                KfLowerIrql(DeferredIrql);
            }

            /* KiRetireDpcList runs at DISPATCH_LEVEL and is entered and exited with interrupts disabled */
            KiRetireDpcList(Prcb);

            /* A remote CPU may have handed us a thread (or the deferred-ready
             * processing above may have produced one) while we were draining.
             * Consume it now instead of looping back and parking in WFI with a
             * runnable thread sitting in Prcb->NextThread. */
            if (KiArm64IdleDispatchNextThread(Prcb))
                continue;

            _enable();
            continue;
        }

        /* Dispatch a thread handed to this core (locally selected or installed
         * by a remote KiDeferredReadyThread); otherwise fall through to WFI. */
        if (KiArm64IdleDispatchNextThread(Prcb))
            continue;

        /* Re-advertise this core as idle so KiSelectNextProcessor can hand off to it. */
        InterlockedOr64((PLONG64)&KiIdleSummary, (LONG64)Prcb->SetMember);
        if (Prcb->SchedulerSubNode != NULL)
        {
            PKSCHEDULER_SUBNODE SubNode = (PKSCHEDULER_SUBNODE)Prcb->SchedulerSubNode;
            InterlockedOr64((PLONG64)&SubNode->IdleCpuSet, (LONG64)Prcb->SetMember);
            if (KiArm64SmtFinalized &&
                (Prcb->MultiThreadProcessorSet & ~SubNode->IdleCpuSet) == 0)
            {
                InterlockedOr64((PLONG64)&SubNode->IdleSmtSet, (LONG64)Prcb->MultiThreadProcessorSet);
                if ((Prcb->MultiThreadProcessorSet & ~SubNode->IdleCpuSet) != 0)
                    InterlockedAnd64((PLONG64)&SubNode->IdleSmtSet, (LONG64)~Prcb->MultiThreadProcessorSet);
            }
        }

        /* SMP boot diagnostics: the idle-loop GIC/PMR probe is DISABLED here.
         * It read ICC_PMR_EL1 (S3_0_C4_C6_0), a GICv3-only system register that
         * is UNDEFINED on a GICv2 CPU interface and traps as an unknown-instruction
         * exception (ESR EC=0) -> bugcheck 0x1E. We probe via silent counters and
         * read GIC state externally (QEMU monitor) instead. */

        /* WFI wakes on a pending interrupt even with DAIF.I masked; unmasking afterwards takes it immediately */
        SmpDbgPark(Prcb->Number);
        __asm__ __volatile__("wfi" ::: "memory");
        SmpDbgWake(Prcb->Number);
        Prcb->Sleeping = FALSE;
        if (Prcb->SchedulerSubNode != NULL)
        {
            PKSCHEDULER_SUBNODE SubNode = (PKSCHEDULER_SUBNODE)Prcb->SchedulerSubNode;
            InterlockedAnd64((PLONG64)&SubNode->IdleCpuSet, (LONG64)~Prcb->SetMember);
            if (KiArm64SmtFinalized)
                InterlockedAnd64((PLONG64)&SubNode->IdleSmtSet, (LONG64)~Prcb->MultiThreadProcessorSet);
        }

        if (Prcb->IpiFrozen == IPI_FROZEN_STATE_TARGET_FREEZE)
        {
            KiProcessorFreezeHandler(NULL, NULL);
        }

        _enable();
    }
}

BOOLEAN
FASTCALL
KiSwapContextResume(
    _In_ KIRQL WaitIrql,
    _Inout_ PKTHREAD OldThread,
    _Inout_ PKTHREAD NewThread)
{
    PKPRCB Prcb;
    PKPROCESS OldProcess, NewProcess;

    ASSERT(OldThread != NULL);
    ASSERT(NewThread != NULL);

    Prcb = KeGetCurrentPrcb();

    NewThread->ContextSwitches++;

    /*
     * Update per-CPU current thread pointer.
     * On SMP, only write the PRCB (per-CPU) - the global is a UP-only fallback.
     * Writing the global on SMP corrupts other CPUs' view of their current thread.
     */
    if (Prcb != NULL)
    {
        Prcb->KeContextSwitches++;
        Prcb->CurrentThread = NewThread;
    }
#ifndef CONFIG_SMP
    KeArm64CurrentThread = NewThread;
#endif

    KeMemoryBarrier();
#if (NTDDI_VERSION < NTDDI_WIN7)
    OldThread->SwapBusy = FALSE;
#elif defined(_M_ARM64)
    OldThread->SwapBusy = FALSE;
#else
    OldThread->Running = FALSE;
#endif
#ifdef _M_ARM64
    __asm__ __volatile__("dmb ishst" ::: "memory");
    __asm__ __volatile__("sev" ::: "memory");
#endif

    /*
     * ARM64: Let KiSwapProcess repair the active TTBR roots. This is needed
     * even for same-process switches because APs can enter the scheduler with
     * stale hardware roots during bring-up.
     *
     * KiSwapProcess will:
     * 1. Update process ActiveProcessors for SMP
     * 2. Write the new process's DirectoryTableBase[0] to TTBR0_EL1
     * 3. Perform necessary TLB invalidation
     */
    OldProcess = OldThread->ApcState.Process;
    NewProcess = NewThread->ApcState.Process;

    KiSwapProcess(NewProcess, OldProcess);

    /*
     * FP/SIMD trap-on-first-use remains deferred until the ARM64 context-switch
     * path has validated IRQL-safe state handling.
     */

    /*
     * ARM64 ABI: Do NOT set x18=TEB or tpidr_el0=TEB here.
     *
     * Per Microsoft's ARM64 Windows ABI conventions:
     *   - In kernel mode (EL1): x18 = KPCR  (platform register)
     *   - In user mode  (EL0): x18 = TEB
     *   - TPIDR_EL0: reserved (not part of the ABI contract)
     *
     * Setting x18=TEB while still executing in EL1 violates the ABI and
     * corrupts the kernel's platform register.  The correct location for
     * x18=TEB and tpidr_el0=TEB is KiTrapReturn (trapret.S), which sets
     * them immediately before ERET to EL0.
     *
     * TPIDR_EL0 is written in trapret.S as an internal convenience for
     * user-mode NtCurrentTeb() implementations, but it is NOT backed by
     * Microsoft's ABI contract (they mark it as "reserved").
     */

    if (NewThread->ApcState.KernelApcPending &&
        !NewThread->SpecialApcDisable &&
        !WaitIrql)
    {
        return TRUE;
    }

    if (NewThread->ApcState.KernelApcPending)
    {
        HalRequestSoftwareInterrupt(APC_LEVEL);
    }

    return FALSE;
}

VOID
NTAPI
KiDispatchInterrupt(VOID)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    PKPRCB Prcb = &Pcr->Prcb;
    PKTHREAD NewThread, OldThread;

    _disable();

    /* Process pending DPCs, timers, and deferred ready threads */
    if ((Prcb->DpcData[0].DpcQueueDepth) ||
        (Prcb->TimerRequest) ||
        (Prcb->DeferredReadyListHead.Next))
    {
        KiRetireDpcListInDpcStack(Prcb, Prcb->DpcStack);
    }

    _enable();

    /* Handle quantum end - this may raise/lower IRQL internally */
    if (Prcb->QuantumEnd)
    {
        Prcb->QuantumEnd = FALSE;
        KiQuantumEnd();
    }
    else if (Prcb->NextThread)
    {
        /* Thread switch needed - acquire PRCB lock and swap context */
        _disable();
        KiAcquirePrcbLock(Prcb);

        OldThread = Prcb->CurrentThread;
        NewThread = Prcb->NextThread;

        if (OldThread == Prcb->IdleThread)
        {
            /* Idle thread is never on a ready list; abandon it instead of queueing
             * it through KxQueueReadyThread, which would corrupt the lists. */
            InterlockedAnd64((PLONG64)&KiIdleSummary, (LONG64)~Prcb->SetMember);
            Prcb->NextThread = NULL;
            Prcb->CurrentThread = NewThread;
            NewThread->State = Running;
            KiReleasePrcbLock(Prcb);
        }
        else
        {
            KiSetThreadSwapBusy(OldThread);

            Prcb->NextThread = NULL;
            Prcb->CurrentThread = NewThread;

            NewThread->State = Running;
            OldThread->WaitReason = WrDispatchInt;

            KxQueueReadyThread(OldThread, Prcb);
        }

        KiSwapContext(APC_LEVEL, OldThread);
    }
}
