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
    PKSWITCH_FRAME SwitchFrame;
    PKSTART_FRAME StartFrame;

    ASSERT(Thread != NULL);
    ASSERT(SystemRoutine != NULL);

    if (ContextPointer != NULL)
    {
        PKUINIT_FRAME InitFrame;
        PKEXCEPTION_FRAME ExceptionFrame;
        PKTRAP_FRAME TrapFrame;

        {
            SIZE_T FrameSize = ALIGN_UP_BY(sizeof(*InitFrame), 16);
            InitFrame = (PKUINIT_FRAME)((ULONG_PTR)Thread->InitialStack - FrameSize);
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
        Thread->InitialStack = (PVOID)InitFrame;
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
            InitFrame = (PKKINIT_FRAME)((ULONG_PTR)Thread->InitialStack - FrameSize);
        }
        RtlZeroMemory(InitFrame, sizeof(*InitFrame));

        SwitchFrame = &InitFrame->SwitchFrame;
        StartFrame = &InitFrame->StartFrame;

        KiArm64SetupStartFrame(StartFrame,
                               SystemRoutine,
                               StartRoutine,
                               StartContext);

        Thread->PreviousMode = KernelMode;
        Thread->InitialStack = (PVOID)InitFrame;
        Thread->KernelStack = SwitchFrame;
        Thread->TrapFrame = NULL;
    }

    SwitchFrame->ReturnAddress = (ULONG64)KiThreadStartup;
    SwitchFrame->ApcBypass = FALSE;
}

DECLSPEC_NORETURN
VOID
KiIdleLoop(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    for (;;)
    {
        if (Prcb->DpcData[0].DpcQueueDepth ||
            Prcb->TimerRequest ||
            Prcb->DeferredReadyListHead.Next)
        {
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);
            KiRetireDpcList(Prcb);
            continue;
        }

        if (Prcb->NextThread)
        {
            PKTHREAD OldThread = Prcb->CurrentThread;
            PKTHREAD NewThread = Prcb->NextThread;

#if defined(_M_ARM64) || defined(__aarch64__)
            {
                extern VOID KiArm64BootStageLog(_In_z_ PCSTR Stage);
                CHAR Stage[160];
                if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                                  sizeof(Stage),
                                                  "[arm64] KiIdleLoop: switching Idle->Thread=%p",
                                                  NewThread)))
                {
                    KiArm64BootStageLog(Stage);
                }
            }
#endif

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

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        extern VOID KiArm64BootStageLog(_In_z_ PCSTR Stage);
        CHAR Stage[192];
        /* Dump switch frame return and start frame addresses for the new thread */
        {
            PKSWITCH_FRAME Sw = (PKSWITCH_FRAME)NewThread->KernelStack;
            PKSTART_FRAME Sf = (PKSTART_FRAME)((ULONG_PTR)NewThread->KernelStack + sizeof(KSWITCH_FRAME));
            if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                              sizeof(Stage),
                                              "[arm64] KiSwapContextResume: NewThread Sw=%p SwReturn=%p Sf=%p Sys=%p Start=%p Ctx=%p",
                                              Sw,
                                              (PVOID)Sw->ReturnAddress,
                                              Sf,
                                              (PVOID)Sf->SystemRoutine,
                                              (PVOID)Sf->StartRoutine,
                                              (PVOID)Sf->StartContext)))
            {
                KiArm64BootStageLog(Stage);
            }
        }
        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] KiSwapContextResume: entry Old=%p New=%p WaitIrql=%u KAP=%u SAD=%u",
                                          OldThread,
                                          NewThread,
                                          (unsigned)WaitIrql,
                                          (unsigned)NewThread->ApcState.KernelApcPending,
                                          (unsigned)NewThread->SpecialApcDisable)))
        {
            KiArm64BootStageLog(Stage);
        }
    }
#endif

    Prcb = KeGetCurrentPrcb();

    NewThread->ContextSwitches++;
    KeArm64CurrentThread = NewThread;

    OldThread->SwapBusy = FALSE;
    if (Prcb != NULL)
    {
        Prcb->KeContextSwitches++;
        Prcb->CurrentThread = NewThread;
    }

    if (OldThread->ApcState.Process != NewThread->ApcState.Process)
    {
        PKPROCESS NewProcess = NewThread->ApcState.Process;
        ASSERT(NewProcess != NULL);
        ASSERT(NewProcess->DirectoryTableBase[0] != 0);
#if defined(_M_ARM64) || defined(__aarch64__)
        /* Bring-up: keep current kernel TTBR; skip user TTBR switch to avoid faults. */
        KiArm64BootStageLog("[arm64] KiSwapContextResume: skipping TTBR switch (bring-up)");
#else
        KiArm64WriteUserTtbr(NewProcess->DirectoryTableBase[0]);
#endif
    }

    if (NewThread->ApcState.KernelApcPending &&
        !NewThread->SpecialApcDisable &&
        !WaitIrql)
    {
#if defined(_M_ARM64) || defined(__aarch64__)
        KiArm64BootStageLog("[arm64] KiSwapContextResume: returning TRUE (Kernel APC pending)");
#endif
        return TRUE;
    }

    if (NewThread->ApcState.KernelApcPending)
    {
#if defined(_M_ARM64) || defined(__aarch64__)
        KiArm64BootStageLog("[arm64] KiSwapContextResume: KernelApcPending true, requesting APC interrupt");
#endif
        HalRequestSoftwareInterrupt(APC_LEVEL);
    }

#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64 bring-up: Directly call the system thread startup routine rather
     * than returning to KiThreadStartup. This is diagnostic to surface any
     * early exception on the RET path and to prove Phase 1 runs.
     */
    {
        extern VOID KiArm64BootStageLog(_In_z_ PCSTR Stage);
        CHAR Stage[160];
        PKSTART_FRAME Sf = (PKSTART_FRAME)((ULONG_PTR)NewThread->KernelStack + sizeof(KSWITCH_FRAME));
        PKSYSTEM_ROUTINE SystemRoutine = (PKSYSTEM_ROUTINE)(ULONG_PTR)Sf->SystemRoutine;
        PKSTART_ROUTINE StartRoutine = (PKSTART_ROUTINE)(ULONG_PTR)Sf->StartRoutine;
        PVOID StartContext = (PVOID)(ULONG_PTR)Sf->StartContext;

        if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                          sizeof(Stage),
                                          "[arm64] KiSwapContextResume: calling SystemRoutine=%p Start=%p Ctx=%p",
                                          SystemRoutine,
                                          StartRoutine,
                                          StartContext)))
        {
            KiArm64BootStageLog(Stage);
        }

        SystemRoutine(StartRoutine, StartContext);

        KiArm64BootStageLog("[arm64] KiSwapContextResume: SystemRoutine returned unexpectedly; terminating thread");
        PsTerminateSystemThread(STATUS_SUCCESS);
    }
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
    KiArm64BootStageLog("[arm64] KiSwapContextResume: exit");
#endif
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

    if ((Prcb->DpcData[0].DpcQueueDepth) ||
        (Prcb->TimerRequest) ||
        (Prcb->DeferredReadyListHead.Next))
    {
        KiRetireDpcList(Prcb);
    }

    _enable();

    if (Prcb->QuantumEnd)
    {
        Prcb->QuantumEnd = FALSE;
        KiQuantumEnd();
    }
    else if (Prcb->NextThread)
    {
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
