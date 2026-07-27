/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         stubs
 * PROGRAMMERS:     Timo Kreuzer (timo.kreuzer@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

ULONG ProcessCount;
SIZE_T KeXStateLength = sizeof(XSAVE_FORMAT);

PVOID
KiSwitchKernelStackHelper(
    LONG_PTR StackOffset,
    PVOID OldStackBase);

/*
 * Kernel stack layout (example pointers):
 * 0xFFFFFC0F'2D008000 KTHREAD::StackBase
 *    [XSAVE_AREA size == KeXStateLength = 0x440]
 * 0xFFFFFC0F'2D007BC0 KTHREAD::StateSaveArea _XSAVE_FORMAT
 * 0xFFFFFC0F'2D007B90 KTHREAD::InitialStack
 *    [0x190 bytes KTRAP_FRAME]
 * 0xFFFFFC0F'2D007A00 KTHREAD::TrapFrame
 *    [KSTART_FRAME] or ...
 *    [KSWITCH_FRAME]
 * 0xFFFFFC0F'2D007230 KTHREAD::KernelStack
 */

PVOID
NTAPI
KiSwitchKernelStack(PVOID StackBase, PVOID StackLimit)
{
    PKTHREAD CurrentThread;
    PKTRAP_FRAME TrapFrame;
    PVOID OldStackBase;
    ULONG_PTR OldStackLimit;
    ULONG_PTR LinkedTrapFrame;
    LONG_PTR StackOffset;
    SIZE_T StackSize;
    SIZE_T TrapFramesLeft;
    PKIPCR Pcr;

    /* Get the current thread */
    CurrentThread = KeGetCurrentThread();

    /* Save the old stack base */
    OldStackBase = CurrentThread->StackBase;
    OldStackLimit = CurrentThread->StackLimit;

    /* Get the size of the current stack */
    StackSize = (ULONG_PTR)CurrentThread->StackBase - CurrentThread->StackLimit;
    ASSERT(StackSize <= (ULONG_PTR)StackBase - (ULONG_PTR)StackLimit);

    /* Copy the current stack contents to the new stack */
    RtlCopyMemory((PUCHAR)StackBase - StackSize,
                  (PVOID)OldStackLimit,
                  StackSize);

    /* Calculate the offset between the old and the new stack */
    StackOffset = (PUCHAR)StackBase - (PUCHAR)CurrentThread->StackBase;

    /* Relocate the current and linked trap frames. */
    TrapFrame = CurrentThread->TrapFrame;
    if (TrapFrame != NULL)
    {
        ASSERT((ULONG_PTR)TrapFrame >= OldStackLimit);
        ASSERT((ULONG_PTR)TrapFrame <=
               (ULONG_PTR)OldStackBase - sizeof(KTRAP_FRAME));

        TrapFrame = (PKTRAP_FRAME)Add2Ptr(TrapFrame, StackOffset);
        CurrentThread->TrapFrame = TrapFrame;

        TrapFramesLeft = StackSize / sizeof(KTRAP_FRAME);
        while (TrapFramesLeft-- != 0)
        {
            LinkedTrapFrame = TrapFrame->TrapFrame;
            if ((LinkedTrapFrame == 0) ||
                (LinkedTrapFrame < OldStackLimit) ||
                (LinkedTrapFrame >
                 (ULONG_PTR)OldStackBase - sizeof(KTRAP_FRAME)))
            {
                break;
            }

            TrapFrame->TrapFrame =
                (ULONG_PTR)Add2Ptr((PVOID)LinkedTrapFrame, StackOffset);
            TrapFrame = (PKTRAP_FRAME)TrapFrame->TrapFrame;
        }
    }

    /* Set the new initial stack */
    CurrentThread->InitialStack = Add2Ptr(CurrentThread->InitialStack,
                                          StackOffset);

    /* Switch StateSaveArea */
    CurrentThread->StateSaveArea = Add2Ptr(CurrentThread->StateSaveArea,
                                           StackOffset);

    /* Set the new stack limits */
    CurrentThread->StackBase = StackBase;
    CurrentThread->StackLimit = (ULONG_PTR)StackLimit;
    CurrentThread->LargeStack = TRUE;

    /* Adjust RspBase in the PCR */
    Pcr = (PKIPCR)KeGetPcr();
    Pcr->Prcb.RspBase += StackOffset;

    /* Adjust Rsp0 in the TSS */
    Pcr->TssBase->Rsp0 += StackOffset;

    return OldStackBase;
}

static
VOID
KiIdleSwapContext(_In_ PKTHREAD OldThread)
{
#ifdef CONFIG_SMP
    KiSwapContext(APC_LEVEL, OldThread);
    KeLowerIrql(DISPATCH_LEVEL);
#else
    KiSwapContext(APC_LEVEL, OldThread);
#endif
}

DECLSPEC_NORETURN
VOID
KiIdleLoop(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD OldThread, NewThread;

    /* Now loop forever */
    while (TRUE)
    {
        /* Start of the idle loop: disable interrupts */
        _enable();
        YieldProcessor();
        YieldProcessor();
        _disable();

        /* Check for pending timers, pending DPCs, or pending ready threads */
        if ((Prcb->DpcData[0].DpcQueueDepth) ||
            (Prcb->TimerRequest) ||
            (Prcb->DeferredReadyListHead.Next))
        {
            /* Quiesce the DPC software interrupt */
            HalClearSoftwareInterrupt(DISPATCH_LEVEL);

            /* Handle it */
            KiRetireDpcList(Prcb);
        }

        /* Consume scheduled and ready-queued threads under the PRCB lock. */
        _disable();
        KiAcquirePrcbLock(Prcb);

        NewThread = Prcb->NextThread;
        if (NewThread)
        {
            Prcb->NextThread = NULL;
        }
#ifdef CONFIG_SMP
        else
        {
            NewThread = KiIdleSchedule(Prcb);
            if (NewThread == Prcb->IdleThread)
            {
                NewThread = Prcb->NextThread;
                if (NewThread != NULL)
                    Prcb->NextThread = NULL;
            }
        }
#endif

        if (NewThread)
        {
            OldThread = Prcb->CurrentThread;
#ifdef CONFIG_SMP
            KfRaiseIrql(SYNCH_LEVEL);

            if (KiIdleSummary & Prcb->SetMember)
            {
                InterlockedBitTestAndResetAffinity(&KiIdleSummary,
                                                   Prcb->Number);
            }
#endif
            Prcb->CurrentThread = NewThread;
            NewThread->State = Running;
            KiReleasePrcbLock(Prcb);

            _enable();
            KiIdleSwapContext(OldThread);
        }
        else
        {
#ifdef CONFIG_SMP
            if (!(KiIdleSummary & Prcb->SetMember))
            {
                InterlockedBitTestAndSetAffinity(&KiIdleSummary,
                                                 Prcb->Number);
            }
#endif
            Prcb->Sleeping = TRUE;
            KeMemoryBarrier();

            if ((Prcb->DpcData[0].DpcQueueDepth != 0) ||
                (Prcb->TimerRequest != 0) ||
                (Prcb->DeferredReadyListHead.Next != NULL) ||
                (Prcb->DpcInterruptRequested != FALSE))
            {
                Prcb->Sleeping = FALSE;
                KiReleasePrcbLock(Prcb);
                continue;
            }

            /* The HAL performs the atomic STI;HLT boundary. */
            KiReleasePrcbLock(Prcb);
            Prcb->PowerState.IdleFunction(&Prcb->PowerState);
            Prcb->Sleeping = FALSE;
        }
    }
}

VOID
NTAPI
KiSwapProcess(IN PKPROCESS NewProcess,
              IN PKPROCESS OldProcess)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();

#ifdef CONFIG_SMP
    if (NewProcess != OldProcess)
    {
        InterlockedOr64((PLONG64)&NewProcess->ActiveProcessors,
                        Pcr->Prcb.SetMember);
    }
#endif

    /* Update CR3 */
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    __writecr3(NewProcess->DirectoryTableBase);
#else
    __writecr3(NewProcess->DirectoryTableBase[0]);
#endif

#ifdef CONFIG_SMP
    if (NewProcess != OldProcess)
    {
        InterlockedAnd64((PLONG64)&OldProcess->ActiveProcessors,
                         ~Pcr->Prcb.SetMember);
    }
#endif

    /* Update IOPM offset */
    Pcr->TssBase->IoMapBase = NewProcess->IopmOffset;
}

NTSTATUS
NTAPI
NtSetLdtEntries(ULONG Selector1, LDT_ENTRY LdtEntry1, ULONG Selector2, LDT_ENTRY LdtEntry2)
{
    UNIMPLEMENTED;
    __debugbreak();
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
NTAPI
NtVdmControl(IN ULONG ControlCode,
             IN PVOID ControlData)
{
    /* Not supported */
    return STATUS_NOT_IMPLEMENTED;
}
