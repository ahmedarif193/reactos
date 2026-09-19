/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V thread context creation, switch completion and idle loop
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * Initial kernel stack of a thread that has not run yet:
 *
 *   InitialStack -> +-------------------+
 *                   |   KSTART_FRAME    |  popped by KiThreadStartup
 *                   +-------------------+
 *   KernelStack  -> |   KSWITCH_FRAME   |  Ra = KiThreadStartup, s0-s11 = 0
 *                   +-------------------+
 */
typedef struct _KKINIT_FRAME
{
    KSWITCH_FRAME SwitchFrame;
    KSTART_FRAME StartFrame;
} KKINIT_FRAME, *PKKINIT_FRAME;

C_ASSERT((sizeof(KKINIT_FRAME) & 15) == 0);

VOID
NTAPI
KiInitializeContextThread(
    _Inout_ PKTHREAD Thread,
    _In_opt_ PKSYSTEM_ROUTINE SystemRoutine,
    _In_opt_ PKSTART_ROUTINE StartRoutine,
    _In_opt_ PVOID StartContext,
    _In_opt_ PCONTEXT Context)
{
    PKKINIT_FRAME InitFrame;
    ULONG_PTR StackPointer;
    SIZE_T FrameSize;
    PKTRAP_FRAME TrapFrame = NULL;

    Thread->CallbackStack = NULL;

    /* The boot thread is already executing on the loader-owned stack. It is
     * adopted in place: its first KSWITCH_FRAME is built when it switches. */
    if ((Thread == KeGetCurrentThread()) && (SystemRoutine == NULL) &&
        (StartRoutine == NULL) && (StartContext == NULL) && (Context == NULL))
    {
        __asm__ __volatile__("mv %0, sp" : "=r"(StackPointer) :: "memory");
        if ((StackPointer < Thread->StackLimit) ||
            (StackPointer >= (ULONG_PTR)Thread->StackBase) ||
            (StackPointer & 15))
        {
            KiRiscvUnimplemented("KiInitializeContextThread/boot-stack");
        }

        Thread->PreviousMode = KernelMode;
        Thread->KernelStack = (PVOID)StackPointer;
        Thread->TrapFrame = NULL;
        return;
    }

    if (SystemRoutine == NULL)
        KiRiscvUnimplemented("KiInitializeContextThread/no-system-routine");

    FrameSize = sizeof(KKINIT_FRAME) + (Context ? sizeof(KTRAP_FRAME) : 0);
    if (((ULONG_PTR)Thread->InitialStack & 15) ||
        ((ULONG_PTR)Thread->InitialStack - Thread->StackLimit < FrameSize))
    {
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                     STATUS_BAD_INITIAL_STACK,
                     (ULONG_PTR)Thread,
                     (ULONG_PTR)Thread->InitialStack,
                     Thread->StackLimit);
    }

    if (Context)
    {
        /* Keep the complete user register bank above the startup frames.
         * Zero FP state even for an integer-only initial context, so the
         * eventual first return cannot expose another thread's registers. */
        TrapFrame = ((PKTRAP_FRAME)Thread->InitialStack) - 1;
        RtlZeroMemory(TrapFrame, sizeof(*TrapFrame));
        TrapFrame->Context.ContextFlags = CONTEXT_FULL;
        KeContextToTrapFrame(Context, NULL, TrapFrame, Context->ContextFlags, UserMode);
        TrapFrame->Context.Tp = (ULONG_PTR)Thread->Teb;
        TrapFrame->PreviousIrql = PASSIVE_LEVEL;
        InitFrame = ((PKKINIT_FRAME)TrapFrame) - 1;
    }
    else
    {
        InitFrame = ((PKKINIT_FRAME)Thread->InitialStack) - 1;
    }
    RtlZeroMemory(InitFrame, sizeof(*InitFrame));
    InitFrame->StartFrame.SystemRoutine = (ULONG64)SystemRoutine;
    InitFrame->StartFrame.StartRoutine = (ULONG64)StartRoutine;
    InitFrame->StartFrame.StartContext = (ULONG64)StartContext;
    InitFrame->SwitchFrame.Ra = (ULONG64)KiThreadStartup;
    InitFrame->SwitchFrame.ApcBypass = APC_LEVEL;

    Thread->PreviousMode = Context ? UserMode : KernelMode;
    Thread->KernelStack = &InitFrame->SwitchFrame;
    Thread->TrapFrame = TrapFrame;
}

/* Runs on the incoming thread's stack with the outgoing frame published. */
BOOLEAN
NTAPI
KiSwapContextResume(
    _In_ BOOLEAN ApcBypass,
    _In_ PKTHREAD OldThread,
    _In_ PKTHREAD NewThread)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKPROCESS OldProcess, NewProcess;

    ASSERT(Prcb->CurrentThread == NewThread);
    if (Prcb->DpcRoutineActive)
    {
        KeBugCheckEx(ATTEMPTED_SWITCH_FROM_DPC,
                     (ULONG_PTR)OldThread,
                     (ULONG_PTR)NewThread,
                     (ULONG_PTR)OldThread->InitialStack,
                     0);
    }

    OldProcess = OldThread->ApcState.Process;
    NewProcess = NewThread->ApcState.Process;
    if (OldProcess != NewProcess)
        KiSwapProcess(NewProcess, OldProcess);

    Prcb->KeContextSwitches++;
    NewThread->ContextSwitches++;
#if (NTDDI_VERSION >= NTDDI_WIN7)
    NewThread->Running = TRUE;
    OldThread->Running = FALSE;
#endif

    if (NewThread->ApcState.KernelApcPending)
    {
        if ((NewThread->SpecialApcDisable == 0) && !ApcBypass)
            return TRUE;
        HalRequestSoftwareInterrupt(APC_LEVEL);
    }
    return FALSE;
}

/* Every process root already carries the shared kernel half, so only the
 * root PPN changes. No ASID is allocated. */
VOID
NTAPI
KiSwapProcess(
    _In_ PKPROCESS NewProcess,
    _In_ PKPROCESS OldProcess)
{
    ULONG_PTR Satp;

    UNREFERENCED_PARAMETER(OldProcess);
    ASSERT((NewProcess->DirectoryTableBase & (PAGE_SIZE - 1)) == 0);
    ASSERT(NewProcess->DirectoryTableBase != 0);

    Satp = RISCV64_LOADER_SATP_MODE_SV39 | (NewProcess->DirectoryTableBase >> PAGE_SHIFT);
    __asm__ __volatile__("csrw satp, %0\n\tsfence.vma zero, zero" :: "r"(Satp) : "memory");
}

/* No per-thread coprocessor ownership or DPC affinity exists yet. */
VOID
NTAPI
KiRundownThread(_In_ PKTHREAD Thread)
{
    UNREFERENCED_PARAMETER(Thread);
}

/* Interrupt sources are enabled by the HAL alone. Until it enables one, a
 * wfi could only wake through a pending software request, so the idle hart
 * polls instead of sleeping; the interrupt enable bit stays clear so a new
 * thread inherits the boot-time masked state through KiSwapContext. */
static
VOID
KiRiscvIdleWait(_In_ PKPCR Pcr)
{
    if (Pcr->InterruptEnable == 0)
    {
        YieldProcessor();
        return;
    }

    _enable();
    __asm__ __volatile__("wfi" ::: "memory");
    _disable();
}

DECLSPEC_NORETURN
VOID
KiIdleLoop(VOID)
{
    PKPCR Pcr = KeGetPcr();
    PKPRCB Prcb = &Pcr->Prcb;
    PKTHREAD OldThread, NewThread;

    ASSERT(Prcb->CurrentThread == Prcb->IdleThread);
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    for (;;)
    {
        _disable();
        if ((Prcb->DpcData[0].DpcQueueDepth) ||
            (Prcb->TimerRequest) ||
            (Prcb->DeferredReadyListHead.Next))
        {
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            KiRetireDpcListInDpcStack(Prcb, Prcb->DpcStack);
        }

        NewThread = Prcb->NextThread;
        if (NewThread)
        {
            Prcb->NextThread = NULL;
            OldThread = Prcb->CurrentThread;
            Prcb->CurrentThread = NewThread;
            NewThread->State = Running;
            if (Pcr->InterruptEnable)
                _enable();
            KiSwapContext(APC_LEVEL, OldThread);

            /* The thread that switched back may have been at SYNCH_LEVEL. */
            if (KeGetCurrentIrql() > DISPATCH_LEVEL)
                KfLowerIrql(DISPATCH_LEVEL);
            continue;
        }

        Prcb->Sleeping = TRUE;
        KiRiscvIdleWait(Pcr);
        Prcb->Sleeping = FALSE;
    }
}
