/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/thrdini.c
 * PURPOSE:         Thread startup stubs for ARM64
 */

#include <ntoskrnl.h>
#include <fpstate.h>
#define NDEBUG
#include <debug.h>

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
BOOLEAN
KiArm64ShouldTraceIdle(VOID)
{
    return FALSE;
}

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

    /* NT contract: the idle thread runs at DISPATCH_LEVEL; normalize from the boot (HIGH_LEVEL) or AP entry state */
    if (KeGetCurrentIrql() > DISPATCH_LEVEL) KeLowerIrql(DISPATCH_LEVEL);
    if (KeGetCurrentIrql() < DISPATCH_LEVEL) KfRaiseIrql(DISPATCH_LEVEL);

    for (;;)
    {
        /* Close the interrupt window so the work check and wfi are atomic */
        _disable();

        __asm__ __volatile__("dmb ishld" ::: "memory");

        if (Prcb->DpcData[0].DpcQueueDepth || Prcb->TimerRequest || Prcb->DeferredReadyListHead.Next || Prcb->DpcInterruptRequested)
        {
            if (KiArm64ShouldTraceIdle()) DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "[arm64][IDLE] dispatch work: DpcDepth=%lu TimerReq=%p Deferred=%p DpcReq=%u Next=%p\n", Prcb->DpcData[0].DpcQueueDepth, Prcb->TimerRequest, Prcb->DeferredReadyListHead.Next, Prcb->DpcInterruptRequested, Prcb->NextThread);

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
            _enable();
            continue;
        }

        KiAcquirePrcbLock(Prcb);
        if (Prcb->NextThread)
        {
            PKTHREAD OldThread = Prcb->CurrentThread;
            PKTHREAD NewThread = Prcb->NextThread;

            Prcb->NextThread = NULL;
            Prcb->CurrentThread = NewThread;
            NewThread->State = Running;

            KiReleasePrcbLock(Prcb);
            _enable();

            ASSERT(OldThread != NULL);
            KiSwapContext(APC_LEVEL, OldThread);
            if (KeGetCurrentIrql() > DISPATCH_LEVEL) KeLowerIrql(DISPATCH_LEVEL);
            continue;
        }
        KiReleasePrcbLock(Prcb);

        /* WFI wakes on a pending interrupt even with DAIF.I masked; unmasking afterwards takes it immediately */
        __asm__ __volatile__("wfi" ::: "memory");
        _enable();
    }
}

/*
 * Debug trace function called from assembly at key points.
 * Uses DbgPrintEx which should work regardless of NDEBUG.
 */
VOID
FASTCALL
KiDebugTracePoint(
    _In_ ULONG Point,
    _In_ ULONG64 Value1,
    _In_ ULONG64 Value2)
{
    UNREFERENCED_PARAMETER(Point);
    UNREFERENCED_PARAMETER(Value1);
    UNREFERENCED_PARAMETER(Value2);
}

/*
 * Debug function to dump KSWITCH_FRAME contents before returning.
 * Called from assembly to verify stack frame integrity.
 */
VOID
FASTCALL
KiDebugDumpSwitchFrame(
    _In_ PVOID StackPointer,
    _In_ ULONG64 SwLrValue)
{
    /* Disable verbose logging to reduce log spam - only log on errors */
    UNREFERENCED_PARAMETER(StackPointer);
    UNREFERENCED_PARAMETER(SwLrValue);

#if 0  /* Enable for detailed context switch debugging */
    PKSWITCH_FRAME Frame = (PKSWITCH_FRAME)StackPointer;
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD IdleThread = Prcb ? Prcb->IdleThread : NULL;
    PKTHREAD CurrentThread = Prcb ? Prcb->CurrentThread : NULL;

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[KiDebugDumpSwitchFrame] SP=%p SwLr=0x%llx\n",
               StackPointer, SwLrValue);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[KiDebugDumpSwitchFrame] CurrentThread=%p IdleThread=%p IsIdle=%d\n",
               CurrentThread, IdleThread, (CurrentThread == IdleThread) ? 1 : 0);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[KiDebugDumpSwitchFrame] Frame->Lr=0x%llx Frame->ReturnAddress=0x%llx ApcBypass=%u\n",
               Frame->Lr, Frame->ReturnAddress, Frame->ApcBypass);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[KiDebugDumpSwitchFrame] X19=0x%llx X20=0x%llx Fp=0x%llx\n",
               Frame->X19, Frame->X20, Frame->Fp);
#endif
}

/*
 * Debug function called at .Lrestore_context to confirm we reached it.
 */
VOID
FASTCALL
KiDebugRestoreContext(
    _In_ PVOID StackPointer)
{
    UNREFERENCED_PARAMETER(StackPointer);
}

/*
 * Debug function called right before ret x9 to confirm final return.
 */
VOID
FASTCALL
KiDebugBeforeReturn(
    _In_ ULONG64 ReturnAddr)
{
    UNREFERENCED_PARAMETER(ReturnAddr);
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

    if (KiArm64ShouldTraceIdle())
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[arm64][CTX] Resume entry: WaitIrql=%u CurIrql=%u Old=%p(S=%u,P=%d,Busy=%u) New=%p(S=%u,P=%d,Busy=%u)\n",
                   WaitIrql,
                   KeGetCurrentIrql(),
                   OldThread,
                   OldThread->State,
                   OldThread->Priority,
#if (NTDDI_VERSION >= NTDDI_WIN7)
                   0,
#else
                   OldThread->SwapBusy,
#endif
                   NewThread,
                   NewThread->State,
                   NewThread->Priority,
#if (NTDDI_VERSION >= NTDDI_WIN7)
                   0);
#else
                   NewThread->SwapBusy);
#endif
    }

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
#else
    OldThread->Running = FALSE;
#endif
#ifdef _M_ARM64
    __asm__ __volatile__("dmb ishst" ::: "memory");
    __asm__ __volatile__("sev" ::: "memory");
#endif

    /*
     * ARM64: Switch address space (TTBR0) when moving to a different process.
     * This is critical for user-mode execution - without this, user-mode code
     * would execute with the wrong page tables and access the wrong memory.
     *
     * KiSwapProcess will:
     * 1. Update process ActiveProcessors for SMP
     * 2. Write the new process's DirectoryTableBase[0] to TTBR0_EL1
     * 3. Perform necessary TLB invalidation
     */
    OldProcess = OldThread->ApcState.Process;
    NewProcess = NewThread->ApcState.Process;

    if (OldProcess != NewProcess)
    {
        KiSwapProcess(NewProcess, OldProcess);

        if (KiArm64ShouldTraceIdle())
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_ERROR_LEVEL,
                       "[arm64][CTX] Resume after KiSwapProcess\n");
        }
    }

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
        if (KiArm64ShouldTraceIdle())
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_ERROR_LEVEL,
                       "[arm64][CTX] Resume exit: return TRUE for APC delivery\n");
        }
        return TRUE;
    }

    if (NewThread->ApcState.KernelApcPending)
    {
        if (KiArm64ShouldTraceIdle())
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_ERROR_LEVEL,
                       "[arm64][CTX] Resume: New thread has KernelApcPending, WaitIrql=%u\n",
                       WaitIrql);
        }

        HalRequestSoftwareInterrupt(APC_LEVEL);
    }

    if (KiArm64ShouldTraceIdle())
    {
        ULONG64 TpidrEl1;

        __asm__ __volatile__("mrs %0, tpidr_el1" : "=r"(TpidrEl1));

        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[arm64][CTX] Resume exit: return FALSE CurThread=%p tpidr=%p pcr=%p fallbackThread=%p\n",
                   KeGetCurrentThread(),
                   (PVOID)(ULONG_PTR)TpidrEl1,
                   KeArm64CurrentPcr,
                   KeArm64CurrentThread);
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
        KiAcquirePrcbLock(Prcb);

        OldThread = Prcb->CurrentThread;
        NewThread = Prcb->NextThread;

        Prcb->NextThread = NULL;
        Prcb->CurrentThread = NewThread;

        NewThread->State = Running;
        OldThread->WaitReason = WrDispatchInt;

        KxQueueReadyThread(OldThread, Prcb);

        KiSwapContext(APC_LEVEL, OldThread);
    }
}
