/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/ipi.c
 * PURPOSE:         Inter-processor interrupt stubs for ARM64
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>

#ifdef CONFIG_SMP
static struct
{
    PKIPI_BROADCAST_WORKER volatile Function;
    volatile ULONG_PTR Argument;
    volatile LONG TargetCount;   /* remote targets still running the function   */
    volatile LONG Arrived;       /* remote targets parked at the rendezvous      */
    volatile LONG Release;       /* initiator sets 1 to let parked targets run   */
    volatile LONG Busy;
} KiArm64IpiPacket;
#endif

VOID
NTAPI
KiIpiGenericCallTarget(
    _In_ PKIPI_CONTEXT PacketContext,
    _In_ PVOID BroadcastFunction,
    _In_ PVOID Argument,
    _In_ PVOID Count)
{
    UNREFERENCED_PARAMETER(PacketContext);
    UNREFERENCED_PARAMETER(Count);

    if (BroadcastFunction != NULL)
    {
        ((PKIPI_BROADCAST_WORKER)BroadcastFunction)((ULONG_PTR)Argument);
    }
}

VOID
FASTCALL
KiIpiSend(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG IpiRequest)
{
#ifdef CONFIG_SMP
    KAFFINITY RemainingSet;
    ULONG Index;
    PKPRCB Prcb;

    if (TargetSet == 0)
        return;

    /* Iterate only the set bits; the common case is a single-target IPI. */
    for (RemainingSet = TargetSet; RemainingSet != 0; RemainingSet &= RemainingSet - 1)
    {
        BitScanForwardAffinity(&Index, RemainingSet);
        if (Index >= (ULONG)KeNumberProcessors)
        {
            break;
        }

        Prcb = KiProcessorBlock[Index];
        if (Prcb != NULL)
        {
            InterlockedBitTestAndSet((PLONG)&Prcb->RequestSummary, IpiRequest);
        }
    }

    HalRequestIpi(TargetSet);
#else
    UNREFERENCED_PARAMETER(TargetSet);
    UNREFERENCED_PARAMETER(IpiRequest);
#endif
}

/*
 * The KPRCB-packet IPI protocol (KiIpiSendPacket / KiIpiSignalPacketDone /
 * KiIpiSignalPacketDoneAndStall) is unused on ARM64: the generic ke/ipi.c that
 * drives it is excluded from the ARM64 build (ntos.cmake) in favour of this
 * file's self-contained KeIpiGenericCall. These remain only to satisfy the
 * internal prototypes. If a future shared path ever calls them, the warning
 * below makes the otherwise-silent no-op visible instead of dropping the work.
 */
VOID
NTAPI
KiIpiSendPacket(
    _In_ KAFFINITY TargetSet,
    _In_ PKIPI_WORKER WorkerFunction,
    _In_ PKIPI_BROADCAST_WORKER BroadcastFunction,
    _In_ ULONG_PTR Context,
    _Inout_ PULONG Count)
{
    UNREFERENCED_PARAMETER(TargetSet);
    UNREFERENCED_PARAMETER(WorkerFunction);
    UNREFERENCED_PARAMETER(BroadcastFunction);
    UNREFERENCED_PARAMETER(Context);

    DbgPrint("[arm64] KiIpiSendPacket called but unimplemented on ARM64; use "
             "KeIpiGenericCall instead (packet IPI work dropped)\n");

    if (Count != NULL)
    {
        *Count = 1;
    }
}

VOID
FASTCALL
KiIpiSignalPacketDone(
    _In_ PKIPI_CONTEXT PacketContext)
{
    UNREFERENCED_PARAMETER(PacketContext);
}

VOID
FASTCALL
KiIpiSignalPacketDoneAndStall(
    _In_ PKIPI_CONTEXT PacketContext,
    _Inout_ volatile PULONG ReverseStall)
{
    UNREFERENCED_PARAMETER(PacketContext);

    if (ReverseStall != NULL)
    {
        *ReverseStall = 0;
    }
}

BOOLEAN
NTAPI
KiIpiServiceRoutine(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
#ifdef CONFIG_SMP
    {
        PKPRCB Prcb = KeGetCurrentPrcb();

        /* SMP boot diagnostics: silent per-CPU IPI-service counter (no print:
         * a serial DbgPrint here perturbs the very timing race we are probing). */
        SmpDbgIpi(KeGetCurrentProcessorNumber());

        /*
         * Check for freeze IPI FIRST, before any other processing.
         *
         * The freeze mechanism uses IpiFrozen as a STATE value (not bit flags).
         * KxFreezeExecution sets IpiFrozen = IPI_FROZEN_STATE_TARGET_FREEZE
         * on target CPUs, then sends a raw SGI via HalRequestIpi.
         *
         * If IpiFrozen indicates a freeze request, enter the freeze handler
         * which saves state and spins until the debugger releases us.
         */
        if (Prcb->IpiFrozen == IPI_FROZEN_STATE_TARGET_FREEZE)
        {
            KiProcessorFreezeHandler(TrapFrame, ExceptionFrame);
        }

        if (InterlockedBitTestAndReset((PLONG)&Prcb->RequestSummary, IPI_APC))
        {
            HalRequestSoftwareInterrupt(APC_LEVEL);
        }

        if (InterlockedBitTestAndReset((PLONG)&Prcb->RequestSummary, IPI_DPC))
        {
            SmpDbgRemoteDpc(Prcb->Number, SMPDBG_SOURCE_UNKNOWN);
            Prcb->DpcInterruptRequested = TRUE;
            HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
        }

        if (InterlockedBitTestAndReset((PLONG)&Prcb->RequestSummary, IPI_SYNCH_REQUEST))
        {
            PKIPI_BROADCAST_WORKER Function = KiArm64IpiPacket.Function;
            ULONG_PTR Argument = KiArm64IpiPacket.Argument;

            SmpDbgGenericCallIpi(Prcb->Number);
            if (Function != NULL)
            {
                /*
                 * Rendezvous with the initiator (KeIpiGenericCall): announce
                 * arrival, then spin at IPI_LEVEL until the initiator confirms
                 * every target is parked and signals Release. This guarantees
                 * the whole machine is quiesced while the broadcast routine
                 * runs, which is the contract callers rely on.
                 */
                InterlockedIncrement(&KiArm64IpiPacket.Arrived);

                while (KiArm64IpiPacket.Release == 0)
                {
                    YieldProcessor();
                    KeMemoryBarrier();
                }

                Function(Argument);
                InterlockedDecrement(&KiArm64IpiPacket.TargetCount);
            }
        }
    }
#else
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
#endif
    return TRUE;
}

ULONG_PTR
NTAPI
KeIpiGenericCall(
    _In_ PKIPI_BROADCAST_WORKER Function,
    _In_ ULONG_PTR Argument)
{
#ifdef CONFIG_SMP
    KIRQL OldIrql;
    ULONG_PTR Result;
    KAFFINITY Targets;
    LONG TargetCount;

    if (Function == NULL)
    {
        return 0;
    }

    /*
     * Raise to IPI_LEVEL (not SYNCH_LEVEL) so the initiator cannot be preempted
     * by clock/DPC interrupts while it owns the rendezvous and runs the
     * broadcast routine. The debugger freeze SGI sits above IPI_LEVEL and is
     * still deliverable.
     */
    OldIrql = KfRaiseIrql(IPI_LEVEL);

    while (InterlockedCompareExchange(&KiArm64IpiPacket.Busy, 1, 0) != 0)
    {
        YieldProcessor();
        KeMemoryBarrier();
    }

    Targets = KeActiveProcessors & ~KeGetCurrentPrcb()->SetMember;
    TargetCount = 0;
    for (KAFFINITY Bit = Targets; Bit != 0; Bit &= (Bit - 1))
    {
        TargetCount++;
    }

    KiArm64IpiPacket.Function = Function;
    KiArm64IpiPacket.Argument = Argument;
    KiArm64IpiPacket.Arrived = 0;
    KiArm64IpiPacket.Release = 0;
    InterlockedExchange(&KiArm64IpiPacket.TargetCount, TargetCount);

    if (Targets != 0)
    {
        KiIpiSend(Targets, IPI_SYNCH_REQUEST);
    }

    /*
     * Phase 1 - quiesce: wait until every target has parked at the rendezvous
     * (KiIpiServiceRoutine) before touching anything. While we spin here the
     * targets are spinning there, so the machine is fully quiesced.
     */
    {
        ULONG_PTR SpinCount = 0;

        while (KiArm64IpiPacket.Arrived != TargetCount)
        {
            YieldProcessor();
            KeMemoryBarrier();

            if (SmpDbgEnabled && ((++SpinCount & 0x3FFFFFF) == 0))
            {
                DbgPrint("SMP4DBG KeIpiGenericCall quiesce stuck: cpu=%lu arrived=%ld/%ld targets=0x%Ix\n",
                         KeGetCurrentProcessorNumber(),
                         KiArm64IpiPacket.Arrived, TargetCount, (ULONG_PTR)Targets);
            }
        }
    }

    /*
     * Phase 2 - release the parked targets, THEN run the routine locally so all
     * CPUs execute it concurrently. This is required for barrier-style callbacks
     * (e.g. PciIpiGenericCallHandler) that spin until every processor has entered
     * the callback: if the initiator ran the callback before releasing the
     * targets, it would wait inside the callback for CPUs that are still blocked
     * on Release == 0 -> deadlock. The phase-1 quiesce above already guarantees
     * every target is parked, so releasing first does not lose the rendezvous.
     */
    InterlockedExchange(&KiArm64IpiPacket.Release, 1);

    Result = Function(Argument);

    {
        ULONG_PTR SpinCount = 0;

        while (KiArm64IpiPacket.TargetCount != 0)
        {
            YieldProcessor();
            KeMemoryBarrier();

            /*
             * Watchdog: a target wedged at/above IPI_LEVEL (or not servicing
             * SGIs) never drains the packet, which would hang here forever. The
             * rendezvous contract still requires every target to run the
             * function, so we keep waiting - but surface the stuck state under
             * /SMPDIAG instead of spinning silently.
             */
            if (SmpDbgEnabled && ((++SpinCount & 0x3FFFFFF) == 0))
            {
                DbgPrint("SMP4DBG KeIpiGenericCall stuck: cpu=%lu remaining=%ld targets=0x%Ix\n",
                         KeGetCurrentProcessorNumber(),
                         KiArm64IpiPacket.TargetCount,
                         (ULONG_PTR)Targets);
            }
        }
    }

    KiArm64IpiPacket.Function = NULL;
    InterlockedExchange(&KiArm64IpiPacket.Busy, 0);

    KfLowerIrql(OldIrql);
    return Result;
#else
    if (Function != NULL)
    {
        return Function(Argument);
    }

    return 0;
#endif
}
