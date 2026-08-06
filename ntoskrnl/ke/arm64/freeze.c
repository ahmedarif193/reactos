/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Kernel-debugger freeze helpers
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

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
    /*
     * The idle and pending-IPI paths may notice a freeze request before the
     * SGI carrying the interrupted register state is dispatched.  They call
     * this routine without frames and must not claim the request: publishing
     * FROZEN in that case would expose a zero or stale ProcessorState to KD.
     * Leave TARGET_FREEZE pending for the real IRQ path instead.
     */
    if ((CurrentPrcb == NULL) ||
        (TrapFrame == NULL) ||
        (CurrentPrcb->IpiFrozen != IPI_FROZEN_STATE_TARGET_FREEZE))
    {
        return FALSE;
    }

    if (InterlockedCompareExchange((PLONG)&CurrentPrcb->IpiFrozen, IPI_FROZEN_STATE_SAVING, IPI_FROZEN_STATE_TARGET_FREEZE) !=
        IPI_FROZEN_STATE_TARGET_FREEZE)
    {
        return FALSE;
    }
    KiSaveProcessorState(TrapFrame, ExceptionFrame);
    KeMemoryBarrier();
    InterlockedExchange((PLONG)&CurrentPrcb->IpiFrozen, IPI_FROZEN_STATE_FROZEN);

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
                /* IRQs are masked while the interrupted state is frozen.
                 * Wait on the architectural event register so the owner can
                 * wake us without relying on interrupt delivery. */
                __asm__ __volatile__("wfe" ::: "memory");
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

            if ((TargetPrcb != NULL) &&
                (Remaining & TargetPrcb->SetMember) &&
                ((TargetPrcb->IpiFrozen & ~IPI_FROZEN_FLAG_ACTIVE) ==
                 IPI_FROZEN_STATE_FROZEN))
                Remaining &= ~TargetPrcb->SetMember;
        }

        if (Remaining == 0)
            break;

        YieldProcessor();
        KeMemoryBarrier();

        if (++Spins > 1000000ULL)
        {
            KAFFINITY RetrySet = 0;

            for (ULONG i = 0; i < KeNumberProcessors; i++)
            {
                PKPRCB TargetPrcb = KiProcessorBlock[i];
                ULONG State;

                if ((TargetPrcb == NULL) ||
                    !(Remaining & TargetPrcb->SetMember))
                {
                    continue;
                }

                State = TargetPrcb->IpiFrozen & ~IPI_FROZEN_FLAG_ACTIVE;
                if (State == IPI_FROZEN_STATE_SAVING)
                {
                    /* State publication is in progress; it is not safe to
                     * abandon or thaw this processor until saving completes. */
                    continue;
                }

                if (State == IPI_FROZEN_STATE_FROZEN)
                {
                    Remaining &= ~TargetPrcb->SetMember;
                    continue;
                }

                /* Never proceed with an active processor left running: KDB
                 * commands assume a quiesced machine. Re-send both the debug
                 * wake SGI and the generic IPI until the target publishes a
                 * complete state. */
                if (State == IPI_FROZEN_STATE_TARGET_FREEZE)
                    RetrySet |= TargetPrcb->SetMember;
            }

            if (RetrySet != 0)
            {
                HalRequestDebugWakeIpi(RetrySet);
                HalRequestIpi(RetrySet);
            }

            Spins = 0;
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

    KeMemoryBarrier();
    __asm__ __volatile__("dsb sy; sev" ::: "memory");
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
                    /* RUNNING is an acknowledgement from the target, never
                     * an owner-side recovery value. Re-signal the event and
                     * keep waiting until the saved frame has been restored. */
                    __asm__ __volatile__("dsb sy; sev" ::: "memory");
                    HalRequestDebugWakeIpi(TargetPrcb->SetMember);
                    Spins = 0;
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
    KeMemoryBarrier();
    __asm__ __volatile__("dsb sy; sev" ::: "memory");
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
            if (InterlockedCompareExchange((PLONG)&CurrentPrcb->IpiFrozen, IPI_FROZEN_STATE_SAVING, IPI_FROZEN_STATE_TARGET_FREEZE) ==
                IPI_FROZEN_STATE_TARGET_FREEZE)
            {
                /* A simultaneous debugger entry can make this CPU both an
                 * owner contender and another owner's freeze target. There
                 * is no synthetic IRQ frame in this path, so capture a real
                 * inspection context before publishing FROZEN. */
                RtlCaptureContext(&CurrentPrcb->ProcessorState.ContextFrame);
                KiSaveProcessorControlState(&CurrentPrcb->ProcessorState);
                KeMemoryBarrier();
                InterlockedExchange((PLONG)&CurrentPrcb->IpiFrozen, IPI_FROZEN_STATE_FROZEN);

                for (;;)
                {
                    ULONG State = CurrentPrcb->IpiFrozen & ~IPI_FROZEN_FLAG_ACTIVE;

                    if (State == IPI_FROZEN_STATE_THAW || State == IPI_FROZEN_STATE_RUNNING)
                        break;

                    if (CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE)
                    {
                        KCONTINUE_STATUS ContinueStatus;

                        ContinueStatus = KdReportProcessorChange();
                        CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_FROZEN;
                        if ((ContinueStatus == ContinueSuccess) &&
                            (KiFreezeOwner != NULL))
                        {
                            KiFreezeOwner->IpiFrozen = IPI_FROZEN_STATE_THAW;
                        }
                    }

                    YieldProcessor();
                    KeMemoryBarrier();
                }
                KiRestoreProcessorControlState(&CurrentPrcb->ProcessorState);
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
        BOOLEAN Requested = FALSE;

        if ((TargetPrcb == NULL) ||
            (TargetPrcb == CurrentPrcb) ||
            !(KiFreezeTargetSet & TargetPrcb->SetMember))
        {
            continue;
        }

        RtlZeroMemory(&TargetPrcb->ProcessorState.ContextFrame, sizeof(TargetPrcb->ProcessorState.ContextFrame));
        KeMemoryBarrier();

        for (;;)
        {
            if (InterlockedCompareExchange((PLONG)&TargetPrcb->IpiFrozen, IPI_FROZEN_STATE_TARGET_FREEZE, IPI_FROZEN_STATE_RUNNING) ==
                IPI_FROZEN_STATE_RUNNING)
            {
                Requested = TRUE;
                break;
            }

            YieldProcessor();
            KeMemoryBarrier();
            if (++Spins > 2000000UL)
            {
                break;
            }
        }

        if (!Requested)
        {
            KiFreezeTargetSet &= ~TargetPrcb->SetMember;
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
