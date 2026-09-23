/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V interprocessor requests and broadcast rendezvous
 */

#include <ntoskrnl.h>

static struct
{
    PKIPI_BROADCAST_WORKER Function;
    ULONG_PTR Argument;
    volatile LONG Arriving;
    volatile LONG Remaining;
    volatile LONG Release;
    volatile LONG Busy;
} KiRiscvGenericCall;

VOID FASTCALL KiIpiSend(KAFFINITY Targets, ULONG Request)
{
    KAFFINITY Remaining = Targets & KeActiveProcessors;
    ULONG Number;
    Targets = Remaining;
    while (BitScanForwardAffinity(&Number, Remaining))
    {
        InterlockedBitTestAndSet(&KiProcessorBlock[Number]->RequestSummary, Request);
        Remaining &= ~AFFINITY_MASK(Number);
    }
    if (Targets) HalRequestIpi(Targets);
}

/* A target runs the broadcast worker at IPI_LEVEL once every target has
 * arrived, so the whole machine is quiesced while any worker runs. */
static
VOID
KiRiscvExecuteGenericCall(VOID)
{
    PKIPI_BROADCAST_WORKER Function;
    ULONG_PTR Argument;

    ASSERT(KeGetCurrentIrql() == IPI_LEVEL);
    KeMemoryBarrier();
    Function = KiRiscvGenericCall.Function;
    Argument = KiRiscvGenericCall.Argument;
    InterlockedDecrement(&KiRiscvGenericCall.Arriving);
    while (InterlockedCompareExchange(&KiRiscvGenericCall.Release, 0, 0) == 0)
        YieldProcessor();
    Function(Argument);
    InterlockedDecrement(&KiRiscvGenericCall.Remaining);
}

BOOLEAN NTAPI
KiIpiServiceRoutine(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ASSERT(KeGetCurrentIrql() == IPI_LEVEL);
    if (Prcb->IpiFrozen == IPI_FROZEN_STATE_TARGET_FREEZE)
        KiProcessorFreezeHandler(TrapFrame, ExceptionFrame);
    InterlockedBitTestAndReset(&Prcb->RequestSummary, IPI_FREEZE);
    if (InterlockedBitTestAndReset(&Prcb->RequestSummary, IPI_APC))
        HalRequestSoftwareInterrupt(APC_LEVEL);
    if (InterlockedBitTestAndReset(&Prcb->RequestSummary, IPI_DPC))
    {
        Prcb->DpcInterruptRequested = TRUE;
        HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }
    if (InterlockedBitTestAndReset(&Prcb->RequestSummary, IPI_SYNCH_REQUEST))
        KiRiscvExecuteGenericCall();
    return TRUE;
}

VOID NTAPI KiIpiProcessRequests(VOID)
{
    KiIpiServiceRoutine(NULL, NULL);
}

ULONG_PTR
NTAPI
KeIpiGenericCall(PKIPI_BROADCAST_WORKER Function, ULONG_PTR Argument)
{
    KIRQL OldIrql, ExecuteIrql;
    KAFFINITY Targets, Bits;
    LONG Count = 0;
    ULONG_PTR Result;

    ASSERT(Function != NULL);
    ASSERT(KeGetCurrentIrql() < IPI_LEVEL);
    OldIrql = KeGetCurrentIrql();
    /* Keep the packet owner above other interrupt handlers while allowing IPIs. */
    if (OldIrql < IPI_LEVEL - 1) KfRaiseIrql(IPI_LEVEL - 1);

    /* A competing caller may itself be a target. Service requests while
     * waiting, including when entered with hardware interrupts masked. */
    while (InterlockedCompareExchange(&KiRiscvGenericCall.Busy, 1, 0))
    {
        ExecuteIrql = KfRaiseIrql(IPI_LEVEL);
        KiIpiProcessRequests();
        KfLowerIrql(ExecuteIrql);
        YieldProcessor();
    }

    Targets = KeActiveProcessors & ~KeGetCurrentPrcb()->SetMember;
    for (Bits = Targets; Bits; Bits &= Bits - 1) ++Count;
    KiRiscvGenericCall.Function = Function;
    KiRiscvGenericCall.Argument = Argument;
    KiRiscvGenericCall.Arriving = Count;
    KiRiscvGenericCall.Remaining = Count;
    KiRiscvGenericCall.Release = 0;
    KeMemoryBarrier();
    if (Targets) KiIpiSend(Targets, IPI_SYNCH_REQUEST);

    while (KiRiscvGenericCall.Arriving)
    {
        ExecuteIrql = KfRaiseIrql(IPI_LEVEL);
        KiIpiProcessRequests();
        KfLowerIrql(ExecuteIrql);
        YieldProcessor();
        KeMemoryBarrier();
    }

    ExecuteIrql = KfRaiseIrql(IPI_LEVEL);
    /* Release before calling locally: workers may rendezvous themselves. */
    InterlockedExchange(&KiRiscvGenericCall.Release, 1);
    Result = Function(Argument);
    while (KiRiscvGenericCall.Remaining)
    {
        YieldProcessor();
        KeMemoryBarrier();
    }
    KfLowerIrql(ExecuteIrql);
    InterlockedExchange(&KiRiscvGenericCall.Busy, 0);
    KfLowerIrql(OldIrql);
    return Result;
}
