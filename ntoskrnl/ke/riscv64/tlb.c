/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
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
KeFlushProcessTb(VOID)
{
    /* The current process can run elsewhere once SMP is implemented. */
    if (KeNumberProcessors > 1)
        KeBugCheckEx(MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED, KeNumberProcessors, 0, 0, 0);

    /* Flush more than the current ASID; no ASID allocation policy is implied. */
    KeFlushCurrentTb();
}

VOID
NTAPI
KeFlushEntireTb(_In_ BOOLEAN Invalid, _In_ BOOLEAN AllProcessors)
{
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Invalid);

    if (AllProcessors && (KeNumberProcessors > 1))
        KeBugCheckEx(MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED, KeNumberProcessors, 0, 0, 0);

    OldIrql = KeRaiseIrqlToSynchLevel();
    KeFlushCurrentTb();
    KeLowerIrql(OldIrql);
}
