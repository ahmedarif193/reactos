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

/* GLOBALS ********************************************************************/

PKPRCB KiFreezeOwner;

/* (Optional) fallback if FORCEINLINE isn't defined by headers */
#ifndef FORCEINLINE
# if defined(_MSC_VER)
#  define FORCEINLINE __forceinline
# else
#  define FORCEINLINE __attribute__((always_inline)) static inline
# endif
#endif

/* HELPERS ********************************************************************/

FORCEINLINE
BOOLEAN
KiCpuInMask(_In_ ULONG Cpu, _In_ KAFFINITY Mask)
{
#if defined(_WIN64) || defined(_M_AMD64)
    return (Mask & ((KAFFINITY)1ULL << Cpu)) != 0;
#else
    return (Mask & ((KAFFINITY)1UL  << Cpu)) != 0;
#endif
}

FORCEINLINE
VOID
KiIpiFrozenSet(_Inout_ volatile LONG* State, _In_ LONG Value)
{
    InterlockedExchange(State, Value);
}

FORCEINLINE
VOID
KiIpiFrozenOr(_Inout_ volatile LONG* State, _In_ LONG Bits)
{
    InterlockedOr(State, Bits);
}

FORCEINLINE
VOID
KiIpiFrozenAnd(_Inout_ volatile LONG* State, _In_ LONG BitsMask)
{
    InterlockedAnd(State, BitsMask);
}

/* FUNCTIONS ******************************************************************/

BOOLEAN
KiProcessorFreezeHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    volatile LONG* pState = (volatile LONG*)&CurrentPrcb->IpiFrozen;

    /* Make sure this is a freeze request */
    if (CurrentPrcb->IpiFrozen != IPI_FROZEN_STATE_TARGET_FREEZE)
    {
        /* Not a freeze request, return FALSE to signal it is unhandled */
        return FALSE;
    }

    /* We are frozen now */
    KiIpiFrozenSet(pState, IPI_FROZEN_STATE_FROZEN);

    /* Save the processor state */
    KiSaveProcessorState(TrapFrame, ExceptionFrame);

    /* Wait for the freeze owner to release us */
    for (;;)
    {
        LONG s = CurrentPrcb->IpiFrozen;
        if (s == IPI_FROZEN_STATE_THAW)
        {
            break;
        }

        /* Check for Kd processor switch */
        if (s & IPI_FROZEN_FLAG_ACTIVE)
        {
            KCONTINUE_STATUS ContinueStatus;

            /* Enter the debugger */
            ContinueStatus = KdReportProcessorChange();

            /*
             * If the owner thawed us while inside KD, do NOT clobber THAW.
             * Attempt to transition (FROZEN|ACTIVE) -> FROZEN. If state
             * changed to THAW (or anything else), leave it alone.
             */
            (void)InterlockedCompareExchange(
                pState,
                IPI_FROZEN_STATE_FROZEN,
                IPI_FROZEN_STATE_FROZEN | IPI_FROZEN_FLAG_ACTIVE);

            /* If KD wants to continue, release the freeze owner */
            if (ContinueStatus == ContinueSuccess && KiFreezeOwner)
            {
                volatile LONG* pOwnerState = (volatile LONG*)&KiFreezeOwner->IpiFrozen;
                KiIpiFrozenSet(pOwnerState, IPI_FROZEN_STATE_THAW);
                KeMemoryBarrier(); /* ensure owner observes THAW promptly */
            }
        }

        YieldProcessor();
        KeMemoryBarrier();
    }

    /* Restore the processor state */
    KiRestoreProcessorState(TrapFrame, ExceptionFrame);

    /* We are running again now */
    KiIpiFrozenSet(pState, IPI_FROZEN_STATE_RUNNING);

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
    while (InterlockedCompareExchangePointer((void * volatile*)&KiFreezeOwner, CurrentPrcb, NULL))
    {
        /* Someone else was faster. We expect an IPI to freeze any time.
           Spin here until the freeze owner is available. */
        while (KiFreezeOwner != NULL)
        {
            YieldProcessor();
            KeMemoryBarrier();
        }
    }

    /* We are the owner now and active */
    KiIpiFrozenSet((volatile LONG*)&CurrentPrcb->IpiFrozen,
                   IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE);

    /* Build the target mask (all active except us) */
    KAFFINITY targetMask = KeActiveProcessors & ~CurrentPrcb->SetMember;

    /* Request each target to freeze */
    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        if (!KiCpuInMask(i, targetMask)) continue;

        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if (!TargetPrcb) continue;

        /* Only the active processor is allowed to change IpiFrozen */
        ASSERT(TargetPrcb->IpiFrozen == IPI_FROZEN_STATE_RUNNING);

        /* Request target to freeze */
        KiIpiFrozenSet((volatile LONG*)&TargetPrcb->IpiFrozen,
                       IPI_FROZEN_STATE_TARGET_FREEZE);
    }

    /* Make sure TARGET_FREEZE stores are visible before sending the IPI */
    KeMemoryBarrier();

    /* Send the freeze IPI */
    KiIpiSend(targetMask, IPI_FREEZE);

    /* Wait for all targets in the mask to be frozen */
    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        if (!KiCpuInMask(i, targetMask)) continue;

        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if (!TargetPrcb) continue;

        while (TargetPrcb->IpiFrozen != IPI_FROZEN_STATE_FROZEN)
        {
            YieldProcessor();
            KeMemoryBarrier();
        }
    }

    /* All targets are frozen, we can continue */
}

VOID
NTAPI
KxThawExecution(
    VOID)
{
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    ASSERT(CurrentPrcb->IpiFrozen & IPI_FROZEN_FLAG_ACTIVE);

    /* Compute the same target mask (all active except us) */
    KAFFINITY targetMask = KeActiveProcessors & ~CurrentPrcb->SetMember;

    /* Request each target to thaw */
    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        if (!KiCpuInMask(i, targetMask)) continue;

        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if (!TargetPrcb) continue;

        /* Make sure they are still frozen */
        ASSERT(TargetPrcb->IpiFrozen == IPI_FROZEN_STATE_FROZEN);

        /* Request target to thaw */
        KiIpiFrozenSet((volatile LONG*)&TargetPrcb->IpiFrozen,
                       IPI_FROZEN_STATE_THAW);
    }

    /* Ensure THAW stores are visible before we start polling */
    KeMemoryBarrier();

    /* Wait for all targets to be running again */
    for (ULONG i = 0; i < KeNumberProcessors; i++)
    {
        if (!KiCpuInMask(i, targetMask)) continue;

        PKPRCB TargetPrcb = KiProcessorBlock[i];
        if (!TargetPrcb) continue;

        while (TargetPrcb->IpiFrozen != IPI_FROZEN_STATE_RUNNING)
        {
            YieldProcessor();
            KeMemoryBarrier();
        }
    }

    /* We are running again now */
    KiIpiFrozenSet((volatile LONG*)&CurrentPrcb->IpiFrozen,
                   IPI_FROZEN_STATE_RUNNING);

    /* Release the freeze owner */
    InterlockedExchangePointer((void * volatile*)&KiFreezeOwner, NULL);
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
    KiIpiFrozenAnd((volatile LONG*)&CurrentPrcb->IpiFrozen, ~IPI_FROZEN_FLAG_ACTIVE);

    /* Inform the target processor that it's his turn now */
    TargetPrcb = KiProcessorBlock[ProcessorIndex];
    ASSERT(TargetPrcb != NULL);
    KiIpiFrozenOr((volatile LONG*)&TargetPrcb->IpiFrozen, IPI_FROZEN_FLAG_ACTIVE);

    /* If we are not the freeze owner, we return back to the freeze loop */
    if (KiFreezeOwner != CurrentPrcb)
    {
        return ContinueNextProcessor;
    }

    /* Loop until it's our turn again (OWNER | ACTIVE) or we get THAWed */
    for (;;)
    {
        LONG s = CurrentPrcb->IpiFrozen;
        if (s == IPI_FROZEN_STATE_THAW)
        {
            /* Another CPU has completed; we can leave the debugger now */
            KdpDprintf("[%u] KxSwitchKdProcessor: ContinueSuccess\n",
                       KeGetCurrentProcessorNumber());
            /* Mark ourselves active owner again for post-KD flow */
            KiIpiFrozenSet((volatile LONG*)&CurrentPrcb->IpiFrozen,
                           IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE);
            return ContinueSuccess;
        }

        if (s == (IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE))
        {
            /* We have been reselected, return to KD to continue */
            return ContinueProcessorReselected;
        }

        YieldProcessor();
        KeMemoryBarrier();
    }
}
