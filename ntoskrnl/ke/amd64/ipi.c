/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IPI code for x64
 * COPYRIGHT:   Copyright 2023 Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2025 ReactOS Team
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

extern KSPIN_LOCK KiReverseStallIpiLock;

/* FUNCTIONS *****************************************************************/

/*
 * KiIpiServiceRoutine (AMD64-specific)
 *
 * On AMD64, APC/DPC IPIs are delivered via separate vectors (APC_VECTOR,
 * DISPATCH_VECTOR), not via the IPI vector. The IPI vector (0xE1) is
 * used ONLY for packet-based IPIs (IPI_SYNCH_REQUEST).
 *
 * CRITICAL: On AMD64, IpiFrozen is reserved exclusively for the freeze
 * state machine (freeze.c). IPI request bits use RequestSummary instead.
 * This separation prevents conflicts between IPI packet delivery and
 * debugger freeze operations.
 */
BOOLEAN
NTAPI
KiIpiServiceRoutine(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
#ifdef CONFIG_SMP
    PKPRCB Prcb = KeGetCurrentPrcb();

    ASSERT(KeGetCurrentIrql() == IPI_LEVEL);

    /* Check for IPI_SYNCH_REQUEST in RequestSummary (NOT IpiFrozen) */
    if (InterlockedBitTestAndReset((volatile LONG *)&Prcb->RequestSummary, IPI_SYNCH_REQUEST))
    {
        PKPRCB Sender = (PKPRCB)Prcb->SignalDone;
        if (Sender)
        {
            volatile LONG *Count = (volatile LONG *)&Sender->CurrentPacket[1];
            volatile LONG *Sync = (volatile LONG *)&Sender->CurrentPacket[2];

            /* Decrement ready count */
            InterlockedDecrement(Count);

            /* If synchronize requested, wait until all targets are ready */
            if (*Sync)
            {
                while (*Count != 0)
                    YieldProcessor();
            }

            /* Call the worker routine */
            ((PKIPI_WORKER)(ULONG_PTR)Sender->WorkerRoutine)(
                Prcb->SignalDone,
                Sender->CurrentPacket[0],
                (PVOID)Count,
                (PVOID)Sync);

            /* Clear our bit in the sender's TargetSet */
            InterlockedAnd64((PLONG64)&Sender->TargetSet,
                             ~(LONG64)Prcb->SetMember);

            /* If synchronize, wait for all targets to complete */
            if (*Sync)
            {
                while (Sender->TargetSet != 0)
                    YieldProcessor();
            }

            /* Release the signal */
            InterlockedExchangePointer((volatile PVOID *)&Prcb->SignalDone, NULL);
        }
    }
#endif
    return TRUE;
}

/*
 * KiIpiSend
 *
 * Sends an IPI request to the specified processors.
 * On AMD64, different IPI types map to different interrupt delivery mechanisms:
 *   IPI_APC   -> Software interrupt at APC_LEVEL
 *   IPI_DPC   -> Software interrupt at DISPATCH_LEVEL
 *   IPI_FREEZE -> NMI (used for debugger freeze)
 */
VOID
FASTCALL
KiIpiSend(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG IpiRequest)
{
    if (IpiRequest == IPI_APC)
    {
        HalSendSoftwareInterrupt(TargetSet, APC_LEVEL);
    }
    else if (IpiRequest == IPI_DPC)
    {
        HalSendSoftwareInterrupt(TargetSet, DISPATCH_LEVEL);
    }
    else if (IpiRequest == IPI_FREEZE)
    {
        /* On AMD64 the freeze IPI is delivered as an NMI */
        HalSendNMI(TargetSet);
    }
    else
    {
        ASSERT(FALSE);
    }
}

/*
 * KiIpiGenericCallTarget
 *
 * Target-side handler for KeIpiGenericCall. Called via IPI packet on
 * each remote processor. Decrements the ready count, waits for the
 * caller to signal all targets ready, then executes the broadcast function.
 */
VOID
NTAPI
KiIpiGenericCallTarget(
    _In_ PKIPI_CONTEXT PacketContext,
    _In_ PVOID BroadcastFunction,
    _In_ PVOID Argument,
    _In_ PVOID Count)
{
    /* Decrement the count to signal we're ready */
    InterlockedDecrement((volatile LONG *)Count);

    /* Wait for the caller to signal us to proceed (count == 0) */
    while (*(volatile LONG *)Count != 0)
    {
        YieldProcessor();
    }

    /* Call the broadcast function */
    ((PKIPI_BROADCAST_WORKER)(ULONG_PTR)BroadcastFunction)((ULONG_PTR)Argument);
}

/*
 * KiIpiSendPacket
 *
 * Sends an IPI packet containing a function pointer and arguments to
 * each target processor. Sets up the packet in the sender's KPRCB,
 * signals each target via SignalDone + IPI_SYNCH_REQUEST, then sends
 * the hardware IPI.
 */
VOID
NTAPI
KiIpiSendPacket(
    _In_ KAFFINITY TargetProcessors,
    _In_ PKIPI_WORKER WorkerFunction,
    _In_ PKIPI_BROADCAST_WORKER BroadcastFunction,
    _In_ ULONG_PTR Context,
    _In_ PULONG Count)
{
#ifdef CONFIG_SMP
    PKPRCB CurrentPrcb = KeGetCurrentPrcb();
    PKPRCB TargetPrcb;
    ULONG i;
    KAFFINITY Remaining;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    /* Set up our packet data in the sender's PRCB */
    CurrentPrcb->TargetSet = TargetProcessors;
    CurrentPrcb->WorkerRoutine = (PVOID)WorkerFunction;
    CurrentPrcb->CurrentPacket[0] = (PVOID)BroadcastFunction;
    CurrentPrcb->CurrentPacket[1] = (PVOID)(ULONG_PTR)(*Count);
    CurrentPrcb->CurrentPacket[2] = (PVOID)Context;

    /* Signal each target processor */
    for (i = 0, Remaining = TargetProcessors; Remaining != 0; i++, Remaining >>= 1)
    {
        if (!(Remaining & 1))
            continue;

        TargetPrcb = KiProcessorBlock[i];

        /* Wait for target to finish any previous packet */
        while (InterlockedCompareExchangePointer(
                   (volatile PVOID *)&TargetPrcb->SignalDone,
                   CurrentPrcb, NULL) != NULL)
        {
            YieldProcessor();
        }

        /* Signal the target via RequestSummary (NOT IpiFrozen, which is
         * reserved exclusively for the freeze state machine on amd64) */
        InterlockedOr((volatile LONG *)&TargetPrcb->RequestSummary, IPI_SYNCH_REQUEST);
    }

    /* Send the hardware IPI to all targets */
    HalRequestIpi(TargetProcessors);
#endif
}

/*
 * KeIpiGenericCall
 *
 * Broadcasts a function call to all processors and executes it on each.
 * The calling processor also executes the function. All processors
 * synchronize before and after execution.
 */
ULONG_PTR
NTAPI
KeIpiGenericCall(
    _In_ PKIPI_BROADCAST_WORKER Function,
    _In_ ULONG_PTR Argument)
{
    ULONG_PTR Status;
    KIRQL OldIrql;
#ifdef CONFIG_SMP
    KAFFINITY Affinity;
    PKPRCB Prcb = KeGetCurrentPrcb();
    volatile LONG Count;
#endif

    /* Raise to DPC level if needed */
    OldIrql = KeGetCurrentIrql();
    if (OldIrql < DISPATCH_LEVEL)
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

#ifdef CONFIG_SMP
    /* Get active processor set excluding ourselves */
    Affinity = KeActiveProcessors & ~Prcb->SetMember;
#endif

    /* Acquire the IPI lock to serialize generic calls */
    KeAcquireSpinLockAtDpcLevel(&KiReverseStallIpiLock);

#ifdef CONFIG_SMP
    if (Affinity)
    {
        /* Set count to total processors (including self) */
        Count = KeNumberProcessors;

        /* Send the packet to all other processors */
        KiIpiSendPacket(Affinity,
                        (PKIPI_WORKER)(ULONG_PTR)KiIpiGenericCallTarget,
                        Function,
                        (ULONG_PTR)Argument,
                        (PULONG)&Count);

        /* Wait for all remote processors to check in */
        while (Count != 1)
        {
            YieldProcessor();
        }
    }
#endif

    /* Raise to IPI_LEVEL */
    {
        KIRQL IpiIrql;
        KeRaiseIrql(IPI_LEVEL, &IpiIrql);
    }

#ifdef CONFIG_SMP
    /* Signal all remote processors to proceed */
    if (Affinity)
        Count = 0;
#endif

    /* Execute the function locally */
    Status = Function(Argument);

#ifdef CONFIG_SMP
    /* Wait for all remote processors to finish */
    if (Affinity)
    {
        while (Prcb->TargetSet != 0)
        {
            YieldProcessor();
        }
    }
#endif

    /* Release the IPI lock */
    KeReleaseSpinLockFromDpcLevel(&KiReverseStallIpiLock);

    /* Restore original IRQL */
    KeLowerIrql(OldIrql);

    return Status;
}
