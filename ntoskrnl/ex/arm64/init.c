/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Executive initialization helpers for ARM64
 */

#include <ntoskrnl.h>

CODE_SEG("INIT")
VOID
NTAPI
ExArchPostHalInitSystemPhase0(
    VOID)
{
    /*
     * HAL phase 0 has initialized enough GIC state for direct HAL use, but
     * MM phase 0 still runs on bootstrap page tables. Keep ARM64 IRQL
     * transitions on the DAIF-only path until HAL phase 1 re-enables the
     * timer PPI after MmArmInitSystem completes.
     */
    KiArm64DiscoverNumaTopology(KeLoaderBlock);
}

CODE_SEG("INIT")
VOID
NTAPI
ExArchPostHalInitSystemPhase1(
    VOID)
{
    KIRQL CurrentIrql;

    KeReenableTimerInterrupt();

    /* HAL has moved interrupt MMIO to kernel VAs; retire its low aliases. */
    MiArm64UnmapEarlyDeviceAliases(KeLoaderBlock);

    /*
     * From this point the MM bootstrap mappings are in place and the timer
     * PPI has been re-enabled, so IRQL transitions may use HAL's GIC PMR path.
     * HAL phase 1 ran while KiHalInitialized was FALSE; its HIGH_LEVEL sections
     * therefore lowered through the DAIF-only bootstrap path and can leave
     * DAIF.I set. Open the normal interrupt window explicitly here.
     */
    CurrentIrql = KeGetCurrentIrql();
    KiHalInitialized = TRUE;
    HalSetGicPriorityMask(CurrentIrql);
    __asm__ __volatile__("dsb sy" ::: "memory");

    if (CurrentIrql < HIGH_LEVEL)
    {
        __asm__ __volatile__("msr daifclr, #0xb\n\tisb" ::: "memory");
    }
    else
    {
        __asm__ __volatile__("isb" ::: "memory");
    }
}
