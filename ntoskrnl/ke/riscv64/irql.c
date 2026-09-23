/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V local interrupt priority and software requests
 */

#include <ntoskrnl.h>

static
VOID
KiRiscvUpdateInterruptMask(_In_ PKPCR Pcr)
{
    ULONG_PTR Mask = Pcr->InterruptEnable;
    KIRQL Irql = Pcr->CurrentIrql;

    /* The initial HAL delivers every external source at SYNCH_LEVEL. It may
     * enable a class only after installing its routing and acknowledgement. */
    if (Irql >= SYNCH_LEVEL) Mask &= ~RISCV_SIE_SEIE;
    if (Irql >= CLOCK_LEVEL) Mask &= ~RISCV_SIE_STIE;

    if (Irql >= IPI_LEVEL) Mask &= ~RISCV_SIE_SSIE;

    /* APC, DPC and remote IPIs share SSIP. Keep the remote doorbell enabled
     * below IPI_LEVEL, but only raise it locally for an eligible APC/DPC. */
    if (((Irql < DISPATCH_LEVEL) && (Pcr->SoftwareInterrupts & (1 << DISPATCH_LEVEL))) ||
        ((Irql < APC_LEVEL) && (Pcr->SoftwareInterrupts & (1 << APC_LEVEL))))
    {
        Mask |= RISCV_SIE_SSIE;
        __asm__ __volatile__("csrsi sip, 2" ::: "memory");
    }

    __asm__ __volatile__("csrw sie, %0" :: "r"(Mask) : "memory");
    /* Only the interrupt handler acknowledges SSIP. Clearing it here could
     * lose a remote hart's doorbell racing with a local IRQL change. */
}

KIRQL
FASTCALL
KfRaiseIrql(_In_ KIRQL NewIrql)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    PKPCR Pcr = KeGetPcr();
    KIRQL OldIrql = Pcr->CurrentIrql;

    if ((NewIrql < OldIrql) || (NewIrql > HIGH_LEVEL))
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, NewIrql, OldIrql, (ULONG_PTR)_ReturnAddress(), 0);

    Pcr->CurrentIrql = NewIrql;
    KiRiscvUpdateInterruptMask(Pcr);
    KeRestoreInterrupts(Interrupts);
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
    KiRiscvUpdateInterruptMask(Pcr);
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

/* Internal entry points behind the exported IRQL routines, as on the other
 * 64-bit architectures. */
NTKERNELAPI
VOID
KxLowerIrql(_In_ KIRQL NewIrql)
{
    KfLowerIrql(NewIrql);
}

NTKERNELAPI
KIRQL
KxRaiseIrql(_In_ KIRQL NewIrql)
{
    return KfRaiseIrql(NewIrql);
}

NTKERNELAPI
KIRQL
KxRaiseIrqlToDpcLevel(VOID)
{
    return KeRaiseIrqlToDpcLevel();
}

KIRQL
NTAPI
KeGetEffectiveIrql(VOID)
{
    ULONG_PTR Status;

    /* Nothing can preempt code running with interrupts masked. */
    __asm__ __volatile__("csrr %0, sstatus" : "=r"(Status));
    if (!(Status & RISCV_SSTATUS_SIE))
        return HIGH_LEVEL;

    return KeGetCurrentIrql();
}

VOID
NTAPI
KiRiscvSetInterruptEnabled(_In_ ULONG_PTR Mask, _In_ BOOLEAN Enable)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    PKPCR Pcr = KeGetPcr();

    /* SSIE is also the SBI interprocessor doorbell. */
    if (Mask & ~(RISCV_SIE_STIE | RISCV_SIE_SEIE | RISCV_SIE_SSIE))
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, Mask, Enable, 0, 0);

    if (Enable)
        Pcr->InterruptEnable |= Mask;
    else
        Pcr->InterruptEnable &= ~Mask;

    KiRiscvUpdateInterruptMask(Pcr);
    KeRestoreInterrupts(Interrupts);
}

VOID
NTAPI
KiRiscvRequestSoftwareInterrupt(_In_ KIRQL Irql)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    PKPCR Pcr = KeGetPcr();

    if ((Irql != APC_LEVEL) && (Irql != DISPATCH_LEVEL))
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, Irql, Pcr->CurrentIrql, 0, 0);

    Pcr->SoftwareInterrupts |= 1 << Irql;
    KiRiscvUpdateInterruptMask(Pcr);
    KeRestoreInterrupts(Interrupts);
}

VOID
NTAPI
KiRiscvClearSoftwareInterrupt(_In_ KIRQL Irql)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    PKPCR Pcr = KeGetPcr();

    if ((Irql != APC_LEVEL) && (Irql != DISPATCH_LEVEL))
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, Irql, Pcr->CurrentIrql, 0, 0);

    Pcr->SoftwareInterrupts &= ~(1 << Irql);
    KiRiscvUpdateInterruptMask(Pcr);
    KeRestoreInterrupts(Interrupts);
}

/* The HAL forwards its KeRaiseIrql export here, as on ARM64. */
#undef KeRaiseIrql
VOID
NTAPI
KeRaiseIrql(_In_ KIRQL NewIrql, _Out_ PKIRQL OldIrql)
{
    *OldIrql = KfRaiseIrql(NewIrql);
}
