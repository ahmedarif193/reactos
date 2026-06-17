/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/ipi.c
 * PURPOSE:         Inter-processor interrupt stubs for ARM64
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>

#ifdef CONFIG_SMP
static struct
{
    PKIPI_BROADCAST_WORKER volatile Function;
    volatile ULONG_PTR Argument;
    volatile LONG TargetCount;
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
    KAFFINITY ProcessorMask;
    ULONG Index;

    if (TargetSet == 0)
        return;

    for (Index = 0, ProcessorMask = 1; Index < KeNumberProcessors; Index++, ProcessorMask <<= 1)
    {
        if (TargetSet & ProcessorMask)
        {
            PKPRCB Prcb = KiProcessorBlock[Index];
            if (Prcb != NULL)
            {
                InterlockedBitTestAndSet((PLONG)&Prcb->RequestSummary, IpiRequest);
            }
        }
    }

    HalRequestIpi(TargetSet);
#else
    UNREFERENCED_PARAMETER(TargetSet);
    UNREFERENCED_PARAMETER(IpiRequest);
#endif
}

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

        /* SMP boot diagnostics (/SMPDIAG): trace each serviced IPI. */
        if (SmpDbgEnabled)
            DbgPrint("SMP4DBG ipi-service cpu=%lu request=0x%Ix dpcint=%u next=%p\n",
                     KeGetCurrentProcessorNumber(), (ULONG_PTR)Prcb->RequestSummary,
                     Prcb->DpcInterruptRequested, Prcb->NextThread);

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
            Prcb->DpcInterruptRequested = TRUE;
            HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
        }

        if (InterlockedBitTestAndReset((PLONG)&Prcb->RequestSummary, IPI_SYNCH_REQUEST))
        {
            PKIPI_BROADCAST_WORKER Function = KiArm64IpiPacket.Function;
            ULONG_PTR Argument = KiArm64IpiPacket.Argument;

            if (Function != NULL)
            {
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

    OldIrql = KfRaiseIrql(SYNCH_LEVEL);

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
    InterlockedExchange(&KiArm64IpiPacket.TargetCount, TargetCount);

    if (Targets != 0)
    {
        KiIpiSend(Targets, IPI_SYNCH_REQUEST);
    }

    Result = Function(Argument);

    while (KiArm64IpiPacket.TargetCount != 0)
    {
        YieldProcessor();
        KeMemoryBarrier();
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
