/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Kernel-debugger freeze helpers
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#ifdef CONFIG_SMP

static PKPRCB KiFreezeOwner;

BOOLEAN
KiProcessorFreezeHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB CurrentPrcb;

    CurrentPrcb = KeGetCurrentPrcb();
    if ((CurrentPrcb == NULL) ||
        (CurrentPrcb->IpiFrozen != IPI_FROZEN_STATE_TARGET_FREEZE))
    {
        return FALSE;
    }

    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_FROZEN;
    KiSaveProcessorState(TrapFrame, ExceptionFrame);

    for (;;)
    {
        if (CurrentPrcb->IpiFrozen == IPI_FROZEN_STATE_THAW)
        {
            break;
        }

        if (CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE)
        {
            KCONTINUE_STATUS ContinueStatus;

            ContinueStatus = KdReportProcessorChange();
            CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_FROZEN;

            if ((ContinueStatus == ContinueSuccess) && (KiFreezeOwner != NULL))
            {
                KiFreezeOwner->IpiFrozen = IPI_FROZEN_STATE_THAW;
            }
        }

        YieldProcessor();
        KeMemoryBarrier();
    }

    KiRestoreProcessorState(TrapFrame, ExceptionFrame);
    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_RUNNING;
    return TRUE;
}

static
VOID
KiArm64WaitForFrozenTargets(
    _In_ PKPRCB CurrentPrcb)
{
    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if ((TargetPrcb != NULL) && (TargetPrcb != CurrentPrcb))
        {
            while (TargetPrcb->IpiFrozen != IPI_FROZEN_STATE_FROZEN)
            {
                YieldProcessor();
                KeMemoryBarrier();
            }
        }
    }
}

static
VOID
KiArm64RequestThaw(
    _In_ PKPRCB CurrentPrcb)
{
    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if ((TargetPrcb != NULL) && (TargetPrcb != CurrentPrcb))
        {
            ASSERT(TargetPrcb->IpiFrozen == IPI_FROZEN_STATE_FROZEN);
            TargetPrcb->IpiFrozen = IPI_FROZEN_STATE_THAW;
        }
    }

    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if ((TargetPrcb != NULL) && (TargetPrcb != CurrentPrcb))
        {
            while (TargetPrcb->IpiFrozen != IPI_FROZEN_STATE_RUNNING)
            {
                YieldProcessor();
                KeMemoryBarrier();
            }
        }
    }

    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_RUNNING;
    InterlockedExchangePointer((PVOID *)&KiFreezeOwner, NULL);
}

KCONTINUE_STATUS
NTAPI
KxSwitchKdProcessor(
    _In_ ULONG ProcessorIndex)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    PKPRCB TargetPrcb;

    if ((ProcessorIndex >= KeNumberProcessors) ||
        (CurrentPrcb == NULL))
    {
        return ContinueProcessorReselected;
    }

    TargetPrcb = KiProcessorBlock[ProcessorIndex];
    if ((TargetPrcb == NULL) || (TargetPrcb == CurrentPrcb))
    {
        return ContinueProcessorReselected;
    }

    ASSERT(CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE);
    CurrentPrcb->IpiFrozen &= ~IPI_FROZEN_FLAG_ACTIVE;
    TargetPrcb->IpiFrozen |= IPI_FROZEN_FLAG_ACTIVE;

    if (KiFreezeOwner != CurrentPrcb)
    {
        return ContinueNextProcessor;
    }

    while (CurrentPrcb->IpiFrozen == IPI_FROZEN_STATE_OWNER)
    {
        YieldProcessor();
        KeMemoryBarrier();
    }

    if (CurrentPrcb->IpiFrozen == IPI_FROZEN_STATE_THAW)
    {
        CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE;
        return ContinueSuccess;
    }

    ASSERT(CurrentPrcb->IpiFrozen ==
           (IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE));
    return ContinueProcessorReselected;
}

VOID
NTAPI
KxFreezeExecution(
    VOID)
{
    PKPRCB CurrentPrcb;

    CurrentPrcb = KeGetCurrentPrcb();
    if (CurrentPrcb == NULL)
    {
        return;
    }

    if (CurrentPrcb == KiFreezeOwner)
    {
        return;
    }

    while (InterlockedCompareExchangePointer((PVOID *)&KiFreezeOwner,
                                              CurrentPrcb,
                                              NULL) != NULL)
    {
        while (KiFreezeOwner != NULL)
        {
            YieldProcessor();
            KeMemoryBarrier();
        }
    }

    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE;

    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if ((TargetPrcb != NULL) && (TargetPrcb != CurrentPrcb))
        {
            TargetPrcb->IpiFrozen = IPI_FROZEN_STATE_TARGET_FREEZE;
        }
    }

    KiIpiSend(KeActiveProcessors & ~CurrentPrcb->SetMember, IPI_FREEZE);
    KiArm64WaitForFrozenTargets(CurrentPrcb);
}

VOID
NTAPI
KxThawExecution(
    VOID)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    if ((CurrentPrcb == NULL) || !(CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE))
    {
        return;
    }

    KiArm64RequestThaw(CurrentPrcb);
}

#else /* !CONFIG_SMP */

static PKPRCB KiSingleProcessorFreezeOwner;

BOOLEAN
KiProcessorFreezeHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    return FALSE;
}

KCONTINUE_STATUS
NTAPI
KxSwitchKdProcessor(
    _In_ ULONG ProcessorIndex)
{
    UNREFERENCED_PARAMETER(ProcessorIndex);
    return ContinueProcessorReselected;
}

VOID
NTAPI
KxFreezeExecution(
    VOID)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();

    if (CurrentPrcb == NULL)
    {
        return;
    }

    if (KiSingleProcessorFreezeOwner == CurrentPrcb)
    {
        return;
    }

    KiSingleProcessorFreezeOwner = CurrentPrcb;
    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE;
}

VOID
NTAPI
KxThawExecution(
    VOID)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();

    if (CurrentPrcb != NULL)
    {
        CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_RUNNING;
    }

    KiSingleProcessorFreezeOwner = NULL;
}

#endif /* CONFIG_SMP */
