/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Processor freeze support for x64
 * COPYRIGHT:   Copyright 2023-2024 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/*

 IpiFrozen state graph (based on Windows behavior):

    +-----------------+     Freeze request      +-----------------+
    | RUNNING         |------------------------>| TARGET_FREEZE   |
    +-----------------+<---------               +-----------------+
            |^                  | Resume                |
     Freeze || Thaw        +-----------+ Thaw request   | Freeze IPI
            v|             | THAW      |<-----------\   v
    +-----------------+    +-----------+        +-----------------+
    | OWNER + ACTIVE  |         ^               | FROZEN          |
    +-----------------+         |               +-----------------+
            ^                   |                       ^
            | Kd proc switch    |                       | Kd proc switch
            v                   |                       v
    +-----------------+         |               +-----------------+
    | OWNER           |---------+               | FROZEN + ACTIVE |
    +-----------------+ Thaw request            +-----------------+

 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* NOT INCLUDES ANYMORE ******************************************************/

PKPRCB KiFreezeOwner;

/* FUNCTIONS *****************************************************************/

BOOLEAN
KiProcessorFreezeHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();

    /* Make sure this is a freeze request */
    if (CurrentPrcb->IpiFrozen != IPI_FROZEN_STATE_TARGET_FREEZE)
    {
        /* Not a freeze request, return FALSE to signal it is unhandled */
        return FALSE;
    }

    /* We are frozen now */
    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_FROZEN;

    /* Save the processor state */
    KiSaveProcessorState(TrapFrame, ExceptionFrame);

    /* Wait for the freeze owner to release us */
    while (CurrentPrcb->IpiFrozen != IPI_FROZEN_STATE_THAW)
    {
        /* Check for Kd processor switch */
        if (CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE)
        {
            KCONTINUE_STATUS ContinueStatus;

            /* Enter the debugger */
            ContinueStatus = KdReportProcessorChange();

            /* Set the state back to frozen */
            CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_FROZEN;

            /* If the status is ContinueSuccess, we need to release the freeze owner */
            if (ContinueStatus == ContinueSuccess)
            {
                /* Release the freeze owner */
                KiFreezeOwner->IpiFrozen = IPI_FROZEN_STATE_THAW;
            }
        }

        YieldProcessor();
        KeMemoryBarrier();
    }

    /* Restore the processor state */
    KiRestoreProcessorState(TrapFrame, ExceptionFrame);

    /* We are running again now */
    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_RUNNING;

    /* Return TRUE to signal that we handled the freeze */
    return TRUE;
}

VOID
NTAPI
KxFreezeExecution(
    VOID)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();

    /* Avoid blocking on recursive debug action */
    if (KiFreezeOwner == CurrentPrcb)
    {
        return;
    }

    /* Try to acquire the freeze owner */
    while (InterlockedCompareExchangePointer((volatile PVOID *)&KiFreezeOwner, CurrentPrcb, NULL))
    {
        /* Someone else was faster. We expect an NMI to freeze any time.
           Spin here until the freeze owner is available. */
        while (KiFreezeOwner != NULL)
        {
            YieldProcessor();
            KeMemoryBarrier();
        }
    }

    /* We are the owner now and active */
    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE;

    /*
     * Use KeActiveProcessors (not KeNumberProcessors) to determine targets.
     * During AP startup, KeNumberProcessors is incremented before the new AP
     * joins KeActiveProcessors.  Targeting a CPU that is registered but not
     * yet active would set TARGET_FREEZE without sending an NMI, causing
     * the wait loop below to spin forever.
     */
    {
        KAFFINITY TargetSet = KeActiveProcessors & ~CurrentPrcb->SetMember;
        KAFFINITY RemainingSet;
        ULONG ProcessorIndex;

        /* Set TARGET_FREEZE on each active target */
        RemainingSet = TargetSet;
        while (RemainingSet != 0)
        {
            BitScanForwardAffinity(&ProcessorIndex, RemainingSet);
            RemainingSet &= ~AFFINITY_MASK(ProcessorIndex);
            ASSERT(KiProcessorBlock[ProcessorIndex]->IpiFrozen == IPI_FROZEN_STATE_RUNNING);
            KiProcessorBlock[ProcessorIndex]->IpiFrozen = IPI_FROZEN_STATE_TARGET_FREEZE;
        }

        /* Send the freeze NMI to active targets */
        KiIpiSend(TargetSet, IPI_FREEZE);

        /* Wait for all active targets to reach FROZEN */
        RemainingSet = TargetSet;
        while (RemainingSet != 0)
        {
            BitScanForwardAffinity(&ProcessorIndex, RemainingSet);
            RemainingSet &= ~AFFINITY_MASK(ProcessorIndex);
            while (KiProcessorBlock[ProcessorIndex]->IpiFrozen != IPI_FROZEN_STATE_FROZEN)
            {
                YieldProcessor();
                KeMemoryBarrier();
            }
        }
    }

    /* All active targets are frozen, we can continue */
}

VOID
NTAPI
KxThawExecution(
    VOID)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    KAFFINITY TargetSet = KeActiveProcessors & ~CurrentPrcb->SetMember;
    KAFFINITY RemainingSet;
    ULONG ProcessorIndex;

    ASSERT(CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE);

    /* Thaw all active targets */
    RemainingSet = TargetSet;
    while (RemainingSet != 0)
    {
        BitScanForwardAffinity(&ProcessorIndex, RemainingSet);
        RemainingSet &= ~AFFINITY_MASK(ProcessorIndex);
        ASSERT(KiProcessorBlock[ProcessorIndex]->IpiFrozen == IPI_FROZEN_STATE_FROZEN);
        KiProcessorBlock[ProcessorIndex]->IpiFrozen = IPI_FROZEN_STATE_THAW;
    }

    /* Wait for all active targets to resume */
    RemainingSet = TargetSet;
    while (RemainingSet != 0)
    {
        BitScanForwardAffinity(&ProcessorIndex, RemainingSet);
        RemainingSet &= ~AFFINITY_MASK(ProcessorIndex);
        while (KiProcessorBlock[ProcessorIndex]->IpiFrozen != IPI_FROZEN_STATE_RUNNING)
        {
            YieldProcessor();
            KeMemoryBarrier();
        }
    }

    /* We are running again now */
    CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_RUNNING;

    /* Release the freeze owner */
    InterlockedExchangePointer((volatile PVOID *)&KiFreezeOwner, NULL);
}

KCONTINUE_STATUS
NTAPI
KxSwitchKdProcessor(
    _In_ ULONG ProcessorIndex)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    PKPRCB TargetPrcb;

    /* Make sure that the processor index is valid */
    ASSERT(ProcessorIndex < KeNumberProcessors);

    /* We are no longer active */
    ASSERT(CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE);
    CurrentPrcb->IpiFrozen &= ~IPI_FROZEN_FLAG_ACTIVE;

    /* Inform the target processor that it's his turn now */
    TargetPrcb = KiProcessorBlock[ProcessorIndex];
    TargetPrcb->IpiFrozen |= IPI_FROZEN_FLAG_ACTIVE;

    /* If we are not the freeze owner, we return back to the freeze loop */
    if (KiFreezeOwner != CurrentPrcb)
    {
        return ContinueNextProcessor;
    }

    /* Loop until it's our turn again */
    while (CurrentPrcb->IpiFrozen == IPI_FROZEN_STATE_OWNER)
    {
        YieldProcessor();
        KeMemoryBarrier();
    }

    /* Check if we have been thawed */
    if (CurrentPrcb->IpiFrozen == IPI_FROZEN_STATE_THAW)
    {
        /* Another CPU has completed, we can leave the debugger now */
        KdpDprintf("[%u] KxSwitchKdProcessor: ContinueSuccess\n", KeGetCurrentProcessorNumber());
        CurrentPrcb->IpiFrozen = IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE;
        return ContinueSuccess;
    }

    /* We have been reselected, return to Kd to continue in the debugger */
    ASSERT(CurrentPrcb->IpiFrozen == (IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE));

    return ContinueProcessorReselected;
}
