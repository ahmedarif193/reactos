/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V translation-cache invalidation
 */

#include <ntoskrnl.h>

VOID
NTAPI
KeFlushCurrentTb(VOID)
{
    /* All local leaf and non-leaf translations, including global mappings.
     * This does not change satp or synchronize another hart. */
    __asm__ __volatile__("sfence.vma zero, zero" ::: "memory");
}

VOID
NTAPI
KiIpiSendTbFlush(KAFFINITY Targets, PVOID Address, ULONG Pages)
{
    KIRQL OldIrql = KeGetCurrentIrql();
    KAFFINITY Self;
    ULONG Index;
    if (OldIrql < SYNCH_LEVEL) KfRaiseIrql(SYNCH_LEVEL);
    Self = KeGetCurrentPrcb()->SetMember;
    Targets &= KeActiveProcessors;
    if (Targets & Self)
    {
        if (!Pages || Pages > FLUSH_MULTIPLE_MAXIMUM)
            KeFlushCurrentTb();
        else
            for (Index = 0; Index < Pages; ++Index)
                KeInvalidateTlbEntry((PUCHAR)Address + (SIZE_T)Index * PAGE_SIZE);
    }
    Targets &= ~Self;
    if (Targets)
        HalpRiscvRemoteFence(Targets, Pages ? Address : NULL,
                            Pages ? (SIZE_T)Pages * PAGE_SIZE : 0, FALSE);
    KfLowerIrql(OldIrql);
}

VOID NTAPI KeFlushProcessTb(VOID)
{
    /* ASID zero: every context switch flushes before entering another root.
     * Broadcasting also covers a hart switching into this process now. */
    KiIpiSendTbFlush(KeActiveProcessors, NULL, 0);
}

VOID NTAPI KeFlushEntireTb(BOOLEAN Invalid, BOOLEAN AllProcessors)
{
    UNREFERENCED_PARAMETER(Invalid);
    if (AllProcessors)
        KiIpiSendTbFlush(KeActiveProcessors, NULL, 0);
    else
        KeFlushCurrentTb();
}
