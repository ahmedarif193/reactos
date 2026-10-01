/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC thread and process context
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * Initial kernel stack of a thread that has not run yet:
 *
 *   InitialStack -> +-------------------+
 *                   |   KTRAP_FRAME     |  user threads only
 *                   +-------------------+
 *                   |   KSTART_FRAME    |  popped by KiThreadStartup
 *                   +-------------------+
 *   KernelStack  -> |   KSWITCH_FRAME   |  Lr = ..KiThreadStartup
 *                   +-------------------+
 */
typedef struct _KKINIT_FRAME
{
    KSWITCH_FRAME SwitchFrame;
    KSTART_FRAME StartFrame;
} KKINIT_FRAME, *PKKINIT_FRAME;

C_ASSERT((sizeof(KKINIT_FRAME) & 15) == 0);

extern UCHAR KiThreadStartupCode[] __asm__("..KiThreadStartup");

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
    PKTRAP_FRAME TrapFrame = NULL;
    ULONG_PTR StackPointer;
    SIZE_T FrameSize;

    Thread->CallbackStack = NULL;

    /* The boot thread is already executing on the loader-owned stack. */
    if ((Thread == KeGetCurrentThread()) && (SystemRoutine == NULL) && (StartRoutine == NULL) && (StartContext == NULL) && (Context == NULL))
    {
        __asm__ __volatile__("mr %0, 1" : "=r"(StackPointer) :: "memory");
        if ((StackPointer < Thread->StackLimit) || (StackPointer >= (ULONG_PTR)Thread->StackBase))
            KiPpcUnimplemented("KiInitializeContextThread/boot-stack");

        Thread->PreviousMode = KernelMode;
        Thread->KernelStack = (PVOID)StackPointer;
        Thread->TrapFrame = NULL;
        KeGetPcr()->InitialStack = (ULONG_PTR)Thread->InitialStack;
        return;
    }

    if (SystemRoutine == NULL)
        KiPpcUnimplemented("KiInitializeContextThread/no-system-routine");

    FrameSize = sizeof(KKINIT_FRAME) + (Context ? sizeof(KTRAP_FRAME) : 0);
    if (((ULONG_PTR)Thread->InitialStack & 15) || ((ULONG_PTR)Thread->InitialStack - Thread->StackLimit < FrameSize))
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_BAD_INITIAL_STACK, (ULONG_PTR)Thread, (ULONG_PTR)Thread->InitialStack, Thread->StackLimit);

    if (Context)
    {
        /* The complete user register bank sits at the top of the stack, where
         * trap entry from user mode builds its frames. */
        TrapFrame = ((PKTRAP_FRAME)Thread->InitialStack) - 1;
        RtlZeroMemory(TrapFrame, sizeof(*TrapFrame));
        TrapFrame->Context.Msr = PPC_USER_MSR;
        KeContextToTrapFrame(Context, NULL, TrapFrame, Context->ContextFlags | CONTEXT_CONTROL, UserMode);
        TrapFrame->Context.Gpr13 = (ULONG_PTR)Thread->Teb;
        TrapFrame->PreviousIrql = PASSIVE_LEVEL;
        TrapFrame->PreviousMode = UserMode;
        InitFrame = ((PKKINIT_FRAME)TrapFrame) - 1;
    }
    else
    {
        InitFrame = ((PKKINIT_FRAME)Thread->InitialStack) - 1;
    }

    RtlZeroMemory(InitFrame, sizeof(*InitFrame));
    InitFrame->StartFrame.SystemRoutine = (ULONG)SystemRoutine;
    InitFrame->StartFrame.StartRoutine = (ULONG)StartRoutine;
    InitFrame->StartFrame.StartContext = (ULONG)StartContext;
    InitFrame->SwitchFrame.Lr = (ULONG)KiThreadStartupCode;
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
        KeBugCheckEx(ATTEMPTED_SWITCH_FROM_DPC, (ULONG_PTR)OldThread, (ULONG_PTR)NewThread, (ULONG_PTR)OldThread->InitialStack, 0);

    OldProcess = OldThread->ApcState.Process;
    NewProcess = NewThread->ApcState.Process;
    if (OldProcess != NewProcess)
        KiSwapProcess(NewProcess, OldProcess);

    Prcb->KeContextSwitches++;
    NewThread->ContextSwitches++;
#if KI_THREAD_HANDOFF_USES_RUNNING
    KiCompleteThreadSwitch(OldThread, NewThread);
#endif

    if (NewThread->ApcState.KernelApcPending)
    {
        if ((NewThread->SpecialApcDisable == 0) && !ApcBypass)
            return TRUE;
        HalRequestSoftwareInterrupt(APC_LEVEL);
    }
    return FALSE;
}

/* DirectoryTableBase is the physical root of the address space. The user
 * segments' VSIDs derive from its frame, which is unique while the process
 * lives; a reused root frame starts from a flushed hashed page table (see
 * MiArchInitializeProcessRoot). */
VOID
NTAPI
KiSwapProcess(
    _In_ PKPROCESS NewProcess,
    _In_ PKPROCESS OldProcess)
{
    KAFFINITY Member = KeGetCurrentPrcb()->SetMember;

    InterlockedOr((PLONG)&NewProcess->ActiveProcessors, Member);
    KiPpcLoadAddressSpace(NewProcess->DirectoryTableBase, (NewProcess->DirectoryTableBase >> PAGE_SHIFT) << 4);
    InterlockedAnd((PLONG)&OldProcess->ActiveProcessors, ~Member);
}

VOID
NTAPI
KiRundownThread(_In_ PKTHREAD Thread)
{
    UNREFERENCED_PARAMETER(Thread);
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
        if ((Prcb->DpcData[0].DpcQueueDepth) || (Prcb->TimerRequest) || (Prcb->DeferredReadyListHead.Next))
        {
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            KiRetireDpcListInDpcStack(Prcb, Prcb->DpcStack);
        }
        _enable();

        KiAcquirePrcbLock(Prcb);
        if (!Prcb->NextThread)
        {
            NewThread = KiIdleSchedule(Prcb);
            if (NewThread != Prcb->IdleThread)
            {
                NewThread->State = Standby;
                Prcb->NextThread = NewThread;
            }
        }
        NewThread = Prcb->NextThread;
        if (NewThread)
        {
            InterlockedAnd((PLONG)&KiIdleSummary, ~Prcb->SetMember);
            Prcb->NextThread = NULL;
            OldThread = Prcb->CurrentThread;
            Prcb->CurrentThread = NewThread;
            NewThread->State = Running;
            KiReleasePrcbLock(Prcb);
            KiSwapContext(APC_LEVEL, OldThread);
            if (KeGetCurrentIrql() > DISPATCH_LEVEL)
                KfLowerIrql(DISPATCH_LEVEL);
            continue;
        }
        InterlockedOr((PLONG)&KiIdleSummary, Prcb->SetMember);
        KiReleasePrcbLock(Prcb);

        /* The decrementer and devices wake the processor; an interrupt taken
         * here runs its handler before the loop checks for work again. */
        Prcb->Sleeping = TRUE;
        YieldProcessor();
        Prcb->Sleeping = FALSE;
    }
}
