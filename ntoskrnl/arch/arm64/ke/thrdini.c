/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/thrdini.c
 * PURPOSE:         Thread startup stubs for ARM64
 */

#include <ntoskrnl.h>
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
    SwitchFrame->ApcBypass = APC_LEVEL;
}

DECLSPEC_NORETURN
VOID
KiIdleLoop(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    static ULONG IdleLoopCounter = 0;
    static ULONG LastLogCounter = 0;
    static BOOLEAN FirstIdle = TRUE;

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[KiIdleLoop] ENTRY - Prcb=%p\n", Prcb);

    for (;;)
    {
        IdleLoopCounter++;

        /*
         * Every few iterations, print that we're alive.
         * This helps detect if the idle loop is running but stuck.
         */
        if (IdleLoopCounter % 100000 == 1)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[KiIdleLoop] Alive loop=%lu ReadySummary=0x%llx NextThread=%p\n",
                       IdleLoopCounter, (ULONG64)Prcb->ReadySummary, Prcb->NextThread);
        }

        /*
         * ARM64 CRITICAL: Issue a data memory barrier before checking scheduler
         * state variables. This ensures we see updates from other CPUs/threads
         * that may have modified DPC queues or ready lists.
         *
         * Without this barrier, the weakly-ordered ARM64 memory model could cause
         * us to see stale values and miss ready threads or pending DPCs.
         */
        __asm__ __volatile__("dmb ish" ::: "memory");

        /*
         * Debug: Check ready summary periodically to find hung threads.
         * This helps diagnose cases where threads are on ready queues but
         * never get scheduled.
         */
        if (FirstIdle || (IdleLoopCounter % 5000000 == 0))
        {
            FirstIdle = FALSE;
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[KiIdleLoop] Loop=%lu ReadySummary=0x%llx NextThread=%p DpcDepth=%lu DefReady=%p\n",
                       IdleLoopCounter,
                       (ULONG64)Prcb->ReadySummary,
                       Prcb->NextThread,
                       Prcb->DpcData[0].DpcQueueDepth,
                       Prcb->DeferredReadyListHead.Next);
        }

        /*
         * Check for pending DPC work. On ARM64, also check DpcInterruptRequested
         * which is set by HalRequestSoftwareInterrupt when a DPC is queued.
         */
        if (Prcb->DpcData[0].DpcQueueDepth ||
            Prcb->TimerRequest ||
            Prcb->DeferredReadyListHead.Next ||
            Prcb->DpcInterruptRequested)
        {
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            Prcb->DpcInterruptRequested = FALSE;
            KiRetireDpcList(Prcb);

            /* ARM64: Memory barrier after KiRetireDpcList to ensure NextThread is visible */
            __asm__ __volatile__("dmb ish" ::: "memory");
            continue;
        }

        /*
         * ARM64 FIX: If we have threads in the ready queue (ReadySummary != 0),
         * but no NextThread, something is wrong. Try to select a ready thread.
         * This catches cases where a thread was placed on the ready queue but
         * NextThread was never set due to timing issues.
         */
        if (Prcb->ReadySummary && !Prcb->NextThread)
        {
            PKTHREAD ReadyThread;
            KIRQL OldIrql;

            /* Acquire PRCB lock at SYNCH_LEVEL to select a thread */
            OldIrql = KeRaiseIrqlToSynchLevel();
            KiAcquirePrcbLock(Prcb);

            /* Double-check after acquiring lock */
            if (Prcb->ReadySummary && !Prcb->NextThread)
            {
                ReadyThread = KiSelectReadyThread(0, Prcb);
                if (ReadyThread)
                {
                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                               "[KiIdleLoop] FIX: Found ready thread %p (Priority=%d) from ReadySummary=0x%llx\n",
                               ReadyThread, ReadyThread->Priority, (ULONG64)Prcb->ReadySummary);
                    ReadyThread->State = Standby;
                    Prcb->NextThread = ReadyThread;
                }
            }

            KiReleasePrcbLock(Prcb);
            KeLowerIrql(OldIrql);
        }

        if (Prcb->NextThread)
        {
            PKTHREAD OldThread = Prcb->CurrentThread;
            PKTHREAD NewThread = Prcb->NextThread;

            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[KiIdleLoop] Switching to NextThread=%p (State=%d Pri=%d) from Idle=%p\n",
                       NewThread, NewThread->State, NewThread->Priority, OldThread);

            Prcb->NextThread = NULL;
            Prcb->CurrentThread = NewThread;
            NewThread->State = Running;

            KiSwapContext(APC_LEVEL, OldThread);
            continue;
        }

        KeStallExecutionProcessor(50);
        __asm__ __volatile__("wfe" ::: "memory");
    }
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
    PKSWITCH_FRAME Frame = (PKSWITCH_FRAME)StackPointer;

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[KiDebugRestoreContext] Reached! SP=%p ReturnAddress=0x%llx\n",
               StackPointer, Frame->ReturnAddress);
}

/*
 * Debug function called right before ret x9 to confirm final return.
 */
VOID
FASTCALL
KiDebugBeforeReturn(
    _In_ ULONG64 ReturnAddr)
{
    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[KiDebugBeforeReturn] About to return to 0x%llx\n",
               ReturnAddr);
}

BOOLEAN
FASTCALL
KiSwapContextResume(
    _In_ KIRQL WaitIrql,
    _Inout_ PKTHREAD OldThread,
    _Inout_ PKTHREAD NewThread)
{
    PKPRCB Prcb;

    ASSERT(OldThread != NULL);
    ASSERT(NewThread != NULL);

    Prcb = KeGetCurrentPrcb();

    NewThread->ContextSwitches++;
    KeArm64CurrentThread = NewThread;

    OldThread->SwapBusy = FALSE;
    if (Prcb != NULL)
    {
        Prcb->KeContextSwitches++;
        Prcb->CurrentThread = NewThread;
    }

    /* Skip address space switch during bring-up to avoid TLB issues */
    /* TODO: Implement proper TTBR switch when user-mode is supported */

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
    KIRQL OldIrql;

    /*
     * ARM64 DPC/Dispatch Interrupt Handler
     *
     * This function processes pending DPCs and handles thread scheduling.
     * It follows the same pattern as AMD64's KiDpcInterruptHandler:
     *
     * 1. Save current IRQL and raise to DISPATCH_LEVEL
     * 2. Process DPCs (retire DPC list)
     * 3. Handle quantum end or thread switch if needed
     * 4. Restore original IRQL at the END
     *
     * CRITICAL: The IRQL restoration MUST happen at the very end of this
     * function, AFTER any KiQuantumEnd() or context switch operations.
     * This is because:
     * - KiQuantumEnd() raises IRQL to SYNCH_LEVEL then lowers to DISPATCH_LEVEL
     * - KiSwapContext may change threads and IRQL state
     *
     * If we restore IRQL before these operations, the function will return
     * with IRQL stuck at DISPATCH_LEVEL instead of the original IRQL,
     * causing PAGED_CODE assertions to fail in subsequent code.
     *
     * This function is called from two contexts:
     * 1. Hardware IRQ handler (interrupt.c) - already at elevated IRQL
     * 2. KfLowerIrql (irql.c) - at the newly lowered IRQL
     *
     * We use direct IRQL manipulation (KiSetCurrentIrql/KiApplyIrqMaskForIrqlTransition)
     * instead of KfLowerIrql() to avoid infinite recursion, since KfLowerIrql()
     * calls this function when DpcInterruptRequested is set.
     */

    /* Save current IRQL and raise to DISPATCH_LEVEL for DPC processing */
    OldIrql = KeGetCurrentIrql();
    if (OldIrql < DISPATCH_LEVEL)
    {
        KfRaiseIrql(DISPATCH_LEVEL);
    }

    _disable();

    /* Process pending DPCs, timers, and deferred ready threads */
    if ((Prcb->DpcData[0].DpcQueueDepth) ||
        (Prcb->TimerRequest) ||
        (Prcb->DeferredReadyListHead.Next))
    {
        KiRetireDpcList(Prcb);
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

    /*
     * CRITICAL: Restore original IRQL at the END, after all dispatch work.
     *
     * Following the AMD64 KiDpcInterruptHandler pattern:
     * 1. Disable interrupts to prevent new hardware interrupts during lowering
     * 2. Call KeLowerIrql to restore original IRQL
     *
     * Note: KeLowerIrql may check for DpcInterruptRequested and call back into
     * KiDispatchInterrupt, but this is safe because:
     * - DpcInterruptRequested is cleared BEFORE calling KiDispatchInterrupt in irql.c
     * - So we won't get infinite recursion, just one level of "retry" if new DPCs
     *   were queued during our dispatch processing
     *
     * If no new DPCs were queued, KeLowerIrql simply lowers the IRQL.
     */
    _disable();
    KeLowerIrql(OldIrql);
}
