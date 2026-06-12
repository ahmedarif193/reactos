/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Kernel-debugger freeze helpers
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern BOOLEAN KiHalInitialized;

KIRQL
NTAPI
KxFreezeExecutionRaiseIrql(
    VOID)
{
    KIRQL OldIrql;

    /*
     * Bypass the generic IRQL path while entering KDBG freeze on ARM64.
     * The debugger only needs a stable HIGH_LEVEL mask across DAIF and PMR.
     */
    OldIrql = KeGetCurrentIrql();
    __asm__ __volatile__("msr daifset, #0xf" ::: "memory");
    __asm__ __volatile__("dsb sy" ::: "memory");
    if (KiHalInitialized)
    {
        HalSetGicPriorityMask(HIGH_LEVEL);
    }
    KiSetCurrentIrql(HIGH_LEVEL);
    __asm__ __volatile__("isb" ::: "memory");

    return OldIrql;
}

VOID
NTAPI
KxFreezeExecutionLowerIrql(
    _In_ KIRQL OldIrql)
{
    /*
     * Restore the saved logical IRQL and matching GIC PMR.
     * KeRestoreInterrupts() handles the final DAIF state on the shared path.
     */
    KiSetCurrentIrql(OldIrql);
    if (KiHalInitialized)
    {
        HalSetGicPriorityMask(OldIrql);
    }
    __asm__ __volatile__("dsb sy\n\tisb" ::: "memory");
}

#ifdef CONFIG_SMP

VOID
NTAPI
HalRequestDebugWakeIpi(
    _In_ KAFFINITY TargetSet);

static PKPRCB KiFreezeOwner;
static KAFFINITY KiFreezeTargetSet;

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

    if (InterlockedCompareExchange((PLONG)&CurrentPrcb->IpiFrozen,
                                   IPI_FROZEN_STATE_FROZEN,
                                   IPI_FROZEN_STATE_TARGET_FREEZE) !=
        IPI_FROZEN_STATE_TARGET_FREEZE)
    {
        return FALSE;
    }
    KiSaveProcessorState(TrapFrame, ExceptionFrame);

    {
        ULONG64 Spins = 0;

        __asm__ __volatile__("msr daifset, #2" ::: "memory");

        for (;;)
        {
            ULONG FrozenState = CurrentPrcb->IpiFrozen;

            if (FrozenState == IPI_FROZEN_STATE_THAW || FrozenState == IPI_FROZEN_STATE_RUNNING)
                break;

            if (KiFreezeOwner == NULL)
                break;

            if (FrozenState & IPI_FROZEN_FLAG_ACTIVE)
            {
                KCONTINUE_STATUS ContinueStatus;

                ContinueStatus = KdReportProcessorChange();
                CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_FROZEN;

                if ((ContinueStatus == ContinueSuccess) && (KiFreezeOwner != NULL))
                {
                    KiFreezeOwner->IpiFrozen = IPI_FROZEN_STATE_THAW;
                }
            }

            if (++Spins > 2000)
            {
                __asm__ __volatile__("wfi" ::: "memory");
            }
            else
            {
                YieldProcessor();
            }
            KeMemoryBarrier();
        }

        __asm__ __volatile__("msr daifclr, #2" ::: "memory");
    }

    KiRestoreProcessorState(TrapFrame, ExceptionFrame);
    InterlockedExchange((PLONG)&CurrentPrcb->IpiFrozen, IPI_FROZEN_STATE_RUNNING);
    return TRUE;
}

static
VOID
KiArm64WaitForFrozenTargets(
    _In_ PKPRCB CurrentPrcb)
{
    KAFFINITY Remaining = 0;
    ULONG64 Spins = 0;

    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];

        if (TargetPrcb != NULL && TargetPrcb != CurrentPrcb && (KiFreezeTargetSet & TargetPrcb->SetMember))
            Remaining |= TargetPrcb->SetMember;
    }

    while (Remaining != 0)
    {
        for (ULONG i = 0; i < KeNumberProcessors; i++)
        {
            PKPRCB TargetPrcb = KiProcessorBlock[i];

            if (TargetPrcb != NULL && (Remaining & TargetPrcb->SetMember) && TargetPrcb->IpiFrozen == IPI_FROZEN_STATE_FROZEN)
                Remaining &= ~TargetPrcb->SetMember;
        }

        if (Remaining == 0)
            break;

        YieldProcessor();
        KeMemoryBarrier();

        if (++Spins > 1000000ULL)
        {
            for (ULONG i = 0; i < KeNumberProcessors; i++)
            {
                PKPRCB TargetPrcb = KiProcessorBlock[i];

                if (TargetPrcb != NULL && (Remaining & TargetPrcb->SetMember))
                {
                    InterlockedCompareExchange((PLONG)&TargetPrcb->IpiFrozen,
                                               IPI_FROZEN_STATE_RUNNING,
                                               IPI_FROZEN_STATE_TARGET_FREEZE);
                }
            }
            break;
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
        if ((TargetPrcb == NULL) || (TargetPrcb == CurrentPrcb) ||
            !(KiFreezeTargetSet & TargetPrcb->SetMember))
        {
            continue;
        }

        InterlockedCompareExchange((PLONG)&TargetPrcb->IpiFrozen,
                                   IPI_FROZEN_STATE_THAW,
                                   IPI_FROZEN_STATE_FROZEN);
    }

    HalRequestDebugWakeIpi(KiFreezeTargetSet);

    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];
        ULONG64 Spins = 0;

        if ((TargetPrcb != NULL) && (TargetPrcb != CurrentPrcb) &&
            (KiFreezeTargetSet & TargetPrcb->SetMember))
        {
            while (TargetPrcb->IpiFrozen != IPI_FROZEN_STATE_RUNNING)
            {
                YieldProcessor();
                KeMemoryBarrier();
                if (++Spins > 4000000ULL)
                {
                    InterlockedCompareExchange((PLONG)&TargetPrcb->IpiFrozen,
                                               IPI_FROZEN_STATE_RUNNING,
                                               IPI_FROZEN_STATE_THAW);
                    break;
                }
            }
        }
    }

    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_RUNNING;
    InterlockedExchangePointer((PVOID *)&KiFreezeOwner, NULL);
    HalRequestDebugWakeIpi(KiFreezeTargetSet);
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
    HalRequestDebugWakeIpi(TargetPrcb->SetMember);

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
            if (InterlockedCompareExchange((PLONG)&CurrentPrcb->IpiFrozen,
                                           IPI_FROZEN_STATE_FROZEN,
                                           IPI_FROZEN_STATE_TARGET_FREEZE) ==
                IPI_FROZEN_STATE_TARGET_FREEZE)
            {
                for (;;)
                {
                    ULONG State = CurrentPrcb->IpiFrozen & ~IPI_FROZEN_FLAG_ACTIVE;

                    if (State == IPI_FROZEN_STATE_THAW || State == IPI_FROZEN_STATE_RUNNING)
                        break;

                    YieldProcessor();
                    KeMemoryBarrier();
                }
                InterlockedExchange((PLONG)&CurrentPrcb->IpiFrozen, IPI_FROZEN_STATE_RUNNING);
            }
            YieldProcessor();
            KeMemoryBarrier();
        }
    }

    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE;
    KiFreezeTargetSet = KeActiveProcessors & ~CurrentPrcb->SetMember;

    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        PKPRCB TargetPrcb = KiProcessorBlock[i];
        ULONG Spins = 0;

        if ((TargetPrcb == NULL) ||
            (TargetPrcb == CurrentPrcb) ||
            !(KiFreezeTargetSet & TargetPrcb->SetMember))
        {
            continue;
        }

        while (InterlockedCompareExchange((PLONG)&TargetPrcb->IpiFrozen,
                                          IPI_FROZEN_STATE_TARGET_FREEZE,
                                          IPI_FROZEN_STATE_RUNNING) !=
               IPI_FROZEN_STATE_RUNNING)
        {
            YieldProcessor();
            KeMemoryBarrier();
            if (++Spins > 2000000UL)
            {
                break;
            }
        }
    }

    /*
     * Send IPI directly via HalRequestIpi, NOT through KiIpiSend.
     *
     * KiIpiSend uses InterlockedBitTestAndSet on IpiFrozen to signal the
     * IPI type, but the freeze code uses IpiFrozen as a STATE value
     * (IPI_FROZEN_STATE_TARGET_FREEZE = 5). KiIpiSend would corrupt the
     * state by setting bit IPI_FREEZE (bit 4), changing 5 → 21.
     *
     * The freeze state is already communicated via IpiFrozen assignment above.
     * We just need the SGI to interrupt the target CPUs.
     */
    HalRequestIpi(KiFreezeTargetSet);
    HalRequestDebugWakeIpi(KiFreezeTargetSet);
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
