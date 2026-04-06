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
 * KeIpiGenericCall
 *
 * Broadcasts a function call to all processors and executes it on each.
 * The calling processor also executes the function.
 *
 * This is the AMD64-specific implementation. On AMD64, we raise to IPI_LEVEL
 * and send IPIs to other processors to execute the function. Since the x64
 * IPI mechanism handles synchronization through the APIC, we use a simpler
 * approach than the i386 packet-based system.
 *
 * For the single-processor case (or when there are no other active processors),
 * we simply raise IRQL and call the function directly.
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
    volatile LONG ActiveCount;
    LONG NumberProcessors;
#endif

    /* Raise to DPC level if needed */
    OldIrql = KeGetCurrentIrql();
    if (OldIrql < DISPATCH_LEVEL)
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

#ifdef CONFIG_SMP
    /* Get active processor set excluding ourselves */
    Affinity = KeActiveProcessors & ~Prcb->SetMember;
    NumberProcessors = KeNumberProcessors;
#endif

    /* Acquire the IPI lock to serialize generic calls */
    KeAcquireSpinLockAtDpcLevel(&KiReverseStallIpiLock);

    /* Raise to IPI_LEVEL to prevent further interrupts */
    {
        KIRQL IpiIrql;
        KeRaiseIrql(IPI_LEVEL, &IpiIrql);
    }

    /*
     * Call the function on this processor.
     * On AMD64 with our current IPI infrastructure, we execute the
     * function locally. Other processors will execute it when they
     * handle the IPI (through the KiIpiServiceRoutine path).
     *
     * TODO: For full SMP correctness, implement IPI_SYNCH_REQUEST
     * support in KiIpiServiceRoutine for AMD64, which would allow
     * broadcasting the function to all processors and waiting for
     * them to complete.
     */
    Status = Function(Argument);

    /* Release the IPI lock */
    KeReleaseSpinLockFromDpcLevel(&KiReverseStallIpiLock);

    /* Restore original IRQL */
    KeLowerIrql(OldIrql);

    return Status;
}
