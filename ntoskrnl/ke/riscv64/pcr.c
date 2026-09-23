/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V supervisor per-processor state
 */

#include <ntoskrnl.h>

static DECLSPEC_ALIGN(16) UCHAR KiRiscvPanicStack[KERNEL_STACK_SIZE];

PKPCR
NTAPI
KeGetPcr(VOID)
{
    PKPCR Pcr;

    __asm__ __volatile__("csrr %0, sscratch" : "=r"(Pcr) :: "memory");
    return Pcr;
}

PKPRCB
NTAPI
KeGetCurrentPrcb(VOID)
{
    return &KeGetPcr()->Prcb;
}

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
    BOOLEAN Enabled = KeDisableInterrupts();
    KIRQL Irql = KeGetPcr()->CurrentIrql;

    KeRestoreInterrupts(Enabled);
    return Irql;
}

BOOLEAN
NTAPI
KiRiscvInitializeBootPcr(
    _Out_ PKPCR Pcr,
    _In_ PKTHREAD Thread,
    _In_ ULONG_PTR HartId,
    _In_ PVOID DpcStack)
{
    ULONG_PTR Status;
    PKPRCB Prcb;

    /* Only the selected boot hart may enter here. Other harts remain parked.
     * Failure must not need an already initialized PCR or bugcheck path. */
    __asm__ __volatile__("csrr %0, sstatus" : "=r"(Status) :: "memory");
    if ((Pcr == NULL) || (Thread == NULL) || (DpcStack == NULL) ||
        ((ULONG_PTR)DpcStack & 15) || (Status & RISCV_SSTATUS_SIE) ||
        (KeNumberProcessors != 0) || (KiProcessorBlock[0] != NULL))
    {
        return FALSE;
    }

    KiRiscvInitializePcr(Pcr, Thread, HartId, 0, DpcStack,
                         KiRiscvPanicStack + sizeof(KiRiscvPanicStack));
    Prcb = &Pcr->Prcb;

    /* Publish only initialized queues and processor identity. sscratch is
     * reserved from here onward; trap entry must restore it before C code. */
    __asm__ __volatile__("csrw sie, zero\n\tcsrw sscratch, %0" :: "r"(Pcr) : "memory");
    KiProcessorBlock[0] = Prcb;
    KeActiveProcessors = 1;
    KeMemoryBarrier();
    KeNumberProcessors = 1;
    return TRUE;
}

VOID NTAPI
KiRiscvInitializePcr(PKPCR Pcr, PKTHREAD Thread, ULONG_PTR HartId,
                     ULONG Number, PVOID DpcStack, PVOID PanicStack)
{
    PKPRCB Prcb;
    /* The caller owns resident PCR and bootstrap-thread storage. This does
     * not replace KeInitializeThread or initialize the idle process. */
    RtlZeroMemory(Pcr, sizeof(*Pcr));
    Pcr->HartId = HartId;
    Pcr->CurrentIrql = HIGH_LEVEL;
    Pcr->PanicStack = PanicStack;
    Prcb = &Pcr->Prcb;
    Prcb->CurrentThread = Thread;
    Prcb->IdleThread = Thread;
    Prcb->DpcStack = DpcStack;
    Prcb->Number = Number;
    Prcb->SetMember = AFFINITY_MASK(Number);
    Prcb->ParentNode = &KiNode0;
    Prcb->MultiThreadProcessorSet = Prcb->SetMember;
    Prcb->MultiThreadSetMaster = Prcb;
    KiInitSpinLocks(Prcb, Number);

    Pcr->KdVersionBlock = Number ? KeGetPcr()->KdVersionBlock : NULL;
}
