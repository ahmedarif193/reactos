/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/time.c
 * PURPOSE:         Implements timebase functionality
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LONG KiTickOffset;
ULONG KeTimeAdjustment;
BOOLEAN KiTimeAdjustmentEnabled = FALSE;

/* FUNCTIONS ******************************************************************/

#if (defined(_M_AMD64) || defined(_M_ARM64)) && (NTDDI_VERSION >= NTDDI_LONGHORN)
VOID
NTAPI
KiChargeThreadCycleTime(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread)
{
    ULONGLONG CurrentCycles, StartCycles, Delta;

#if defined(_M_ARM64)
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(CurrentCycles));
#else
    CurrentCycles = __rdtsc();
#endif
    StartCycles = Prcb->StartCycles;
    Prcb->StartCycles = CurrentCycles;

    /* Reset the baseline after an uninitialized or backwards sample. */
    if ((StartCycles == 0) || (CurrentCycles < StartCycles))
    {
#if defined(_M_AMD64)
        if (SmpDbgEnabled)
            SmpDbgCycleCharge(Prcb->Number, 0, TRUE);
#endif
        return;
    }

    Delta = CurrentCycles - StartCycles;
    Thread->CycleTime += Delta;
    if (Thread->Process != NULL)
        InterlockedExchangeAdd64((PLONG64)&Thread->Process->CycleTime, (LONG64)Delta);
    Prcb->CycleTime += Delta;
#if defined(_M_AMD64)
    if (SmpDbgEnabled)
        SmpDbgCycleCharge(Prcb->Number, Delta, FALSE);
#endif
}
#endif

FORCEINLINE
VOID
KiCheckForTimerExpiration(
    PKPRCB Prcb,
    PKTRAP_FRAME TrapFrame,
    LARGE_INTEGER InterruptTime)
{
    ULONG Hand;

    /* Check for timer expiration */
    Hand = KeTickCount.LowPart & (TIMER_TABLE_SIZE - 1);
    if (KiTimerTableListHead[Hand].Time.QuadPart <= (ULONG64)InterruptTime.QuadPart)
    {
        /* Check if we are already doing expiration */
        if (!Prcb->TimerRequest)
        {
            /* Request a DPC to handle this */
            Prcb->TimerRequest = (ULONG_PTR)TrapFrame;
            Prcb->TimerHand = Hand;
            HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
        }
    }
}

VOID
FASTCALL
KeUpdateSystemTime(IN PKTRAP_FRAME TrapFrame,
                   IN ULONG Increment,
                   IN KIRQL Irql)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    LARGE_INTEGER CurrentTime, InterruptTime;
    LONG OldTickOffset;

    /* Check if this tick is being skipped */
    if (Prcb->SkipTick)
    {
        /* Handle it next time */
        Prcb->SkipTick = FALSE;

        /* Increase interrupt count and end the interrupt */
        Prcb->InterruptCount++;

#ifdef _M_IX86 // x86 optimization
        KiEndInterrupt(Irql, TrapFrame);
#endif

        /* Note: non-x86 return back to the caller! */
        return;
    }

    /* Add the increment time to the shared data */
    InterruptTime.QuadPart = *(ULONGLONG*)&SharedUserData->InterruptTime;
    InterruptTime.QuadPart += Increment;
    KiWriteSystemTime(&MmWriteableSharedUserData->InterruptTime, InterruptTime);
#if DBG && defined(KDBG)
    KdpLogWatchdogCheck(InterruptTime.QuadPart);
#endif

    /* Check for timer expiration */
    KiCheckForTimerExpiration(Prcb, TrapFrame, InterruptTime);

    /* Update the tick offset */
    OldTickOffset = InterlockedExchangeAdd(&KiTickOffset, -(LONG)Increment);

    /* If the debugger is enabled, check for break-in request */
    if (KdDebuggerEnabled && KdPollBreakIn())
    {
        /* Break-in requested! */
        DbgBreakPointWithStatus(DBG_STATUS_CONTROL_C);
    }

    /* Check for full tick */
    if (OldTickOffset <= (LONG)Increment)
    {
        /* Update the system time */
        CurrentTime.QuadPart = *(ULONGLONG*)&SharedUserData->SystemTime;
        CurrentTime.QuadPart += KeTimeAdjustment;
        KiWriteSystemTime(&MmWriteableSharedUserData->SystemTime, CurrentTime);

        /* Update the tick count */
        CurrentTime.QuadPart = (*(ULONGLONG*)&KeTickCount) + 1;
        KiWriteSystemTime(&KeTickCount, CurrentTime);

        /* Update it in the shared user data */
        KiWriteSystemTime(&MmWriteableSharedUserData->TickCount, CurrentTime);

        /* Check for expiration with the new tick count as well */
        KiCheckForTimerExpiration(Prcb, TrapFrame, InterruptTime);

        /* Reset the tick offset */
        KiTickOffset += KeMaximumIncrement;

        /* Update processor/thread runtime */
        KeUpdateRunTime(TrapFrame, Irql);
    }
    else
    {
        /* Increase interrupt count only */
        Prcb->InterruptCount++;
    }

#ifdef _M_IX86 // x86 optimization
    /* Disable interrupts and end the interrupt */
    KiEndInterrupt(Irql, TrapFrame);
#endif
}

VOID
NTAPI
KeUpdateRunTime(IN PKTRAP_FRAME TrapFrame,
                IN KIRQL Irql)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKPRCB Prcb = KeGetCurrentPrcb();

#if defined(_M_AMD64)
    if (SmpDbgEnabled)
        SmpDbgRuntimeTick(Prcb->Number);
#endif

    /* Check if this tick is being skipped */
    if (Prcb->SkipTick)
    {
        /* Handle it next time */
        Prcb->SkipTick = FALSE;
        return;
    }

#if (defined(_M_AMD64) || defined(_M_ARM64)) && (NTDDI_VERSION >= NTDDI_LONGHORN)
    /* Charge measured execution against the absolute quantum target. */
    KiChargeThreadCycleTime(Prcb, Thread);
#endif

    /* Increase interrupt count */
    Prcb->InterruptCount++;

    /* Check if we came from user mode */
#if !defined(_M_ARM) && !defined(_M_ARM64)
    if (KiUserTrap(TrapFrame) || (TrapFrame->EFlags & EFLAGS_V86_MASK))
#else
    if (TrapFrame->PreviousMode == UserMode)
#endif
    {
        /* Increase thread user time */
        Prcb->UserTime++;
        Thread->UserTime++;
    }
    else
    {
        /* See if we were in an ISR */
        Prcb->KernelTime++;
        if (Irql > DISPATCH_LEVEL)
        {
            /* Handle that */
            Prcb->InterruptTime++;
        }
        else if ((Irql < DISPATCH_LEVEL) || !(Prcb->DpcRoutineActive))
        {
            /* Handle being in kernel mode */
            Thread->KernelTime++;
        }
        else
        {
            /* Handle being in a DPC */
            Prcb->DpcTime++;

#if DBG && (NTDDI_VERSION < NTDDI_LONGHORN)
            /* Update the DPC time (DebugDpcTime removed at Vista+) */
            Prcb->DebugDpcTime++;

            /* Check if we have timed out */
            if (Prcb->DebugDpcTime == KiDPCTimeout)
            {
                /* We did! */
                DbgPrint("*** DPC routine > 1 sec --- This is not a break in KeUpdateSystemTime\n");

                /* Break if debugger is enabled */
                if (KdDebuggerEnabled) DbgBreakPoint();

                /* Clear state */
                Prcb->DebugDpcTime = 0;
            }
#endif
        }
    }

    /* Update DPC rates */
    Prcb->DpcRequestRate = ((Prcb->DpcData[0].DpcCount - Prcb->DpcLastCount) +
                            Prcb->DpcRequestRate) >> 1;
    Prcb->DpcLastCount = Prcb->DpcData[0].DpcCount;

    /* Check if the queue is large enough */
    if ((Prcb->DpcData[0].DpcQueueDepth) && !(Prcb->DpcRoutineActive))
    {
        /* Request a DPC */
        Prcb->AdjustDpcThreshold = KiAdjustDpcThreshold;
        HalRequestSoftwareInterrupt(DISPATCH_LEVEL);

        /* Fix the maximum queue depth */
        if ((Prcb->DpcRequestRate < KiIdealDpcRate) &&
            (Prcb->MaximumDpcQueueDepth > 1))
        {
            /* Make it smaller */
            Prcb->MaximumDpcQueueDepth--;
        }
    }
    else
    {
        /* Check if we've reached the adjustment limit */
        if (!(--Prcb->AdjustDpcThreshold))
        {
            /* Reset it, and check the queue maximum */
            Prcb->AdjustDpcThreshold = KiAdjustDpcThreshold;
            if (KiMaximumDpcQueueDepth != Prcb->MaximumDpcQueueDepth)
            {
                /* Increase it */
                Prcb->MaximumDpcQueueDepth++;
            }
        }
    }

#if !defined(_M_AMD64) || (NTDDI_VERSION < NTDDI_LONGHORN)
    KiDecrementThreadQuantum(Thread, CLOCK_QUANTUM_DECREMENT);
#endif

    /* Check if the time expired */
    if (KiIsThreadQuantumExpired(Thread) && (Thread != Prcb->IdleThread))
    {
#if defined(_M_AMD64)
        if (SmpDbgEnabled)
            SmpDbgQuantumRequest(Prcb->Number);
#endif

        /* Schedule a quantum end */
        Prcb->QuantumEnd = 1;
        HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }
}
