/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC processor control region and IRQL
 *
 * The IRQL is a software state in the PCR. The decrementer and the external
 * interrupt share MSR[EE], so neither is masked by raising the IRQL: a
 * decrementer interrupt taken at or above CLOCK_LEVEL is parked and replayed
 * when the IRQL drops below it, and an external interrupt taken at or above
 * the device interrupt level makes the HAL mask the controller until the IRQL
 * drops below it. APC and DPC requests are delivered synchronously when the
 * IRQL drops below their level.
 */

#include <ntoskrnl.h>

ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID)
{
    return KeGetCurrentPrcb()->Number;
}

KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    return KeGetPcr()->CurrentIrql;
}

VOID
NTAPI
KiPpcInitializePcr(
    _Out_ PKPCR Pcr,
    _In_ PKTHREAD Thread,
    _In_ ULONG Number,
    _In_ PVOID DpcStack,
    _In_ PVOID PanicStack)
{
    PKPRCB Prcb;

    /* The caller owns resident PCR and bootstrap-thread storage. */
    RtlZeroMemory(Pcr, sizeof(*Pcr));
    Pcr->ProcessorId = Number;
    Pcr->CurrentIrql = HIGH_LEVEL;
    Pcr->PanicStack = PanicStack;
    Pcr->PhysicalAddress = (ULONG_PTR)Pcr - PPC_LOADER_KSEG0_BASE;
    Prcb = &Pcr->Prcb;
    Prcb->CurrentThread = Thread;
    Prcb->IdleThread = Thread;
    Prcb->DpcStack = DpcStack;
    Prcb->Number = Number;
#if DBG
    Prcb->BuildType |= PRCB_BUILD_DEBUG;
#endif
#ifndef CONFIG_SMP
    Prcb->BuildType |= PRCB_BUILD_UNIPROCESSOR;
#endif
    Prcb->SetMember = AFFINITY_MASK(Number);
    Prcb->ParentNode = &KiNode0;
    Prcb->MultiThreadProcessorSet = Prcb->SetMember;
    Prcb->MultiThreadSetMaster = Prcb;
    KiInitSpinLocks(Prcb, Number);
}

BOOLEAN
NTAPI
KiPpcInitializeBootPcr(
    _Out_ PKPCR Pcr,
    _In_ PKTHREAD Thread,
    _In_ PVOID DpcStack,
    _In_ PVOID PanicStack)
{
    if ((Pcr == NULL) || (Thread == NULL) || (DpcStack == NULL) || ((ULONG_PTR)DpcStack & 15) || (KiPpcReadMsr() & MSR_EE) || (KeNumberProcessors != 0) || (KiProcessorBlock[0] != NULL))
        return FALSE;

    KiPpcInitializePcr(Pcr, Thread, 0, DpcStack, PanicStack);

    /* Publish the PCR: SPRG0 for kernel code, SPRG1 for the vectors. */
    __asm__ __volatile__("mtsprg 0, %0\n\tmtsprg 1, %1" :: "r"(Pcr), "r"(Pcr->PhysicalAddress) : "memory");
    KiProcessorBlock[0] = &Pcr->Prcb;
    KeActiveProcessors = 1;
    KeMemoryBarrier();
    KeNumberProcessors = 1;
    return TRUE;
}

/* Deliver whatever deferred work the current IRQL admits. Entered and left
 * with interrupts disabled. */
static
VOID
KiPpcCheckDeferred(
    _In_ PKPCR Pcr)
{
    KIRQL Irql = Pcr->CurrentIrql;

    if (Pcr->PendingClock && (Irql < CLOCK_LEVEL))
    {
        PKTHREAD Thread = Pcr->Prcb.CurrentThread;
        KTRAP_FRAME Frame;

        /* Replay the tick at CLOCK_LEVEL. Time accounting charges the
         * thread's last trap frame, or kernel time when there is none. */
        Pcr->PendingClock = FALSE;
        Pcr->CurrentIrql = CLOCK_LEVEL;
        if (Thread && Thread->TrapFrame)
        {
            Frame = *Thread->TrapFrame;
        }
        else
        {
            RtlZeroMemory(&Frame, sizeof(Frame));
            Frame.Context.Msr = PPC_KERNEL_MSR;
        }
        Frame.PreviousIrql = Irql;
        HalpPpcClockInterrupt(&Frame);
        Pcr = KeGetPcr();
        Pcr->CurrentIrql = Irql;
    }

    if (Pcr->PendingExternal && (Irql < KI_PPC_EXTERNAL_IRQL))
    {
        /* The controller asserts again once unmasked. */
        Pcr->PendingExternal = FALSE;
        HalpPpcRestoreExternal();
    }

    if (((Irql < DISPATCH_LEVEL) && (Pcr->SoftwareInterrupts & (1 << DISPATCH_LEVEL))) || ((Irql < APC_LEVEL) && (Pcr->SoftwareInterrupts & (1 << APC_LEVEL))))
        KiPpcDeliverSoftwareInterrupts();
}

KIRQL
FASTCALL
KfRaiseIrql(_In_ KIRQL NewIrql)
{
    PKPCR Pcr = KeGetPcr();
    KIRQL OldIrql = Pcr->CurrentIrql;

    if ((NewIrql < OldIrql) || (NewIrql > HIGH_LEVEL))
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, NewIrql, OldIrql, (ULONG_PTR)_ReturnAddress(), 0);

    Pcr->CurrentIrql = NewIrql;
    return OldIrql;
}

VOID
FASTCALL
KfLowerIrql(_In_ KIRQL NewIrql)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    PKPCR Pcr = KeGetPcr();
    KIRQL OldIrql = Pcr->CurrentIrql;

    if (NewIrql > OldIrql)
        KeBugCheckEx(IRQL_NOT_LESS_OR_EQUAL, NewIrql, OldIrql, (ULONG_PTR)_ReturnAddress(), 0);

    Pcr->CurrentIrql = NewIrql;
    if (Pcr->PendingClock || Pcr->PendingExternal || Pcr->SoftwareInterrupts)
        KiPpcCheckDeferred(Pcr);
    KeRestoreInterrupts(Interrupts);
}

KIRQL
NTAPI
KeRaiseIrqlToDpcLevel(VOID)
{
    return KfRaiseIrql(DISPATCH_LEVEL);
}

KIRQL
NTAPI
KeRaiseIrqlToSynchLevel(VOID)
{
    return KfRaiseIrql(SYNCH_LEVEL);
}

VOID
NTAPI
KiPpcRequestSoftwareInterrupt(_In_ KIRQL Irql)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    PKPCR Pcr = KeGetPcr();

    if ((Irql != APC_LEVEL) && (Irql != DISPATCH_LEVEL))
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, Irql, Pcr->CurrentIrql, 0, 0);

    Pcr->SoftwareInterrupts |= 1 << Irql;
    if (Pcr->CurrentIrql < Irql)
        KiPpcDeliverSoftwareInterrupts();
    KeRestoreInterrupts(Interrupts);
}

VOID
NTAPI
KiPpcClearSoftwareInterrupt(_In_ KIRQL Irql)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    PKPCR Pcr = KeGetPcr();

    if ((Irql != APC_LEVEL) && (Irql != DISPATCH_LEVEL))
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, Irql, Pcr->CurrentIrql, 0, 0);

    Pcr->SoftwareInterrupts &= ~(1 << Irql);
    KeRestoreInterrupts(Interrupts);
}

/* Deliver eligible DPC and APC requests at their levels. Entered and left
 * with interrupts disabled. A dispatch may resume this thread on another
 * processor, so the PCR is looked up again after each one. */
VOID
NTAPI
KiPpcDeliverSoftwareInterrupts(VOID)
{
    PKPCR Pcr;
    KIRQL Irql;

    for (;;)
    {
        Pcr = KeGetPcr();
        Irql = Pcr->CurrentIrql;
        if ((Irql < DISPATCH_LEVEL) && (Pcr->SoftwareInterrupts & (1 << DISPATCH_LEVEL)))
        {
            Pcr->SoftwareInterrupts &= ~(1 << DISPATCH_LEVEL);
            Pcr->CurrentIrql = DISPATCH_LEVEL;
            _enable();
            KiDispatchInterrupt();
            _disable();
            Pcr = KeGetPcr();
            Pcr->CurrentIrql = Irql;
            continue;
        }

        if ((Irql < APC_LEVEL) && (Pcr->SoftwareInterrupts & (1 << APC_LEVEL)))
        {
            PKTHREAD Thread = Pcr->Prcb.CurrentThread;

            Pcr->SoftwareInterrupts &= ~(1 << APC_LEVEL);
            Pcr->CurrentIrql = APC_LEVEL;
            _enable();
            KiDeliverApc(KernelMode, NULL, (Thread && Thread->TrapFrame && KiUserTrap(Thread->TrapFrame)) ? Thread->TrapFrame : NULL);
            _disable();
            Pcr = KeGetPcr();
            Pcr->CurrentIrql = Irql;
            continue;
        }
        break;
    }
}
