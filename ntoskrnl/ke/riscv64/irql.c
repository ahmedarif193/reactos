/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
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

    /* APC and DPC share SSIP. Enable it only for an eligible request, or a
     * masked APC alone would continually retrap while running at APC_LEVEL. */
    if (((Irql < DISPATCH_LEVEL) && (Pcr->SoftwareInterrupts & (1 << DISPATCH_LEVEL))) ||
        ((Irql < APC_LEVEL) && (Pcr->SoftwareInterrupts & (1 << APC_LEVEL))))
    {
        Mask |= RISCV_SIE_SSIE;
    }

    __asm__ __volatile__("csrw sie, %0" :: "r"(Mask) : "memory");
    if (Pcr->SoftwareInterrupts)
        __asm__ __volatile__("csrsi sip, 2" ::: "memory");
    else
        __asm__ __volatile__("csrci sip, 2" ::: "memory");
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

VOID
NTAPI
KiRiscvSetInterruptEnabled(_In_ ULONG_PTR Mask, _In_ BOOLEAN Enable)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    PKPCR Pcr = KeGetPcr();

    /* Software requests are kernel-owned; there is no SMP/IPI provider yet. */
    if (Mask & ~(RISCV_SIE_STIE | RISCV_SIE_SEIE))
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
