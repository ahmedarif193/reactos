/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Executive initialization helpers for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

CODE_SEG("INIT")
VOID
NTAPI
ExArchExecutiveInitBarrier(
    VOID)
{
    /* Ensure early bring-up state is globally visible before phase-0 init continues. */
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

CODE_SEG("INIT")
VOID
NTAPI
ExArchCopyNlsSection(
    _Out_writes_bytes_(Size) PVOID Destination,
    _In_reads_bytes_(Size) CONST VOID *Source,
    _In_ SIZE_T Size)
{
    SIZE_T Offset;

    /*
     * Pre-fault destination pages with scalar accesses before memmove.
     * This avoids losing SIMD register state if a demand fault occurs mid-copy.
     */
    for (Offset = 0; Offset < Size; Offset += PAGE_SIZE)
    {
        volatile PUCHAR PagePtr = (volatile PUCHAR)((ULONG_PTR)Destination + Offset);

        *PagePtr = 0;
        __dsb(_ARM64_BARRIER_SY);
    }

    memmove(Destination, Source, Size);
    __dsb(_ARM64_BARRIER_SY);
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExArchKeepPoolBackedNls(
    _In_opt_ PVOID SectionBase)
{
    if (SectionBase != NULL)
    {
        NTSTATUS Status = MmUnmapViewInSystemSpace(SectionBase);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed to unmap ARM64 NLS system view: 0x%08lx\n", Status);
        }
    }

    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExArchShouldMapSystemProcessNlsView(
    VOID)
{
    return FALSE;
}

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
